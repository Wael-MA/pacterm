#!/bin/bash
set -e

CXX=g++
CXXFLAGS="-std=c++23 -Wall -Wextra -Wpedantic -Werror -Isrc -O2"

SRC="src/main.cpp src/GameEngine.cpp src/I18n.cpp"
TARGET="pacterm"

echo "Compiling pacterm..."

$CXX $CXXFLAGS -c src/main.cpp -o src/main.o
$CXX $CXXFLAGS -c src/GameEngine.cpp -o src/GameEngine.o
$CXX $CXXFLAGS -c src/I18n.cpp -o src/I18n.o

$CXX $CXXFLAGS -o $TARGET src/main.o src/GameEngine.o src/I18n.o

echo "Build successful: $TARGET"