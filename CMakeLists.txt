if((CMAKE_VERSION VERSION_GREATER 3.12 OR CMAKE_VERSION VERSION_EQUAL 3.12))
    cmake_minimum_required(VERSION 3.12...3.31)
else()
    cmake_minimum_required(VERSION 3.5)
endif()
project(test_eigen3_find_package VERSION 0.4.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 11)

find_package(Eigen3 3.3 REQUIRED NO_MODULE)
if(NOT Eigen3_FOUND)
    find_package(Eigen3 3.3...5 REQUIRED NO_MODULE)
endif()

