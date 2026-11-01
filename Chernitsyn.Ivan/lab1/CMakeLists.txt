cmake_minimum_required(VERSION 3.16)
project(reminderd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Werror)

add_library(reminder STATIC
    src/event.cpp
    src/config.cpp
    src/pidfile.cpp
    src/notify.cpp
)
target_include_directories(reminder PUBLIC include)

add_executable(reminderd src/main.cpp src/daemon.cpp)
target_link_libraries(reminderd PRIVATE reminder)

option(BUILD_TESTS "Build criterion tests" OFF)

if(BUILD_TESTS)
    include(cmake/criterion.cmake)
    enable_testing()

    foreach(name event config pidfile notify)
        add_executable(test_${name} tests/test_${name}.cpp)
        target_link_libraries(test_${name} PRIVATE reminder criterion)
        add_test(NAME ${name} COMMAND test_${name})
    endforeach()
endif()
