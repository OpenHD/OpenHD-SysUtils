cmake_minimum_required(VERSION 3.10)
project(openhd_sys_utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(openhd_sys_utils
    src/openhd_sys_utils.cpp
)

target_include_directories(openhd_sys_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
