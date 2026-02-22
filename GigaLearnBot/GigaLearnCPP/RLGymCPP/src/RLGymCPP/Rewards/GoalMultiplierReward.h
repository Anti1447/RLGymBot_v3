#pragma once
// ============================================================================
// GoalMultiplierReward.h
// Pays a style bonus multiplier on the step a goal is scored.
//
// Rev 1.0 (2026-02-19, NJH)
//
// Run ALONGSIDE GoalReward (not replacing it). The base payout is 1.0 and
// grows up to MAX_MULTIPLIER (3.0) depending on how impressive the goal was.
// Only the player whose carId matches state.lastTouchCarID receives a reward;
// all other players get 0.
//
// Multiplier bonuses (additive on top of 1.0 base):
//   Assist (passing play)       +0.5   player.eventState.assist on a teammate
//   Ball speed >= 120 kph       +0.3   BallSpeedKph(state) >= 120
//   Flip reset in 4s window     +0.5   scorer acquired flip reset ≤ 4s ago
//   Air dribble (2+ aerial)     +0.4   scorer had ≥2 consecutive aerial touches
//   Tight angle (|ball.x|>600)  +0.3   near post at goal-line crossing
//   1 defender near net         +0.3   1 opponent within 1500 UU of their goal
//   2+ defenders near net       +0.5   2+ opponents (replaces the +0.3)
//
// Total max: 1.0 + 0.5+0.3+0.5+0.4+0.3+0.5 = 3.5 → capped at 3.0
//
// Recommended config (add alongside GoalReward):
//   WeightedReward(new ZeroSumReward(new GoalMultiplierReward(), 1.0f), 50.0f)
// ============================================================================

#include "Reward.h"
#include "CommonRewards.h"   // BallSpeedKph, RS_TEAM_FROM_Y
#include "../CommonValues.h" // ORANGE_GOAL_CENTER, BLUE_GOAL_CENTER
#include <unordered_map>
#include <cmath>

namespace RLGC {

class GoalMultiplierReward : public Reward {
private:
    // Tunable thresholds
    static constexpr float FLIP_RESET_WINDOW   = 4.0f;    // seconds before goal
    static constexpr float AERIAL_BREAK_TIME   = 2.0f;    // gap that resets touch sequence
    static constexpr float AERIAL_TOUCH_Z      = 200.0f;  // min ball Z to count as aerial
    static constexpr float DEFENDER_RADIUS     = 1500.0f; // UU from opponent goal center
    static constexpr float TIGHT_ANGLE_X       = 600.0f;  // UU from center (near post)
    static constexpr float SPEED_THRESHOLD_KPH = 120.0f;
    static constexpr float MAX_MULTIPLIER      = 3.0f;

    // Per-player state (keyed by carId)
    std::unordered_map<uint32_t, float> _elapsedTime;
    std::unordered_map<uint32_t, float> _flipResetAcquiredTime;
    std::unordered_map<uint32_t, int>   _aerialTouchCount;
    std::unordered_map<uint32_t, float> _lastAerialTouchTime;

    void ClearState() {
        _elapsedTime.clear();
        _flipResetAcquiredTime.clear();
        _aerialTouchCount.clear();
        _lastAerialTouchTime.clear();
    }

public:
    void Reset(const GameState& initialState) override {
        ClearState();
        // Seed elapsed time entries for all current players
        for (const auto& player : initialState.players)
            _elapsedTime[player.carId] = 0.0f;
    }

    void PreStep(const GameState& state) override {
        float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

        for (const auto& player : state.players) {
            uint32_t id = player.carId;
            _elapsedTime[id] += dt;

            // ----------------------------------------------------------------
            // Flip reset acquisition tracking
            // ----------------------------------------------------------------
            if (player.prev != nullptr) {
                bool prevHad = player.prev->HasFlipReset();
                bool nowHas  = player.HasFlipReset();
                if (!prevHad && nowHas && !player.isOnGround) {
                    // Just acquired a flip reset while airborne
                    _flipResetAcquiredTime[id] = _elapsedTime[id];
                }
            }

            // ----------------------------------------------------------------
            // Aerial touch sequence tracking
            // ----------------------------------------------------------------
            bool isAerial = !player.isOnGround && (state.ball.pos.z > AERIAL_TOUCH_Z);
            if (player.ballTouchedStep && isAerial) {
                float prevTouchTime = 0.0f;
                auto it = _lastAerialTouchTime.find(id);
                if (it != _lastAerialTouchTime.end())
                    prevTouchTime = it->second;

                float gap = _elapsedTime[id] - prevTouchTime;
                if (prevTouchTime > 0.0f && gap < AERIAL_BREAK_TIME) {
                    // Continuing existing aerial touch sequence
                    _aerialTouchCount[id]++;
                } else {
                    // New sequence
                    _aerialTouchCount[id] = 1;
                }
                _lastAerialTouchTime[id] = _elapsedTime[id];
            } else if (player.isOnGround) {
                // Landing resets the aerial touch sequence
                _aerialTouchCount[id] = 0;
            }
        }
    }

    float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        // Only pay out on the goal step
        if (!state.goalScored) return 0.0f;

        // Only the scorer gets the bonus (identified by lastTouchCarID)
        if ((int)player.carId != state.lastTouchCarID) return 0.0f;

        // Confirm this player's team actually scored (not an own-goal scorer)
        bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
        if (!scored) return 0.0f;

        float bonus = 0.0f;
        uint32_t id = player.carId;

        // ----------------------------------------------------------------
        // 1. Assist bonus (passing play) — check native game event flag
        // ----------------------------------------------------------------
        for (const auto& p : state.players) {
            if (p.team == player.team && p.carId != id && p.eventState.assist) {
                bonus += 0.5f;
                break;
            }
        }

        // ----------------------------------------------------------------
        // 2. Fast goal (>= 120 kph)
        // ----------------------------------------------------------------
        if (BallSpeedKph(state) >= SPEED_THRESHOLD_KPH)
            bonus += 0.3f;

        // ----------------------------------------------------------------
        // 3. Flip reset was used within the play window
        // ----------------------------------------------------------------
        {
            auto it = _flipResetAcquiredTime.find(id);
            if (it != _flipResetAcquiredTime.end()) {
                auto elIt = _elapsedTime.find(id);
                if (elIt != _elapsedTime.end()) {
                    float age = elIt->second - it->second;
                    if (age >= 0.0f && age <= FLIP_RESET_WINDOW)
                        bonus += 0.5f;
                }
            }
        }

        // ----------------------------------------------------------------
        // 4. Air dribble (2 or more consecutive aerial touches)
        // ----------------------------------------------------------------
        {
            auto it = _aerialTouchCount.find(id);
            if (it != _aerialTouchCount.end() && it->second >= 2)
                bonus += 0.4f;
        }

        // ----------------------------------------------------------------
        // 5. Tight angle — ball near post at goal line
        // ----------------------------------------------------------------
        if (std::abs(state.ball.pos.x) > TIGHT_ANGLE_X)
            bonus += 0.3f;

        // ----------------------------------------------------------------
        // 6. Defenders near opponent net
        // ----------------------------------------------------------------
        Vec oppGoalCenter = (player.team == Team::BLUE)
            ? CommonValues::ORANGE_GOAL_CENTER
            : CommonValues::BLUE_GOAL_CENTER;

        int nearDef = 0;
        for (const auto& p : state.players) {
            if (p.team != player.team && !p.isDemoed) {
                if ((p.pos - oppGoalCenter).Length() < DEFENDER_RADIUS)
                    nearDef++;
            }
        }

        if (nearDef >= 2)
            bonus += 0.5f;
        else if (nearDef == 1)
            bonus += 0.3f;

        // ----------------------------------------------------------------
        // Final multiplier — base 1.0 + bonuses, capped
        // ----------------------------------------------------------------
        return RS_CLAMP(1.0f + bonus, 1.0f, MAX_MULTIPLIER);
    }
};

} // namespace RLGC
