#include "ground_base.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <termios.h>

using namespace std::chrono_literals;

GroundBase::~GroundBase()
{
    if (sock >= 0) ::close(sock);
}

// Establishes TCP connection to drone server
void GroundBase::connectTo(const std::string& ip, int port)
{
    sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) throw std::runtime_error("Socket creation failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) throw std::runtime_error("Connection failed");

    std::cout << "Connected to drone server\n";
    running = true;

    // Start background threads for receiving and sending
    std::thread(&GroundBase::receiveLoop, this).detach();
    std::thread(&GroundBase::inputLoop, this).detach();
}

// Reads user input character by character in non-canonical mode
std::string GroundBase::readLine()
{
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON); // Disable terminal canonical mode to get user input
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::string input;
    char c;

    while (true)
    {
        if (::read(STDIN_FILENO, &c, 1) <= 0) continue;

        if (input.empty()) userTyping = true;

        if (c == '\n') break;

        input += c;
    }

    userTyping = false;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore terminal settings
    std::cout << std::endl;

    return input;
}

// Continuously receives and processes messages from server
void GroundBase::receiveLoop()
{
    char cbuf[512];
    std::string rxBuffer;

    while (running.load())
    {
        ssize_t n = ::recv(sock, cbuf, sizeof(cbuf), 0);

        if (n <= 0)
        {
            std::cout << "Disconnected from server\n";
            running = false;
            break;
        }

        rxBuffer.append(cbuf, n);

        size_t pos;
        // Extract complete lines from buffer
        while ((pos = rxBuffer.find('\n')) != std::string::npos)
        {
            std::string line = rxBuffer.substr(0, pos);
            rxBuffer.erase(0, pos + 1);

            // Handle acknowledgment messages
            if (line.rfind("OK ", 0) == 0)
            {
                std::lock_guard<std::mutex> lock(ackMutex);
                lastAck = line;
                ackReceived = true;
            }
            else
            {
                // Display telemetry only when user is not typing
                if (!userTyping.load()) std::cout << line << std::endl;
            }
        }
    }
}

// Reads user commands and sends them with retry logic
void GroundBase::inputLoop()
{
    while (running.load())
    {
        std::string cmd = readLine();

        if (!running.load()) break;

        if (cmd.empty()) continue;

        std::string packet = cmd + "\n";

        bool success = false;
        ackReceived = false;

        // Retry up to 5 times with 1 second timeout
        for (int attempt = 1; attempt <= 5; ++attempt)
        {
            ::send(sock, packet.data(), packet.size(), 0);

            auto start = std::chrono::steady_clock::now();

            // Wait for acknowledgment
            while (std::chrono::steady_clock::now() - start < 1s)
            {
                if (ackReceived.load())
                {
                    std::lock_guard<std::mutex> lock(ackMutex);

                    if (lastAck.rfind("OK ", 0) != 0)
                    {
                        std::cout << "OK\n";
                        success = true;
                    }

                    ackReceived = false;
                    break;
                }

                std::this_thread::sleep_for(50ms);
            }

            if (success) break;

            if (attempt < 5) std::cout << "retrying (" << attempt << "/5)...\n";
        }

        if (!success) std::cout << "TIMEOUT\n";
    }
}