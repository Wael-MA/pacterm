# PacTerm - Terminal Pac-Man V1.3.5

A terminal-based Pac-Man game written in C++20. Navigate the maze, eat pellets, avoid ghosts, and chase high scores in your terminal.

- **Website**: [https://wael.work.gd/pacterm](https://wael.work.gd/pacterm)

## Features

- **Classic Pac-Man Gameplay**: Navigate mazes, eat pellets, avoid ghosts, and collect power pellets
- **Terminal-Based**: Runs in any modern terminal with Unicode support
- **Multiple Ghost AI**: Each ghost has unique behavior (chase, scatter, frightened modes)
- **High Score System**: Persistent high scores saved locally
- **Sound Effects**: Optional terminal bell sounds for actions
- **Multiple Levels**: Progressive difficulty with increasing ghost speed
- **Responsive Controls**: Smooth keyboard controls (WASD / ZQSD / Arrow / Vim keys)
- **PACTERM Letter Hunt**: Collect the hidden `P-A-C-T-E-R-M` letters each level (20s despawn) to unlock the **PacTerm+** composite theme and more
- **PacTerm+ Theme**: A dynamic composite theme blending the best of every color scheme; unlock it by collecting all 7 letters
- **Themes**: Unlockable palette (Classic, Cyan, Green, Pink, Red, Violet, Ice, Amber, Rainbow, PacTerm+)
- **Fever Time**: Chaining 4 ghosts in one power pellet eats triggers Fever x2 scoring with ghost trails
- **Letter Buffs**: Collecting a letter grants ghost freeze, a speed boost, and a temporary 2× score multiplier
- **Level Rating**: Each cleared level is rated 0–10 with a bonus of rating × 1000 pts
- **Cheat Code**: Enter `WAEL` in the redeem menu to unlock all levels, Rainbow, and PacTerm+

## Install Compiled Binary for Arch Linux
```bash
yay -S pacterm-bin
```

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 19.28+)
- Terminal with Unicode/UTF-8 support
- ncurses (optional, for enhanced terminal control)
- CMake 3.16+ or Make

## Building

### Using CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
make
./pacterm
```

### Using Make

```bash
make
./pacterm
```

### Using Build Script

```bash
./build.sh
./pacterm
```

## Installation

```bash
# Using CMake
mkdir build && cd build
cmake ..
make
sudo make install

# Or manually
sudo cp pacterm /usr/local/bin/
```

## Usage

```bash
# Run the game
./pacterm

# Run with automatic install to PATH (for Linux and MacOS)
./pacterm install

# Unistall the game from PATH (for Linux and MacOS)
./pacterm delete
```

### Controls

| Key | Action |
|-----|--------|
| `WASD` / `ZQSD` / `↑←↓→` / `HJKL` | Move (arrows and Vim keys always enabled) |
| `P` / `ESC` | Pause / Resume |
| `M` | Toggle Sound |
| `SPACE` | Dash (Levels 17-19) |

## Gameplay

- **Objective**: Clear all pellets in the maze to advance to the next level
- **Pellets**: Small dots worth 10 points each
- **Power Pellets**: Large flashing pellets worth 50 points; make ghosts vulnerable
- **Ghosts**: Four ghosts with unique AI behaviors
  - **Blinky (Red)**: Directly chases Pac-Man
  - **Pinky (Pink)**: Ambushes ahead of Pac-Man
  - **Inky (Cyan)**: Unpredictable, mirrors Blinky's position
  - **Clyde (Orange)**: Alternates between chase and scatter
- **Scoring**: 
  - Pellet: 10 pts
  - Power Pellet: 50 pts
  - Ghost (1st): 200 pts, (2nd): 400 pts, (3rd): 800 pts, (4th): 1600 pts
  - Letter Hunt: 1000 pts + buffs per letter
  - Level Rating: rating (0–10) × 1000 pts
- **Fever Time**: Chain 4 ghosts in one power pellet for 2× scoring for 6 seconds
- **Letter Hunt**: The expected letter (`P` on level 1, `A` on level 2, … `M` on level 7, then repeats) spawns on a reachable tile for 20 seconds. Collect all 7 to unlock PacTerm+
- **Level Rating**: `par = 45s + 0.15s × dots`; each death costs 2.5, every 10s over par costs 1

## Building from Source

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake libncurses5-dev

# Fedora
sudo dnf install gcc-c++ cmake ncurses-devel

# Arch
sudo pacman -S base-devel cmake ncurses

# macOS
brew install cmake ncurses
```

### Build Types

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release build (default)
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Project Structure

```
pacterm/
├── CMakeLists.txt      # CMake build configuration
├── Makefile            # Makefile for simple builds
├── build.sh            # Simple build script
├── LICENSE             # GPL-3.0 License
├── README.md           # This file
├── pacterm.keys        # Key configuration
├── pacterm             # Compiled binary (after build)
├── src/
│   ├── main.cpp        # Entry point
│   ├── GameEngine.cpp  # Game engine implementation
│   ├── GameEngine.hpp  # Game engine header
│   ├── Pacman.hpp      # Pac-Man entity
│   ├── Ghost.hpp       # Ghost entity & AI
│   └── Types.hpp       # Shared types & constants
└── sounds/             # Source sound files (optional)
```

Runtime data is stored under `~/.pacterm/`:
- `pacterm.cache` — scores, settings, stats
- `sounds/` — generated sound effects (fallback to terminal bell)

## Configuration

Key bindings can be customized by editing `pacterm.keys`:

```ini
UP=KEY_UP
DOWN=KEY_DOWN
LEFT=KEY_LEFT
RIGHT=KEY_RIGHT
PAUSE=p
QUIT=q
SOUND=m
```

## High Scores

Scores, settings, and stats are saved to `~/.pacterm/pacterm.cache` automatically.

## Building for Distribution

```bash
# Create release build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..
make
make package  # Creates .tar.gz or .deb/.rpm with CPack
```

## Contributing

Contributions are welcome! Please follow these steps:

1. **Fork** the repository
2. **Create** a feature branch: `git checkout -b feature/amazing-feature`
3. **Commit** your changes: `git commit -m 'feat: add amazing feature'`
4. **Push** to the branch: `git push origin feature/amazing-feature`
5. **Open** a Pull Request

### Development Guidelines

- Follow C++20 best practices and modern C++ idioms
- Maintain `-Wall -Wextra -Wpedantic -Werror` clean builds
- Format code with `clang-format` (config in `.clang-format`)
- Write tests for new features
- Update documentation for user-facing changes

### Code Style

```bash
# Format code
clang-format -i src/*.cpp src/*.hpp

# Static analysis
clang-tidy src/*.cpp -- -std=c++20 -Isrc
```

## Changelog

### v1.3.5
- **Performance**: Fixed lag spikes when the frame rate drops (delta is now clamped), and cut per-frame allocations in the renderer (theme palettes and glitch glyphs are built once per frame instead of once per cell)
- **Bug fix**: Fixed a softlock where a letter spawning on a dot could leave a level with a dot counter that never reached zero
- **Controls**: `ESC` now pauses during gameplay (previously ignored); updated controls and scoring docs to match the game
- **Cleanup**: Removed dead code (unused keybinding loader, legacy `Fruit` tile, unused animation/PRNG helpers) and made timer naming consistent

### v1.3.4
- Added Fever Time, Letter Hunt buffs, dynamic level rating, `WAEL` cheat code, and the PacTerm+ composite theme

## License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Inspired by the classic Namco arcade game **Pac-Man** (1980)
- Built with C++20 and standard library only (no external game engines)
- Terminal rendering uses ANSI escape codes and Unicode box-drawing characters
- Ghost AI inspired by original Pac-Man ghost behavior documentation

## Support

- **Issues**: [GitHub Issues](https://github.com/wa-el-az/pacterm/issues)
- **Discussions**: [GitHub Discussions](https://github.com/wa-el-az/pacterm/discussions)

---

<p align="center">
  Made with ❤️ for terminal retro gamers
</p>
