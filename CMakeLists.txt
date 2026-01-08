cmake_minimum_required(VERSION 3.10)
project(openhd_sys_utils LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_program(PYTHON_EXECUTABLE NAMES python3 python)
if(NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR "python3 or python is required to generate platform definitions")
endif()

set(PLATFORMS_JSON ${CMAKE_CURRENT_SOURCE_DIR}/misc/platforms.json)
set(GENERATED_PLATFORMS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/platforms_generated.h)

add_custom_command(
    OUTPUT ${GENERATED_PLATFORMS_HEADER}
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_platforms.py
            --input ${PLATFORMS_JSON}
            --output ${GENERATED_PLATFORMS_HEADER}
    DEPENDS ${PLATFORMS_JSON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_platforms.py
    COMMENT "Generating platform definitions from JSON"
    VERBATIM
)

add_custom_target(generate_platforms DEPENDS ${GENERATED_PLATFORMS_HEADER})

add_executable(openhd_sys_utils
    src/openhd_sys_utils.cpp
    src/sysutil_debug.cpp
    src/sysutil_firstboot.cpp
    src/sysutil_config.cpp
    src/sysutil_hostname.cpp
    src/sysutil_led.cpp
    src/sysutil_protocol.cpp
    src/sysutil_platform.cpp
    src/sysutil_settings.cpp
    src/sysutil_status.cpp
    src/sysutil_part.cpp
    ${GENERATED_PLATFORMS_HEADER}
)

add_dependencies(openhd_sys_utils generate_platforms)

target_include_directories(openhd_sys_utils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}
)

install(TARGETS openhd_sys_utils
    RUNTIME DESTINATION bin
)
