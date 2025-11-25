cmake_minimum_required(VERSION 3.16)
project(OpenHDSysUtils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(openhd_sys_utils src/openhd_sys_utils.cpp)

target_include_directories(openhd_sys_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

install(TARGETS openhd_sys_utils DESTINATION bin)
