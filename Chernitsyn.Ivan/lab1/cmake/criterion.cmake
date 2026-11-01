set(criterion_version 2.4.2)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CRITERION QUIET IMPORTED_TARGET criterion)
endif()

if(CRITERION_FOUND)
    message(STATUS "Using system criterion ${CRITERION_VERSION}")
    add_library(criterion INTERFACE IMPORTED)
    target_link_libraries(criterion INTERFACE PkgConfig::CRITERION)
    return()
endif()

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    message(FATAL_ERROR "criterion not found, install it with your package manager")
endif()

message(STATUS "criterion not found, downloading ${criterion_version}")

include(FetchContent)
FetchContent_Declare(criterion_bin
    URL https://github.com/Snaipe/Criterion/releases/download/v${criterion_version}/criterion-${criterion_version}-linux-x86_64.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(criterion_bin)

add_library(criterion SHARED IMPORTED)
set_target_properties(criterion PROPERTIES
    IMPORTED_LOCATION ${criterion_bin_SOURCE_DIR}/lib/libcriterion.so.3
    INTERFACE_INCLUDE_DIRECTORIES ${criterion_bin_SOURCE_DIR}/include
)
