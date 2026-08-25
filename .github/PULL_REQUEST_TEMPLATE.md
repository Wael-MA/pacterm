<!--
  Thank you for contributing to PacTerm!
  Please fill in the template below. See CONTRIBUTING.md for details
  on commit conventions, code style, and branch naming.
-->

## Overview

<!-- A brief summary of the changes in this pull request and the problem
     it solves. Keep it concise; implementation details belong in commits. -->

Closes #<!-- e.g. Closes #42 -->

## Type of Change

<!-- Check the boxes that apply by replacing `[ ]` with `[x]`. -->

- [ ] Bug fix (non-breaking change that fixes an issue)
- [ ] New feature (non-breaking change that adds functionality)
- [ ] Breaking change (fix or feature that breaks existing behavior)
- [ ] Refactor (no functional change)
- [ ] Performance improvement
- [ ] Documentation update
- [ ] Test / CI change
- [ ] Chore (build, cleanup, version bump)

## Testing Performed

<!-- Describe the tests you ran and how to verify your change. Include:
     - Build system used (CMake / Make / build.sh)
     - Compiler and build type (e.g. `g++ 13` / Release)
     - Any manual gameplay testing (which levels, features affected) -->

- [ ] Build passes with **`-Wall -Wextra -Wpedantic -Werror`** clean
- [ ] `clang-format --dry-run --Werror src/*.cpp src/*.hpp` passes
- [ ] `clang-tidy` static analysis passes (if run)
- [ ] Tested relevant gameplay (levels / features affected by this change)

## Checklist

<!-- Ensure the following are complete before requesting review. -->

- [ ] Code follows the project's [code style](CONTRIBUTING.md#code-style) (C++23, modern idioms)
- [ ] Changes are focused and one logical change per PR
- [ ] Commit messages follow [Conventional Commits](CONTRIBUTING.md#git-commit-conventions)
- [ ] Branch name follows the [naming standards](CONTRIBUTING.md#branch-naming-standards)
- [ ] New behavior is covered by tests (if applicable)
- [ ] Documentation updated (README, changelog) for user-facing changes
- [ ] Verified against `upstream/main` and rebased if needed

## Screenshots / Logs

<!-- If the change affects rendering, gameplay, or the UI, add screenshots
     or terminal captures here. -->

## Additional Context

<!-- Anything else the reviewers should know. Link any related issues,
     discussions, or previous attempts. -->
