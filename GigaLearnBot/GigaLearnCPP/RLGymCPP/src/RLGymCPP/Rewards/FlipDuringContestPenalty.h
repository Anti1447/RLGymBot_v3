#pragma once
#include "Reward.h"

namespace RLGC {

// ============================================================================
// FlipDuringContestPenalty
// 2026-06-26 | Rev 1.0 | NJH
//
// Fires once per flip episode when our bot is mid-flip AND any other player
// (teammate or opponent) is within CONTEST_RADIUS of the ball.
//
// Problem it solves:
//   Flipping locks the car for ~400ms. If our bot flips while another player
//   is contesting the ball, we lose that reaction window — we can't reposition,
//   block, or challenge until the flip animation completes.
//
// Trigger:
//   player.isFlipping == true
//   AND any other living player has dist(ball, player) < 400 UU
//   AND NOT kickoff (ball.pos.Length() >= 300 or ball.vel.Length() >= 200)
//
// Anti-farming: this is a penalty — the bot wants to avoid it. The contest
//   check (other player near ball) means productive approach flips toward the
//   ball where no one else is nearby are not penalized.
//
// Cooldown: 600ms prevents stacking when the bot does several short flips in
//   quick succession. Counter ticks every step regardless of condition.
//
// Does NOT fire when:
//   - The bot is the only player near the ball (no other player < 400 UU)
//   - During kickoff
//   - During the cooldown window after a prior trigger
//
// ZeroSum: No — individual behavioral penalty.
// W&B metric: flip_during_contest_penalty_rate
//
// Complements:
//   - GroundedAtContactReward (positive reinforcement for same goal)
//   - NonPrimaryAirPenalty (penalizes off-ball airtime generally)
//   - SecondaryAerialPenalty (penalizes secondary aerial when ball is high)
// ============================================================================
class FlipDuringContestPenalty : public Reward {
    static constexpr float CONTEST_RADIUS = 400.0f;  // UU from ball: "in contest"
    static constexpr float PENALTY        = 0.5f;
    static constexpr float COOLDOWN_SEC   = 0.6f;

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

        if (!player.isFlipping) return 0.0f;

        // Kickoff gate
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 200.0f) return 0.0f;

        for (const auto& p : state.players) {
            if (p.carId == player.carId || p.isDemoed) continue;
            if ((state.ball.pos - p.pos).Length() < CONTEST_RADIUS) {
                cd = COOLDOWN_SEC;
                return -PENALTY;
            }
        }

        return 0.0f;
    }
};

} // namespace RLGC
