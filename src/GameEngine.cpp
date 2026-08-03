// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.1
#include "GameEngine.hpp"
#include <unistd.h>
#include <cerrno>
#include <sys/ioctl.h>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <queue>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <sstream>
#include <csignal>

// Static signal handler state
bool GameEngine::signal_term_restored_ = false;
struct termios GameEngine::signal_original_termios_{};

namespace {
// Terminal escape sequences. Storing as string_views kills the fragile
// "magic byte length" arguments formerly passed to ::write().
constexpr std::string_view kEnterAltScreen = "\033[?1049h\033[2J\033[H\033[?25l";
constexpr std::string_view kEnterMouse     = "\033[?1000h\033[?1002h\033[?1003h\033[?1006h";
constexpr std::string_view kLeaveAltScreen = "\033[?25h\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[?1049l";
}

// Sanitize a key code read from disk into a value the keybinding map can
// actually hold (printable chars / 0x1A, plus our synthetic ANSI codes).
static int clampKeyCode(int k) noexcept {
    if (k >= ' ' && k <= '~') return k;
    if (k == '\n' || k == '\r') return k;
    if (k == 27 || k == 127) return k;
    if (k >= 1000 && k <= 1005) return k;
    return 0; // unknown/corrupt -> disallowed
}

void GameEngine::restoreTerminalForSignal() {
    if (signal_term_restored_) return;
    signal_term_restored_ = true;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &signal_original_termios_);
    (void)::write(STDOUT_FILENO, kLeaveAltScreen.data(), kLeaveAltScreen.size());
}

void GameEngine::signalHandler(int sig) {
    if (sig == SIGTSTP) {
        // Suspend cleanly: restore the cooked terminal so the shell regains
        // control, then re-raise with the default handler to actually stop. On
        // resume (SIGCONT) signalContHandler() re-enters raw mode.
        restoreTerminalForSignal();
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTSTP, &sa, nullptr);
        raise(SIGTSTP);
        return; // reached only if the process is resumed and raises returns
    }
    restoreTerminalForSignal();
    _exit(128 + sig);
}

void GameEngine::signalContHandler(int) {
    // Re-enter raw mode exactly as enableRawMode() does, then restore the
    // alternate screen / cursor / mouse tracking that the TSTP handler tore down.
    if (tcgetattr(STDIN_FILENO, &signal_original_termios_) >= 0) {
        struct termios raw = signal_original_termios_;
        raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    }
    (void)::write(STDOUT_FILENO, kEnterAltScreen.data(), kEnterAltScreen.size());
    (void)::write(STDOUT_FILENO, kEnterMouse.data(), kEnterMouse.size());
    signal_term_restored_ = false;
    // Re-arm the handlers that the TSTP handler downgraded to SIG_DFL before
    // re-raising; otherwise a second Ctrl-Z would stop with a raw terminal.
    installSignals();
}

void GameEngine::installSignals() {
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // avoid EINTR loops on non-blocking readKey()
    sa.sa_handler = signalHandler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGTSTP, &sa, nullptr);
    sa.sa_handler = signalContHandler;
    sigaction(SIGCONT, &sa, nullptr);
}

// Static BFS Next Direction Pathfinding Helper
//
// Returns the first step Ghost should take towards `target`, or Direction::None.
// Bounds- and wall-safe; unclamped (out-of-map) targets simply yield None, so
// callers must clamp targets via clampToMap().
static Direction bfsNextDirection(
    const Map& map,
    Vec2 start,
    Vec2 target,
    Direction forbidden_direction
) {
    if (start == target) return Direction::None;

    // Fixed-capacity ring buffer: the grid holds MAP_WIDTH*MAP_HEIGHT nodes,
    // and every node is enqueued at most once, so no dynamic allocation is
    // ever needed on the hot movement path.
    struct QueueNode {
        Vec2 pos;
        Direction firstStep;
    };
    std::array<QueueNode, Config::MAP_WIDTH * Config::MAP_HEIGHT> queue{};
    std::array<bool, Config::MAP_WIDTH * Config::MAP_HEIGHT> visited{};

    auto inBounds = [](Vec2 pos) {
        return pos.x >= 0 && pos.x < Config::MAP_WIDTH &&
               pos.y >= 0 && pos.y < Config::MAP_HEIGHT;
    };
    auto getVisited = [&](Vec2 pos) -> bool {
        return !inBounds(pos) || visited[pos.y * Config::MAP_WIDTH + pos.x];
    };
    auto setVisited = [&](Vec2 pos) {
        if (inBounds(pos)) {
            visited[pos.y * Config::MAP_WIDTH + pos.x] = true;
        }
    };

    size_t head = 0;
    size_t tail = 0;

    auto push = [&](Vec2 pos, Direction first) {
        if (tail < queue.size()) {
            queue[tail++] = QueueNode{pos, first};
        }
    };

    setVisited(start);

    // Check initial steps
    for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
        if (d == forbidden_direction) continue;
        Vec2 next = map.wrapTunnel(start + directionToVec2(d));
        if (getVisited(next)) continue;
        if (!map.isWalkableByGhost(next)) continue;
        if (next == target) return d;
        setVisited(next);
        push(next, d);
    }

    while (head < tail) {
        QueueNode current = queue[head++];

        for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
            Vec2 next = map.wrapTunnel(current.pos + directionToVec2(d));
            if (getVisited(next)) continue;
            if (!map.isWalkableByGhost(next)) continue;
            if (next == target) return current.firstStep;
            setVisited(next);
            push(next, current.firstStep);
        }
    }

    return Direction::None;
}

// Mode Waves Constants for Classic Pac-Man AI state alternation
struct ModeWave {
    GhostMode mode;
    int duration_ms;  // -1 means infinite duration
};

static constexpr std::array<ModeWave, 8> MODE_WAVES = {{
    {GhostMode::Scatter, 7000},
    {GhostMode::Chase,   20000},
    {GhostMode::Scatter, 7000},
    {GhostMode::Chase,   20000},
    {GhostMode::Scatter, 5000},
    {GhostMode::Chase,   20000},
    {GhostMode::Scatter, 5000},
    {GhostMode::Chase,   -1}
}};

GameEngine::GameEngine() 
    : ghosts_{
        Ghost{GhostPersonality::Blinky},
        Ghost{GhostPersonality::Pinky},
        Ghost{GhostPersonality::Inky},
        Ghost{GhostPersonality::Clyde}
      } 
{
    // Check environment variable for Nerd Fonts toggle
    const char* env_nf = std::getenv("PACMAN_NERD_FONTS");
    if (env_nf && std::string(env_nf) == "0") {
        use_nerd_fonts_ = false;
    }

    // Seed the two RNG sources once so gameplay/item timing is not identical
    // across runs. (std::rand() is used by legacy call sites.)
    unsigned seed = static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::srand(seed);
    rng_.seed(seed);

    enableRawMode();
    installSignals();
    // Hide cursor, alternate screen, clear screen, enable SGR mouse tracking
    (void)::write(STDOUT_FILENO, kEnterAltScreen.data(), kEnterAltScreen.size());
    (void)::write(STDOUT_FILENO, kEnterMouse.data(), kEnterMouse.size());
    queryTerminalSize();
    initRenderer();
    try {
        loadHighScore();
    } catch (...) { /* never let a corrupt score file corrupt the terminal */ }
    rebuildKeybindings();
    // Sound synthesis touches the filesystem and can throw; keep the ctor
    // exception-free so the terminal is ALWAYS restored via ~GameEngine().
    try {
        generateSounds();
    } catch (...) { /* e.g. EACCES on a read-only install dir */ }
    startMainMenu();
    fade_animation_.fadeIn({255, 255, 255}, 400); // Boot fade-in
}

GameEngine::~GameEngine() {
    // Show cursor, disable mouse tracking, return to main buffer, restore original terminal settings
    (void)::write(STDOUT_FILENO, kLeaveAltScreen.data(), kLeaveAltScreen.size());
    disableRawMode();
    
    std::string message = "Thanks for playing pacterm by Wael!";
    for (size_t i = 0; i < message.length(); ++i) {
        Color c;
        if (selected_general_theme_ == 0) {
            double ratio = static_cast<double>(i) / (message.length() - 1);
            c.r = static_cast<uint8_t>(255 - ratio * 255);
            c.g = static_cast<uint8_t>(100 + ratio * 155);
            c.b = 255;
        } else if (selected_general_theme_ == 5) {
            c = getRainbowColor(static_cast<double>(i) * 0.15);
        } else {
            c = applyGeneralTheme({255, 255, 255}, 0, static_cast<int>(i) * 2);
        }
        std::printf("\033[38;2;%d;%d;%dm%c", c.r, c.g, c.b, message[i]);
    }
    std::printf("\033[0m\n");
    std::fflush(stdout);
}

void GameEngine::enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &original_termios_) < 0) return;
    signal_original_termios_ = original_termios_;
    struct termios raw = original_termios_;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) >= 0) {
        raw_mode_enabled_ = true;
    }
}

void GameEngine::disableRawMode() {
    if (raw_mode_enabled_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios_);
        raw_mode_enabled_ = false;
    }
}

void GameEngine::queryTerminalSize() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) >= 0) {
        term_size_ = {ws.ws_col, ws.ws_row};
    } else {
        term_size_ = {80, 24};
    }
}

int GameEngine::readKey() {
    char c = 0;
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    if (n <= 0) return -1;
    
    if (c == 27) { // ESC sequence prefix
        auto read_char_timeout = [](char& out_c) -> bool {
            int retries = 0;
            while (retries < 1000) {
                ssize_t nr = ::read(STDIN_FILENO, &out_c, 1);
                if (nr > 0) return true;
                if (nr == 0 || (nr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                    usleep(50);
                    retries++;
                } else {
                    return false;
                }
            }
            return false;
        };
        
        char seq[2];
        if (!read_char_timeout(seq[0])) return 27;
        if (!read_char_timeout(seq[1])) return 27;
        
if (seq[0] == '[') {
                if (seq[1] == '<') {
                    // SGR mouse: \033[<btn;X;YM (press) or \033[<btn;X;Ym (release)
                    std::string params;
                    params.reserve(32);
                    bool valid = false;
                    char term = 0;
                    while (params.size() < 32) { // cap: reject garbage/infinite sequences
                        char ch;
                        if (!read_char_timeout(ch)) break;
                        if (ch == 'M' || ch == 'm') { term = ch; valid = true; break; }
                        params += ch;
                    }
                if (valid) {
                    int btn = 0, x = 0, y = 0;
                    if (std::sscanf(params.c_str(), "%d;%d;%d", &btn, &x, &y) >= 3) {
                        mouse_button_ = btn;
                        mouse_x_ = x - 1;  // SGR is 1-indexed, convert to 0-indexed
                        mouse_y_ = y - 1;
                        mouse_press_ = (term == 'M');
                        if (btn & 32) {
                            // Motion event (no button held): track hover, not a click
                            mouse_hover_active_ = true;
                            return 1005; // Mouse motion
                        }
                        if (mouse_press_) return 1004; // Mouse click
                        return -1; // Release events: ignore
                    }
                }
                return -1;
            }
            switch (seq[1]) {
                case 'A': return 1000; // Arrow Up
                case 'B': return 1001; // Arrow Down
                case 'C': return 1002; // Arrow Right
                case 'D': return 1003; // Arrow Left
            }
        }
        return 27;
    }
    return c;
}

void GameEngine::handleInput(int key) {
    afk_timer_ = 0;
    if (phase_ == GamePhase::Screensaver) {
        phase_ = GamePhase::Paused;
        pause_menu_selection_ = 0;
        return;
    }
    
    // Dev code rolling buffer detection
    if (key >= 32 && key <= 126 && phase_ != GamePhase::DevPasswordInput && phase_ != GamePhase::DevMenu && phase_ != GamePhase::RedeemInput) {
        char c = std::tolower(static_cast<char>(key));
        dev_input_sequence_ += c;
        if (dev_input_sequence_.length() > 3) {
            dev_input_sequence_ = dev_input_sequence_.substr(dev_input_sequence_.length() - 3);
        }
        if (dev_input_sequence_ == "dev") {
            pre_dev_phase_ = phase_;
            phase_ = GamePhase::DevPasswordInput;
            dev_password_buffer_ = "";
            dev_input_sequence_ = "";
            return;
        }
    }
    
    if (key == 'm' || key == 'M') {
        muted_ = !muted_;
        saveHighScore();
        return;
    }

    if (key == 1005) {
        // Mouse motion: hover state already tracked, nothing else to do
        return;
    }

    if (key == 1004 && mouse_press_) {
        handleMouseClick();
        return;
    }

    mouse_hover_active_ = false;

    GameAction action = GameAction::None;
    auto it = key_to_action_.find(key);
    if (it != key_to_action_.end()) {
        action = it->second;
    }
    
    Direction dir = Direction::None;
    switch (action) {
        case GameAction::Up:    dir = Direction::Up; break;
        case GameAction::Down:  dir = Direction::Down; break;
        case GameAction::Left:  dir = Direction::Left; break;
        case GameAction::Right: dir = Direction::Right; break;
        default: break;
    }
    
    switch (phase_) {
        case GamePhase::MainMenu:
            if (action == GameAction::Up) {
                main_menu_selection_ = (main_menu_selection_ - 1 + 7) % 7;
            } else if (action == GameAction::Down) {
                main_menu_selection_ = (main_menu_selection_ + 1) % 7;
            } else if (key == '\n' || key == '\r' || key == ' ') {
                if (main_menu_selection_ == 0) {
                    phase_ = GamePhase::LevelSelector;
                    level_select_cursor_ = 0;
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                } else if (main_menu_selection_ == 1) {
                    phase_ = GamePhase::UsernameInput;
                    input_username_ = username_;
                } else if (main_menu_selection_ == 2) {
                    phase_ = GamePhase::Stats;
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                } else if (main_menu_selection_ == 3) {
                    phase_ = GamePhase::RedeemInput;
                    redeem_input_ = "";
                    redeem_result_ = "";
                } else if (main_menu_selection_ == 4) {
                    if (isInstalledLocally()) {
                        if (delete_bin()) {
                            main_menu_message_ = "Removed successfully!";
                        } else {
                            main_menu_message_ = "Removal failed!";
                        }
                    } else {
                        if (install_bin()) {
                            main_menu_message_ = "Installed successfully!";
                        } else {
                            main_menu_message_ = "Installation failed!";
                        }
                    }
                    main_menu_msg_timer_ = 3000;
                } else if (main_menu_selection_ == 5) {
                    phase_ = GamePhase::Settings;
                    settings_selection_ = 0;
                    main_menu_message_ = "";
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                } else if (main_menu_selection_ == 6) {
                    running_ = false;
                }
            }
            break;
            
        case GamePhase::Settings:
            if (key == 27) { // ESC to go back
                phase_ = GamePhase::MainMenu;
                main_menu_message_ = "";
                fade_animation_.fadeIn({255, 255, 255}, 300);
            } else if (action == GameAction::Up) {
                settings_selection_ = (settings_selection_ - 1 + 7) % 7;
            } else if (action == GameAction::Down) {
                settings_selection_ = (settings_selection_ + 1) % 7;
            } else if (action == GameAction::Left) {
                if (settings_selection_ == 0) { // General Theme
                    int temp = selected_general_theme_;
                    do {
                        temp = (temp - 1 + 6) % 6;
                    } while (isColorLocked(temp));
                    selected_general_theme_ = temp;
                    saveHighScore();
                } else if (settings_selection_ == 3) { // Pac-Man Theme
                    int temp = selected_pacman_color_;
                    do {
                        temp = (temp - 1 + 6) % 6;
                    } while (isColorLocked(temp));
                    selected_pacman_color_ = temp;
                    saveHighScore();
                }
            } else if (action == GameAction::Right) {
                if (settings_selection_ == 0) { // General Theme
                    int temp = selected_general_theme_;
                    do {
                        temp = (temp + 1) % 6;
                    } while (isColorLocked(temp));
                    selected_general_theme_ = temp;
                    saveHighScore();
                } else if (settings_selection_ == 3) { // Pac-Man Theme
                    int temp = selected_pacman_color_;
                    do {
                        temp = (temp + 1) % 6;
                    } while (isColorLocked(temp));
                    selected_pacman_color_ = temp;
                    saveHighScore();
                }
            } else if (key == '\n' || key == '\r' || key == ' ') {
                activateSettingsSelection();
            }
            break;
            
        case GamePhase::Stats:
            if (key == 27 || key == '\n' || key == '\r' || key == ' ') { // any to return
                phase_ = GamePhase::MainMenu;
                fade_animation_.fadeIn({255, 255, 255}, 300);
            }
            break;
            
        case GamePhase::LevelSelector:
            if (action == GameAction::Up) {
                if (level_select_cursor_ == 20) {
                    level_select_cursor_ = 17; // Middle bottom of 4x5 grid
                } else if (level_select_cursor_ >= 5) {
                    level_select_cursor_ -= 5;
                } else {
                    level_select_cursor_ = 20; // Go to Back Button
                }
            } else if (action == GameAction::Down) {
                if (level_select_cursor_ == 20) {
                    level_select_cursor_ = 2;  // Go to top row grid
                } else if (level_select_cursor_ + 5 < 20) {
                    level_select_cursor_ += 5;
                } else {
                    level_select_cursor_ = 20; // Go to Back Button
                }
            } else if (action == GameAction::Left) {
                if (level_select_cursor_ < 20) {
                    if (level_select_cursor_ % 5 > 0) {
                        level_select_cursor_ -= 1;
                    }
                }
            } else if (action == GameAction::Right) {
                if (level_select_cursor_ < 20) {
                    if (level_select_cursor_ % 5 < 4) {
                        level_select_cursor_ += 1;
                    }
                }
            } else if (key == '\n' || key == '\r' || key == ' ') {
                if (level_select_cursor_ < 20) {
                    int lvl = level_select_cursor_ + 1;
                    if (lvl <= max_unlocked_level_) {
                        score_ = 0;
                        lives_ = Config::INITIAL_LIVES;
                        level_ = lvl;
                        games_played_++;
                        saveHighScore();
                        
                        acid_trails_.clear();
                        lava_tiles_.clear();
                        
                        startLevel(level_);
                        fade_animation_.fadeIn({255, 255, 255}, 300);
                        startGetReady();
                    }
                } else if (level_select_cursor_ == 20) {
                    phase_ = GamePhase::MainMenu;
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                }
            } else if (key == 27) { // ESC key
                phase_ = GamePhase::MainMenu;
                fade_animation_.fadeIn({255, 255, 255}, 300);
            }
            break;
            
        case GamePhase::UsernameInput:
            if (key == 27) { // ESC to cancel
                phase_ = GamePhase::MainMenu;
            } else if (key == '\n' || key == '\r') {
                if (!input_username_.empty()) {
                    // Truncate spaces at ends to be clean
                    username_ = input_username_;
                    saveHighScore();
                }
                phase_ = GamePhase::MainMenu;
            } else if (key == 127 || key == 8) {
                if (!input_username_.empty()) {
                    input_username_.pop_back();
                }
            } else if (key >= 32 && key <= 126) {
                if (input_username_.length() < 15) {
                    input_username_ += static_cast<char>(key);
                }
            }
            break;
            
        case GamePhase::RedeemInput:
            if (key == 27) { // ESC to cancel
                phase_ = GamePhase::MainMenu;
            } else if (key == '\n' || key == '\r') {
                std::string code;
                for (char c : redeem_input_) {
                    code += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                if (code == "RAINBOW") {
                    unlocked_rainbow_ = true;
                    saveHighScore();
                    redeem_result_ = "CODE REDEEMED";
                    redeem_result_valid_ = true;
                } else {
                    redeem_result_ = "INVALID CODE";
                    redeem_result_valid_ = false;
                }
            } else if (key == 127 || key == 8) {
                if (!redeem_input_.empty()) {
                    redeem_input_.pop_back();
                }
            } else if (key >= 32 && key <= 126) {
                if (redeem_input_.length() < 20) {
                    redeem_input_ += static_cast<char>(key);
                }
            }
            break;
            
        case GamePhase::KeyConfig:
            if (is_binding_) {
                if (key == 27) { // ESC to cancel
                    is_binding_ = false;
                } else {
                    if (binding_action_ == GameAction::Up) custom_key_up_ = key;
                    else if (binding_action_ == GameAction::Down) custom_key_down_ = key;
                    else if (binding_action_ == GameAction::Left) custom_key_left_ = key;
                    else if (binding_action_ == GameAction::Right) custom_key_right_ = key;
                    else if (binding_action_ == GameAction::Pause) custom_key_pause_ = key;
                    
                    is_binding_ = false;
                    saveHighScore();
                    rebuildKeybindings();
                }
            } else {
                if (action == GameAction::Up) {
                    key_config_selection_ = (key_config_selection_ - 1 + 6) % 6;
                } else if (action == GameAction::Down) {
                    key_config_selection_ = (key_config_selection_ + 1) % 6;
                } else if (key == '\n' || key == '\r' || key == ' ') {
                    if (key_config_selection_ == 5) { // Save & Back
                        phase_ = GamePhase::Settings;
                    } else {
                        is_binding_ = true;
                        if (key_config_selection_ == 0) binding_action_ = GameAction::Up;
                        else if (key_config_selection_ == 1) binding_action_ = GameAction::Down;
                        else if (key_config_selection_ == 2) binding_action_ = GameAction::Left;
                        else if (key_config_selection_ == 3) binding_action_ = GameAction::Right;
                        else if (key_config_selection_ == 4) binding_action_ = GameAction::Pause;
                    }
                } else if (key == 27) { // ESC to go back
                    phase_ = GamePhase::Settings;
                }
            }
            break;
            
        case GamePhase::Playing:
            if (dir != Direction::None) {
                pacman_.requestedDirection = dir;
            }
            if (action == GameAction::Pause) {
                phase_ = GamePhase::Paused;
                pause_menu_selection_ = 0;
            }
            
            // Dash (Theme 5 - Level 17-19)
            if (key == ' ' && level_ >= 17 && level_ <= 19 && dash_cooldown_ <= 0) {
                dash_cooldown_ = 3000;
                playSound("sounds/eat_ghost.wav");
                Vec2 dir_vec = {0, 0};
                if (pacman_.currentDirection == Direction::Up) dir_vec = {0, -1};
                else if (pacman_.currentDirection == Direction::Down) dir_vec = {0, 1};
                else if (pacman_.currentDirection == Direction::Left) dir_vec = {-1, 0};
                else if (pacman_.currentDirection == Direction::Right) dir_vec = {1, 0};
                
                for (int i = 0; i < 3; ++i) {
                    Vec2 next_pos = { pacman_.position.x + dir_vec.x, pacman_.position.y + dir_vec.y };
                    if (map_.getTile(next_pos) != TileType::Wall) {
                        pacman_.position = next_pos;
                        spawnParticleBurst(pacman_.position, {255, 215, 0});
                    } else {
                        break;
                    }
                }
            }
            break;
            
        case GamePhase::Paused:
            if (action == GameAction::Pause || key == 27 /* Esc */) {
                phase_ = GamePhase::Playing;
            } else if (action == GameAction::Up) {
                pause_menu_selection_ = (pause_menu_selection_ - 1 + 4) % 4;
            } else if (action == GameAction::Down) {
                pause_menu_selection_ = (pause_menu_selection_ + 1) % 4;
            } else if (key == '\n' || key == '\r' || key == ' ') {
                if (pause_menu_selection_ == 0) {
                    phase_ = GamePhase::Playing;
                } else if (pause_menu_selection_ == 1) {
                    startLevel(level_);
                    startGetReady();
                } else if (pause_menu_selection_ == 2) {
                    phase_ = GamePhase::MainMenu;
                } else if (pause_menu_selection_ == 3) {
                    running_ = false;
                }
            }
            break;

        case GamePhase::DevPasswordInput:
            if (key == 27) { // ESC key
                phase_ = pre_dev_phase_;
            } else if (key == '\n' || key == '\r') {
                std::string upper_pw = dev_password_buffer_;
                for (auto& ch : upper_pw) ch = std::toupper(ch);
                
                if (upper_pw == "WARCH") {
                    phase_ = GamePhase::DevMenu;
                    dev_menu_selection_ = 0;
                } else {
                    phase_ = pre_dev_phase_;
                }
                dev_password_buffer_ = "";
            } else if (key == 127 || key == 8) { // Backspace
                if (!dev_password_buffer_.empty()) {
                    dev_password_buffer_.pop_back();
                }
            } else if (key >= 32 && key <= 126) {
                if (dev_password_buffer_.length() < 20) {
                    dev_password_buffer_ += static_cast<char>(key);
                }
            }
            break;

        case GamePhase::DevMenu:
            if (key == 27) { // ESC key
                phase_ = pre_dev_phase_;
            } else if (action == GameAction::Up) {
                dev_menu_selection_ = (dev_menu_selection_ - 1 + 10) % 10;
            } else if (action == GameAction::Down) {
                dev_menu_selection_ = (dev_menu_selection_ + 1) % 10;
            } else if (action == GameAction::Left || action == GameAction::Right) {
                int dir = (action == GameAction::Right) ? 1 : -1;
                switch (dev_menu_selection_) {
                    case 0: // Level ID
                        level_ = std::clamp(level_ + dir, 1, 20);
                        if (pre_dev_phase_ == GamePhase::Playing || pre_dev_phase_ == GamePhase::Paused) {
                            startLevel(level_);
                        }
                        break;
                    case 1: // Pac-Man Color
                        selected_pacman_color_ = (selected_pacman_color_ + dir + 6) % 6;
                        saveHighScore();
                        break;
                    case 2: // Lives
                        lives_ = std::clamp(lives_ + dir, 1, 66);
                        break;
                    case 3: // Score
                        score_ = std::max(0, score_ + dir * 1000);
                        break;
                    case 4: // Rainbow Unlocked
                        unlocked_rainbow_ = !unlocked_rainbow_;
                        saveHighScore();
                        break;
                    case 5: // Immortality
                        immortal_ = !immortal_;
                        break;
                    case 6: // Freeze Ghosts
                        cheat_freeze_ghosts_ = !cheat_freeze_ghosts_;
                        break;
                    case 7: // Super Speed
                        cheat_super_speed_ = !cheat_super_speed_;
                        break;
                    case 8: // Skip Level
                        break;
                    default:
                        break;
                }
            } else if (key == '\n' || key == '\r' || key == ' ') {
                if (dev_menu_selection_ == 4) {
                    unlocked_rainbow_ = !unlocked_rainbow_;
                    saveHighScore();
                } else if (dev_menu_selection_ == 5) {
                    immortal_ = !immortal_;
                } else if (dev_menu_selection_ == 6) {
                    cheat_freeze_ghosts_ = !cheat_freeze_ghosts_;
                } else if (dev_menu_selection_ == 7) {
                    cheat_super_speed_ = !cheat_super_speed_;
                } else if (dev_menu_selection_ == 8) {
                    if (pre_dev_phase_ == GamePhase::Playing || pre_dev_phase_ == GamePhase::Paused) {
                        int points = 0;
                        for (int y = 0; y < Config::MAP_HEIGHT; ++y) {
                            for (int x = 0; x < Config::MAP_WIDTH; ++x) {
                                TileType t = map_.getTile(x, y);
                                if (t == TileType::Dot) points += Config::SCORE_DOT;
                                else if (t == TileType::PowerPellet) points += Config::SCORE_POWER_PELLET;
                                if (t == TileType::Dot || t == TileType::PowerPellet) {
                                    map_.setTile({x, y}, TileType::Empty);
                                }
                            }
                        }
                        addScore(points);
                        startLevelClear();
                        phase_ = GamePhase::LevelClear;
                    }
                } else if (dev_menu_selection_ == 9) {
                    phase_ = pre_dev_phase_;
                }
            }
            break;
            
        case GamePhase::GameOver:
            if (key == '\n' || key == '\r' || key == ' ') {
                score_ = 0;
                lives_ = Config::INITIAL_LIVES;
                level_ = 1;
                immortal_ = false;
                map_.reset();
                startMainMenu();
            }
            break;
            
        default:
            break;
    }
}

void GameEngine::handleMouseClick() {
    click_feedback_timer_ = 200;
    // Trigger fade animation for menu selection
    fade_animation_.fadeIn({255, 255, 255}, 300); // 300ms fade in
    
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    int mx = mouse_x_;
    int my = mouse_y_;
    
    auto hitText = [](int row, int col, const std::string& text, int my, int mx) -> bool {
        return my == row && mx >= col && mx < col + (int)text.size();
    };
    
    switch (phase_) {
        case GamePhase::MainMenu: {
            bool big = render_width_ >= 66 && render_height_ >= 22;

            std::string install_text = isInstalledLocally() ? "Uninstall pacterm" : "Install pacterm";
            std::string user_text = big ? ("Username:       " + username_) : ("User: " + username_);

            std::array<std::string, 7> opts = {
                "  Start Game",
                "  " + user_text,
                "  Stats",
                "  Redeem Code",
                "  " + install_text,
                "  Settings",
                "  Quit"
            };

                        // Fixed column, no centering (matches render block_left)
            int col_start = big ? (center_col - 13) : (center_col - 10);

            for (int i = 0; i < 7; ++i) {
                int row = center_row - 2 + i;
                if (hitText(row, col_start, opts[i], my, mx)) {
                    main_menu_selection_ = i;
                    switch (i) {
                        case 0: phase_ = GamePhase::LevelSelector; level_select_cursor_ = 0; break;
                        case 1: phase_ = GamePhase::UsernameInput; input_username_ = username_; break;
                        case 2: phase_ = GamePhase::Stats; break;
                        case 3: phase_ = GamePhase::RedeemInput; redeem_input_ = ""; redeem_result_ = ""; break;
                        case 4:
                            if (isInstalledLocally()) {
                                if (delete_bin()) main_menu_message_ = "Removed successfully!";
                                else main_menu_message_ = "Removal failed!";
                            } else {
                                if (install_bin()) main_menu_message_ = "Installed successfully!";
                                else main_menu_message_ = "Installation failed!";
                            }
                            main_menu_msg_timer_ = 3000;
                            break;
                        case 5: phase_ = GamePhase::Settings; settings_selection_ = 0; main_menu_message_ = ""; break;
                        case 6: running_ = false; break;
                    }
                    return;
                }
            }
            break;
        }
        case GamePhase::Settings: {
            bool big = render_width_ >= 66 && render_height_ >= 26;
            int col_start = big ? (center_col - 16) : (center_col - 13);

            std::string nf_text = "Nerd Fonts:     " + std::string(use_nerd_fonts_ ? "ON" : "OFF");
            std::string sound_text = "Sound:          " + std::string(muted_ ? "OFF" : "ON");
            const std::array<std::string, 6> theme_names = {"Classic Yellow", "Cyan", "Green", "Pink", "Red", "Rainbow"};
            std::string theme_text = big ? "General Theme:  " + theme_names[selected_general_theme_]
                                         : "Theme: " + theme_names[selected_general_theme_];
            std::string pm_text = "Pac-Man Theme:  " + theme_names[selected_pacman_color_];

            std::array<std::string, 7> opts = {
                "  " + theme_text,
                "  " + nf_text,
                "  " + sound_text,
                "  " + pm_text,
                "  Configure Keys",
                "  Reset",
                "  Back to Menu"
            };

            for (int i = 0; i < 7; ++i) {
                int row = center_row - 3 + i;
                if (hitText(row, col_start, opts[i], my, mx)) {
                    settings_selection_ = i;
                    activateSettingsSelection();
                    return;
                }
            }
            break;
        }
        case GamePhase::LevelSelector: {
            // 4x5 level grid — each label is 8 chars: "  [01]  " or "> [01] <"
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 5; ++c) {
                    int lvl = r * 5 + c + 1;
                    int row = center_row - 6 + r * 2;
                    int col = center_col - 22 + c * 11;
                    if (my == row && mx >= col - 4 && mx < col + 4) {
                        if (lvl <= max_unlocked_level_) {
                            score_ = 0; lives_ = Config::INITIAL_LIVES; level_ = lvl;
                            games_played_++; saveHighScore();
                            acid_trails_.clear(); lava_tiles_.clear();
                            startLevel(level_); startGetReady();
                        }
                        return;
                    }
                }
            }
            // Back button
            int row_back = center_row + 2;
            std::string back_text = "  [ BACK TO MENU ]  ";
            int back_col_start = center_col - (int)back_text.length() / 2;
            if (hitText(row_back, back_col_start, back_text, my, mx)) {
                phase_ = GamePhase::MainMenu;
                return;
            }
            break;
        }
        case GamePhase::Paused: {
            std::array<std::string, 4> opts = {"  Resume Game", "  Restart Level", "  Return to Menu", "  Quit Game"};
            for (int i = 0; i < 4; ++i) {
                int row = center_row - 1 + i;
                if (hitText(row, center_col - 8, opts[i], my, mx)) {
                    if (i == 0) { phase_ = GamePhase::Playing; }
                    else if (i == 1) {
                        map_.reset(); score_ = 0; lives_ = Config::INITIAL_LIVES;
                        acid_trails_.clear(); lava_tiles_.clear(); popups_.clear(); particles_.clear();
                        startLevel(level_); startGetReady();
                    } else if (i == 2) {
                        startMainMenu();
                    } else {
                        running_ = false;
                    }
                    return;
                }
            }
            break;
        }
        case GamePhase::KeyConfig: {
            if (is_binding_) break;
            std::array<std::string, 6> opts = {
                "  UP:    [ " + getKeyName(custom_key_up_) + " ]",
                "  DOWN:  [ " + getKeyName(custom_key_down_) + " ]",
                "  LEFT:  [ " + getKeyName(custom_key_left_) + " ]",
                "  RIGHT: [ " + getKeyName(custom_key_right_) + " ]",
                "  PAUSE: [ " + getKeyName(custom_key_pause_) + " ]",
                "  Save & Back"
            };
            for (int i = 0; i < 6; ++i) {
                int row = center_row - 3 + i;
                if (hitText(row, center_col - 15, opts[i], my, mx)) {
                    if (i < 5) {
                        key_config_selection_ = i; is_binding_ = true;
                        if (i == 0) binding_action_ = GameAction::Up;
                        else if (i == 1) binding_action_ = GameAction::Down;
                        else if (i == 2) binding_action_ = GameAction::Left;
                        else if (i == 3) binding_action_ = GameAction::Right;
                        else if (i == 4) binding_action_ = GameAction::Pause;
                    } else {
                        phase_ = GamePhase::Settings;
                    }
                    return;
                }
            }
            break;
        }
        case GamePhase::GameOver: {
            std::string text = "Press ENTER to return to Menu";
            int col = center_col - 14;
            if (hitText(center_row + 1, col, text, my, mx)) {
                score_ = 0; lives_ = Config::INITIAL_LIVES; level_ = 1; immortal_ = false;
                map_.reset(); startMainMenu();
            }
            break;
        }
        default:
            break;
    }
}

bool GameEngine::isMouseHovering(int row, int col, const std::string& text) const {
    if (!mouse_hover_active_) return false;
    return mouse_y_ == row && mouse_x_ >= col && mouse_x_ < col + static_cast<int>(text.size());
}

void GameEngine::run() {
    auto last_time = std::chrono::steady_clock::now();
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        int delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        last_time = now;
        
        // Handle runtime terminal size changes
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) >= 0) {
            if (ws.ws_col != render_width_ || ws.ws_row != render_height_) {
                term_size_ = {ws.ws_col, ws.ws_row};
                initRenderer();
                ::write(STDOUT_FILENO, "\033[2J", 4); // Full clear
                front_buffer_.assign(render_height_, std::vector<Cell>(render_width_, Cell{}));
            }
        }
        
        // Process all buffered inputs
        int key = readKey();
        while (key != -1) {
            handleInput(key);
            if (!running_) break;
            key = readKey();
        }
        
        if (!running_) break;
        
        update(delta_ms);
        render();
        presentFrame();
        
        auto frame_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now).count();
        if (frame_time < Config::TARGET_FRAME_MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(Config::TARGET_FRAME_MS - frame_time));
        }
    }
}

void GameEngine::update(int delta_ms) {
    fade_animation_.update(delta_ms);
    
    switch (phase_) {
        case GamePhase::MainMenu:
        case GamePhase::LevelSelector:
        case GamePhase::Settings:
        case GamePhase::RedeemInput:
        case GamePhase::Stats:
            if (main_menu_msg_timer_ > 0) {
                main_menu_msg_timer_ -= delta_ms;
                if (main_menu_msg_timer_ <= 0) {
                    main_menu_message_ = "";
                }
            }
            break;
            
        case GamePhase::UsernameInput:
            if (main_menu_msg_timer_ > 0) {
                main_menu_msg_timer_ -= delta_ms;
                if (main_menu_msg_timer_ <= 0) {
                    main_menu_message_ = "";
                }
            }
            break;
            
        case GamePhase::Screensaver:
            if (screensaver_dir_ == 1) {
                screensaver_x_ += delta_ms * 0.012;
                if (screensaver_x_ > render_width_ + 25) {
                    screensaver_dir_ = -1;
                    screensaver_x_ = render_width_ + 10;
                }
            } else {
                screensaver_x_ -= delta_ms * 0.012;
                if (screensaver_x_ < -25) {
                    screensaver_dir_ = 1;
                    screensaver_x_ = -10;
                }
            }
            break;
            
        case GamePhase::GetReady:
            phase_timer_ -= delta_ms;
            if (phase_timer_ <= 0) {
                startPlaying();
            }
            break;
            
        case GamePhase::Playing:
        {
            time_played_ms_ += delta_ms;
            if (special_item_active_) {
                special_item_timer_ -= delta_ms;
                if (special_item_timer_ <= 0) {
                    if (map_.getTile(special_item_pos_) == special_item_type_) {
                        map_.setTile(special_item_pos_, TileType::Empty);
                    }
                    special_item_active_ = false;
                }
            } else {
                cherry_spawn_timer_ -= delta_ms;
                if (cherry_spawn_timer_ <= 0) {
                    cherry_spawn_timer_ = 25000 + std::rand() % 25000;
                    spawnSpecialItem(TileType::Cherry);
                }
                
                apple_spawn_timer_ -= delta_ms;
                if (apple_spawn_timer_ <= 0) {
                    apple_spawn_timer_ = 45000 + std::rand() % 45000;
                    spawnSpecialItem(TileType::GoldenApple);
                }
                
                heart_spawn_timer_ -= delta_ms;
                if (heart_spawn_timer_ <= 0) {
                    heart_spawn_timer_ = 50000 + std::rand() % 50000;
                    spawnSpecialItem(TileType::Heart);
                }
                
            }
            
            updateGlobalModeTimer(delta_ms);
            
            // Decays status effect and mechanical timers
            if (speed_boost_timer_ > 0) {
                speed_boost_timer_ -= delta_ms;
                if (speed_boost_timer_ < 0) speed_boost_timer_ = 0;
            }
            if (ghost_freeze_timer_ > 0) {
                ghost_freeze_timer_ -= delta_ms;
                if (ghost_freeze_timer_ < 0) ghost_freeze_timer_ = 0;
            }
            if (dash_cooldown_ > 0) {
                dash_cooldown_ -= delta_ms;
            }
            
            // Update popups
            for (auto it = popups_.begin(); it != popups_.end(); ) {
                it->lifetime_ms -= delta_ms;
                if (it->lifetime_ms <= 0) {
                    it = popups_.erase(it);
                } else {
                    ++it;
                }
            }
            
            // Update particles
            double dt_sec = delta_ms / 1000.0;
            for (auto it = particles_.begin(); it != particles_.end(); ) {
                it->lifetime_ms -= delta_ms;
                if (it->lifetime_ms <= 0) {
                    it = particles_.erase(it);
                } else {
                    it->x += it->vx * dt_sec;
                    it->y += it->vy * dt_sec;
                    ++it;
                }
            }
            
            // Update Acid Trails (Theme 2 - Toxic Green)
            for (auto it = acid_trails_.begin(); it != acid_trails_.end(); ) {
                it->lifetime_ms -= delta_ms;
                if (it->lifetime_ms <= 0) {
                    it = acid_trails_.erase(it);
                } else {
                    ++it;
                }
            }
            if (level_ >= 5 && level_ <= 8) {
                bool already_exists = false;
                for (const auto& trail : acid_trails_) {
                    if (trail.pos == pacman_.position) {
                        already_exists = true;
                        break;
                    }
                }
                if (!already_exists) {
                    acid_trails_.push_back(AcidTrail{ .pos = pacman_.position, .lifetime_ms = 3000 });
                }
            }
            
            // Update Lava Tiles (Theme 4 - Lava Orange)
            for (auto it = lava_tiles_.begin(); it != lava_tiles_.end(); ) {
                if (it->warning_ms > 0) {
                    it->warning_ms -= delta_ms;
                    ++it;
                } else if (it->active_ms > 0) {
                    it->active_ms -= delta_ms;
                    if (pacman_.position == it->pos && !immortal_) {
                        pacmanCaught();
                    }
                    ++it;
                } else {
                    it = lava_tiles_.erase(it);
                }
            }
            if (level_ >= 13 && level_ <= 16) {
                lava_spawn_timer_ -= delta_ms;
                if (lava_spawn_timer_ <= 0) {
                    lava_spawn_timer_ = 3000 + std::rand() % 3000;
                    std::vector<Vec2> empty_cells;
                    for (int y = 0; y < Config::MAP_HEIGHT; ++y) {
                        for (int x = 0; x < Config::MAP_WIDTH; ++x) {
                            if (map_.getTile(x, y) == TileType::Empty && !(y >= 12 && y <= 16 && x >= 10 && x <= 17)) {
                                empty_cells.push_back({x, y});
                            }
                        }
                    }
                    if (!empty_cells.empty()) {
                        Vec2 pos = empty_cells[std::rand() % empty_cells.size()];
                        lava_tiles_.push_back(LavaTile{ .pos = pos, .warning_ms = 1500, .active_ms = 1500 });
                    }
                }
            }
            
            // Warp Portals check for Pac-man (Theme 3 - Cyberpunk)
            if (level_ >= 9 && level_ <= 12) {
                Vec2 pos = pacman_.position;
                if (pos == portal_A1_ && !pac_just_warped_) {
                    pacman_.position = portal_A2_;
                    pac_just_warped_ = true;
                } else if (pos == portal_A2_ && !pac_just_warped_) {
                    pacman_.position = portal_A1_;
                    pac_just_warped_ = true;
                } else if (pos == portal_B1_ && !pac_just_warped_) {
                    pacman_.position = portal_B2_;
                    pac_just_warped_ = true;
                } else if (pos == portal_B2_ && !pac_just_warped_) {
                    pacman_.position = portal_B1_;
                    pac_just_warped_ = true;
                } else if (pos != portal_A1_ && pos != portal_A2_ && pos != portal_B1_ && pos != portal_B2_) {
                    pac_just_warped_ = false;
                }
            }
            
            // Update Ghost AI
            for (size_t i = 0; i < ghosts_.size(); ++i) {
                updateGhostAI(ghosts_[i], delta_ms);
            }
            
            // Pac-Man Movement
            int pac_interval = Config::PAC_MOVE_INTERVAL;
            if (cheat_super_speed_) {
                pac_interval = Config::PAC_MOVE_INTERVAL * 4 / 10;
            } else if (speed_boost_timer_ > 0) {
                pac_interval = Config::PAC_MOVE_INTERVAL * 7 / 10;
            }
            pac_move_accumulator_ += delta_ms;
            while (pac_move_accumulator_ >= pac_interval) {
                pac_move_accumulator_ -= pac_interval;
                pacman_.tryMove(map_);
                pacman_.advanceAnim();
                checkCollisions();
            }
            
            // Ghosts Movement
            for (size_t i = 0; i < ghosts_.size(); ++i) {
                auto& g = ghosts_[i];
                if ((ghost_freeze_timer_ > 0 || cheat_freeze_ghosts_) && g.mode != GhostMode::Eaten) {
                    continue;
                }
                
                int interval = Config::GHOST_MOVE_INTERVAL;
                if (g.mode == GhostMode::Frightened) {
                    interval = Config::GHOST_MOVE_INTERVAL * 2;
                } else if (g.mode == GhostMode::Eaten) {
                    interval = Config::GHOST_MOVE_INTERVAL / 2;
                }
                
                // Theme-specific ghost speed modifiers
                if (level_ >= 5 && level_ <= 8 && g.mode != GhostMode::Eaten) {
                    bool on_acid = false;
                    for (const auto& trail : acid_trails_) {
                        if (trail.pos == g.position) {
                            on_acid = true;
                            break;
                        }
                    }
                    if (on_acid) {
                        interval *= 2; // slow by 50%
                    }
                }
                if (level_ >= 13 && level_ <= 16) {
                    bool on_lava = false;
                    for (const auto& lava : lava_tiles_) {
                        if (lava.pos == g.position && lava.warning_ms <= 0 && lava.active_ms > 0) {
                            on_lava = true;
                            break;
                        }
                    }
                    if (on_lava) {
                        interval = interval * 3 / 4; // speed up by 25%
                    }
                }
                
                ghost_accumulators_[i] += delta_ms;
                while (ghost_accumulators_[i] >= interval) {
                    ghost_accumulators_[i] -= interval;
                    moveGhost(g);
                    
                    // Warp Portals check for ghosts (Theme 3 - Cyberpunk)
                    if (level_ >= 9 && level_ <= 12) {
                        Vec2 pos = g.position;
                        if (pos == portal_A1_) {
                            g.position = portal_A2_;
                        } else if (pos == portal_A2_) {
                            g.position = portal_A1_;
                        } else if (pos == portal_B1_) {
                            g.position = portal_B2_;
                        } else if (pos == portal_B2_) {
                            g.position = portal_B1_;
                        }
                    }
                    
                    checkCollisions();
                }
            }
            
            afk_timer_ += delta_ms;
            if (afk_timer_ >= 20000) {
                phase_ = GamePhase::Screensaver;
                screensaver_x_ = -20.0;
                screensaver_dir_ = 1;
            }
            break;
        }
            
        case GamePhase::PacDying:
            phase_timer_ -= delta_ms;
            if (phase_timer_ <= 0) {
                lives_--;
                if (lives_ <= 0) {
                    startGameOver();
                } else {
                    startGetReady();
                }
            }
            break;
            
        case GamePhase::LevelClear:
            phase_timer_ -= delta_ms;
            if (phase_timer_ <= 0) {
                if (level_ == 20) {
                    unlocked_rainbow_ = true;
                    saveHighScore();
                    phase_ = GamePhase::LevelSelector;
                } else {
                    if (level_ + 1 > max_unlocked_level_) {
                        max_unlocked_level_ = level_ + 1;
                        if (max_unlocked_level_ > 20) max_unlocked_level_ = 20;
                        saveHighScore();
                    }
                    level_++;
                    startLevel(level_);
                    startGetReady();
                }
            }
            break;
            
        case GamePhase::GameOver:
            break;
            
        case GamePhase::Paused:
            break;

        case GamePhase::DevMenu:
        case GamePhase::DevPasswordInput:
            break;

        case GamePhase::KeyConfig:
            break;
    }
    if (click_feedback_timer_ > 0) {
        click_feedback_timer_ -= delta_ms;
        if (click_feedback_timer_ < 0) click_feedback_timer_ = 0;
    }
}

void GameEngine::updateGlobalModeTimer(int delta_ms) {
    if (current_wave_ >= MODE_WAVES.size()) return;
    
    // Pause wave timer if ghosts are currently frightened
    bool any_frightened = false;
    for (const auto& g : ghosts_) {
        if (g.mode == GhostMode::Frightened) {
            any_frightened = true;
            break;
        }
    }
    if (any_frightened) return;
    
    int duration = MODE_WAVES[current_wave_].duration_ms;
    if (duration == -1) return; // Scatter/Chase waves finished, chase infinitely
    
    global_mode_timer_ += delta_ms;
    if (global_mode_timer_ >= duration) {
        global_mode_timer_ -= duration;
        current_wave_++;
        
        // Reverse direction of all active ghosts on wave transition
        GhostMode next_mode = getGlobalMode();
        for (auto& g : ghosts_) {
            if (g.mode == GhostMode::Chase || g.mode == GhostMode::Scatter) {
                g.mode = next_mode;
                g.currentDirection = g.reverseDirection();
            }
        }
    }
}

GhostMode GameEngine::getGlobalMode() const {
    if (current_wave_ >= MODE_WAVES.size()) {
        return GhostMode::Chase;
    }
    return MODE_WAVES[current_wave_].mode;
}

Vec2 GameEngine::calculateGhostTarget(const Ghost& ghost) const {
    if (ghost.mode == GhostMode::Scatter) {
        switch (ghost.personality) {
            case GhostPersonality::Blinky: return Config::BLINKY_SCATTER;
            case GhostPersonality::Pinky:  return Config::PINKY_SCATTER;
            case GhostPersonality::Inky:   return Config::INKY_SCATTER;
            case GhostPersonality::Clyde:  return Config::CLYDE_SCATTER;
        }
    }
    
    if (ghost.mode == GhostMode::Chase) {
        switch (ghost.personality) {
            case GhostPersonality::Blinky:
                return pacman_.position;
                
            case GhostPersonality::Pinky: {
                Vec2 target = pacman_.position + directionToVec2(pacman_.currentDirection) * 4;
                if (pacman_.currentDirection == Direction::Up) {
                    target.x -= 4; // Reproduce classic overflow bug
                }
                return clampToMap(target);
            }
            
            case GhostPersonality::Inky: {
                Vec2 pivot = pacman_.position + directionToVec2(pacman_.currentDirection) * 2;
                if (pacman_.currentDirection == Direction::Up) {
                    pivot.x -= 2;
                }
                Vec2 blinky_pos = ghosts_[0].position; // Blinky is personality index 0
                // Classic Inky vector extrapolation: 2*pivot - blinky.
                // Clamp the pivot into the grid so the extrapolated target is
                // always reachable; an unclamped target made the ghost stall
                // (BFS returns Direction::None) near map edges.
                return clampToMap(pivot + (pivot - blinky_pos));
            }
            
            case GhostPersonality::Clyde: {
                int dx = ghost.position.x - pacman_.position.x;
                int dy = ghost.position.y - pacman_.position.y;
                int dist_sq = dx*dx + dy*dy;
                if (dist_sq > 64) {
                    return pacman_.position;
                } else {
                    return Config::CLYDE_SCATTER;
                }
            }
        }
    }
    
    if (ghost.mode == GhostMode::Eaten) {
        return Config::GHOST_HOUSE_EXIT;
    }
    
    if (ghost.mode == GhostMode::InHouse) {
        return Config::GHOST_HOUSE_EXIT;
    }
    
    return Config::GHOST_HOUSE_EXIT;
}

void GameEngine::moveGhost(Ghost& ghost) {
    if (ghost.mode == GhostMode::InHouse) {
        bool release = false;
        if (ghost.personality == GhostPersonality::Pinky && ghost.dotCounter >= 0) release = true;
        else if (ghost.personality == GhostPersonality::Inky && ghost.dotCounter >= 30) release = true;
        else if (ghost.personality == GhostPersonality::Clyde && ghost.dotCounter >= 60) release = true;
        
        if (release) {
            ghost.exitHouse();
        } else {
            // Idle bounce inside ghost house
            if (ghost.position.y == Config::GHOST_HOUSE_CENTER.y) {
                ghost.position.y += 1;
            } else {
                ghost.position.y = Config::GHOST_HOUSE_CENTER.y;
            }
            return;
        }
    }
    
    ghost.position = map_.wrapTunnel(ghost.position);
    
    if (ghost.mode == GhostMode::Eaten && ghost.hasReachedHouse()) {
        ghost.reset();
        ghost.mode = GhostMode::InHouse;
        return;
    }
    
    Direction next_dir = Direction::None;
    
    if (ghost.mode == GhostMode::Frightened) {
        // Random valid direction selection at intersections
        Direction forbidden = ghost.reverseDirection();
        std::vector<Direction> valid_dirs;
        for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
            if (d == forbidden) continue;
            Vec2 next = ghost.position + directionToVec2(d);
            next = map_.wrapTunnel(next);
            if (map_.isWalkableByGhost(next)) {
                valid_dirs.push_back(d);
            }
        }
        if (!valid_dirs.empty()) {
            next_dir = valid_dirs[rng_() % valid_dirs.size()];
        } else {
            next_dir = forbidden;
        }
    } else {
        Vec2 target = calculateGhostTarget(ghost);
        next_dir = bfsNextDirection(map_, ghost.position, target, ghost.reverseDirection());
    }
    
    if (next_dir != Direction::None) {
        ghost.position = ghost.position + directionToVec2(next_dir);
        ghost.position = map_.wrapTunnel(ghost.position);
        ghost.currentDirection = next_dir;
    }
}

void GameEngine::updateGhostAI(Ghost& ghost, int delta_ms) {
    if (ghost.mode == GhostMode::Frightened) {
        ghost.updateFrightened(delta_ms);
        // If frightened ends, let the ghost return to global mode state
        if (ghost.mode != GhostMode::Frightened) {
            ghost.mode = getGlobalMode();
        }
    }
}

void GameEngine::checkCollisions() {
    if (phase_ != GamePhase::Playing) return;
    
    TileType tile = map_.getTile(pacman_.position);
    
    if (tile == TileType::Dot) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(Config::SCORE_DOT);
        eatDot(pacman_.position);
    } else if (tile == TileType::PowerPellet) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(Config::SCORE_POWER_PELLET);
        eatPowerPellet();
    } else if (tile == TileType::Fruit) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(Config::SCORE_FRUIT);
    } else if (tile == TileType::Cherry) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(100);
        speed_boost_timer_ = 5000; // 5 seconds speed boost
        spawnScorePopup(pacman_.position, 100, {255, 100, 100});
        spawnParticleBurst(pacman_.position, {255, 50, 50});
        playSound("sounds/eat_pellet.wav");
        special_item_active_ = false;
    } else if (tile == TileType::GoldenApple) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(1000);
        ghost_freeze_timer_ = 3000; // 3 seconds ghost freeze
        spawnScorePopup(pacman_.position, 1000, {255, 215, 0});
        spawnParticleBurst(pacman_.position, {255, 215, 0});
        playSound("sounds/eat_pellet.wav");
        special_item_active_ = false;
    } else if (tile == TileType::Heart) {
        map_.setTile(pacman_.position, TileType::Empty);
        if (lives_ < 66) {
            lives_++;
        }
        spawnScorePopup(pacman_.position, 100, {255, 105, 180});
        spawnParticleBurst(pacman_.position, {255, 105, 180});
        playSound("sounds/eat_pellet.wav");
        special_item_active_ = false;
    }
    
    // Ghost Collisions
    for (auto& g : ghosts_) {
        if (g.position == pacman_.position) {
            if (g.mode == GhostMode::Frightened) {
                eatGhost(g);
            } else if (g.mode == GhostMode::Chase || g.mode == GhostMode::Scatter) {
                if (!immortal_) {
                    pacmanCaught();
                    return;
                }
            }
        }
    }
    
    // Extra Life calculation
    if (!extra_life_awarded_ && score_ >= Config::EXTRA_LIFE_AT) {
        lives_++;
        extra_life_awarded_ = true;
    }
    
    // Level Complete transition
    if (map_.remainingDots() == 0) {
        startLevelClear();
    }
}

void GameEngine::eatDot(Vec2 /*pos*/) {
    dots_eaten_++;
    // Increment dots for any ghost trapped in house
    for (auto& g : ghosts_) {
        if (g.mode == GhostMode::InHouse) {
            g.dotCounter++;
        }
    }
    playSound("sounds/eat_dot.wav");
}

void GameEngine::eatPowerPellet() {
    power_pellets_++;
    ghosts_eaten_combo_ = 0;
    for (auto& g : ghosts_) {
        g.frighten(Config::FRIGHTENED_DURATION);
    }
    playSound("sounds/eat_pellet.wav");
    spawnParticleBurst(pacman_.position, {255, 183, 174});
}

void GameEngine::eatGhost(Ghost& ghost) {
    ghost.setEaten();
    ghosts_eaten_++;
    int points = Config::SCORE_GHOST_BASE << ghosts_eaten_combo_;
    addScore(points);
    spawnScorePopup(ghost.position, points, {100, 255, 100});
    spawnParticleBurst(ghost.position, {100, 255, 100});
    ghosts_eaten_combo_ = std::min(ghosts_eaten_combo_ + 1, 3); // 200, 400, 800, 1600
    
    playSound("sounds/eat_ghost.wav");
}

void GameEngine::pacmanCaught() {
    pacman_.kill();
    deaths_++;
    playSound("sounds/death.wav");
    startDeath();
}

void GameEngine::startMainMenu() {
    phase_ = GamePhase::MainMenu;
    fade_animation_.fadeIn({255, 255, 255}, 300);
}

void GameEngine::startGetReady() {
    phase_ = GamePhase::GetReady;
    phase_timer_ = Config::GETREADY_DURATION;
    
    pacman_.reset();
    for (auto& g : ghosts_) {
        g.reset();
    }
    
    pac_move_accumulator_ = 0;
    ghost_move_accumulator_ = 0;
    ghost_accumulators_.fill(0);
    
    playSound("sounds/ready.wav");
}

void GameEngine::startPlaying() {
    phase_ = GamePhase::Playing;
    current_wave_ = 0;
    global_mode_timer_ = 0;
}

void GameEngine::startDeath() {
    phase_ = GamePhase::PacDying;
    phase_timer_ = Config::DEATH_ANIM_DURATION;
}

void GameEngine::startLevelClear() {
    phase_ = GamePhase::LevelClear;
    phase_timer_ = Config::GETREADY_DURATION;
    playSound("sounds/clear.wav");
}

void GameEngine::startGameOver() {
    phase_ = GamePhase::GameOver;
    if (score_ > high_score_) {
        high_score_ = score_;
    }
    saveHighScore();
}

void GameEngine::initRenderer() {
    render_width_ = term_size_.x;
    render_height_ = term_size_.y;
    front_buffer_.assign(render_height_, std::vector<Cell>(render_width_, Cell{}));
    back_buffer_.assign(render_height_, std::vector<Cell>(render_width_, Cell{}));
}

void GameEngine::setCell(int row, int col, const Cell& cell) {
    if (row >= 0 && row < render_height_ && col >= 0 && col < render_width_) {
        Cell out = cell;
        if (!cell.glyph.empty() && cell.glyph != " ") {
            if (apply_menu_theme_) {
                out.fg = applyGeneralTheme(out.fg, row, col);
            }
        }
        back_buffer_[row][col] = out;
    }
}

void GameEngine::fillRow(int row, Color fg, Color bg) {
    if (row < 0 || row >= render_height_) return;
    Cell fill{ .glyph = " ", .fg = fg, .bg = bg };
    auto& row_buf = back_buffer_[row];
    std::fill(row_buf.begin(), row_buf.end(), fill);
}

size_t GameEngine::utf8SequenceLength(unsigned char c) noexcept {
    if (c >= 0xf0) return 4;
    if (c >= 0xe0) return 3;
    if (c >= 0xc0) return 2;
    return 1;
}

void GameEngine::drawString(int row, int col, const std::string& text, Color fg, Color bg, bool bold) {
    int current_col = col;
    for (size_t i = 0; i < text.size() && current_col < render_width_; ) {
        size_t len = utf8SequenceLength(text[i]);
        if (i + len > text.size()) len = text.size() - i;

        Cell cell;
        cell.glyph = text.substr(i, len);
        cell.fg = fg;
        cell.bg = bg;
        cell.bold = bold;

        setCell(row, current_col, cell);
        current_col++;
        i += len;
    }
}

void GameEngine::drawGradientString(int row, int col, const std::string& text, Color start_fg, Color end_fg, Color bg) {
    // Pre-count glyph (graphical) columns; any text this function draws is
    // assigned one column per glyph, matching drawString().
    size_t glyph_count = 0;
    for (size_t i = 0; i < text.size(); ) {
        i += utf8SequenceLength(text[i]);
        ++glyph_count;
    }
    if (glyph_count == 0) return;

    int current_col = col;
    size_t idx = 0;
    for (size_t i = 0; i < text.size() && current_col < render_width_; ) {
        size_t len = utf8SequenceLength(text[i]);
        if (i + len > text.size()) len = text.size() - i;

        double ratio = glyph_count > 1 ? static_cast<double>(idx) / (glyph_count - 1) : 0.0;
        Cell cell;
        cell.glyph = text.substr(i, len);
        cell.fg = {
            static_cast<uint8_t>(start_fg.r + ratio * (end_fg.r - start_fg.r)),
            static_cast<uint8_t>(start_fg.g + ratio * (end_fg.g - start_fg.g)),
            static_cast<uint8_t>(start_fg.b + ratio * (end_fg.b - start_fg.b)),
        };
        cell.bg = bg;
        setCell(row, current_col, cell);
        current_col++;
        i += len;
        ++idx;
    }
}

void GameEngine::drawBox(int row, int col, int w, int h, Color fg, Color bg) {
    for (int r = row; r < row + h; ++r) {
        for (int c = col; c < col + w; ++c) {
            Cell cell{ .glyph = " ", .fg = fg, .bg = bg };
            setCell(r, c, cell);
        }
    }
}

void GameEngine::clearBuffer(Color bg) {
    for (int r = 0; r < render_height_; ++r) {
        for (int c = 0; c < render_width_; ++c) {
            back_buffer_[r][c] = Cell{ .glyph = " ", .fg = {255, 255, 255}, .bg = bg };
        }
    }
}

void GameEngine::presentFrame() {
    output_batch_.clear();
    output_batch_.reserve(64000); // Pre-reserve capacity to avoid allocations
    
    // Fade overlay: dims the frame during menu fades. Never applied during gameplay.
    float fade = 1.0f;
    bool fade_active = false;
    if (phase_ != GamePhase::GetReady && phase_ != GamePhase::Playing &&
        phase_ != GamePhase::PacDying && phase_ != GamePhase::LevelClear &&
        phase_ != GamePhase::Paused) {
        if (fade_animation_.state == AnimationController::State::FadingIn ||
            fade_animation_.state == AnimationController::State::FadingOut) {
            Color fc = fade_animation_.getCurrentColor();
            fade = (fc.r + fc.g + fc.b) / 765.0f;
            if (fade < 0.0f) fade = 0.0f;
            if (fade > 1.0f) fade = 1.0f;
            fade_active = true;
        }
    }
    
    Color current_fg = {0, 0, 0};
    Color current_bg = {0, 0, 0};
    bool current_bold = false;
    bool current_blink = false;
    bool style_initialized = false;
    
    auto appendInt = [&](int val) {
        if (val >= 100) {
            output_batch_ += (char)('0' + (val / 100));
            output_batch_ += (char)('0' + ((val / 10) % 10));
            output_batch_ += (char)('0' + (val % 10));
        } else if (val >= 10) {
            output_batch_ += (char)('0' + (val / 10));
            output_batch_ += (char)('0' + (val % 10));
        } else {
            output_batch_ += (char)('0' + val);
        }
    };
    
    
    auto appendStyle = [&](const Color& fg, const Color& bg, bool bold, bool blink) {
        output_batch_ += "\033[0m"; // Reset style
        
        output_batch_ += "\033[38;2;";
        appendInt(fg.r); output_batch_ += ';';
        appendInt(fg.g); output_batch_ += ';';
        appendInt(fg.b); output_batch_ += 'm';
        
        output_batch_ += "\033[48;2;";
        appendInt(bg.r); output_batch_ += ';';
        appendInt(bg.g); output_batch_ += ';';
        appendInt(bg.b); output_batch_ += 'm';
        
        if (bold) output_batch_ += "\033[1m";
        if (blink) output_batch_ += "\033[5m";
        
        current_fg = fg;
        current_bg = bg;
        current_bold = bold;
        current_blink = blink;
        style_initialized = true;
    };
    
    auto appendPos = [&](int r, int c) {
        output_batch_ += "\033[";
        appendInt(r);
        output_batch_ += ';';
        appendInt(c);
        output_batch_ += 'H';
    };
    
    int cursor_r = -1;
    int cursor_c = -1;
    
    for (int r = 0; r < render_height_; ++r) {
        for (int c = 0; c < render_width_; ++c) {
            const Cell& back = back_buffer_[r][c];
            const Cell& front = front_buffer_[r][c];
            
            if (fade_active || back != front) {
                // If cursor is not at (r, c), move it there
                if (cursor_r != r || cursor_c != c) {
                    appendPos(r + 1, c + 1);
                    cursor_r = r;
                }
                
                Color out_fg = back.fg;
                Color out_bg = back.bg;
                if (fade_active) {
                    out_fg = { static_cast<uint8_t>(back.fg.r * fade),
                               static_cast<uint8_t>(back.fg.g * fade),
                               static_cast<uint8_t>(back.fg.b * fade) };
                    out_bg = { static_cast<uint8_t>(back.bg.r * fade),
                               static_cast<uint8_t>(back.bg.g * fade),
                               static_cast<uint8_t>(back.bg.b * fade) };
                }
                
                if (!style_initialized || out_fg != current_fg || out_bg != current_bg || back.bold != current_bold || back.blink != current_blink) {
                    appendStyle(out_fg, out_bg, back.bold, back.blink);
                }
                
                output_batch_ += back.glyph;
                
                // If the printed glyph is single-byte, we know the terminal cursor advanced by exactly 1 cell.
                // Otherwise, reset cursor position tracking to force repositioning for the next cell.
                if (back.glyph.length() == 1) {
                    cursor_c = c + 1;
                } else {
                    cursor_c = -1;
                }
            }
        }
    }
    
    // Commit the freshly-drawn frame. Swapping (instead of copying) reuses the
    // two pre-allocated buffers and turns a full O(row*col) element copy into
    // an O(1) pointer exchange on every frame.
    front_buffer_.swap(back_buffer_);

    if (!output_batch_.empty()) {
        ::write(STDOUT_FILENO, output_batch_.data(), output_batch_.size());
        static_cast<void>(::fflush(stdout));
    }
}

void GameEngine::render() {
    current_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    clearBuffer();
    apply_menu_theme_ = false;
    
    switch (phase_) {
        case GamePhase::MainMenu:
        case GamePhase::UsernameInput:
            apply_menu_theme_ = true;
            renderMainMenu();
            if (phase_ == GamePhase::UsernameInput) {
                int c_row = render_height_ / 2;
                int c_col = render_width_ / 2;
                drawDoubleBorderBox(c_row - 3, c_col - 16, 32, 7, {255, 255, 0}, {0, 0, 0});
                drawString(c_row - 2, c_col - 6, " SET USERNAME ", {255, 255, 0});
                drawString(c_row, c_col - 14, "> " + input_username_ + "_", {255, 255, 255});
                drawString(c_row + 2, c_col - 13, "ESC: Cancel  ENTER: Set", {150, 150, 150});
            }
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::Settings:
            apply_menu_theme_ = true;
            renderSettings();
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::RedeemInput:
            apply_menu_theme_ = true;
            renderRedeem();
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::Stats:
            apply_menu_theme_ = true;
            renderStats();
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::LevelSelector:
            apply_menu_theme_ = true;
            renderLevelSelector();
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::KeyConfig:
            apply_menu_theme_ = true;
            renderKeyConfig();
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::Screensaver:
            renderScreensaver();
            break;
            
        case GamePhase::GetReady:
            renderMap();
            renderEntities();
            renderEffects();
            renderHUD();
            apply_menu_theme_ = true;
            renderGetReady();
            apply_menu_theme_ = false;
            break;
            
        case GamePhase::Playing:
        case GamePhase::Paused:
        case GamePhase::DevMenu:
        case GamePhase::DevPasswordInput:
            renderMap();
            renderEntities();
            renderEffects();
            renderHUD();
            if (phase_ == GamePhase::Paused) {
                int center_row = render_height_ / 2;
                int center_col = render_width_ / 2;
                apply_menu_theme_ = true;
                
                // Draw a nice centered menu box with outline
                drawDoubleBorderBox(center_row - 4, center_col - 11, 22, 8, {0, 255, 255}, {0, 0, 0});
                drawString(center_row - 3, center_col - 6, " GAME PAUSED ", {0, 255, 255});
                
                std::array<std::string, 4> options = {
                    "Resume Game",
                    "Restart Level",
                    "Return to Menu",
                    "Quit Game"
                };
                
                for (int i = 0; i < 4; ++i) {
                    Color fg = {255, 255, 255};
                    std::string prefix = "  ";
                    bool bold = false;
                    if (i == pause_menu_selection_) {
                        if (click_feedback_timer_ > 0) {
                            fg = {255, 255, 255};
                            bold = true;
                        } else {
                            fg = {255, 255, 0}; // Yellow for selection
                        }
                        prefix = "> ";
                    }
                    if (isMouseHovering(center_row - 1 + i, center_col - 8, prefix + options[i])) {
                        fg.r = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.r) + 50));
                        fg.g = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.g) + 50));
                        fg.b = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.b) + 50));
                        bold = true;
                    }
                    drawString(center_row - 1 + i, center_col - 8, prefix + options[i], fg, {0, 0, 0}, bold);
                }
                apply_menu_theme_ = false;
            } else if (phase_ == GamePhase::DevPasswordInput) {
                renderDevPasswordInput();
            } else if (phase_ == GamePhase::DevMenu) {
                renderDevMenu();
            }
            break;
            
        case GamePhase::PacDying:
            renderMap();
            renderEntities();
            renderEffects();
            renderHUD();
            break;
            
        case GamePhase::LevelClear:
            renderMap();
            renderEffects();
            renderHUD();
            {
                int base_col = (render_width_ - Config::MAP_WIDTH * Config::TILE_RENDER_W) / 2;
                int base_row = (render_height_ - Config::MAP_HEIGHT - 2) / 2;
                drawString(base_row + Config::MAP_HEIGHT / 2, base_col + (Config::MAP_WIDTH * Config::TILE_RENDER_W - 11) / 2, "LEVEL CLEAR", {0, 255, 0});
            }
            break;
            
        case GamePhase::GameOver:
            renderMap();
            renderHUD();
            apply_menu_theme_ = true;
            renderGameOver();
            apply_menu_theme_ = false;
            break;
    }
}

GameEngine::Viewport GameEngine::getViewport() const {
    Viewport vp;
    int req_w = Config::MAP_WIDTH * Config::TILE_RENDER_W; // 56
    int req_h = Config::MAP_HEIGHT + 2; // 33
    
    if (render_width_ >= req_w && render_height_ >= req_h) {
        // Full map fits
        vp.start_x = 0;
        vp.start_y = 0;
        vp.visible_cols = Config::MAP_WIDTH;
        vp.visible_rows = Config::MAP_HEIGHT;
        vp.base_row = (render_height_ - Config::MAP_HEIGHT - 2) / 2 + 1; // leave 1 line for top HUD
        vp.base_col = (render_width_ - req_w) / 2;
        vp.is_scrolling = false;
    } else {
        // Scrolling camera centered on Pac-Man
        vp.is_scrolling = true;
        vp.visible_rows = std::max(5, render_height_ - 2); // reserve top/bottom lines for HUD
        vp.visible_cols = std::max(5, render_width_ / Config::TILE_RENDER_W);
        
        // Center on Pacman
        vp.start_y = pacman_.position.y - vp.visible_rows / 2;
        vp.start_x = pacman_.position.x - vp.visible_cols / 2;
        
        // Clamp to map boundaries
        if (vp.start_y < 0) vp.start_y = 0;
        if (vp.start_y + vp.visible_rows > Config::MAP_HEIGHT) {
            vp.start_y = Config::MAP_HEIGHT - vp.visible_rows;
        }
        if (vp.start_y < 0) vp.start_y = 0;
        
        if (vp.start_x < 0) vp.start_x = 0;
        if (vp.start_x + vp.visible_cols > Config::MAP_WIDTH) {
            vp.start_x = Config::MAP_WIDTH - vp.visible_cols;
        }
        if (vp.start_x < 0) vp.start_x = 0;
        
        vp.base_row = 1;
        vp.base_col = (render_width_ - vp.visible_cols * Config::TILE_RENDER_W) / 2;
    }
    return vp;
}

void GameEngine::renderMap() {
    Viewport vp = getViewport();
    
    for (int vy = 0; vy < vp.visible_rows; ++vy) {
        int y = vp.start_y + vy;
        if (y < 0 || y >= Config::MAP_HEIGHT) continue;
        
        for (int vx = 0; vx < vp.visible_cols; ++vx) {
            int x = vp.start_x + vx;
            if (x < 0 || x >= Config::MAP_WIDTH) continue;
            
            TileType t = map_.getTile(x, y);
            int screen_row = vp.base_row + vy;
            int screen_col = vp.base_col + vx * Config::TILE_RENDER_W;
            
            Cell c1, c2;
            c1.bg = {0, 0, 0};
            c2.bg = {0, 0, 0};
            
            if (t == TileType::Wall) {
                uint8_t mask = map_.wallNeighborMask({x, y});
                std::string g1 = " ";
                std::string g2 = " ";
                if (use_nerd_fonts_) {
                    switch (mask) {
                        case 0:  g1 = " "; g2 = " "; break;
                        case 1:  g1 = "┃"; g2 = " "; break;
                        case 2:  g1 = "━"; g2 = "━"; break;
                        case 3:  g1 = "┗"; g2 = "━"; break;
                        case 4:  g1 = "┃"; g2 = " "; break;
                        case 5:  g1 = "┃"; g2 = " "; break;
                        case 6:  g1 = "┏"; g2 = "━"; break;
                        case 7:  g1 = "┣"; g2 = "━"; break;
                        case 8:  g1 = "━"; g2 = " "; break;
                        case 9:  g1 = "┛"; g2 = " "; break;
                        case 10: g1 = "━"; g2 = "━"; break;
                        case 11: g1 = "┻"; g2 = "━"; break;
                        case 12: g1 = "┓"; g2 = " "; break;
                        case 13: g1 = "┫"; g2 = " "; break;
                        case 14: g1 = "┳"; g2 = "━"; break;
                        case 15: g1 = "╋"; g2 = "━"; break;
                    }
                } else {
                    g1 = "#";
                    g2 = "#";
                }
                c1.glyph = g1;
                c2.glyph = g2;
                if (level_ == 20) {
                    std::array<std::string, 6> corrupt_chars = {"?", "!", "%", "$", "&", "*"};
                    c1.glyph = corrupt_chars[(current_time_ms_ / 100 + x + y) % 6];
                    c2.glyph = " ";
                }
                
                double ratio = static_cast<double>(y) / (Config::MAP_HEIGHT - 1);
                uint8_t wg = static_cast<uint8_t>(180 - ratio * 140);
                uint8_t wb = static_cast<uint8_t>(255 - ratio * 75);
                
                Color wall_color;
                Color dot_color;
                if (level_ >= 1 && level_ <= 4) {
                    wall_color = Color{0, wg, wb};
                    dot_color = Color{255, 183, 174};
                } else if (level_ >= 5 && level_ <= 8) {
                    wall_color = Color{0, wb, wg};
                    dot_color = Color{174, 255, 183};
                } else if (level_ >= 9 && level_ <= 12) {
                    wall_color = Color{wb, 0, wg};
                    dot_color = Color{255, 174, 255};
                } else if (level_ >= 13 && level_ <= 16) {
                    wall_color = Color{wb, wg, 0};
                    dot_color = Color{255, 183, 100};
                } else if (level_ == 20) {
                    uint8_t gr = (current_time_ms_ / 100 % 2 == 0) ? 255 : 50;
                    uint8_t gg = (current_time_ms_ / 150 % 2 == 0) ? 50 : 0;
                    uint8_t gb = (current_time_ms_ / 200 % 2 == 0) ? 255 : 0;
                    wall_color = Color{gr, gg, gb};
                    dot_color = Color{gb, gr, gg};
                } else {
                    wall_color = Color{wb, wb, 0};
                    dot_color = Color{255, 255, 150};
                }
                
                c1.fg = wall_color;
                c2.fg = wall_color;
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::Dot) {
                c1.glyph = use_nerd_fonts_ ? "·" : ".";
                c2.glyph = " ";
                
                Color dot_color;
                if (level_ >= 1 && level_ <= 4) {
                    dot_color = Color{255, 183, 174};
                } else if (level_ >= 5 && level_ <= 8) {
                    dot_color = Color{174, 255, 183};
                } else if (level_ >= 9 && level_ <= 12) {
                    dot_color = Color{255, 174, 255};
                } else if (level_ >= 13 && level_ <= 16) {
                    dot_color = Color{255, 183, 100};
                } else {
                    dot_color = Color{255, 255, 150};
                }
                
                c1.fg = dot_color;
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::PowerPellet) {
                c1.glyph = use_nerd_fonts_ ? "●" : "O";
                c2.glyph = " ";
                
                Color dot_color;
                if (level_ >= 1 && level_ <= 4) {
                    dot_color = Color{255, 183, 174};
                } else if (level_ >= 5 && level_ <= 8) {
                    dot_color = Color{174, 255, 183};
                } else if (level_ >= 9 && level_ <= 12) {
                    dot_color = Color{255, 174, 255};
                } else if (level_ >= 13 && level_ <= 16) {
                    dot_color = Color{255, 183, 100};
                } else {
                    dot_color = Color{255, 255, 150};
                }
                
                c1.fg = dot_color;
                c1.blink = true;
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::GhostDoor) {
                c1.glyph = use_nerd_fonts_ ? "━" : "-";
                c2.glyph = use_nerd_fonts_ ? "━" : "-";
                c1.fg = {255, 183, 222};
                c2.fg = {255, 183, 222};
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::Cherry) {
                c1.glyph = use_nerd_fonts_ ? "🍒" : "c";
                c2.glyph = " ";
                c1.fg = Color{255, 0, 0};
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::GoldenApple) {
                c1.glyph = use_nerd_fonts_ ? "🍏" : "a";
                c2.glyph = " ";
                c1.fg = Color{255, 215, 0};
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::Heart) {
                c1.glyph = use_nerd_fonts_ ? "❤️" : "h";
                c2.glyph = " ";
                c1.fg = Color{255, 105, 180};
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            }
        }
    }
}

void GameEngine::renderEntities() {
    Viewport vp = getViewport();
    
    // Draw Pac-Man
    if (pacman_.isAlive()) {
        int vx = pacman_.position.x - vp.start_x;
        int vy = pacman_.position.y - vp.start_y;
        
        if (vx >= 0 && vx < vp.visible_cols && vy >= 0 && vy < vp.visible_rows) {
            Cell c1, c2;
            c1.bg = {0, 0, 0};
            c2.bg = {0, 0, 0};
            Color pm_fg;
            if (selected_pacman_color_ == 0) pm_fg = {255, 255, 0}; // Yellow
            else if (selected_pacman_color_ == 1) pm_fg = {0, 255, 255}; // Cyan
            else if (selected_pacman_color_ == 2) pm_fg = {0, 255, 0}; // Green
            else if (selected_pacman_color_ == 3) pm_fg = {255, 100, 255}; // Pink
            else if (selected_pacman_color_ == 4) pm_fg = {255, 50, 50}; // Red
            else pm_fg = getRainbowColor(0.0); // Rainbow
            c1.fg = pm_fg;
            c2.fg = pm_fg;
            
            if (pacman_.animFrame() == 0 || pacman_.animFrame() == 2) {
                if (use_nerd_fonts_) {
                    switch (pacman_.currentDirection) {
                        case Direction::Left:  c1.glyph = "Ɔ"; break;
                        case Direction::Right: c1.glyph = "C"; break;
                        case Direction::Down:  c1.glyph = "∩"; break;
                        case Direction::Up:    c1.glyph = "∪"; break;
                        default:               c1.glyph = "Ɔ"; break;
                    }
                } else {
                    switch (pacman_.currentDirection) {
                        case Direction::Left:  c1.glyph = ">"; break;
                        case Direction::Right: c1.glyph = "<"; break;
                        case Direction::Down:  c1.glyph = "^"; break;
                        case Direction::Up:    c1.glyph = "v"; break;
                        default:               c1.glyph = ">"; break;
                    }
                }
            } else {
                c1.glyph = use_nerd_fonts_ ? "●" : "O";
            }
            c2.glyph = " ";
            
            int r = vp.base_row + vy;
            int c = vp.base_col + vx * Config::TILE_RENDER_W;
            setCell(r, c, c1);
            setCell(r, c + 1, c2);
        }
    } else if (phase_ == GamePhase::PacDying) {
        int vx = pacman_.position.x - vp.start_x;
        int vy = pacman_.position.y - vp.start_y;
        
        if (vx >= 0 && vx < vp.visible_cols && vy >= 0 && vy < vp.visible_rows) {
            Cell c1, c2;
            c1.bg = {0, 0, 0};
            c2.bg = {0, 0, 0};
            Color pm_fg;
            if (selected_pacman_color_ == 0) pm_fg = {255, 255, 0}; // Yellow
            else if (selected_pacman_color_ == 1) pm_fg = {0, 255, 255}; // Cyan
            else if (selected_pacman_color_ == 2) pm_fg = {0, 255, 0}; // Green
            else if (selected_pacman_color_ == 3) pm_fg = {255, 100, 255}; // Pink
            else if (selected_pacman_color_ == 4) pm_fg = {255, 50, 50}; // Red
            else pm_fg = getRainbowColor(0.0); // Rainbow
            c1.fg = pm_fg;
            c2.fg = pm_fg;
            
            int step = (Config::DEATH_ANIM_DURATION - phase_timer_) / 300;
            if (use_nerd_fonts_) {
                switch (step) {
                    case 0:  c1.glyph = "◠"; break;
                    case 1:  c1.glyph = "◡"; break;
                    case 2:  c1.glyph = "○"; break;
                    case 3:  c1.glyph = "·"; break;
                    default: c1.glyph = " "; break;
                }
            } else {
                switch (step) {
                    case 0:  c1.glyph = "o"; break;
                    case 1:  c1.glyph = "x"; break;
                    case 2:  c1.glyph = "*"; break;
                    case 3:  c1.glyph = "."; break;
                    default: c1.glyph = " "; break;
                }
            }
            c2.glyph = " ";
            int r = vp.base_row + vy;
            int c = vp.base_col + vx * Config::TILE_RENDER_W;
            setCell(r, c, c1);
            setCell(r, c + 1, c2);
        }
    }
    
    // Draw Ghosts
    for (size_t i = 0; i < ghosts_.size(); ++i) {
        const auto& g = ghosts_[i];
        if (phase_ == GamePhase::PacDying) continue;
        if (level_ == 20 && i > 0) continue;
        
        int vx = g.position.x - vp.start_x;
        int vy = g.position.y - vp.start_y;
        
        if (level_ == 20) {
            std::array<std::string, 4> glitch_glyphs = {"█", "░", "▒", "▓"};
            if (!use_nerd_fonts_) {
                glitch_glyphs = {"X", "#", "@", "%"};
            }
            
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    int cvx = vx + dx;
                    int cvy = vy + dy;
                    if (cvx >= 0 && cvx < vp.visible_cols && cvy >= 0 && cvy < vp.visible_rows) {
                        int r = vp.base_row + cvy;
                        int c = vp.base_col + cvx * Config::TILE_RENDER_W;
                        Cell c1, c2;
                        c1.bg = {0, 0, 0};
                        c2.bg = {0, 0, 0};
                        
                        uint8_t gr = (current_time_ms_ / 100 % 2 == 0) ? 255 : 50;
                        uint8_t gg = (current_time_ms_ / 150 % 2 == 0) ? 50 : 0;
                        uint8_t gb = (current_time_ms_ / 200 % 2 == 0) ? 255 : 0;
                        c1.fg = {gr, gg, gb};
                        c2.fg = {gr, gg, gb};
                        
                        c1.glyph = glitch_glyphs[(current_time_ms_ / 100 + dx + dy) % 4];
                        c2.glyph = " ";
                        setCell(r, c, c1);
                        setCell(r, c + 1, c2);
                    }
                }
            }
            continue;
        }
        
        if (vx >= 0 && vx < vp.visible_cols && vy >= 0 && vy < vp.visible_rows) {
            Cell c1, c2;
            c1.bg = {0, 0, 0};
            c2.bg = {0, 0, 0};
            
            int r = vp.base_row + vy;
            int c = vp.base_col + vx * Config::TILE_RENDER_W;
            
            if (g.mode == GhostMode::Frightened) {
                c1.glyph = use_nerd_fonts_ ? "ᗣ" : "M";
                c2.glyph = " ";
                if (g.isFrightenedFlashing() && ((phase_timer_ / 200) % 2 == 0)) {
                    c1.fg = {255, 255, 255};
                } else {
                    c1.fg = {33, 33, 255};
                }
            } else if (g.mode == GhostMode::Eaten) {
                c1.glyph = "o";
                c2.glyph = "o";
                c1.fg = {255, 255, 255};
            } else {
                c1.glyph = use_nerd_fonts_ ? "ᗣ" : "M";
                c2.glyph = " ";
                switch (g.personality) {
                    case GhostPersonality::Blinky: c1.fg = {255, 0, 0}; break;
                    case GhostPersonality::Pinky:  c1.fg = {255, 184, 255}; break;
                    case GhostPersonality::Inky:   c1.fg = {0, 255, 255}; break;
                    case GhostPersonality::Clyde:  c1.fg = {255, 184, 82}; break;
                }
            }
            setCell(r, c, c1);
            setCell(r, c + 1, c2);
        }
    }
}

void GameEngine::renderHUD() {
    std::string score_str = " " + username_ + "'s SCORE: " + std::to_string(score_);
    if (muted_) {
        score_str += " [MUTED]";
    }
    std::string high_str = "HIGH: " + std::to_string(high_score_) + " ";
    int center_col = render_width_ / 2;
    
    // Top HUD bar
    fillRow(0, {255, 255, 255}, {0, 0, 0});
    drawGradientString(0, 2, score_str, {0, 255, 128}, {0, 255, 255}, {0, 0, 0});
    drawGradientString(0, render_width_ - high_str.size() - 2, high_str, {255, 50, 50}, {255, 120, 0}, {0, 0, 0});
    
    // Active power-up indicators
    std::string powerup_str = "";
    Color powerup_color = {255, 255, 255};
    if (speed_boost_timer_ > 0) {
        int sec = (speed_boost_timer_ + 999) / 1000;
        powerup_str = "⚡ SPEED: " + std::to_string(sec) + "s";
        powerup_color = {255, 215, 0}; // Gold/Yellow
    } else if (ghost_freeze_timer_ > 0) {
        int sec = (ghost_freeze_timer_ + 999) / 1000;
        powerup_str = "❄️ FREEZE: " + std::to_string(sec) + "s";
        powerup_color = {100, 255, 255}; // Light Blue
    }
    if (!powerup_str.empty()) {
        drawString(0, center_col - powerup_str.length() / 2, powerup_str, powerup_color, {0, 0, 0});
    }
    
    // Bottom HUD bar
    std::string lives_str = " LIVES: ";
    for (int i = 0; i < lives_; ++i) {
        lives_str += use_nerd_fonts_ ? "♥ " : "<3";
    }
    std::string level_str = "LEVEL " + std::to_string(level_) + " ";
    
    fillRow(render_height_ - 1, {255, 255, 255}, {0, 0, 0});
    drawGradientString(render_height_ - 1, 2, lives_str, {255, 255, 0}, {255, 140, 0}, {0, 0, 0});
    drawGradientString(render_height_ - 1, render_width_ - level_str.size() - 2, level_str, {255, 255, 255}, {180, 180, 180}, {0, 0, 0});
}

void GameEngine::renderMainMenu() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    // Get current system time in ms for animation
    auto now_ms = current_time_ms_;
    
    // Gradient cycle over time
    const double PI_CONST = 3.14159265358979323846;
    double phase = (now_ms % 5000) / 5000.0 * 2.0 * PI_CONST; // cycles every 5 seconds
    
    auto getYellowGradient = [phase, PI_CONST](double offset) -> Color {
        double g = 195.0 + std::sin(phase + offset) * 60.0;
        return { 255, static_cast<uint8_t>(g), 0 };
    };
    
    auto getCyanGradient = [phase, PI_CONST](double offset) -> Color {
        double g = 207.0 + std::sin(phase + offset) * 47.0;
        double b = 227.0 + std::cos(phase + offset) * 27.0;
        return { 0, static_cast<uint8_t>(g), static_cast<uint8_t>(b) };
    };
    
    
    if (render_width_ >= 66 && render_height_ >= 22) {
        // Render beautiful arcade TUI with dynamic gradients
        // ASCII Title with vertical gradient shifting over time (yellow/gold)
        if (use_nerd_fonts_) {
            drawString(center_row - 10, center_col - 31, "  ██████╗  █████╗  ██████╗████████╗███████╗██████╗ ███╗   ███╗", getYellowGradient(0.0 * 0.3));
            drawString(center_row - 9, center_col - 31, "  ██╔══██╗██╔══██╗██╔════╝╚══██╔══╝██╔════╝██╔══██╗████╗ ████║", getYellowGradient(1.0 * 0.3));
            drawString(center_row - 8, center_col - 31, "  ██████╔╝███████║██║        ██║   █████╗  ██████╔╝██╔████╔██║", getYellowGradient(2.0 * 0.3));
            drawString(center_row - 7, center_col - 31, "  ██╔═══╝ ██╔══██║██║        ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║", getYellowGradient(3.0 * 0.3));
            drawString(center_row - 6, center_col - 31, "  ██║     ██║  ██║╚██████╗   ██║   ███████╗██║  ██║██║ ╚═╝ ██║", getYellowGradient(4.0 * 0.3));
            drawString(center_row - 5, center_col - 31, "  ╚═╝     ╚═╝  ╚═╝ ╚═════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝", getYellowGradient(5.0 * 0.3));
        } else {
            drawString(center_row - 10, center_col - 27, "  #####    ###    ###   #####  #####  ####   #   #   #  ", getYellowGradient(0.0 * 0.3));
            drawString(center_row - 9, center_col - 27, "  #    #  #   #  #   #    #    #      #   #  ## ##   #  ", getYellowGradient(1.0 * 0.3));
            drawString(center_row - 8, center_col - 27, "  #####   #####  #        #    ####   ####   # # #   #  ", getYellowGradient(2.0 * 0.3));
            drawString(center_row - 7, center_col - 27, "  #       #   #  #   #    #    #      # #    #   #   #  ", getYellowGradient(3.0 * 0.3));
            drawString(center_row - 6, center_col - 27, "  #       #   #   ###     #    #####  #  ##  #   #   #  ", getYellowGradient(4.0 * 0.3));
            drawString(center_row - 5, center_col - 27, "                                                        ", getYellowGradient(5.0 * 0.3));
        }
        
        // Draw box border & clear background using dynamic shifting colors (cyan/blue)
        Color border_color_top = getCyanGradient(6.0 * 0.3);

        std::string dash_ch = use_nerd_fonts_ ? "═" : "-";
        std::string mid_line;
        for (int i = 0; i < 58; ++i) {
            mid_line += dash_ch;
        }

        // Clear the box interior first (height 14: rows center_row-4 .. center_row+9)
        drawBox(center_row - 4, center_col - 30, 60, 14, {0, 0, 0}, {0, 0, 0});

        std::string tl = use_nerd_fonts_ ? "╔" : "+";
        std::string tr = use_nerd_fonts_ ? "╗" : "+";
        std::string vt = use_nerd_fonts_ ? "║" : "|";
        std::string bl = use_nerd_fonts_ ? "╚" : "+";
        std::string br = use_nerd_fonts_ ? "╝" : "+";

// Top border with the version framed on the right: +──...─v1.3.1─+
        std::string ver = "v1.3.1";
        std::string ver_block = dash_ch + ver + dash_ch;
        int vb_glyphs = 0;
        for (size_t j = 0; j < ver_block.size(); ) {
            j += utf8SequenceLength(ver_block[j]);
            ++vb_glyphs;
        }
        int fill = 58 - vb_glyphs;
        std::string top_prefill;
        for (int i = 0; i < fill; ++i) {
            top_prefill += dash_ch;
        }
        drawString(center_row - 4, center_col - 30, tl + top_prefill + ver_block + tr, border_color_top);
        for (int r = center_row - 3; r <= center_row + 8; ++r) {
            Color side_color = getCyanGradient((6.0 + (r - (center_row - 3)) * 0.5) * 0.3);
            drawString(r, center_col - 30, vt, side_color);
            drawString(r, center_col + 29, vt, side_color);
        }
        Color border_color_bottom = getCyanGradient((6.0 + 12 * 0.5) * 0.3);
        drawString(center_row + 9, center_col - 30, bl + mid_line + br, border_color_bottom);

        std::string install_text = isInstalledLocally() ? "Uninstall pacterm" : "Install pacterm locally";
        std::string user_text = "Username:       " + username_;
        
        std::array<std::string, 7> main_options = {
            "Start Game",
            user_text,
            "Stats",
            "Redeem Code",
            install_text,
            "Settings",
            "Quit"
        };

        // Fixed column (no centering), items start 1 row below the top border
        int block_left = center_col - 13;
        
        for (int i = 0; i < 7; ++i) {
            Color fg = {255, 255, 255};
            std::string prefix = "  ";
            bool bold = false;
            if (i == main_menu_selection_) {
                if (click_feedback_timer_ > 0) {
                    fg = {255, 255, 255};
                    bold = true;
                } else {
                    double sel_phase = phase * 2.0;
                    double r = 255.0;
                    double g = 180.0 + std::sin(sel_phase) * 75.0;
                    fg = { static_cast<uint8_t>(r), static_cast<uint8_t>(g), 0 };
                }
                prefix = "> ";
            }
            if (isMouseHovering(center_row - 2 + i, block_left, prefix + main_options[i])) {
                fg.r = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.r) + 50));
                fg.g = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.g) + 50));
                fg.b = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.b) + 50));
                bold = true;
            }
            drawString(center_row - 2 + i, block_left, prefix + main_options[i], fg, {0, 0, 0}, bold);
        }
        
        if (!main_menu_message_.empty()) {
            drawString(center_row + 5, center_col - main_menu_message_.length() / 2, main_menu_message_, {0, 255, 0});
        }
        
        // Credits (white/cyan)
        Color label_color = {0, 255, 255};
        Color val_color = {255, 255, 255};
        
        drawString(center_row + 6, center_col - 18, "Developed by : ", label_color);
        drawString(center_row + 6, center_col - 3, "Wael Amrani Zerrifi", val_color);
        
        drawString(center_row + 7, center_col - 18, "Website      : ", label_color);
        drawString(center_row + 7, center_col - 3, "https://wael.work.gd/pacterm", val_color);
        
        drawString(center_row + 8, center_col - 18, "License      : ", label_color);
        drawString(center_row + 8, center_col - 3, "GPL-3.0-or-later", val_color);
    } else {
        // Fallback for small screens
        Color title_color = getYellowGradient(0.0);
        drawString(center_row - 4, center_col - 10, "  pacterm  ", title_color);
        drawString(center_row - 4, center_col + 6, "[v1.3.1]", {100, 100, 100});
        
        std::string install_text = isInstalledLocally() ? "Uninstall pacterm" : "Install pacterm";
        std::string user_text = "User: " + username_;
        
        std::array<std::string, 7> main_options = {
            "Start Game",
            user_text,
            "Stats",
            "Redeem Code",
            install_text,
            "Settings",
            "Quit"
        };
        
        // Fixed column, no centering
        int block_left = center_col - 10;
        
        for (int i = 0; i < 7; ++i) {
            Color fg = {255, 255, 255};
            std::string prefix = "  ";
            bool bold = false;
            if (i == main_menu_selection_) {
                if (click_feedback_timer_ > 0) {
                    fg = {255, 255, 255};
                    bold = true;
                } else {
                    double sel_phase = phase * 2.0;
                    double r = 255.0;
                    double g = 180.0 + std::sin(sel_phase) * 75.0;
                    fg = { static_cast<uint8_t>(r), static_cast<uint8_t>(g), 0 };
                }
                prefix = "> ";
            }
            if (isMouseHovering(center_row - 2 + i, block_left, prefix + main_options[i])) {
                fg.r = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.r) + 50));
                fg.g = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.g) + 50));
                fg.b = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.b) + 50));
                bold = true;
            }
            drawString(center_row - 2 + i, block_left, prefix + main_options[i], fg, {0, 0, 0}, bold);
        }
        
        if (!main_menu_message_.empty()) {
            drawString(center_row + 5, center_col - main_menu_message_.length() / 2, main_menu_message_, {0, 255, 0});
        }
        
        // Credits for small screen (soft grey/white)
        drawString(center_row + 6, center_col - 17, "Developed by: Wael Amrani Zerrifi", {200, 200, 200});
        drawString(center_row + 7, center_col - 15, "Website: wael.work.gd/pacterm", {200, 200, 200});
        drawString(center_row + 8, center_col - 10, "License: GPL-3.0+", {200, 200, 200});
    }
}

void GameEngine::renderSettings() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    const std::array<std::string, 6> theme_names = {
        "Classic Yellow", "Cyan", "Green", "Pink", "Red", "Rainbow"
    };

    if (render_width_ >= 66 && render_height_ >= 26) {
        drawDoubleBorderBox(center_row - 5, center_col - 22, 44, 11, {0, 255, 255}, {0, 0, 0});
        drawString(center_row - 5, center_col - 5, " SETTINGS ", {255, 255, 0});

        std::string nf_text = "Nerd Fonts:     " + std::string(use_nerd_fonts_ ? "ON" : "OFF");
        std::string sound_text = "Sound:          " + std::string(muted_ ? "OFF" : "ON");
        std::string theme_text = "General Theme:  " + theme_names[selected_general_theme_];
        std::string pm_text = "Pac-Man Theme:  " + theme_names[selected_pacman_color_];

        const std::array<std::string, 7> options = {
            theme_text,
            nf_text,
            sound_text,
            pm_text,
            "Configure Keys",
            "Reset",
            "Back to Menu"
        };

        for (int i = 0; i < 7; ++i) {
            Color fg = {255, 255, 255};
            std::string prefix = "  ";
            bool bold = false;
            if (i == settings_selection_) {
                fg = {255, 200, 0};
                prefix = "> ";
            }
            if (isMouseHovering(center_row - 3 + i, center_col - 16, prefix + options[i])) {
                fg.r = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.r) + 50));
                fg.g = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.g) + 50));
                fg.b = static_cast<uint8_t>(std::min(255, static_cast<int>(fg.b) + 50));
                bold = true;
            }
            std::string draw_text = prefix + options[i];
            if ((i == 0 && settings_selection_ == 0) || (i == 3 && settings_selection_ == 3)) {
                draw_text = "> " + (i == 0 ? theme_text : pm_text) + " <";
            }
            drawString(center_row - 3 + i, center_col - 16, draw_text, fg, {0, 0, 0}, bold);
        }

        if (!main_menu_message_.empty()) {
            drawString(center_row + 6, center_col - static_cast<int>(main_menu_message_.length()) / 2, main_menu_message_, {0, 255, 0});
        }
    } else {
        // Small screens fallback
        drawString(center_row - 7, center_col - 4, " SETTINGS ", {255, 255, 0});

        const std::array<std::string, 7> options = {
            "Theme: " + theme_names[selected_general_theme_],
            "Nerd Fonts: " + std::string(use_nerd_fonts_ ? "ON" : "OFF"),
            "Sound: " + std::string(muted_ ? "OFF" : "ON"),
            "Pac-Man Theme: " + theme_names[selected_pacman_color_],
            "Configure Keys",
            "Reset",
            "Back to Menu"
        };

        for (int i = 0; i < 7; ++i) {
            Color fg = {255, 255, 255};
            std::string prefix = "  ";
            if (i == settings_selection_) {
                fg = {255, 200, 0};
                prefix = "> ";
            }
            drawString(center_row - 4 + i, center_col - 13, prefix + options[i], fg, {0, 0, 0}, false);
        }

        if (!main_menu_message_.empty()) {
            drawString(center_row + 5, center_col - static_cast<int>(main_menu_message_.length()) / 2, main_menu_message_, {0, 255, 0});
        }
    }
}

void GameEngine::activateSettingsSelection() {
    switch (settings_selection_) {
        case 0: // General Theme
        {
            int temp = selected_general_theme_;
            do {
                temp = (temp + 1) % 6;
            } while (isColorLocked(temp));
            selected_general_theme_ = temp;
            saveHighScore();
            break;
        }
        case 1: // Nerd Fonts
            use_nerd_fonts_ = !use_nerd_fonts_;
            saveHighScore();
            break;
        case 2: // Sound
            muted_ = !muted_;
            saveHighScore();
            break;
        case 3: // Pac-Man Theme
        {
            int temp = selected_pacman_color_;
            do {
                temp = (temp + 1) % 6;
            } while (isColorLocked(temp));
            selected_pacman_color_ = temp;
            saveHighScore();
            break;
        }
        case 4: // Configure Keys
            phase_ = GamePhase::KeyConfig;
            key_config_selection_ = 0;
            is_binding_ = false;
            break;
        case 5: // Reset
        {
            const char* home_env = std::getenv("HOME");
            if (home_env) {
                try {
                    std::filesystem::remove_all(std::filesystem::path(home_env) / ".pacterm");
                } catch (...) {}
            }
            try {
                std::filesystem::remove(getCacheFilePath());
            } catch (...) {}
            high_score_ = 0;
            muted_ = false;
            use_nerd_fonts_ = true;
            if (isAzertyLayout()) {
                custom_key_up_ = 'z';
                custom_key_down_ = 's';
                custom_key_left_ = 'q';
                custom_key_right_ = 'd';
            } else {
                custom_key_up_ = 'w';
                custom_key_down_ = 's';
                custom_key_left_ = 'a';
                custom_key_right_ = 'd';
            }
            custom_key_pause_ = 'p';
            unlocked_rainbow_ = false;
            selected_general_theme_ = 0;
            games_played_ = 0;
            dots_eaten_ = 0;
            ghosts_eaten_ = 0;
            deaths_ = 0;
            power_pellets_ = 0;
            time_played_ms_ = 0;
            username_ = "Wael";
            const char* env_nf = std::getenv("PACMAN_NERD_FONTS");
            if (env_nf && std::string(env_nf) == "0") {
                use_nerd_fonts_ = false;
            }
            rebuildKeybindings();
            main_menu_message_ = "Everything reset successfully!";
            main_menu_msg_timer_ = 3000;
            break;
        }
        case 6: // Back
            phase_ = GamePhase::MainMenu;
            main_menu_message_ = "";
            fade_animation_.fadeIn({255, 255, 255}, 300);
            break;
    }
}

void GameEngine::renderRedeem() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    drawDoubleBorderBox(center_row - 3, center_col - 18, 36, 6, {255, 215, 0}, {0, 0, 0});
    drawString(center_row - 3, center_col - 7, " REDEEM CODE ", {255, 215, 0});
    drawString(center_row - 1, center_col - 16, "> " + redeem_input_ + "_", {255, 255, 255});
    if (!redeem_result_.empty()) {
        Color result_c = redeem_result_valid_ ? Color{0, 255, 0} : Color{255, 50, 50};
        drawString(center_row, center_col - static_cast<int>(redeem_result_.length()) / 2, redeem_result_, result_c);
    }
    drawString(center_row + 1, center_col - 17, "    ESC: Cancel  ENTER: Redeem    ", {150, 150, 150});
}

void GameEngine::renderStats() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    // Format total play time as H:MM:SS
    int total_sec = time_played_ms_ / 1000;
    int hours = total_sec / 3600;
    int minutes = (total_sec % 3600) / 60;
    int seconds = total_sec % 60;
    char time_buf[32];
    std::snprintf(time_buf, sizeof(time_buf), "%d:%02d:%02d", hours, minutes, seconds);

    if (render_width_ >= 66 && render_height_ >= 22) {
        drawDoubleBorderBox(center_row - 7, center_col - 26, 53, 15, {0, 200, 255}, {0, 0, 0});
        drawString(center_row - 7, center_col - 3, " STATS ", {255, 255, 0});

        std::string labels[8] = {
            "High Score", "Games Played", "Ghosts Eaten", "Deaths",
            "Best Level", "Dots Eaten", "Power Pellets", "Time Played"
        };
        std::string values[8] = {
            std::to_string(high_score_), std::to_string(games_played_),
            std::to_string(ghosts_eaten_), std::to_string(deaths_),
            std::to_string(max_unlocked_level_), std::to_string(dots_eaten_),
            std::to_string(power_pellets_), time_buf
        };

        // 2 pairs per row; each stat drawn as a label line above a value line.
        int label_rows[4] = { center_row - 5, center_row - 2, center_row + 1, center_row + 4 };
        int col_left  = center_col - 22;
        int col_right = center_col + 2;

        for (int i = 0; i < 4; ++i) {
            Color label_c = {180, 180, 200};
            Color val_c = {255, 255, 255};
            // left cell
            int li = i * 2;
            drawString(label_rows[i],     col_left,  labels[li], label_c, {0, 0, 0}, false);
            drawString(label_rows[i] + 1, col_left,  values[li], val_c, {0, 0, 0}, true);
            // right cell
            int ri = li + 1;
            drawString(label_rows[i],     col_right, labels[ri], label_c, {0, 0, 0}, false);
            drawString(label_rows[i] + 1, col_right, values[ri], val_c, {0, 0, 0}, true);
        }
    } else {
        // Compact list (one stat per line, label + value)
        std::string list[6] = {
            "High Score:     " + std::to_string(high_score_),
            "Best Level:     " + std::to_string(max_unlocked_level_),
            "Games Played:   " + std::to_string(games_played_),
            "Dots Eaten:     " + std::to_string(dots_eaten_),
            "Ghosts Eaten:   " + std::to_string(ghosts_eaten_),
            "Power Pellets:  " + std::to_string(power_pellets_),
        };
        for (int i = 0; i < 6; ++i) {
            Color fg = {255, 255, 255};
            drawString(center_row - 3 + i, center_col - 18, list[i], fg, {0, 0, 0}, false);
        }
        drawString(center_row + 4, center_col - 14, "Deaths: " + std::to_string(deaths_) +
                   "   Time: " + std::to_string(total_sec) + "s", {255, 255, 255});
    }
}

void GameEngine::renderGameOver() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    // Draw a double border box for GAME OVER
    drawDoubleBorderBox(center_row - 3, center_col - 17, 34, 7, {255, 0, 0}, {0, 0, 0});
    drawString(center_row - 1, center_col - 5, "GAME OVER", {255, 0, 0});
    drawString(center_row + 1, center_col - 14, "Press ENTER to return to Menu", {255, 255, 255});
}

void GameEngine::renderGetReady() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    drawDoubleBorderBox(center_row - 2, center_col - 8, 16, 5, {255, 255, 0}, {0, 0, 0});
    drawString(center_row, center_col - 3, "READY!", {255, 255, 0});
}

void GameEngine::addScore(int points) {
    score_ += points;
    if (score_ > high_score_) {
        high_score_ = score_;
        saveHighScore();
    }
}

void GameEngine::saveHighScore() {
    // Never throw from a persistence path called from the input/render loop.
    std::filesystem::path path;
    try {
        path = getCacheFilePath();
    } catch (...) {
        return;
    }
    if (path.empty()) return;

    std::string key = "PacTermWaelSecure2026";
    std::string plaintext = std::to_string(score_ > high_score_ ? score_ : high_score_) + " " + (muted_ ? "1" : "0") + " " + (use_nerd_fonts_ ? "1" : "0") + " " +
                            std::to_string(custom_key_up_) + " " + std::to_string(custom_key_down_) + " " +
                            std::to_string(custom_key_left_) + " " + std::to_string(custom_key_right_) + " " +
                            std::to_string(custom_key_pause_) + " " +
                            std::to_string(unlocked_rainbow_ ? 1 : 0) + " " +
                            std::to_string(max_unlocked_level_) + " " +
                            std::to_string(selected_pacman_color_) + " " +
                            std::to_string(selected_general_theme_) + " " +
                            std::to_string(games_played_) + " " +
                            std::to_string(dots_eaten_) + " " +
                            std::to_string(ghosts_eaten_) + " " +
                            std::to_string(deaths_) + " " +
                            std::to_string(power_pellets_) + " " +
                            std::to_string(time_played_ms_) + " " +
                            username_;
    for (size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] ^= key[i % key.size()];
    }

    try {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(plaintext.data(), static_cast<std::streamsize>(plaintext.size()));
    } catch (...) {
        // Disk full / permission denied must never crash the game.
    }
}

void GameEngine::loadHighScore() {
    std::filesystem::path path;
    try {
        path = getCacheFilePath();
    } catch (...) {
        path.clear();
    }
    if (path.empty()) {
        return; // defaults already initialized in the header
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;

    // Bound the file size we are willing to read; guard against a corrupt or
    // maliciously huge score file (buffer overrun / runaway memory).
    const std::streamsize max_file = 4096;
    std::streamsize size = f.tellg();
    if (size <= 0) return;
    if (size > max_file) size = max_file;
    f.seekg(0, std::ios::beg);

    std::string ciphertext(static_cast<size_t>(size), '\0');
    if (!f.read(&ciphertext[0], size)) return;

    std::string key = "PacTermWaelSecure2026";
    for (size_t i = 0; i < ciphertext.size(); ++i) {
        ciphertext[i] ^= key[i % key.size()];
    }

    int temp_muted = 0;
    int temp_nf = 1;
    int k_up = 'w', k_down = 's', k_left = 'a', k_right = 'd', k_pause = 'p';
    int temp_unlocked = 0;
    int temp_use_rainbow = 0;
    int temp_max_level = 1;
    int temp_color = 0;
    int temp_general_theme = 0;
    int temp_games = 0;
    int temp_dots = 0;
    int temp_ghosts = 0;
    int temp_deaths = 0;
    int temp_power = 0;
    int temp_time = 0;
    char username_buf[128] = "";

    // Newest format (1.2.5+): score muted nf up down left right pause unlocked max_level color general_theme games dots ghosts deaths power time username
    int read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]",
                                 &high_score_, &temp_muted, &temp_nf, &k_up, &k_down, &k_left, &k_right, &k_pause,
                                 &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme, &temp_games,
                                 &temp_dots, &temp_ghosts, &temp_deaths, &temp_power, &temp_time, username_buf);

    // 1.2.4 format (no ghosts/deaths/power/time): score ... games dots username
    if (read_count < 19) {
        temp_ghosts = 0;
        temp_deaths = 0;
        temp_power = 0;
        temp_time = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]",
                                 &high_score_, &temp_muted, &temp_nf, &k_up, &k_down, &k_left, &k_right, &k_pause,
                                 &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme, &temp_games,
                                 &temp_dots, username_buf);
    }
    // Legacy 13-int format: ... unlocked use_rainbow max_level color general_theme username
    if (read_count < 15) {
        temp_max_level = 1;
        temp_color = 0;
        temp_general_theme = 0;
        temp_games = 0;
        temp_dots = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]",
                                 &high_score_, &temp_muted, &temp_nf, &k_up, &k_down, &k_left, &k_right, &k_pause,
                                 &temp_unlocked, &temp_use_rainbow, &temp_max_level, &temp_color, &temp_general_theme,
                                 username_buf);
    }
    // Legacy 12-int format: ... unlocked use_rainbow max_level color username
    if (read_count < 14) {
        temp_max_level = 1;
        temp_color = 0;
        temp_general_theme = 0;
        temp_games = 0;
        temp_dots = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]",
                                 &high_score_, &temp_muted, &temp_nf, &k_up, &k_down, &k_left, &k_right, &k_pause,
                                 &temp_unlocked, &temp_use_rainbow, &temp_max_level, &temp_color, username_buf);
    }
    // Legacy 10-int format: ... unlocked use_rainbow username
    if (read_count < 13) {
        temp_max_level = 1;
        temp_color = 0;
        temp_general_theme = 0;
        temp_games = 0;
        temp_dots = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %127[^\r\n]",
                                 &high_score_, &temp_muted, &temp_nf, &k_up, &k_down, &k_left, &k_right, &k_pause,
                                 &temp_unlocked, &temp_use_rainbow, username_buf);
    }

    if (read_count >= 3) {
        muted_ = (temp_muted != 0);
        use_nerd_fonts_ = (temp_nf != 0);
    }
    if (read_count >= 8) {
        custom_key_up_ = clampKeyCode(k_up);
        custom_key_down_ = clampKeyCode(k_down);
        custom_key_left_ = clampKeyCode(k_left);
        custom_key_right_ = clampKeyCode(k_right);
        custom_key_pause_ = clampKeyCode(k_pause);
    } else {
        if (isAzertyLayout()) {
            custom_key_up_ = 'z'; custom_key_down_ = 's';
            custom_key_left_ = 'q'; custom_key_right_ = 'd';
        } else {
            custom_key_up_ = 'w'; custom_key_down_ = 's';
            custom_key_left_ = 'a'; custom_key_right_ = 'd';
        }
        custom_key_pause_ = 'p';
    }
    if (read_count >= 10) {
        unlocked_rainbow_ = (temp_unlocked != 0);
    } else {
        unlocked_rainbow_ = false;
    }

    max_unlocked_level_ = temp_max_level;
    selected_pacman_color_ = temp_color;
    selected_general_theme_ = (read_count >= 14) ? temp_general_theme : 0;
    games_played_ = (read_count >= 15) ? temp_games : 0;
    dots_eaten_ = (read_count >= 15) ? temp_dots : 0;
    ghosts_eaten_ = (read_count >= 19) ? temp_ghosts : 0;
    deaths_ = (read_count >= 19) ? temp_deaths : 0;
    power_pellets_ = (read_count >= 19) ? temp_power : 0;
    time_played_ms_ = (read_count >= 19) ? temp_time : 0;
    if (max_unlocked_level_ < 1 || max_unlocked_level_ > 20) max_unlocked_level_ = 1;
    if (selected_pacman_color_ < 0 || selected_pacman_color_ > 5) selected_pacman_color_ = 0;
    if (isColorLocked(selected_pacman_color_)) {
        selected_pacman_color_ = 0;
    }
    if (selected_general_theme_ < 0 || selected_general_theme_ > 5) selected_general_theme_ = 0;
    if (isColorLocked(selected_general_theme_)) {
        selected_general_theme_ = 0;
    }
    if (games_played_ < 0) games_played_ = 0;
    if (dots_eaten_ < 0) dots_eaten_ = 0;
    if (ghosts_eaten_ < 0) ghosts_eaten_ = 0;
    if (deaths_ < 0) deaths_ = 0;
    if (power_pellets_ < 0) power_pellets_ = 0;
    if (time_played_ms_ < 0) time_played_ms_ = 0;

    if (read_count >= 11) {
        std::string uname(username_buf);
        if (uname.size() > 15) uname = uname.substr(0, 15);
        username_ = uname;
    } else {
        username_ = "Wael";
    }
}

std::filesystem::path GameEngine::getCacheFilePath() {
    std::filesystem::path home_dir;
    const char* home_env = std::getenv("HOME");
    if (home_env && home_env[0] != '\0') {
        home_dir = home_env;
    } else {
        home_dir = std::filesystem::current_path();
    }
    
    std::filesystem::path pacterm_dir = home_dir / ".pacterm";
    try {
        if (!std::filesystem::exists(pacterm_dir)) {
            std::filesystem::create_directories(pacterm_dir);
        }
        return pacterm_dir / "pacterm.cache";
    } catch (...) {
        return std::filesystem::current_path() / "pacterm.cache";
    }
}

bool GameEngine::isAzertyLayout() {
    // 1. Check setxkbmap output
    FILE* pipe = popen("setxkbmap -query 2>/dev/null", "r");
    if (pipe) {
        char buffer[128];
        bool found = false;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            if (line.find("layout:") != std::string::npos) {
                if (line.find("fr") != std::string::npos || 
                    line.find("be") != std::string::npos || 
                    line.find("dz") != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }
        pclose(pipe);
        if (found) return true;
    }
    // 2. Fallback to LANG environment variable check
    const char* lang = std::getenv("LANG");
    if (lang) {
        std::string lang_str(lang);
        if (lang_str.rfind("fr", 0) == 0 || lang_str.rfind("be", 0) == 0) {
            return true;
        }
    }
    return false;
}


Color GameEngine::getRainbowColor(double offset) const {
    double p = (current_time_ms_ % 5000) / 5000.0 * 2.0 * 3.14159265358979323846;
    double h = p * 2.0 + offset;
    double r = std::sin(h) * 127.0 + 128.0;
    double g = std::sin(h + 2.0944) * 127.0 + 128.0;
    double b = std::sin(h + 4.1888) * 127.0 + 128.0;
    return { static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b) };
}

Color GameEngine::applyGeneralTheme(Color fg, int row, int col) const {
    if (selected_general_theme_ == 0) return fg; // Classic: unchanged
    // Keep dim/grey/locked elements (e.g. [XX] levels, hints) unthemed so
    // lock state and de-emphasized entries stay readable.
    const uint8_t mx = fg.r > fg.g ? (fg.r > fg.b ? fg.r : fg.b) : (fg.g > fg.b ? fg.g : fg.b);
    if (mx <= 170) return fg;
    // Moving gradient: hue sweeps over time plus a subtle per-cell offset so
    // the whole menu shimmers instead of being a single flat color.
    const double PI_CONST = 3.14159265358979323846;
    const double phase = (current_time_ms_ % 5000) / 5000.0 * 2.0 * PI_CONST;
    const double offset = row * 0.08 + col * 0.04;
    if (selected_general_theme_ == 5) return getRainbowColor(offset);
    switch (selected_general_theme_) {
        case 1: { // Cyan
            double g = 207.0 + std::sin(phase + offset) * 47.0;
            double b = 227.0 + std::cos(phase + offset) * 27.0;
            return {0, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        }
        case 2: { // Green
            double r = 40.0 + std::sin(phase + offset) * 40.0;
            double b = 90.0 + std::cos(phase + offset) * 70.0;
            return {static_cast<uint8_t>(r), 255, static_cast<uint8_t>(b)};
        }
        case 3: { // Pink
            double g = 100.0 + std::sin(phase + offset) * 80.0;
            double b = 235.0 + std::cos(phase + offset) * 20.0;
            return {255, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        }
        default: { // Red
            double g = 70.0 + std::sin(phase + offset) * 50.0;
            double b = 60.0 + std::cos(phase + offset) * 50.0;
            return {255, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
        }
    }
}

bool GameEngine::isColorLocked(int color_idx) const {
    if (color_idx == 0) return false;
    if (color_idx == 1) return (max_unlocked_level_ <= 4);
    if (color_idx == 2) return (max_unlocked_level_ <= 8);
    if (color_idx == 3) return (max_unlocked_level_ <= 12);
    if (color_idx == 4) return (max_unlocked_level_ <= 16);
    if (color_idx == 5) return !unlocked_rainbow_;
    return false;
}

void GameEngine::startLevel(int lvl) {
    level_ = lvl;
    map_.loadLevel(lvl);
    
    special_item_active_ = false;
    cherry_spawn_timer_ = 15000 + std::rand() % 15000;
    apple_spawn_timer_ = 30000 + std::rand() % 30000;
    heart_spawn_timer_ = 25000 + std::rand() % 35000;
    
    popups_.clear();
    particles_.clear();
    speed_boost_timer_ = 0;
    ghost_freeze_timer_ = 0;
    
    // Clear custom mechanics states
    acid_trails_.clear();
    lava_tiles_.clear();
    dash_cooldown_ = 0;
    
    // Setup Cyber Portals (Theme 3 - Levels 9-12)
    portal_A1_ = {0, 0};
    portal_A2_ = {0, 0};
    portal_B1_ = {0, 0};
    portal_B2_ = {0, 0};
    if (level_ >= 9 && level_ <= 12) {
        for (int y = 2; y < Config::MAP_HEIGHT / 2; ++y) {
            for (int x = 2; x < Config::MAP_WIDTH / 2; ++x) {
                if (map_.getTile(x, y) == TileType::Empty) { portal_A1_ = {x, y}; break; }
            }
            if (portal_A1_.x != 0) break;
        }
        for (int y = Config::MAP_HEIGHT - 3; y >= Config::MAP_HEIGHT / 2; --y) {
            for (int x = Config::MAP_WIDTH - 3; x >= Config::MAP_WIDTH / 2; --x) {
                if (map_.getTile(x, y) == TileType::Empty) { portal_A2_ = {x, y}; break; }
            }
            if (portal_A2_.x != 0) break;
        }
        for (int y = 2; y < Config::MAP_HEIGHT / 2; ++y) {
            for (int x = Config::MAP_WIDTH - 3; x >= Config::MAP_WIDTH / 2; --x) {
                if (map_.getTile(x, y) == TileType::Empty) { portal_B1_ = {x, y}; break; }
            }
            if (portal_B1_.x != 0) break;
        }
        for (int y = Config::MAP_HEIGHT - 3; y >= Config::MAP_HEIGHT / 2; --y) {
            for (int x = 2; x < Config::MAP_WIDTH / 2; ++x) {
                if (map_.getTile(x, y) == TileType::Empty) { portal_B2_ = {x, y}; break; }
            }
            if (portal_B2_.x != 0) break;
        }
    }
}

void GameEngine::spawnSpecialItem(TileType type) {
    // Run BFS from Pacman spawn point to find all reachable cells
    std::vector<Vec2> reachable_positions;
    std::queue<Vec2> q;
    std::vector<std::vector<bool>> visited(Config::MAP_HEIGHT, std::vector<bool>(Config::MAP_WIDTH, false));
    
    Vec2 start_pos = {13, 23};
    q.push(start_pos);
    visited[start_pos.y][start_pos.x] = true;
    
    while (!q.empty()) {
        Vec2 curr = q.front();
        q.pop();
        
        // Pacman cannot enter the ghost house (rows 12-16, cols 10-17)
        bool inside_ghost_house = (curr.y >= 12 && curr.y <= 16 && curr.x >= 10 && curr.x <= 17);
        if (!inside_ghost_house && map_.getTile(curr) == TileType::Empty) {
            reachable_positions.push_back(curr);
        }
        
        std::array<Vec2, 4> neighbors = {{
            {curr.x + 1, curr.y},
            {curr.x - 1, curr.y},
            {curr.x, curr.y + 1},
            {curr.x, curr.y - 1}
        }};
        
        for (auto next : neighbors) {
            // wrap coordinate if outside map limits
            if (next.x < 0) next.x = Config::MAP_WIDTH - 1;
            if (next.x >= Config::MAP_WIDTH) next.x = 0;
            
            if (next.y >= 0 && next.y < Config::MAP_HEIGHT) {
                if (!visited[next.y][next.x]) {
                    TileType t = map_.getTile(next.x, next.y);
                    if (t != TileType::Wall && t != TileType::GhostDoor) {
                        visited[next.y][next.x] = true;
                        q.push(next);
                    }
                }
            }
        }
    }
    
    if (reachable_positions.empty()) return;
    
    // Choose a random position
    int idx = std::rand() % reachable_positions.size();
    Vec2 pos = reachable_positions[idx];
    
    // Set tile
    map_.setTile(pos, type);
    
    // Record special item state
    special_item_active_ = true;
    special_item_pos_ = pos;
    special_item_type_ = type;
    special_item_timer_ = 10000; // Active for 10 seconds!
}


void GameEngine::rebuildKeybindings() {
    key_to_action_.clear();
    
    auto bind_custom = [this](int k, GameAction action) {
        key_to_action_[k] = action;
        if (k >= 'a' && k <= 'z') {
            key_to_action_[std::toupper(k)] = action;
        } else if (k >= 'A' && k <= 'Z') {
            key_to_action_[std::tolower(k)] = action;
        }
    };
    
    bind_custom(custom_key_up_, GameAction::Up);
    bind_custom(custom_key_down_, GameAction::Down);
    bind_custom(custom_key_left_, GameAction::Left);
    bind_custom(custom_key_right_, GameAction::Right);
    bind_custom(custom_key_pause_, GameAction::Pause);
    
    // Also always bind standard fallback Arrow keys for convenience
    key_to_action_[1000] = GameAction::Up;    // ARROW_UP
    key_to_action_[1001] = GameAction::Down;  // ARROW_DOWN
    key_to_action_[1003] = GameAction::Left;  // ARROW_LEFT
    key_to_action_[1002] = GameAction::Right; // ARROW_RIGHT
    
    // And standard fallback controls (Vim keys)
    key_to_action_['k'] = GameAction::Up;
    key_to_action_['K'] = GameAction::Up;
    key_to_action_['j'] = GameAction::Down;
    key_to_action_['J'] = GameAction::Down;
    key_to_action_['h'] = GameAction::Left;
    key_to_action_['H'] = GameAction::Left;
    key_to_action_['l'] = GameAction::Right;
    key_to_action_['L'] = GameAction::Right;
    
    // Pause fallback keys
    key_to_action_['p'] = GameAction::Pause;
    key_to_action_['P'] = GameAction::Pause;
}

std::string GameEngine::getKeyName(int k) {
    if (k == ' ') return "SPACE";
    if (k == '\n' || k == '\r') return "ENTER";
    if (k == 27) return "ESC";
    if (k == 1000) return "ARROW_UP";
    if (k == 1001) return "ARROW_DOWN";
    if (k == 1002) return "ARROW_RIGHT";
    if (k == 1003) return "ARROW_LEFT";
    if (k >= 32 && k <= 126) {
        std::string s = "";
        s += (char)std::toupper(k);
        return s;
    }
    return "KEY_" + std::to_string(k);
}

void GameEngine::renderScreensaver() {
    clearBuffer({0, 0, 0});
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    // Draw ASCII Title from Main Menu
    auto now_ms = current_time_ms_;
    const double PI_CONST = 3.14159265358979323846;
    double phase = (now_ms % 5000) / 5000.0 * 2.0 * PI_CONST;
    auto getYellowGradient = [phase, PI_CONST](double offset) -> Color {
        double g = 195.0 + std::sin(phase + offset) * 60.0;
        return { 255, static_cast<uint8_t>(g), 0 };
    };

    if (use_nerd_fonts_) {
        drawString(center_row - 10, center_col - 31, "  ██████╗  █████╗  ██████╗████████╗███████╗██████╗ ███╗   ███╗", getYellowGradient(0.0 * 0.3));
        drawString(center_row - 9, center_col - 31, "  ██╔══██╗██╔══██╗██╔════╝╚══██╔══╝██╔════╝██╔══██╗████╗ ████║", getYellowGradient(1.0 * 0.3));
        drawString(center_row - 8, center_col - 31, "  ██████╔╝███████║██║        ██║   █████╗  ██████╔╝██╔████╔██║", getYellowGradient(2.0 * 0.3));
        drawString(center_row - 7, center_col - 31, "  ██╔═══╝ ██╔══██║██║        ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║", getYellowGradient(3.0 * 0.3));
        drawString(center_row - 6, center_col - 31, "  ██║     ██║  ██║╚██████╗   ██║   ███████╗██║  ██║██║ ╚═╝ ██║", getYellowGradient(4.0 * 0.3));
        drawString(center_row - 5, center_col - 31, "  ╚═╝     ╚═╝  ╚═╝ ╚═════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝", getYellowGradient(5.0 * 0.3));
    } else {
        drawString(center_row - 10, center_col - 27, "  #####    ###    ###   #####  #####  ####   #   #   #  ", getYellowGradient(0.0 * 0.3));
        drawString(center_row - 9, center_col - 27, "  #    #  #   #  #   #    #    #      #   #  ## ##   #  ", getYellowGradient(1.0 * 0.3));
        drawString(center_row - 8, center_col - 27, "  #####   #####  #        #    ####   ####   # # #   #  ", getYellowGradient(2.0 * 0.3));
        drawString(center_row - 7, center_col - 27, "  #       #   #  #   #    #    #      # #    #   #   #  ", getYellowGradient(3.0 * 0.3));
        drawString(center_row - 6, center_col - 27, "  #       #   #   ###     #    #####  #  ##  #   #   #  ", getYellowGradient(4.0 * 0.3));
    }
    
    std::string sub = "S C R E E N S A V E R";
    drawString(center_row - 3, center_col - sub.length() / 2, sub, {255, 255, 0});
    
    // Draw dots across the animation row (center_row)
    int anim_row = center_row;
    for (int col = 4; col < render_width_ - 4; ++col) {
        bool eaten = false;
        if (screensaver_dir_ == 1) {
            if (col <= screensaver_x_) eaten = true;
        } else {
            if (col >= screensaver_x_) eaten = true;
        }
        if (!eaten && (col % 4 == 0)) {
            Cell dot_cell;
            dot_cell.glyph = use_nerd_fonts_ ? "·" : ".";
            dot_cell.fg = {255, 183, 174};
            dot_cell.bg = {0, 0, 0};
            setCell(anim_row, col, dot_cell);
        }
    }
    
    // Draw Pacman
    int pm_frame = (int)(screensaver_x_ * 1.5) % 2;
    Cell pm_cell;
    pm_cell.bg = {0, 0, 0};
    pm_cell.fg = (selected_general_theme_ == 5) ? getRainbowColor(screensaver_x_ * 0.05) : Color{255, 255, 0};
    if (pm_frame == 0) {
        pm_cell.glyph = use_nerd_fonts_ ? (screensaver_dir_ == 1 ? "C" : "Ɔ") : (screensaver_dir_ == 1 ? "<" : ">");
    } else {
        pm_cell.glyph = use_nerd_fonts_ ? "●" : "O";
    }
    
    int pm_x = (int)screensaver_x_;
    if (pm_x >= 4 && pm_x < render_width_ - 4) {
        setCell(anim_row, pm_x, pm_cell);
    }
    
    // Ghosts positions (relative to Pacman)
    struct ScreensaverGhost {
        int offset;
        Color color;
        bool frightened;
    };

    // Fixed array: avoids a per-frame heap allocation on the screensaver path.
    std::array<ScreensaverGhost, 4> s_ghosts;
    if (screensaver_dir_ == 1) {
        s_ghosts = {{
            {-6,  {255, 0, 0},      false},
            {-10, {255, 184, 255},  false},
            {-14, {0, 255, 255},    false},
            {-18, {255, 184, 82},   false}
        }};
    } else {
        s_ghosts = {{
            {-6,  {33, 33, 255},    true},
            {-10, {33, 33, 255},    true},
            {-14, {33, 33, 255},    true},
            {-18, {33, 33, 255},    true}
        }};
    }
    
    for (const auto& sg : s_ghosts) {
        int gx = pm_x + sg.offset;
        if (gx >= 4 && gx < render_width_ - 4) {
            Cell g_cell;
            g_cell.bg = {0, 0, 0};
            g_cell.fg = sg.color;
            g_cell.glyph = use_nerd_fonts_ ? "ᗣ" : "M";
            if (sg.frightened) {
                if ((gx % 8) < 2) {
                    g_cell.fg = {255, 255, 255};
                }
            }
            setCell(anim_row, gx, g_cell);
        }
    }
    
    // Draw bottom return message
    std::string prompt = "Press any key to return to the game";
    static int blink_counter = 0;
    blink_counter++;
    Color prompt_color = (blink_counter % 20 < 10) ? Color{150, 150, 150} : Color{80, 80, 80};
    drawString(center_row + 5, center_col - prompt.length() / 2, prompt, prompt_color);
}

void GameEngine::renderEffects() {
    Viewport vp = getViewport();
    
    // 1. Draw particles
    for (const auto& p : particles_) {
        int x = static_cast<int>(p.x);
        int y = static_cast<int>(p.y);
        
        if (x >= vp.start_x && x < vp.start_x + vp.visible_cols &&
            y >= vp.start_y && y < vp.start_y + vp.visible_rows) {
            
            int screen_row = vp.base_row + (y - vp.start_y);
            int screen_col = vp.base_col + (x - vp.start_x) * Config::TILE_RENDER_W;
            
            Cell cell;
            cell.fg = p.color;
            cell.bg = {0, 0, 0};
            if (p.lifetime_ms > 250) {
                cell.glyph = "*";
            } else if (p.lifetime_ms > 120) {
                cell.glyph = "+";
            } else {
                cell.glyph = "·";
            }
            setCell(screen_row, screen_col, cell);
        }
    }
    
    // 2. Draw floating score popups
    for (const auto& popup : popups_) {
        int x = popup.pos.x;
        int y = popup.pos.y;
        
        if (x >= vp.start_x && x < vp.start_x + vp.visible_cols &&
            y >= vp.start_y && y < vp.start_y + vp.visible_rows) {
            
            int screen_row = vp.base_row + (y - vp.start_y);
            int screen_col = vp.base_col + (x - vp.start_x) * Config::TILE_RENDER_W - (popup.text.length() / 2);
            
            drawString(screen_row, screen_col, popup.text, popup.fg);
        }
    }
}

void GameEngine::spawnScorePopup(Vec2 pos, int points, Color fg) {
    FloatingPopup popup;
    popup.pos = pos;
    popup.text = "+" + std::to_string(points);
    popup.fg = fg;
    popup.lifetime_ms = 600;
    popups_.push_back(popup);
}

void GameEngine::spawnParticleBurst(Vec2 pos, Color color) {
    std::uniform_real_distribution<double> dist_angle(0, 2 * 3.141592653589793);
    std::uniform_real_distribution<double> dist_speed(2.0, 6.0);
    
    int count = 8 + (static_cast<int>(rng_()) % 5);
    for (int i = 0; i < count; ++i) {
        double angle = dist_angle(rng_);
        double speed = dist_speed(rng_);
        
        Particle p;
        p.x = pos.x + 0.5;
        p.y = pos.y + 0.5;
        p.vx = std::cos(angle) * speed * 2.0;
        p.vy = std::sin(angle) * speed;
        p.color = color;
        p.lifetime_ms = 300 + (static_cast<int>(rng_()) % 150);
        particles_.push_back(p);
    }
}

void GameEngine::renderLevelSelector() {
    clearBuffer({0, 0, 0});
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    // Draw box outline
    drawDoubleBorderBox(center_row - 8, center_col - 30, 60, 13, {0, 255, 255}, {0, 0, 0});
    drawString(center_row - 8, center_col - 7, " SELECT LEVEL ", {255, 255, 0});
    
    // Render 4x5 grid for 20 levels
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 5; ++c) {
            int lvl = r * 5 + c + 1;
            int row = center_row - 6 + r * 2;
            int col = center_col - 22 + c * 11;
            
            bool locked = (lvl > max_unlocked_level_);
            bool selected = (level_select_cursor_ == lvl - 1);
            
            Color theme_color;
            if (lvl >= 1 && lvl <= 4) theme_color = {0, 255, 255};      // Cyan
            else if (lvl >= 5 && lvl <= 8) theme_color = {0, 255, 0};    // Green
            else if (lvl >= 9 && lvl <= 12) theme_color = {255, 100, 255}; // Pink
            else if (lvl >= 13 && lvl <= 16) theme_color = {255, 100, 0}; // Orange
            else if (lvl >= 17 && lvl <= 19) theme_color = {255, 255, 0}; // Gold
            else theme_color = {255, 50, 50}; // Glitch Red
            
            std::string label;
            if (locked) {
                theme_color = {60, 60, 60}; // Dark Grey
                if (selected) {
                    label = "> [XX] <";
                } else {
                    label = "  [XX]  ";
                }
            } else {
                std::string num = (lvl < 10) ? "0" + std::to_string(lvl) : std::to_string(lvl);
                if (selected) {
                    label = "> [" + num + "] <";
                } else {
                    label = "  [" + num + "]  ";
                }
            }
            
            bool hovered = isMouseHovering(row, col - 4, label);
            
            if (selected) {
                Color sel_color = locked ? Color{180, 180, 180} : Color{255, 255, 255};
                if (hovered) {
                    sel_color.r = static_cast<uint8_t>(std::min(255, static_cast<int>(sel_color.r) + 50));
                    sel_color.g = static_cast<uint8_t>(std::min(255, static_cast<int>(sel_color.g) + 50));
                    sel_color.b = static_cast<uint8_t>(std::min(255, static_cast<int>(sel_color.b) + 50));
                    drawString(row, col - 4, label, sel_color, {0, 0, 0}, true);
                } else {
                    drawString(row, col - 4, label, sel_color);
                }
            } else if (hovered) {
                theme_color.r = static_cast<uint8_t>(std::min(255, static_cast<int>(theme_color.r) + 50));
                theme_color.g = static_cast<uint8_t>(std::min(255, static_cast<int>(theme_color.g) + 50));
                theme_color.b = static_cast<uint8_t>(std::min(255, static_cast<int>(theme_color.b) + 50));
                drawString(row, col - 4, label, theme_color, {0, 0, 0}, true);
            } else {
                drawString(row, col - 4, label, theme_color);
            }
        }
    }
    
    // Render Back Button
    int row_back = center_row + 2;
    std::string back_text;
    Color back_color = {150, 150, 150};
    bool back_bold = false;
    if (level_select_cursor_ == 20) {
        back_text = "> [ BACK TO MENU ] <";
        if (click_feedback_timer_ > 0) {
            back_color = {255, 255, 255};
            back_bold = true;
        } else {
            back_color = {255, 255, 0}; // Yellow
        }
    } else {
        back_text = "  [ BACK TO MENU ]  ";
        back_color = {150, 150, 150}; // Grey
    }
    if (isMouseHovering(row_back, center_col - static_cast<int>(back_text.length()) / 2, back_text)) {
        back_color.r = static_cast<uint8_t>(std::min(255, static_cast<int>(back_color.r) + 50));
        back_color.g = static_cast<uint8_t>(std::min(255, static_cast<int>(back_color.g) + 50));
        back_color.b = static_cast<uint8_t>(std::min(255, static_cast<int>(back_color.b) + 50));
        back_bold = true;
    }
    drawString(row_back, center_col - back_text.length() / 2, back_text, back_color, {0, 0, 0}, back_bold);
}

void GameEngine::renderKeyConfig() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    // Clear whole screen using background color
    clearBuffer({0, 0, 0});
    
    // Box dimensions
    drawBox(center_row - 6, center_col - 20, 40, 12, {0, 0, 0}, {0, 0, 0});
    drawDoubleBorderBox(center_row - 6, center_col - 20, 40, 12, {0, 255, 255}, {0, 0, 0});
    
    drawString(center_row - 5, center_col - 9, "KEY CONFIGURATION", {255, 255, 0});
    
    std::string up_name = is_binding_ ? (binding_action_ == GameAction::Up ? "PRESS KEY..." : getKeyName(custom_key_up_)) : getKeyName(custom_key_up_);
    std::string down_name = is_binding_ ? (binding_action_ == GameAction::Down ? "PRESS KEY..." : getKeyName(custom_key_down_)) : getKeyName(custom_key_down_);
    std::string left_name = is_binding_ ? (binding_action_ == GameAction::Left ? "PRESS KEY..." : getKeyName(custom_key_left_)) : getKeyName(custom_key_left_);
    std::string right_name = is_binding_ ? (binding_action_ == GameAction::Right ? "PRESS KEY..." : getKeyName(custom_key_right_)) : getKeyName(custom_key_right_);
    std::string pause_name = is_binding_ ? (binding_action_ == GameAction::Pause ? "PRESS KEY..." : getKeyName(custom_key_pause_)) : getKeyName(custom_key_pause_);

    std::array<std::string, 6> options = {
        "UP:    [ " + up_name + " ]",
        "DOWN:  [ " + down_name + " ]",
        "LEFT:  [ " + left_name + " ]",
        "RIGHT: [ " + right_name + " ]",
        "PAUSE: [ " + pause_name + " ]",
        "Save & Back"
    };
    
    for (int i = 0; i < 6; ++i) {
        Color fg = {255, 255, 255};
        std::string prefix = "  ";
        if (i == key_config_selection_) {
            if (click_feedback_timer_ > 0) {
                fg = {255, 255, 255};
            } else {
                fg = {255, 200, 0};
            }
            prefix = "> ";
        }
        drawString(center_row - 3 + i, center_col - 15, prefix + options[i], fg);
    }
    
    if (is_binding_) {
        drawString(center_row + 4, center_col - 17, "PRESS ANY KEY NOW (ESC TO CANCEL)", {255, 100, 100});
    } else {
        drawString(center_row + 4, center_col - 13, "Press ENTER to edit", {180, 180, 180});
    }
}

void GameEngine::drawDoubleBorderBox(int row, int col, int w, int h, Color fg, Color bg) {
    // Fill background
    drawBox(row, col, w, h, fg, bg);
    
    std::string tl = use_nerd_fonts_ ? "╔" : "+";
    std::string hz = use_nerd_fonts_ ? "═" : "-";
    std::string tr = use_nerd_fonts_ ? "╗" : "+";
    std::string vt = use_nerd_fonts_ ? "║" : "|";
    std::string bl = use_nerd_fonts_ ? "╚" : "+";
    std::string br = use_nerd_fonts_ ? "╝" : "+";
    
    // Draw top border
    setCell(row, col, Cell{ .glyph = tl, .fg = fg, .bg = bg });
    for (int c = col + 1; c < col + w - 1; ++c) {
        setCell(row, c, Cell{ .glyph = hz, .fg = fg, .bg = bg });
    }
    setCell(row, col + w - 1, Cell{ .glyph = tr, .fg = fg, .bg = bg });
    
    // Draw sides
    for (int r = row + 1; r < row + h - 1; ++r) {
        setCell(r, col, Cell{ .glyph = vt, .fg = fg, .bg = bg });
        setCell(r, col + w - 1, Cell{ .glyph = vt, .fg = fg, .bg = bg });
    }
    
    // Draw bottom border
    setCell(row + h - 1, col, Cell{ .glyph = bl, .fg = fg, .bg = bg });
    for (int c = col + 1; c < col + w - 1; ++c) {
        setCell(row + h - 1, c, Cell{ .glyph = hz, .fg = fg, .bg = bg });
    }
    setCell(row + h - 1, col + w - 1, Cell{ .glyph = br, .fg = fg, .bg = bg });
}

void GameEngine::renderDevPasswordInput() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    drawDoubleBorderBox(center_row - 3, center_col - 18, 36, 7, {255, 50, 50}, {0, 0, 0});
    drawString(center_row - 3, center_col - 10, " DEVELOPER PASSWORD ", {255, 255, 0});
    
    drawString(center_row - 1, center_col - 14, "Enter Password:", {180, 180, 180});
    
    std::string masked(dev_password_buffer_.length(), '*');
    drawString(center_row, center_col - 14, "> " + masked + "_", {255, 255, 255});
    
    drawString(center_row + 2, center_col - 14, "ESC: Cancel   ENTER: Submit", {150, 150, 150});
}

void GameEngine::renderDevMenu() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    
    drawDoubleBorderBox(center_row - 7, center_col - 22, 44, 15, {255, 50, 50}, {0, 0, 0});
    drawString(center_row - 7, center_col - 8, " DEVELOPER MENU ", {255, 255, 0});
    
    std::vector<std::string> pm_colors = {"Classic Yellow", "Cyan", "Green", "Pink", "Red", "Rainbow"};
    std::string color_name = pm_colors[selected_pacman_color_];
    
    std::array<std::string, 10> options = {
        "Level ID:         < " + std::to_string(level_) + " >",
        "Pac-Man Color:    < " + color_name + " >",
        "Hearts (Max 66):  < " + std::to_string(lives_) + " >",
        "Score:            < " + std::to_string(score_) + " >",
        "Rainbow Unlocked: < " + std::string(unlocked_rainbow_ ? "YES" : "NO") + " >",
        "Immortal Cheat:   < " + std::string(immortal_ ? "YES" : "NO") + " >",
        "Freeze Ghosts:    < " + std::string(cheat_freeze_ghosts_ ? "YES" : "NO") + " >",
        "Super Speed:      < " + std::string(cheat_super_speed_ ? "YES" : "NO") + " >",
        "Skip Level:       [ Press ENTER ]",
        "[ EXIT DEVELOPER MENU ]"
    };
    
    for (int i = 0; i < 10; ++i) {
        Color fg = {255, 255, 255};
        std::string prefix = "  ";
        if (i == dev_menu_selection_) {
            fg = {255, 255, 0};
            prefix = "> ";
        }
        drawString(center_row - 5 + i, center_col - 19, prefix + options[i], fg);
    }
}

void GameEngine::loadKeybindings() {
    // Set defaults (supports QWERTY WASD, AZERTY ZQSD, Arrow keys, Vim keys, and Pause triggers)
    key_to_action_.clear();
    
    // UP
    for (int k : {(int)'w', (int)'W', (int)'z', (int)'Z', (int)'k', (int)'K', 1000}) key_to_action_[k] = GameAction::Up;
    // DOWN
    for (int k : {(int)'s', (int)'S', (int)'j', (int)'J', 1001}) key_to_action_[k] = GameAction::Down;
    // LEFT
    for (int k : {(int)'a', (int)'A', (int)'q', (int)'Q', (int)'h', (int)'H', 1003}) key_to_action_[k] = GameAction::Left;
    // RIGHT
    for (int k : {(int)'d', (int)'D', (int)'l', (int)'L', 1002}) key_to_action_[k] = GameAction::Right;
    // PAUSE
    for (int k : {(int)'p', (int)'P'}) key_to_action_[k] = GameAction::Pause;
    
    std::ifstream f("pacterm.keys");
    if (!f.is_open()) {
        // Create the file with default settings to let the user customize it
        std::ofstream out("pacterm.keys");
        if (out.is_open()) {
            out << "# pacterm keybindings configuration\n"
                << "# Format: ACTION=key1,key2,...\n"
                << "# Supported actions: UP, DOWN, LEFT, RIGHT, PAUSE\n"
                << "# Special keys: ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT, SPACE, ESC\n\n"
                << "UP=w,z,k,ARROW_UP\n"
                << "DOWN=s,j,ARROW_DOWN\n"
                << "LEFT=a,q,h,ARROW_LEFT\n"
                << "RIGHT=d,l,ARROW_RIGHT\n"
                << "PAUSE=p\n";
        }
        return;
    }
    
    std::string line;
    std::unordered_map<GameAction, std::vector<int>> new_bindings;
    constexpr size_t kMaxKeyLine = 512; // guard against a corrupt config file

    auto parse_key = [](const std::string& s) -> std::vector<int> {
        if (s.empty()) return {};
        if (s == "ARROW_UP") return {1000};
        if (s == "ARROW_DOWN") return {1001};
        if (s == "ARROW_RIGHT") return {1002};
        if (s == "ARROW_LEFT") return {1003};
        if (s == "SPACE") return {' '};
        if (s == "ESC") return {27};
        if (s == "ENTER") return {'\n', '\r'};
        if (s.length() == 1) {
            char c = s[0];
            if (c >= 'a' && c <= 'z') {
                return {c, static_cast<int>(std::toupper(static_cast<unsigned char>(c)))};
            } else if (c >= 'A' && c <= 'Z') {
                return {c, static_cast<int>(std::tolower(static_cast<unsigned char>(c)))};
            }
            return {c};
        }
        return {};
    };
    
    while (std::getline(f, line)) {
        if (line.size() > kMaxKeyLine) line.resize(kMaxKeyLine); // truncate runaway lines
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim line
        if (!line.empty()) {
            size_t last = line.find_last_not_of(" \t\r\n");
            if (last != std::string::npos) {
                line = line.substr(0, last + 1);
            }
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first != std::string::npos) {
                line = line.substr(first);
            } else {
                line.clear();
            }
        }
        if (line.empty()) continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string action_str = line.substr(0, eq_pos);
        std::string keys_str = line.substr(eq_pos + 1);
        
        // Trim action
        if (!action_str.empty()) {
            size_t last = action_str.find_last_not_of(" \t");
            if (last != std::string::npos) {
                action_str = action_str.substr(0, last + 1);
            }
            size_t first = action_str.find_first_not_of(" \t");
            if (first != std::string::npos) {
                action_str = action_str.substr(first);
            }
        }
        
        GameAction action = GameAction::None;
        if (action_str == "UP") action = GameAction::Up;
        else if (action_str == "DOWN") action = GameAction::Down;
        else if (action_str == "LEFT") action = GameAction::Left;
        else if (action_str == "RIGHT") action = GameAction::Right;
        else if (action_str == "PAUSE") action = GameAction::Pause;
        
        if (action == GameAction::None) continue;
        
        if (new_bindings.find(action) == new_bindings.end()) {
            new_bindings[action] = {};
        }
        
        std::stringstream ss(keys_str);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                size_t last = item.find_last_not_of(" \t");
                if (last != std::string::npos) {
                    item = item.substr(0, last + 1);
                }
                size_t first = item.find_first_not_of(" \t");
                if (first != std::string::npos) {
                    item = item.substr(first);
                }
            }
            auto codes = parse_key(item);
            for (int code : codes) {
                new_bindings[action].push_back(code);
            }
        }
    }
    
    for (const auto& [action, codes] : new_bindings) {
        for (auto it = key_to_action_.begin(); it != key_to_action_.end(); ) {
            if (it->second == action) {
                it = key_to_action_.erase(it);
            } else {
                ++it;
            }
        }
        for (int code : codes) {
            key_to_action_[code] = action;
        }
    }
}

bool GameEngine::install_bin(bool cli_mode) {
    try {
        std::filesystem::path self_path = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path dest_path = "/usr/bin/pacterm";
        
        if (std::filesystem::exists(dest_path)) {
            std::filesystem::remove(dest_path);
        }
        
        std::filesystem::copy_file(self_path, dest_path, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(dest_path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec, std::filesystem::perm_options::add);
        
        if (cli_mode) {
            std::cout << "Successfully copied pacterm binary to " << dest_path << std::endl;
        }
        return true;
    } catch (const std::exception& e) {
        if (cli_mode) {
            std::cerr << "Installation failed: " << e.what() << std::endl;
        }
        return false;
    }
}

bool GameEngine::delete_bin(bool cli_mode) {
    try {
        std::filesystem::path installed_path = "/usr/bin/pacterm";
        
        if (std::filesystem::exists(installed_path)) {
            std::filesystem::remove(installed_path);
            if (cli_mode) {
                std::cout << "Removed pacterm from " << installed_path << std::endl;
            }
        } else {
            if (cli_mode) {
                std::cout << "No installed binary found at " << installed_path << "." << std::endl;
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (cli_mode) {
            std::cerr << "Deletion failed: " << e.what() << std::endl;
        }
        return false;
    }
}

bool GameEngine::isInstalledLocally() const {
    return std::filesystem::exists("/usr/bin/pacterm");
}

namespace {
    enum class WaveType { Sine, Square, Triangle };

    struct WavHeader {
        char chunkId[4] = {'R', 'I', 'F', 'F'};
        uint32_t chunkSize = 0;
        char format[4] = {'W', 'A', 'V', 'E'};
        char subchunk1Id[4] = {'f', 'm', 't', ' '};
        uint32_t subchunk1Size = 16;
        uint16_t audioFormat = 1; // PCM
        uint16_t numChannels = 1; // Mono
        uint32_t sampleRate = 44100;
        uint32_t byteRate = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 16;
        char subchunk2Id[4] = {'d', 'a', 't', 'a'};
        uint32_t subchunk2Size = 0;
    };

    const double PI = 3.14159265358979323846;

    std::vector<int16_t> generateRetroSound(double start_freq, double end_freq, double duration, WaveType type, double volume = 0.2) {
        int sample_rate = 44100;
        int num_samples = static_cast<int>(duration * sample_rate);
        std::vector<int16_t> samples(num_samples);
        double max_amp = 32767 * volume;
        double phase = 0.0;

        for (int i = 0; i < num_samples; ++i) {
            double t = static_cast<double>(i) / sample_rate;
            double progress = t / duration;
            double freq = start_freq + (end_freq - start_freq) * progress;
            
            phase += 2.0 * PI * freq / sample_rate;
            if (phase > 2.0 * PI) {
                phase = std::fmod(phase, 2.0 * PI);
            }

            double val = 0.0;
            if (type == WaveType::Sine) {
                val = std::sin(phase);
            } else if (type == WaveType::Square) {
                val = (std::sin(phase) >= 0.0) ? 0.35 : -0.35;
            } else if (type == WaveType::Triangle) {
                double normalized = phase / (2.0 * PI);
                if (normalized < 0.25) {
                    val = normalized * 4.0;
                } else if (normalized < 0.75) {
                    val = 2.0 - normalized * 4.0;
                } else {
                    val = normalized * 4.0 - 4.0;
                }
            }

            double env = 1.0;
            double fade_in_len = 0.01 * duration;
            double fade_out_len = 0.15 * duration;
            if (t < fade_in_len) {
                env = t / fade_in_len;
            } else if (t > duration - fade_out_len) {
                env = (duration - t) / fade_out_len;
            }

            samples[i] = static_cast<int16_t>(val * max_amp * env);
        }
        return samples;
    }

    std::vector<int16_t> generateNote(double freq, double duration, WaveType type, double volume = 0.2) {
        if (freq <= 0.0) {
            int sample_rate = 44100;
            return std::vector<int16_t>(static_cast<int>(duration * sample_rate), 0);
        }
        return generateRetroSound(freq, freq, duration, type, volume);
    }

    std::vector<int16_t> generateMelody(const std::vector<std::pair<double, double>>& notes, WaveType type, double volume = 0.2) {
        std::vector<int16_t> result;
        for (const auto& note : notes) {
            auto samples = generateNote(note.first, note.second, type, volume);
            result.insert(result.end(), samples.begin(), samples.end());
        }
        return result;
    }

    void writeWavFile(const std::string& filename, const std::vector<int16_t>& samples) {
        WavHeader header;
        header.subchunk2Size = samples.size() * sizeof(int16_t);
        header.chunkSize = 36 + header.subchunk2Size;
        header.byteRate = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
        header.blockAlign = header.numChannels * (header.bitsPerSample / 8);

        std::ofstream out(filename, std::ios::binary);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(samples.data()), header.subchunk2Size);
    }
}

void GameEngine::generateSounds() {
    std::filesystem::path sound_dir = getSoundDirectory();
    std::filesystem::create_directories(sound_dir);
    
    // 1. eat_dot.wav
    std::filesystem::path p_eat_dot = sound_dir / "eat_dot.wav";
    if (!std::filesystem::exists(p_eat_dot)) {
        auto samples = generateRetroSound(550.0, 850.0, 0.05, WaveType::Triangle, 0.15);
        writeWavFile(p_eat_dot.string(), samples);
    }
    
    // 2. eat_pellet.wav
    std::filesystem::path p_eat_pellet = sound_dir / "eat_pellet.wav";
    if (!std::filesystem::exists(p_eat_pellet)) {
        std::vector<int16_t> samples;
        auto sweep1 = generateRetroSound(500.0, 800.0, 0.06, WaveType::Triangle, 0.2);
        auto sweep2 = generateRetroSound(700.0, 1000.0, 0.06, WaveType::Triangle, 0.2);
        samples.insert(samples.end(), sweep1.begin(), sweep1.end());
        samples.insert(samples.end(), sweep2.begin(), sweep2.end());
        writeWavFile(p_eat_pellet.string(), samples);
    }
    
    // 3. eat_ghost.wav
    std::filesystem::path p_eat_ghost = sound_dir / "eat_ghost.wav";
    if (!std::filesystem::exists(p_eat_ghost)) {
        auto samples = generateRetroSound(200.0, 1200.0, 0.35, WaveType::Square, 0.25);
        writeWavFile(p_eat_ghost.string(), samples);
    }
    
    // 4. death.wav
    std::filesystem::path p_death = sound_dir / "death.wav";
    if (!std::filesystem::exists(p_death)) {
        std::vector<int16_t> death_samples;
        for (int step = 0; step < 8; ++step) {
            double start_f = 850.0 - step * 95.0;
            double end_f = 750.0 - step * 95.0;
            auto chirp = generateRetroSound(start_f, end_f, 0.09, WaveType::Square, 0.2);
            death_samples.insert(death_samples.end(), chirp.begin(), chirp.end());
            auto silence = generateNote(0.0, 0.02, WaveType::Sine, 0.0);
            death_samples.insert(death_samples.end(), silence.begin(), silence.end());
        }
        writeWavFile(p_death.string(), death_samples);
    }
    
    // 5. ready.wav
    std::filesystem::path p_ready = sound_dir / "ready.wav";
    if (!std::filesystem::exists(p_ready)) {
        std::vector<std::pair<double, double>> ready_notes = {
            {523.25, 0.08}, // C5
            {0.0, 0.02},
            {659.25, 0.08}, // E5
            {0.0, 0.02},
            {783.99, 0.08}, // G5
            {0.0, 0.02},
            {659.25, 0.08}, // E5
            {0.0, 0.02},
            {783.99, 0.08}, // G5
            {0.0, 0.02},
            {1046.50, 0.25} // C6
        };
        auto samples = generateMelody(ready_notes, WaveType::Triangle, 0.2);
        writeWavFile(p_ready.string(), samples);
    }
    
    // 6. clear.wav
    std::filesystem::path p_clear = sound_dir / "clear.wav";
    if (!std::filesystem::exists(p_clear)) {
        std::vector<std::pair<double, double>> clear_notes = {
            {523.25, 0.08}, // C5
            {659.25, 0.08}, // E5
            {783.99, 0.08}, // G5
            {1046.50, 0.08}, // C6
            {783.99, 0.08}, // G5
            {1046.50, 0.08}, // C6
            {1318.51, 0.35}  // E6
        };
        auto samples = generateMelody(clear_notes, WaveType::Triangle, 0.2);
        writeWavFile(p_clear.string(), samples);
    }
}

void GameEngine::playSound(const std::string& name) {
    if (muted_) return;
    
    std::filesystem::path p(name);
    std::string filename = p.filename().string();
    std::filesystem::path full_path = getSoundDirectory() / filename;
    std::string path_str = full_path.string();
    
    std::string cmd = "(paplay " + path_str + " || pw-play " + path_str + " || mpg123 " + path_str + " || mpv --no-video " + path_str + ") >/dev/null 2>&1 &";
    std::system(cmd.c_str());
}

std::filesystem::path GameEngine::getSoundDirectory() {
    try {
        std::filesystem::path home_path;
        const char* home_env = std::getenv("HOME");
        if (home_env) {
            home_path = home_env;
        } else {
            home_path = std::filesystem::current_path();
        }
        std::filesystem::path pacterm_dir = home_path / ".pacterm";
        if (!std::filesystem::exists(pacterm_dir)) {
            std::filesystem::create_directories(pacterm_dir);
        }
        return pacterm_dir / "sounds";
    } catch (...) {
        const char* home_env = std::getenv("HOME");
        if (home_env) {
            return std::filesystem::path(home_env) / ".pacterm" / "sounds";
        }
        return std::filesystem::current_path() / "sounds";
    }
}

// AnimationController implementations
void AnimationController::fadeIn(const Color& color, int duration_ms) {
    state = State::FadingIn;
    elapsed_ms_ = 0;
    this->duration_ms = duration_ms;
    start_color = {0, 0, 0};
    end_color = color;
    progress = 0.0f;
}

void AnimationController::fadeOut(const Color& color, int duration_ms) {
    state = State::FadingOut;
    elapsed_ms_ = 0;
    this->duration_ms = duration_ms;
    start_color = color;
    end_color = {0, 0, 0};
    progress = 0.0f;
}

void AnimationController::update(int delta_ms) {
    if (state == State::Idle) return;
    elapsed_ms_ += delta_ms;
    if (elapsed_ms_ >= duration_ms) {
        elapsed_ms_ = duration_ms;
        progress = 1.0f;
        state = State::Active;
    } else {
        progress = static_cast<float>(elapsed_ms_) / duration_ms;
    }
}

Color AnimationController::getCurrentColor() const {
    if (state == State::Idle) return start_color;
    float t = progress;
    uint8_t r = static_cast<uint8_t>(start_color.r + t * (end_color.r - start_color.r));
    uint8_t g = static_cast<uint8_t>(start_color.g + t * (end_color.g - start_color.g));
    uint8_t b = static_cast<uint8_t>(start_color.b + t * (end_color.b - start_color.b));
    return {r, g, b};
}

bool AnimationController::isComplete() const {
    return state == State::Active && progress >= 1.0f;
}

void AnimationController::reset() {
    state = State::Idle;
    progress = 0.0f;
    elapsed_ms_ = 0;
}

