// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.1
#pragma once

#include "Types.hpp"
#include "Pacman.hpp"
#include "Ghost.hpp"
#include <array>
#include <string>
#include <vector>
#include <chrono>
#include <termios.h>
#include <csignal>
#include <unordered_map>
#include <filesystem>
#include <functional>

enum class GameAction : uint8_t {
    None  = 0,
    Up    = 1,
    Down  = 2,
    Left  = 3,
    Right = 4,
    Pause = 5
};

// Animation Controller for smooth fade effects
struct AnimationController {
    enum class State {
        Idle,
        FadingIn,
        Active,
        FadingOut
    };
    
    State state = State::Idle;
    int elapsed_ms_ = 0;
    int duration_ms = 0;
    Color start_color;
    Color end_color;
    float progress = 0.0f;
    
    void fadeIn(const Color& color, int duration_ms);
    void fadeOut(const Color& color, int duration_ms);
    void update(int delta_ms);
    Color getCurrentColor() const;
    bool isComplete() const;
    void reset();
};

class GameEngine {
public:
    GameEngine();
    ~GameEngine();
    
    void run();
    static bool install_bin(bool cli_mode = false);
    static bool delete_bin(bool cli_mode = false);
    bool isInstalledLocally() const;
    
private:
    // Core state
    GamePhase phase_ = GamePhase::MainMenu;
    Map map_;
    PacMan pacman_;
    std::array<Ghost, 4> ghosts_;  // Blinky, Pinky, Inky, Clyde
    
    int score_ = 0;
    int high_score_ = 0;
    int lives_ = Config::INITIAL_LIVES;
    int level_ = 1;
    int pause_menu_selection_ = 0;
    int ghosts_eaten_combo_ = 0;  // resets when frightened ends
    bool extra_life_awarded_ = false;
    
    int main_menu_selection_ = 0;
    std::string main_menu_message_ = "";
    int main_menu_msg_timer_ = 0;
    
    // Timing & Loop logic
    bool running_ = true;
    int phase_timer_ = 0;
    int pac_move_accumulator_ = 0;
    int ghost_move_accumulator_ = 0;
    std::array<int, 4> ghost_accumulators_{};
    int global_mode_timer_ = 0;
    size_t current_wave_ = 0;
    
    // Terminal Raw Mode settings
    struct termios original_termios_{};
    bool raw_mode_enabled_ = false;
    Vec2 term_size_ = {80, 24};
    bool use_nerd_fonts_ = true;
    
    // Mouse tracking state (SGR)
    int mouse_x_ = 0;
    int mouse_y_ = 0;
    int mouse_button_ = 0;
    bool mouse_press_ = false;
    bool mouse_hover_active_ = false;  // set on mouse motion, cleared on keyboard
    static bool signal_term_restored_;
    static struct termios signal_original_termios_;
    static void signalHandler(int sig);
    static void signalContHandler(int sig);
    static void restoreTerminalForSignal();
    static void installSignals();
    
    // Double buffered rendering buffers
    int render_width_ = 0;
    int render_height_ = 0;
    std::vector<std::vector<Cell>> front_buffer_;  // what's on screen
    std::vector<std::vector<Cell>> back_buffer_;   // what we're drawing to
    std::string output_batch_;
    
    // Engine internals
    void enableRawMode();
    void disableRawMode();
    void queryTerminalSize();
    
    int readKey();
    void handleInput(int key);
    void handleMouseClick();
    bool isMouseHovering(int row, int col, const std::string& text) const;
    
    void update(int delta_ms);
    void render();
    void checkCollisions();
    
    // Ghost Target Calculation & AI Wave Timing
    Vec2 calculateGhostTarget(const Ghost& ghost) const;
    void moveGhost(Ghost& ghost);
    void updateGhostAI(Ghost& ghost, int delta_ms);
    void updateGlobalModeTimer(int delta_ms);
    GhostMode getGlobalMode() const;
    
    // Phase transitions
    void startMainMenu();
    void startGetReady();
    void startPlaying();
    void startDeath();
    void startLevelClear();
    void startGameOver();
    
    // Collision logic
    void eatDot(Vec2 pos);
    void eatPowerPellet();
    void eatGhost(Ghost& ghost);
    void pacmanCaught();
    struct Viewport {
        int start_x = 0;
        int start_y = 0;
        int visible_cols = Config::MAP_WIDTH;
        int visible_rows = Config::MAP_HEIGHT;
        int base_row = 0;
        int base_col = 0;
        bool is_scrolling = false;
    };
    Viewport getViewport() const;
    
    // Renderer double buffering
    void initRenderer();
    void setCell(int row, int col, const Cell& cell);
    void fillRow(int row, Color fg, Color bg);
    static size_t utf8SequenceLength(unsigned char c) noexcept;
    void drawString(int row, int col, const std::string& text, Color fg, Color bg = {0,0,0}, bool bold = false);
    void drawGradientString(int row, int col, const std::string& text, Color start_fg, Color end_fg, Color bg = {0,0,0});
    void drawBox(int row, int col, int w, int h, Color fg, Color bg = {0,0,0});
    void drawDoubleBorderBox(int row, int col, int w, int h, Color fg, Color bg = {0,0,0});
    void clearBuffer(Color bg = {0, 0, 0});
    void presentFrame();
    
    // Drawing components
    void renderMap();
    void renderEntities();
    void renderHUD();
    void renderEffects();
    void renderMainMenu();
    void renderSettings();
    void renderRedeem();
    void renderStats();
    void renderGameOver();
    void renderGetReady();
    void renderDevMenu();
    void renderDevPasswordInput();
    void activateSettingsSelection();
    void startLevel(int lvl);

    // High Score and Cheats/Dev Options
    bool immortal_ = false;
    bool muted_ = false;
    bool cheat_freeze_ghosts_ = false;
    bool cheat_super_speed_ = false;
    int dev_menu_selection_ = 0;
    std::string dev_input_sequence_ = "";
    std::string dev_password_buffer_ = "";
    GamePhase pre_dev_phase_ = GamePhase::MainMenu;
    void addScore(int points);
    void saveHighScore();
    void loadHighScore();
    std::filesystem::path getCacheFilePath();
    bool isAzertyLayout();
    
    // Campaign, Level Selector & Theme Mechanics
    int max_unlocked_level_ = 1;
    int selected_pacman_color_ = 0; // 0 = Yellow, 1 = Cyan, 2 = Green, 3 = Pink, 4 = Red, 5 = Rainbow
    int level_select_cursor_ = 0;
    void renderLevelSelector();
    
    bool unlocked_rainbow_ = false;
    int selected_general_theme_ = 0; // 0 = Classic, 1 = Cyan, 2 = Green, 3 = Pink, 4 = Red, 5 = Rainbow
    int settings_selection_ = 0;
    bool apply_menu_theme_ = false;
    std::string username_ = "Wael";
    std::string input_username_ = "";
    std::string redeem_input_ = "";
    std::string redeem_result_ = "";
    bool redeem_result_valid_ = false;
    int games_played_ = 0;
    int dots_eaten_ = 0;
    int ghosts_eaten_ = 0;
    int deaths_ = 0;
    int power_pellets_ = 0;
    int time_played_ms_ = 0;
    Color getRainbowColor(double offset) const;
    Color applyGeneralTheme(Color fg, int row, int col) const;
    bool isColorLocked(int color_idx) const;
    
    // Warp Portals check for Pac-man (Theme 3 - Cyberpunk)
    Vec2 portal_A1_ = {0, 0};
    Vec2 portal_A2_ = {0, 0};
    Vec2 portal_B1_ = {0, 0};
    Vec2 portal_B2_ = {0, 0};
    bool pac_just_warped_ = false;

    // Acid trails (Theme 2 - Toxic Green)
    struct AcidTrail {
        Vec2 pos;
        int lifetime_ms;
    };
    std::vector<AcidTrail> acid_trails_;
    
    // Lava (Theme 4 - Lava Orange)
    struct LavaTile {
        Vec2 pos;
        int warning_ms; // warning countdown
        int active_ms;  // active duration
    };
    std::vector<LavaTile> lava_tiles_;
    int lava_spawn_timer_ = 2000;
    
    // Dash (Theme 5 - Cybernetic Gold)
    int dash_cooldown_ = 0;
    
    // Timed special item spawning
    bool special_item_active_ = false;
    Vec2 special_item_pos_;
    TileType special_item_type_;
    int special_item_timer_ = 0;
    
    int cherry_spawn_timer_ = 0;
    int apple_spawn_timer_ = 0;
    int heart_spawn_timer_ = 0;
    
    void spawnSpecialItem(TileType type);
    void spawnScorePopup(Vec2 pos, int points, Color fg);
    void spawnParticleBurst(Vec2 pos, Color color);
    
    // Click feedback flash
    int click_feedback_timer_ = 0;
    
    // Animation controller for fade effects
    AnimationController fade_animation_;
    
    // AFK Screensaver Variables
    int afk_timer_ = 0;
    uint64_t current_time_ms_ = 0;
    double screensaver_x_ = -10.0;
    int screensaver_dir_ = 1;
    void renderScreensaver();
    
    // Popups, Particles, and Status Effects
    std::vector<FloatingPopup> popups_;
    std::vector<Particle> particles_;
    int speed_boost_timer_ = 0;
    int ghost_freeze_timer_ = 0;

    // Keybindings configuration
    std::unordered_map<int, GameAction> key_to_action_;
    void loadKeybindings();
    void rebuildKeybindings();
    void renderKeyConfig();
    std::string getKeyName(int k);
    
    // Custom keys & configuration state
    int custom_key_up_ = 'w';
    int custom_key_down_ = 's';
    int custom_key_left_ = 'a';
    int custom_key_right_ = 'd';
    int custom_key_pause_ = 'p';
    int key_config_selection_ = 0;
    bool is_binding_ = false;
    GameAction binding_action_ = GameAction::None;

    // Audio synthesis & playback
    void generateSounds();
    void playSound(const std::string& name);
    std::filesystem::path getSoundDirectory();

    // Single process-wide PRNG (seeded once) replacing hidden statics & std::rand()
    std::mt19937 rng_;
};

