#include "drone_server.h"

#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>
#include <algorithm>
#include <iomanip>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace std::chrono_literals;

// Corruption Simulation
void corruptBuffer(std::vector<char>& buffer)
{
    if (buffer.empty()) return;

    for (auto& b : buffer)
    {
        if ((rand() % 100) > 90)
        {
            b = (rand() % 0xFF);
        }
    }
}

ssize_t corruptAndSend(int sock, const void* buf, size_t len, int flags)
{
    if (!buf || len == 0) return 0;

    std::vector<char> temp(
        static_cast<const char*>(buf),
        static_cast<const char*>(buf) + len
    );

    corruptBuffer(temp);
    return ::send(sock, temp.data(), temp.size(), flags);
}

ssize_t corruptAndReceive(int sock, void* buf, size_t len, int flags)
{
    if (!buf || len == 0) return 0;

    char* cbuf = static_cast<char*>(buf);
    ssize_t n = ::recv(sock, cbuf, len, flags);

    if (n > 0)
    {
        std::vector<char> temp(cbuf, cbuf + n);
        corruptBuffer(temp);
        std::memcpy(cbuf, temp.data(), n);
    }

    return n;
}

// Telemetry handling
std::string Telemetry::toString() const
{
    return "BAT:" + std::to_string(battery) +
           "% POS:(" + std::to_string(x) + "," +
           std::to_string(y) + "," +
           std::to_string(z) + ")" +
           " SPD:" + std::to_string(speed) +
           " ORI:" + std::to_string(orientation) + "\n";
}

// Command Handler
const std::unordered_map<std::string, DroneCommands>
CommandHandler::CommandMap = {
    {"TAKEOFF", DroneCommands::TAKEOFF},
    {"LAND",    DroneCommands::LAND},
    {"STOP",    DroneCommands::STOP},
    {"GOTO",    DroneCommands::GOTO},
};

DroneCommands CommandHandler::parseCommand(const std::string& msg)
{
    auto pos = msg.find(' ');
    std::string cmd = msg.substr(0, pos);

    auto it = CommandMap.find(cmd);
    return (it != CommandMap.end()) ? it->second
                                    : DroneCommands::UNKNOWN;
}

void CommandHandler::handle(const std::string& msg,
                            Telemetry& telemetry,
                            std::mutex& mtx)
{
    switch (parseCommand(msg))
    {
    case DroneCommands::TAKEOFF:
        std::cout << "Drone taking off...\n";
        break;

    case DroneCommands::LAND:
        std::cout << "Drone landing...\n";
        break;

    case DroneCommands::STOP:
        std::cout << "Emergency stop engaged!\n";
        break;

    case DroneCommands::GOTO:
    {
        float xf{0}, yf{0}, zf{0};

        if (std::sscanf(msg.c_str(),
                        "GOTO %f %f %f",
                        &xf, &yf, &zf) == 3)
        {
            std::lock_guard<std::mutex> lock(mtx);
            telemetry.x = static_cast<int>(xf);
            telemetry.y = static_cast<int>(yf);
            telemetry.z = static_cast<int>(zf);

            std::cout << "Navigating to ("
                      << xf << ", "
                      << yf << ", "
                      << zf << ")\n";
        }
        else
        {
            std::cout << "Invalid GOTO format\n";
        }
        break;
    }

    default:
        std::cout << "Unknown command\n";
        break;
    }
}

// DroneNode
DroneNode::DroneNode(int p)
    : port(p),
      serverSock(-1),
      clientSock(-1),
      running(false)
{
}

DroneNode::~DroneNode()
{
    if (clientSock >= 0)
        ::close(clientSock);

    if (serverSock >= 0)
        ::close(serverSock);
}

void DroneNode::saveLog(const std::string& filename,
                        const std::string& data)
{
    std::ofstream file(filename, std::ios::app);
    if (!file.is_open())
        throw std::runtime_error("Failed to open log file");

    auto now = std::chrono::system_clock::now();
    std::time_t now_c =
        std::chrono::system_clock::to_time_t(now);

    std::tm* tm = std::localtime(&now_c);

    file << std::put_time(tm, "%H:%M:%S ")
         << data;
}

void DroneNode::telemetryLoop()
{
    while (running.load())
    {
        std::string packet;
        {
            std::lock_guard<std::mutex> lock(telemetryMutex);
            packet = telemetry.toString();
        }

        corruptAndSend(clientSock, packet.data(), packet.size(), 0);

        saveLog("telemetry_log.csv", packet);

        std::this_thread::sleep_for(100ms);
    }
}

void DroneNode::commandLoop()
{
    std::string rxBuffer;
    char temp[256];

    while (running.load())
    {
        ssize_t n =
            corruptAndReceive(clientSock, temp, sizeof(temp), 0);

        if (n <= 0)
        {
            std::cout << "Client disconnected.\n";
            running = false;
            break;
        }

        rxBuffer.append(temp, n);
        std::transform(rxBuffer.begin(), rxBuffer.end(), rxBuffer.begin(), ::toupper);

        std::size_t pos;

        while ((pos = rxBuffer.find('\n')) != std::string::npos)
        {
            std::string line =
                rxBuffer.substr(0, pos);

            rxBuffer.erase(0, pos + 1);

            std::cout << "Received: "
                      << line << '\n';

            CommandHandler::handle(line, telemetry, telemetryMutex);

            std::string ack = "OK " + line + "\n";

            corruptAndSend(clientSock, ack.data(), ack.size(), 0);

            saveLog("telemetry_log.csv", line + "\n");
        }
    }
}

void DroneNode::start()
{
    serverSock = ::socket(AF_INET,
                          SOCK_STREAM,
                          0);

    if (serverSock < 0)
        throw std::runtime_error(
            "Socket creation failed");

    int opt{1};
    ::setsockopt(serverSock,
                 SOL_SOCKET,
                 SO_REUSEADDR,
                 &opt,
                 sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(serverSock,
               (sockaddr*)&addr,
               sizeof(addr)) < 0)
        throw std::runtime_error("Bind failed");

    if (::listen(serverSock, 1) < 0)
        throw std::runtime_error("Listen failed");

    std::cout << "Drone listening on port "
              << port << "...\n";

    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);

    clientSock = ::accept(serverSock,
                          (sockaddr*)&clientAddr,
                          &len);

    if (clientSock < 0)
        throw std::runtime_error("Accept failed");

    std::cout << "Client connected: " << ::inet_ntoa(clientAddr.sin_addr) << '\n';

    running = true;

    std::thread t1(&DroneNode::telemetryLoop,
                   this);

    std::thread t2(&DroneNode::commandLoop,
                   this);

    t2.join();
    running = false;
    t1.join();
}
