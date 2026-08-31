// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.4.0
#include "GameEngine.hpp"
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <algorithm>
#include <thread>
#include <queue>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <csignal>
#include <poll.h>
#include <utility>
#include <charconv>

namespace {
    constexpr std::string_view kEnterAltScreen = "\033[?1049h\033[2J\033[H\033[?25l";
    constexpr std::string_view kEnterMouse     = "\033[?1000h\033[?1002h\033[?1003h\033[?1006h";
    constexpr std::string_view kLeaveAltScreen = "\033[0m\033[?25h\033[?1000l\033[?1002l\033[?1003l\033[?1006l\033[?1049l";

    inline void safeWrite(int fd, const void* buf, size_t count) noexcept {
        auto res = ::write(fd, buf, count);
        (void)res;
    }

    constexpr std::array<const char*, 6> kGlitchWallGlyphs  = {"┼", "╣", "┬", "╪", "╬", "┚"};
    constexpr std::array<const char*, 4> kGlitchBlockGlyphs = {"█", "▓", "▞", "▦"};
    constexpr std::array<const char*, 4> kGlitchAsciiGlyphs = {"§", "¶", "Ø", "‡"};
    constexpr std::array<std::string_view, 11> kThemeNames  = {"Classic", "Cyan",  "Green",   "Pink",   "Red",     "Violet",
                                                               "Ice",     "Amber", "Rainbow", "Glitch", "PacTerm+"};

    Color glitchRGB(uint64_t now) {
        const uint8_t r = (now / 100 % 2 == 0) ? 255 : 50;
        const uint8_t g = (now / 150 % 2 == 0) ? 50 : 0;
        const uint8_t b = (now / 200 % 2 == 0) ? 255 : 0;
        return {r, g, b};
    }

    struct ThemePowerups {
        LevelTheme theme;
        std::array<Powerup, 2> powerups;
    };

    constexpr std::array<ThemePowerups, 8> THEME_POWERUPS = {{
        {LevelTheme::Cyan,
         {{
             {
                 "Speed",
                 PowerupKind::PacSpeed,
             },
             {
                 "Corrosion",
                 PowerupKind::PelletBonus,
             },
         }}},
        {LevelTheme::Green,
         {{
             {
                 "Warp Stun",
                 PowerupKind::WarpStun,
             },
             {
                 "Harvest",
                 PowerupKind::DotBonus,
             },
         }}},
        {LevelTheme::Pink,
         {{
             {
                 "Ember Shield",
                 PowerupKind::LavaResist,
             },
             {
                 "Magma Points",
                 PowerupKind::PelletBonus,
             },
         }}},
        {LevelTheme::Red,
         {{
             {
                 "Dash Mastery",
                 PowerupKind::DashRapid,
             },
             {
                 "Fury Points",
                 PowerupKind::GhostBonus,
             },
         }}},
        {LevelTheme::Glitch,
         {{
             {
                 "Chaos Luck",
                 PowerupKind::GlitchLuck,
             },
             {
                 "Corruption",
                 PowerupKind::GlitchWarp,
             },
         }}},
        {LevelTheme::Violet,
         {{
             {
                 "Blitz Bounty",
                 PowerupKind::BlitzBounty,
             },
             {
                 "Arcane Harvest",
                 PowerupKind::DotBonus,
             },
         }}},
        {LevelTheme::Ice,
         {{
             {
                 "Permafrost",
                 PowerupKind::FreezeLinger,
             },
             {
                 "Frostbite",
                 PowerupKind::GhostSlow,
             },
         }}},
        {LevelTheme::Amber,
         {{
             {
                 "Molten Shield",
                 PowerupKind::LavaResist,
             },
             {
                 "Golden Bounty",
                 PowerupKind::GhostBonus,
             },
         }}},
    }};
} // namespace

static int clampKeyCode(int k) noexcept {
    if (k >= ' ' && k <= '~')
        return k;
    if (k == '\n' || k == '\r')
        return k;
    if (k == 27 || k == 127)
        return k;
    if (k >= 1000 && k <= 1005)
        return k;
    return 0;
}

static size_t utf8SeqLength(unsigned char c) noexcept {
    if (c >= 0xf0)
        return 4;
    if (c >= 0xe0)
        return 3;
    if (c >= 0xc0)
        return 2;
    return 1;
}

// Decode the UTF-8 codepoint starting at text[i] (len bytes).
static uint32_t decodeUTF8(std::string_view text, size_t i, size_t len) noexcept {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data()) + i;
    switch (len) {
    case 1: return p[0];
    case 2: return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    case 3: return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    case 4: return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    }
    return p[0];
}

// Terminal column width of a single decoded codepoint (0 == zero-width).
static size_t codepointWidth(uint32_t cp) noexcept {
    if (cp == 0x0483 || (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE00 && cp <= 0xFE0F))
        return 0;

    // Classic East Asian Wide/Fullwidth ranges.
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2329 && cp <= 0x232A) || (cp >= 0x2E80 && cp <= 0x303E) || (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) || (cp >= 0xA960 && cp <= 0xA97F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;

    // Supplementary-plane emoji blocks render two columns wide.
    if (cp >= 0x1F000 && cp <= 0x1FAFF)
        return 2;

    // East Asian "Wide" symbols inside the else single-column Misc-Symbols and
    // Dingbats blocks (matches glibc wcwidth on a UTF-8 locale, e.g. U+26A1).
    if ((cp >= 0x231A && cp <= 0x231B) || (cp >= 0x23E9 && cp <= 0x23EC) || cp == 0x23F0 || cp == 0x23F3 || (cp >= 0x25FD && cp <= 0x25FE) ||
        (cp >= 0x2614 && cp <= 0x2615) || (cp >= 0x2630 && cp <= 0x2637) || (cp >= 0x2648 && cp <= 0x2653) || cp == 0x267F || (cp >= 0x268A && cp <= 0x268F) ||
        cp == 0x2693 || cp == 0x26A1 || (cp >= 0x26AA && cp <= 0x26AB) || (cp >= 0x26BD && cp <= 0x26BE) || (cp >= 0x26C4 && cp <= 0x26C5) || cp == 0x26CE ||
        cp == 0x26D4 || cp == 0x26EA || (cp >= 0x26F2 && cp <= 0x26F3) || cp == 0x26F5 || cp == 0x26FA || cp == 0x26FD || cp == 0x2705 ||
        (cp >= 0x270A && cp <= 0x270B) || cp == 0x2728 || cp == 0x274C || cp == 0x274E || (cp >= 0x2753 && cp <= 0x2755) || cp == 0x2757 ||
        (cp >= 0x2795 && cp <= 0x2797) || cp == 0x27B0 || cp == 0x27BF || (cp >= 0x2B1B && cp <= 0x2B1C) || cp == 0x2B50 || cp == 0x2B55)
        return 2;

    return 1;
}

// Display width of the UTF-8 sequence beginning at text[i]. A trailing
// variation selector (U+FE0F) is consumed with its base so sequences like U+2744+VS16
// are treated as one two-column glyph instead of "1 + zero-width". Sets *consumed to
// the number of bytes the glyph unit occupies.
static size_t seqWidth(std::string_view text, size_t i, size_t* consumed) noexcept {
    size_t len = utf8SeqLength(static_cast<unsigned char>(text[i]));
    if (i + len > text.size())
        len = text.size() - i;
    *consumed = len;

    const bool vs16 = i + len + 3 <= text.size() && static_cast<unsigned char>(text[i + len]) == 0xEF &&
                      static_cast<unsigned char>(text[i + len + 1]) == 0xB8 && static_cast<unsigned char>(text[i + len + 2]) == 0x8F;

    if (vs16) {
        *consumed = len + 3;
        return 2;
    }
    return codepointWidth(decodeUTF8(text, i, len));
}

namespace {
    class SignalManager {
    public:
        static inline std::atomic<bool> shutdown_requested{false};
        static inline std::atomic<bool> window_resized{false};

        static void install() noexcept {
            struct sigaction sa = {};
            ::sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;

            sa.sa_handler = &SignalManager::handleTermination;
            ::sigaction(SIGINT, &sa, nullptr);
            ::sigaction(SIGTERM, &sa, nullptr);
            ::sigaction(SIGQUIT, &sa, nullptr);

            sa.sa_handler = &SignalManager::handleResize;
            ::sigaction(SIGWINCH, &sa, nullptr);

            sa.sa_handler = &SignalManager::handleCrash;
            ::sigaction(SIGSEGV, &sa, nullptr);
            ::sigaction(SIGABRT, &sa, nullptr);
            ::sigaction(SIGBUS, &sa, nullptr);
            ::sigaction(SIGFPE, &sa, nullptr);
        }

    private:
        static void handleTermination(int) noexcept { shutdown_requested.store(true, std::memory_order_relaxed); }

        static void handleResize(int) noexcept { window_resized.store(true, std::memory_order_relaxed); }

        static void handleCrash(int sig) noexcept {
            safeWrite(STDOUT_FILENO, kLeaveAltScreen.data(), kLeaveAltScreen.size());
            struct sigaction sa = {};
            ::sigemptyset(&sa.sa_mask);
            sa.sa_handler = SIG_DFL;
            ::sigaction(sig, &sa, nullptr);
            ::raise(sig);
        }
    };
} // namespace

GameEngine::TerminalSession*& GameEngine::TerminalSession::instance() noexcept {
    static TerminalSession* s_inst = nullptr;
    return s_inst;
}

GameEngine::TerminalSession::TerminalSession() {
    instance() = this;
    if (::tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
        struct termios raw = orig_termios;
        raw.c_lflag &= static_cast<unsigned int>(~(ECHO | ICANON | ISIG | IEXTEN));
        raw.c_iflag &= static_cast<unsigned int>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
        raw.c_oflag &= static_cast<unsigned int>(~(OPOST));
        raw.c_cflag |= static_cast<unsigned int>(CS8);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
            raw_active = true;
        }
    }
    safeWrite(STDOUT_FILENO, kEnterAltScreen.data(), kEnterAltScreen.size());
    safeWrite(STDOUT_FILENO, kEnterMouse.data(), kEnterMouse.size());
    SignalManager::install();
}

GameEngine::TerminalSession::~TerminalSession() noexcept {
    restore();
    if (instance() == this) {
        instance() = nullptr;
    }
}

void GameEngine::TerminalSession::restore() noexcept {
    if (raw_active) {
        safeWrite(STDOUT_FILENO, kLeaveAltScreen.data(), kLeaveAltScreen.size());
        (void)::tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        raw_active = false;
    }
}

static Direction bfsNextDirection(const Map& map, Vec2 start, Vec2 target, Direction forbidden_direction) noexcept {
    if (start == target)
        return Direction::None;

    struct QueueNode {
        Vec2 pos;
        Direction firstStep;
    };
    std::array<QueueNode, Config::MAP_WIDTH * Config::MAP_HEIGHT> queue{};
    std::array<bool, Config::MAP_WIDTH * Config::MAP_HEIGHT> visited{};

    auto inBounds   = [](Vec2 pos) noexcept { return pos.x >= 0 && pos.x < Config::MAP_WIDTH && pos.y >= 0 && pos.y < Config::MAP_HEIGHT; };
    auto getVisited = [&](Vec2 pos) noexcept -> bool { return !inBounds(pos) || visited[pos.y * Config::MAP_WIDTH + pos.x]; };
    auto setVisited = [&](Vec2 pos) noexcept {
        if (inBounds(pos)) {
            visited[pos.y * Config::MAP_WIDTH + pos.x] = true;
        }
    };

    auto distSq = [](Vec2 a, Vec2 b) noexcept -> int {
        int dx = a.x - b.x;
        int dy = a.y - b.y;
        return dx * dx + dy * dy;
    };

    int best_dist_sq    = std::numeric_limits<int>::max();
    Direction best_step = Direction::None;
    Direction fallback  = Direction::None;

    size_t head = 0;
    size_t tail = 0;

    auto push = [&](Vec2 pos, Direction first) noexcept {
        if (tail < queue.size()) {
            queue[tail++] = QueueNode{pos, first};
        }
    };

    setVisited(start);

    for (Direction d : {Direction::Up, Direction::Left, Direction::Down, Direction::Right}) {
        if (d == forbidden_direction)
            continue;
        Vec2 next = map.wrapTunnel(start + directionToVec2(d));
        if (getVisited(next) || !map.isWalkableByGhost(next))
            continue;

        if (fallback == Direction::None)
            fallback = d;

        int d_sq = distSq(next, target);
        if (d_sq < best_dist_sq) {
            best_dist_sq = d_sq;
            best_step    = d;
        }

        if (next == target)
            return d;

        setVisited(next);
        push(next, d);
    }

    while (head < tail) {
        QueueNode current = queue[head++];

        for (Direction d : {Direction::Up, Direction::Left, Direction::Down, Direction::Right}) {
            Vec2 next = map.wrapTunnel(current.pos + directionToVec2(d));
            if (getVisited(next) || !map.isWalkableByGhost(next))
                continue;

            int d_sq = distSq(next, target);
            if (d_sq < best_dist_sq) {
                best_dist_sq = d_sq;
                best_step    = current.firstStep;
            }

            if (next == target)
                return current.firstStep;

            setVisited(next);
            push(next, current.firstStep);
        }
    }

    if (best_step != Direction::None)
        return best_step;
    return fallback;
}

struct ModeWave {
    GhostMode mode;
    int duration_ms;
};

static constexpr std::array<ModeWave, 8> MODE_WAVES = {{{GhostMode::Scatter, 7000},
                                                        {GhostMode::Chase, 20000},
                                                        {GhostMode::Scatter, 7000},
                                                        {GhostMode::Chase, 20000},
                                                        {GhostMode::Scatter, 5000},
                                                        {GhostMode::Chase, 20000},
                                                        {GhostMode::Scatter, 5000},
                                                        {GhostMode::Chase, -1}}};

GameEngine::GameEngine()
    : ghosts_{Ghost{GhostPersonality::Blinky}, Ghost{GhostPersonality::Pinky}, Ghost{GhostPersonality::Inky}, Ghost{GhostPersonality::Clyde}} {
    const char* env_nf = std::getenv("PACMAN_NERD_FONTS");
    if (env_nf && std::string(env_nf) == "0") {
        use_nerd_fonts_ = false;
    }

    unsigned seed = static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count());
    rng_.seed(seed);

    SignalManager::install();
    I18n::initFromLocale();
    queryTerminalSize();
    initRenderer();
    try {
        loadHighScore();
    } catch (...) {}
    rebuildKeybindings();
    preloadAssets();
    startMainMenu();
    fade_animation_.fadeIn({255, 255, 255}, 400);
    audio_thread_ = std::thread(&GameEngine::audioWorkerLoop, this);
}

GameEngine::~GameEngine() {
    audio_running_.store(false, std::memory_order_release);
    audio_cv_.notify_all();
    if (audio_thread_.joinable()) {
        audio_thread_.join();
    }

    terminal_session_.restore();

    std::string_view message = "Thanks for playing pacterm by Wael!";
    for (size_t i = 0; i < message.length(); ++i) {
        Color c;
        if (selected_general_theme_ == 0) {
            double ratio = static_cast<double>(i) / (message.length() - 1);
            c.r          = static_cast<uint8_t>(255 - ratio * 255);
            c.g          = static_cast<uint8_t>(100 + ratio * 155);
            c.b          = 255;
        } else if (selected_general_theme_ == 8) {
            c = getRainbowColor(static_cast<double>(i) * 0.15);
        } else {
            c = themePrimary(selected_general_theme_, static_cast<double>(i) * 0.15);
        }
        std::printf("\033[38;2;%u;%u;%um%c", c.r, c.g, c.b, message[i]);
    }
    std::fputs("\033[0m\n", stdout);
    std::fflush(stdout);
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
    static char s_buf[256];
    static size_t s_len = 0;
    static size_t s_pos = 0;

    if (s_pos >= s_len) {
        s_pos     = 0;
        s_len     = 0;
        ssize_t n = ::read(STDIN_FILENO, s_buf, sizeof(s_buf));
        if (n <= 0) {
            return -1;
        }
        s_len = static_cast<size_t>(n);
    }

    char c = s_buf[s_pos++];
    if (c == '\033') {
        size_t rem = s_len - (s_pos - 1);
        if (rem == 1) {
            struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
            if (::poll(&pfd, 1, 15) > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = ::read(STDIN_FILENO, s_buf, sizeof(s_buf));
                if (n > 0) {
                    s_len = static_cast<size_t>(n);
                    s_pos = 0;
                    return readKey();
                }
            }
            return 27;
        }
        if (s_pos < s_len && s_buf[s_pos] == '[') {
            if (s_pos + 1 < s_len && s_buf[s_pos + 1] == '<') {
                size_t p     = s_pos + 2;
                bool found   = false;
                char term_ch = 0;
                while (p < s_len && p - s_pos < 32) {
                    if (s_buf[p] == 'M' || s_buf[p] == 'm') {
                        term_ch = s_buf[p];
                        found   = true;
                        break;
                    }
                    ++p;
                }
                if (found) {
                    int btn = 0, x = 0, y = 0;
                    std::string params(&s_buf[s_pos + 2], p - (s_pos + 2));
                    s_pos = p + 1;
                    if (std::sscanf(params.c_str(), "%d;%d;%d", &btn, &x, &y) >= 3) {
                        mouse_button_       = btn;
                        mouse_x_            = x - 1;
                        mouse_y_            = y - 1;
                        mouse_press_        = (term_ch == 'M');
                        mouse_hover_active_ = true;
                        if (btn & 32) {
                            return 1005;
                        }
                        if (mouse_press_)
                            return 1004;
                        return -1;
                    }
                    return -1;
                }
            } else if (s_pos + 1 < s_len) {
                char code = s_buf[s_pos + 1];
                s_pos += 2;
                switch (code) {
                case 'A': return 1000;
                case 'B': return 1001;
                case 'C': return 1002;
                case 'D': return 1003;
                }
                return 27;
            }
        }
        return 27;
    }
    return static_cast<unsigned char>(c);
}

void GameEngine::handleInput(int key) {
    afk_timer_ms_ = 0;
    if (phase_ == GamePhase::Screensaver) {
        phase_                = GamePhase::Paused;
        pause_menu_selection_ = 0;
        return;
    }

    if (key >= 32 && key <= 126 && phase_ != GamePhase::DevPasswordInput && phase_ != GamePhase::DevMenu && phase_ != GamePhase::RedeemInput &&
        (phase_ == GamePhase::Playing || phase_ == GamePhase::MainMenu || phase_ == GamePhase::LevelSelector)) {
        char c = std::tolower(static_cast<char>(key));
        dev_input_sequence_ += c;
        if (dev_input_sequence_.length() > 3) {
            dev_input_sequence_ = dev_input_sequence_.substr(dev_input_sequence_.length() - 3);
        }
        if (dev_input_sequence_ == "dev") {
            pre_dev_phase_       = phase_;
            phase_               = GamePhase::DevPasswordInput;
            dev_password_buffer_ = "";
            dev_input_sequence_  = "";
            return;
        }
    }

    if (key == 'm' || key == 'M') {
        muted_ = !muted_;
        saveHighScore();
        return;
    }

    if (key == 1005) {
        return;
    }

    if (key == 1004 && mouse_press_) {
        handleMouseClick();
        return;
    }

    GameAction action = GameAction::None;
    auto it           = key_to_action_.find(key);
    if (it != key_to_action_.end()) {
        action = it->second;
    }

    Direction dir = Direction::None;
    switch (action) {
    case GameAction::Up: dir = Direction::Up; break;
    case GameAction::Down: dir = Direction::Down; break;
    case GameAction::Left: dir = Direction::Left; break;
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
                phase_               = GamePhase::LevelSelector;
                level_select_cursor_ = 0;
                fade_animation_.fadeIn({255, 255, 255}, 300);
            } else if (main_menu_selection_ == 1) {
                phase_          = GamePhase::UsernameInput;
                input_username_ = username_;
            } else if (main_menu_selection_ == 2) {
                phase_ = GamePhase::Stats;
                fade_animation_.fadeIn({255, 255, 255}, 300);
            } else if (main_menu_selection_ == 3) {
                phase_         = GamePhase::RedeemInput;
                redeem_input_  = "";
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
                main_menu_msg_timer_ms_ = 3000;
            } else if (main_menu_selection_ == 5) {
                phase_              = GamePhase::Settings;
                settings_selection_ = 0;
                main_menu_message_  = "";
                fade_animation_.fadeIn({255, 255, 255}, 300);
            } else if (main_menu_selection_ == 6) {
                running_ = false;
            }
        }
        break;

    case GamePhase::Settings:
        if (key == 27) {
            phase_             = GamePhase::MainMenu;
            main_menu_message_ = "";
            fade_animation_.fadeIn({255, 255, 255}, 300);
        } else if (action == GameAction::Up) {
            settings_selection_ = (settings_selection_ - 1 + 8) % 8;
        } else if (action == GameAction::Down) {
            settings_selection_ = (settings_selection_ + 1) % 8;
        } else if (action == GameAction::Left) {
            if (settings_selection_ == 0) {
                int temp = selected_general_theme_;
                do {
                    temp = (temp - 1 + Config::THEME_COUNT) % Config::THEME_COUNT;
                } while (isColorLocked(temp));
                selected_general_theme_ = temp;
                saveHighScore();
            } else if (settings_selection_ == 1) {
                I18n::cycleLanguage(-1);
                saveHighScore();
            } else if (settings_selection_ == 4) {
                int temp = selected_pacman_color_;
                do {
                    temp = (temp - 1 + Config::THEME_COUNT) % Config::THEME_COUNT;
                } while (isColorLocked(temp));
                selected_pacman_color_ = temp;
                saveHighScore();
            }
        } else if (action == GameAction::Right) {
            if (settings_selection_ == 0) {
                int temp = selected_general_theme_;
                do {
                    temp = (temp + 1) % Config::THEME_COUNT;
                } while (isColorLocked(temp));
                selected_general_theme_ = temp;
                saveHighScore();
            } else if (settings_selection_ == 1) {
                I18n::cycleLanguage(1);
                saveHighScore();
            } else if (settings_selection_ == 4) {
                int temp = selected_pacman_color_;
                do {
                    temp = (temp + 1) % Config::THEME_COUNT;
                } while (isColorLocked(temp));
                selected_pacman_color_ = temp;
                saveHighScore();
            }
        } else if (key == '\n' || key == '\r' || key == ' ') {
            activateSettingsSelection();
        }
        break;

    case GamePhase::Stats:
        if (key == 27 || key == '\n' || key == '\r' || key == ' ') {
            phase_             = GamePhase::MainMenu;
            main_menu_message_ = "";
            fade_animation_.fadeIn({255, 255, 255}, 300);
        }
        break;

    case GamePhase::LevelSelector:
        if (action == GameAction::Up) {
            if (level_select_cursor_ == 30) {
                level_select_cursor_ = 27;
            } else if (level_select_cursor_ >= 5) {
                level_select_cursor_ -= 5;
            } else {
                level_select_cursor_ = 30;
            }
        } else if (action == GameAction::Down) {
            if (level_select_cursor_ == 30) {
                level_select_cursor_ = 2;
            } else if (level_select_cursor_ + 5 < 30) {
                level_select_cursor_ += 5;
            } else {
                level_select_cursor_ = 30;
            }
        } else if (action == GameAction::Left) {
            if (level_select_cursor_ < 30) {
                if (level_select_cursor_ % 5 > 0) {
                    level_select_cursor_ -= 1;
                }
            }
        } else if (action == GameAction::Right) {
            if (level_select_cursor_ < 30) {
                if (level_select_cursor_ % 5 < 4) {
                    level_select_cursor_ += 1;
                }
            }
        } else if (key == '\n' || key == '\r' || key == ' ') {
            if (level_select_cursor_ < 30) {
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
            } else if (level_select_cursor_ == 30) {
                phase_ = GamePhase::MainMenu;
                fade_animation_.fadeIn({255, 255, 255}, 300);
            }
        } else if (key == 27) {
            phase_ = GamePhase::MainMenu;
            fade_animation_.fadeIn({255, 255, 255}, 300);
        }
        break;

    case GamePhase::UsernameInput:
        if (key == 27) {
            phase_ = GamePhase::MainMenu;
        } else if (key == '\n' || key == '\r') {
            if (!input_username_.empty()) {
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
        if (key == 27) {
            phase_ = GamePhase::MainMenu;
        } else if (key == '\n' || key == '\r') {
            std::string code;
            for (char c : redeem_input_) {
                code += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (code == "RAINBOW") {
                unlocked_rainbow_ = true;
                saveHighScore();
                redeem_result_       = "CODE REDEEMED";
                redeem_result_valid_ = true;
            } else if (code == "WAEL") {
                max_unlocked_level_      = 30;
                unlocked_rainbow_        = true;
                letter_hunt_.letter_mask = LetterHuntState::FULL_MASK;
                pacterm_plus_unlocked_   = true;
                saveHighScore();
                redeem_result_       = "ALL LEVELS & THEMES UNLOCKED!";
                redeem_result_valid_ = true;
            } else {
                redeem_result_       = "INVALID CODE";
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
            if (key == 27) {
                is_binding_ = false;
            } else {
                if (binding_action_ == GameAction::Up)
                    custom_key_up_ = key;
                else if (binding_action_ == GameAction::Down)
                    custom_key_down_ = key;
                else if (binding_action_ == GameAction::Left)
                    custom_key_left_ = key;
                else if (binding_action_ == GameAction::Right)
                    custom_key_right_ = key;
                else if (binding_action_ == GameAction::Pause)
                    custom_key_pause_ = key;

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
                if (key_config_selection_ == 5) {
                    phase_ = GamePhase::Settings;
                } else {
                    is_binding_ = true;
                    if (key_config_selection_ == 0)
                        binding_action_ = GameAction::Up;
                    else if (key_config_selection_ == 1)
                        binding_action_ = GameAction::Down;
                    else if (key_config_selection_ == 2)
                        binding_action_ = GameAction::Left;
                    else if (key_config_selection_ == 3)
                        binding_action_ = GameAction::Right;
                    else if (key_config_selection_ == 4)
                        binding_action_ = GameAction::Pause;
                }
            } else if (key == 27) {
                phase_ = GamePhase::Settings;
            }
        }
        break;

    case GamePhase::Playing:
        if (dir != Direction::None) {
            pacman_.requestedDirection = dir;
        }
        if (action == GameAction::Pause || key == 27) {
            phase_                = GamePhase::Paused;
            pause_menu_selection_ = 0;
        }

        if (key == ' ' && level_ >= 17 && level_ <= 19 && dash_cooldown_ <= 0) {
            dash_cooldown_ = hasPowerup(PowerupKind::DashRapid) ? 1800 : 3000;
            playSound("sounds/eat_ghost.wav");
            Vec2 dir_vec = {0, 0};
            if (pacman_.currentDirection == Direction::Up)
                dir_vec = {0, -1};
            else if (pacman_.currentDirection == Direction::Down)
                dir_vec = {0, 1};
            else if (pacman_.currentDirection == Direction::Left)
                dir_vec = {-1, 0};
            else if (pacman_.currentDirection == Direction::Right)
                dir_vec = {1, 0};

            for (int i = 0; i < 3; ++i) {
                Vec2 next_pos      = map_.wrapTunnel(pacman_.position + dir_vec);
                TileType next_tile = map_.getTile(next_pos);
                if (next_tile != TileType::Wall && next_tile != TileType::GhostDoor) {
                    pacman_.prevPosition = pacman_.position;
                    pacman_.position     = next_pos;
                    spawnParticleBurst(pacman_.position, {255, 215, 0});
                    checkCollisions();
                    if (phase_ != GamePhase::Playing)
                        break;
                } else {
                    break;
                }
            }
        }
        break;

    case GamePhase::Paused:
        if (action == GameAction::Pause || key == 27) {
            phase_ = GamePhase::Playing;
        } else if (action == GameAction::Up) {
            pause_menu_selection_ = (pause_menu_selection_ - 1 + 5) % 5;
        } else if (action == GameAction::Down) {
            pause_menu_selection_ = (pause_menu_selection_ + 1) % 5;
        } else if (key == '\n' || key == '\r' || key == ' ') {
            if (pause_menu_selection_ == 0) {
                phase_ = GamePhase::Playing;
            } else if (pause_menu_selection_ == 1) {
                pre_theme_info_phase_ = GamePhase::Paused;
                phase_                = GamePhase::ThemeInfo;
            } else if (pause_menu_selection_ == 2) {
                startLevel(level_);
                startGetReady();
            } else if (pause_menu_selection_ == 3) {
                phase_ = GamePhase::MainMenu;
            } else if (pause_menu_selection_ == 4) {
                running_ = false;
            }
        }
        break;

    case GamePhase::ThemeInfo:
        if (key == 27 || key == '\n' || key == '\r' || key == ' ' || action == GameAction::Pause) {
            phase_ = pre_theme_info_phase_;
        }
        break;

    case GamePhase::DevPasswordInput:
        if (key == 27) {
            phase_ = pre_dev_phase_;
        } else if (key == '\n' || key == '\r') {
            std::string upper_pw = dev_password_buffer_;
            for (auto& ch : upper_pw)
                ch = std::toupper(ch);

            if (upper_pw == "WARCH") {
                phase_              = GamePhase::DevMenu;
                dev_menu_selection_ = 0;
            } else {
                phase_ = pre_dev_phase_;
            }
            dev_password_buffer_ = "";
        } else if (key == 127 || key == 8) {
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
        if (key == 27) {
            phase_ = pre_dev_phase_;
        } else if (action == GameAction::Up) {
            dev_menu_selection_ = (dev_menu_selection_ - 1 + 9) % 9;
        } else if (action == GameAction::Down) {
            dev_menu_selection_ = (dev_menu_selection_ + 1) % 9;
        } else if (action == GameAction::Left || action == GameAction::Right) {
            int dir = (action == GameAction::Right) ? 1 : -1;
            switch (dev_menu_selection_) {
            case 0:
                level_ = std::clamp(level_ + dir, 1, 30);
                if (pre_dev_phase_ == GamePhase::Playing || pre_dev_phase_ == GamePhase::Paused) {
                    startLevel(level_);
                }
                break;
            case 1: {
                int temp = selected_pacman_color_;
                do {
                    temp = (temp + dir + Config::THEME_COUNT) % Config::THEME_COUNT;
                } while (isColorLocked(temp));
                selected_pacman_color_ = temp;
                saveHighScore();
                break;
            }
            case 2: lives_ = std::clamp(lives_ + dir, 1, 66); break;
            case 3: score_ = std::max(0, score_ + dir * 1000); break;
            case 4: immortal_ = !immortal_; break;
            case 5: cheat_freeze_ghosts_ = !cheat_freeze_ghosts_; break;
            case 6: cheat_super_speed_ = !cheat_super_speed_; break;
            case 7:
            case 8: break;
            default: break;
            }
        } else if (key == '\n' || key == '\r' || key == ' ') {
            if (dev_menu_selection_ == 4) {
                immortal_ = !immortal_;
            } else if (dev_menu_selection_ == 5) {
                cheat_freeze_ghosts_ = !cheat_freeze_ghosts_;
            } else if (dev_menu_selection_ == 6) {
                cheat_super_speed_ = !cheat_super_speed_;
            } else if (dev_menu_selection_ == 7) {
                if (pre_dev_phase_ == GamePhase::Playing || pre_dev_phase_ == GamePhase::Paused) {
                    int points = 0;
                    for (int y = 0; y < Config::MAP_HEIGHT; ++y) {
                        for (int x = 0; x < Config::MAP_WIDTH; ++x) {
                            TileType t = map_.getTile(x, y);
                            if (t == TileType::Dot)
                                points += Config::SCORE_DOT;
                            else if (t == TileType::PowerPellet)
                                points += Config::SCORE_POWER_PELLET;
                            if (t == TileType::Dot || t == TileType::PowerPellet) {
                                map_.setTile({x, y}, TileType::Empty);
                            }
                        }
                    }
                    addScore(points);
                    startLevelClear();
                }
            } else if (dev_menu_selection_ == 8) {
                phase_ = pre_dev_phase_;
            }
        }
        break;

    case GamePhase::GameOver:
        if (key == '\n' || key == '\r' || key == ' ') {
            score_    = 0;
            lives_    = Config::INITIAL_LIVES;
            level_    = 1;
            immortal_ = false;
            map_.reset();
            startMainMenu();
        }
        break;

    default: break;
    }
}

void GameEngine::handleMouseClick() {
    click_feedback_timer_ms_ = 200;
    fade_animation_.fadeIn({255, 255, 255}, 300);

    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;
    int mx         = mouse_x_;
    int my         = mouse_y_;

    auto hitText = [this](int row, int col, std::string_view text, int my, int mx) noexcept -> bool {
        return my == row && mx >= col && mx < col + static_cast<int>(displayWidth(text));
    };

    switch (phase_) {
    case GamePhase::MainMenu: {
        bool big = render_width_ >= 66 && render_height_ >= 22;

        std::string install_text = isInstalledLocally() ? std::string(I18n::t("main_menu.uninstall")) : std::string(I18n::t("main_menu.install"));
        std::string user_text =
            big ? (std::string(I18n::t("main_menu.username")) + ":       " + username_) : (std::string(I18n::t("main_menu.username")) + ": " + username_);

        std::array<std::string, 7> opts = {
            "  " + std::string(I18n::t("main_menu.start")),  "  " + user_text,    "  " + std::string(I18n::t("main_menu.stats")),
            "  " + std::string(I18n::t("main_menu.redeem")), "  " + install_text, "  " + std::string(I18n::t("main_menu.settings")),
            "  " + std::string(I18n::t("main_menu.quit"))};

        size_t max_w = 0;
        for (const auto& o : opts) {
            max_w = std::max(max_w, glyphCount(o));
        }
        int col_start = center_col - static_cast<int>(max_w / 2);

        for (int i = 0; i < 7; ++i) {
            int row = center_row - 2 + i;
            if (hitText(row, col_start, opts[i], my, mx)) {
                main_menu_selection_ = i;
                switch (i) {
                case 0:
                    phase_               = GamePhase::LevelSelector;
                    level_select_cursor_ = 0;
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                    break;
                case 1:
                    phase_          = GamePhase::UsernameInput;
                    input_username_ = username_;
                    break;
                case 2:
                    phase_ = GamePhase::Stats;
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                    break;
                case 3:
                    phase_         = GamePhase::RedeemInput;
                    redeem_input_  = "";
                    redeem_result_ = "";
                    break;
                case 4:
                    if (isInstalledLocally()) {
                        if (delete_bin())
                            main_menu_message_ = "Removed successfully!";
                        else
                            main_menu_message_ = "Removal failed!";
                    } else {
                        if (install_bin())
                            main_menu_message_ = "Installed successfully!";
                        else
                            main_menu_message_ = "Installation failed!";
                    }
                    main_menu_msg_timer_ms_ = 3000;
                    break;
                case 5:
                    phase_              = GamePhase::Settings;
                    settings_selection_ = 0;
                    main_menu_message_  = "";
                    fade_animation_.fadeIn({255, 255, 255}, 300);
                    break;
                case 6: running_ = false; break;
                }
                return;
            }
        }
        break;
    }
    case GamePhase::Settings: {
        for (int i = 0; i < 8; ++i) {
            int row = center_row - 4 + i;
            if (my == row && mx >= center_col - 22 && mx < center_col + 22) {
                settings_selection_ = i;
                activateSettingsSelection();
                return;
            }
        }
        break;
    }
    case GamePhase::LevelSelector: {
        for (int r = 0; r < 6; ++r) {
            for (int c = 0; c < 5; ++c) {
                int lvl = r * 5 + c + 1;
                int row = center_row - 8 + r * 2;
                int col = center_col - 22 + c * 11;
                if (my == row && mx >= col - 4 && mx < col + 4) {
                    if (lvl <= max_unlocked_level_) {
                        score_ = 0;
                        lives_ = Config::INITIAL_LIVES;
                        level_ = lvl;
                        games_played_++;
                        saveHighScore();
                        acid_trails_.clear();
                        lava_tiles_.clear();
                        startLevel(level_);
                        startGetReady();
                    }
                    return;
                }
            }
        }
        int row_back          = center_row + 4;
        std::string back_base = "[ " + std::string(I18n::t("level_select.back")) + " ]";
        std::string back_text = "  " + back_base + "  ";
        int back_col_start    = center_col - static_cast<int>(glyphCount(back_text)) / 2;
        if (hitText(row_back, back_col_start, back_text, my, mx)) {
            phase_ = GamePhase::MainMenu;
            return;
        }
        break;
    }
    case GamePhase::Paused: {
        std::array<std::string, 5> opts = {"  " + std::string(I18n::t("pause.resume")), "  " + std::string(I18n::t("pause.theme_info")),
                                           "  " + std::string(I18n::t("pause.restart")), "  " + std::string(I18n::t("pause.return_menu")),
                                           "  " + std::string(I18n::t("pause.quit"))};
        int col_start                   = center_col - 12;
        for (int i = 0; i < 5; ++i) {
            int row = center_row - 2 + i;
            if (hitText(row, col_start, opts[i], my, mx)) {
                if (i == 0) {
                    phase_ = GamePhase::Playing;
                } else if (i == 1) {
                    pre_theme_info_phase_ = GamePhase::Paused;
                    phase_                = GamePhase::ThemeInfo;
                } else if (i == 2) {
                    map_.reset();
                    score_ = 0;
                    lives_ = Config::INITIAL_LIVES;
                    acid_trails_.clear();
                    lava_tiles_.clear();
                    popups_.clear();
                    particles_.clear();
                    startLevel(level_);
                    startGetReady();
                } else if (i == 3) {
                    startMainMenu();
                } else {
                    running_ = false;
                }
                return;
            }
        }
        break;
    }
    case GamePhase::ThemeInfo: {
        phase_ = pre_theme_info_phase_;
        return;
    }
    case GamePhase::KeyConfig: {
        if (is_binding_)
            break;
        std::array<std::string, 6> opts = {"  UP:    [ " + getKeyName(custom_key_up_) + " ]",    "  DOWN:  [ " + getKeyName(custom_key_down_) + " ]",
                                           "  LEFT:  [ " + getKeyName(custom_key_left_) + " ]",  "  RIGHT: [ " + getKeyName(custom_key_right_) + " ]",
                                           "  PAUSE: [ " + getKeyName(custom_key_pause_) + " ]", "  " + std::string(I18n::t("key_config.save_back"))};
        int col_start                   = center_col - 16;
        for (int i = 0; i < 6; ++i) {
            int row = center_row - 4 + i;
            if (hitText(row, col_start, opts[i], my, mx)) {
                if (i < 5) {
                    key_config_selection_ = i;
                    is_binding_           = true;
                    if (i == 0)
                        binding_action_ = GameAction::Up;
                    else if (i == 1)
                        binding_action_ = GameAction::Down;
                    else if (i == 2)
                        binding_action_ = GameAction::Left;
                    else if (i == 3)
                        binding_action_ = GameAction::Right;
                    else if (i == 4)
                        binding_action_ = GameAction::Pause;
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
        int col          = center_col - 14;
        if (hitText(center_row + 1, col, text, my, mx)) {
            score_    = 0;
            lives_    = Config::INITIAL_LIVES;
            level_    = 1;
            immortal_ = false;
            map_.reset();
            startMainMenu();
        }
        break;
    }
    default: break;
    }
}

bool GameEngine::isMouseHovering(int row, int col, std::string_view text) const noexcept {
    if (!mouse_hover_active_)
        return false;
    return mouse_y_ == row && mouse_x_ >= col && mouse_x_ < col + static_cast<int>(displayWidth(text));
}

void GameEngine::run() {
    auto last_time = std::chrono::steady_clock::now();

    while (running_) {
        if (SignalManager::shutdown_requested.load(std::memory_order_relaxed)) {
            running_ = false;
            break;
        }

        if (SignalManager::window_resized.exchange(false, std::memory_order_relaxed)) {
            queryTerminalSize();
            if (term_size_.x != render_width_ || term_size_.y != render_height_) {
                initRenderer();
            }
        }

        auto now     = std::chrono::steady_clock::now();
        int delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        last_time    = now;
        if (delta_ms > 50)
            delta_ms = 50;

        int key = readKey();
        while (key != -1) {
            handleInput(key);
            if (!running_)
                break;
            key = readKey();
        }

        if (!running_)
            break;

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
    case GamePhase::UsernameInput:
        if (main_menu_msg_timer_ms_ > 0) {
            main_menu_msg_timer_ms_ -= delta_ms;
            if (main_menu_msg_timer_ms_ <= 0) {
                main_menu_message_ = "";
            }
        }
        break;

    case GamePhase::Screensaver:
        if (screensaver_dir_ == 1) {
            screensaver_x_ += delta_ms * 0.012;
            if (screensaver_x_ > render_width_ + 25) {
                screensaver_dir_ = -1;
                screensaver_x_   = render_width_ + 10;
            }
        } else {
            screensaver_x_ -= delta_ms * 0.012;
            if (screensaver_x_ < -25) {
                screensaver_dir_ = 1;
                screensaver_x_   = -10;
            }
        }
        break;

    case GamePhase::GetReady:
        phase_timer_ms_ -= delta_ms;
        if (phase_timer_ms_ <= 0) {
            startPlaying();
        }
        break;

    case GamePhase::Playing: {
        time_played_ms_ += delta_ms;
        level_start_time_ms_ += delta_ms;

        if (fever_timer_ms_ > 0) {
            fever_timer_ms_ -= delta_ms;
            if (fever_timer_ms_ <= 0) {
                fever_timer_ms_ = 0;
                fever_active_   = false;
            }
        }

        if (ghost_freeze_timer_ms_ > 0) {
            ghost_freeze_timer_ms_ -= delta_ms;
            if (ghost_freeze_timer_ms_ < 0)
                ghost_freeze_timer_ms_ = 0;
        }
        if (pac_speed_timer_ms_ > 0) {
            pac_speed_timer_ms_ -= delta_ms;
            if (pac_speed_timer_ms_ < 0)
                pac_speed_timer_ms_ = 0;
        }
        if (letter_score_mult_timer_ms_ > 0) {
            letter_score_mult_timer_ms_ -= delta_ms;
            if (letter_score_mult_timer_ms_ < 0)
                letter_score_mult_timer_ms_ = 0;
        }

        updateBonusFruit(delta_ms);

        if (fruit_magnet_timer_ms_ > 0) {
            fruit_magnet_timer_ms_ -= delta_ms;
            if (fruit_magnet_timer_ms_ < 0)
                fruit_magnet_timer_ms_ = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    int nx = pacman_.position.x + dx;
                    int ny = pacman_.position.y + dy;
                    if (nx >= 0 && nx < Config::MAP_WIDTH && ny >= 0 && ny < Config::MAP_HEIGHT) {
                        Vec2 np{nx, ny};
                        if (map_.getTile(np) == TileType::Dot) {
                            map_.setTile(np, TileType::Empty);
                            addScore(Config::SCORE_DOT);
                            eatDot();
                            spawnParticleBurst(np, {255, 200, 100});
                        }
                    }
                }
            }
        }

        if (fruit_double_bounty_timer_ms_ > 0) {
            fruit_double_bounty_timer_ms_ -= delta_ms;
            if (fruit_double_bounty_timer_ms_ < 0)
                fruit_double_bounty_timer_ms_ = 0;
        }

        if (letter_hunt_.active) {
            letter_hunt_.timer_ms -= delta_ms;
            if (letter_hunt_.timer_ms <= 0) {
                if (map_.getTile(letter_hunt_.pos) == letter_hunt_.type) {
                    map_.setTile(letter_hunt_.pos, TileType::Empty);
                }
                letter_hunt_.active = false;
            }
        }

        updateGlobalModeTimer(delta_ms);

        if (speed_boost_timer_ms_ > 0) {
            speed_boost_timer_ms_ -= delta_ms;
            if (speed_boost_timer_ms_ < 0)
                speed_boost_timer_ms_ = 0;
        }
        if (ice_freeze_timer_ms_ > 0) {
            ice_freeze_timer_ms_ -= delta_ms;
            if (ice_freeze_timer_ms_ < 0)
                ice_freeze_timer_ms_ = 0;
        }
        if (dash_cooldown_ > 0) {
            dash_cooldown_ -= delta_ms;
        }

        if (hasPowerup(PowerupKind::LavaResist)) {
            if (lava_resist_active_) {
                lava_resist_window_ms_ -= delta_ms;
                if (lava_resist_window_ms_ <= 0) {
                    lava_resist_active_      = false;
                    lava_resist_cooldown_ms_ = 9000;
                }
            } else if (lava_resist_cooldown_ms_ > 0) {
                lava_resist_cooldown_ms_ -= delta_ms;
                if (lava_resist_cooldown_ms_ < 0)
                    lava_resist_cooldown_ms_ = 0;
            } else {
                lava_resist_active_    = true;
                lava_resist_window_ms_ = 3500;
                spawnParticleBurst(pacman_.position, levelThemeColor(level_));
            }
        }
        if (warp_stun_timer_ms_ > 0) {
            warp_stun_timer_ms_ -= delta_ms;
            if (warp_stun_timer_ms_ < 0)
                warp_stun_timer_ms_ = 0;
        }

        if (hasPowerup(PowerupKind::GlitchWarp)) {
            glitch_warp_timer_ms_ -= delta_ms;
            if (glitch_warp_timer_ms_ <= 0) {
                glitch_warp_timer_ms_   = 7000 + static_cast<int>(rng_() % 5000);
                std::vector<Vec2> tiles = reachableTiles();
                if (!tiles.empty()) {
                    Vec2 target = tiles[rng_() % tiles.size()];
                    if (target != pacman_.position) {
                        spawnParticleBurst(pacman_.position, levelThemeColor(level_));
                        pacman_.position = target;
                        pac_just_warped_ = true;
                        spawnParticleBurst(target, levelThemeColor(level_));
                    }
                }
            }
        }

        if (ghost_blitz_timer_ms_ > 0) {
            ghost_blitz_timer_ms_ -= delta_ms;
        }
        if (level_ >= 21 && level_ <= 23) {
            ghost_blitz_cooldown_ -= delta_ms;
            if (ghost_blitz_cooldown_ <= 0) {
                ghost_blitz_cooldown_ = 4000 + rng_() % 3000;
                ghost_blitz_timer_ms_ = 1500;
            }
        }

        if (level_ >= 24 && level_ <= 26) {
            ice_freeze_cooldown_ -= delta_ms;
            if (ice_freeze_cooldown_ <= 0) {
                ice_freeze_cooldown_ = 7000 + rng_() % 3000;
                ice_freeze_timer_ms_ = hasPowerup(PowerupKind::FreezeLinger) ? 2000 : 1200;
                spawnParticleBurst(pacman_.position, {140, 230, 255});
            }
        }

        for (auto& popup : popups_) {
            popup.lifetime_ms -= delta_ms;
        }
        std::erase_if(popups_, [](const FloatingPopup& p) { return p.lifetime_ms <= 0; });

        double dt_sec = delta_ms / 1000.0;
        for (auto& p : particles_) {
            p.lifetime_ms -= delta_ms;
            p.x += p.vx * dt_sec;
            p.y += p.vy * dt_sec;
        }
        std::erase_if(particles_, [](const Particle& p) { return p.lifetime_ms <= 0; });

        for (auto& trail : acid_trails_) {
            trail.lifetime_ms -= delta_ms;
        }
        std::erase_if(acid_trails_, [](const AcidTrail& t) { return t.lifetime_ms <= 0; });

        if (level_ >= 5 && level_ <= 8) {
            bool already_exists = false;
            for (const auto& trail : acid_trails_) {
                if (trail.pos == pacman_.position) {
                    already_exists = true;
                    break;
                }
            }
            if (!already_exists) {
                acid_trails_.push_back(AcidTrail{.pos = pacman_.position, .lifetime_ms = 3000});
            }
        }

        for (auto& lava : lava_tiles_) {
            if (lava.warning_ms > 0) {
                lava.warning_ms -= delta_ms;
            } else if (lava.active_ms > 0) {
                lava.active_ms -= delta_ms;
                if (pacman_.position == lava.pos && !immortal_ && !(hasPowerup(PowerupKind::LavaResist) && lava_resist_active_)) {
                    pacmanCaught();
                }
            }
        }
        std::erase_if(lava_tiles_, [](const LavaTile& l) { return l.warning_ms <= 0 && l.active_ms <= 0; });
        if ((level_ >= 13 && level_ <= 16) || (level_ >= 27 && level_ <= 29)) {
            lava_spawn_timer_ms_ -= delta_ms;
            if (lava_spawn_timer_ms_ <= 0) {
                lava_spawn_timer_ms_ = 3000 + rng_() % 3000;
                std::vector<Vec2> empty_cells;
                for (int y = 0; y < Config::MAP_HEIGHT; ++y) {
                    for (int x = 0; x < Config::MAP_WIDTH; ++x) {
                        if (map_.getTile(x, y) == TileType::Empty && !(y >= 12 && y <= 16 && x >= 10 && x <= 17)) {
                            empty_cells.push_back({x, y});
                        }
                    }
                }
                if (!empty_cells.empty()) {
                    Vec2 pos = empty_cells[rng_() % empty_cells.size()];
                    lava_tiles_.push_back(LavaTile{.pos = pos, .warning_ms = 1500, .active_ms = 1500});
                }
            }
        }

        if (level_ >= 9 && level_ <= 12) {
            Vec2 pos    = pacman_.position;
            bool warped = false;
            if (pos == portal_A1_ && !pac_just_warped_) {
                pacman_.position = portal_A2_;
                pac_just_warped_ = true;
                warped           = true;
            } else if (pos == portal_A2_ && !pac_just_warped_) {
                pacman_.position = portal_A1_;
                pac_just_warped_ = true;
                warped           = true;
            } else if (pos == portal_B1_ && !pac_just_warped_) {
                pacman_.position = portal_B2_;
                pac_just_warped_ = true;
                warped           = true;
            } else if (pos == portal_B2_ && !pac_just_warped_) {
                pacman_.position = portal_B1_;
                pac_just_warped_ = true;
                warped           = true;
            } else if (pos != portal_A1_ && pos != portal_A2_ && pos != portal_B1_ && pos != portal_B2_) {
                pac_just_warped_ = false;
            }
            if (warped && hasPowerup(PowerupKind::WarpStun)) {
                warp_stun_timer_ms_ = 700;
                spawnParticleBurst(pacman_.position, levelThemeColor(level_));
            }
        }

        for (size_t i = 0; i < ghosts_.size(); ++i) {
            updateGhostAI(ghosts_[i], delta_ms);
        }

        int pac_interval = Config::PAC_MOVE_INTERVAL;
        if (cheat_super_speed_) {
            pac_interval = Config::PAC_MOVE_INTERVAL * 4 / 10;
        } else if (speed_boost_timer_ms_ > 0 || pac_speed_timer_ms_ > 0) {
            pac_interval = static_cast<int>(Config::PAC_MOVE_INTERVAL * Config::PAC_SPEED_BOOST_FACTOR);
        } else if (fever_active_ && fever_timer_ms_ > 0) {
            pac_interval = static_cast<int>(Config::PAC_MOVE_INTERVAL * Config::FEVER_SPEED_FACTOR);
        } else if (hasPowerup(PowerupKind::PacSpeed)) {
            pac_interval = static_cast<int>(Config::PAC_MOVE_INTERVAL * 85 / 100);
        }
        pac_move_accumulator_ = std::min(pac_move_accumulator_ + delta_ms, pac_interval * 2);
        while (pac_move_accumulator_ >= pac_interval) {
            pac_move_accumulator_ -= pac_interval;
            pacman_.tryMove(map_);
            pacman_.advanceAnim();
            checkCollisions();
        }

        for (size_t i = 0; i < ghosts_.size(); ++i) {
            auto& g = ghosts_[i];
            if ((ice_freeze_timer_ms_ > 0 || ghost_freeze_timer_ms_ > 0 || cheat_freeze_ghosts_ || warp_stun_timer_ms_ > 0) && g.mode != GhostMode::Eaten) {
                continue;
            }

            int interval = Config::GHOST_MOVE_INTERVAL;
            if (g.mode == GhostMode::Frightened) {
                interval = Config::GHOST_MOVE_INTERVAL * 2;
            } else if (g.mode == GhostMode::Eaten) {
                interval = Config::GHOST_MOVE_INTERVAL / 2;
            }

            if (level_ >= 5 && level_ <= 8 && g.mode != GhostMode::Eaten) {
                bool on_acid = false;
                for (const auto& trail : acid_trails_) {
                    if (trail.pos == g.position) {
                        on_acid = true;
                        break;
                    }
                }
                if (on_acid) {
                    interval *= 2;
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
                    interval = interval * 3 / 4;
                }
            }
            if (level_ >= 21 && level_ <= 23 && ghost_blitz_timer_ms_ > 0 && g.mode != GhostMode::Eaten) {
                interval = interval * 2 / 3;
            }
            if (hasPowerup(PowerupKind::GhostSlow) && g.mode != GhostMode::Eaten) {
                interval = static_cast<int>(interval * 118 / 100);
            }

            ghost_accumulators_[i] = std::min(ghost_accumulators_[i] + delta_ms, interval * 2);
            while (ghost_accumulators_[i] >= interval) {
                ghost_accumulators_[i] -= interval;
                moveGhost(g);

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

        afk_timer_ms_ += delta_ms;
        if (afk_timer_ms_ >= 20000) {
            phase_           = GamePhase::Screensaver;
            screensaver_x_   = -20.0;
            screensaver_dir_ = 1;
        }
        break;
    }

    case GamePhase::LevelClear:
        for (auto& popup : popups_) {
            popup.lifetime_ms -= delta_ms;
        }
        std::erase_if(popups_, [](const FloatingPopup& p) { return p.lifetime_ms <= 0; });

        for (auto& p : particles_) {
            p.lifetime_ms -= delta_ms;
            p.x += p.vx * (delta_ms / 1000.0);
            p.y += p.vy * (delta_ms / 1000.0);
        }
        std::erase_if(particles_, [](const Particle& p) { return p.lifetime_ms <= 0; });
        phase_timer_ms_ -= delta_ms;
        if (phase_timer_ms_ <= 0) {
            finishLevelClear();
        } else {
            if (phase_timer_ms_ % 200 < delta_ms) {
                spawnParticleBurst(pacman_.position, static_cast<int>(phase_timer_ms_ / 200) % 2 == 0 ? Color{255, 255, 0} : Color{255, 183, 174});
            }
        }
        break;

    case GamePhase::PacDying:
        phase_timer_ms_ -= delta_ms;
        if (phase_timer_ms_ <= 0) {
            lives_--;
            if (lives_ <= 0) {
                startGameOver();
            } else {
                startGetReady();
            }
        }
        break;

    case GamePhase::GameOver: break;

    case GamePhase::Paused: break;

    case GamePhase::DevMenu:
    case GamePhase::DevPasswordInput: break;

    case GamePhase::KeyConfig: break;
    case GamePhase::ThemeInfo: break;
    }
    if (click_feedback_timer_ms_ > 0) {
        click_feedback_timer_ms_ -= delta_ms;
        if (click_feedback_timer_ms_ < 0)
            click_feedback_timer_ms_ = 0;
    }
}

void GameEngine::updateGlobalModeTimer(int delta_ms) {
    if (current_wave_ >= MODE_WAVES.size())
        return;

    bool any_frightened = false;
    for (const auto& g : ghosts_) {
        if (g.mode == GhostMode::Frightened) {
            any_frightened = true;
            break;
        }
    }
    if (any_frightened)
        return;

    int duration = MODE_WAVES[current_wave_].duration_ms;
    if (duration == -1)
        return;

    global_mode_timer_ms_ += delta_ms;
    if (global_mode_timer_ms_ >= duration) {
        global_mode_timer_ms_ -= duration;
        current_wave_++;

        GhostMode next_mode = getGlobalMode();
        for (auto& g : ghosts_) {
            if (g.mode == GhostMode::Chase || g.mode == GhostMode::Scatter) {
                g.mode             = next_mode;
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
        case GhostPersonality::Pinky: return Config::PINKY_SCATTER;
        case GhostPersonality::Inky: return Config::INKY_SCATTER;
        case GhostPersonality::Clyde: return Config::CLYDE_SCATTER;
        }
    }

    if (ghost.mode == GhostMode::Chase) {
        switch (ghost.personality) {
        case GhostPersonality::Blinky: return pacman_.position;

        case GhostPersonality::Pinky: {
            Vec2 target = pacman_.position + directionToVec2(pacman_.currentDirection) * 4;
            if (pacman_.currentDirection == Direction::Up) {
                target.x -= 4;
            }
            return clampToMap(target);
        }

        case GhostPersonality::Inky: {
            Vec2 pivot = pacman_.position + directionToVec2(pacman_.currentDirection) * 2;
            if (pacman_.currentDirection == Direction::Up) {
                pivot.x -= 2;
            }
            Vec2 blinky_pos = ghosts_[0].position;
            return clampToMap(pivot + (pivot - blinky_pos));
        }

        case GhostPersonality::Clyde: {
            int dx      = ghost.position.x - pacman_.position.x;
            int dy      = ghost.position.y - pacman_.position.y;
            int dist_sq = dx * dx + dy * dy;
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
    ghost.prevPosition = ghost.position;

    if (ghost.mode == GhostMode::InHouse) {
        bool release = false;
        if (ghost.personality == GhostPersonality::Pinky && ghost.dotCounter >= 0)
            release = true;
        else if (ghost.personality == GhostPersonality::Inky && ghost.dotCounter >= 30)
            release = true;
        else if (ghost.personality == GhostPersonality::Clyde && ghost.dotCounter >= 60)
            release = true;

        if (release) {
            ghost.exitHouse(getGlobalMode());
        } else {
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
        ghost.reviveInHouse(getGlobalMode());
        return;
    }

    Direction next_dir = Direction::None;

    if (ghost.mode == GhostMode::Frightened) {
        Direction forbidden = ghost.reverseDirection();
        std::array<Direction, 4> valid_dirs;
        size_t count = 0;
        for (Direction d : {Direction::Up, Direction::Right, Direction::Down, Direction::Left}) {
            if (d == forbidden)
                continue;
            Vec2 next = map_.wrapTunnel(ghost.position + directionToVec2(d));
            if (map_.isWalkableByGhost(next)) {
                valid_dirs[count++] = d;
            }
        }
        if (count > 0) {
            next_dir = valid_dirs[rng_() % count];
        } else {
            next_dir = forbidden;
        }
    } else {
        Vec2 target = calculateGhostTarget(ghost);
        next_dir    = bfsNextDirection(map_, ghost.position, target, ghost.reverseDirection());
    }

    if (next_dir != Direction::None) {
        ghost.position         = ghost.position + directionToVec2(next_dir);
        ghost.position         = map_.wrapTunnel(ghost.position);
        ghost.currentDirection = next_dir;
    }
}

void GameEngine::updateGhostAI(Ghost& ghost, int delta_ms) {
    if (ghost.mode == GhostMode::Frightened) {
        ghost.updateFrightened(delta_ms);
        if (ghost.mode != GhostMode::Frightened) {
            ghost.mode = getGlobalMode();
        }
    }
}

void GameEngine::checkCollisions() {
    if (phase_ != GamePhase::Playing)
        return;

    TileType tile = map_.getTile(pacman_.position);

    if (tile == TileType::Dot) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(Config::SCORE_DOT);
        if (hasPowerup(PowerupKind::DotBonus)) {
            addScore(Config::SCORE_DOT);
        }
        if (hasPowerup(PowerupKind::GlitchLuck) && (rng_() % 4) == 0) {
            addScore(Config::SCORE_DOT * 2);
        }
        eatDot();
    } else if (tile == TileType::PowerPellet) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(Config::SCORE_POWER_PELLET);
        if (hasPowerup(PowerupKind::PelletBonus)) {
            addScore(Config::SCORE_POWER_PELLET);
        }
        eatPowerPellet();
    } else if (tile == TileType::Cherry || tile == TileType::Strawberry || tile == TileType::Orange || tile == TileType::Apple || tile == TileType::Melon ||
               tile == TileType::Galaxian || tile == TileType::Bell || tile == TileType::Key) {
        map_.setTile(pacman_.position, TileType::Empty);
        eatBonusFruit(tile);
    } else if (tile == TileType::GoldenApple) {
        map_.setTile(pacman_.position, TileType::Empty);
        addScore(1000);
        ice_freeze_timer_ms_ = 3000;
        spawnScorePopup(pacman_.position, 1000, {255, 215, 0});
        spawnParticleBurst(pacman_.position, {255, 215, 0});
        playSound("sounds/eat_pellet.wav");
    } else if (tile == TileType::Heart) {
        map_.setTile(pacman_.position, TileType::Empty);
        if (lives_ < 66) {
            lives_++;
        }
        spawnScorePopup(pacman_.position, 100, {255, 105, 180});
        spawnParticleBurst(pacman_.position, {255, 105, 180});
        playSound("sounds/eat_pellet.wav");
    } else if (tile >= TileType::LetterP && tile <= TileType::LetterM) {
        map_.setTile(pacman_.position, TileType::Empty);
        int letter_idx = std::to_underlying(tile) - std::to_underlying(TileType::LetterP);
        collectLetter(letter_idx, pacman_.position);
    }

    if (bonus_fruit_active_ && bonus_fruit_pos_ == pacman_.position) {
        eatBonusFruit(bonus_fruit_type_);
        bonus_fruit_active_ = false;
    }

    for (auto& g : ghosts_) {
        const bool direct_hit = (g.position == pacman_.position);
        const bool swap_hit =
            (g.prevPosition.x >= 0 && pacman_.prevPosition.x >= 0 && g.prevPosition == pacman_.position && pacman_.prevPosition == g.position);
        if (direct_hit || swap_hit) {
            if (g.mode == GhostMode::Frightened) {
                eatGhost(g);
            } else if (g.mode == GhostMode::Chase || g.mode == GhostMode::Scatter) {
                if (fruit_shield_active_) {
                    fruit_shield_active_ = false;
                    spawnScorePopup(pacman_.position, 0, {100, 255, 100});
                    spawnParticleBurst(pacman_.position, {100, 255, 100});
                    playSound("sounds/eat_ghost.wav");
                    g.position = Config::GHOST_HOUSE_CENTER;
                    g.mode     = GhostMode::InHouse;
                    break;
                }
                if (!immortal_) {
                    pacmanCaught();
                    return;
                }
            }
        }
    }

    if (!extra_life_awarded_ && score_ >= Config::EXTRA_LIFE_AT) {
        lives_++;
        extra_life_awarded_ = true;
    }

    if (map_.remainingDots() == 0) {
        startLevelClear();
    }
}

void GameEngine::eatDot() {
    dots_eaten_++;
    for (auto& g : ghosts_) {
        if (g.mode == GhostMode::InHouse) {
            g.dotCounter++;
        }
    }
    int dots_left = map_.remainingDots();
    if (dots_left == 170 || dots_left == 70) {
        spawnBonusFruit();
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
    if (hasPowerup(PowerupKind::BlitzBounty) && ghost_blitz_timer_ms_ > 0) {
        points *= 2;
    }
    if (hasPowerup(PowerupKind::GhostBonus)) {
        points += 100;
    }
    addScore(points);
    spawnScorePopup(ghost.position, points, {100, 255, 100});
    spawnParticleBurst(ghost.position, {100, 255, 100});

    int next_combo = ghosts_eaten_combo_ + 1;
    if (next_combo == 4) {
        triggerFever();
    }
    if (fever_active_ && fever_timer_ms_ > 0) {
        spawnGhostTrail(ghost.position);
    }
    ghosts_eaten_combo_ = std::min(next_combo, 3);

    playSound("sounds/eat_ghost.wav");
}

void GameEngine::pacmanCaught() {
    pacman_.kill();
    deaths_++;
    level_deaths_++;
    playSound("sounds/death.wav");
    startDeath();
}

void GameEngine::startMainMenu() {
    phase_ = GamePhase::MainMenu;
    fade_animation_.fadeIn({255, 255, 255}, 300);
}

void GameEngine::startGetReady() {
    phase_          = GamePhase::GetReady;
    phase_timer_ms_ = Config::GETREADY_DURATION;

    pacman_.reset();
    pacman_.position = map_.findNearestWalkable(pacman_.position);
    for (auto& g : ghosts_) {
        g.reset();
    }

    pac_move_accumulator_ = 0;
    ghost_accumulators_.fill(0);

    playSound("sounds/ready.wav");
}

void GameEngine::startPlaying() {
    phase_                = GamePhase::Playing;
    current_wave_         = 0;
    global_mode_timer_ms_ = 0;
    level_start_time_ms_  = 0;
}

void GameEngine::startDeath() {
    phase_          = GamePhase::PacDying;
    phase_timer_ms_ = Config::DEATH_ANIM_DURATION;
}

void GameEngine::startLevelClear() {
    double par_s     = Config::LEVEL_PAR_BASE_S + map_.totalDots() * Config::LEVEL_PAR_PER_DOT_S;
    double elapsed_s = level_start_time_ms_ / 1000.0;
    double penalty   = 0.0;
    int rating       = computeLevelRating(elapsed_s, par_s, penalty);
    int bonus        = rating * Config::RATING_BONUS_PER_POINT;

    score_ += bonus;
    if (score_ > high_score_) {
        high_score_ = score_;
    }
    saveHighScore();

    spawnScorePopup(pacman_.position, bonus, {255, 255, 0});
    spawnParticleBurst(pacman_.position, {255, 255, 0});
    spawnParticleBurst(pacman_.position, {255, 183, 174});
    playSound("sounds/clear.wav");
    phase_          = GamePhase::LevelClear;
    phase_timer_ms_ = Config::LEVEL_CLEAR_ANIM_DURATION;
}

void GameEngine::finishLevelClear() {
    if (level_ == 30) {
        unlocked_rainbow_ = true;
        saveHighScore();
        phase_                  = GamePhase::LevelSelector;
        main_menu_message_      = "Congratulations! You beat the game!";
        main_menu_msg_timer_ms_ = 5000;
    } else {
        if (level_ + 1 > max_unlocked_level_) {
            max_unlocked_level_ = level_ + 1;
            if (max_unlocked_level_ > 30)
                max_unlocked_level_ = 30;
            saveHighScore();
        }
        level_++;
        startLevel(level_);
        startGetReady();
    }
}

void GameEngine::startGameOver() {
    phase_ = GamePhase::GameOver;
    if (score_ > high_score_) {
        high_score_ = score_;
    }
    saveHighScore();
}

void GameEngine::initRenderer() {
    render_width_  = term_size_.x;
    render_height_ = term_size_.y;
    front_buffer_.assign(render_width_ * render_height_, Cell{});
    back_buffer_.assign(render_width_ * render_height_, Cell{});
    output_batch_.reserve(65536);
    safeWrite(STDOUT_FILENO, "\033[2J\033[H\033[?25l", 14);
}

void GameEngine::setCell(int row, int col, const Cell& cell) {
    if (row >= 0 && row < render_height_ && col >= 0 && col < render_width_) {
        Cell out = cell;
        if (!cell.glyph.empty() && cell.glyph != " ") {
            if (apply_menu_theme_) {
                out.fg = applyGeneralTheme(out.fg, row, col);
            }
        }
        back_buffer_[row * render_width_ + col] = out;
    }
}

void GameEngine::setTileGlyph(int row, int col, std::string glyph, Color fg, Color bg, bool bold) {
    size_t w = displayWidth(glyph);
    Cell c1{.glyph = std::move(glyph), .fg = fg, .bg = bg, .bold = bold};
    setCell(row, col, c1);
    if (w < 2) {
        Cell pad{.glyph = " ", .fg = fg, .bg = bg, .bold = bold};
        setCell(row, col + 1, pad);
    } else {
        for (size_t k = 1; k < w; ++k) {
            Cell pad{.glyph = "", .fg = fg, .bg = bg, .bold = bold};
            setCell(row, col + static_cast<int>(k), pad);
        }
    }
}

void GameEngine::fillRow(int row, Color fg, Color bg) {
    if (row < 0 || row >= render_height_)
        return;
    Cell fill{.glyph = " ", .fg = fg, .bg = bg};
    auto start = back_buffer_.begin() + row * render_width_;
    std::fill(start, start + render_width_, fill);
}

size_t GameEngine::utf8SequenceLength(unsigned char c) noexcept {
    if (c >= 0xf0)
        return 4;
    if (c >= 0xe0)
        return 3;
    if (c >= 0xc0)
        return 2;
    return 1;
}

size_t GameEngine::glyphCount(std::string_view text) const noexcept {
    size_t count = 0;
    for (size_t i = 0; i < text.size();) {
        i += utf8SequenceLength(static_cast<unsigned char>(text[i]));
        ++count;
    }
    return count;
}

// Terminal display width of the text, matching how the terminal lays out each
// UTF-8 glyph (emoji, CJK, etc. occupy two columns; VS16 pairs count as one).
size_t GameEngine::displayWidth(std::string_view text) const noexcept {
    size_t w = 0;
    for (size_t i = 0; i < text.size();) {
        size_t consumed = 0;
        w += seqWidth(text, i, &consumed);
        i += consumed;
    }
    return w;
}

std::string GameEngine::truncateText(std::string_view text, size_t max_width) const {
    size_t total_w = displayWidth(text);
    if (total_w <= max_width) {
        return std::string(text);
    }
    if (max_width <= 3) {
        return std::string(max_width, '.');
    }
    size_t target_w = max_width - 3;
    size_t cur_w    = 0;
    size_t byte_len = 0;
    for (size_t i = 0; i < text.size();) {
        size_t consumed = 0;
        size_t w        = seqWidth(text, i, &consumed);
        if (cur_w + w > target_w) {
            break;
        }
        cur_w += w;
        i += consumed;
        byte_len = i;
    }
    return std::string(text.substr(0, byte_len)) + "...";
}

void GameEngine::drawString(int row, int col, std::string_view text, Color fg, Color bg, bool bold) {
    int current_col = col;
    for (size_t i = 0; i < text.size() && current_col < render_width_;) {
        size_t consumed = 0;
        size_t width    = seqWidth(text, i, &consumed);
        if (i + consumed > text.size())
            consumed = text.size() - i;

        Cell cell;
        cell.glyph.assign(text.data() + i, consumed);
        cell.fg   = fg;
        cell.bg   = bg;
        cell.bold = bold;

        setCell(row, current_col, cell);
        // A two-column glyph also occupies the following column in the
        // terminal. Reserve those cells (empty "continuation" cells skipped by
        // presentFrame) so later characters stay column-aligned.
        for (size_t k = 1; k < width; ++k) {
            Cell pad;
            pad.glyph = "";
            pad.fg    = fg;
            pad.bg    = bg;
            pad.bold  = bold;
            setCell(row, current_col + static_cast<int>(k), pad);
        }
        current_col += static_cast<int>(width);
        i += consumed;
    }
}

void GameEngine::drawGradientString(int row, int col, std::string_view text, Color start_fg, Color end_fg, Color bg) {
    size_t glyph_count = 0;
    for (size_t i = 0; i < text.size();) {
        size_t consumed = 0;
        seqWidth(text, i, &consumed);
        i += consumed;
        ++glyph_count;
    }
    if (glyph_count == 0)
        return;

    int current_col = col;
    size_t idx      = 0;
    for (size_t i = 0; i < text.size() && current_col < render_width_;) {
        size_t consumed = 0;
        size_t width    = seqWidth(text, i, &consumed);
        if (i + consumed > text.size())
            consumed = text.size() - i;

        double ratio = glyph_count > 1 ? static_cast<double>(idx) / (glyph_count - 1) : 0.0;
        Cell cell;
        cell.glyph.assign(text.data() + i, consumed);
        cell.fg = {
            static_cast<uint8_t>(start_fg.r + ratio * (end_fg.r - start_fg.r)),
            static_cast<uint8_t>(start_fg.g + ratio * (end_fg.g - start_fg.g)),
            static_cast<uint8_t>(start_fg.b + ratio * (end_fg.b - start_fg.b)),
        };
        cell.bg = bg;
        setCell(row, current_col, cell);
        for (size_t k = 1; k < width; ++k) {
            Cell pad;
            pad.glyph = "";
            pad.fg    = cell.fg;
            pad.bg    = bg;
            setCell(row, current_col + static_cast<int>(k), pad);
        }
        current_col += static_cast<int>(width);
        i += consumed;
        ++idx;
    }
}

void GameEngine::drawBox(int row, int col, int w, int h, Color fg, Color bg) {
    for (int r = row; r < row + h; ++r) {
        for (int c = col; c < col + w; ++c) {
            Cell cell{.glyph = " ", .fg = fg, .bg = bg};
            setCell(r, c, cell);
        }
    }
}

void GameEngine::clearBuffer(Color bg) {
    Cell empty_cell{.glyph = " ", .fg = {255, 255, 255}, .bg = bg};
    std::fill(back_buffer_.begin(), back_buffer_.end(), empty_cell);
}

void GameEngine::presentFrame() {
    output_batch_.clear();

    float fade       = 1.0f;
    bool fade_active = false;
    if (phase_ != GamePhase::GetReady && phase_ != GamePhase::Playing && phase_ != GamePhase::PacDying && phase_ != GamePhase::Paused &&
        phase_ != GamePhase::LevelClear) {
        if (fade_animation_.state == AnimationController::State::FadingIn) {
            Color fc = fade_animation_.getCurrentColor();
            fade     = (fc.r + fc.g + fc.b) / 765.0f;
            if (fade < 0.0f)
                fade = 0.0f;
            if (fade > 1.0f)
                fade = 1.0f;
            fade_active = true;
        }
    }

    Color current_fg       = {0, 0, 0};
    Color current_bg       = {0, 0, 0};
    bool current_bold      = false;
    bool current_blink     = false;
    bool style_initialized = false;

    auto appendInt = [&](int val) {
        char buf[16];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        output_batch_.append(buf, ptr - buf);
    };

    auto appendStyle = [&](const Color& fg, const Color& bg, bool bold, bool blink) {
        if (!style_initialized || (current_bold && !bold) || (current_blink && !blink)) {
            output_batch_ += "\033[0m";
            current_fg        = {0, 0, 0};
            current_bg        = {0, 0, 0};
            current_bold      = false;
            current_blink     = false;
            style_initialized = false;
        }

        if (!style_initialized || fg != current_fg) {
            output_batch_ += "\033[38;2;";
            appendInt(fg.r);
            output_batch_ += ';';
            appendInt(fg.g);
            output_batch_ += ';';
            appendInt(fg.b);
            output_batch_ += 'm';
            current_fg = fg;
        }

        if (!style_initialized || bg != current_bg) {
            output_batch_ += "\033[48;2;";
            appendInt(bg.r);
            output_batch_ += ';';
            appendInt(bg.g);
            output_batch_ += ';';
            appendInt(bg.b);
            output_batch_ += 'm';
            current_bg = bg;
        }

        if (bold && !current_bold) {
            output_batch_ += "\033[1m";
            current_bold = true;
        }
        if (blink && !current_blink) {
            output_batch_ += "\033[5m";
            current_blink = true;
        }

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
        int row_offset = r * render_width_;
        for (int c = 0; c < render_width_; ++c) {
            size_t idx       = static_cast<size_t>(row_offset + c);
            const Cell& back = back_buffer_[idx];
            if (back.glyph.empty()) {
                front_buffer_[idx] = back;
                continue;
            }
            Cell& front = front_buffer_[idx];

            Color out_fg = back.fg;
            Color out_bg = back.bg;
            if (fade_active) {
                out_fg = {static_cast<uint8_t>(back.fg.r * fade), static_cast<uint8_t>(back.fg.g * fade), static_cast<uint8_t>(back.fg.b * fade)};
                out_bg = {static_cast<uint8_t>(back.bg.r * fade), static_cast<uint8_t>(back.bg.g * fade), static_cast<uint8_t>(back.bg.b * fade)};
            }

            const bool is_dirty =
                (back.glyph != front.glyph || out_fg != front.fg || out_bg != front.bg || back.bold != front.bold || back.blink != front.blink);

            if (is_dirty) {
                if (cursor_r != r || cursor_c != c) {
                    appendPos(r + 1, c + 1);
                    cursor_r = r;
                    cursor_c = c;
                }

                if (!style_initialized || out_fg != current_fg || out_bg != current_bg || back.bold != current_bold || back.blink != current_blink) {
                    appendStyle(out_fg, out_bg, back.bold, back.blink);
                }

                output_batch_ += back.glyph;
                cursor_c += static_cast<int>(displayWidth(back.glyph));

                front.glyph = back.glyph;
                front.fg    = out_fg;
                front.bg    = out_bg;
                front.bold  = back.bold;
                front.blink = back.blink;
            }
        }
    }

    if (!output_batch_.empty()) {
        safeWrite(STDOUT_FILENO, output_batch_.data(), output_batch_.size());
        static_cast<void>(::fflush(stdout));
    }
}

void GameEngine::render() {
    current_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
            drawDoubleBorderBox(c_row - 2, c_col - 16, 32, 6, {255, 255, 0}, {0, 0, 0});
            drawString(c_row - 2, c_col - 5, " USERNAME ", {255, 255, 0});
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

    case GamePhase::ThemeInfo:
        apply_menu_theme_ = true;
        renderThemeInfo();
        apply_menu_theme_ = false;
        break;

    case GamePhase::Screensaver: renderScreensaver(); break;

    case GamePhase::GetReady: {
        const Viewport vp = getViewport();
        renderMap(&vp);
        renderEntities(&vp);
        renderEffects(&vp);
        renderHUD();
        apply_menu_theme_ = true;
        renderGetReady();
        apply_menu_theme_ = false;
        break;
    }

    case GamePhase::LevelClear: {
        const Viewport vp = getViewport();
        renderMap(&vp);
        renderEntities(&vp);
        renderEffects(&vp);
        renderHUD();
        break;
    }

    case GamePhase::Playing:
    case GamePhase::Paused:
    case GamePhase::DevMenu:
    case GamePhase::DevPasswordInput: {
        const Viewport vp = getViewport();
        renderMap(&vp);
        renderEntities(&vp);
        renderEffects(&vp);
        renderHUD();
        if (phase_ == GamePhase::Paused) {
            int center_row    = render_height_ / 2;
            int center_col    = render_width_ / 2;
            apply_menu_theme_ = true;

            drawTitleBorderBox(center_row - 4, center_col - 15, 31, 9, " " + std::string(I18n::t("pause.title")) + " ", {0, 255, 255}, {255, 255, 0},
                               {0, 0, 0});

            std::array<std::string, 5> options = {std::string(I18n::t("pause.resume")), std::string(I18n::t("pause.theme_info")),
                                                  std::string(I18n::t("pause.restart")), std::string(I18n::t("pause.return_menu")),
                                                  std::string(I18n::t("pause.quit"))};

            int block_left = center_col - 12;

            for (int i = 0; i < 5; ++i) {
                Color fg           = {220, 220, 220};
                std::string prefix = "  ";
                bool bold          = false;
                bool is_selected   = (i == pause_menu_selection_);
                bool is_hovered    = isMouseHovering(center_row - 2 + i, block_left, "  " + options[i]);

                if (is_selected) {
                    if (click_feedback_timer_ms_ > 0) {
                        fg   = {255, 255, 255};
                        bold = true;
                    } else {
                        fg = {255, 255, 0};
                    }
                    prefix = "> ";
                } else if (is_hovered) {
                    fg     = {0, 255, 255};
                    prefix = "> ";
                    bold   = true;
                }

                if (is_selected && is_hovered) {
                    fg   = {255, 255, 120};
                    bold = true;
                }
                drawString(center_row - 2 + i, block_left, prefix + options[i], fg, {0, 0, 0}, bold);
            }
            apply_menu_theme_ = false;
        } else if (phase_ == GamePhase::DevPasswordInput) {
            renderDevPasswordInput();
        } else if (phase_ == GamePhase::DevMenu) {
            renderDevMenu();
        }
        break;
    }

    case GamePhase::PacDying: {
        const Viewport vp = getViewport();
        renderMap(&vp);
        renderEntities(&vp);
        renderEffects(&vp);
        renderHUD();
        break;
    }

    case GamePhase::GameOver:
        apply_menu_theme_ = true;
        renderGameOver();
        apply_menu_theme_ = false;
        break;
    }
}

GameEngine::Viewport GameEngine::getViewport() const {
    Viewport vp;
    int req_w = Config::MAP_WIDTH * Config::TILE_RENDER_W;
    int req_h = Config::MAP_HEIGHT + 2;

    if (render_width_ >= req_w && render_height_ >= req_h) {
        vp.start_x      = 0;
        vp.start_y      = 0;
        vp.visible_cols = Config::MAP_WIDTH;
        vp.visible_rows = Config::MAP_HEIGHT;
        vp.base_row     = (render_height_ - Config::MAP_HEIGHT - 2) / 2 + 1;
        vp.base_col     = (render_width_ - req_w) / 2;
        vp.is_scrolling = false;
    } else {
        vp.is_scrolling = true;
        vp.visible_rows = std::min(Config::MAP_HEIGHT, std::max(1, render_height_ - 2));
        vp.visible_cols = std::min(Config::MAP_WIDTH, std::max(1, render_width_ / Config::TILE_RENDER_W));

        vp.start_y = pacman_.position.y - vp.visible_rows / 2;
        vp.start_x = pacman_.position.x - vp.visible_cols / 2;

        if (vp.start_y + vp.visible_rows > Config::MAP_HEIGHT) {
            vp.start_y = Config::MAP_HEIGHT - vp.visible_rows;
        }
        if (vp.start_y < 0) {
            vp.start_y = 0;
        }

        if (vp.start_x + vp.visible_cols > Config::MAP_WIDTH) {
            vp.start_x = Config::MAP_WIDTH - vp.visible_cols;
        }
        if (vp.start_x < 0) {
            vp.start_x = 0;
        }

        vp.base_row = std::max(0, (render_height_ - vp.visible_rows) / 2);
        vp.base_col = std::max(0, (render_width_ - vp.visible_cols * Config::TILE_RENDER_W) / 2);
    }
    return vp;
}

void GameEngine::renderMap(const Viewport* vpp) {
    const Viewport vp = vpp ? *vpp : getViewport();

    for (int vy = 0; vy < vp.visible_rows; ++vy) {
        int y = vp.start_y + vy;
        if (y < 0 || y >= Config::MAP_HEIGHT)
            continue;

        for (int vx = 0; vx < vp.visible_cols; ++vx) {
            int x = vp.start_x + vx;
            if (x < 0 || x >= Config::MAP_WIDTH)
                continue;

            TileType t     = map_.getTile(x, y);
            int screen_row = vp.base_row + vy;
            int screen_col = vp.base_col + vx * Config::TILE_RENDER_W;

            Cell c1, c2;
            c1.bg = {0, 0, 0};
            c2.bg = {0, 0, 0};

            if (t == TileType::Wall) {
                uint8_t mask   = map_.wallNeighborMask({x, y});
                std::string g1 = " ";
                std::string g2 = " ";
                if (use_nerd_fonts_) {
                    switch (mask) {
                    case 0:
                        g1 = " ";
                        g2 = " ";
                        break;
                    case 1:
                        g1 = "┃";
                        g2 = " ";
                        break;
                    case 2:
                        g1 = "━";
                        g2 = "━";
                        break;
                    case 3:
                        g1 = "┗";
                        g2 = "━";
                        break;
                    case 4:
                        g1 = "┃";
                        g2 = " ";
                        break;
                    case 5:
                        g1 = "┃";
                        g2 = " ";
                        break;
                    case 6:
                        g1 = "┏";
                        g2 = "━";
                        break;
                    case 7:
                        g1 = "┣";
                        g2 = "━";
                        break;
                    case 8:
                        g1 = "━";
                        g2 = " ";
                        break;
                    case 9:
                        g1 = "┛";
                        g2 = " ";
                        break;
                    case 10:
                        g1 = "━";
                        g2 = "━";
                        break;
                    case 11:
                        g1 = "┻";
                        g2 = "━";
                        break;
                    case 12:
                        g1 = "┓";
                        g2 = " ";
                        break;
                    case 13:
                        g1 = "┫";
                        g2 = " ";
                        break;
                    case 14:
                        g1 = "┳";
                        g2 = "━";
                        break;
                    case 15:
                        g1 = "╋";
                        g2 = "━";
                        break;
                    }
                } else {
                    g1 = "#";
                    g2 = "#";
                }
                c1.glyph = g1;
                c2.glyph = g2;
                if (isGlitchZone(x, y)) {
                    c1.glyph = kGlitchWallGlyphs[(current_time_ms_ / 100 + x + y) % 6];
                    c2.glyph = " ";
                }

                Color wall_color;
                if (isGlitchZone(x, y)) {
                    wall_color = glitchRGB(current_time_ms_);
                } else {
                    double ratio = static_cast<double>(y) / (Config::MAP_HEIGHT - 1);
                    uint8_t wg   = static_cast<uint8_t>(180 - ratio * 140);
                    uint8_t wb   = static_cast<uint8_t>(255 - ratio * 75);
                    if (level_ >= 1 && level_ <= 4) {
                        wall_color = Color{0, wg, wb};
                    } else if (level_ >= 5 && level_ <= 8) {
                        wall_color = Color{0, wb, wg};
                    } else if (level_ >= 9 && level_ <= 12) {
                        wall_color = Color{0, wb, static_cast<uint8_t>(wg / 2)};
                    } else if (level_ >= 13 && level_ <= 16) {
                        wall_color = Color{wb, 50, static_cast<uint8_t>(wg + 50)};
                    } else if (level_ >= 17 && level_ <= 19) {
                        wall_color = Color{wb, 40, 40};
                    } else if (level_ >= 21 && level_ <= 23) {
                        wall_color = Color{wg, 0, wb};
                    } else if (level_ >= 24 && level_ <= 26) {
                        wall_color = Color{0, static_cast<uint8_t>(235 - wg), 245};
                    } else if (level_ >= 27 && level_ <= 29) {
                        wall_color = Color{wb, wg, 20};
                    } else {
                        wall_color = Color{wb, wb, 0};
                    }
                }

                c1.fg = wall_color;
                c2.fg = wall_color;
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::Dot) {
                c1.glyph = use_nerd_fonts_ ? "·" : ".";
                c2.glyph = " ";
                c1.fg    = tileAccentColor(x, y);
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::PowerPellet) {
                c1.glyph = use_nerd_fonts_ ? "●" : "O";
                c2.glyph = " ";
                c1.fg    = tileAccentColor(x, y);
                c1.blink = true;
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::GhostDoor) {
                c1.glyph = use_nerd_fonts_ ? "━" : "-";
                c2.glyph = use_nerd_fonts_ ? "━" : "-";
                c1.fg    = {255, 183, 222};
                c2.fg    = {255, 183, 222};
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            } else if (t == TileType::Cherry) {
                setTileGlyph(screen_row, screen_col, use_nerd_fonts_ ? "󰄯" : "c", Color{255, 0, 0});
            } else if (t == TileType::GoldenApple) {
                setTileGlyph(screen_row, screen_col, use_nerd_fonts_ ? "" : "a", Color{255, 215, 0});
            } else if (t == TileType::Heart) {
                setTileGlyph(screen_row, screen_col, use_nerd_fonts_ ? "♥" : "h", Color{255, 105, 180});
            } else if (t >= TileType::LetterP && t <= TileType::LetterM) {
                int letter_idx = std::to_underlying(t) - std::to_underlying(TileType::LetterP);
                c1.glyph       = std::string(1, "PACTERM"[letter_idx]);
                c2.glyph       = " ";
                c1.fg          = Color{255, 215, 0};
                c1.blink       = true;
                setCell(screen_row, screen_col, c1);
                setCell(screen_row, screen_col + 1, c2);
            }
        }
    }
}

void GameEngine::renderEntities(const Viewport* vpp) {
    const Viewport vp = vpp ? *vpp : getViewport();

    if (pacman_.isAlive()) {
        int vx = pacman_.position.x - vp.start_x;
        int vy = pacman_.position.y - vp.start_y;

        if (vx >= 0 && vx < vp.visible_cols && vy >= 0 && vy < vp.visible_rows) {
            Cell c1, c2;
            c1.bg       = {0, 0, 0};
            c2.bg       = {0, 0, 0};
            Color pm_fg = pacManColor(pacman_.position.y * 0.08 + pacman_.position.x * 0.04);
            if (isGlitchZone(pacman_.position.x, pacman_.position.y)) {
                pm_fg = glitchRGB(current_time_ms_);
            }
            c1.fg = pm_fg;
            c2.fg = pm_fg;

            if (isGlitchZone(pacman_.position.x, pacman_.position.y)) {
                const auto& glitch_glyphs = use_nerd_fonts_ ? kGlitchBlockGlyphs : kGlitchAsciiGlyphs;
                c1.glyph                  = glitch_glyphs[(current_time_ms_ / 100) % 4];
                c2.glyph                  = " ";
            } else if (pacman_.animFrame() == 0 || pacman_.animFrame() == 2) {
                if (use_nerd_fonts_) {
                    switch (pacman_.currentDirection) {
                    case Direction::Left: c1.glyph = "Ɔ"; break;
                    case Direction::Right: c1.glyph = "C"; break;
                    case Direction::Down: c1.glyph = "∩"; break;
                    case Direction::Up: c1.glyph = "∪"; break;
                    default: c1.glyph = "Ɔ"; break;
                    }
                } else {
                    switch (pacman_.currentDirection) {
                    case Direction::Left: c1.glyph = ">"; break;
                    case Direction::Right: c1.glyph = "<"; break;
                    case Direction::Down: c1.glyph = "^"; break;
                    case Direction::Up: c1.glyph = "v"; break;
                    default: c1.glyph = ">"; break;
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
            c1.bg       = {0, 0, 0};
            c2.bg       = {0, 0, 0};
            Color pm_fg = pacManColor(pacman_.position.y * 0.08 + pacman_.position.x * 0.04);
            if (isGlitchZone(pacman_.position.x, pacman_.position.y)) {
                pm_fg = glitchRGB(current_time_ms_);
            }
            c1.fg = pm_fg;
            c2.fg = pm_fg;

            int step = (Config::DEATH_ANIM_DURATION - phase_timer_ms_) / 300;
            if (use_nerd_fonts_) {
                switch (step) {
                case 0: c1.glyph = "◠"; break;
                case 1: c1.glyph = "◡"; break;
                case 2: c1.glyph = "○"; break;
                case 3: c1.glyph = "·"; break;
                default: c1.glyph = " "; break;
                }
            } else {
                switch (step) {
                case 0: c1.glyph = "o"; break;
                case 1: c1.glyph = "x"; break;
                case 2: c1.glyph = "*"; break;
                case 3: c1.glyph = "."; break;
                default: c1.glyph = " "; break;
                }
            }
            c2.glyph = " ";
            int r    = vp.base_row + vy;
            int c    = vp.base_col + vx * Config::TILE_RENDER_W;
            setCell(r, c, c1);
            setCell(r, c + 1, c2);
        }
    }

    for (size_t i = 0; i < ghosts_.size(); ++i) {
        const auto& g = ghosts_[i];
        if (phase_ == GamePhase::PacDying)
            continue;
        if (level_ == 20 && i > 0)
            continue;
        if (level_ == 30 && i > 0)
            continue;

        int vx = g.position.x - vp.start_x;
        int vy = g.position.y - vp.start_y;

        if (isGlitchZone(g.position.x, g.position.y)) {
            const auto& glitch_glyphs = use_nerd_fonts_ ? kGlitchBlockGlyphs : kGlitchAsciiGlyphs;

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

                        c1.fg = glitchRGB(current_time_ms_);
                        c2.fg = glitchRGB(current_time_ms_);

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
                c1.glyph = use_nerd_fonts_ ? "󰊠" : "M";
                c2.glyph = " ";
                if (g.isFrightenedFlashing() && ((current_time_ms_ / 200) % 2 == 0)) {
                    c1.fg = {255, 255, 255};
                } else {
                    c1.fg = {33, 33, 255};
                }
            } else if (g.mode == GhostMode::Eaten) {
                c1.glyph = use_nerd_fonts_ ? "󰈈" : "\"";
                c2.glyph = " ";
                c1.fg    = {255, 255, 255};
            } else {
                c1.glyph = use_nerd_fonts_ ? "󰊠" : "M";
                c2.glyph = " ";
                switch (g.personality) {
                case GhostPersonality::Blinky: c1.fg = {255, 0, 0}; break;
                case GhostPersonality::Pinky: c1.fg = {255, 184, 255}; break;
                case GhostPersonality::Inky: c1.fg = {0, 255, 255}; break;
                case GhostPersonality::Clyde: c1.fg = {255, 184, 82}; break;
                }
            }
            setCell(r, c, c1);
            setCell(r, c + 1, c2);
        }
    }

    if (bonus_fruit_active_) {
        int vx = bonus_fruit_pos_.x - vp.start_x;
        int vy = bonus_fruit_pos_.y - vp.start_y;

        if (vx >= 0 && vx < vp.visible_cols && vy >= 0 && vy < vp.visible_rows) {
            bool blink = (bonus_fruit_timer_ms_ < 3000 && (bonus_fruit_timer_ms_ / 200) % 2 == 1);
            if (!blink) {
                std::string glyph = "c";
                Color fg          = {255, 60, 60};
                switch (bonus_fruit_type_) {
                case TileType::Cherry:
                    glyph = use_nerd_fonts_ ? "󰄯" : "c";
                    fg    = {255, 60, 60};
                    break;
                case TileType::Strawberry:
                    glyph = use_nerd_fonts_ ? "󰄯" : "s";
                    fg    = {255, 105, 180};
                    break;
                case TileType::Orange:
                    glyph = use_nerd_fonts_ ? "" : "o";
                    fg    = {255, 165, 0};
                    break;
                case TileType::Apple:
                    glyph = use_nerd_fonts_ ? "" : "a";
                    fg    = {100, 255, 100};
                    break;
                case TileType::Melon:
                    glyph = use_nerd_fonts_ ? "󱁕" : "m";
                    fg    = {150, 255, 150};
                    break;
                case TileType::Galaxian:
                    glyph = use_nerd_fonts_ ? "󰛡" : "g";
                    fg    = {0, 220, 255};
                    break;
                case TileType::Bell:
                    glyph = use_nerd_fonts_ ? "󰂚" : "b";
                    fg    = {255, 215, 0};
                    break;
                case TileType::Key:
                    glyph = use_nerd_fonts_ ? "󰌌" : "k";
                    fg    = {255, 230, 100};
                    break;
                default: break;
                }
                int r = vp.base_row + vy;
                int c = vp.base_col + vx * Config::TILE_RENDER_W;
                setTileGlyph(r, c, glyph, fg);
            }
        }
    }
}

void GameEngine::renderHUD() {
    std::string score_str = username_.empty() ? I18n::format("hud.score_anon", score_) : I18n::format("hud.score", username_, score_);
    if (muted_) {
        score_str += std::string(I18n::t("hud.muted"));
    }
    std::string high_str = I18n::format("hud.high", high_score_);
    int center_col       = render_width_ / 2;

    Color hud_fg = {255, 255, 255};
    Color hud_bg = {0, 0, 0};
    if (fever_active_ && fever_timer_ms_ > 0) {
        double pulse = 0.5 + 0.5 * std::sin(current_time_ms_ / 120.0);
        uint8_t m    = static_cast<uint8_t>(35 + 60 * pulse);
        hud_bg       = {m, 0, m};
    }

    const int t = selected_general_theme_;
    fillRow(0, hud_fg, hud_bg);
    drawGradientString(0, 2, score_str, themePrimary(t, 0.0), themePrimary(t, 1.6), hud_bg);
    drawGradientString(0, render_width_ - static_cast<int>(displayWidth(high_str)) - 2, high_str, themeAccent(t, 0.0), themeAccent(t, 1.6), hud_bg);

    std::string powerup_str = "";
    Color powerup_color     = {255, 255, 255};
    if (fever_active_ && fever_timer_ms_ > 0) {
        int sec       = (fever_timer_ms_ + 999) / 1000;
        powerup_str   = (use_nerd_fonts_ ? "󰈸 " : "") + I18n::format("hud.fever", sec);
        powerup_color = {255, 0, 255};
    } else if (letter_score_mult_timer_ms_ > 0) {
        int sec       = (letter_score_mult_timer_ms_ + 999) / 1000;
        powerup_str   = (use_nerd_fonts_ ? "★ " : "* ") + I18n::format("hud.x2_score", sec);
        powerup_color = {255, 215, 0};
    } else if (speed_boost_timer_ms_ > 0 || pac_speed_timer_ms_ > 0) {
        int sec       = (std::max(speed_boost_timer_ms_, pac_speed_timer_ms_) + 999) / 1000;
        powerup_str   = (use_nerd_fonts_ ? "󱐋 " : ">> ") + I18n::format("hud.speed", sec);
        powerup_color = {255, 215, 0};
    } else if (ice_freeze_timer_ms_ > 0 || ghost_freeze_timer_ms_ > 0) {
        int sec       = (std::max(ice_freeze_timer_ms_, ghost_freeze_timer_ms_) + 999) / 1000;
        powerup_str   = (use_nerd_fonts_ ? "󰜗 " : "|| ") + I18n::format("hud.freeze", sec);
        powerup_color = {100, 255, 255};
    } else if (fruit_magnet_timer_ms_ > 0) {
        int sec       = (fruit_magnet_timer_ms_ + 999) / 1000;
        powerup_str   = (use_nerd_fonts_ ? "󰛡 " : "U ") + I18n::format("hud.magnet", sec);
        powerup_color = {255, 165, 0};
    } else if (fruit_shield_active_) {
        powerup_str   = (use_nerd_fonts_ ? "󰞍 " : "[") + std::string(I18n::t("hud.shield")) + (use_nerd_fonts_ ? "" : "]");
        powerup_color = {100, 255, 100};
    } else if (fruit_double_bounty_timer_ms_ > 0) {
        int sec       = (fruit_double_bounty_timer_ms_ + 999) / 1000;
        powerup_str   = (use_nerd_fonts_ ? "󰄯 " : "$ ") + I18n::format("hud.bounty", sec);
        powerup_color = {150, 255, 150};
    }
    if (!powerup_str.empty()) {
        const int w       = static_cast<int>(displayWidth(powerup_str));
        int start_col     = center_col - w / 2;
        const int max_col = render_width_ - static_cast<int>(displayWidth(high_str)) - 2 - w;
        if (start_col > max_col)
            start_col = max_col;
        if (start_col < 0)
            start_col = 0;
        drawString(0, start_col, powerup_str, powerup_color, hud_bg);
    }

    std::string lives_str = std::string(I18n::t("hud.lives"));
    for (int i = 0; i < lives_; ++i) {
        lives_str += use_nerd_fonts_ ? "♥ " : "<3";
    }
    std::string level_str = I18n::format("hud.level", level_);

    fillRow(render_height_ - 1, hud_fg, hud_bg);
    drawGradientString(render_height_ - 1, 2, lives_str, themeAccent(t, 0.0), themeAccent(t, 1.4), hud_bg);
    drawGradientString(render_height_ - 1, render_width_ - static_cast<int>(displayWidth(level_str)) - 2, level_str, themePrimary(t, 0.0), themePrimary(t, 1.2),
                       hud_bg);
}

void GameEngine::renderMainMenu() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    const double PI_CONST = 3.14159265358979323846;
    double phase          = current_time_ms_ / 5000.0 * 2.0 * PI_CONST;

    auto getPrimaryGradient = [this, phase](double offset) -> Color {
        if (selected_general_theme_ == 0) {
            double g = 195.0 + std::sin(phase + offset) * 60.0;
            return {255, static_cast<uint8_t>(g), 0};
        }
        return themePrimary(selected_general_theme_, offset);
    };

    auto getAccentGradient = [this](double offset) -> Color { return themeAccent(selected_general_theme_, offset); };

    if (render_width_ >= 66 && render_height_ >= 22) {
        if (use_nerd_fonts_) {
            drawString(center_row - 10, center_col - 31, "  ██████╗  █████╗  ██████╗████████╗███████╗██████╗ ███╗   ███╗", getPrimaryGradient(0.0 * 0.3));
            drawString(center_row - 9, center_col - 31, "  ██╔══██╗██╔══██╗██╔════╝╚══██╔══╝██╔════╝██╔══██╗████╗ ████║", getPrimaryGradient(1.0 * 0.3));
            drawString(center_row - 8, center_col - 31, "  ██████╔╝███████║██║        ██║   █████╗  ██████╔╝██╔████╔██║", getPrimaryGradient(2.0 * 0.3));
            drawString(center_row - 7, center_col - 31, "  ██╔═══╝ ██╔══██║██║        ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║", getPrimaryGradient(3.0 * 0.3));
            drawString(center_row - 6, center_col - 31, "  ██║     ██║  ██║╚██████╗   ██║   ███████╗██║  ██║██║ ╚═╝ ██║", getPrimaryGradient(4.0 * 0.3));
            drawString(center_row - 5, center_col - 31, "  ╚═╝     ╚═╝  ╚═╝ ╚═════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝", getPrimaryGradient(5.0 * 0.3));
        } else {
            drawString(center_row - 10, center_col - 24, "#####    ###    ###   #####  #####  ####   #   #", getPrimaryGradient(0.0 * 0.3));
            drawString(center_row - 9, center_col - 24, "#    #  #   #  #   #    #    #      #   #  ## ##", getPrimaryGradient(1.0 * 0.3));
            drawString(center_row - 8, center_col - 24, "#####   #####  #        #    ####   ####   # # #", getPrimaryGradient(2.0 * 0.3));
            drawString(center_row - 7, center_col - 24, "#       #   #  #   #    #    #      # #    #   #", getPrimaryGradient(3.0 * 0.3));
            drawString(center_row - 6, center_col - 24, "#       #   #   ###     #    #####  #  ##  #   #", getPrimaryGradient(4.0 * 0.3));
            drawString(center_row - 5, center_col - 24, "                                                ", getPrimaryGradient(5.0 * 0.3));
        }

        menu_accent_           = true;
        Color border_color_top = getAccentGradient(6.0 * 0.3);
        std::string dash_ch    = use_nerd_fonts_ ? "═" : "-";
        std::string mid_line;
        for (int i = 0; i < 58; ++i) {
            mid_line += dash_ch;
        }

        drawBox(center_row - 4, center_col - 30, 60, 14, {0, 0, 0}, {0, 0, 0});

        std::string tl = use_nerd_fonts_ ? "╔" : "+";
        std::string tr = use_nerd_fonts_ ? "╗" : "+";
        std::string vt = use_nerd_fonts_ ? "║" : "|";
        std::string bl = use_nerd_fonts_ ? "╚" : "+";
        std::string br = use_nerd_fonts_ ? "╝" : "+";

        std::string ver       = std::string("v") + Config::PACTERM_VERSION;
        std::string ver_block = dash_ch + ver + dash_ch;
        int vb_glyphs         = 0;
        for (size_t j = 0; j < ver_block.size();) {
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
            double side_offset;
            if (selected_general_theme_ == 0) {
                side_offset = (6.0 + (r - (center_row - 3)) * 0.5) * 0.3;
            } else {
                side_offset = 1.2;
            }
            Color side_color = getAccentGradient(side_offset);
            drawString(r, center_col - 30, vt, side_color);
            drawString(r, center_col + 29, vt, side_color);
        }
        Color border_color_bottom = getAccentGradient((6.0 + 12 * 0.5) * 0.3);
        drawString(center_row + 9, center_col - 30, bl + mid_line + br, border_color_bottom);
        menu_accent_ = false;

        std::string install_text = isInstalledLocally() ? std::string(I18n::t("main_menu.uninstall")) : std::string(I18n::t("main_menu.install"));
        std::string user_text    = std::string(I18n::t("main_menu.username")) + ":       " + username_;

        std::array<std::string, 7> main_options = {std::string(I18n::t("main_menu.start")),  user_text,    std::string(I18n::t("main_menu.stats")),
                                                   std::string(I18n::t("main_menu.redeem")), install_text, std::string(I18n::t("main_menu.settings")),
                                                   std::string(I18n::t("main_menu.quit"))};

        size_t max_w = 0;
        for (const auto& o : main_options) {
            max_w = std::max(max_w, glyphCount(o));
        }
        max_w += 2;
        int block_left = center_col - static_cast<int>(max_w / 2);

        for (int i = 0; i < 7; ++i) {
            Color fg           = {220, 220, 220};
            std::string prefix = "  ";
            bool bold          = false;
            bool is_selected   = (i == main_menu_selection_);
            bool is_hovered    = isMouseHovering(center_row - 2 + i, block_left, "  " + main_options[i]);

            if (is_selected) {
                if (click_feedback_timer_ms_ > 0) {
                    fg   = {255, 255, 255};
                    bold = true;
                } else {
                    double sel_phase = phase * 2.0;
                    double r         = 255.0;
                    double g         = 180.0 + std::sin(sel_phase) * 75.0;
                    fg               = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), 0};
                }
                prefix = "> ";
            } else if (is_hovered) {
                fg     = {0, 255, 255};
                prefix = "> ";
                bold   = true;
            }

            if (is_selected && is_hovered) {
                fg   = {255, 255, 120};
                bold = true;
            }
            drawString(center_row - 2 + i, block_left, prefix + main_options[i], fg, {0, 0, 0}, bold);
        }

        if (!main_menu_message_.empty()) {
            drawString(center_row + 5, center_col - static_cast<int>(main_menu_message_.length()) / 2, main_menu_message_, {0, 255, 0});
        }

        Color label_color = {0, 255, 255};
        Color val_color   = {255, 255, 255};

        drawString(center_row + 6, center_col - 18, "Developed by : ", label_color);
        drawString(center_row + 6, center_col - 3, "Wael Amrani Zerrifi", val_color);

        drawString(center_row + 7, center_col - 18, "Website      : ", label_color);
        drawString(center_row + 7, center_col - 3, "https://wael.work.gd/pacterm", val_color);

        drawString(center_row + 8, center_col - 18, "License      : ", label_color);
        drawString(center_row + 8, center_col - 3, "GPL-3.0-or-later", val_color);
    } else {
        Color title_color = getPrimaryGradient(0.0);
        drawString(center_row - 4, center_col - 10, "  pacterm  ", title_color);
        drawString(center_row - 4, center_col + 6, "[v" + std::string(Config::PACTERM_VERSION) + "]", {100, 100, 100});

        std::string install_text = isInstalledLocally() ? std::string(I18n::t("main_menu.uninstall")) : std::string(I18n::t("main_menu.install"));
        std::string user_text    = std::string(I18n::t("main_menu.username")) + ": " + username_;

        std::array<std::string, 7> main_options = {std::string(I18n::t("main_menu.start")),  user_text,    std::string(I18n::t("main_menu.stats")),
                                                   std::string(I18n::t("main_menu.redeem")), install_text, std::string(I18n::t("main_menu.settings")),
                                                   std::string(I18n::t("main_menu.quit"))};

        size_t max_w = 0;
        for (const auto& o : main_options) {
            max_w = std::max(max_w, glyphCount(o));
        }
        max_w += 2;
        int block_left = center_col - static_cast<int>(max_w / 2);

        for (int i = 0; i < 7; ++i) {
            Color fg           = {220, 220, 220};
            std::string prefix = "  ";
            bool bold          = false;
            bool is_selected   = (i == main_menu_selection_);
            bool is_hovered    = isMouseHovering(center_row - 2 + i, block_left, "  " + main_options[i]);

            if (is_selected) {
                if (click_feedback_timer_ms_ > 0) {
                    fg   = {255, 255, 255};
                    bold = true;
                } else {
                    double sel_phase = phase * 2.0;
                    double r         = 255.0;
                    double g         = 180.0 + std::sin(sel_phase) * 75.0;
                    fg               = {static_cast<uint8_t>(r), static_cast<uint8_t>(g), 0};
                }
                prefix = "> ";
            } else if (is_hovered) {
                fg     = {0, 255, 255};
                prefix = "> ";
                bold   = true;
            }

            if (is_selected && is_hovered) {
                fg   = {255, 255, 120};
                bold = true;
            }
            drawString(center_row - 2 + i, block_left, prefix + main_options[i], fg, {0, 0, 0}, bold);
        }

        if (!main_menu_message_.empty()) {
            drawString(center_row + 6, center_col - static_cast<int>(main_menu_message_.length()) / 2, main_menu_message_, {0, 255, 0});
        }
    }
}

void GameEngine::renderSettings() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    const std::array<std::string_view, Config::THEME_COUNT>& theme_names = kThemeNames;

    struct SettingItem {
        std::string label;
        std::string value;
        bool is_kv = true;
    };

    const std::array<SettingItem, 8> items = {
        {{std::string(I18n::t("settings.general_theme")), std::string(theme_names[selected_general_theme_]), true},
         {std::string(I18n::t("settings.language")), std::string(I18n::getCurrentLanguageName()), true},
         {std::string(I18n::t("settings.nerd_fonts")), std::string(use_nerd_fonts_ ? I18n::t("settings.on") : I18n::t("settings.off")), true},
         {std::string(I18n::t("settings.sound")), std::string(muted_ ? I18n::t("settings.off") : I18n::t("settings.on")), true},
         {std::string(I18n::t("settings.pacman_theme")), std::string(theme_names[selected_pacman_color_]), true},
         {std::string(I18n::t("settings.key_config")), "", false},
         {std::string(I18n::t("settings.reset")), "", false},
         {std::string(I18n::t("settings.back")), "", false}}};

    size_t max_label_w = 15;
    for (size_t i = 0; i < 5; ++i) {
        max_label_w = std::max(max_label_w, displayWidth(items[i].label));
    }

    std::array<std::string, 8> options;
    for (size_t i = 0; i < 8; ++i) {
        if (items[i].is_kv) {
            size_t lw  = displayWidth(items[i].label);
            size_t pad = (max_label_w > lw) ? (max_label_w - lw) : 0;
            options[i] = items[i].label + ":" + std::string(pad + 2, ' ') + items[i].value;
        } else {
            options[i] = items[i].label;
        }
    }

    std::string title_str = " " + std::string(I18n::t("settings.title")) + " ";
    int title_x           = center_col - static_cast<int>(displayWidth(title_str)) / 2;

    if (render_width_ >= 66 && render_height_ >= 26) {
        drawDoubleBorderBox(center_row - 6, center_col - 22, 44, 12, {0, 255, 255}, {0, 0, 0});
        drawString(center_row - 6, title_x, title_str, {255, 255, 0});

        int block_left = center_col - 19;

        for (int i = 0; i < 8; ++i) {
            Color fg           = {220, 220, 220};
            std::string prefix = "  ";
            bool bold          = false;
            bool is_selected   = (i == settings_selection_);
            bool is_hovered    = isMouseHovering(center_row - 4 + i, block_left, "  " + options[i]);

            if (is_selected) {
                fg     = {255, 200, 0};
                prefix = "> ";
            } else if (is_hovered) {
                fg     = {0, 255, 255};
                prefix = "> ";
                bold   = true;
            }

            if (is_selected && is_hovered) {
                fg   = {255, 255, 120};
                bold = true;
            }

            std::string draw_text = prefix + options[i];
            drawString(center_row - 4 + i, block_left, truncateText(draw_text, 38), fg, {0, 0, 0}, bold);
        }

        if (!main_menu_message_.empty()) {
            drawString(center_row + 4, center_col - static_cast<int>(main_menu_message_.length()) / 2, main_menu_message_, {0, 255, 0});
        }
    } else {
        drawString(center_row - 7, title_x, title_str, {255, 255, 0});

        int block_left = std::max(2, center_col - 19);

        for (int i = 0; i < 8; ++i) {
            Color fg           = {220, 220, 220};
            std::string prefix = "  ";
            bool bold          = false;
            bool is_selected   = (i == settings_selection_);
            bool is_hovered    = isMouseHovering(center_row - 4 + i, block_left, "  " + options[i]);

            if (is_selected) {
                fg     = {255, 200, 0};
                prefix = "> ";
            } else if (is_hovered) {
                fg     = {0, 255, 255};
                prefix = "> ";
                bold   = true;
            }

            if (is_selected && is_hovered) {
                fg   = {255, 255, 120};
                bold = true;
            }
            drawString(center_row - 4 + i, block_left, truncateText(prefix + options[i], render_width_ - 4), fg, {0, 0, 0}, bold);
        }

        if (!main_menu_message_.empty()) {
            drawString(center_row + 4, center_col - static_cast<int>(main_menu_message_.length()) / 2, main_menu_message_, {0, 255, 0});
        }
    }
}

void GameEngine::activateSettingsSelection() {
    switch (settings_selection_) {
    case 0: {
        int temp = selected_general_theme_;
        do {
            temp = (temp + 1) % Config::THEME_COUNT;
        } while (isColorLocked(temp));
        selected_general_theme_ = temp;
        saveHighScore();
        break;
    }
    case 1:
        I18n::cycleLanguage(1);
        saveHighScore();
        break;
    case 2:
        use_nerd_fonts_ = !use_nerd_fonts_;
        saveHighScore();
        break;
    case 3:
        muted_ = !muted_;
        saveHighScore();
        break;
    case 4: {
        int temp = selected_pacman_color_;
        do {
            temp = (temp + 1) % Config::THEME_COUNT;
        } while (isColorLocked(temp));
        selected_pacman_color_ = temp;
        saveHighScore();
        break;
    }
    case 5:
        phase_                = GamePhase::KeyConfig;
        key_config_selection_ = 0;
        is_binding_           = false;
        break;
    case 6: {
        const char* home_env = std::getenv("HOME");
        if (home_env) {
            try {
                std::filesystem::remove_all(std::filesystem::path(home_env) / ".pacterm");
            } catch (...) {}
        }
        try {
            std::filesystem::remove(getCacheFilePath());
        } catch (...) {}
        high_score_     = 0;
        muted_          = false;
        use_nerd_fonts_ = true;
        if (isAzertyLayout()) {
            custom_key_up_    = 'z';
            custom_key_down_  = 's';
            custom_key_left_  = 'q';
            custom_key_right_ = 'd';
        } else {
            custom_key_up_    = 'w';
            custom_key_down_  = 's';
            custom_key_left_  = 'a';
            custom_key_right_ = 'd';
        }
        custom_key_pause_       = 'p';
        unlocked_rainbow_       = false;
        selected_general_theme_ = 0;
        selected_pacman_color_  = 0;
        letter_hunt_            = LetterHuntState{};
        pacterm_plus_unlocked_  = false;
        games_played_           = 0;
        dots_eaten_             = 0;
        ghosts_eaten_           = 0;
        deaths_                 = 0;
        power_pellets_          = 0;
        time_played_ms_         = 0;
        username_               = "Wael";
        const char* env_nf      = std::getenv("PACMAN_NERD_FONTS");
        if (env_nf && std::string(env_nf) == "0") {
            use_nerd_fonts_ = false;
        }
        I18n::initFromLocale();
        rebuildKeybindings();
        main_menu_message_      = std::string(I18n::t("settings.reset_success"));
        main_menu_msg_timer_ms_ = 3000;
        break;
    }
    case 7:
        phase_             = GamePhase::MainMenu;
        main_menu_message_ = "";
        fade_animation_.fadeIn({255, 255, 255}, 300);
        break;
    }
}

void GameEngine::renderRedeem() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    drawDoubleBorderBox(center_row - 3, center_col - 18, 36, 6, {255, 215, 0}, {0, 0, 0});
    std::string redeem_title = " " + std::string(I18n::t("redeem.title")) + " ";
    drawString(center_row - 3, center_col - static_cast<int>(displayWidth(redeem_title)) / 2, redeem_title, {255, 215, 0});
    drawString(center_row - 1, center_col - 16, "> " + redeem_input_ + "_", {255, 255, 255});
    if (!redeem_result_.empty()) {
        Color result_c = redeem_result_valid_ ? Color{0, 255, 0} : Color{255, 50, 50};
        drawString(center_row, center_col - static_cast<int>(displayWidth(redeem_result_)) / 2, redeem_result_, result_c);
    }
    std::string redeem_hint = std::string(I18n::t("redeem.hint"));
    drawString(center_row + 1, center_col - static_cast<int>(displayWidth(redeem_hint)) / 2, redeem_hint, {150, 150, 150});
}

void GameEngine::renderStats() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    int total_sec = time_played_ms_ / 1000;
    int hours     = total_sec / 3600;
    int minutes   = (total_sec % 3600) / 60;
    int seconds   = total_sec % 60;
    char time_buf[32];
    std::snprintf(time_buf, sizeof(time_buf), "%d:%02d:%02d", hours, minutes, seconds);

    if (render_width_ >= 66 && render_height_ >= 22) {
        drawDoubleBorderBox(center_row - 7, center_col - 26, 53, 17, {0, 255, 255}, {0, 0, 0});
        std::string title_str = " " + std::string(I18n::t("stats.title")) + " ";
        drawString(center_row - 7, center_col - static_cast<int>(displayWidth(title_str)) / 2, title_str, {255, 255, 0});

        std::string labels[8] = {std::string(I18n::t("stats.high_score")),    std::string(I18n::t("stats.games_played")),
                                 std::string(I18n::t("stats.ghosts_eaten")),  std::string(I18n::t("stats.deaths")),
                                 std::string(I18n::t("stats.best_level")),    std::string(I18n::t("stats.dots_eaten")),
                                 std::string(I18n::t("stats.power_pellets")), std::string(I18n::t("stats.time_played"))};
        std::string values[8] = {std::to_string(high_score_),         std::to_string(games_played_), std::to_string(ghosts_eaten_),  std::to_string(deaths_),
                                 std::to_string(max_unlocked_level_), std::to_string(dots_eaten_),   std::to_string(power_pellets_), time_buf};

        int label_rows[4] = {center_row - 5, center_row - 2, center_row + 1, center_row + 4};
        int col_left      = center_col - 22;
        int col_right     = center_col + 2;

        for (int i = 0; i < 4; ++i) {
            Color label_c = {180, 180, 200};
            Color val_c   = {255, 255, 255};
            int li        = i * 2;
            drawString(label_rows[i], col_left, labels[li], label_c, {0, 0, 0}, false);
            drawString(label_rows[i] + 1, col_left, values[li], val_c, {0, 0, 0}, true);
            int ri = li + 1;
            drawString(label_rows[i], col_right, labels[ri], label_c, {0, 0, 0}, false);
            drawString(label_rows[i] + 1, col_right, values[ri], val_c, {0, 0, 0}, true);
        }

        std::string letters_lbl = std::string(I18n::t("stats.letters")) + ": ";
        drawString(center_row + 7, center_col - 22, letters_lbl, {180, 180, 200}, {0, 0, 0}, false);
        int letters_col = center_col - 22 + static_cast<int>(glyphCount(letters_lbl));
        for (int i = 0; i < LetterHuntState::LETTER_COUNT; ++i) {
            Color lc = letter_hunt_.isCollected(i) ? Color{255, 215, 0} : Color{70, 70, 75};
            drawString(center_row + 7, letters_col + i, std::string(1, "PACTERM"[i]), lc, {0, 0, 0}, false);
        }
    } else {
        std::string list[6] = {
            std::string(I18n::t("stats.high_score")) + ": " + std::to_string(high_score_),
            std::string(I18n::t("stats.best_level")) + ": " + std::to_string(max_unlocked_level_),
            std::string(I18n::t("stats.games_played")) + ": " + std::to_string(games_played_),
            std::string(I18n::t("stats.dots_eaten")) + ": " + std::to_string(dots_eaten_),
            std::string(I18n::t("stats.ghosts_eaten")) + ": " + std::to_string(ghosts_eaten_),
            std::string(I18n::t("stats.power_pellets")) + ": " + std::to_string(power_pellets_),
        };
        for (int i = 0; i < 6; ++i) {
            drawString(center_row - 3 + i, center_col - 12, list[i], {180, 180, 200});
        }
        std::string letters_lbl = std::string(I18n::t("stats.letters")) + ": ";
        drawString(center_row + 3, center_col - 12, letters_lbl, {180, 180, 200}, {0, 0, 0}, false);
        int letters_col = center_col - 12 + static_cast<int>(glyphCount(letters_lbl));
        for (int i = 0; i < LetterHuntState::LETTER_COUNT; ++i) {
            Color lc = letter_hunt_.isCollected(i) ? Color{255, 215, 0} : Color{70, 70, 75};
            drawString(center_row + 3, letters_col + i, std::string(1, "PACTERM"[i]), lc, {0, 0, 0}, false);
        }
        drawString(center_row + 4, center_col - 12,
                   std::string(I18n::t("stats.deaths")) + ": " + std::to_string(deaths_) + "   " + std::string(I18n::t("stats.time_played")) + ": " +
                       std::to_string(total_sec) + "s",
                   {180, 180, 200});
    }
}

void GameEngine::renderGameOver() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    std::string go_text    = std::string(I18n::t("game.game_over"));
    std::string retry_text = std::string(I18n::t("game.press_continue"));
    int box_w              = std::max(static_cast<int>(displayWidth(retry_text)) + 4, 33);
    int box_h              = 7;

    drawDoubleBorderBox(center_row - 3, center_col - box_w / 2, box_w, box_h, {255, 0, 0}, {0, 0, 0});
    drawString(center_row - 1, center_col - static_cast<int>(displayWidth(go_text)) / 2, go_text, {255, 0, 0});
    drawString(center_row + 1, center_col - static_cast<int>(displayWidth(retry_text)) / 2, retry_text, {255, 255, 255});
}

void GameEngine::renderGetReady() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    std::string ready_text = std::string(I18n::t("game.ready"));
    int box_w              = std::max(static_cast<int>(displayWidth(ready_text)) + 6, 16);

    drawDoubleBorderBox(center_row - 2, center_col - box_w / 2, box_w, 5, {255, 255, 0}, {0, 0, 0});
    drawString(center_row, center_col - static_cast<int>(displayWidth(ready_text)) / 2, ready_text, {255, 255, 0});
}

void GameEngine::addScore(int points) {
    double mult = getActiveScoreMultiplier();
    score_ += static_cast<int>(static_cast<double>(points) * mult);
    if (score_ > high_score_) {
        high_score_ = score_;
        saveHighScore();
    }
}

void GameEngine::saveHighScore() {
    std::filesystem::path path;
    try {
        path = getCacheFilePath();
    } catch (...) {
        return;
    }
    if (path.empty())
        return;

    std::string key       = "PacTermWaelSecure2026";
    std::string lang_code = std::string(I18n::getCurrentLanguageCode());
    std::string plaintext = std::to_string(score_ > high_score_ ? score_ : high_score_) + " " + (muted_ ? "1" : "0") + " " + (use_nerd_fonts_ ? "1" : "0") +
                            " " + std::to_string(custom_key_up_) + " " + std::to_string(custom_key_down_) + " " + std::to_string(custom_key_left_) + " " +
                            std::to_string(custom_key_right_) + " " + std::to_string(custom_key_pause_) + " " + std::to_string(unlocked_rainbow_ ? 1 : 0) +
                            " " + std::to_string(max_unlocked_level_) + " " + std::to_string(selected_pacman_color_) + " " +
                            std::to_string(selected_general_theme_) + " " + std::to_string(games_played_) + " " + std::to_string(dots_eaten_) + " " +
                            std::to_string(ghosts_eaten_) + " " + std::to_string(deaths_) + " " + std::to_string(power_pellets_) + " " +
                            std::to_string(time_played_ms_) + " " + std::to_string(static_cast<int>(letter_hunt_.letter_mask)) + " " +
                            std::to_string(pacterm_plus_unlocked_ ? 1 : 0) + " " + lang_code + " " + username_;
    for (size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] ^= key[i % key.size()];
    }

    std::string hex_encoded;
    hex_encoded.reserve(plaintext.size() * 2);
    constexpr char hex_chars[] = "0123456789ABCDEF";
    for (unsigned char c : plaintext) {
        hex_encoded.push_back(hex_chars[c >> 4]);
        hex_encoded.push_back(hex_chars[c & 0x0F]);
    }

    try {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
            return;
        f.write(hex_encoded.data(), static_cast<std::streamsize>(hex_encoded.size()));
    } catch (...) {}
}

void GameEngine::loadHighScore() {
    std::filesystem::path path;
    try {
        path = getCacheFilePath();
    } catch (...) {
        path.clear();
    }
    if (path.empty()) {
        return;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open())
        return;

    const std::streamsize max_file = 8192;
    std::streamsize size           = f.tellg();
    if (size <= 0)
        return;
    if (size > max_file)
        size = max_file;
    f.seekg(0, std::ios::beg);

    std::string raw_content(static_cast<size_t>(size), '\0');
    if (!f.read(&raw_content[0], size))
        return;

    auto hexVal = [](char ch) noexcept -> int {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        return -1;
    };

    std::string ciphertext;
    bool is_hex = (raw_content.size() >= 2 && raw_content.size() % 2 == 0);
    for (char ch : raw_content) {
        if (hexVal(ch) < 0) {
            is_hex = false;
            break;
        }
    }

    if (is_hex && !raw_content.empty()) {
        ciphertext.reserve(raw_content.size() / 2);
        for (size_t i = 0; i + 1 < raw_content.size(); i += 2) {
            int hi = hexVal(raw_content[i]);
            int lo = hexVal(raw_content[i + 1]);
            ciphertext.push_back(static_cast<char>((hi << 4) | lo));
        }
    } else {
        ciphertext = raw_content;
    }

    std::string key = "PacTermWaelSecure2026";
    for (size_t i = 0; i < ciphertext.size(); ++i) {
        ciphertext[i] ^= key[i % key.size()];
    }

    int temp_muted = 0;
    int temp_nf    = 1;
    int k_up = 'w', k_down = 's', k_left = 'a', k_right = 'd', k_pause = 'p';
    int temp_unlocked         = 0;
    int temp_max_level        = 1;
    int temp_color            = 0;
    int temp_general_theme    = 0;
    int temp_games            = 0;
    int temp_dots             = 0;
    int temp_ghosts           = 0;
    int temp_deaths           = 0;
    int temp_power            = 0;
    int temp_time             = 0;
    int temp_letter_mask      = 0;
    int temp_pacterm_unlocked = 0;
    char lang_buf[32]         = "";
    char username_buf[128]    = "";

    int read_count =
        std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %31s %127[^\r\n]", &high_score_, &temp_muted, &temp_nf,
                    &k_up, &k_down, &k_left, &k_right, &k_pause, &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme, &temp_games, &temp_dots,
                    &temp_ghosts, &temp_deaths, &temp_power, &temp_time, &temp_letter_mask, &temp_pacterm_unlocked, lang_buf, username_buf);

    if (read_count >= 22) {
        I18n::setLanguageByCode(lang_buf);
    } else {
        temp_letter_mask      = 0;
        temp_pacterm_unlocked = 0;
        read_count =
            std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]", &high_score_, &temp_muted, &temp_nf,
                        &k_up, &k_down, &k_left, &k_right, &k_pause, &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme, &temp_games, &temp_dots,
                        &temp_ghosts, &temp_deaths, &temp_power, &temp_time, &temp_letter_mask, &temp_pacterm_unlocked, username_buf);
        if (read_count < 21) {
            read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]", &high_score_, &temp_muted,
                                     &temp_nf, &k_up, &k_down, &k_left, &k_right, &k_pause, &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme,
                                     &temp_games, &temp_dots, &temp_ghosts, &temp_deaths, &temp_power, &temp_time, username_buf);
        }
    }

    if (read_count < 19) {
        temp_ghosts = 0;
        temp_deaths = 0;
        temp_power  = 0;
        temp_time   = 0;
        read_count =
            std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %127[^\r\n]", &high_score_, &temp_muted, &temp_nf, &k_up, &k_down,
                        &k_left, &k_right, &k_pause, &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme, &temp_games, &temp_dots, username_buf);
    }
    if (read_count < 15) {
        temp_max_level     = 1;
        temp_color         = 0;
        temp_general_theme = 0;
        temp_games         = 0;
        temp_dots          = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %*d %d %d %d %127[^\r\n]", &high_score_, &temp_muted, &temp_nf, &k_up, &k_down,
                                 &k_left, &k_right, &k_pause, &temp_unlocked, &temp_max_level, &temp_color, &temp_general_theme, username_buf);
    }
    if (read_count < 14) {
        temp_max_level     = 1;
        temp_color         = 0;
        temp_general_theme = 0;
        temp_games         = 0;
        temp_dots          = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %*d %d %d %127[^\r\n]", &high_score_, &temp_muted, &temp_nf, &k_up, &k_down,
                                 &k_left, &k_right, &k_pause, &temp_unlocked, &temp_max_level, &temp_color, username_buf);
    }
    if (read_count < 13) {
        temp_max_level     = 1;
        temp_color         = 0;
        temp_general_theme = 0;
        temp_games         = 0;
        temp_dots          = 0;
        read_count = std::sscanf(ciphertext.c_str(), "%d %d %d %d %d %d %d %d %d %*d %127[^\r\n]", &high_score_, &temp_muted, &temp_nf, &k_up, &k_down, &k_left,
                                 &k_right, &k_pause, &temp_unlocked, username_buf);
    }

    if (read_count >= 3) {
        muted_          = (temp_muted != 0);
        use_nerd_fonts_ = (temp_nf != 0);
    }
    if (read_count >= 8) {
        custom_key_up_    = clampKeyCode(k_up);
        custom_key_down_  = clampKeyCode(k_down);
        custom_key_left_  = clampKeyCode(k_left);
        custom_key_right_ = clampKeyCode(k_right);
        custom_key_pause_ = clampKeyCode(k_pause);
    } else {
        if (isAzertyLayout()) {
            custom_key_up_    = 'z';
            custom_key_down_  = 's';
            custom_key_left_  = 'q';
            custom_key_right_ = 'd';
        } else {
            custom_key_up_    = 'w';
            custom_key_down_  = 's';
            custom_key_left_  = 'a';
            custom_key_right_ = 'd';
        }
        custom_key_pause_ = 'p';
    }
    if (read_count >= 10) {
        unlocked_rainbow_ = (temp_unlocked != 0);
    } else {
        unlocked_rainbow_ = false;
    }

    max_unlocked_level_      = temp_max_level;
    selected_pacman_color_   = temp_color;
    selected_general_theme_  = (read_count >= 14) ? temp_general_theme : 0;
    games_played_            = (read_count >= 15) ? temp_games : 0;
    dots_eaten_              = (read_count >= 15) ? temp_dots : 0;
    ghosts_eaten_            = (read_count >= 19) ? temp_ghosts : 0;
    deaths_                  = (read_count >= 19) ? temp_deaths : 0;
    power_pellets_           = (read_count >= 19) ? temp_power : 0;
    time_played_ms_          = (read_count >= 19) ? temp_time : 0;
    letter_hunt_.letter_mask = static_cast<uint8_t>(read_count >= 21 ? (temp_letter_mask & 0x7F) : 0);
    pacterm_plus_unlocked_   = (read_count >= 21) && (temp_pacterm_unlocked != 0);
    if (max_unlocked_level_ < 1 || max_unlocked_level_ > 30)
        max_unlocked_level_ = 1;
    if (selected_pacman_color_ < 0 || selected_pacman_color_ >= Config::THEME_COUNT)
        selected_pacman_color_ = 0;
    if (isColorLocked(selected_pacman_color_)) {
        selected_pacman_color_ = 0;
    }
    if (selected_general_theme_ < 0 || selected_general_theme_ >= Config::THEME_COUNT)
        selected_general_theme_ = 0;
    if (isColorLocked(selected_general_theme_)) {
        selected_general_theme_ = 0;
    }
    if (games_played_ < 0)
        games_played_ = 0;
    if (dots_eaten_ < 0)
        dots_eaten_ = 0;
    if (ghosts_eaten_ < 0)
        ghosts_eaten_ = 0;
    if (deaths_ < 0)
        deaths_ = 0;
    if (power_pellets_ < 0)
        power_pellets_ = 0;
    if (time_played_ms_ < 0)
        time_played_ms_ = 0;

    if (read_count >= 11) {
        std::string uname(username_buf);
        if (uname.size() > 15)
            uname = uname.substr(0, 15);
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
    FILE* pipe = popen("setxkbmap -query 2>/dev/null", "r");
    if (pipe) {
        char buffer[128];
        bool found = false;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            if (line.find("layout:") != std::string::npos) {
                if (line.find("fr") != std::string::npos || line.find("be") != std::string::npos || line.find("dz") != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }
        pclose(pipe);
        if (found)
            return true;
    }
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
    double p = current_time_ms_ / 5000.0 * 2.0 * 3.14159265358979323846;
    double h = p * 2.0 + offset;
    double r = std::sin(h) * 127.0 + 128.0;
    double g = std::sin(h + 2.0944) * 127.0 + 128.0;
    double b = std::sin(h + 4.1888) * 127.0 + 128.0;
    return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
}

Color GameEngine::themePrimary(int theme, double offset) const {
    const double PI_CONST = 3.14159265358979323846;
    const double phase    = current_time_ms_ / 5000.0 * 2.0 * PI_CONST;
    if (theme == 8)
        return getRainbowColor(offset);
    switch (theme) {
    case 0: return {255, 255, 0};
    case 1: {
        double g = 207.0 + std::sin(phase + offset) * 47.0;
        double b = 227.0 + std::cos(phase + offset) * 27.0;
        return {0, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    case 2: {
        double r = 40.0 + std::sin(phase + offset) * 40.0;
        double b = 90.0 + std::cos(phase + offset) * 70.0;
        return {static_cast<uint8_t>(r), 255, static_cast<uint8_t>(b)};
    }
    case 3: {
        double g = 100.0 + std::sin(phase + offset) * 80.0;
        double b = 235.0 + std::cos(phase + offset) * 20.0;
        return {255, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    case 4: return {255, static_cast<uint8_t>(70.0 + std::sin(phase + offset) * 50.0), static_cast<uint8_t>(60.0 + std::cos(phase + offset) * 50.0)};
    case 5: {
        double r = 160.0 + std::sin(phase + offset) * 60.0;
        double g = 90.0 + std::cos(phase + offset) * 50.0;
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), 255};
    }
    case 6: {
        double r = 120.0 + std::cos(phase + offset) * 40.0;
        double g = 225.0 + std::sin(phase + offset) * 30.0;
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), 255};
    }
    case 7: {
        double g = 175.0 + std::sin(phase + offset) * 60.0;
        double b = 50.0 + std::cos(phase + offset) * 40.0;
        return {255, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    case 9: return glitchRGB(current_time_ms_ + static_cast<uint64_t>(offset * 400.0));
    case 10: return {255, static_cast<uint8_t>(195.0 + std::sin(phase + offset) * 60.0), 0};
    default: return {255, 255, 255};
    }
}

Color GameEngine::themeAccent(int theme, double offset) const {
    const double PI_CONST = 3.14159265358979323846;
    const double phase    = current_time_ms_ / 5000.0 * 2.0 * PI_CONST;
    if (theme == 8)
        return getRainbowColor(offset);
    switch (theme) {
    case 0:
    case 1: {
        double g = 207.0 + std::sin(phase + offset) * 47.0;
        double b = 227.0 + std::cos(phase + offset) * 27.0;
        return {0, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    case 2: {
        double r = 40.0 + std::sin(phase + offset) * 40.0;
        double b = 90.0 + std::cos(phase + offset) * 70.0;
        return {static_cast<uint8_t>(r), 255, static_cast<uint8_t>(b)};
    }
    case 3: {
        double g = 100.0 + std::sin(phase + offset) * 80.0;
        double b = 235.0 + std::cos(phase + offset) * 20.0;
        return {255, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    case 4: return {255, static_cast<uint8_t>(70.0 + std::sin(phase + offset) * 50.0), static_cast<uint8_t>(60.0 + std::cos(phase + offset) * 50.0)};
    case 5: {
        double r = 160.0 + std::sin(phase + offset) * 60.0;
        double g = 90.0 + std::cos(phase + offset) * 50.0;
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), 255};
    }
    case 6: {
        double r = 120.0 + std::cos(phase + offset) * 40.0;
        double g = 225.0 + std::sin(phase + offset) * 30.0;
        return {static_cast<uint8_t>(r), static_cast<uint8_t>(g), 255};
    }
    case 7: {
        double g = 175.0 + std::sin(phase + offset) * 60.0;
        double b = 50.0 + std::cos(phase + offset) * 40.0;
        return {255, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
    }
    case 9: {
        const Color g = glitchRGB(current_time_ms_ + static_cast<uint64_t>(offset * 400.0));
        return {g.b, g.r, g.g};
    }
    case 10: return {0, static_cast<uint8_t>(207.0 + std::sin(phase + offset) * 47.0), static_cast<uint8_t>(227.0 + std::cos(phase + offset) * 27.0)};
    default: return {255, 255, 255};
    }
}

Color GameEngine::pacManColor(double offset) const {
    switch (selected_pacman_color_) {
    case 0: return {255, 255, 0};
    case 1: return {0, 255, 255};
    case 2: return {0, 255, 0};
    case 3: return {255, 100, 255};
    case 4: return {255, 50, 50};
    case 5: return {185, 100, 255};
    case 6: return {140, 230, 255};
    case 7: return {255, 190, 70};
    case 8: return getRainbowColor(offset);
    case 9: return glitchRGB(current_time_ms_);
    case 10: return themePrimary(Config::PACTERM_PLUS_THEME, offset);
    default: return themePrimary(Config::PACTERM_PLUS_THEME, offset);
    }
}

Color GameEngine::tileAccentColor(int x, int y) const {
    if (isGlitchZone(x, y)) {
        const Color g = glitchRGB(current_time_ms_);
        return {g.b, g.r, g.g};
    }
    if (level_ >= 1 && level_ <= 4)
        return {255, 183, 174};
    if (level_ >= 5 && level_ <= 8)
        return {174, 255, 255};
    if (level_ >= 9 && level_ <= 12)
        return {174, 255, 183};
    if (level_ >= 13 && level_ <= 16)
        return {255, 180, 220};
    if (level_ >= 17 && level_ <= 19)
        return {255, 150, 150};
    if (level_ >= 21 && level_ <= 23)
        return {215, 180, 255};
    if (level_ >= 24 && level_ <= 26)
        return {170, 235, 255};
    if (level_ >= 27 && level_ <= 29)
        return {255, 210, 120};
    return {255, 255, 150};
}

GameEngine::BrightnessTier GameEngine::getBrightnessTier(Color fg) const {
    // Perceived brightness approximated by the max component (cheap, no sqrt).
    const uint8_t mx = fg.r > fg.g ? (fg.r > fg.b ? fg.r : fg.b) : (fg.g > fg.b ? fg.g : fg.b);
    if (mx > 220)
        return BrightnessTier::VeryBright;
    if (mx > 170)
        return BrightnessTier::Bright;
    if (mx > 120)
        return BrightnessTier::Normal;
    if (mx > 60)
        return BrightnessTier::Dim;
    return BrightnessTier::VeryDim;
}

Color GameEngine::applyGeneralThemeImpl(Color fg, double offset) const {
    // Nearly black (borders, spacing): keep as-is.
    const uint8_t mx = fg.r > fg.g ? (fg.r > fg.b ? fg.r : fg.b) : (fg.g > fg.b ? fg.g : fg.b);
    if (mx <= 30)
        return fg;

    // In PacTerm+ theming, if a text is white or grey in Classic, it should be in PacTerm+ too.
    if (selected_general_theme_ == Config::PACTERM_PLUS_THEME) {
        int r = static_cast<int>(fg.r);
        int g = static_cast<int>(fg.g);
        int b = static_cast<int>(fg.b);
        if (std::abs(r - g) <= 25 && std::abs(g - b) <= 25 && std::abs(r - b) <= 25) {
            return fg;
        }
    }

    const BrightnessTier tier = getBrightnessTier(fg);

    const Color base = (selected_general_theme_ == 8)
                           ? getRainbowColor(offset)
                           : (menu_accent_ ? themeAccent(selected_general_theme_, offset) : themePrimary(selected_general_theme_, offset));

    // Split the animated theme color into a unit-intensity hue and its current
    // animated intensity, so we can re-drive it with the element's own brightness.
    const float base_max = static_cast<float>(base.r > base.g ? (base.r > base.b ? base.r : base.b) : (base.g > base.b ? base.g : base.b));
    if (base_max <= 0.0f)
        return fg;
    const float inv = 1.0f / base_max;
    const float hr  = static_cast<float>(base.r) * inv; // theme hue, unit intensity
    const float hg  = static_cast<float>(base.g) * inv;
    const float hb  = static_cast<float>(base.b) * inv;

    // Relative pulse of the theme right now (1.0 = peak). Dimmer tiers dampen
    // the swing so only prominent elements throb noticeably.
    const float pulse_rel  = base_max / 255.0f;
    const float pulse_keep = tier == BrightnessTier::VeryBright ? 1.0f
                             : tier == BrightnessTier::Bright   ? 0.7f
                             : tier == BrightnessTier::Normal   ? 0.5f
                             : tier == BrightnessTier::Dim      ? 0.3f
                                                                : 0.2f;
    const float damped     = pulse_keep * pulse_rel + (1.0f - pulse_keep) * 1.0f;

    // Mix the element's original color toward the pure theme hue driven at the
    // element's own brightness mx. The theme therefore supplies hue, not a flat
    // replacement: white stays bright, gray stays dim, and traces of the
    // original channel mix survive (the Classic gold selection pulse keeps
    // visibly breathing). The mix weight varies smoothly with brightness so
    // hover-brightening (+50) never causes a visible tier snap.
    const float fmx = static_cast<float>(mx);
    float mix       = 0.55f + 0.3f * ((fmx - 30.0f) / 225.0f); // 30->0.55, 255->0.85
    if (tier == BrightnessTier::VeryBright)
        mix = 0.85f; // full theme hue at the top
    const float cr = (1.0f - mix) * static_cast<float>(fg.r) + mix * hr * fmx;
    const float cg = (1.0f - mix) * static_cast<float>(fg.g) + mix * hg * fmx;
    const float cb = (1.0f - mix) * static_cast<float>(fg.b) + mix * hb * fmx;

    return {static_cast<uint8_t>(std::min(255.0f, cr * damped)), static_cast<uint8_t>(std::min(255.0f, cg * damped)),
            static_cast<uint8_t>(std::min(255.0f, cb * damped))};
}

Color GameEngine::applyGeneralTheme(Color fg, int row, int col) const {
    if (selected_general_theme_ == 0)
        return fg;
    const double offset = row * 0.08 + col * 0.04;
    return applyGeneralThemeImpl(fg, offset);
}

Color GameEngine::applyGeneralThemeGradient(Color fg, int row, int col, bool is_gradient_start) const {
    if (selected_general_theme_ == 0)
        return fg;
    const double offset = row * 0.08 + col * 0.04 + (is_gradient_start ? 0.0 : 0.8);
    return applyGeneralThemeImpl(fg, offset);
}

bool GameEngine::isColorLocked(int color_idx) const {
    if (color_idx == 0)
        return false;
    if (color_idx == 1)
        return (max_unlocked_level_ <= 4);
    if (color_idx == 2)
        return (max_unlocked_level_ <= 8);
    if (color_idx == 3)
        return (max_unlocked_level_ <= 12);
    if (color_idx == 4)
        return (max_unlocked_level_ <= 16);
    if (color_idx == 5)
        return (max_unlocked_level_ <= 20);
    if (color_idx == 6)
        return (max_unlocked_level_ <= 23);
    if (color_idx == 7)
        return (max_unlocked_level_ <= 26);
    if (color_idx == 8)
        return !unlocked_rainbow_;
    if (color_idx == 9)
        return (max_unlocked_level_ < 30);
    if (color_idx == 10)
        return !pacterm_plus_unlocked_;
    return false;
}

bool GameEngine::isGlitchZone(int x, int y) const {
    (void)y;
    if (level_ == 30)
        return true;
    if (level_ == 20)
        return x >= 14;
    return false;
}

LevelTheme GameEngine::themeForLevel(int lvl) const {
    if (lvl >= 1 && lvl <= 4)
        return LevelTheme::Classic;
    if (lvl >= 5 && lvl <= 8)
        return LevelTheme::Cyan;
    if (lvl >= 9 && lvl <= 12)
        return LevelTheme::Green;
    if (lvl >= 13 && lvl <= 16)
        return LevelTheme::Pink;
    if (lvl >= 17 && lvl <= 19)
        return LevelTheme::Red;
    if (lvl == 20 || lvl == 30)
        return LevelTheme::Glitch;
    if (lvl >= 21 && lvl <= 23)
        return LevelTheme::Violet;
    if (lvl >= 24 && lvl <= 26)
        return LevelTheme::Ice;
    if (lvl >= 27 && lvl <= 29)
        return LevelTheme::Amber;
    return LevelTheme::Classic;
}

Color GameEngine::levelThemeColor(int lvl) const {
    if (lvl >= 1 && lvl <= 4)
        return {255, 255, 0};
    if (lvl >= 5 && lvl <= 8)
        return {0, 255, 255};
    if (lvl >= 9 && lvl <= 12)
        return {0, 255, 100};
    if (lvl >= 13 && lvl <= 16)
        return {255, 105, 180};
    if (lvl >= 17 && lvl <= 19)
        return {255, 50, 50};
    if (lvl == 20 || lvl == 30)
        return {255, 0, 255};
    if (lvl >= 21 && lvl <= 23)
        return {200, 100, 255};
    if (lvl >= 24 && lvl <= 26)
        return {120, 220, 255};
    if (lvl >= 27 && lvl <= 29)
        return {255, 180, 60};
    return {255, 255, 255};
}

bool GameEngine::hasPowerup(PowerupKind kind) const {
    for (const auto& p : current_powerups_) {
        if (p.kind == kind)
            return true;
    }
    return false;
}

void GameEngine::loadThemePowerups() {
    current_theme_    = themeForLevel(level_);
    current_powerups_ = {};
    for (const auto& tp : THEME_POWERUPS) {
        if (tp.theme == current_theme_) {
            current_powerups_ = tp.powerups;
            break;
        }
    }
    lava_resist_cooldown_ms_ = 0;
    lava_resist_active_      = false;
    lava_resist_window_ms_   = 0;
    warp_stun_timer_ms_      = 0;
    glitch_warp_timer_ms_    = 8000;
}

std::vector<Vec2> GameEngine::reachableTiles() const {
    std::vector<Vec2> out;
    out.reserve(Config::MAP_WIDTH * Config::MAP_HEIGHT / 2);

    std::array<std::array<bool, Config::MAP_WIDTH>, Config::MAP_HEIGHT> visited{};
    std::array<Vec2, Config::MAP_WIDTH * Config::MAP_HEIGHT> q{};

    Vec2 start                = map_.findNearestWalkable(pacman_.position);
    visited[start.y][start.x] = true;

    size_t head = 0;
    size_t tail = 0;
    q[tail++]   = start;

    while (head < tail) {
        Vec2 curr               = q[head++];
        bool inside_ghost_house = (curr.y >= 12 && curr.y <= 16 && curr.x >= 10 && curr.x <= 17);
        if (!inside_ghost_house && map_.isWalkable(curr)) {
            out.push_back(curr);
        }

        std::array<Vec2, 4> neighbors = {{{curr.x + 1, curr.y}, {curr.x - 1, curr.y}, {curr.x, curr.y + 1}, {curr.x, curr.y - 1}}};
        for (auto next : neighbors) {
            Vec2 wrapped = map_.wrapTunnel(next);
            if (wrapped.x >= 0 && wrapped.x < Config::MAP_WIDTH && wrapped.y >= 0 && wrapped.y < Config::MAP_HEIGHT) {
                if (!visited[wrapped.y][wrapped.x] && map_.isWalkable(wrapped)) {
                    visited[wrapped.y][wrapped.x] = true;
                    if (tail < q.size()) {
                        q[tail++] = wrapped;
                    }
                }
            }
        }
    }
    return out;
}

void GameEngine::startLevel(int lvl) {
    level_ = lvl;
    map_.loadLevel(lvl);
    pacman_.position = map_.findNearestWalkable(pacman_.position);
    loadThemePowerups();

    bonus_fruit_active_           = false;
    fruit_magnet_timer_ms_        = 0;
    fruit_shield_active_          = false;
    fruit_double_bounty_timer_ms_ = 0;

    popups_.clear();
    particles_.clear();
    speed_boost_timer_ms_ = 0;
    ice_freeze_timer_ms_  = 0;

    letter_hunt_.active         = false;
    ghost_freeze_timer_ms_      = 0;
    pac_speed_timer_ms_         = 0;
    letter_score_mult_timer_ms_ = 0;
    fever_timer_ms_             = 0;
    fever_active_               = false;
    level_deaths_               = 0;
    spawnLetter();
    acid_trails_.clear();
    lava_tiles_.clear();
    dash_cooldown_        = 0;
    ghost_blitz_timer_ms_ = 0;
    ghost_blitz_cooldown_ = 0;
    ice_freeze_cooldown_  = 0;

    portal_A1_ = {0, 0};
    portal_A2_ = {0, 0};
    portal_B1_ = {0, 0};
    portal_B2_ = {0, 0};
    if (level_ >= 9 && level_ <= 12) {
        for (int y = 2; y < Config::MAP_HEIGHT / 2; ++y) {
            for (int x = 2; x < Config::MAP_WIDTH / 2; ++x) {
                if (map_.getTile(x, y) == TileType::Empty) {
                    portal_A1_ = {x, y};
                    break;
                }
            }
            if (portal_A1_.x != 0)
                break;
        }
        for (int y = Config::MAP_HEIGHT - 3; y >= Config::MAP_HEIGHT / 2; --y) {
            for (int x = Config::MAP_WIDTH - 3; x >= Config::MAP_WIDTH / 2; --x) {
                if (map_.getTile(x, y) == TileType::Empty) {
                    portal_A2_ = {x, y};
                    break;
                }
            }
            if (portal_A2_.x != 0)
                break;
        }
        for (int y = 2; y < Config::MAP_HEIGHT / 2; ++y) {
            for (int x = Config::MAP_WIDTH - 3; x >= Config::MAP_WIDTH / 2; --x) {
                if (map_.getTile(x, y) == TileType::Empty) {
                    portal_B1_ = {x, y};
                    break;
                }
            }
            if (portal_B1_.x != 0)
                break;
        }
        for (int y = Config::MAP_HEIGHT - 3; y >= Config::MAP_HEIGHT / 2; --y) {
            for (int x = 2; x < Config::MAP_WIDTH / 2; ++x) {
                if (map_.getTile(x, y) == TileType::Empty) {
                    portal_B2_ = {x, y};
                    break;
                }
            }
            if (portal_B2_.x != 0)
                break;
        }
    }
}

void GameEngine::spawnLetter() {
    if (letter_hunt_.allCollected() || letter_hunt_.active) {
        return;
    }

    if ((rng_() % 100) >= 25) {
        return;
    }

    int target = -1;
    for (int k = 0; k < LetterHuntState::LETTER_COUNT; ++k) {
        if (!letter_hunt_.isCollected(k)) {
            target = k;
            break;
        }
    }
    if (target < 0)
        return;

    TileType type = static_cast<TileType>(std::to_underlying(TileType::LetterP) + target);

    std::vector<Vec2> reachable;
    std::vector<std::vector<bool>> visited(Config::MAP_HEIGHT, std::vector<bool>(Config::MAP_WIDTH, false));
    std::queue<Vec2> q;
    Vec2 start_pos = {13, 23};
    q.push(start_pos);
    visited[start_pos.y][start_pos.x] = true;

    while (!q.empty()) {
        Vec2 curr = q.front();
        q.pop();

        bool inside_ghost_house = (curr.y >= 12 && curr.y <= 16 && curr.x >= 10 && curr.x <= 17);
        TileType ct             = map_.getTile(curr);
        if (!inside_ghost_house && (ct == TileType::Empty || ct == TileType::Dot)) {
            reachable.push_back(curr);
        }

        std::array<Vec2, 4> neighbors = {{{curr.x + 1, curr.y}, {curr.x - 1, curr.y}, {curr.x, curr.y + 1}, {curr.x, curr.y - 1}}};
        for (auto next : neighbors) {
            if (next.x < 0)
                next.x = Config::MAP_WIDTH - 1;
            if (next.x >= Config::MAP_WIDTH)
                next.x = 0;
            if (next.y >= 0 && next.y < Config::MAP_HEIGHT && !visited[next.y][next.x]) {
                TileType nt = map_.getTile(next.x, next.y);
                if (nt != TileType::Wall && nt != TileType::GhostDoor) {
                    visited[next.y][next.x] = true;
                    q.push(next);
                }
            }
        }
    }

    if (reachable.empty())
        return;

    std::shuffle(reachable.begin(), reachable.end(), rng_);
    Vec2 pos = reachable.front();
    if (map_.getTile(pos) == TileType::Dot) {
        map_.setTile(pos, TileType::Empty);
    }
    map_.setTile(pos, type);

    letter_hunt_.active   = true;
    letter_hunt_.pos      = pos;
    letter_hunt_.type     = type;
    letter_hunt_.timer_ms = LetterHuntState::ACTIVE_MS;
}

void GameEngine::collectLetter(int letter_idx, Vec2 pos) {
    letter_hunt_.active = false;

    score_ += Config::LETTER_SCORE;

    if (!letter_hunt_.isCollected(letter_idx)) {
        letter_hunt_.collect(letter_idx);
        if (letter_hunt_.allCollected() && !pacterm_plus_unlocked_) {
            pacterm_plus_unlocked_  = true;
            main_menu_message_      = "PACTERM+ THEME UNLOCKED!";
            main_menu_msg_timer_ms_ = 4000;
        }
    }

    ghost_freeze_timer_ms_      = Config::LETTER_GHOST_FREEZE_MS;
    pac_speed_timer_ms_         = Config::LETTER_SPEED_BOOST_MS;
    letter_score_mult_timer_ms_ = Config::LETTER_SCORE_MULT_MS;

    if (score_ > high_score_)
        high_score_ = score_;
    saveHighScore();
    spawnScorePopup(pos, Config::LETTER_SCORE, {255, 215, 0});
    spawnParticleBurst(pos, {255, 215, 0});
    playSound("sounds/eat_pellet.wav");
}

double GameEngine::getActiveScoreMultiplier() const {
    double mult = 1.0;
    if (letter_score_mult_timer_ms_ > 0)
        mult *= Config::LETTER_SCORE_MULTIPLIER;
    if (fever_active_ && fever_timer_ms_ > 0)
        mult *= FeverState::MULTIPLIER;
    if (fruit_double_bounty_timer_ms_ > 0)
        mult *= 2.0;
    return mult;
}

void GameEngine::triggerFever() {
    fever_active_   = true;
    fever_timer_ms_ = FeverState::DURATION_MS;
    FloatingPopup popup;
    popup.pos         = pacman_.position;
    popup.text        = "FEVER x2!";
    popup.fg          = {255, 0, 255};
    popup.lifetime_ms = 900;
    popups_.push_back(popup);
    playSound("sounds/eat_ghost.wav");
}

void GameEngine::spawnGhostTrail(Vec2 pos) {
    for (int i = 0; i < 2; ++i) {
        Particle p;
        p.x           = pos.x + 0.5;
        p.y           = pos.y + 0.5;
        p.vx          = 0.0;
        p.vy          = 0.0;
        p.color       = themeAccent(Config::PACTERM_PLUS_THEME, static_cast<double>(i) * 0.9);
        p.lifetime_ms = 140 + i * 90;
        p.glyph       = use_nerd_fonts_ ? "•" : "::";
        particles_.push_back(p);
    }
}

int GameEngine::computeLevelRating(double elapsed_s, double par_s, double& penalty_out) const {
    double penalty = level_deaths_ * Config::RATING_DEATH_PENALTY + std::max(0.0, (elapsed_s - par_s) / 10.0 * Config::RATING_TIME_PENALTY_PER_10S);
    penalty_out    = penalty;
    double rating  = std::clamp(std::round(10.0 - penalty), 0.0, 10.0);
    return static_cast<int>(rating);
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

    key_to_action_[1000] = GameAction::Up;
    key_to_action_[1001] = GameAction::Down;
    key_to_action_[1003] = GameAction::Left;
    key_to_action_[1002] = GameAction::Right;

    key_to_action_['k'] = GameAction::Up;
    key_to_action_['K'] = GameAction::Up;
    key_to_action_['j'] = GameAction::Down;
    key_to_action_['J'] = GameAction::Down;
    key_to_action_['h'] = GameAction::Left;
    key_to_action_['H'] = GameAction::Left;
    key_to_action_['l'] = GameAction::Right;
    key_to_action_['L'] = GameAction::Right;

    key_to_action_['p'] = GameAction::Pause;
    key_to_action_['P'] = GameAction::Pause;
}

std::string GameEngine::getKeyName(int k) {
    if (k == ' ')
        return "SPACE";
    if (k == '\n' || k == '\r')
        return "ENTER";
    if (k == 27)
        return "ESC";
    if (k == 1000)
        return "ARROW_UP";
    if (k == 1001)
        return "ARROW_DOWN";
    if (k == 1002)
        return "ARROW_RIGHT";
    if (k == 1003)
        return "ARROW_LEFT";
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

    if (use_nerd_fonts_) {
        drawString(center_row - 10, center_col - 31, "  ██████╗  █████╗  ██████╗████████╗███████╗██████╗ ███╗   ███╗",
                   themePrimary(selected_general_theme_, 0.0 * 0.3));
        drawString(center_row - 9, center_col - 31, "  ██╔══██╗██╔══██╗██╔════╝╚══██╔══╝██╔════╝██╔══██╗████╗ ████║",
                   themePrimary(selected_general_theme_, 1.0 * 0.3));
        drawString(center_row - 8, center_col - 31, "  ██████╔╝███████║██║        ██║   █████╗  ██████╔╝██╔████╔██║",
                   themePrimary(selected_general_theme_, 2.0 * 0.3));
        drawString(center_row - 7, center_col - 31, "  ██╔═══╝ ██╔══██║██║        ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║",
                   themePrimary(selected_general_theme_, 3.0 * 0.3));
        drawString(center_row - 6, center_col - 31, "  ██║     ██║  ██║╚██████╗   ██║   ███████╗██║  ██║██║ ╚═╝ ██║",
                   themePrimary(selected_general_theme_, 4.0 * 0.3));
        drawString(center_row - 5, center_col - 31, "  ╚═╝     ╚═╝  ╚═╝ ╚═════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝",
                   themePrimary(selected_general_theme_, 5.0 * 0.3));
    } else {
        drawString(center_row - 10, center_col - 27, "  #####    ###    ###   #####  #####  ####   #   #   #  ",
                   themePrimary(selected_general_theme_, 0.0 * 0.3));
        drawString(center_row - 9, center_col - 27, "  #    #  #   #  #   #    #    #      #   #  ## ##   #  ",
                   themePrimary(selected_general_theme_, 1.0 * 0.3));
        drawString(center_row - 8, center_col - 27, "  #####   #####  #        #    ####   ####   # # #   #  ",
                   themePrimary(selected_general_theme_, 2.0 * 0.3));
        drawString(center_row - 7, center_col - 27, "  #       #   #  #   #    #    #      # #    #   #   #  ",
                   themePrimary(selected_general_theme_, 3.0 * 0.3));
        drawString(center_row - 6, center_col - 27, "  #       #   #   ###     #    #####  #  ##  #   #   #  ",
                   themePrimary(selected_general_theme_, 4.0 * 0.3));
    }

    std::string sub = "S C R E E N S A V E R";
    drawString(center_row - 3, center_col - sub.length() / 2, sub, {255, 255, 0});

    int anim_row = center_row;
    for (int col = 4; col < render_width_ - 4; ++col) {
        bool eaten = false;
        if (screensaver_dir_ == 1) {
            if (col <= screensaver_x_)
                eaten = true;
        } else {
            if (col >= screensaver_x_)
                eaten = true;
        }
        if (!eaten && (col % 4 == 0)) {
            Cell dot_cell;
            dot_cell.glyph = use_nerd_fonts_ ? "·" : ".";
            dot_cell.fg    = themeAccent(selected_general_theme_, col * 0.04);
            dot_cell.bg    = {0, 0, 0};
            setCell(anim_row, col, dot_cell);
        }
    }

    int pm_frame = (int)(screensaver_x_ * 1.5) % 2;
    Cell pm_cell;
    pm_cell.bg = {0, 0, 0};
    pm_cell.fg = themePrimary(selected_general_theme_, screensaver_x_ * 0.05);
    if (pm_frame == 0) {
        pm_cell.glyph = use_nerd_fonts_ ? (screensaver_dir_ == 1 ? "C" : "Ɔ") : (screensaver_dir_ == 1 ? "<" : ">");
    } else {
        pm_cell.glyph = use_nerd_fonts_ ? "●" : "O";
    }

    int pm_x = (int)screensaver_x_;
    if (pm_x >= 4 && pm_x < render_width_ - 4) {
        setCell(anim_row, pm_x, pm_cell);
    }

    struct ScreensaverGhost {
        int offset;
        Color color;
        bool frightened;
    };

    std::array<ScreensaverGhost, 4> s_ghosts;
    if (screensaver_dir_ == 1) {
        s_ghosts = {{{-6, {255, 0, 0}, false}, {-10, {255, 184, 255}, false}, {-14, {0, 255, 255}, false}, {-18, {255, 184, 82}, false}}};
    } else {
        s_ghosts = {{{-6, {33, 33, 255}, true}, {-10, {33, 33, 255}, true}, {-14, {33, 33, 255}, true}, {-18, {33, 33, 255}, true}}};
    }

    for (const auto& sg : s_ghosts) {
        int gx = pm_x + sg.offset;
        if (gx >= 4 && gx < render_width_ - 4) {
            Cell g_cell;
            g_cell.bg    = {0, 0, 0};
            g_cell.fg    = sg.color;
            g_cell.glyph = use_nerd_fonts_ ? "ᗣ" : "M";
            if (sg.frightened) {
                if ((gx % 8) < 2) {
                    g_cell.fg = {255, 255, 255};
                }
            }
            setCell(anim_row, gx, g_cell);
        }
    }

    std::string prompt       = std::string(I18n::t("game.press_any_key"));
    static int blink_counter = 0;
    blink_counter++;
    Color prompt_color = (blink_counter % 20 < 10) ? Color{150, 150, 150} : Color{80, 80, 80};
    drawString(center_row + 5, center_col - static_cast<int>(displayWidth(prompt)) / 2, prompt, prompt_color);
}

void GameEngine::renderEffects(const Viewport* vpp) {
    const Viewport vp = vpp ? *vpp : getViewport();

    for (const auto& p : particles_) {
        int x = static_cast<int>(p.x);
        int y = static_cast<int>(p.y);

        if (x >= vp.start_x && x < vp.start_x + vp.visible_cols && y >= vp.start_y && y < vp.start_y + vp.visible_rows) {

            int screen_row = vp.base_row + (y - vp.start_y);
            int screen_col = vp.base_col + (x - vp.start_x) * Config::TILE_RENDER_W;

            Cell cell;
            cell.fg = p.color;
            cell.bg = {0, 0, 0};
            if (!p.glyph.empty()) {
                cell.glyph = p.glyph;
            } else if (p.lifetime_ms > 250) {
                cell.glyph = "*";
            } else if (p.lifetime_ms > 120) {
                cell.glyph = "+";
            } else {
                cell.glyph = "·";
            }
            if (displayWidth(cell.glyph) > 1) {
                setTileGlyph(screen_row, screen_col, cell.glyph, cell.fg, cell.bg);
            } else {
                setCell(screen_row, screen_col, cell);
            }
        }
    }

    for (const auto& popup : popups_) {
        int x = popup.pos.x;
        int y = popup.pos.y;

        if (x >= vp.start_x && x < vp.start_x + vp.visible_cols && y >= vp.start_y && y < vp.start_y + vp.visible_rows) {

            int screen_row = vp.base_row + (y - vp.start_y);
            int screen_col = vp.base_col + (x - vp.start_x) * Config::TILE_RENDER_W - static_cast<int>(popup.text.length() / 2);
            if (screen_col < 0)
                screen_col = 0;

            drawString(screen_row, screen_col, popup.text, popup.fg);
        }
    }
}

void GameEngine::spawnScorePopup(Vec2 pos, int points, Color fg) {
    FloatingPopup popup;
    popup.pos         = pos;
    popup.text        = "+" + std::to_string(points);
    popup.fg          = fg;
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
        p.x           = pos.x + 0.5;
        p.y           = pos.y + 0.5;
        p.vx          = std::cos(angle) * speed * 2.0;
        p.vy          = std::sin(angle) * speed;
        p.color       = color;
        p.lifetime_ms = 300 + (static_cast<int>(rng_()) % 150);
        particles_.push_back(p);
    }
}

void GameEngine::renderLevelSelector() {
    clearBuffer({0, 0, 0});
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    // Apply theme to border and title
    apply_menu_theme_ = true;
    drawDoubleBorderBox(center_row - 10, center_col - 30, 60, 17, {0, 255, 255}, {0, 0, 0});
    std::string ls_title = " " + std::string(I18n::t("level_select.title")) + " ";
    drawString(center_row - 10, center_col - static_cast<int>(displayWidth(ls_title)) / 2, ls_title, {255, 255, 0});
    apply_menu_theme_ = false;

    // Level numbers use fixed per-level colors (no theme override)
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 5; ++c) {
            int lvl = r * 5 + c + 1;
            int row = center_row - 8 + r * 2;
            int col = center_col - 22 + c * 11;

            bool locked   = (lvl > max_unlocked_level_);
            bool selected = (level_select_cursor_ == lvl - 1);

            Color theme_color = levelThemeColor(lvl);

            std::string label;
            if (locked) {
                theme_color = {60, 60, 60};
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

            if (lvl == 20) {
                Color norm_c = selected ? (locked ? Color{180, 180, 180} : Color{255, 255, 255}) : (locked ? Color{60, 60, 60} : Color{255, 255, 0});
                if (hovered && !selected) {
                    norm_c = locked ? Color{140, 140, 140} : Color{0, 255, 255};
                }
                Color gl_c           = glitchRGB(current_time_ms_);
                const auto& g_glyphs = use_nerd_fonts_ ? kGlitchBlockGlyphs : kGlitchAsciiGlyphs;
                std::string g_digit  = ((current_time_ms_ / 150) % 3 == 0) ? (locked ? "X" : "0") : g_glyphs[(current_time_ms_ / 100) % 4];
                std::string g_brk    = ((current_time_ms_ / 200) % 4 == 0) ? "]" : g_glyphs[(current_time_ms_ / 100 + 1) % 4];
                std::string pfx      = (selected || hovered) ? "> " : "  ";
                std::string sfx      = (selected || hovered) ? " <" : "  ";
                std::string d1       = locked ? "X" : "2";

                drawString(row, col - 4, pfx, norm_c, {0, 0, 0}, selected || hovered);
                drawString(row, col - 2, "[", norm_c, {0, 0, 0}, selected || hovered);
                drawString(row, col - 1, d1, norm_c, {0, 0, 0}, selected || hovered);
                drawString(row, col, g_digit, gl_c, {0, 0, 0}, selected || hovered);
                drawString(row, col + 1, g_brk, gl_c, {0, 0, 0}, selected || hovered);
                drawString(row, col + 2, sfx, gl_c, {0, 0, 0}, selected || hovered);
            } else if (lvl == 30) {
                Color gl_c1          = glitchRGB(current_time_ms_);
                Color gl_c2          = glitchRGB(current_time_ms_ + 60);
                Color gl_c3          = glitchRGB(current_time_ms_ + 120);
                Color gl_c4          = glitchRGB(current_time_ms_ + 180);
                const auto& g_glyphs = use_nerd_fonts_ ? kGlitchBlockGlyphs : kGlitchAsciiGlyphs;

                std::string g_brk1 = ((current_time_ms_ / 200) % 4 == 0) ? "[" : g_glyphs[(current_time_ms_ / 100) % 4];
                std::string g_d1   = ((current_time_ms_ / 150) % 3 == 0) ? (locked ? "X" : "3") : g_glyphs[(current_time_ms_ / 100 + 1) % 4];
                std::string g_d2   = ((current_time_ms_ / 150) % 3 == 0) ? (locked ? "X" : "0") : g_glyphs[(current_time_ms_ / 100 + 2) % 4];
                std::string g_brk2 = ((current_time_ms_ / 200) % 4 == 0) ? "]" : g_glyphs[(current_time_ms_ / 100 + 3) % 4];

                Color pfx_c     = selected ? (locked ? Color{180, 180, 180} : Color{255, 255, 255}) : (hovered ? Color{0, 255, 255} : gl_c1);
                std::string pfx = (selected || hovered) ? "> " : "  ";
                std::string sfx = (selected || hovered) ? " <" : "  ";

                drawString(row, col - 4, pfx, pfx_c, {0, 0, 0}, selected || hovered);
                drawString(row, col - 2, g_brk1, gl_c1, {0, 0, 0}, selected || hovered);
                drawString(row, col - 1, g_d1, gl_c2, {0, 0, 0}, selected || hovered);
                drawString(row, col, g_d2, gl_c3, {0, 0, 0}, selected || hovered);
                drawString(row, col + 1, g_brk2, gl_c4, {0, 0, 0}, selected || hovered);
                drawString(row, col + 2, sfx, pfx_c, {0, 0, 0}, selected || hovered);
            } else if (selected) {
                Color sel_color = locked ? Color{180, 180, 180} : Color{255, 255, 255};
                if (hovered) {
                    sel_color = {255, 255, 120};
                }
                drawString(row, col - 4, label, sel_color, {0, 0, 0}, true);
            } else if (hovered) {
                Color hov_color       = locked ? Color{140, 140, 140} : Color{0, 255, 255};
                std::string num       = (lvl < 10) ? "0" + std::to_string(lvl) : std::to_string(lvl);
                std::string hov_label = locked ? "> [XX] <" : "> [" + num + "] <";
                drawString(row, col - 4, hov_label, hov_color, {0, 0, 0}, true);
            } else {
                drawString(row, col - 4, label, theme_color);
            }
        }
    }

    int row_back          = center_row + 4;
    std::string back_base = "[ " + std::string(I18n::t("level_select.back")) + " ]";
    std::string back_text;
    Color back_color = {150, 150, 150};
    bool back_bold   = false;
    if (level_select_cursor_ == 30) {
        back_text = "> " + back_base + " <";
        if (click_feedback_timer_ms_ > 0) {
            back_color = {255, 255, 255};
            back_bold  = true;
        } else {
            back_color = {255, 255, 0};
        }
    } else {
        back_text  = "  " + back_base + "  ";
        back_color = {150, 150, 150};
    }
    if (isMouseHovering(row_back, center_col - static_cast<int>(glyphCount(back_text)) / 2, "  " + back_base + "  ")) {
        back_color = {0, 255, 255};
        back_bold  = true;
        back_text  = "> " + back_base + " <";
    }
    drawString(row_back, center_col - static_cast<int>(glyphCount(back_text)) / 2, back_text, back_color, {0, 0, 0}, back_bold);
}

void GameEngine::renderKeyConfig() {
    int center_row = render_height_ / 2;
    int center_col = render_width_ / 2;

    clearBuffer({0, 0, 0});

    std::string press_key_text = std::string(I18n::t("key_config.press_key"));
    std::string up_name        = is_binding_ ? (binding_action_ == GameAction::Up ? press_key_text : getKeyName(custom_key_up_)) : getKeyName(custom_key_up_);
    std::string down_name = is_binding_ ? (binding_action_ == GameAction::Down ? press_key_text : getKeyName(custom_key_down_)) : getKeyName(custom_key_down_);
    std::string left_name = is_binding_ ? (binding_action_ == GameAction::Left ? press_key_text : getKeyName(custom_key_left_)) : getKeyName(custom_key_left_);
    std::string right_name =
        is_binding_ ? (binding_action_ == GameAction::Right ? press_key_text : getKeyName(custom_key_right_)) : getKeyName(custom_key_right_);
    std::string pause_name =
        is_binding_ ? (binding_action_ == GameAction::Pause ? press_key_text : getKeyName(custom_key_pause_)) : getKeyName(custom_key_pause_);

    std::array<std::string, 6> options = {"UP:    [ " + up_name + " ]",    "DOWN:  [ " + down_name + " ]",  "LEFT:  [ " + left_name + " ]",
                                          "RIGHT: [ " + right_name + " ]", "PAUSE: [ " + pause_name + " ]", std::string(I18n::t("key_config.save_back"))};

    drawTitleBorderBox(center_row - 6, center_col - 20, 40, 11, " " + std::string(I18n::t("key_config.title")) + " ", {0, 255, 255}, {255, 255, 0}, {0, 0, 0});

    int block_left = center_col - 16;

    for (int i = 0; i < 6; ++i) {
        Color fg           = {220, 220, 220};
        std::string prefix = "  ";
        bool bold          = false;
        bool is_selected   = (i == key_config_selection_);
        bool is_hovered    = isMouseHovering(center_row - 4 + i, block_left, "  " + options[i]);

        if (is_selected) {
            if (click_feedback_timer_ms_ > 0) {
                fg   = {255, 255, 255};
                bold = true;
            } else {
                fg = {255, 200, 0};
            }
            prefix = "> ";
        } else if (is_hovered) {
            fg     = {0, 255, 255};
            prefix = "> ";
            bold   = true;
        }

        if (is_selected && is_hovered) {
            fg   = {255, 255, 120};
            bold = true;
        }
        drawString(center_row - 4 + i, block_left, prefix + options[i], fg, {0, 0, 0}, bold);
    }

    if (is_binding_) {
        std::string binding_p = std::string(I18n::t("key_config.binding_prompt"));
        drawString(center_row + 3, center_col - static_cast<int>(glyphCount(binding_p)) / 2, binding_p, {255, 100, 100});
    } else {
        std::string enter_p = std::string(I18n::t("key_config.enter_prompt"));
        drawString(center_row + 3, center_col - static_cast<int>(glyphCount(enter_p)) / 2, enter_p, {180, 180, 180});
    }
}

void GameEngine::drawDoubleBorderBox(int row, int col, int w, int h, Color fg, Color bg) {
    drawBox(row, col, w, h, fg, bg);
    menu_accent_ = true;

    std::string tl = use_nerd_fonts_ ? "╔" : "+";
    std::string hz = use_nerd_fonts_ ? "═" : "-";
    std::string tr = use_nerd_fonts_ ? "╗" : "+";
    std::string vt = use_nerd_fonts_ ? "║" : "|";
    std::string bl = use_nerd_fonts_ ? "╚" : "+";
    std::string br = use_nerd_fonts_ ? "╝" : "+";

    setCell(row, col, Cell{.glyph = tl, .fg = fg, .bg = bg});
    for (int c = col + 1; c < col + w - 1; ++c) {
        setCell(row, c, Cell{.glyph = hz, .fg = fg, .bg = bg});
    }
    setCell(row, col + w - 1, Cell{.glyph = tr, .fg = fg, .bg = bg});

    for (int r = row + 1; r < row + h - 1; ++r) {
        setCell(r, col, Cell{.glyph = vt, .fg = fg, .bg = bg});
        setCell(r, col + w - 1, Cell{.glyph = vt, .fg = fg, .bg = bg});
    }

    setCell(row + h - 1, col, Cell{.glyph = bl, .fg = fg, .bg = bg});
    for (int c = col + 1; c < col + w - 1; ++c) {
        setCell(row + h - 1, c, Cell{.glyph = hz, .fg = fg, .bg = bg});
    }
    setCell(row + h - 1, col + w - 1, Cell{.glyph = br, .fg = fg, .bg = bg});
    menu_accent_ = false;
}

void GameEngine::drawTitleBorderBox(int row, int col, int w, int h, std::string_view title, Color border_fg, Color title_fg, Color bg) {
    drawBox(row, col, w, h, border_fg, bg);
    menu_accent_ = true;

    std::string tl = use_nerd_fonts_ ? "╔" : "+";
    std::string hz = use_nerd_fonts_ ? "═" : "-";
    std::string tr = use_nerd_fonts_ ? "╗" : "+";
    std::string vt = use_nerd_fonts_ ? "║" : "|";
    std::string bl = use_nerd_fonts_ ? "╚" : "+";
    std::string br = use_nerd_fonts_ ? "╝" : "+";

    int title_len = static_cast<int>(glyphCount(title));
    int side      = (w - 2 - title_len) / 2;

    setCell(row, col, Cell{.glyph = tl, .fg = border_fg, .bg = bg});
    for (int i = 0; i < side; ++i) {
        setCell(row, col + 1 + i, Cell{.glyph = hz, .fg = border_fg, .bg = bg});
    }

    menu_accent_ = false;
    drawString(row, col + 1 + side, title, title_fg, bg, false);
    menu_accent_ = true;

    for (int i = col + 1 + side + title_len; i < col + w - 1; ++i) {
        setCell(row, i, Cell{.glyph = hz, .fg = border_fg, .bg = bg});
    }
    setCell(row, col + w - 1, Cell{.glyph = tr, .fg = border_fg, .bg = bg});

    for (int r = row + 1; r < row + h - 1; ++r) {
        setCell(r, col, Cell{.glyph = vt, .fg = border_fg, .bg = bg});
        setCell(r, col + w - 1, Cell{.glyph = vt, .fg = border_fg, .bg = bg});
    }

    setCell(row + h - 1, col, Cell{.glyph = bl, .fg = border_fg, .bg = bg});
    for (int c2 = col + 1; c2 < col + w - 1; ++c2) {
        setCell(row + h - 1, c2, Cell{.glyph = hz, .fg = border_fg, .bg = bg});
    }
    setCell(row + h - 1, col + w - 1, Cell{.glyph = br, .fg = border_fg, .bg = bg});
    menu_accent_ = false;
}

void GameEngine::drawTitleBorderBox(int row, int col, int w, int h, std::string_view title, Color fg) {
    drawTitleBorderBox(row, col, w, h, title, fg, fg, {0, 0, 0});
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

    drawTitleBorderBox(center_row - 6, center_col - 22, 44, 13, " DEVELOPER MENU ", {255, 50, 50}, {255, 255, 0}, {0, 0, 0});

    std::string color_name{kThemeNames[selected_pacman_color_ % Config::THEME_COUNT]};

    std::array<std::string, 9> options = {"Level ID:         < " + std::to_string(level_) + " >",
                                          "Pac-Man Color:    < " + color_name + " >",
                                          "Hearts (Max 66):  < " + std::to_string(lives_) + " >",
                                          "Score:            < " + std::to_string(score_) + " >",
                                          "Immortal Cheat:   < " + std::string(immortal_ ? "YES" : "NO") + " >",
                                          "Freeze Ghosts:    < " + std::string(cheat_freeze_ghosts_ ? "YES" : "NO") + " >",
                                          "Super Speed:      < " + std::string(cheat_super_speed_ ? "YES" : "NO") + " >",
                                          "Skip Level:       [ Press ENTER ]",
                                          "[ EXIT DEVELOPER MENU ]"};

    int block_left = center_col - 18;

    for (int i = 0; i < 9; ++i) {
        Color fg           = {255, 255, 255};
        std::string prefix = "  ";
        if (i == dev_menu_selection_) {
            fg     = {255, 255, 0};
            prefix = "> ";
        }
        drawString(center_row - 4 + i, block_left, prefix + options[i], fg);
    }
}

std::filesystem::path GameEngine::localBinPath() {
    const char* home_env       = std::getenv("HOME");
    std::filesystem::path home = (home_env != nullptr && *home_env != '\0') ? std::filesystem::path(home_env) : std::filesystem::path("/tmp");
    return home / ".local" / "bin" / "pacterm";
}

bool GameEngine::install_bin(bool cli_mode) {
    try {
        std::filesystem::path self_path = std::filesystem::read_symlink("/proc/self/exe");
        std::filesystem::path dest_path = localBinPath();

        std::filesystem::create_directories(dest_path.parent_path());

        std::filesystem::copy_file(self_path, dest_path, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(dest_path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add);

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
        std::filesystem::path installed_path = localBinPath();

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
    return std::filesystem::exists(localBinPath());
}

namespace {
    enum class WaveType { Sine, Square, Triangle };

    struct WavHeader {
        char chunkId[4]        = {'R', 'I', 'F', 'F'};
        uint32_t chunkSize     = 0;
        char format[4]         = {'W', 'A', 'V', 'E'};
        char subchunk1Id[4]    = {'f', 'm', 't', ' '};
        uint32_t subchunk1Size = 16;
        uint16_t audioFormat   = 1;
        uint16_t numChannels   = 1;
        uint32_t sampleRate    = 44100;
        uint32_t byteRate      = 0;
        uint16_t blockAlign    = 0;
        uint16_t bitsPerSample = 16;
        char subchunk2Id[4]    = {'d', 'a', 't', 'a'};
        uint32_t subchunk2Size = 0;
    };

    const double PI = 3.14159265358979323846;

    std::vector<int16_t> generateRetroSound(double start_freq, double end_freq, double duration, WaveType type, double volume = 0.2) {
        int sample_rate = 44100;
        int num_samples = static_cast<int>(duration * sample_rate);
        std::vector<int16_t> samples(num_samples);
        double max_amp = 32767 * volume;
        double phase   = 0.0;

        for (int i = 0; i < num_samples; ++i) {
            double t        = static_cast<double>(i) / sample_rate;
            double progress = t / duration;
            double freq     = start_freq + (end_freq - start_freq) * progress;

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

            double env          = 1.0;
            double fade_in_len  = 0.01 * duration;
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
        header.chunkSize     = 36 + header.subchunk2Size;
        header.byteRate      = header.sampleRate * header.numChannels * (header.bitsPerSample / 8);
        header.blockAlign    = header.numChannels * (header.bitsPerSample / 8);

        std::ofstream out(filename, std::ios::binary);
        if (!out)
            return;
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(samples.data()), header.subchunk2Size);
    }
} // namespace

void GameEngine::generateSounds() {
    std::filesystem::path sound_dir = getSoundDirectory();
    std::filesystem::create_directories(sound_dir);

    std::filesystem::path p_eat_dot = sound_dir / "eat_dot.wav";
    if (!std::filesystem::exists(p_eat_dot)) {
        auto samples = generateRetroSound(550.0, 850.0, 0.05, WaveType::Triangle, 0.15);
        writeWavFile(p_eat_dot.string(), samples);
    }

    std::filesystem::path p_eat_pellet = sound_dir / "eat_pellet.wav";
    if (!std::filesystem::exists(p_eat_pellet)) {
        std::vector<int16_t> samples;
        auto sweep1 = generateRetroSound(500.0, 800.0, 0.06, WaveType::Triangle, 0.2);
        auto sweep2 = generateRetroSound(700.0, 1000.0, 0.06, WaveType::Triangle, 0.2);
        samples.insert(samples.end(), sweep1.begin(), sweep1.end());
        samples.insert(samples.end(), sweep2.begin(), sweep2.end());
        writeWavFile(p_eat_pellet.string(), samples);
    }

    std::filesystem::path p_eat_ghost = sound_dir / "eat_ghost.wav";
    if (!std::filesystem::exists(p_eat_ghost)) {
        auto samples = generateRetroSound(200.0, 1200.0, 0.35, WaveType::Square, 0.25);
        writeWavFile(p_eat_ghost.string(), samples);
    }

    std::filesystem::path p_death = sound_dir / "death.wav";
    if (!std::filesystem::exists(p_death)) {
        std::vector<int16_t> death_samples;
        for (int step = 0; step < 8; ++step) {
            double start_f = 850.0 - step * 95.0;
            double end_f   = 750.0 - step * 95.0;
            auto chirp     = generateRetroSound(start_f, end_f, 0.09, WaveType::Square, 0.2);
            death_samples.insert(death_samples.end(), chirp.begin(), chirp.end());
            auto silence = generateNote(0.0, 0.02, WaveType::Sine, 0.0);
            death_samples.insert(death_samples.end(), silence.begin(), silence.end());
        }
        writeWavFile(p_death.string(), death_samples);
    }

    std::filesystem::path p_ready = sound_dir / "ready.wav";
    if (!std::filesystem::exists(p_ready)) {
        std::vector<std::pair<double, double>> ready_notes = {{523.25, 0.08}, {0.0, 0.02}, {659.25, 0.08}, {0.0, 0.02}, {783.99, 0.08}, {0.0, 0.02},
                                                              {659.25, 0.08}, {0.0, 0.02}, {783.99, 0.08}, {0.0, 0.02}, {1046.50, 0.25}};
        auto samples                                       = generateMelody(ready_notes, WaveType::Triangle, 0.2);
        writeWavFile(p_ready.string(), samples);
    }

    std::filesystem::path p_clear = sound_dir / "clear.wav";
    if (!std::filesystem::exists(p_clear)) {
        std::vector<std::pair<double, double>> clear_notes = {{523.25, 0.08}, {659.25, 0.08},  {783.99, 0.08}, {1046.50, 0.08},
                                                              {783.99, 0.08}, {1046.50, 0.08}, {1318.51, 0.35}};
        auto samples                                       = generateMelody(clear_notes, WaveType::Triangle, 0.2);
        writeWavFile(p_clear.string(), samples);
    }
}

void GameEngine::preloadAssets() {
    // 1. Synthesize all audio assets and pre-warm OS page cache
    try {
        generateSounds();
        std::filesystem::path sound_dir = getSoundDirectory();
        if (std::filesystem::exists(sound_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(sound_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".wav") {
                    std::ifstream f(entry.path(), std::ios::binary);
                    if (f) {
                        char buf[4096];
                        while (f.read(buf, sizeof(buf))) {}
                    }
                }
            }
        }
    } catch (...) {}

    // 2. Preload and validate all 30 level maze layouts
    try {
        Map preload_map;
        for (int lvl = 1; lvl <= 30; ++lvl) {
            preload_map.loadLevel(lvl);
        }
    } catch (...) {}

    // 3. Pre-warm localization strings and formatting caches
    try {
        (void)I18n::t("menu.play");
        (void)I18n::t("menu.settings");
        (void)I18n::t("menu.high_scores");
        (void)I18n::t("game.ready");
        (void)I18n::t("game.game_over");
    } catch (...) {}
}

void GameEngine::playSound(const std::string& name) {
    if (muted_)
        return;

    std::filesystem::path p(name);
    std::string filename            = p.filename().string();
    std::filesystem::path full_path = getSoundDirectory() / filename;
    std::string path_str            = full_path.string();

    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (audio_queue_.size() < 16) {
            audio_queue_.push(path_str);
        }
    }
    audio_cv_.notify_one();
}

void GameEngine::audioWorkerLoop() {
    while (audio_running_.load(std::memory_order_acquire)) {
        std::string path_str;
        {
            std::unique_lock<std::mutex> lock(audio_mutex_);
            audio_cv_.wait(lock, [this] { return !audio_queue_.empty() || !audio_running_.load(std::memory_order_relaxed); });
            if (!audio_running_.load(std::memory_order_relaxed) && audio_queue_.empty()) {
                break;
            }
            if (!audio_queue_.empty()) {
                path_str = std::move(audio_queue_.front());
                audio_queue_.pop();
            }
        }

        if (!path_str.empty()) {
            std::string cmd = "(paplay '" + path_str + "' || pw-play '" + path_str + "' || aplay -q '" + path_str + "' || mpg123 -q '" + path_str +
                              "' || mpv --no-video '" + path_str + "') >/dev/null 2>&1 &";
            auto res        = std::system(cmd.c_str());
            (void)res;
        }
    }
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

void AnimationController::fadeIn(const Color& color, int duration_ms) {
    state             = State::FadingIn;
    elapsed_ms_       = 0;
    this->duration_ms = duration_ms;
    start_color       = {0, 0, 0};
    end_color         = color;
    progress          = 0.0f;
}

void AnimationController::update(int delta_ms) {
    if (state == State::Idle)
        return;
    elapsed_ms_ += delta_ms;
    if (elapsed_ms_ >= duration_ms) {
        elapsed_ms_ = duration_ms;
        progress    = 1.0f;
        state       = State::Active;
    } else {
        progress = static_cast<float>(elapsed_ms_) / duration_ms;
    }
}

Color AnimationController::getCurrentColor() const {
    if (state == State::Idle)
        return start_color;
    float t   = progress;
    uint8_t r = static_cast<uint8_t>(start_color.r + t * (end_color.r - start_color.r));
    uint8_t g = static_cast<uint8_t>(start_color.g + t * (end_color.g - start_color.g));
    uint8_t b = static_cast<uint8_t>(start_color.b + t * (end_color.b - start_color.b));
    return {r, g, b};
}

void GameEngine::spawnBonusFruit() {
    if (bonus_fruit_active_)
        return;

    std::vector<Vec2> tiles = reachableTiles();
    std::vector<Vec2> valid_tiles;
    valid_tiles.reserve(tiles.size());
    for (const auto& pos : tiles) {
        if (pos == pacman_.position)
            continue;
        if (pos.y == Config::TUNNEL_ROW && (pos.x < 3 || pos.x > Config::MAP_WIDTH - 4))
            continue;
        valid_tiles.push_back(pos);
    }

    if (valid_tiles.empty()) {
        bonus_fruit_pos_ = {13, 17};
    } else {
        bonus_fruit_pos_ = valid_tiles[rng_() % valid_tiles.size()];
    }

    // Weighted Rarity Distribution:
    // Common (40%): Cherry (20%), Strawberry (20%)
    // Uncommon (30%): Orange (15%), Apple (15%)
    // Rare (18%): Melon (9%), Galaxian (9%)
    // Epic (9%): Bell (9%)
    // Legendary (3%): Key (3%)
    int roll            = rng_() % 100;
    TileType fruit_type = TileType::Cherry;

    if (roll < 20) {
        fruit_type = TileType::Cherry;
    } else if (roll < 40) {
        fruit_type = TileType::Strawberry;
    } else if (roll < 55) {
        fruit_type = TileType::Orange;
    } else if (roll < 70) {
        fruit_type = TileType::Apple;
    } else if (roll < 79) {
        fruit_type = TileType::Melon;
    } else if (roll < 88) {
        fruit_type = TileType::Galaxian;
    } else if (roll < 97) {
        fruit_type = TileType::Bell;
    } else {
        fruit_type = TileType::Key;
    }

    bonus_fruit_active_   = true;
    bonus_fruit_type_     = fruit_type;
    bonus_fruit_timer_ms_ = 14000;
}

void GameEngine::updateBonusFruit(int delta_ms) {
    if (!bonus_fruit_active_)
        return;

    bonus_fruit_timer_ms_ -= delta_ms;
    if (bonus_fruit_timer_ms_ <= 0) {
        bonus_fruit_active_ = false;
        return;
    }

    if (bonus_fruit_pos_ == pacman_.position) {
        eatBonusFruit(bonus_fruit_type_);
        bonus_fruit_active_ = false;
    }
}

void GameEngine::eatBonusFruit(TileType type) {
    int points        = 100;
    Color popup_color = {255, 100, 100};

    switch (type) {
    case TileType::Cherry:
        points                = 100;
        speed_boost_timer_ms_ = 6000;
        popup_color           = {255, 60, 60};
        break;
    case TileType::Strawberry:
        points                 = 300;
        ghost_freeze_timer_ms_ = 4000;
        popup_color            = {255, 105, 180};
        break;
    case TileType::Orange:
        points                 = 500;
        fruit_magnet_timer_ms_ = 6000;
        popup_color            = {255, 165, 0};
        break;
    case TileType::Apple:
        points               = 700;
        fruit_shield_active_ = true;
        popup_color          = {100, 255, 100};
        break;
    case TileType::Melon:
        points                        = 1000;
        fruit_double_bounty_timer_ms_ = 8000;
        popup_color                   = {150, 255, 150};
        break;
    case TileType::Galaxian:
        points      = 2000;
        popup_color = {0, 220, 255};
        for (auto& g : ghosts_) {
            if (g.mode != GhostMode::Eaten && g.mode != GhostMode::InHouse) {
                // If Pac-Man is right near the house exit, keep ghosts safely inside the house
                if (std::abs(pacman_.position.x - Config::GHOST_HOUSE_EXIT.x) <= 1 && std::abs(pacman_.position.y - Config::GHOST_HOUSE_EXIT.y) <= 1) {
                    g.position = Config::GHOST_HOUSE_CENTER;
                    g.mode     = GhostMode::InHouse;
                } else {
                    g.position = Config::GHOST_HOUSE_EXIT;
                    g.mode     = GhostMode::Scatter;
                }
                g.currentDirection = Direction::Up;
            }
        }
        ghost_freeze_timer_ms_ = 3000;
        break;
    case TileType::Bell:
        points      = 3000;
        popup_color = {255, 215, 0};
        for (auto& g : ghosts_) {
            g.frighten(Config::FRIGHTENED_DURATION);
        }
        break;
    case TileType::Key:
        points      = 5000;
        popup_color = {255, 230, 100};
        if (lives_ < 66)
            lives_++;
        triggerFever();
        break;
    default: points = 100; break;
    }

    addScore(points);
    spawnScorePopup(pacman_.position, points, popup_color);
    spawnParticleBurst(pacman_.position, popup_color);
    playSound("sounds/eat_pellet.wav");
}

void GameEngine::renderThemeInfo() {
    apply_menu_theme_ = false;
    int center_row    = render_height_ / 2;
    int center_col    = render_width_ / 2;

    int box_w    = 54;
    int box_h    = 11;
    int left_col = center_col - box_w / 2;
    int top_row  = center_row - box_h / 2;

    int theme_idx = std::to_underlying(themeForLevel(level_)) % Config::THEME_COUNT;
    if (theme_idx < 0)
        theme_idx = 0;

    Color lvl_primary = themePrimary(theme_idx, 0.0);
    Color lvl_accent  = themeAccent(theme_idx, 0.0);

    drawTitleBorderBox(top_row, left_col, box_w, box_h, " " + std::string(I18n::t("theme_info.title")) + " ", lvl_accent, lvl_primary, {0, 0, 0});

    int cur_y            = top_row + 2;
    int detail_x         = left_col + 3;
    size_t max_content_w = (box_w > 7) ? static_cast<size_t>(box_w - 7) : 0;

    std::string theme_name{kThemeNames[theme_idx]};
    std::string header_line = I18n::format("hud.level", level_) + "| " + std::string(I18n::t("theme_info.theme")) + theme_name;
    drawString(cur_y++, detail_x, truncateText(header_line, (box_w > 5) ? static_cast<size_t>(box_w - 5) : 0), lvl_primary, {0, 0, 0}, true);
    cur_y++;

    drawString(cur_y++, detail_x, std::string(I18n::t("theme_info.active_mechanics")), lvl_primary, {0, 0, 0}, true);

    std::string m1 = std::string(I18n::t("theme_info.t" + std::to_string(theme_idx) + "_m1"));
    std::string m2 = std::string(I18n::t("theme_info.t" + std::to_string(theme_idx) + "_m2"));
    std::string hz = std::string(I18n::t("theme_info.t" + std::to_string(theme_idx) + "_hz"));

    if (!m1.empty()) {
        drawString(cur_y++, detail_x + 2, truncateText("• " + m1, max_content_w), {220, 240, 255}, {0, 0, 0}, false);
    }
    if (!m2.empty()) {
        drawString(cur_y++, detail_x + 2, truncateText("• " + m2, max_content_w), {220, 240, 255}, {0, 0, 0}, false);
    }
    if (!hz.empty()) {
        std::string hz_prefix = std::string(I18n::t("theme_info.hazard_prefix"));
        drawString(cur_y++, detail_x + 2, truncateText(hz_prefix + hz, max_content_w), {255, 140, 140}, {0, 0, 0}, false);
    }
}
