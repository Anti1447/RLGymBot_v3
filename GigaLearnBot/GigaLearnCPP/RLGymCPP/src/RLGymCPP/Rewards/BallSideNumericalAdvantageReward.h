#pragma once
#include "Reward.h"
#include "../Gamestates/GameState.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// BallSideNumericalAdvantageReward (2026-05-30 | Rev 1.1)
// ============================================================================
// Rewards a team when it has more players "behind the ball" (between the ball
// and their own goal) than the opponent. A crude Y-only approximation is used:
//   Blue behind ball:   player.pos.y < ball.pos.y  (blue goal at Y = -5120)
//   Orange behind ball: player.pos.y > ball.pos.y  (orange goal at Y = +5120)
//
// Fires when advantageDiff = (myBehind - oppBehind) > 0.
// Returns advantageDiff * REWARD_PER_PLAYER (0.05f per player of advantage).
//
// Reward table:
//   1v1: me behind, opp in front           → 0.05f
//   2v2: me 2 behind, opp 1 behind         → 0.05f
//   2v2: me 2 behind, opp 0 behind         → 0.10f
//   3v3: me 3 behind, opp 0 behind         → 0.15f  (max)
//   Any: equal counts or opp has more      → 0.0f
//
// Gates:
//   - At least one player from either team must be within CONTESTED_RADIUS (1500 UU)
//     of the ball — prevents passive midfield camping exploit.
//   - Kickoff skipped (ball near center, velocity < 100 UU/s).
//   - Phase 3+ only (not Phase 1/2, where ball-chase signal must stay clean).
//
// ZeroSum wrapper: advantaged team receives +reward, overcommitted team receives
// -reward of same magnitude via ZeroSumReward wrapper in RewardConfig.cpp.
//
// Note on 3v3 scaling: in 3v3 each player independently returns the reward, so
// the total team signal scales as (diff × teamSize × 0.05f). A 3-player advantage
// gives each player +0.15f (net team delta 0.45f/step). This is intentional —
// larger teams have larger positional stakes.
//
// W&B: ball_side_advantage (mean per episode, via GetAndResetMetrics())
// Tuning: if activation rate too low, raise CONTESTED_RADIUS 1500 → 2000 UU.
//         if farming observed, lower to 1000 UU.
// ============================================================================

class BallSideNumericalAdvantageReward : public Reward {
private:
    static constexpr float REWARD_PER_PLAYER = 0.05f;
    static constexpr float CONTESTED_RADIUS  = 1500.0f; // UU — someone must be near ball

    inline static std::atomic<int>   s_fireCount { 0 };
    inline static std::atomic<float> s_rewardSum { 0.0f };

    // True if the player is between the ball and their own goal (Y-axis only).
    static bool IsBehindBall(const Player& p, const GameState& state) {
        return (p.team == Team::BLUE)
            ? (p.pos.y < state.ball.pos.y)   // Blue goal at Y = -5120
            : (p.pos.y > state.ball.pos.y);  // Orange goal at Y = +5120
    }

public:
    struct Metrics { int fireCount = 0; float avgReward = 0.0f; };
    static Metrics GetAndResetMetrics() {
        int   count = s_fireCount.exchange(0);
        float sum   = s_rewardSum.exchange(0.0f);
        return { count, count > 0 ? sum / count : 0.0f };
    }

    virtual void Reset(const GameState& /*initialState*/) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool /*isFinal*/) override {
        if (player.isDemoed) return 0.0f;

        // Skip kickoff: ball near center with near-zero velocity
        if (state.ball.pos.Length() < 300.0f && state.ball.vel.Length() < 100.0f)
            return 0.0f;

        // Contested gate: at least one non-demoed player (either team) must be
        // within CONTESTED_RADIUS of the ball. Prevents passive camping reward.
        bool anyoneContesting = false;
        for (const auto& p : state.players) {
            if (!p.isDemoed && (p.pos - state.ball.pos).Length() < CONTESTED_RADIUS) {
                anyoneContesting = true;
                break;
            }
        }
        if (!anyoneContesting) return 0.0f;

        // Count behind-ball players per team (demoed players excluded — they provide
        // no on-field presence and should not contribute to positional counts).
        int myBehind  = 0;
        int oppBehind = 0;
        for (const auto& p : state.players) {
            if (p.isDemoed) continue;
            if (IsBehindBall(p, state)) {
                if (p.team == player.team) ++myBehind;
                else                       ++oppBehind;
            }
        }

        int advantageDiff = myBehind - oppBehind;
        if (advantageDiff <= 0) return 0.0f;

        float reward = static_cast<float>(advantageDiff) * REWARD_PER_PLAYER;

        s_fireCount.fetch_add(1);
        float prev = s_rewardSum.load();
        while (!s_rewardSum.compare_exchange_weak(prev, prev + reward)) {}

        return reward;
    }
};

} // namespace RLGC
