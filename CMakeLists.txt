cmake_minimum_required(VERSION 3.10)
project(Kekno VERSION 0.5.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Include header files directory
include_directories(include)

# Gather source files
set(SOURCES
    src/chunk.cpp
    src/lexer.cpp
    src/compiler.cpp
    src/vm.cpp
    src/main.cpp
)

# Build executable
add_executable(kekno ${SOURCES})

# Test executable
enable_testing()
set(TEST_SOURCES
    src/chunk.cpp
    src/lexer.cpp
    src/compiler.cpp
    src/vm.cpp
    tests/test_kekno.cpp
)
add_executable(test_kekno ${TEST_SOURCES})
add_test(NAME test_kekno COMMAND test_kekno)
