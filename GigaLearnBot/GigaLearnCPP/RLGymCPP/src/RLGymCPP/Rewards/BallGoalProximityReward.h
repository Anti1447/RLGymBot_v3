// ============================================================================
// BallGoalProximityReward.h
//
// Continuous per-tick reward/penalty based on ball distance to goal centers.
//   +reward when ball is within RADIUS UU of the opponent's goal mouth center
//   -penalty when ball is within RADIUS UU of the own team's goal mouth center
//
// Value scales linearly: 1.0 at the goal center, 0.0 at RADIUS UU away.
// Both zones use the same linear scale — no separate penalty weight.
// The two zones are geometrically far apart; they never co-fire.
//
// Use without ZeroSum — the sign already flips correctly per team
// (what is positive for Blue is also positive for Blue under the same formula;
// both teams evaluate against their own goals independently).
// ============================================================================

#pragma once
#include "Reward.h"
#include "../CommonValues.h"

namespace RLGC {

class BallGoalProximityReward : public Reward {
public:
    struct Config {
        float radius = 1500.0f;  // UU — reward/penalty zone radius
    };

private:
    Config _cfg;

    // Goal mouth center: x=0, y=±GOAL_LINE_Y, z=0
    static constexpr float GOAL_Y = CommonValues::GOAL_LINE_Y;  // 5120.0f

public:
    explicit BallGoalProximityReward(Config cfg = Config{}) : _cfg(cfg) {}

    float GetReward(const Player& player, const GameState& state,
                    bool /*isFinal*/) override {

        const Vec& ball = state.ball.pos;

        // Opponent goal: +Y for Blue, -Y for Orange
        float oppGoalY = (player.team == Team::BLUE) ?  GOAL_Y : -GOAL_Y;
        float ownGoalY = (player.team == Team::BLUE) ? -GOAL_Y :  GOAL_Y;

        float oppDist = Vec(ball.x, ball.y - oppGoalY, ball.z).Length();
        float ownDist = Vec(ball.x, ball.y - ownGoalY, ball.z).Length();

        float reward  = (oppDist < _cfg.radius) ? (1.0f - oppDist / _cfg.radius) : 0.0f;
        float penalty = (ownDist < _cfg.radius) ? (1.0f - ownDist / _cfg.radius) : 0.0f;

        return reward - penalty;
    }
};

} // namespace RLGC
