cmake_minimum_required(VERSION 3.16)
project(OpenHDSysUtils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SOURCES
    src/main.cpp
    src/ui/banner.cpp
    src/utils/filesystem_utils.cpp
    src/utils/process.cpp
    src/utils/string_utils.cpp
    src/commands/emmc.cpp
    src/commands/init.cpp
    src/commands/resize.cpp
    src/commands/update.cpp
)

add_executable(openhd_sys_utils ${SOURCES})

target_include_directories(openhd_sys_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

install(TARGETS openhd_sys_utils DESTINATION bin)
