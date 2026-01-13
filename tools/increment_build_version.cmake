if(NOT DEFINED VERSION_FILE)
    message(FATAL_ERROR "VERSION_FILE is not set")
endif()

if(NOT DEFINED OUTPUT_HEADER)
    message(FATAL_ERROR "OUTPUT_HEADER is not set")
endif()

if(NOT DEFINED BASE_VERSION)
    message(FATAL_ERROR "BASE_VERSION is not set")
endif()

if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" _openhd_build_ver)
    string(STRIP "${_openhd_build_ver}" _openhd_build_ver)
endif()

if(NOT _openhd_build_ver)
    set(_openhd_build_ver "0")
endif()

math(EXPR _openhd_next_build "${_openhd_build_ver} + 1")
file(WRITE "${VERSION_FILE}" "${_openhd_next_build}\n")

set(_openhd_full_version "${BASE_VERSION}.${_openhd_next_build}")
file(WRITE "${OUTPUT_HEADER}"
"#pragma once\n"
"#define OPENHD_SYS_UTILS_VERSION \"${_openhd_full_version}\"\n"
)
