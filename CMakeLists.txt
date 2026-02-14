cmake_minimum_required(VERSION 3.10)
project(DroneCommServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)

add_executable(drone_server Drone/drone_server.cpp Drone/main.cpp)
add_executable(ground_base GroundBase/ground_base.cpp GroundBase/main.cpp)

target_link_libraries(drone_server pthread)
target_link_libraries(ground_base pthread)
