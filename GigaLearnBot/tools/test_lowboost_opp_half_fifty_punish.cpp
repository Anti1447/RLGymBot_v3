// ============================================================================
// test_lowboost_opp_half_fifty_punish.cpp
// 2025-12-16 - Rev 1 (NJH):
//  - Add standalone assert-based tests for LowBoostOppHalfFiftyPunish sign + scaling.
//  - Includes a minimal local implementation for fast verification.
// Author: NJH
// ============================================================================

#include <cassert>
#include <cmath>
#include <iostream>

// If you want this test to use your REAL Reward/GameState types, swap this file
// to include your project headers instead of the tiny mocks below.
// For now: tiny mocks so it compiles fast and proves the math/sign.

enum class Team { BLUE, ORANGE };

struct Vec {
    float x = 0, y = 0, z = 0;
};

static inline float Abs(float v) { return (v < 0.0f) ? -v : v; }

static inline float Clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

struct Ball {
    Vec pos;
};

struct Player {
    Team team = Team::BLUE;
    float boost = 0.0f;
    bool ballTouchedStep = false;
};

struct GameState {
    Ball ball;
};

// ---------------------------------------------------------------------------
// Minimal punish implementation (matches what you described)
// - Punish if: you challenge a 50/50 with boost < 25 in opponent half.
// - More punish near opponent corners.
// NOTE: This test just checks sign + scaling behavior.
// ---------------------------------------------------------------------------
class LowBoostOppHalfFiftyPunish {
public:
    float GetReward(const Player& player, const GameState& state) const {
        if (!player.ballTouchedStep) return 0.0f;

        const bool lowBoost = (player.boost < 25.0f);
        if (!lowBoost) return 0.0f;

        // "Opponent half" from player perspective:
        // Blue attacks +Y, Orange attacks -Y.
        const bool ballInOppHalf = (player.team == Team::BLUE)
            ? (state.ball.pos.y > 0.0f)
            : (state.ball.pos.y < 0.0f);

        if (!ballInOppHalf) return 0.0f;

        // Corner-ness: scale up as |x| and |y| get large.
        // Field-ish numbers (Rocket League): X ~ 4096, Y ~ 5120 (not exact; good enough for shaping).
        const float xNorm = Clamp(Abs(state.ball.pos.x) / 4096.0f, 0.0f, 1.0f);
        const float yNorm = Clamp(Abs(state.ball.pos.y) / 5120.0f, 0.0f, 1.0f);

        // Weight corners heavily: both x and y near max => ~1.0
        const float cornerFactor = Clamp(0.5f * xNorm + 0.5f * yNorm, 0.0f, 1.0f);

        // Base penalty and corner bonus
        const float basePenalty = -2.0f;              // always negative
        const float cornerBonus = -6.0f * cornerFactor; // more negative in corners

        return basePenalty + cornerBonus; // [-2 .. -8]
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static void Test_Punish_IsNegative_InOppHalf() {
    LowBoostOppHalfFiftyPunish punish;

    Player p;
    p.team = Team::BLUE;
    p.boost = 0.0f;
    p.ballTouchedStep = true;

    GameState s;
    s.ball.pos = { 0.0f, 1000.0f, 0.0f }; // Blue: +Y is opponent half

    float r = punish.GetReward(p, s);
    assert(r < 0.0f);
}

static void Test_NoPunish_InOwnHalf() {
    LowBoostOppHalfFiftyPunish punish;

    Player p;
    p.team = Team::BLUE;
    p.boost = 0.0f;
    p.ballTouchedStep = true;

    GameState s;
    s.ball.pos = { 0.0f, -1000.0f, 0.0f }; // Blue: -Y is own half

    float r = punish.GetReward(p, s);
    assert(r == 0.0f);
}

static void Test_NoPunish_WhenBoostHigh() {
    LowBoostOppHalfFiftyPunish punish;

    Player p;
    p.team = Team::BLUE;
    p.boost = 30.0f; // not low
    p.ballTouchedStep = true;

    GameState s;
    s.ball.pos = { 0.0f, 2000.0f, 0.0f }; // opp half

    float r = punish.GetReward(p, s);
    assert(r == 0.0f);
}

static void Test_CornerMoreNegativeThanCenter() {
    LowBoostOppHalfFiftyPunish punish;

    Player p;
    p.team = Team::BLUE;
    p.boost = 10.0f;
    p.ballTouchedStep = true;

    GameState center;
    center.ball.pos = { 0.0f, 2500.0f, 0.0f };

    GameState corner;
    corner.ball.pos = { 4096.0f, 5120.0f, 0.0f };

    float rCenter = punish.GetReward(p, center);
    float rCorner = punish.GetReward(p, corner);

    assert(rCenter < 0.0f);
    assert(rCorner < 0.0f);
    assert(rCorner < rCenter); // corner should be more negative
}

int main() {
    Test_Punish_IsNegative_InOppHalf();
    Test_NoPunish_InOwnHalf();
    Test_NoPunish_WhenBoostHigh();
    Test_CornerMoreNegativeThanCenter();

    std::cout << "[OK] LowBoostOppHalfFiftyPunish tests passed.\n";
    return 0;
}
