CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror -Isrc -O2

SRC = src/main.cpp src/GameEngine.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = pacterm

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) pactermbak
