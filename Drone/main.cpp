#include "drone_server.h"
#include <iostream>

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
