// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.5
#pragma once

#include "Types.hpp"

class Ghost : public Entity {
public:
    GhostPersonality personality;
    GhostMode mode = GhostMode::InHouse;

    int dotCounter = 0;      // for ghost house release logic

    Ghost(GhostPersonality personality)
        : personality(personality) {
        reset();
    }

    void reset() override {
        frightened_remaining_ = 0;
        currentDirection = Direction::None;
        requestedDirection = Direction::None;
        dotCounter = 0;

        switch (personality) {
            case GhostPersonality::Blinky:
                spawn_position_ = Config::BLINKY_SPAWN;
                // Blinky starts outside the house
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

    // Frightened timer setup
    void frighten(int duration_ms) {
        if (mode != GhostMode::Eaten) {
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
                // Transition will be fully handled by GameEngine (reverts to current wave mode)
                mode = GhostMode::Chase;
            }
        }
    }

    bool isFrightenedFlashing() const {
        return mode == GhostMode::Frightened && frightened_remaining_ <= Config::FRIGHTENED_FLASH_AT;
    }

    // For eaten mode: set target to ghost house
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
            mode = GhostMode::Scatter; // Default back to active mode
            currentDirection = Direction::Left;
        }
    }

private:
    int frightened_remaining_ = 0;
};
