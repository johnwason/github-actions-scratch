cmake_minimum_required(VERSION 3.16)
project(drekar_launch_process_cpp_test LANGUAGES CXX)

find_package(drekar-launch-process-cpp REQUIRED)

add_executable(drekar_test main.cpp)
target_link_libraries(drekar_test drekar-launch-process-cpp)
