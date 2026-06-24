#pragma once
#include "Reward.h"
#include "../Math.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// LastManOvercommitPenalty
// 2026-06-20 | Rev 1.0 | NJH
//
// Penalizes the player closest to own goal (last man) for rushing the ball
// instead of shadow defending. Fills the gap in DefensivePositioningChallengeReward,
// which returns 0.0f (not negative) when the last man is < 2000 UU from ball.
//
// Design:
//   - Stateless — no PreStep, no maps
//   - 2v2+ only (state.players.size() >= 4)
//   - Fires when: team defending AND player is last man AND ball in own half
//   - Emergency exception: ball within 1800 UU of own goal center (allow save)
//   - Penalty ramps linearly from 0 at SHADOW_START_DIST (900 UU) to
//     -MAX_PENALTY at FULL_PENALTY_DIST (150 UU)
//
// Complements:
//   - DefensivePositioningChallengeReward — gives +reward for staying back
//   - ShadowDefenseVsPossessionReward — gives +reward for shadow geometry
//
// Overlaps:
//   - AdvancedOvercommitmentPenalty: partial — that is boost/trajectory-based;
//     this is role-specific and position-based
// ============================================================================

class LastManOvercommitPenalty : public Reward {
    static constexpr float SHADOW_START_DIST = 900.0f;   // below this = overcommitting
    static constexpr float FULL_PENALTY_DIST = 150.0f;   // at or below this = max penalty
    static constexpr float MAX_PENALTY       = 0.4f;     // max return magnitude (outer weight scales this)
    static constexpr float EMERGENCY_DIST    = 1800.0f;  // ball within this of own goal → allow commitment

    Vec GetOwnGoalPosition(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::BLUE_GOAL_CENTER
            : CommonValues::ORANGE_GOAL_CENTER;
    }

    Vec GetOppGoalPosition(Team team) const {
        return (team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_CENTER
            : CommonValues::BLUE_GOAL_CENTER;
    }

    // Ball is closer to own goal than opponent goal (defensive scenario)
    bool IsTeamDefending(const Player& player, const GameState& state) const {
        Vec ownGoal = GetOwnGoalPosition(player.team);
        Vec oppGoal = GetOppGoalPosition(player.team);
        return state.ball.pos.Dist(ownGoal) < state.ball.pos.Dist(oppGoal);
    }

    // This player has the smallest distance to own goal among living teammates
    bool IsLastManBack(const Player& player, const GameState& state) const {
        Vec ownGoal = GetOwnGoalPosition(player.team);
        float myDist = player.pos.Dist(ownGoal);
        for (const auto& tm : state.players) {
            if (tm.team != player.team) continue;
            if (tm.carId == player.carId) continue;
            if (tm.isDemoed) continue;
            if (tm.pos.Dist(ownGoal) < myDist) return false;
        }
        return true;
    }

public:
    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        // 2v2+ only
        if (state.players.size() < 4) return 0.0f;
        if (player.isDemoed) return 0.0f;

        // Kickoff gate
        const bool isKickoff = (state.ball.pos.Length() < 200.0f
                                && state.ball.vel.Length() < 150.0f);
        if (isKickoff) return 0.0f;

        // Defensive scenario only
        if (!IsTeamDefending(player, state)) return 0.0f;

        // Must be the last man
        if (!IsLastManBack(player, state)) return 0.0f;

        // Emergency save exception — ball threatening own goal
        Vec ownGoal = GetOwnGoalPosition(player.team);
        if (state.ball.pos.Dist(ownGoal) < EMERGENCY_DIST) return 0.0f;

        // Penalty ramp: 0 at SHADOW_START_DIST, -MAX_PENALTY at FULL_PENALTY_DIST
        float ballDist = player.pos.Dist(state.ball.pos);
        if (ballDist >= SHADOW_START_DIST) return 0.0f;

        float t = RS_CLAMP(
            (SHADOW_START_DIST - ballDist) / (SHADOW_START_DIST - FULL_PENALTY_DIST),
            0.0f, 1.0f);
        return -MAX_PENALTY * t;
    }
};

} // namespace RLGC
