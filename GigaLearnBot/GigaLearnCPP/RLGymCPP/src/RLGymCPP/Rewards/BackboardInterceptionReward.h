#pragma once
#include "Reward.h"
#include "../CommonValues.h"
#include <unordered_map>
#include <cmath>

namespace RLGC {

// ============================================================================
// BackboardInterceptionReward
// 2026-06-21 | Rev 1.0
//
// Rewards the defender for touching the ball BEFORE it reaches the defensive
// backboard on a trajectory that would produce a high backboard bounce (above
// the goal crossbar). After such a bounce the attacker gets a high-percentage
// redirect; intercepting early removes that advantage.
//
// Trigger (on player touch):
//   Pre-touch ball (state.prev) heading toward own backboard at > 200 UU/s,
//   time-to-wall 0–1.5s, projected Z at wall: GOAL_HEIGHT+100 → CEILING-200,
//   ball current distance from own backboard > 300 UU (not already there).
//
// Anti-farming: event-based (touch only), per-player 3s cooldown.
// ZeroSum: applied by caller (0.5f recommended).
// Does NOT fire when ball is already on the backboard (BackboardDefenseReward
// handles that case). Zero trigger overlap with ImpendingShotDefenseReward
// (which requires projected Z < GOAL_HEIGHT, i.e. into the goal mouth).
// ============================================================================
class BackboardInterceptionReward : public Reward {
    static constexpr float MIN_SPEED_TOWARD_WALL = 200.0f;  // UU/s
    static constexpr float HORIZON_SECS         = 1.5f;
    static constexpr float Z_MIN_ABOVE_GOAL     = 100.0f;   // projected Z must exceed GOAL_HEIGHT by this
    static constexpr float Z_CEILING_MARGIN     = 200.0f;   // don't count near-ceiling hits
    static constexpr float MIN_DIST_FROM_WALL   = 300.0f;   // ball must not already be at wall
    static constexpr float COOLDOWN_SECS        = 3.0f;
    static constexpr float BASE_REWARD          = 6.0f;
    static constexpr float EARLY_BONUS          = 4.0f;     // bonus when intercepting with >50% horizon remaining

    mutable std::unordered_map<uint32_t, float> _cooldown;

    bool BallWasHeadingToBackboardAboveCrossbar(
        const Player& player, const GameState& state, float& tOut) const
    {
        const float goalSign  = (player.team == Team::BLUE) ? -1.0f : 1.0f;
        const float backwallY = goalSign * CommonValues::BACK_WALL_Y;

        const Vec& v0 = state.prev->ball.vel;
        const Vec& p0 = state.prev->ball.pos;

        const float vyToward = -goalSign * v0.y;
        if (vyToward < MIN_SPEED_TOWARD_WALL) return false;

        const float t = (backwallY - p0.y) / v0.y;
        if (t <= 0.0f || t > HORIZON_SECS) return false;

        const float zAtWall = p0.z + v0.z * t
            + 0.5f * CommonValues::BALL_GRAVITY * t * t;

        if (zAtWall < CommonValues::GOAL_HEIGHT + Z_MIN_ABOVE_GOAL) return false;
        if (zAtWall > CommonValues::CEILING_Z - Z_CEILING_MARGIN) return false;

        const float distToWall = std::fabs(state.ball.pos.y - backwallY);
        if (distToWall < MIN_DIST_FROM_WALL) return false;

        tOut = t;
        return true;
    }

public:
    virtual void Reset(const GameState&) override { _cooldown.clear(); }

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (player.isDemoed || !player.ballTouchedStep || !state.prev) return 0.0f;

        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        float& cd = _cooldown[player.carId];
        if (cd > 0.0f) { cd = RS_MAX(0.0f, cd - dt); return 0.0f; }

        float tRemaining = 0.0f;
        if (!BallWasHeadingToBackboardAboveCrossbar(player, state, tRemaining)) return 0.0f;

        float earlinessBonus = (tRemaining > HORIZON_SECS * 0.5f) ? EARLY_BONUS : 0.0f;
        float reward = BASE_REWARD + earlinessBonus;

        cd = COOLDOWN_SECS;
        return reward;
    }
};

} // namespace RLGC
