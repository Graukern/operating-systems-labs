cmake_minimum_required(VERSION 3.16)
project(ProcessesDemonization CXX)

if(NOT UNIX)
    message(FATAL_ERROR "Проект собирается только в Linux")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Werror)

file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.cpp)
list(FILTER CORE_SRC EXCLUDE REGEX ".*/main\\.cpp$")

add_library(core STATIC ${CORE_SRC})
target_include_directories(core PUBLIC src)

add_executable(ProcessesDemonization src/main.cpp)
target_link_libraries(ProcessesDemonization PRIVATE core)

file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*.cpp)
add_executable(tests ${TEST_SRC})
target_link_libraries(tests PRIVATE core)
target_include_directories(tests PRIVATE tests)

enable_testing()
add_test(NAME unit COMMAND tests)