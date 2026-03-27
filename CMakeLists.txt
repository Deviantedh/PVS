cmake_minimum_required(VERSION 3.20)

project(pvs_simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(PVS_BUILD_TESTS "Build simulator tests" ON)

if(PVS_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(sim)
