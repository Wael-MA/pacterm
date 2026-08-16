# PacTerm - Terminal Pac-Man V1.3.8

A terminal-based Pac-Man game written in C++20. Navigate the maze, eat pellets, avoid ghosts, and chase high scores right in your terminal.

- **Website**: [https://wael.work.gd/pacterm](https://wael.work.gd/pacterm)

## Features

- **Classic Pac-Man Gameplay**: Navigate mazes, eat pellets, avoid ghosts, and collect power pellets
- **Terminal-Based**: Runs in any modern terminal with Unicode support
- **Multiple Ghost AI**: Each ghost has its own behavior (chase, scatter, frightened modes)
- **High Score System**: Persistent high scores saved locally
- **Sound Effects**: Procedurally generated WAV effects played through `paplay`/`pw-play`/`mpg123`/`mpv` (can be muted with `M`)
- **Multiple Levels**: Progressive difficulty with increasing ghost speed
- **Responsive Controls**: Smooth keyboard controls (WASD / ZQSD / Arrow / Vim keys)
- **PACTERM Letter Hunt**: Collect the hidden `P-A-C-T-E-R-M` letters each level (20s despawn) to unlock the **PacTerm+** composite theme and more
- **PacTerm+ Theme**: A dynamic composite theme blending the best of every color scheme into two dual gradients (a warm primary mix and a cool accent mix); unlock it by collecting all 7 letters
- **Themes**: Unlockable two-color dual-gradient palettes (Classic, Cyan, Green, Pink, Red, Violet, Ice, Amber, Rainbow, PacTerm+)
- **Fever Time**: Chain 4 ghosts on one power pellet to trigger Fever x2 scoring with ghost trails
- **Letter Buffs**: Collecting a letter grants ghost freeze, a speed boost, and a temporary 2x score multiplier
- **Level Rating**: Each cleared level is rated 0 to 10 with a bonus of rating x 1000 pts

## Themes

Each unlockable theme restyles the menus, the HUD, and Pac-Man himself. Unlock them by reaching the matching level group in the Settings menu. Every theme is a **two-color duality**: a *primary* gradient drives Pac-Man, the PACTERM logo, and the HUD text, while a complementary *accent* gradient drives menu borders, walls, and dots.

| Theme | Unlocked At | Two-Color Duo |
|-------|-------------|---------------|
| **Classic** | Default | The classic duo. Golden-yellow primary with cyan accents on borders, walls, and dots. |
| **Cyan** | Level 5 | Neon duo. Cyan primary with coral-red accents. |
| **Green** | Level 9 | Fresh duo. Green primary with magenta accents. |
| **Pink** | Level 13 | Sweet duo. Pink primary with mint-green accents. |
| **Red** | Level 17 | Hot duo. Red primary with teal accents. |
| **Violet** | Level 21 | Arcane duo. Violet primary with golden accents. |
| **Ice** | Level 24 | Chilled duo. Ice-blue primary with deep ocean-blue accents. |
| **Amber** | Level 27 | Warm duo. Amber primary with indigo accents. |
| **Rainbow** | All Levels | Full spectrum. Both the primary and accent families shimmer through every hue. |
| **PacTerm+** | All 7 letters | Composite duo. Blends all 8 color schemes into a warm **primary mix** and a cool **accent mix** for Pac-Man, menus, and the HUD. |

> **Note:** Levels 20 and 30 use the special **Glitch** theme, a flickering, corrupting maze with chaos-based powerups.

## Install Compiled Binary for Arch Linux
```bash
yay -S pacterm-bin
```

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 19.28+)
- Terminal with Unicode/UTF-8 support
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
./pacterm --install

# Uninstall the game from PATH (for Linux and MacOS)
./pacterm --delete

# Print version
./pacterm --version
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
  - Level Rating: rating (0 to 10) x 1000 pts
- **Fever Time**: Chain 4 ghosts in one power pellet for 2x scoring for 6 seconds
- **Letter Hunt**: The expected letter (`P` on level 1, `A` on level 2, ... `M` on level 7, then repeats) spawns on a reachable tile for 20 seconds. Collect all 7 to unlock PacTerm+
- **Level Rating**: `par = 45s + 0.15s x dots`; each death costs 2.5, every 10s over par costs 1

## Building from Source

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake

# Fedora
sudo dnf install gcc-c++ cmake

# Arch
sudo pacman -S base-devel cmake

# macOS
brew install cmake
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
- `pacterm.cache`: scores, settings, keybindings, stats
- `sounds/`: generated sound effects

## Configuration

Key bindings are remappable in-game via **Settings > Configure Keys**; choices persist in `~/.pacterm/pacterm.cache`.

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
- Write tests for new features
- Update documentation for user-facing changes

## Changelog

### v1.3.8
- **Bug fix (critical)**: Fixed a malformed 26-char row in map 17 that caused an out-of-bounds template read, a broken wall, and 2 unreachable dots at (19,6) and (19,7); added a compile-time static_assert that all 30 map templates are exactly 28 columns wide
- **Bug fix**: Wide emoji glyphs (🍒 🍏 ❤️ ✨) no longer misalign the rest of the row — the frame composer tracks cursor position by glyph display width and reserves empty continuation cells
- **Bug fix**: Frightened ghosts no longer escape the ghost house during a power pellet (in-house dot-count release can no longer be bypassed)
- **Bug fix**: Frightened ghosts now flicker blue/white correctly — the flash clock was using a stale timer
- **Bug fix**: Ghost swap/pass-through crossings are now caught via previous-tile tracking
- **Bug fix**: The dash (levels 17–19) no longer ignores collisions/dot scoring and can no longer pass through the ghost-house door
- **Bug fix**: Scrolling viewport is clamped at the bottom edge so the map isn't cut off near the bottom of the maze
- **Bug fix**: The "dev" cheat sequence is only captured in Playing/Main Menu/Level Selector and can no longer hijack the username or settings screens
- **Bug fix**: Added SIGSEGV/SIGABRT/SIGBUS/SIGFPE handlers that restore the terminal if the game crashes
- **Performance**: Replaced the ESC-sequence busy-wait with a `poll()`-based timeout, cutting input latency

### v1.3.7
- **Cleanup**: Unified the version string across all files, renamed the CMake target to `pacterm`, and aligned the README with what the code actually does (CLI flags, sound backend, key configuration)
- **Bug fix**: Mouse hover highlight no longer sticks after the cursor leaves a menu entry; the map generator now protects the full ghost-house region
- **Performance**: Eliminated per-glyph allocations in the text renderer and redundant viewport recalculations in the frame loop

### v1.3.6
- **UI**: Redesigned the Pause and Developer menus, titles are now embedded in the box border, with cleaner, left-aligned layouts
- **Bug fix**: Fixed 2 unreachable dots on level 18
- **Docs**: Added a Themes reference describing every unlockable theme's specialty 

### v1.3.5
- **Performance**: Fixed lag spikes when the frame rate drops (delta is now clamped), and cut per-frame allocations in the renderer (theme palettes and glitch glyphs are built once per frame instead of once per cell)
- **Bug fix**: Fixed a softlock where a letter spawning on a dot could leave a level with a dot counter that never reached zero
- **Controls**: `ESC` now pauses during gameplay (previously ignored); updated controls and scoring docs to match the game
- **Cleanup**: Removed dead code (unused keybinding loader, legacy `Fruit` tile, unused animation/PRNG helpers) and made timer naming consistent

### v1.3.4
- Added Fever Time, Letter Hunt buffs, dynamic level rating, and the PacTerm+ composite theme

## License

This project is licensed under the **GNU General Public License v3.0**. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Inspired by the classic Namco arcade game **Pac-Man** (1980)
- Built with C++20 and standard library only (no external game engines)
- Terminal rendering uses ANSI escape codes and Unicode box-drawing characters
- Ghost AI inspired by original Pac-Man ghost behavior documentation

## Support

- **Issues**: [GitHub Issues](https://github.com/Wael-MA/pacterm/issues)
- **Discussions**: [GitHub Discussions](https://github.com/Wael-MA/pacterm/discussions)

---

<p align="center">
  Made with ❤️ for terminal retro gamers
</p>
