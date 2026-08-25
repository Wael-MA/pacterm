// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.9
#include "GameEngine.hpp"
#include <print>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string_view arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            std::println("pacterm v{}", Config::PACTERM_VERSION);
            return 0;
        } else if (arg == "--install" || arg == "-i") {
            return GameEngine::install_bin(true) ? 0 : 1;
        } else if (arg == "--delete" || arg == "-d") {
            return GameEngine::delete_bin(true) ? 0 : 1;
        } else {
            std::println(stderr, "Usage: {} [--install | --delete | --version]", argv[0]);
            return 1;
        }
    }

    try {
        GameEngine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::println(stderr, "Fatal Exception: {}", e.what());
        return 1;
    }
    return 0;
}
