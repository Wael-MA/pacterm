cmake_minimum_required(VERSION 3.25)
project(pacterm VERSION 1.3.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(pacterm
    src/main.cpp
    src/GameEngine.cpp
)

target_include_directories(pacterm PRIVATE src)
target_compile_options(pacterm PRIVATE -Wall -Wextra -Wpedantic -Werror)
