#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <cmath>

namespace RLGC {

// ============================================================================
// KICKOFF RANDOMIZATION REWARD
// ============================================================================
// Encourages varied kickoff strategies to prevent repetitive behavior
// Rewards different approach angles, speeds, and tactics during kickoff
// 2026-02-08 | NJH
//
// Strategy:
// 1. Track player's approach angle to ball each kickoff
// 2. Penalize repeating the same angle (within 10° tolerance)
// 3. Reward novel approaches different from recent history
// 4. Maintain rolling history of last 5 kickoffs per player
// ============================================================================

class KickoffRandomizationReward : public Reward {
private:
	// Kickoff state tracking
	struct KickoffHistory {
		float approachAngle = 0.0f;        // Angle approach to ball relative to forward
		float approachSpeed = 0.0f;        // Speed component toward ball
		bool wasCompleted = false;         // Completed kickoff phase
	};

	// Per-player tracking: rolling history of last 5 kickoffs
	struct PlayerKickoffData {
		std::vector<KickoffHistory> history;  // Last 5 kickoffs
		bool inKickoffPhase = false;
		float kickoffStartTime = -1.0f;
	};

	mutable std::map<uint32_t, PlayerKickoffData> playerData;

	// Configuration
	static constexpr float KICKOFF_RADIUS = 300.0f;           // Kickoff phase detection
	static constexpr float MAX_BALL_SPEED_KICKOFF = 500.0f;   // Ball must be stationary
	static constexpr float ANGLE_SIMILARITY_THRESHOLD = 15.0f; // ±15° tolerance for "same" angle
	static constexpr float KICKOFF_TIMEOUT = 10.0f;           // Kickoff phase ends after 10s
	static constexpr float BASE_REWARD = 2.0f;                // Base reward for novel strategy
	static constexpr int HISTORY_SIZE = 5;                    // Track last 5 kickoffs

public:
	virtual void Reset(const GameState& initialState) override {
		playerData.clear();
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!state.prev) return 0.0f;

		auto& data = playerData[player.carId];
		const Player& prevPlayer = state.prev->players[player.index];

		// Detect kickoff phase entry
		bool inKickoff = IsInKickoffPhase(state);
		bool wasInKickoff = data.inKickoffPhase;

		// Kickoff phase just ended - evaluate strategy novelty
		if (wasInKickoff && !inKickoff) {
			float reward = EvaluateKickoffNovelty(player, state, data);
			data.inKickoffPhase = false;
			return reward;
		}

		// Track if entering kickoff
		if (!wasInKickoff && inKickoff) {
			data.inKickoffPhase = true;
			data.kickoffStartTime = 0.0f;
			ClearCurrentKickoffData(data);
		}

		// Update time in kickoff phase
		if (data.inKickoffPhase) {
			data.kickoffStartTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;

			// Timeout if too long
			if (data.kickoffStartTime > KICKOFF_TIMEOUT) {
				data.inKickoffPhase = false;
				return 0.0f;
			}

			// Track approach characteristics
			if (player.isOnGround) {  // Only track while on ground
				UpdateApproachMetrics(player, state, data);
			}
		}

		return 0.0f;
	}

	void PreStep(const GameState& state) override {}

private:
	// ====================================================================
	// Kickoff Phase Detection
	// ====================================================================
	bool IsInKickoffPhase(const GameState& state) const {
		// Kickoff occurs when ball is near center and stationary
		Vec ballPos = state.ball.pos;
		float ballDist = ballPos.Length();
		float ballSpeed = state.ball.vel.Length();

		return (ballDist < KICKOFF_RADIUS) && (ballSpeed < MAX_BALL_SPEED_KICKOFF);
	}

	// ====================================================================
	// Approach Metrics Tracking
	// ====================================================================
	void UpdateApproachMetrics(const Player& player, const GameState& state,
							   PlayerKickoffData& data) const {
		Vec ballPos = state.ball.pos;
		Vec toBall = ballPos - player.pos;
		float distToBall = toBall.Length();

		if (distToBall < 1e-3f) return;

		Vec dirToBall = toBall / distToBall;

		// Calculate approach angle relative to car's forward direction
		// 0° = straight toward ball
		// 90° = perpendicular approach
		// 180° = backing up toward ball
		float forwardComponent = player.rotMat.forward.Dot(dirToBall);
		float rightComponent = player.rotMat.right.Dot(dirToBall);
		float approachAngle = atan2(rightComponent, forwardComponent) * 57.2958f;  // Convert to degrees

		// Normalize to -180 to +180
		while (approachAngle > 180.0f) approachAngle -= 360.0f;
		while (approachAngle < -180.0f) approachAngle += 360.0f;

		// Speed toward ball
		float speedToBall = player.vel.Dot(dirToBall);

		// Store current metrics (will be saved to history when kickoff ends)
		if (data.history.empty()) {
			data.history.push_back(KickoffHistory());
		}
		data.history.back().approachAngle = approachAngle;
		data.history.back().approachSpeed = speedToBall;
	}

	// ====================================================================
	// Novelty Evaluation
	// ====================================================================
	float EvaluateKickoffNovelty(const Player& player, const GameState& state,
								 PlayerKickoffData& data) const {
		// Not enough history to evaluate
		if (data.history.size() < 2) {
			// Save current kickoff to history
			if (!data.history.empty()) {
				data.history.back().wasCompleted = true;
				if (data.history.size() > HISTORY_SIZE) {
					data.history.erase(data.history.begin());
				}
			}
			return 0.0f;  // First kickoff, no penalty
		}

		KickoffHistory& currentKickoff = data.history.back();
		float currentAngle = currentKickoff.approachAngle;

		// Count how many recent kickoffs were "similar" (within tolerance)
		int similarCount = 0;
		int totalCount = 0;

		for (size_t i = 0; i < data.history.size() - 1; i++) {  // Exclude current
			totalCount++;
			float pastAngle = data.history[i].approachAngle;
			float angleDiff = std::abs(currentAngle - pastAngle);

			// Normalize angle difference to shortest path
			if (angleDiff > 180.0f) {
				angleDiff = 360.0f - angleDiff;
			}

			if (angleDiff < ANGLE_SIMILARITY_THRESHOLD) {
				similarCount++;
			}
		}

		// Reward based on novelty
		// If all recent kickoffs were similar: heavy penalty (0.0)
		// If some were different: moderate reward
		// If all were different: high reward
		float noveltyFraction = 1.0f - (float)similarCount / (float)totalCount;

		// Scale reward: 0 → 2.0 based on novelty
		float reward = BASE_REWARD * noveltyFraction;

		// Save to history for next evaluation
		currentKickoff.wasCompleted = true;
		if (data.history.size() > HISTORY_SIZE) {
			data.history.erase(data.history.begin());
		}

		return reward;
	}

	// ====================================================================
	// Helper Functions
	// ====================================================================
	void ClearCurrentKickoffData(PlayerKickoffData& data) const {
		// Prepare for new kickoff tracking
		data.history.push_back(KickoffHistory());
	}

	float GetDistanceToNearestWall(const Vec& pos) const {
		float dx = std::abs(std::abs(pos.x) - CommonValues::SIDE_WALL_X);
		float dy = std::abs(std::abs(pos.y) - CommonValues::BACK_WALL_Y);
		float minDist = RS_MIN(dx, dy);
		return minDist;
	}
};

} // namespace RLGC
