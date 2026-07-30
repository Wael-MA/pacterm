// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.1.3
#include "GameEngine.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--version" || arg == "-v") {
            std::cout << "pacterm v1.1.0" << std::endl;
            return 0;
        } else if (arg == "--install" || arg == "install") {
            return GameEngine::install_bin(true) ? 0 : 1;
        } else if (arg == "--delete" || arg == "delete") {
            return GameEngine::delete_bin(true) ? 0 : 1;
        } else {
            std::cout << "Usage: " << argv[0] << " [install | delete | --version]" << std::endl;
            return 1;
        }
    }
    
    try {
        GameEngine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
