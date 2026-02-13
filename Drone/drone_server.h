#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>

// Constants
constexpr double CORRUPTION_RATE = 0.1;
constexpr int DEFAULT_PORT = 5000;

// Network corruption helpers
void corruptBuffer(std::vector<char>& buffer);
ssize_t corruptAndSend(int sock, const void* buf, size_t len, int flags);
ssize_t corruptAndReceive(int sock, void* buf, size_t len, int flags);

// Telemetry structure
struct Telemetry
{
    int battery{100};
    int x{0};
    int y{0};
    int z{0};
    int speed{50};
    int orientation{0};

    std::string toString() const;
};

enum class DroneCommands
{
    TAKEOFF,
    LAND,
    STOP,
    GOTO,
    UNKNOWN
};

// Command handler
class CommandHandler
{
private:
    static const std::unordered_map<std::string, DroneCommands> CommandMap;
    static DroneCommands parseCommand(const std::string& msg);

public:
    static void handle(const std::string& msg,
                       Telemetry& telemetry,
                       std::mutex& mtx);
};

// Drone server
class DroneNode
{
private:
    int port;
    int serverSock;
    int clientSock;

    std::atomic<bool> running;
    Telemetry telemetry;
    std::mutex telemetryMutex;

    void saveLog(const std::string& filename,
                 const std::string& data);

    void telemetryLoop();
    void commandLoop();

public:
    explicit DroneNode(int p = DEFAULT_PORT);
    ~DroneNode();

    void start();
};
