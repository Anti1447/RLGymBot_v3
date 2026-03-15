// ============================================================================
// AirDribbleBumpReward
// Rewards bumping/demoing an opponent while executing an air dribble toward goal
// ============================================================================

#pragma once

#include "Reward.h"
#include "BallPrediction.h"
#include "../CommonValues.h"
#include <map>
#include <cmath>

namespace RLGC {

class AirDribbleBumpReward : public Reward {
private:
    // Trajectory cache updated once per frame
    BallPred::Cache _ballCache;

    // Per-player state tracking (keyed by carId)
    std::map<uint32_t, bool> _isInSetup;           // Currently in air dribble setup
    std::map<uint32_t, float> _setupStartTime;     // Time since setup initiated
    std::map<uint32_t, float> _setupBoost;         // Boost amount when setup started

    // Constants
    static constexpr float MIN_BALL_HEIGHT = 250.0f;          // Ball must be airborne
    static constexpr float MAX_SETUP_DISTANCE = 300.0f;       // Ball within reach
    static constexpr float MIN_BOOST_TO_START = 30.0f;        // Minimum boost to initiate
    static constexpr float MAX_SEQUENCE_DURATION = 5.0f;      // Timeout for sequence
    static constexpr float PREDICTION_HORIZON = 2.0f;         // 2 second trajectory prediction
    static constexpr float GOAL_VALIDATION_HORIZON = 0.5f;    // Stricter at bump time

    // ========================================================================
    // Helper: Validate that ball trajectory will intersect goal mouth
    // ========================================================================
    bool ValidateGoalTrajectory(const GameState& state, float horizonSeconds) const {
        // Get ball trajectory prediction
        const BallPred::BallSlice& slice = _ballCache.AtTime(horizonSeconds);

        // Check if ball will hit ground before goal entry
        if (slice.hitGround) {
            return false;  // Ball bounced, invalid trajectory
        }

        // Determine opponent goal based on team
        Vec oppGoal = (state.players.empty()) ? Vec(0.0f, 5120.0f, 0.0f) :
            (state.players[0].team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_CENTER :
            CommonValues::BLUE_GOAL_CENTER;

        // Check if trajectory intersects goal mouth
        // Goal width: ±892.755 UU on X axis, height 0-642.775 UU on Z axis
        float goalX = std::abs(slice.pos.x);
        bool inGoalX = goalX <= CommonValues::GOAL_WIDTH_FROM_CENTER;
        bool inGoalZ = slice.pos.z >= 0.0f && slice.pos.z <= CommonValues::GOAL_HEIGHT;

        // Check if ball moving toward goal (positive direction toward opponent)
        Vec toGoal = oppGoal - slice.pos;
        float ballMovingToward = slice.vel.Dot(toGoal.Normalized());
        bool movingTowardGoal = ballMovingToward > 100.0f;  // Minimal threshold

        return inGoalX && inGoalZ && movingTowardGoal;
    }

    // ========================================================================
    // Helper: Calculate trajectory quality based on ball position
    // ========================================================================
    float CalculateTrajectoryQuality(const GameState& state) const {
        // Optimal entry is at goal mouth center
        Vec oppGoal = (state.players.empty()) ? Vec(0.0f, 5120.0f, 0.0f) :
            (state.players[0].team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_CENTER :
            CommonValues::BLUE_GOAL_CENTER;

        Vec goalMouth = Vec(oppGoal.x, oppGoal.y, CommonValues::GOAL_HEIGHT * 0.5f);
        float distFromOptimal = (state.ball.pos - goalMouth).Length();

        // Quality: 1.0 at goal line, 0.8 at 500 UU away, 0.5 at 2000+ UU
        return RS_MAX(0.5f, 1.0f - (distFromOptimal / 2000.0f));
    }

    // ========================================================================
    // Helper: Calculate timing bonus based on how quickly bump occurs
    // ========================================================================
    float CalculateTimingBonus(float setupDuration) const {
        // Faster execution is better (encourages quick decision-making)
        if (setupDuration < 1.0f) return 1.0f;
        if (setupDuration < 2.0f) return 0.8f;
        if (setupDuration < 3.0f) return 0.6f;
        return 0.5f;  // Very late execution
    }

    // ========================================================================
    // Helper: Calculate boost efficiency (reward for smart boost management)
    // ========================================================================
    float CalculateBoostEfficiency(float currentBoost, float setupBoost) const {
        if (setupBoost < 1.0f) return 0.3f;  // Guard against division issues

        float boostSpent = setupBoost - currentBoost;
        float spentFraction = RS_CLAMP(boostSpent / setupBoost, 0.0f, 1.0f);

        // Optimal is spending 50-70% of boost to position for bump
        float optimalSpend = 0.6f;
        float deviation = std::abs(spentFraction - optimalSpend);
        float efficiency = 1.0f - deviation;

        return RS_MAX(0.3f, efficiency);
    }

    // ========================================================================
    // Helper: Check if air dribble setup is still valid
    // ========================================================================
    bool IsAirDribbleValid(const Player& player, const GameState& state) const {
        // Player must be in air (not on ground)
        if (player.isOnGround) {
            return false;
        }

        // Ball must be airborne
        if (state.ball.pos.z < MIN_BALL_HEIGHT) {
            return false;
        }

        // Ball must be reachable (close and above player)
        Vec toBall = state.ball.pos - player.pos;
        float distToBall = toBall.Length();
        if (distToBall > MAX_SETUP_DISTANCE) {
            return false;
        }

        // Ball must be above player (can't touch from below)
        if (state.ball.pos.z <= player.pos.z) {
            return false;
        }

        // Player must have sufficient boost remaining
        if (player.boost < MIN_BOOST_TO_START) {
            return false;
        }

        // Ball trajectory must be valid (toward goal, no bounces)
        if (!ValidateGoalTrajectory(state, PREDICTION_HORIZON)) {
            return false;
        }

        return true;
    }

public:
    virtual void Reset(const GameState& initialState) override {
        _isInSetup.clear();
        _setupStartTime.clear();
        _setupBoost.clear();
    }

    virtual void PreStep(const GameState& state) override {
        // Update ball trajectory prediction (done once per frame, before all GetReward calls)
        _ballCache.Update(state);

        // Validate active air dribble setups for each player
        for (const auto& player : state.players) {
            uint32_t id = player.carId;

            if (_isInSetup.find(id) == _isInSetup.end() || !_isInSetup[id]) {
                continue;  // Not in active sequence
            }

            // Check if setup conditions still valid
            if (player.isOnGround || state.ball.pos.z < MIN_BALL_HEIGHT) {
                // Player landed or ball fell to ground
                _isInSetup[id] = false;
                continue;
            }

            // Check if ball trajectory still valid
            if (!ValidateGoalTrajectory(state, PREDICTION_HORIZON)) {
                // Trajectory became invalid, will penalize in GetReward
                _isInSetup[id] = false;
            }
        }
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        uint32_t id = player.carId;

        // Initialize state maps if needed
        if (_isInSetup.find(id) == _isInSetup.end()) {
            _isInSetup[id] = false;
            _setupStartTime[id] = 0.0f;
            _setupBoost[id] = 0.0f;
        }

        // Try to initiate new sequence if not already in one
        if (!_isInSetup[id]) {
            if (IsAirDribbleValid(player, state)) {
                _isInSetup[id] = true;
                _setupStartTime[id] = 0.0f;
                _setupBoost[id] = player.boost;
                return 0.0f;  // No reward on initiation, just state change
            }
            return 0.0f;
        }

        // Sequence is active - check for success or failure
        float elapsedTime = _setupStartTime[id];

        // Check if bump or demo occurred
        if (player.eventState.bump || player.eventState.demo) {
            // Validate that ball is STILL on goal trajectory at bump time
            if (!ValidateGoalTrajectory(state, GOAL_VALIDATION_HORIZON)) {
                // Ball is no longer valid at bump time
                _isInSetup[id] = false;
                return 0.0f;
            }

            // Success! Calculate reward with multipliers
            float baseReward = player.eventState.demo ? 2.0f : 1.0f;
            float trajectoryQuality = CalculateTrajectoryQuality(state);
            float timingBonus = CalculateTimingBonus(elapsedTime);
            float boostEfficiency = CalculateBoostEfficiency(player.boost, _setupBoost[id]);

            _isInSetup[id] = false;  // Sequence complete
            return baseReward * trajectoryQuality * timingBonus * boostEfficiency;
        }

        // Check for failure conditions
        if (elapsedTime > MAX_SEQUENCE_DURATION) {
            // Sequence timeout - took too long
            _isInSetup[id] = false;
            return 0.0f;
        }

        // Update elapsed time for next frame
        _setupStartTime[id] += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;

        // Sequence still active, no reward yet
        return 0.0f;
    }
};

} // namespace RLGC
