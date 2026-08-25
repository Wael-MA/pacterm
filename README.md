# PacTerm - Terminal Pac-Man v1.3.9

A full-featured terminal Pac-Man game written in modern C++23. Navigate mazes, eat pellets, outsmart ghosts, trigger powerups, and climb high scores directly in your terminal.

- **Website**: [https://wael.work.gd/pacterm](https://wael.work.gd/pacterm)

## Features

- **30 Handcrafted Levels**: Progressive level layout system featuring unique maze designs, custom color palettes, and escalating difficulty.
- **Dynamic Fruit Powerups**: Fruits spawn at dot milestones (70 and 170 dots remaining) on verified reachable tiles, providing 8 distinct powerups (Speed Boost, Ghost Freeze, Pellet Magnet, Energy Shield, Double Bounty, Tractor Repel, Ghost Panic, and Extra Life).
- **Theme Info & Level Guide**: Accessible directly from the Pause menu (`P` or `ESC` -> `Theme Info & Guide`), detailing active mechanics, hazards, and dual-tone gradient palettes.
- **Classic Ghost AI**: Authentic chase, scatter, and frightened behavior modes with distinct personalities for Blinky, Pinky, Inky, and Clyde.
- **Level Selector**: Browse and launch unlocked levels directly from the menu with custom split visuals for glitch zones.
- **PACTERM Letter Hunt**: Collect the sequential letters (`P-A-C-T-E-R-M`) across levels to unlock the composite **PacTerm+** theme.
- **Fever Time & Multipliers**: Chain 4 ghosts on a single power pellet to trigger Fever 2x score mode with particle trails.
- **Persistent High Scores & Stats**: Local tracking of games played, dots eaten, ghosts defeated, deaths, and playtime.
- **Sound Effects**: Procedurally generated audio effects supporting `paplay`, `pw-play`, `mpg123`, and `mpv` (toggleable with `M`).
- **Flexible Controls & TUI**: Smooth keyboard controls (WASD, ZQSD, Arrow, and Vim keys), in-game key rebinding, full mouse navigation, and clean ASCII/Nerd Font rendering.

## Themes

Each theme restyles menus, borders, the HUD, and Pac-Man. Themes are dual-color palettes: a **primary** color for Pac-Man, titles, and score text, with a complementary **accent** color for walls, dots, and borders.

| Theme | Unlocked At | Description |
|-------|-------------|-------------|
| **Classic** | Default | Golden-yellow primary with cyan accents on borders, walls, and dots. |
| **Cyan** | Level 5 | Bright cyan primary with coral-red accents. |
| **Green** | Level 9 | Emerald green primary with magenta accents. |
| **Pink** | Level 13 | Rose pink primary with mint-green accents. |
| **Red** | Level 17 | Crimson red primary with teal accents. |
| **Violet** | Level 21 | Violet primary with warm golden accents. |
| **Ice** | Level 24 | Ice-blue primary with deep ocean accents. |
| **Amber** | Level 27 | Amber gold primary with indigo accents. |
| **Rainbow** | Redeem | Dynamic multi-hue spectrum cycling across the color wheel. |
| **Glitch** | Level 30 | Corrupted chromatic RGB shift with inverted channel highlights. |
| **PacTerm+** | All 7 Letters | Composite theme blending all palettes into a unified master style. |

## Installation

### Arch Linux (AUR)

```bash
yay -S pacterm
```

### From Source

#### Prerequisites

- C++23 compatible compiler (GCC 14+, Clang 17+, MSVC 19.38+)
- CMake 3.25+ or GNU Make
- Terminal with UTF-8 support

Package manager installation:

```bash
# Ubuntu / Debian
sudo apt-get install build-essential cmake

# Fedora
sudo dnf install gcc-c++ cmake

# Arch Linux
sudo pacman -S base-devel cmake

# macOS (Homebrew)
brew install cmake
```

#### Build with CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
make
sudo make install
```

#### Build with Makefile

```bash
make
sudo make install
```

#### Build with Script

```bash
./build.sh
./pacterm
```

## Usage

```bash
# Run the game
./pacterm

# Install to ~/.local/bin
./pacterm --install

# Uninstall from ~/.local/bin
./pacterm --delete

# Print version
./pacterm --version
```

### Default Controls

| Key | Action |
|-----|--------|
| `WASD` / `ZQSD` / `Arrow Keys` / `HJKL` | Move Pac-Man |
| `P` / `ESC` | Pause / Resume / Back |
| `M` | Toggle Audio Mute |
| `SPACE` | Dash Burst (Levels 17-19) |

*Key bindings can be remapped at any time under **Settings > Configure Keys**.*

## Scoring & Rules

- **Dots**: 10 points
- **Power Pellets**: 50 points (triggers frightened ghost vulnerability)
- **Ghost Multiplier**: 200 -> 400 -> 800 -> 1600 points per consecutive ghost eaten during a power pellet
- **Fever Frenzy**: Eating all 4 ghosts on one power pellet triggers 6 seconds of 2x Fever scoring
- **Fruit Bonuses**: 100 to 5,000 points based on fruit rarity
- **Level Performance Rating**: Cleared levels are rated 0 to 10 based on par time and lives preserved, awarding up to 10,000 bonus points

## Configuration & Data

Player configuration, stats, and high scores are saved locally at:

- `~/.pacterm/pacterm.cache` (settings, key bindings, scores, and stats)
- `~/.pacterm/sounds/` (synthesized WAV sound bank)

## Project Structure

```
pacterm/
├── CMakeLists.txt      # CMake build configuration
├── Makefile            # Makefile build configuration
├── build.sh            # Direct compilation script
├── LICENSE             # GNU GPL-3.0 license
├── README.md           # Documentation
├── CHANGELOG.md        # Release changelog
├── CONTRIBUTING.md     # Contribution guidelines
├── src/
│   ├── main.cpp        # Application entry point and CLI dispatch
│   ├── GameEngine.cpp  # Core engine, rendering, AI, and game loop
│   ├── GameEngine.hpp  # Engine declarations and state structures
│   ├── Pacman.hpp      # Pac-Man entity state and movement
│   ├── Ghost.hpp       # Ghost entity AI, modes, and behavior
│   └── Types.hpp       # Coordinates, colors, tiles, and map templates
└── sounds/             # Sound bank directory
```

## Contributing

Contributions, bug reports, and suggestions are welcome! Please review [CONTRIBUTING.md](CONTRIBUTING.md) for details on code style, commit standards, and workflow.

## License

PacTerm is free software licensed under the **GNU General Public License v3.0**. See the [LICENSE](LICENSE) file for details.
