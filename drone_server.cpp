#include <fstream>
#include <unordered_map>
#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <iomanip>

#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace std::chrono_literals;

constexpr double CORRUPTION_RATE = 0.1;
constexpr int DEFAULT_PORT = 5000;

// Simulates network corruption by randomly modifying buffer bytes
void corruptBuffer(std::vector<char>& buffer)
{
    int i{0};
    if (buffer.empty())
    {
        return;
    }

    static thread_local std::mt19937 gen{std::random_device{}()};
    static std::uniform_real_distribution<> prob(0.0, 1.0);
    static std::uniform_int_distribution<> byteDist(0, 255);

    // Apply corruption to each byte with CORRUPTION_RATE probability
    for (auto& c : buffer)
    {
        if (prob(gen) > CORRUPTION_RATE)
        {
            continue;
        }
        c = static_cast<char>(byteDist(gen));
    }
}

// Sends data with simulated corruption
ssize_t corruptAndSend(int sock, const void* buf, size_t len, int flags)
{
    if (!buf || len == 0)
    {
        return 0;
    }

    std::vector<char> temp(
        static_cast<const char*>(buf),
        static_cast<const char*>(buf) + len
    );

    corruptBuffer(temp);
    return ::send(sock, temp.data(), temp.size(), flags);
}

// Receives data and applies simulated corruption
ssize_t corruptAndReceive(int sock, void* buf, size_t len, int flags)
{
    if (!buf || len == 0)
    {
        return 0;
    }

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

// Stores drone state information
struct Telemetry
{
    int battery{100};
    int x{0};
    int y{0};
    int z{0};
    int speed{50};
    int orientation{0};

    // Formats telemetry as string for transmission
    std::string toString() const
    {
        return "BAT:" + std::to_string(battery) +
               "% POS:(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ")" +
               " SPD:" + std::to_string(speed) +
               " ORI:" + std::to_string(orientation) + "\n";
    }
};

enum class DroneCommands
{
    TAKEOFF,
    LAND,
    STOP,
    GOTO,
    UNKNOWN
};

// Handles incoming drone commands
class CommandHandler
{
private:
    static const std::unordered_map<std::string, DroneCommands> CommandMap;

    // Extracts command type from message
    static DroneCommands parseCommand(const std::string& msg)
    {
        auto pos = msg.find(' ');
        std::string cmd = msg.substr(0, pos);
        auto it = CommandMap.find(cmd);
        return (it != CommandMap.end()) ? it->second : DroneCommands::UNKNOWN;
    }

public:
    // Processes command and updates telemetry
    static void handle(const std::string& msg, Telemetry& telemetry, std::mutex& mtx)
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
            float xf{0.0f};
            float yf{0.0f};
            float zf{0.0f};
            if (std::sscanf(msg.c_str(), "GOTO %f %f %f", &xf, &yf, &zf) == 3)
            {
                std::lock_guard<std::mutex> lock(mtx);
                telemetry.x = static_cast<int>(xf);
                telemetry.y = static_cast<int>(yf);
                telemetry.z = static_cast<int>(zf);
                std::cout << "Navigating to (" << xf << ", " << yf << ", " << zf << ")\n";
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
};

const std::unordered_map<std::string, DroneCommands> CommandHandler::CommandMap = {
    {"TAKEOFF", DroneCommands::TAKEOFF},
    {"LAND",    DroneCommands::LAND},
    {"STOP",    DroneCommands::STOP},
    {"GOTO",    DroneCommands::GOTO},
};

// Manages drone communication server
class DroneNode
{
private:
    int port;
    int serverSock{-1};
    int clientSock{-1};

    std::atomic<bool> running{false};
    Telemetry telemetry;
    std::mutex telemetryMutex;

    // Appends timestamped data to log file
    void saveLog(const std::string& filename, const std::string& data)
    {
        std::ofstream file(filename, std::ios::app);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open log file: " + filename);
        }

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&now_c);

        file << std::put_time(tm, "%H:%M:%S ") << data;
    }

    // Continuously sends telemetry to client
    void telemetryLoop()
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

    // Receives and processes commands from client
    void commandLoop()
    {
        std::string rxBuffer;
        char temp[256];

        while (running.load())
        {
            ssize_t n = corruptAndReceive(clientSock, temp, sizeof(temp), 0);
            if (n <= 0)
            {
                std::cout << "Client disconnected.\n";
                running = false;
                break;
            }

            rxBuffer.append(temp, n);
            std::transform(rxBuffer.begin(), rxBuffer.end(), rxBuffer.begin(), ::toupper);

            std::size_t pos{0};
            // Process complete lines (delimited by newline)
            while ((pos = rxBuffer.find('\n')) != std::string::npos)
            {
                std::string line = rxBuffer.substr(0, pos);
                rxBuffer.erase(0, pos + 1);

                std::cout << "Received: " << line << '\n';
                CommandHandler::handle(line, telemetry, telemetryMutex);

                std::string ack = "OK " + line + "\n";
                corruptAndSend(clientSock, ack.data(), ack.size(), 0);
                saveLog("telemetry_log.csv", line + "\n");
            }
        }
    }

public:
    explicit DroneNode(int p = DEFAULT_PORT) : port(p) {}

    ~DroneNode()
    {
        if (clientSock >= 0)
        {
            ::close(clientSock);
        }
        if (serverSock >= 0)
        {
            ::close(serverSock);
        }
    }

    // Starts the drone server and handles client connections
    void start()
    {
        serverSock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (serverSock < 0)
        {
            throw std::runtime_error("Socket creation failed");
        }

        int opt{1};
        ::setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (::bind(serverSock, (sockaddr*)&addr, sizeof(addr)) < 0)
        {
            throw std::runtime_error("Bind failed");
        }

        if (::listen(serverSock, 1) < 0)
        {
            throw std::runtime_error("Listen failed");
        }

        std::cout << "Drone is listening on port " << port << "...\n";

        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);

        clientSock = ::accept(serverSock, (sockaddr*)&clientAddr, &len);
        if (clientSock < 0)
        {
            throw std::runtime_error("Accept failed");
        }

        std::cout << "Client connected: "
                  << ::inet_ntoa(clientAddr.sin_addr) << '\n';

        running = true;

        // Start telemetry and command processing threads
        std::thread t_telemetry(&DroneNode::telemetryLoop, this);
        std::thread t_command_loop(&DroneNode::commandLoop, this);

        t_command_loop.join();
        running = false;
        t_telemetry.join();
    }
};

int main()
{
    try
    {
        DroneNode drone;
        drone.start();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}