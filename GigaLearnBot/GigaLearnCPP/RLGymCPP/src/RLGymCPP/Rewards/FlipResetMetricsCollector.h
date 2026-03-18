#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <cmath>
#include <nlohmann/json.hpp>
#include "../Gamestates/GameState.h"
#include "FlipResetReward.h"
#include "OutplayRewards.h"


/**
 * FlipResetMetricsCollector - Track flip reset performance metrics
 *
 * Collects per-iteration statistics on:
 * - Flip reset acquisition (success count, height, bonuses)
 * - Flip reset attempts (eligible frames for reset acquisition)
 * - Success rate (resets / attempts)
 * - Post-reset action classification (shot/clear/delayed/neutral)
 * - Temporal patterns (time between resets)
 */

struct FlipResetStats {
	// Counters
	int64_t resetCount = 0;
	int64_t attemptCount = 0;

	// Height tracking (Welford's algorithm)
	double heightMean = 0.0;
	double heightM2 = 0.0;
	float heightMin = FLT_MAX;
	float heightMax = FLT_MIN;
	int64_t heightSampleCount = 0;

	// Time between resets
	double timeSinceLastReset = 0.0;
	double timeBetweenResetsMean = 0.0;
	double timeBetweenResetsM2 = 0.0;
	int64_t timeBetweenResetsCount = 0;

	// Bonus tracking
	int64_t handbrakeBonus = 0;
	int64_t boostOffBonus = 0;

	// Post-reset action classification
	int64_t postResetShot = 0;      // OFFENSIVE_SHOT or DELAYED_SHOT
	int64_t postResetClear = 0;     // DEFENSIVE_CLEAR
	int64_t postResetDelayed = 0;   // DELAYED_SHOT (part of shot but delayed)
	int64_t postResetNeutral = 0;   // NEUTRAL_REDIRECT
	int64_t postResetInvalid = 0;   // INVALID (no productive use)

	// Per-player state for detecting transitions
	bool lastHasFlipReset = false;
	float timeSinceReset = 0.0f;

	void RecordReset(float height, bool hasHandbrake, bool boostOff, float deltaTime) {
		resetCount++;

		// Height stats
		heightMean += (double)height;
		heightMin = std::min(heightMin, height);
		heightMax = std::max(heightMax, height);
		heightSampleCount++;

		// Welford for height
		double delta = (double)height - heightMean;
		heightMean += delta / heightSampleCount;
		double delta2 = (double)height - heightMean;
		heightM2 += delta * delta2;

		// Time since last reset
		if (timeSinceReset > 0.0f && timeBetweenResetsCount >= 0) {
			double timeDelta = (double)timeSinceReset;
			timeBetweenResetsCount++;

			// Welford for time between resets
			double delta = timeDelta - timeBetweenResetsMean;
			timeBetweenResetsMean += delta / timeBetweenResetsCount;
			double delta2 = timeDelta - timeBetweenResetsMean;
			timeBetweenResetsM2 += delta * delta2;
		}

		timeSinceReset = 0.0f;  // Reset timer

		if (hasHandbrake) handbrakeBonus++;
		if (boostOff) boostOffBonus++;
	}

	void RecordPostResetAction(int usageType) {
		// usageType: 0=INVALID, 1=OFFENSIVE_SHOT, 2=DEFENSIVE_CLEAR, 3=DELAYED_SHOT, 4=NEUTRAL_REDIRECT
		switch (usageType) {
			case 1:  // OFFENSIVE_SHOT
				postResetShot++;
				break;
			case 2:  // DEFENSIVE_CLEAR
				postResetClear++;
				break;
			case 3:  // DELAYED_SHOT
				postResetDelayed++;
				postResetShot++;  // Count as shot too
				break;
			case 4:  // NEUTRAL_REDIRECT
				postResetNeutral++;
				break;
			case 0:  // INVALID
			default:
				postResetInvalid++;
				break;
		}
	}

	void RecordAttempt() {
		attemptCount++;
	}

	void Update(float deltaTime) {
		timeSinceReset += deltaTime;
	}

	void Reset() {
		resetCount = 0;
		attemptCount = 0;
		heightMean = 0.0;
		heightM2 = 0.0;
		heightMin = FLT_MAX;
		heightMax = FLT_MIN;
		heightSampleCount = 0;
		timeSinceLastReset = 0.0;
		timeBetweenResetsMean = 0.0;
		timeBetweenResetsM2 = 0.0;
		timeBetweenResetsCount = 0;
		handbrakeBonus = 0;
		boostOffBonus = 0;
		postResetShot = 0;
		postResetClear = 0;
		postResetDelayed = 0;
		postResetNeutral = 0;
		postResetInvalid = 0;
		lastHasFlipReset = false;
		timeSinceReset = 0.0f;
	}

	float GetSuccessRate() const {
		if (attemptCount == 0) return 0.0f;
		return (float)resetCount / (float)attemptCount * 100.0f;
	}

	float GetHeightMean() const {
		if (heightSampleCount == 0) return 0.0f;
		return (float)heightMean;
	}

	float GetHeightVariance() const {
		if (heightSampleCount < 2) return 0.0f;
		return (float)(heightM2 / (heightSampleCount - 1));
	}

	float GetHeightStd() const {
		return std::sqrt(GetHeightVariance());
	}

	float GetAvgTimeBetweenResets() const {
		if (timeBetweenResetsCount == 0) return 0.0f;
		return (float)timeBetweenResetsMean;
	}

	float GetHandbraakeBonusRate() const {
		if (resetCount == 0) return 0.0f;
		return (float)handbrakeBonus / (float)resetCount * 100.0f;
	}

	float GetBoostOffBonusRate() const {
		if (resetCount == 0) return 0.0f;
		return (float)boostOffBonus / (float)resetCount * 100.0f;
	}

	float GetPostResetShotRate() const {
		if (resetCount == 0) return 0.0f;
		return (float)postResetShot / (float)resetCount * 100.0f;
	}

	float GetPostResetClearRate() const {
		if (resetCount == 0) return 0.0f;
		return (float)postResetClear / (float)resetCount * 100.0f;
	}

	float GetPostResetNeutralRate() const {
		if (resetCount == 0) return 0.0f;
		return (float)postResetNeutral / (float)resetCount * 100.0f;
	}

	nlohmann::json ToJSON() const {
		nlohmann::json j;
		j["reset_count"] = resetCount;
		j["attempt_count"] = attemptCount;
		j["success_rate"] = GetSuccessRate();
		j["avg_height"] = GetHeightMean();
		j["height_min"] = heightMin;
		j["height_max"] = heightMax;
		j["height_std"] = GetHeightStd();
		j["avg_time_between_resets"] = GetAvgTimeBetweenResets();
		j["handbrake_bonus_rate"] = GetHandbraakeBonusRate();
		j["boost_off_bonus_rate"] = GetBoostOffBonusRate();
		j["post_reset_shot_rate"] = GetPostResetShotRate();
		j["post_reset_clear_rate"] = GetPostResetClearRate();
		j["post_reset_neutral_rate"] = GetPostResetNeutralRate();
		return j;
	}
};

class FlipResetMetricsCollector {
private:
	static constexpr float MIN_RESET_HEIGHT = 300.0f;
	static constexpr float BALL_RADIUS = 91.25f;
	static constexpr float MIN_AIRTIME_FOR_FLIP = 0.1f;

	// Per-player tracking
	std::unordered_map<uint32_t, FlipResetStats> playerStats;

	// Aggregated stats for the iteration
	FlipResetStats aggregatedStats;

public:
	void Initialize(size_t numPlayers) {
		playerStats.clear();
	}

	void Reset() {
		playerStats.clear();
		aggregatedStats.Reset();
	}

	/**
	 * Process game state and detect flip reset transitions
	 * Call this every frame with deltaTime
	 */
	void Step(const RLGC::GameState& state, float deltaTime) {
		// Update time counters for all players
		for (auto& pair : playerStats) {
			pair.second.Update(deltaTime);
		}

		// Check for flip reset transitions
		for (const auto& player : state.players) {
			uint32_t id = player.carId;

			// Lazy initialization
			if (playerStats.find(id) == playerStats.end()) {
				playerStats[id].Reset();
				playerStats[id].lastHasFlipReset = player.HasFlipReset();
			}

			auto& stats = playerStats[id];

			// Detect transition: !HasFlipReset() -> HasFlipReset()
			bool nowHas = player.HasFlipReset();
			bool prevHad = stats.lastHasFlipReset;

			if (!prevHad && nowHas && !player.isOnGround) {
				// Validate reset conditions
				if (player.pos.z >= MIN_RESET_HEIGHT &&
					state.ball.pos.z > BALL_RADIUS &&
					player.airTime >= MIN_AIRTIME_FOR_FLIP) {

					// Record the reset with bonuses
					bool hasHandbrake = player.handbrakeVal > 0.1f;
					bool boostOff = player.prevAction.boost < 0.5f;

					stats.RecordReset(player.pos.z, hasHandbrake, boostOff, deltaTime);
				}
			}

			stats.lastHasFlipReset = nowHas;
		}

		// Track attempts (frames where flip reset is possible)
		for (const auto& player : state.players) {
			uint32_t id = player.carId;

			if (playerStats.find(id) == playerStats.end()) {
				playerStats[id].Reset();
				playerStats[id].lastHasFlipReset = player.HasFlipReset();
			}

			auto& stats = playerStats[id];

			// Attempt conditions: airborne, height >= 300, ball airborne, not kickoff
			bool isAirborne = !player.isOnGround && player.airTime >= MIN_AIRTIME_FOR_FLIP;
			bool ballAirborne = state.ball.pos.z > BALL_RADIUS;
			bool isKickoff = state.ball.pos.Length() < 300 && state.ball.vel.Length() < 200;

			if (isAirborne && ballAirborne && !isKickoff && player.pos.z >= MIN_RESET_HEIGHT) {
				stats.RecordAttempt();
			}
		}
	}

	/**
	 * Post-process episode to classify post-reset actions
	 * This should be called when you have access to ball position changes
	 */
	void ClassifyPostResetActions(const RLGC::GameState& state) {
		// This requires tracking which player achieved the reset and monitoring subsequent frames
		// For now, we record post-reset action classification when available
		// In a full implementation, this would hook into the ball position changes after reset
		// For this initial version, we skip action classification tracking in Step()
		// and would need a separate callback or lookahead mechanism
	}

	/**
	 * Finalize iteration and prepare for reporting
	 */
	void Finalize() {
		aggregatedStats.Reset();

		// Aggregate all player stats
		for (auto& pair : playerStats) {
			aggregatedStats.resetCount += pair.second.resetCount;
			aggregatedStats.attemptCount += pair.second.attemptCount;
			aggregatedStats.handbrakeBonus += pair.second.handbrakeBonus;
			aggregatedStats.boostOffBonus += pair.second.boostOffBonus;
			aggregatedStats.postResetShot += pair.second.postResetShot;
			aggregatedStats.postResetClear += pair.second.postResetClear;
			aggregatedStats.postResetDelayed += pair.second.postResetDelayed;
			aggregatedStats.postResetNeutral += pair.second.postResetNeutral;
			aggregatedStats.postResetInvalid += pair.second.postResetInvalid;

			// Merge height statistics
			if (pair.second.heightSampleCount > 0) {
				if (aggregatedStats.heightSampleCount == 0) {
					aggregatedStats.heightMean = pair.second.heightMean;
					aggregatedStats.heightM2 = pair.second.heightM2;
					aggregatedStats.heightMin = pair.second.heightMin;
					aggregatedStats.heightMax = pair.second.heightMax;
				} else {
					// Merge Welford statistics
					aggregatedStats.heightMin = std::min(aggregatedStats.heightMin, pair.second.heightMin);
					aggregatedStats.heightMax = std::max(aggregatedStats.heightMax, pair.second.heightMax);

					// Simple mean merge (not perfectly accurate for Welford, but practical)
					aggregatedStats.heightMean =
						(aggregatedStats.heightMean * aggregatedStats.heightSampleCount +
						 pair.second.heightMean * pair.second.heightSampleCount) /
						(aggregatedStats.heightSampleCount + pair.second.heightSampleCount);
				}
				aggregatedStats.heightSampleCount += pair.second.heightSampleCount;
			}

			// Merge time between resets
			if (pair.second.timeBetweenResetsCount > 0) {
				aggregatedStats.timeBetweenResetsMean =
					(aggregatedStats.timeBetweenResetsMean * aggregatedStats.timeBetweenResetsCount +
					 pair.second.timeBetweenResetsMean * pair.second.timeBetweenResetsCount) /
					(aggregatedStats.timeBetweenResetsCount + pair.second.timeBetweenResetsCount);
				aggregatedStats.timeBetweenResetsCount += pair.second.timeBetweenResetsCount;
			}
		}
	}

	/**
	 * Get JSON representation of metrics for checkpoint saving
	 */
	nlohmann::json GetMetricsJSON() const {
		return aggregatedStats.ToJSON();
	}

	/**
	 * Get metrics as a struct for manual reporting
	 * Note: Caller (in Learner.cpp) will add these to the Report object
	 */
	const FlipResetStats& GetStats() const {
		return aggregatedStats;
	}

	const FlipResetStats& GetAggregatedStats() const {
		return aggregatedStats;
	}
};
