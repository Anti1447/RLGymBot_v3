#pragma once
#include "Reward.h"

namespace RLGC {

// ============================================================================
// GroundedAtContactReward
// 2026-06-26 | Rev 1.0 | NJH
//
// Positive complement to FlipDuringContestPenalty. Rewards our bot for being
// grounded at the exact moment any OTHER player touches the ball.
//
// Rationale:
//   Being on the ground when the ball is touched means we are maximally
//   reactive — we can boost, dodge, and reposition in any direction immediately.
//   This shapes the bot to land BEFORE plays develop, not during them.
//
// Trigger:
//   player.isOnGround == true
//   AND player.ballTouchedStep == false (we are NOT the toucher)
//   AND any other player has ballTouchedStep == true
//   AND NOT kickoff
//
// Anti-farming: the bot cannot control when others touch the ball, only
//   whether it is grounded. Cooldown 1.0s prevents repeated payout during
//   opponent dribbles where the ball is tapped every few frames.
//
// ZeroSum: No — individual readiness reward.
// W&B metric: grounded_at_contact_reward_rate
//
// Complements:
//   - FlipDuringContestPenalty (negative reinforcement for same goal)
//   - AirborneOnPossessionLostPenalty (deeper defensive variant)
// ============================================================================
class GroundedAtContactReward : public Reward {
    static constexpr float REWARD       = 0.3f;
    static constexpr float COOLDOWN_SEC = 1.0f;

    mutable std::unordered_map<uint32_t, float> _cd;

public:
    virtual void Reset(const GameState&) override { _cd.clear(); }

    virtual float GetReward(const Player& player, const GameState& state, bool) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        float& cd = _cd[player.carId];
        if (cd > 0.0f) {
            cd -= dt;
            if (cd < 0.0f) cd = 0.0f;
            return 0.0f;
        }

        if (!player.isOnGround) return 0.0f;
        if (player.ballTouchedStep) return 0.0f;

        // Kickoff gate
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f) return 0.0f;

        for (const auto& p : state.players) {
            if (p.carId == player.carId) continue;
            if (p.ballTouchedStep) {
                cd = COOLDOWN_SEC;
                return REWARD;
            }
        }

        return 0.0f;
    }
};

} // namespace RLGC
