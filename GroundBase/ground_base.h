#pragma once

#include <string>
#include <atomic>
#include <mutex>

class GroundBase
{
    int sock{-1};
    std::atomic<bool> running{false};
    std::atomic<bool> userTyping{false};
    std::mutex ackMutex;
    std::string lastAck;
    std::atomic<bool> ackReceived{false};

public:
    ~GroundBase();

    void connectTo(const std::string& ip, int port);

private:
    std::string readLine();
    void receiveLoop();
    void inputLoop();
};

