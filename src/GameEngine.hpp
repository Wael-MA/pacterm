// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.4.0
#pragma once

#include "Types.hpp"
#include "Pacman.hpp"
#include "Ghost.hpp"
#include "I18n.hpp"
#include <array>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include <termios.h>
#include <csignal>
#include <unordered_map>
#include <filesystem>

enum class GameAction : uint8_t { None = 0, Up = 1, Down = 2, Left = 3, Right = 4, Pause = 5 };

enum class LevelTheme : uint8_t {
    Classic     = 0,
    Cyan        = 1,
    Green       = 2,
    Pink        = 3,
    Red         = 4,
    Violet      = 5,
    Ice         = 6,
    Amber       = 7,
    Rainbow     = 8,
    Glitch      = 9,
    PacTermPlus = 10,
};

enum class PowerupKind : uint8_t {
    None         = 0,
    PacSpeed     = 1,
    GhostSlow    = 2,
    DotBonus     = 3,
    PelletBonus  = 4,
    GhostBonus   = 5,
    DashRapid    = 6,
    LavaResist   = 7,
    BlitzBounty  = 8,
    FreezeLinger = 9,
    WarpStun     = 10,
    GlitchLuck   = 11,
    GlitchWarp   = 12,
};

struct Powerup {
    const char* name = nullptr;
    PowerupKind kind = PowerupKind::None;
};

struct AnimationController {
    enum class State { Idle, FadingIn, Active };

    State state     = State::Idle;
    int elapsed_ms_ = 0;
    int duration_ms = 0;
    Color start_color;
    Color end_color;
    float progress = 0.0f;

    void fadeIn(const Color& color, int duration_ms);
    void update(int delta_ms);
    Color getCurrentColor() const;
};

class GameEngine {
public:
    struct TerminalSession {
        struct termios orig_termios{};
        bool raw_active = false;
        TerminalSession();
        ~TerminalSession() noexcept;
        void restore() noexcept;
        static TerminalSession*& instance() noexcept;
    };

    GameEngine();
    ~GameEngine();

    void run();
    static bool install_bin(bool cli_mode = false);
    static bool delete_bin(bool cli_mode = false);
    static std::filesystem::path localBinPath();
    bool isInstalledLocally() const;

private:
    GamePhase phase_ = GamePhase::MainMenu;
    Map map_;
    PacMan pacman_;
    std::array<Ghost, 4> ghosts_;

    int score_                = 0;
    int high_score_           = 0;
    int lives_                = Config::INITIAL_LIVES;
    int level_                = 1;
    int pause_menu_selection_ = 0;
    int ghosts_eaten_combo_   = 0;
    bool extra_life_awarded_  = false;

    int main_menu_selection_       = 0;
    std::string main_menu_message_ = "";
    int main_menu_msg_timer_ms_    = 0;

    bool running_             = true;
    int phase_timer_ms_       = 0;
    int pac_move_accumulator_ = 0;
    std::array<int, 4> ghost_accumulators_{};
    int global_mode_timer_ms_ = 0;
    size_t current_wave_      = 0;

    TerminalSession terminal_session_;

    Vec2 term_size_        = {80, 24};
    bool use_nerd_fonts_   = true;

    int mouse_x_             = 0;
    int mouse_y_             = 0;
    int mouse_button_        = 0;
    bool mouse_press_        = false;
    bool mouse_hover_active_ = false;

    int render_width_  = 0;
    int render_height_ = 0;
    std::vector<std::vector<Cell>> front_buffer_;
    std::vector<std::vector<Cell>> back_buffer_;
    std::string output_batch_;

    void queryTerminalSize();

    int readKey();
    void handleInput(int key);
    void handleMouseClick();
    bool isMouseHovering(int row, int col, std::string_view text) const noexcept;

    void update(int delta_ms);
    void render();
    void checkCollisions();

    Vec2 calculateGhostTarget(const Ghost& ghost) const;
    void moveGhost(Ghost& ghost);
    void updateGhostAI(Ghost& ghost, int delta_ms);
    void updateGlobalModeTimer(int delta_ms);
    GhostMode getGlobalMode() const;

    void startMainMenu();
    void startGetReady();
    void startPlaying();
    void startDeath();
    void startLevelClear();
    void finishLevelClear();
    void startGameOver();

    void eatDot();
    void eatPowerPellet();
    void eatGhost(Ghost& ghost);
    void pacmanCaught();

    void spawnLetter();
    void collectLetter(int letter_idx, Vec2 pos);

    void triggerFever();
    void spawnGhostTrail(Vec2 pos);
    double getActiveScoreMultiplier() const;

    int computeLevelRating(double elapsed_s, double par_s, double& penalty_out) const;
    struct Viewport {
        int start_x       = 0;
        int start_y       = 0;
        int visible_cols  = Config::MAP_WIDTH;
        int visible_rows  = Config::MAP_HEIGHT;
        int base_row      = 0;
        int base_col      = 0;
        bool is_scrolling = false;
    };
    Viewport getViewport() const;

    void initRenderer();
    void setCell(int row, int col, const Cell& cell);
    void setTileGlyph(int row, int col, std::string glyph, Color fg, Color bg = {0, 0, 0}, bool bold = false);
    void fillRow(int row, Color fg, Color bg);
    static size_t utf8SequenceLength(unsigned char c) noexcept;
    size_t glyphCount(std::string_view text) const noexcept;
    size_t displayWidth(std::string_view text) const noexcept;
    std::string truncateText(std::string_view text, size_t max_width) const;
    void drawString(int row, int col, std::string_view text, Color fg, Color bg = {0, 0, 0}, bool bold = false);
    void drawGradientString(int row, int col, std::string_view text, Color start_fg, Color end_fg, Color bg = {0, 0, 0});
    void drawBox(int row, int col, int w, int h, Color fg, Color bg = {0, 0, 0});
    void drawDoubleBorderBox(int row, int col, int w, int h, Color fg, Color bg = {0, 0, 0});
    void drawTitleBorderBox(int row, int col, int w, int h, std::string_view title, Color border_fg, Color title_fg, Color bg = {0, 0, 0});
    void drawTitleBorderBox(int row, int col, int w, int h, std::string_view title, Color fg);
    void clearBuffer(Color bg = {0, 0, 0});
    void presentFrame();

    void renderMap(const Viewport* vp = nullptr);
    void renderEntities(const Viewport* vp = nullptr);
    void renderHUD();
    void renderEffects(const Viewport* vp = nullptr);
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

    bool immortal_                   = false;
    bool muted_                      = false;
    bool cheat_freeze_ghosts_        = false;
    bool cheat_super_speed_          = false;
    int dev_menu_selection_          = 0;
    std::string dev_input_sequence_  = "";
    std::string dev_password_buffer_ = "";
    GamePhase pre_dev_phase_         = GamePhase::MainMenu;
    void addScore(int points);
    void saveHighScore();
    void loadHighScore();
    std::filesystem::path getCacheFilePath();
    bool isAzertyLayout();

    int max_unlocked_level_    = 1;
    int selected_pacman_color_ = 0;
    int level_select_cursor_   = 0;
    void renderLevelSelector();

    bool unlocked_rainbow_      = false;
    int selected_general_theme_ = 0;
    int settings_selection_     = 0;
    bool apply_menu_theme_      = false;
    bool menu_accent_           = false;
    std::string username_       = "Wael";
    std::string input_username_ = "";
    std::string redeem_input_   = "";
    std::string redeem_result_  = "";
    bool redeem_result_valid_   = false;
    int games_played_           = 0;
    int dots_eaten_             = 0;
    int ghosts_eaten_           = 0;
    int deaths_                 = 0;
    int power_pellets_          = 0;
    int time_played_ms_         = 0;
    Color getRainbowColor(double offset) const;
    Color themePrimary(int theme, double offset) const;
    Color themeAccent(int theme, double offset) const;
    Color pacManColor(double offset) const;
    Color tileAccentColor(int x, int y) const;
    Color applyGeneralTheme(Color fg, int row, int col) const;
    Color applyGeneralThemeGradient(Color fg, int row, int col, bool is_gradient_start) const;

    enum class BrightnessTier : uint8_t { VeryDim, Dim, Normal, Bright, VeryBright };
    BrightnessTier getBrightnessTier(Color fg) const;
    Color applyGeneralThemeImpl(Color fg, double offset) const;
    bool isColorLocked(int color_idx) const;
    bool isGlitchZone(int x, int y) const;

    LevelTheme themeForLevel(int lvl) const;
    Color levelThemeColor(int lvl) const;
    bool hasPowerup(PowerupKind kind) const;
    void loadThemePowerups();
    std::vector<Vec2> reachableTiles() const;

    LevelTheme current_theme_ = LevelTheme::Classic;
    std::array<Powerup, 2> current_powerups_{};

    int lava_resist_cooldown_ms_ = 0;
    bool lava_resist_active_     = false;
    int lava_resist_window_ms_   = 0;
    int warp_stun_timer_ms_      = 0;
    int glitch_warp_timer_ms_    = 0;

    Vec2 portal_A1_       = {0, 0};
    Vec2 portal_A2_       = {0, 0};
    Vec2 portal_B1_       = {0, 0};
    Vec2 portal_B2_       = {0, 0};
    bool pac_just_warped_ = false;

    struct AcidTrail {
        Vec2 pos;
        int lifetime_ms;
    };
    std::vector<AcidTrail> acid_trails_;

    struct LavaTile {
        Vec2 pos;
        int warning_ms;
        int active_ms;
    };
    std::vector<LavaTile> lava_tiles_;
    int lava_spawn_timer_ms_ = 2000;

    int dash_cooldown_ = 0;

    int ghost_blitz_timer_ms_ = 0;
    int ghost_blitz_cooldown_ = 0;

    int ice_freeze_cooldown_ = 0;

    void spawnScorePopup(Vec2 pos, int points, Color fg);
    void spawnParticleBurst(Vec2 pos, Color color);

    int click_feedback_timer_ms_ = 0;

    AnimationController fade_animation_;

    int afk_timer_ms_         = 0;
    uint64_t current_time_ms_ = 0;
    double screensaver_x_     = -10.0;
    int screensaver_dir_      = 1;
    void renderScreensaver();

    std::vector<FloatingPopup> popups_;
    std::vector<Particle> particles_;
    int speed_boost_timer_ms_ = 0;
    int ice_freeze_timer_ms_  = 0;

    int fever_timer_ms_ = 0;
    bool fever_active_  = false;

    int ghost_freeze_timer_ms_      = 0;
    int pac_speed_timer_ms_         = 0;
    int letter_score_mult_timer_ms_ = 0;

    bool bonus_fruit_active_ = false;
    Vec2 bonus_fruit_pos_{};
    TileType bonus_fruit_type_ = TileType::Cherry;
    int bonus_fruit_timer_ms_ = 0;
    int fruit_magnet_timer_ms_ = 0;
    bool fruit_shield_active_ = false;
    int fruit_double_bounty_timer_ms_ = 0;
    void spawnBonusFruit();
    void updateBonusFruit(int delta_ms);
    void eatBonusFruit(TileType type);

    GamePhase pre_theme_info_phase_ = GamePhase::Paused;
    void renderThemeInfo();

    LetterHuntState letter_hunt_;
    bool pacterm_plus_unlocked_ = false;

    uint64_t level_start_time_ms_ = 0;
    int level_deaths_             = 0;

    std::unordered_map<int, GameAction> key_to_action_;
    void rebuildKeybindings();
    void renderKeyConfig();
    std::string getKeyName(int k);

    int custom_key_up_         = 'w';
    int custom_key_down_       = 's';
    int custom_key_left_       = 'a';
    int custom_key_right_      = 'd';
    int custom_key_pause_      = 'p';
    int key_config_selection_  = 0;
    bool is_binding_           = false;
    GameAction binding_action_ = GameAction::None;

    void generateSounds();
    void playSound(const std::string& name);
    std::filesystem::path getSoundDirectory();

    std::mt19937 rng_;
};
