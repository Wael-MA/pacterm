CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror -Isrc -O2

SRC = src/main.cpp src/GameEngine.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = pacterm

# Install location (override with `make install PREFIX=/usr`)
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/pacterm

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/pacterm

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all install uninstall clean