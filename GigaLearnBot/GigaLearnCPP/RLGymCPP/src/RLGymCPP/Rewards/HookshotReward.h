#pragma once
#include "../Framework.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace RLGC {

	// =========================================================================
	// DIRECTION CHANGE REWARD - POSSESSION REDIRECT (EVASION/RESET)
	// =========================================================================
	// Rewards flexible angled redirects that maintain possession while evading
	// pressure or resetting play.
	//
	// Core Mechanic:
	// - Ball rolling in any direction (700-1800 uu/s horizontal, z < 120)
	// - Car approaches and touches ball (any reasonable angle)
	// - Touch redirects ball in any direction EXCEPT toward own goal
	// - Ball speed maintained or slightly adjusted
	//
	// 2025-01-28 - Rev 4 (NJH) - Significantly more lenient
	// =========================================================================

	class DirectionChangeReward : public Reward {
	public:

		// =====================================================================
		// CONFIGURATION
		// =====================================================================

		// Ball rolling requirements
		float minHorizontalSpeed = 400.0f;      // Very lenient
		float maxHorizontalSpeed = 2000.0f;     // Very lenient
		float maxBallHeight = 250.0f;           // Lenient

		// Redirect requirements (minimal)
		float minSpeedChange = -200.0f;         // Very lenient
		float maxSpeedChange = 200.0f;          // Very lenient

		// Goal safety
		float minAwayFromOwnGoal = 0.4f;        // Lenient

		// =====================================================================
		// METRICS STRUCTURE
		// =====================================================================

		struct DirectionChangeMetrics {
			int attempts = 0;
			int successful = 0;
			float totalRewardSum = 0.0f;

			void Reset() {
				attempts = 0;
				successful = 0;
				totalRewardSum = 0.0f;
			}

			float GetSuccessRate() const {
				return attempts > 0 ? float(successful) / float(attempts) : 0.0f;
			}

			float GetAverageReward() const {
				return attempts > 0 ? totalRewardSum / float(attempts) : 0.0f;
			}
		};

		// =====================================================================
		// INITIALIZATION
		// =====================================================================

		void Reset(const GameState& state) override {
		}

		// =====================================================================
		// REWARD COMPUTATION
		// =====================================================================

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Only reward on actual ball touches
			if (!player.ballTouchedStep || !state.prev)
				return 0.0f;

			// Only consider balls on ground (rolling)
			if (state.ball.pos.z > maxBallHeight)
				return 0.0f;

			// Get ball velocities before and after touch
			Vec prevVel = state.prev->ball.vel;
			Vec curVel = state.ball.vel;

			float prevHorizSpeed = Vec(prevVel.x, prevVel.y, 0.0f).Length();
			float curHorizSpeed = Vec(curVel.x, curVel.y, 0.0f).Length();

			// Ball must be rolling (very lenient)
			if (prevHorizSpeed < minHorizontalSpeed || prevHorizSpeed > maxHorizontalSpeed)
				return 0.0f;

			// Check speed change is reasonable
			float speedChange = curHorizSpeed - prevHorizSpeed;
			if (speedChange < minSpeedChange || speedChange > maxSpeedChange)
				return 0.0f;

			// Safety: can't farm by hitting toward own goal
			Vec ownGoal = (player.team == Team::BLUE)
				? CommonValues::BLUE_GOAL_CENTER
				: CommonValues::ORANGE_GOAL_CENTER;

			if (prevHorizSpeed > 1e-3f && curHorizSpeed > 1e-3f) {
				Vec prevDir = Vec(prevVel.x, prevVel.y, 0.0f) / prevHorizSpeed;
				Vec curDir = Vec(curVel.x, curVel.y, 0.0f) / curHorizSpeed;

				Vec towardOwnGoal = (ownGoal - state.ball.pos).Normalized();
				float towardOwnGoalAlignment = curDir.Dot(towardOwnGoal);

				if (towardOwnGoalAlignment > minAwayFromOwnGoal)
					return 0.0f;

				// Check direction actually changed
				float directionDot = prevDir.Dot(curDir);

				// Reward: base + direction change bonus
				float directionChangeBonus = RS_CLAMP(1.0f - directionDot, 0.0f, 1.0f);
				float reward = 0.3f + (directionChangeBonus * 0.3f);
				reward = RS_CLAMP(reward, 0.1f, 0.7f);

				// Update metrics
				metrics.attempts++;
				metrics.successful++;
				metrics.totalRewardSum += reward;

				return reward;
			}

			return 0.0f;
		}

	public:

		inline static DirectionChangeMetrics metrics;

		static DirectionChangeMetrics GetAndResetMetrics() {
			DirectionChangeMetrics m = metrics;
			metrics.Reset();
			return m;
		}
	};

	// =========================================================================
	// HOOK SHOT REWARD - OFFENSIVE REDIRECT (GROUND CHIP)
	// =========================================================================
	// Rewards angled shots toward goal from a rolling ball.
	//
	// Core Mechanic:
	// - Ball rolling (600-2000 uu/s horizontal, z < 150)
	// - Touch redirects ball TOWARD OPPONENT'S GOAL
	// - Ball speed increased (any positive gain)
	// - Upward velocity component (z > 300 uu/s)
	//
	// 2025-01-28 - Rev 4 (NJH) - Significantly more lenient
	// =========================================================================

	class HookShotReward : public Reward {
	public:

		// =====================================================================
		// CONFIGURATION
		// =====================================================================

		// Ball state requirements
		float minBallSpeed = 600.0f;           // Very lenient
		float maxBallSpeed = 2000.0f;          // Very lenient
		float maxBallHeight = 250.0f;          // Lenient

		// Touch requirements
		float minSpeedIncrease = 0.0f;         // Any positive gain
		float minUpwardVelocity = 100.0f;      // Much more lenient: 300 instead of 400/500

		// Direction requirements
		float minGoalAlignment = 0.4f;         // Very lenient: 0.3 instead of 0.4/0.5

		// Pre-touch validation
		float maxPreTouchGoalAlignment = 0.5f; // Much more lenient: 0.5 instead of 0.4

		// =====================================================================
		// METRICS STRUCTURE
		// =====================================================================

		struct HookShotMetrics {
			int attempts = 0;
			int onGoal = 0;
			float totalRewardSum = 0.0f;

			void Reset() {
				attempts = 0;
				onGoal = 0;
				totalRewardSum = 0.0f;
			}

			float GetSuccessRate() const {
				return attempts > 0 ? float(onGoal) / float(attempts) : 0.0f;
			}

			float GetAverageReward() const {
				return attempts > 0 ? totalRewardSum / float(attempts) : 0.0f;
			}
		};

		// =====================================================================
		// INITIALIZATION
		// =====================================================================

		void Reset(const GameState& state) override {
		}

		// =====================================================================
		// REWARD COMPUTATION
		// =====================================================================

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Only reward on actual ball touches
			if (!player.ballTouchedStep || !state.prev)
				return 0.0f;

			// Only consider low balls (ground chip)
			if (state.ball.pos.z > maxBallHeight)
				return 0.0f;

			// Get ball velocities before and after touch
			Vec prevVel = state.prev->ball.vel;
			Vec curVel = state.ball.vel;

			float prevHorizSpeed = Vec(prevVel.x, prevVel.y, 0.0f).Length();
			float curHorizSpeed = Vec(curVel.x, curVel.y, 0.0f).Length();
			float curUpwardVel = curVel.z;

			// Ball must be rolling (very lenient)
			if (prevHorizSpeed < minBallSpeed || prevHorizSpeed > maxBallSpeed)
				return 0.0f;

			// Determine target goal
			Vec targetGoal = (player.team == Team::BLUE)
				? CommonValues::ORANGE_GOAL_CENTER
				: CommonValues::BLUE_GOAL_CENTER;

			if (prevHorizSpeed > 1e-3f && curHorizSpeed > 1e-3f) {
				Vec prevDir = Vec(prevVel.x, prevVel.y, 0.0f) / prevHorizSpeed;
				Vec toGoal = (targetGoal - state.ball.pos).Normalized();
				float preGoalAlignment = prevDir.Dot(toGoal);

				// Allow more pre-goal alignment (not just setup phases)
				if (preGoalAlignment > maxPreTouchGoalAlignment)
					return 0.0f;

				// Touch must have upward component
				if (curUpwardVel < minUpwardVelocity)
					return 0.0f;

				// Speed must increase (any positive gain)
				float speedIncrease = curHorizSpeed - prevHorizSpeed;
				if (speedIncrease < minSpeedIncrease)
					return 0.0f;

				// Final direction must be somewhat toward goal
				Vec curDirHoriz = Vec(curVel.x, curVel.y, 0.0f) / curHorizSpeed;
				float postGoalAlignment = curDirHoriz.Dot(toGoal);

				if (postGoalAlignment < minGoalAlignment)
					return 0.0f;

				// Calculate reward: simple formula
				float baseReward = 0.8f;
				float alignBonus = RS_CLAMP(postGoalAlignment - minGoalAlignment, 0.0f, 0.7f);
				float speedBonus = RS_CLAMP(speedIncrease / 500.0f, 0.0f, 0.5f);
				float upwardBonus = RS_CLAMP(curUpwardVel / 1000.0f, 0.0f, 0.5f);

				float reward = baseReward + alignBonus + speedBonus + upwardBonus;
				reward = RS_CLAMP(reward, 0.1f, 2.5f);

				// Update metrics
				metrics.attempts++;
				if (postGoalAlignment >= minGoalAlignment) {
					metrics.onGoal++;
				}
				metrics.totalRewardSum += reward;

				return reward;
			}

			return 0.0f;
		}

	public:

		inline static HookShotMetrics metrics;

		static HookShotMetrics GetAndResetMetrics() {
			HookShotMetrics m = metrics;
			metrics.Reset();
			return m;
		}
	};

}
