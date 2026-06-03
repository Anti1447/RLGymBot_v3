#pragma once
#include "Reward.h"
#include "../Gamestates/GameState.h"
#include "../Math.h"
#include "RewardMath.h"
#include "../CommonValues.h"

namespace RLGC {

// ============================================================================
// ChallengeAngleQualityReward - Phase 6+ (2026-05-30 | Rev 1.0 | NJH)
// ============================================================================
// Rewards approaching contested balls from the correct angle: the player should
// position between their own goal and the ball (on the threat vector) rather
// than swooping in from the side or behind.
//
// Trigger: player within CHALLENGE_RANGE of ball, closing at > MIN_CLOSING_SPEED,
//          and nearest opponent also closing at > MIN_OPP_CLOSING_SPEED within
//          OPP_RANGE. Requires a genuinely contested ball to avoid farming.
//
// Signal: dot(playerToBall, ballToOppGoal). ballToOppGoal points from the ball
//         toward the opponent's goal, so a high dot product means the player is
//         approaching from the own-goal side — the correct proactive blocking position.
//         Do NOT use ballToOwnGoal (inverted: rewards reactive positioning).
//
// W&B: challenge_angle_quality (mean per episode)
// ============================================================================

class ChallengeAngleQualityReward : public Reward {
private:
    static constexpr float CHALLENGE_RANGE      = 1200.0f; // UU, max dist from ball to fire
    static constexpr float MIN_CLOSING_SPEED    = 200.0f;  // UU/s, player must be closing
    static constexpr float OPP_RANGE            = 2000.0f; // UU, opponent must be within this
    static constexpr float MIN_OPP_CLOSING_SPEED = 150.0f; // UU/s, opponent must also be closing

    // Angle quality: zero below this dot product (side/behind approaches)
    static constexpr float ALIGN_MIN  = 0.2f;
    static constexpr float ALIGN_SPAN = 0.8f; // ramps from ALIGN_MIN to 1.0

    // W&B metrics
    inline static std::atomic<int>   s_fireCount   { 0 };
    inline static std::atomic<float> s_rewardSum   { 0.0f };

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

        // Distance and closing speed — player must be within range and approaching
        Vec  playerToBall    = state.ball.pos - player.pos;
        float distToBall     = playerToBall.Length();
        if (distToBall > CHALLENGE_RANGE || distToBall < 1.0f) return 0.0f;

        Vec  playerToBallDir = playerToBall / distToBall;
        float closingSpeed   = player.vel.Dot(playerToBallDir);
        if (closingSpeed < MIN_CLOSING_SPEED) return 0.0f;

        // Nearest opponent must also be closing on the ball (not just nearby)
        bool oppContesting = false;
        for (const auto& p : state.players) {
            if (p.team == player.team || p.isDemoed) continue;
            Vec  oppToBall = state.ball.pos - p.pos;
            float oppDist  = oppToBall.Length();
            if (oppDist > OPP_RANGE || oppDist < 1.0f) continue;
            float oppClosing = p.vel.Dot(oppToBall / oppDist);
            if (oppClosing >= MIN_OPP_CLOSING_SPEED) { oppContesting = true; break; }
        }
        if (!oppContesting) return 0.0f;

        // Opponent goal position (the goal the player is attacking)
        Vec oppGoal = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_CENTER
            : CommonValues::BLUE_GOAL_CENTER;

        // Angle quality: how aligned is the approach with the threat vector?
        // ballToOppGoal points away from own goal — high dot = player on goal-side of ball
        Vec  ballToOppGoal = (oppGoal - state.ball.pos).Normalized();
        float alignment    = playerToBallDir.Dot(ballToOppGoal);

        float reward = RS_CLAMP((alignment - ALIGN_MIN) / ALIGN_SPAN, 0.0f, 1.0f);
        if (reward <= 0.0f) return 0.0f;

        s_fireCount.fetch_add(1);
        // atomic float add: load-add-store (sufficient for training metrics)
        float prev = s_rewardSum.load();
        while (!s_rewardSum.compare_exchange_weak(prev, prev + reward)) {}

        return reward;
    }
};

} // namespace RLGC
