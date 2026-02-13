cmake_minimum_required(VERSION 3.10)
project(openhd_sys_utils VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(PLATFORMS_JSON ${CMAKE_CURRENT_SOURCE_DIR}/misc/platforms.json)
set(GENERATED_PLATFORMS_HEADER ${CMAKE_CURRENT_BINARY_DIR}/platforms_generated.h)

# Determine host compiler
if(CMAKE_CROSSCOMPILING)
    # If cross-compiling, assume 'c++' is the host compiler if not specified via HOST_CXX.
    # Users can set HOST_CXX to point to their native compiler.
    if(DEFINED ENV{HOST_CXX})
        set(HOST_CXX_COMPILER $ENV{HOST_CXX})
    else()
        set(HOST_CXX_COMPILER "c++")
    endif()
else()
    # If native build, use the same compiler.
    set(HOST_CXX_COMPILER ${CMAKE_CXX_COMPILER})
endif()

# Handle extension
if(CMAKE_HOST_WIN32)
    set(HOST_EXE_SUFFIX ".exe")
else()
    set(HOST_EXE_SUFFIX "")
endif()

set(GEN_PLATFORMS_TOOL ${CMAKE_CURRENT_BINARY_DIR}/gen_platforms_tool${HOST_EXE_SUFFIX})

# Determine host compiler flags
if(MSVC)
    set(HOST_CXX_FLAGS "/std:c++17")
    set(HOST_CXX_OUT_FLAG "/Fe${GEN_PLATFORMS_TOOL}")
else()
    set(HOST_CXX_FLAGS "-std=c++17")
    set(HOST_CXX_OUT_FLAG "-o" "${GEN_PLATFORMS_TOOL}")
endif()

# Compile the generator tool on the fly using the host compiler.
# This avoids Python dependency and avoids cross-compilation issues for the tool itself.
add_custom_command(
    OUTPUT ${GENERATED_PLATFORMS_HEADER}
    COMMAND ${HOST_CXX_COMPILER} ${HOST_CXX_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_platforms.cpp ${HOST_CXX_OUT_FLAG}
    COMMAND ${GEN_PLATFORMS_TOOL}
            --input ${PLATFORMS_JSON}
            --output ${GENERATED_PLATFORMS_HEADER}
    DEPENDS ${PLATFORMS_JSON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_platforms.cpp
    COMMENT "Generating platform definitions from JSON"
    VERBATIM
)

add_custom_target(generate_platforms DEPENDS ${GENERATED_PLATFORMS_HEADER})

add_executable(openhd_sys_utils
    src/openhd_sys_utils.cpp
    src/sysutil_debug.cpp
    src/sysutil_firstboot.cpp
    src/sysutil_config.cpp
    src/sysutil_camera.cpp
    src/sysutil_hostname.cpp
    src/sysutil_led.cpp
    src/sysutil_protocol.cpp
    src/sysutil_platform.cpp
    src/sysutil_settings.cpp
    src/sysutil_status.cpp
    src/sysutil_update.cpp
    src/sysutil_part.cpp
    src/sysutil_video.cpp
    src/sysutil_wifi.cpp
    ${GENERATED_PLATFORMS_HEADER}
)

add_dependencies(openhd_sys_utils generate_platforms)

target_include_directories(openhd_sys_utils PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(openhd_sys_utils PRIVATE Threads::Threads)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
    CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(openhd_sys_utils PRIVATE stdc++fs)
endif()

set(BUILD_VERSION_FILE ${CMAKE_CURRENT_BINARY_DIR}/build_version.txt)
set(GENERATED_VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/version_generated.h)

add_custom_target(update_build_version ALL
    COMMAND ${CMAKE_COMMAND}
            -DVERSION_FILE=${BUILD_VERSION_FILE}
            -DOUTPUT_HEADER=${GENERATED_VERSION_HEADER}
            -DBASE_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/increment_build_version.cmake
    BYPRODUCTS ${BUILD_VERSION_FILE} ${GENERATED_VERSION_HEADER}
    COMMENT "Updating build version"
    VERBATIM
)

add_dependencies(openhd_sys_utils update_build_version)
target_sources(openhd_sys_utils PRIVATE ${GENERATED_VERSION_HEADER})

install(TARGETS openhd_sys_utils
    RUNTIME DESTINATION /usr/local/bin
)

install(
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/systemd/openhd-sys-utils.service
    DESTINATION /lib/systemd/system
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "openhd-sys-utils")
set(CPACK_PACKAGE_VENDOR "OpenHD")
set(CPACK_PACKAGE_CONTACT "OpenHD Team")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "OpenHD Team")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OpenHD system utilities")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PROJECT_CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/cpack_project_config.cmake)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpack_project_config.cmake.in
    ${CPACK_PROJECT_CONFIG_FILE}
    @ONLY
)
include(CPack)
