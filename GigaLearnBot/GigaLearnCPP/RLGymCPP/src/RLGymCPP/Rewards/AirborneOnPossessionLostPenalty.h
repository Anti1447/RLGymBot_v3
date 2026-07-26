#pragma once
#include "Reward.h"

namespace RLGC {

// ============================================================================
// AirborneOnPossessionLostPenalty
// 2026-06-26 | Rev 1.0 | NJH
//
// Penalizes our bot for being airborne at the exact moment ball possession
// switches to the opposing team (state.lastTouchCarID changes to an opponent).
//
// Rationale:
//   When the opponent gains possession while our bot is in the air, we cannot
//   challenge, shadow, or reposition until we land. This is one of the most
//   costly positional errors in team play — the opponent has full court control
//   for the duration of our air time.
//
// Trigger:
//   state.lastTouchCarID just changed this step (prev != curr)
//   AND the new toucher is on the opposing team
//   AND player.isOnGround == false
//   AND we are not the new toucher ourselves
//   AND NOT kickoff
//
// Uses state.prev->lastTouchCarID to detect the change — no per-step member
// state required. state.prev is set by the training loop before GetReward().
//
// Anti-farming: this is a penalty — the bot wants to avoid it.
//   - Gates: only fires on genuine possession-switch frames (sparse events).
//   - Does not fire when the bot is the one touching the ball.
//
// ZeroSum: No — individual behavioral penalty.
// W&B metric: airborne_on_possession_lost_penalty_rate
//
// Complements:
//   - FlipDuringContestPenalty (fires during flip; this fires on possession change)
//   - GroundedAtContactReward (positive signal for same grounding goal)
//   - LastManOvercommitPenalty (role-specific; this is game-state-specific)
// ============================================================================
class AirborneOnPossessionLostPenalty : public Reward {
    static constexpr float PENALTY = 0.75f;

public:
    virtual void Reset(const GameState&) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        if (!state.prev) return 0.0f;

        int prevTouch = state.prev->lastTouchCarID;
        int currTouch = state.lastTouchCarID;

        // Possession must have just changed this step
        if (currTouch == prevTouch || currTouch < 0) return 0.0f;

        // We are not the new toucher
        if (player.carId == (uint32_t)currTouch) return 0.0f;

        // Kickoff gate
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f) return 0.0f;

        // Find the new toucher and confirm they're on the opposing team
        for (const auto& p : state.players) {
            if (p.carId != (uint32_t)currTouch) continue;
            if (p.team == player.team) return 0.0f;  // teammate gained possession — not penalized
            break;
        }

        // Bot must be airborne
        if (player.isOnGround) return 0.0f;

        return -PENALTY;
    }
};

} // namespace RLGC
