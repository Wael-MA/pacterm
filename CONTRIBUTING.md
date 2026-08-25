# Contributing to PacTerm

First off, thank you for taking the time to contribute!

The following is a set of guidelines for contributing to PacTerm. These are
guidelines, not rules. Use your best judgment, and feel free to propose changes
to this document in a pull request.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Setting Up the Development Environment](#setting-up-the-development-environment)
  - [Building](#building)
  - [Running](#running)
- [Development Guidelines](#development-guidelines)
  - [Code Style](#code-style)
  - [Linting and Static Analysis](#linting-and-static-analysis)
- [How Can I Contribute?](#how-can-i-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Proposing Features](#proposing-features)
  - [Submitting a Pull Request](#submitting-a-pull-request)
- [Git Commit Conventions](#git-commit-conventions)
- [Branch Naming Standards](#branch-naming-standards)
- [Additional Notes](#additional-notes)

## Code of Conduct

This project and everyone participating in it is governed by the
[PacTerm Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are
expected to uphold this code. Please report unacceptable behavior to
**waelaz1983@gmail.com**.

## Getting Started

### Prerequisites

Before you begin, ensure you have the following installed:

- **C++23 compatible compiler**: GCC 14+, Clang 17+, or MSVC 19.38+
- **CMake** 3.16+ (recommended build system)
- **Make** (alternative build system)
- **clang-format** (for code formatting checks)
- **clang-tidy** (optional, for static analysis)
- A terminal with **Unicode/UTF-8** support

For package manager one-liners:

```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake clang-format clang-tidy

# Fedora
sudo dnf install gcc-c++ cmake clang-format clang-tools-extra

# Arch
sudo pacman -S base-devel cmake clang

# macOS
brew install cmake clang-format
```

### Setting Up the Development Environment

1. **Fork the repository** on GitHub.

2. **Clone your fork** locally:

   ```bash
   git clone https://github.com/<your-username>/pacterm.git
   cd pacterm
   ```

3. **Add the upstream remote** (to stay in sync with the main repository):

   ```bash
   git remote add upstream https://github.com/Wael-MA/pacterm.git
   git fetch upstream
   ```

4. **Create a feature branch** (see [Branch Naming Standards](#branch-naming-standards)):

   ```bash
   git checkout -b feature/your-feature-name
   ```

### Building

#### Using CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
make
```

For a debug build with assertions and symbols:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

#### Using Make

```bash
make
```

#### Using the Build Script

```bash
./build.sh
```

### Running

```bash
# Run the game
./pacterm

# Print version
./pacterm --version

# Install to PATH (Linux/macOS)
./pacterm --install
```

> **Note:** Runtime data (scores, settings, generated sounds) is stored under
> `~/.pacterm/`. If you encounter unexpected behavior, backing up or clearing
> this directory can help isolate issues.

## Development Guidelines

### Code Style

- Follow C++23 best practices and modern C++ idioms.
- Use **2-space indentation** (matching the existing codebase).
- Use `snake_case` for functions, variables, and file names; `PascalCase` for
  types; `UPPER_SNAKE_CASE` for constants.
- Maintain **`-Wall -Wextra -Wpedantic -Werror` clean builds**. The CI will
  fail on any warning.
- Keep code **self-documenting**; add comments only where the intent is not
  obvious.
- Prefer the **standard library** — PacTerm intentionally uses no external
  libraries or game engines.
- Match the style of surrounding code in the file you are editing.

### Linting and Static Analysis

The project uses **clang-format** and **clang-tidy** in CI. Run them locally
before pushing:

```bash
# Format the source (auto-fix in place)
clang-format -i src/*.cpp src/*.hpp

# Check formatting without modifying files
clang-format --dry-run --Werror src/*.cpp src/*.hpp

# Static analysis (requires CMake export of compile commands)
cmake -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy src/*.cpp -p build -- -std=c++20 -Isrc
```

## How Can I Contribute?

### Reporting Bugs

> **Before reporting a bug**, please:
> 1. Check the [existing issues](https://github.com/Wael-MA/pacterm/issues)
>    to avoid duplicates.
> 2. Check the [Discussions](https://github.com/Wael-MA/pacterm/discussions)
>    for known problems or workarounds.
> 3. Make sure you are on the latest release.

If you find a bug, open an issue using the
**[Bug Report template](.github/ISSUE_TEMPLATE/bug_report.md)**. A good bug
report includes:

- **A clear, descriptive title**.
- **Steps to reproduce** the behavior.
- **Expected behavior** vs. **actual behavior**.
- **Environment details** (OS, terminal emulator, compiler, build type,
  PacTerm version).
- **Relevant logs, screenshots, or terminal captures** (ANSI/Unicode rendering
  issues are especially valuable with a screenshot).

### Proposing Features

We welcome new feature suggestions! Before proposing:

1. Search [existing issues](https://github.com/Wael-MA/pacterm/issues) and
   [Discussions](https://github.com/Wael-MA/pacterm/discussions) for prior
   requests.
2. Open an issue using the
   **[Feature Request template](.github/ISSUE_TEMPLATE/feature_request.md)**,
   describing the problem you want to solve, your proposed solution, and any
   alternatives you considered.
3. If the feature is substantial (new mechanics, major refactors), consider
   discussing it first in Discussions before writing code.

### Submitting a Pull Request

1. Ensure your fork is up to date with `upstream/main`:

   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

2. **Keep changes focused** — one logical change per pull request. Large
   features should be split into reviewable commits.
3. **Write tests** for new features or bug fixes.
4. **Update documentation** for user-facing changes (README, keybindings,
   changelog).
5. **Format your code** and run the linters (see
   [Linting and Static Analysis](#linting-and-static-analysis)).
6. Verify the build is warning-free with **CMake** and, if feasible, **Make**.
7. Push your branch and open a PR using the
   **[Pull Request template](.github/PULL_REQUEST_TEMPLATE.md)**, linking any
   related issues.

Your PR must pass CI (build, `clang-format`, and `clang-tidy` checks) before it
can be merged.

## Git Commit Conventions

PacTerm uses **Conventional Commits** to keep the history readable and
searchable. Commit messages must follow this format:

```
<type>(<scope>): <subject>

<body>            (optional)
<footer>          (optional)
```

**Types:**

| Type       | Description                                   |
|------------|-----------------------------------------------|
| `feat`     | A new feature                                |
| `fix`      | A bug fix                                    |
| `docs`     | Documentation-only changes                   |
| `style`    | Code style/formatting (no behavior change)   |
| `refactor` | Code change that neither fixes a bug nor adds a feature |
| `perf`     | Performance improvements                     |
| `test`     | Adding or fixing tests                       |
| `build`    | Build system or dependency changes           |
| `ci`       | CI configuration changes                    |
| `chore`    | Other maintenance, cleanup, version bumps    |

**Examples:**

```text
feat: add letter buffs to the letter hunt

fix(renderer): eliminate per-glyph allocations

perf: reduce frame lag when frame rate drops

docs: describe every unlockable theme

chore: bump version to 1.3.7
```

**Guidelines:**

- Use the **imperative mood** ("add", "fix", "remove" — not "added", "fixes").
- Keep the subject line under **50 characters**; wrap the body at 72.
- Use the subject to state what you changed and the body to explain *why*.
- Reference issues in the footer, e.g. `Fixes #42` or `Closes #42`.
- **Do not** reference issue numbers in the subject line.

## Branch Naming Standards

Branches must use a short type prefix followed by a `kebab-case` description:

| Type        | Prefix            | Example                          |
|-------------|-------------------|----------------------------------|
| Feature     | `feature/`        | `feature/letter-hunt`            |
| Bug fix     | `fix/`            | `fix/unreachable-dot-level-18`   |
| Refactor    | `refactor/`       | `refactor/renderer-palette`      |
| Performance | `perf/`           | `perf/reduce-frame-lag`          |
| Docs        | `docs/`           | `docs/themes-reference`          |
| Chore       | `chore/`          | `chore/bump-version`             |

Examples:

```bash
git checkout -b fix/ghost-softlock
git checkout -b feature/dash-mechanic
```

## Additional Notes

- **Project structure** overview is available in the [README](README.md#project-structure).
- Questions about gameplay, scoring, or themes? See the [README](README.md) first.
- GitHub **issues** are for bug reports and feature requests; **Discussions**
  are for questions, ideas, and general chatter.
- The project is licensed under the **GPL-3.0** — by contributing, you agree to
  license your contributions under the same terms.

---

Thanks for helping make PacTerm better!
