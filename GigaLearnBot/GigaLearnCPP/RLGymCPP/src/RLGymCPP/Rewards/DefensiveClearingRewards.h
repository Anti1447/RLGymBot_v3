#pragma once
#include "Reward.h"
#include "../Math.h"
#include "PositionAnalysis.h"
#include <unordered_map>
#include <cmath>

namespace RLGC {

// ============================================================================
// BIG CLEAR REWARD - Simple Defensive Clear + Ball Distance Bonus
// ============================================================================
// Fires when: defensive half + hard hit (>=300 u/s) + ball directed away from goal.
// BASE_REWARD fires immediately on the touch, scaled by target safety (2026-07-21,
// Rev 4.0 NJH): a clear headed straight at an unmarked opponent previously paid
// the same as one headed to open space. Projects a landing point along the ball's
// exit velocity and scales credit by the nearest opponent's distance to it — full
// credit at/beyond SAFE_DIST, floored (never zeroed) inside DANGER_DIST so a
// desperate clear under pressure still gets partial credit for clearing at all.
// Distance bonus fires on the next touch after the clear: rewards getting the ball
// further from your goal. Pattern ported from RolvBallTravelReward (PreStep
// accumulation + pendingBonuses map consumed in GetReward).
// 2026-02-08 | Rev 2.0 | NJH - Simplified
// 2026-06-27 | Rev 3.0 | NJH - Added ball distance bonus
// 2026-07-21 | Rev 4.0 | NJH - Added opponent-target-awareness scaling
// ============================================================================

class BigClearReward : public Reward {
private:
    static constexpr float BASE_REWARD = 5.0f;
    static constexpr float CLEAR_LOOKAHEAD_DIST = 2500.0f; // UU, projected landing point
    static constexpr float SAFE_DIST   = 2500.0f;          // UU, opponent this far = full credit
    static constexpr float DANGER_DIST = 800.0f;           // UU, opponent this close = floor credit
    static constexpr float MIN_TARGET_SAFETY = 0.2f;       // never fully zero a clear

    float distBonusScale;
    float normFactor;
    Vec   prevBallPos;
    float distanceSinceLastClear;
    bool  clearPending;
    uint32_t lastClearerId;
    std::unordered_map<uint32_t, float> pendingBonuses;

public:
    BigClearReward(float distBonusScale = 3.0f)
        : distBonusScale(distBonusScale),
          normFactor(1.0f / (2.0f * CommonValues::BACK_WALL_Y)),
          prevBallPos(Vec(0, 0, 0)),
          distanceSinceLastClear(0.0f),
          clearPending(false),
          lastClearerId(0) {}

    virtual void Reset(const GameState& initialState) override {
        prevBallPos = initialState.ball.pos;
        distanceSinceLastClear = 0.0f;
        clearPending = false;
        pendingBonuses.clear();
        for (const auto& p : initialState.players)
            pendingBonuses[p.carId] = 0.0f;
    }

    virtual void PreStep(const GameState& state) override {
        Vec bp = state.ball.pos;
        if (clearPending) {
            Vec d = bp - prevBallPos;
            distanceSinceLastClear += sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);
        }
        prevBallPos = bp;

        for (auto& kv : pendingBonuses) kv.second = 0.0f;

        if (clearPending) {
            for (const auto& p : state.players) {
                if (p.ballTouchedStep) {
                    pendingBonuses[lastClearerId] =
                        distanceSinceLastClear * normFactor * distBonusScale;
                    clearPending = false;
                    distanceSinceLastClear = 0.0f;
                    break;
                }
            }
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        float reward = 0.0f;

        auto it = pendingBonuses.find(player.carId);
        if (it != pendingBonuses.end()) reward += it->second;

        if (!player.ballTouchedStep) return reward;

        bool inDefensiveHalf = (player.team == Team::BLUE) ? (player.pos.y < 0) : (player.pos.y > 0);
        if (!inDefensiveHalf) return reward;

        if (state.ball.vel.Length() < 300.0f) return reward;

        Vec ownGoal = (player.team == Team::BLUE) ?
            CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
        Vec awayFromGoal = (state.ball.pos - ownGoal).Normalized();
        Vec ballVelDir = state.ball.vel.Normalized();
        if (ballVelDir.Dot(awayFromGoal) < 0.0f) return reward;

        lastClearerId = player.carId;
        clearPending = true;
        distanceSinceLastClear = 0.0f;

        Vec clearTarget = state.ball.pos + ballVelDir * CLEAR_LOOKAHEAD_DIST;
        float nearestOpp = PositionAnalysis::ClosestOpponentDistance(player, clearTarget, state.players);
        float targetSafety = RS_CLAMP(
            (nearestOpp - DANGER_DIST) / (SAFE_DIST - DANGER_DIST),
            MIN_TARGET_SAFETY, 1.0f);

        return reward + BASE_REWARD * targetSafety;
    }
};

} // namespace RLGC

// ============================================================================
// END BIG CLEAR REWARD
// ============================================================================
