# Pacterm v1.3.8

## What's Changed

### 🛠️ Cleanup & Internal Refinement

* **Version Sync:** Unified the version string across all source files, headers, and the CMake project to `1.3.8`.
* **Map Validation:** Added a compile-time `static_assert` that verifies every manual map template is exactly 28 columns wide, preventing malformed map rows from ever compiling again.

### 🐛 Bug Fixes

* **Map Generation (Critical):** Fixed a malformed row in map 17 (`MAP_TEMPLATE_17`) that was 26 characters instead of 28. The truncated row caused an out-of-bounds read of the template string, a broken wall on the right edge, and two permanently unreachable dots at (19,6) and (19,7). All 240 dots on level 17 are now reachable.
* **Rendering:** Fixed horizontal misalignment caused by wide emoji glyphs (🍒 🍏 ❤️ ✨). The frame composer now tracks cursor position by actual glyph display width and reserves empty continuation cells, so everything to the right of a wide glyph stays aligned.
* **Ghost AI:** Frightened ghosts no longer wander out of the ghost house during a power pellet — the in-house dot-count release mechanic can no longer be bypassed.
* **Ghost Behavior:** Fixed the frightened-ghost flash timer using a stale clock, so ghosts now correctly flicker blue/white during the last seconds of a fright.
* **Collision Detection:** Ghosts now track their previous tile, catching swap/pass-through crossings that exact-equality checks missed.
* **Dash Mechanic:** The dash ability (levels 17–19) no longer ignores collisions or dot scoring and can no longer pass through the ghost-house door.
* **Input Latency:** Replaced the ESC-sequence busy-wait (up to ~50 ms per byte) with a `poll()`-based timeout, cutting bare-ESC and key-response latency.
* **Viewport:** Scrolling viewport is now clamped at the bottom edge, so the map is no longer cut off when Pac-Man nears the bottom of the maze.
* **Dev Cheat:** The "dev" cheat sequence is now only captured in Playing, Main Menu, and Level Selector — it can no longer hijack the username or settings screens.
* **Terminal Safety:** Added SIGSEGV/SIGABRT/SIGBUS/SIGFPE handlers that restore the terminal to a usable state if the game crashes unexpectedly.

### ⚡ Performance

* **Input Handling:** Removed the CPU-burning busy-wait loop in the key reader in favor of efficient `poll()` waits.

---

## Installation / Update on Arch Linux

### AUR (`pacterm-bin`) 

Update your package using your preferred AUR helper:

```bash
# Using yay
yay -Syu pacterm-bin

# Using paru
paru -Syu pacterm-bin

```

### Manual Build via `PKGBUILD`

```bash
git clone https://aur.archlinux.org/pacterm.git
cd pacterm
makepkg -si

```