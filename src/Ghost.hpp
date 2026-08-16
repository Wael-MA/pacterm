// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.8
#pragma once

#include "Types.hpp"

class Ghost : public Entity {
public:
    GhostPersonality personality;
    GhostMode mode = GhostMode::InHouse;

    int dotCounter = 0;
    Vec2 prevPosition = {-1, -1};

    Ghost(GhostPersonality personality)
        : personality(personality) {
        reset();
    }

    void reset() override {
        frightened_remaining_ = 0;
        currentDirection = Direction::None;
        requestedDirection = Direction::None;
        dotCounter = 0;
        prevPosition = {-1, -1};

        switch (personality) {
            case GhostPersonality::Blinky:
                spawn_position_ = Config::BLINKY_SPAWN;
                mode = GhostMode::Scatter;
                currentDirection = Direction::Left;
                break;
            case GhostPersonality::Pinky:
                spawn_position_ = Config::PINKY_SPAWN;
                mode = GhostMode::InHouse;
                break;
            case GhostPersonality::Inky:
                spawn_position_ = Config::INKY_SPAWN;
                mode = GhostMode::InHouse;
                break;
            case GhostPersonality::Clyde:
                spawn_position_ = Config::CLYDE_SPAWN;
                mode = GhostMode::InHouse;
                break;
        }
        position = spawn_position_;
    }

    Direction reverseDirection() const {
        return getOppositeDirection(currentDirection);
    }

    void frighten(int duration_ms) {
        if (mode != GhostMode::Eaten && mode != GhostMode::InHouse) {
            mode = GhostMode::Frightened;
            frightened_remaining_ = duration_ms;
            currentDirection = reverseDirection();
        }
    }

    void updateFrightened(int delta_ms) {
        if (mode == GhostMode::Frightened) {
            frightened_remaining_ -= delta_ms;
            if (frightened_remaining_ <= 0) {
                frightened_remaining_ = 0;
                mode = GhostMode::Chase;
            }
        }
    }

    bool isFrightenedFlashing() const {
        return mode == GhostMode::Frightened && frightened_remaining_ <= Config::FRIGHTENED_FLASH_AT;
    }

    void setEaten() {
        mode = GhostMode::Eaten;
        frightened_remaining_ = 0;
    }

    bool hasReachedHouse() const {
        return position == Config::GHOST_HOUSE_EXIT || position == Config::GHOST_HOUSE_CENTER;
    }

    void exitHouse() {
        if (mode == GhostMode::InHouse) {
            position = Config::GHOST_HOUSE_EXIT;
            mode = GhostMode::Scatter;
            currentDirection = Direction::Left;
        }
    }

private:
    int frightened_remaining_ = 0;
};
