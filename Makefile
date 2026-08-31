CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -Wpedantic -Werror -Isrc -O2

SRC = src/main.cpp src/GameEngine.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = pacterm

# Install location (override with `make install PREFIX=/usr`)
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/pacterm
	install -Dm644 pacterm.desktop $(DESTDIR)$(DATADIR)/applications/pacterm.desktop
	install -Dm644 img/PacTermIcon.png $(DESTDIR)$(DATADIR)/pixmaps/pacterm.png

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/pacterm
	rm -f $(DESTDIR)$(DATADIR)/applications/pacterm.desktop
	rm -f $(DESTDIR)$(DATADIR)/pixmaps/pacterm.png

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all install uninstall clean