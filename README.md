# PacTerm - Terminal Pac-Man v1.4.0

A fast, responsive, and feature-packed Pac-Man game built from the ground up for modern terminals in clean C++23. 

Whether you are looking for authentic arcade ghost pathfinding, 30 handcrafted levels with unique stage mechanics, vibrant TrueColor themes, or full multilingual support right in your terminal window, PacTerm brings the arcade classic to life in your shell.

- **Website**: [https://wael.work.gd/pacterm](https://wael.work.gd/pacterm)
- **Author**: Wael Amrani Zerrifi

---

## Highlights & Features

- **30 Handcrafted Levels**: Progressive maze layouts with escalating difficulty, custom color palettes, and level-specific stage hazards.
- **Multilingual Support (7 Languages)**: Native support for English, Arabiya (Latin script for broad terminal compatibility), French, Spanish, German, Italian, and Japanese with auto-detection and on-the-fly switching.
- **Responsive Terminal UI**: Automatically adapts to your terminal dimensions with double-line border boxes, marquee-scrolling for long option names, and clean column alignments.
- **Authentic Ghost AI**: Faithful reproduction of original arcade behavior—distinct personality targets for Blinky (shadow/chase), Pinky (ambush), Inky (flank), and Clyde (shy/wander) across Chase, Scatter, and Frightened modes.
- **8 Dynamic Fruit Powerups**: Milestone fruit spawns on reachable tiles that reward unique perks: Speed Boosts, Ghost Freezes, Pellet Magnets, Energy Shields, Double Bounty multipliers, Tractor Repels, Ghost Panics, and Extra Lives.
- **In-Game Theme & Mechanic Guide**: Hit `P` or `ESC` during a game and open the **Theme Info & Guide** to see active mechanics, hazards, and buffs for your current stage.
- **PACTERM Letter Hunt**: Discover and collect all seven letters (`P-A-C-T-E-R-M`) scattered across the stages to unlock the ultimate **PacTerm+** theme.
- **Fever Mode**: Eat all four ghosts on a single power pellet to trigger a 6-second high-energy 2x scoring frenzy.
- **Persistent Local Stats**: High scores, total dots eaten, ghost combos, playtime, and settings are saved automatically to `~/.pacterm/pacterm.cache`.
- **Audio Effects**: Built-in sound effect synthesis supporting `paplay`, `pw-play`, `mpg123`, and `mpv` (mute anytime with `M`).
- **Full Input Customization**: Smooth keyboard navigation (WASD, ZQSD, Arrow keys, Vim `HJKL`), interactive mouse click support, and in-game key remapping.

---

## Themes & Visuals

PacTerm comes with 11 custom visual themes. Each theme changes the primary palette for Pac-Man and UI titles, paired with a matching accent color for walls, dots, and borders.

| Theme | How to Unlock | Description |
|---|---|---|
| **Classic** | Default | Iconic golden-yellow Pac-Man with crisp cyan maze walls and dots. |
| **Cyan** | Level 5 | Electric cyan palette with warm coral-red accents. |
| **Green** | Level 9 | Emerald green tone paired with vivid magenta accents. |
| **Pink** | Level 13 | Neon rose pink with mint-green accents. |
| **Red** | Level 17 | Fiery crimson red with cool teal accents. |
| **Violet** | Level 21 | Deep violet with glowing golden accents. |
| **Ice** | Level 24 | Frosty ice-blue with deep oceanic accents. |
| **Amber** | Level 27 | Warm amber gold with rich indigo accents. |
| **Rainbow** | Redeem code (`RAINBOW`) | Dynamic chromatic spectrum cycling across the full color wheel. |
| **Glitch** | Level 30 | Chromatic RGB phase shift with corrupted digital scanline aesthetics. |
| **PacTerm+** | Collect all 7 Letters | Composite master theme featuring dynamic cyber gradients and buffs. |

---

## Installation

### Arch Linux (AUR)

If you're on Arch Linux or an Arch-based distro, you can install PacTerm directly from the AUR:

```bash
yay -S pacterm
```

### Building From Source

#### Prerequisites

To compile PacTerm, you will need:
- A C++23 capable compiler (GCC 14+, Clang 17+, or MSVC 19.38+)
- CMake 3.25+ or GNU Make
- A terminal with UTF-8 support (e.g., Alacritty, Kitty, WezTerm, Ghostty, GNOME Terminal, Windows Terminal)

Quick install for build dependencies:

```bash
# Debian / Ubuntu / Mint
sudo apt-get install build-essential cmake

# Fedora / RHEL
sudo dnf install gcc-c++ cmake

# Arch Linux / Manjaro
sudo pacman -S base-devel cmake

# macOS (Homebrew)
brew install cmake
```

#### Option 1: Build with CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
sudo cmake --install .
```

#### Option 2: Build with Make

```bash
make -j$(nproc)
sudo make install
```

#### Option 3: Quick Build Script

```bash
./build.sh
./pacterm
```

---

## Usage & CLI Options

Run the game directly from your terminal:

```bash
# Start PacTerm
pacterm

# Install to ~/.local/bin (no root required)
./pacterm --install

# Remove from ~/.local/bin
./pacterm --delete

# Print version and build info
./pacterm --version
```

### Default Controls

| Key | Action |
|---|---|
| `WASD` / `ZQSD` / `Arrow Keys` / `HJKL` | Move Pac-Man |
| `P` or `ESC` | Pause / Open In-Game Menu / Back |
| `M` | Toggle sound effects on/off |
| `SPACE` | Activate Dash Burst (Levels 17–19) |
| `Left Click` | Select menu options, levels, and settings buttons |

> [!TIP]
> You can easily remap any gameplay control in the **Settings > Configure Keys** menu.

---

## Scoring System

- **Dot**: 10 points
- **Power Pellet**: 50 points (turns ghosts blue and vulnerable)
- **Ghost Chain**: 200 → 400 → 800 → 1600 points for consecutive ghosts eaten on a single power pellet
- **Fever Frenzy**: 6 seconds of double points (2x) when eating all 4 ghosts on one pellet
- **Bonus Fruits**: 100 to 5,000 points depending on the fruit rarity
- **Stage Clear Rating**: Cleared levels are rated up to 10 points based on speed and lives kept, awarding up to 10,000 bonus points

---

## Save Data & Cache

PacTerm stores configuration files, unlock progress, and sound banks in your user directory:

- `~/.pacterm/pacterm.cache` (settings, key bindings, scores, and statistics)
- `~/.pacterm/sounds/` (synthesized WAV audio assets)

---

## Project Structure

```
pacterm/
├── CMakeLists.txt      # CMake build definitions
├── Makefile            # Standalone Makefile build
├── build.sh            # Quick compile helper
├── LICENSE             # GNU GPL-3.0 License
├── README.md           # Project documentation
├── CHANGELOG.md        # Release version history
├── CONTRIBUTING.md     # Contribution guidelines
├── CODE_OF_CONDUCT.md  # Community standards
├── src/
│   ├── main.cpp        # Entry point and CLI argument parser
│   ├── GameEngine.cpp  # Core engine, rendering loops, game state, and UI
│   ├── GameEngine.hpp  # Engine declarations and member definitions
│   ├── I18n.cpp        # Localization dictionaries and formatter implementations
│   ├── I18n.hpp        # Internationalization interface and string lookup
│   ├── Pacman.hpp      # Pac-Man player state, speed, and movement logic
│   ├── Ghost.hpp       # Ghost entity AI, pathfinding, and behavioral modes
│   └── Types.hpp       # Coordinates, colors, tiles, and 30 maze layouts
└── sounds/             # Sound bank directory
```

---

## Contributing

Feedback, bug reports, and pull requests are always welcome! Check out [CONTRIBUTING.md](CONTRIBUTING.md) for tips on setting up your dev environment, coding style, and submitting changes.

---

## License

PacTerm is open-source software licensed under the **GNU General Public License v3.0** (GPL-3.0-or-later). See [LICENSE](LICENSE) for the full text.
