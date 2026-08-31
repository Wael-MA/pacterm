// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.4.0
#include "GameEngine.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string_view arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            std::cout << "pacterm v" << Config::PACTERM_VERSION << "\n";
            return 0;
        } else if (arg == "--install" || arg == "-i") {
            return GameEngine::install_bin(true) ? 0 : 1;
        } else if (arg == "--delete" || arg == "-d") {
            return GameEngine::delete_bin(true) ? 0 : 1;
        } else {
            std::cerr << "Usage: " << argv[0] << " [--install | --delete | --version]\n";
            return 1;
        }
    }

    try {
        GameEngine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
