# Changelog

All notable changes to **PacTerm** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [v1.4.0] - 2026-08-31

### Added
- **Native Multilingual Support (7 Languages)**:
  - Built-in internationalization system supporting **English**, **Arabiya** (using Latin/Franco transliteration for seamless rendering across all terminal emulators without RTL shaping issues), **French**, **Spanish**, **German**, **Italian**, and **Japanese**.
  - Automatic language detection on startup from system environment variables (`LC_ALL`, `LC_MESSAGES`, `LANG`), with instant runtime switching directly in the Settings menu.
  - Safe, modern C++23 string formatting with `I18n::t(...)` and `I18n::format(...)` backed by `std::vformat`.
- **Responsive TUI Layout & Smart Text Fitting**:
  - Container and border systems now dynamically adapt to any terminal window size without broken border corners or awkward line wraps.
  - Smart text fitting with smooth marquee scrolling for focused menu options, and clean ellipsis truncation for inactive elements.
  - Accurate Unicode visual column width calculation (`displayWidth`) handling multi-byte UTF-8 sequences, wide CJK / Japanese characters, and Nerd Fonts glyphs without text clipping.
- **Theme Info & Stage Guide in Pause Menu**:
  - Fully localized guide accessible anytime during gameplay (`P` or `ESC` -> *Theme Info & Guide*), displaying level mechanics, stage hazards, and active theme palettes.

### Improved & Fixed
- **Settings Screen Layout & Column Alignment**:
  - Dynamically aligns all setting labels, colons (`:`), and value selectors into clean vertical columns across all languages and display widths.
  - Polished container height and padding for a cleaner look.
- **PacTerm+ Theme & Palette Polishing**:
  - Maintained authentic white and grey text highlights across all menus and dialogs when the PacTerm+ theme is active.
  - Preserved authentic per-level dot and pellet colors, keeping the gameplay visually clear and responsive.
  - Cleaned up Stats screen labels to remain crisp and neutral grey while values stay bright white.
  - Fixed theme cycling in Settings and Dev menus so PacTerm+ (theme #10) cycles smoothly once unlocked.
- **Terminal Session Safety & Cleanup**:
  - Unconditionally restores cursor visibility, disables mouse reporting, clears SGR attributes, and resets terminal `termios` flags on exit and upon receiving POSIX signals (`SIGINT`, `SIGTERM`, `SIGSEGV`, etc.).
- **Collision & Movement Safety**:
  - Implemented bidirectional two-body swap collision tracking between Pac-Man and ghosts using `prevPosition` recording.
  - Wrapped tunnel coordinates within the Dash ability loop to prevent out-of-bounds positioning.
  - Fixed ghost house return revival for Inky and Clyde (`dotCounter` reset fix) and dynamic global mode exit (`Scatter`/`Chase`).
- **Data Persistence & Encoding**:
  - Implemented hex-encoded XOR ciphertext serialization for high scores and user statistics to eliminate null-byte truncation.
- **TUI & Rendering Optimization**:
  - Flattened double-buffer storage into contiguous 1D memory buffers (`std::vector<Cell>`), eliminating row vector allocations and cache misses.
  - Replaced coordinate formatting with zero-allocation `std::to_chars` ANSI generation in `presentFrame`.
  - Added single-width tile padding in `setTileGlyph` to prevent wide-character visual artifacting.
- **Audio & Input Subsystem**:
  - Deployed dedicated background audio worker thread with non-blocking dispatch and queueing.
  - Added STDIN poll timeout to safely resolve split ANSI escape sequences without stalling.
  - Made crash signal handler async-signal-safe by removing `tcsetattr` calls from fatal signal context.
- **Asset & Sound Preloading Subsystem**:
  - Added `preloadAssets()` to synthesize all 6 audio tracks and pre-read files into OS page cache at boot.
  - Preloaded and validated all 30 level maze layouts and warmed up `I18n` localization lookup tables on startup for zero-latency gameplay transitions.

---

## [v1.3.9] - 2026-08-25

### Added
- **Reachable Fruit & Weighted Rarity System**:
  - Fruits spawn strictly on reachable walkable tiles using BFS pathfinding (`reachableTiles()`), avoiding ghost-house interiors and tunnel edges.
  - Implemented weighted rarity distribution and unique active powerups across all 8 fruit types:
    - *Cherry* (**Common - 20%**): **Speed Boost** (+25% Pac-Man movement speed for 6s).
    - *Strawberry* (**Common - 20%**): **Ghost Freeze** (freezes all active ghosts in place for 4s).
    - *Orange* (**Uncommon - 15%**): **Pellet Magnet** (siphons nearby pellets directly to Pac-Man for 6s).
    - *Apple* (**Uncommon - 15%**): **Energy Shield** (absorbs 1 lethal ghost hit).
    - *Melon* (**Rare - 9%**): **Double Bounty** (2x score multiplier for all points for 8s).
    - *Galaxian Flagship* (**Rare - 9%**): **Tractor Repel** (returns active ghosts to house doorstep and stuns for 3s).
    - *Bell* (**Epic - 9%**): **Ghost Panic** (instantly triggers frightened vulnerability mode).
    - *Key* (**Legendary - 3%**): **Master Key** (+1 extra life and triggers an 8s Fever frenzy).
  - Fruits spawn authentically at milestone dot thresholds (when 70 and 170 dots remain in the level).
- **Custom Glitch Theme**:
  - Added a dedicated **Glitch Theme** (unlocked upon reaching Level 30) featuring dynamic chromatic RGB phase shifting and inverted channel highlights.
  - Retained the distinct **Rainbow Theme**, expanding total selectable themes to 11.
- **Level Selector Split Glitch Visuals**:
  - Level 20 selector button displays a half-normal, half-glitched visual (`[2` in classic yellow, `0]` in corrupted chromatic glitch RGB), mirroring the level's iconic split-screen maze.
  - Level 30 selector button displays fully corrupted chromatic shifting glitch glyphs.
- **Theme Info Modal in Pause Menu**:
  - Added a dedicated modal in the Pause Menu (`P` / `ESC` -> `Theme Info & Guide`), displaying active mechanics, hazards, and dual-tone palette styling tailored to the current level.
- **C++23 Modernization**:
  - Migrated the codebase and build toolchains to ISO C++23 standard (`-std=c++23`).

### Fixed
- **Ghost AI Scatter Pathfinding**: Fixed a critical pathfinding issue where corner scatter targets located inside border walls caused BFS to fail and stall ghosts; added Euclidean distance minimization fallback.
- **Terminal Alt Screen & Cursor Hiding**: Restored alternate screen buffer (`\033[?1049h\033[2J\033[H\033[?25l`) and mouse mode initialization in `TerminalSession`, eliminating blinking typing cursor artifacts and screen bleeding.
- **TUI Mouse Hover Persistence & Hit-Testing**: Fixed persistent hover state tracking so stationary hovering remains active without flickering; aligned mouse hitboxes with visual terminal columns using `displayWidth`.
- **Title Theme Consistency**: Fixed `drawTitleBorderBox` accent context scoping so all box titles render consistently in yellow in Classic and PacTerm+ themes.
- **Ghost Repel Doorway Safety**: Prevented ghosts from repelling directly onto Pac-Man when Pac-Man is near the ghost house doorstep.
- **Shield Absorption Logic**: Ensured absorbed ghost hits safely repel the attacker into the house without double-triggering death on simultaneous ghost collisions.
- **Movement Accumulator Safety**: Clamped tick accumulators to avoid multi-tile teleportation bursts following pause, freeze expiries, or lag spikes.
- **Small-Screen Viewport Bounds**: Clamped visible row/column calculations in `getViewport()` to prevent out-of-bounds rendering on compact terminals.

### Changed
- **Level & Theme Alignment**: Synchronized the 1-to-1 mapping of levels to themes, ensuring wall palettes, dot accents, selector colors, and powerups match their designated themes across all 30 levels.
- **Typography & Font Weighting**: Cleaned up box titles across all menus with unbolded titles and bolded content headers, ensuring no text collisions with container borders.
- **Nerd Fonts vs ASCII Compliance**: Eliminated all emojis across the entire rendering pipeline, using clean Nerd Font glyphs when enabled and crisp standard ASCII when disabled.
- **String View Parameters**: Modernized render and measurement APIs to take `std::string_view` to eliminate dynamic allocations in hot loops.

### Removed
- Removed legacy periodic item spam timers and obsolete state variables (`special_item_active_`, `special_item_pos_`, etc.).

---

## [v1.3.8] - 2026-08-25

### Fixed
- **Critical Map Boundary Out-of-Bounds**: Fixed a malformed 26-char row in map 17 that caused out-of-bounds template reads, broken wall rendering, and unreachable dots at `(19, 6)` and `(19, 7)`. Added compile-time `static_assert` checking that all 30 map templates are exactly 28 columns wide.
- **Wide Glyph Alignment**: Multi-column Unicode glyphs no longer misalign subsequent characters in the row; the frame composer tracks cursor positions by UTF-8 terminal display width (`wcwidth`/`seqWidth`) and reserves empty continuation cells.
- **Ghost House In-House Release**: Frightened ghosts can no longer escape the ghost house prematurely during a power pellet (in-house dot-count release invariant is now strictly enforced).
- **Frightened Flash Timer**: Frightened ghosts now flash blue/white correctly near expiry.
- **Ghost Pass-Through Collision**: Ghost swap/pass-through crossings during high-speed ticks are now caught via previous-tile intersection tracking.
- **Dash Ability Invariants**: The Dash ability (levels 17–19) now respects wall collisions, dot scoring, and ghost house door boundaries.
- **Scrolling Viewport Clamping**: Clamped scrolling viewports at the map bottom edge so lower maze corridors are never clipped on smaller terminal dimensions.
- **Input State Scoping**: Isolated keyboard shortcut capture to active gameplay and primary menu phases, avoiding interference with text input screens.
- **Signal Handlers & Terminal Restoration**: Added POSIX signal handling for `SIGINT`, `SIGTERM`, `SIGWINCH`, `SIGSEGV`, `SIGABRT`, `SIGBUS`, and `SIGFPE` to restore terminal raw mode and cursor visibility safely.

### Performance
- Replaced synchronous ESC-sequence busy-wait loops with non-blocking stream parsing.
- Refactored ANSI escape sequence generation to eliminate redundant `\033[0m` resets and avoid full-screen wipes on resize.

---

## [v1.3.7] - 2026-08-15

### Changed
- Unified the version string across all source files and CMake project definitions.
- Renamed the CMake build target to `pacterm`.
- Synchronized documentation with runtime CLI flags (`--install`, `--delete`, `--version`), sound backends, and key configuration.

### Fixed
- Mouse hover highlights no longer remain stuck after the cursor leaves a menu item.
- Procedural map generator now protects the full ghost-house region from stray wall generation.

### Performance
- Eliminated per-glyph dynamic allocations in the text renderer.
- Eliminated redundant viewport recalculations in the main frame loop.

---

## [v1.3.6] - 2026-08-01

### Added
- Comprehensive Themes reference documentation describing unlock requirements and color duality palettes.

### Changed
- Redesigned Pause menu with embedded border titles and clean left-aligned layouts.

### Fixed
- Fixed 2 unreachable dots on level 18.

---

## [v1.3.5] - 2026-07-20

### Added
- `ESC` key support to toggle pause during gameplay.

### Fixed
- Fixed lag spikes when frame rate drops by clamping delta time to prevent spiral-of-death accumulation.
- Fixed a softlock where letters spawning on top of dots could leave an uncleared dot counter.
- Updated scoring and control documentation.

### Removed
- Cleaned up dead code (unused keybinding loader, legacy `Fruit` tile, unused animation and PRNG helpers).

### Performance
- Reduced per-frame allocations in the renderer (theme palettes and glitch glyphs are cached per frame instead of rebuilt per cell).

---

## [v1.3.4] - 2026-07-05

### Added
- **Fever Time**: Chain 4 ghosts in one power pellet to trigger Fever 2x scoring with visual ghost trails.
- **Letter Hunt**: Collect `P-A-C-T-E-R-M` letters across levels to earn buffs and unlock the **PacTerm+** composite theme.
- **Dynamic Level Rating**: Cleared levels are rated 0 to 10 based on par time and death count, awarding rating bonuses.
- **Unlockable Themes**: Added dynamic two-color dual-gradient themes (Classic, Cyan, Green, Pink, Red, Violet, Ice, Amber, PacTerm+).
