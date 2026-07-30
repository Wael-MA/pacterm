cmake_minimum_required(VERSION 3.16)
project(terminal-pacman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(pacman
    src/main.cpp
    src/GameEngine.cpp
)

target_include_directories(pacman PRIVATE src)
target_compile_options(pacman PRIVATE -Wall -Wextra -Wpedantic -Werror)
