// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.9
#pragma once

#include "Types.hpp"

class PacMan : public Entity {
public:
    PacMan() { reset(); }

    void reset() noexcept override {
        spawn_position_    = Config::PACMAN_SPAWN;
        position           = spawn_position_;
        currentDirection   = Direction::Left;
        requestedDirection = Direction::None;
        alive_             = true;
        anim_frame_        = 0;
    }

    bool tryMove(const Map& map) noexcept {
        bool moved = false;

        if (!map.isWalkable(position)) {
            position = map.findNearestWalkable(position);
        }

        if (requestedDirection != Direction::None) {
            Vec2 next_pos = position + directionToVec2(requestedDirection);
            if (map.isWalkable(next_pos)) {
                currentDirection   = requestedDirection;
                requestedDirection = Direction::None;
            }
        }

        if (currentDirection != Direction::None) {
            Vec2 next_pos = position + directionToVec2(currentDirection);
            if (map.isWalkable(next_pos)) {
                position = next_pos;
                moved    = true;
            }
        }

        position = map.wrapTunnel(position);

        return moved;
    }

    [[nodiscard]] int animFrame() const noexcept { return anim_frame_; }

    void advanceAnim() noexcept { anim_frame_ = (anim_frame_ + 1) % 4; }

    [[nodiscard]] bool isAlive() const noexcept { return alive_; }

    void kill() noexcept { alive_ = false; }

private:
    bool alive_     = true;
    int anim_frame_ = 0;
};
