// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Wael (https://wael.work.gd)
// pacterm v1.3.5
#pragma once

#include "Types.hpp"

class PacMan : public Entity {
public:
    PacMan() {
        reset();
    }

    void reset() override {
        spawn_position_ = Config::PACMAN_SPAWN;
        position = spawn_position_;
        currentDirection = Direction::Left;
        requestedDirection = Direction::None;
        alive_ = true;
        anim_frame_ = 0;
    }

    // Movement: try requestedDirection first; if blocked, continue currentDirection
    // Returns true if position changed
    bool tryMove(const Map& map) {
        bool moved = false;

        // 1. If requestedDirection != None:
        //      next_pos = position + directionToVec2(requestedDirection)
        //      if map.isWalkable(next_pos):
        //          currentDirection = requestedDirection
        //          requestedDirection = None
        if (requestedDirection != Direction::None) {
            Vec2 next_pos = position + directionToVec2(requestedDirection);
            if (map.isWalkable(next_pos)) {
                currentDirection = requestedDirection;
                requestedDirection = Direction::None;
            }
        }

        // 2. If currentDirection != None:
        //      next_pos = position + directionToVec2(currentDirection)
        //      if map.isWalkable(next_pos):
        //          position = next_pos
        //          moved = true
        if (currentDirection != Direction::None) {
            Vec2 next_pos = position + directionToVec2(currentDirection);
            if (map.isWalkable(next_pos)) {
                position = next_pos;
                moved = true;
            }
        }

        // 3. position = map.wrapTunnel(position)
        position = map.wrapTunnel(position);

        return moved;
    }

    // Animation frame for mouth open/close (cycles 0-3)
    int animFrame() const {
        return anim_frame_;
    }

    void advanceAnim() {
        anim_frame_ = (anim_frame_ + 1) % 4;
    }

    bool isAlive() const {
        return alive_;
    }

    void kill() {
        alive_ = false;
    }

private:
    bool alive_ = true;
    int anim_frame_ = 0;
};
