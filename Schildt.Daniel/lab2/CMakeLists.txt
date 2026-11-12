cmake_minimum_required(VERSION 3.10)
project(lab2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Werror)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

file(GLOB CONN_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/conn_*.cpp)
file(GLOB COMMON_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER COMMON_FILES EXCLUDE REGEX "/(conn_[^/]*|host)\\.cpp$")

foreach(file ${CONN_FILES})
    get_filename_component(name ${file} NAME_WE)
    string(REPLACE "conn_" "" code ${name})
    add_executable(host_${code} src/host.cpp ${COMMON_FILES} ${file})
    target_link_libraries(host_${code} rt pthread)
endforeach()
