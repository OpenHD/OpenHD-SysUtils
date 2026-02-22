if(NOT DEFINED VERSION_FILE)
    message(FATAL_ERROR "VERSION_FILE is not set")
endif()

if(NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "OUTPUT_HEADER is not set")
endif()

if(NOT DEFINED BASE_VERSION)
    message(FATAL_ERROR "BASE_VERSION is not set")
endif()

set(_openhd_build_ver "")

if(DEFINED ENV{OPENHD_BUILD_NUMBER} AND NOT "$ENV{OPENHD_BUILD_NUMBER}" STREQUAL "")
    set(_openhd_build_ver "$ENV{OPENHD_BUILD_NUMBER}")
elseif(DEFINED ENV{GITHUB_RUN_NUMBER} AND NOT "$ENV{GITHUB_RUN_NUMBER}" STREQUAL "")
    set(_openhd_build_ver "$ENV{GITHUB_RUN_NUMBER}")
elseif(DEFINED ENV{CI_PIPELINE_IID} AND NOT "$ENV{CI_PIPELINE_IID}" STREQUAL "")
    set(_openhd_build_ver "$ENV{CI_PIPELINE_IID}")
elseif(DEFINED ENV{BUILD_NUMBER} AND NOT "$ENV{BUILD_NUMBER}" STREQUAL "")
    set(_openhd_build_ver "$ENV{BUILD_NUMBER}")
elseif(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" _openhd_build_ver)
    string(STRIP "${_openhd_build_ver}" _openhd_build_ver)
endif()

if(NOT _openhd_build_ver)
    string(TIMESTAMP _openhd_build_ver "%Y%m%d%H%M%S" UTC)
endif()

if(_openhd_build_ver MATCHES "^[0-9]+$")
    if(EXISTS "${VERSION_FILE}")
        file(READ "${VERSION_FILE}" _openhd_last_build)
        string(STRIP "${_openhd_last_build}" _openhd_last_build)
        if(_openhd_last_build MATCHES "^[0-9]+$")
            if(_openhd_build_ver LESS_EQUAL _openhd_last_build)
                math(EXPR _openhd_build_ver "${_openhd_last_build} + 1")
            endif()
        endif()
    endif()
endif()

file(WRITE "${VERSION_FILE}" "${_openhd_build_ver}\n")

set(_openhd_full_version "${BASE_VERSION}.${_openhd_build_ver}")
file(WRITE "${OUTPUT_HEADER}"
"#pragma once\n"
"#define OPENHD_SYS_UTILS_VERSION \"${_openhd_full_version}\"\n"
)
