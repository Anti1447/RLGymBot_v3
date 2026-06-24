#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <limits>

namespace RLGC {

// ============================================================================
// OpponentInterceptAwarePenalty
// 2026-06-20 | Rev 1.0
//
// Penalizes chasing the ball when the nearest opponent clearly wins the ball
// race. Without this, the bot commits to the ball's predicted trajectory even
// when an opponent will intercept it first, leaving defense open.
//
// Penalty scales with how hard the player is sprinting toward the ball.
// Emergency save exception: ball within 1500 UU of own goal — always allow.
//
// Complements (not duplicates):
//   LastManOvercommitPenalty — role-specific, 2v2+ only
//   SimpleOvercommitmentPenalty — position-based (distance to ball)
//   DoubleCommitPenalty — multi-player double commitment
// ============================================================================
class OpponentInterceptAwarePenalty : public Reward {
    static constexpr float RACE_WIN_THRESHOLD = 0.70f;   // opp must be <70% of player dist
    static constexpr float MAX_PENALTY        = 0.40f;   // internal max; outer weight scales
    static constexpr float EMERGENCY_DIST     = 1500.0f; // ball this close to own goal → skip
    static constexpr float MIN_PLAYER_DIST    = 800.0f;  // already close enough to contest

    Vec GetOwnGoalPos(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_CENTER
            : CommonValues::ORANGE_GOAL_CENTER;
    }

public:
    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (player.isDemoed) return 0.0f;

        // Kickoff gate
        if (state.ball.pos.Length() < 200.0f && state.ball.vel.Length() < 150.0f)
            return 0.0f;

        // Emergency: ball threatening own goal — never suppress a save attempt
        Vec ownGoal = GetOwnGoalPos(player.team);
        if (state.ball.pos.Dist(ownGoal) < EMERGENCY_DIST) return 0.0f;

        float playerDist = player.pos.Dist(state.ball.pos);
        if (playerDist < MIN_PLAYER_DIST) return 0.0f;

        // Find closest live opponent to ball
        float closestOppDist = std::numeric_limits<float>::max();
        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            if (opp.isDemoed) continue;
            float d = opp.pos.Dist(state.ball.pos);
            if (d < closestOppDist) closestOppDist = d;
        }
        if (closestOppDist == std::numeric_limits<float>::max()) return 0.0f;

        // Opponent must be winning by RACE_WIN_THRESHOLD margin
        if (closestOppDist >= playerDist * RACE_WIN_THRESHOLD) return 0.0f;

        // Measure how hard the player is committing toward ball
        Vec toBall = state.ball.pos - player.pos;
        float dist = toBall.Length();
        if (dist < 1.0f) return 0.0f;
        toBall = toBall * (1.0f / dist);

        float velTowardBall = player.vel.Dot(toBall);
        if (velTowardBall <= 0.0f) return 0.0f; // already slowing/retreating — no penalty

        float t = RS_CLAMP(velTowardBall / CommonValues::CAR_MAX_SPEED, 0.0f, 1.0f);
        return -MAX_PENALTY * t;
    }
};

} // namespace RLGC
