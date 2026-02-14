#include "ground_base.h"
#include <iostream>
#include <chrono>
#include <thread>

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " <IP> <PORT>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    int port = std::stoi(argv[2]);

    try
    {
        GroundBase client;
        client.connectTo(ip, port);

        while (true)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}
