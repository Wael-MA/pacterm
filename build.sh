#!/bin/bash
set -e

CXX=g++
CXXFLAGS="-std=c++20 -Wall -Wextra -Wpedantic -Werror -Isrc -O2"

SRC="src/main.cpp src/GameEngine.cpp"
TARGET="pacterm"

echo "Compiling pacterm..."

$CXX $CXXFLAGS -c src/main.cpp -o src/main.o
$CXX $CXXFLAGS -c src/GameEngine.cpp -o src/GameEngine.o

$CXX $CXXFLAGS -o $TARGET src/main.o src/GameEngine.o

echo "Build successful: $TARGET"