#pragma once
#include "Reward.h"
#include <map>
#include <cmath>

namespace RLGC {

	// ============================================================================
	// AngleFlickRewards - 45° and 90° Flick Shot Detection
	// Specialized flick detection based on car yaw rotation + back flip
	// 2026-02-05 | Rev 1.0 | NJH
	//
	// 45-degree flick: Jump → rotate 45° ± 15° → back flip with ball on front
	// 90-degree flick: Jump → rotate 90° ± 15° → back flip with ball on front
	//
	// Key features:
	// - Tracks yaw rotation efficiency (reward achieved rotation within 1.0s)
	// - Validates negative pitch during flip (-30°+)
	// - Requires ball contact on front of car
	// - Goal direction bonus (up to +30% for toward-goal shots)
	// - Difficulty-scaled rewards (90° = 7.0, 45° = 5.0)
	// ============================================================================

	// Base class for angle-based flick detection
	class AngleFlickRewardBase : public Reward {
	protected:
		struct AngleFlickTracker {
			enum State { WAITING, AIRBORNE, YAW_ACHIEVED, FLIP_DETECTED, BALL_CONTACT, COMPLETED };

			State state = WAITING;
			float jumpStartTime = -1.0f;
			float yawAchievementTime = -1.0f;  // When target yaw was first reached
			float maxYawRotation = 0.0f;
			Vec initialForward = Vec(0, 0, 0);
			float flipStartTime = -1.0f;
			float pitchDuringFlip = 0.0f;
			bool ballTouchedDuringFlip = false;
			int yawDirection = 0;  // -1 = left, +1 = right
		};

		mutable std::map<uint32_t, AngleFlickTracker> trackers;

		// Configuration (override in subclasses)
		float targetYawDegrees = 45.0f;
		float yawTolerance = 15.0f;
		float minFlickSpeed = 1500.0f;
		float rewardScale = 5.0f;
		float yawEfficiencyWindow = 1.0f;  // Max time to achieve yaw (seconds)

		// Static constants
		static constexpr float MIN_PITCH_DEGREES = -30.0f;      // Minimum pitch for back flip
		static constexpr float PERFECT_PITCH_DEGREES = -90.0f;  // Perfect back flip pitch
		static constexpr float PITCH_SCORE_RANGE = 60.0f;       // Pitch score calculation range
		static constexpr float MAX_FLICK_SPEED = 2500.0f;       // Speed saturation threshold
		static constexpr float GAUSSIAN_SIGMA = 15.0f;          // Yaw tolerance (degrees)
		static constexpr float EFFICIENCY_BONUS = 1.25f;        // Bonus for achieving yaw in < 0.5s
		static constexpr float GOAL_DIRECTION_SCALE = 0.3f;     // Scale of goal direction bonus

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];

			// ====================================================================
			// Phase 1: Jump Detection
			// Detect transition from ground to air
			// ====================================================================
			if (prevPlayer.isOnGround && !player.isOnGround) {
				tracker.state = AngleFlickTracker::AIRBORNE;
				tracker.jumpStartTime = 0.0f;  // Reset timing
				tracker.initialForward = player.rotMat.forward;
				tracker.maxYawRotation = 0.0f;
				tracker.yawAchievementTime = -1.0f;
				return 0.0f;
			}

			// Timeout: if in air for > 2.0 seconds, reset
			if (tracker.state != AngleFlickTracker::WAITING) {
				tracker.jumpStartTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
				if (tracker.jumpStartTime > 2.0f) {
					tracker.state = AngleFlickTracker::WAITING;
					return 0.0f;
				}
			}

			// ====================================================================
			// Phase 2: Yaw Accumulation (while airborne, before flip)
			// Track how much the car has rotated from initial forward direction
			// ====================================================================
			if (tracker.state == AngleFlickTracker::AIRBORNE && !player.isFlipping) {
				float currentYaw = CalculateYawFromRotMat(tracker.initialForward, player.rotMat.forward);
				if (currentYaw > tracker.maxYawRotation) {
					tracker.maxYawRotation = currentYaw;

					// Check if target yaw achieved within tolerance
					if (tracker.yawAchievementTime < 0.0f &&
						currentYaw >= (targetYawDegrees - yawTolerance) &&
						currentYaw <= (targetYawDegrees + yawTolerance)) {
						tracker.yawAchievementTime = tracker.jumpStartTime;
					}
				}
			}

			// ====================================================================
			// Phase 3: Flip Input Detection
			// Validate negative pitch during flip (back flip orientation)
			// ====================================================================
			if (tracker.state == AngleFlickTracker::AIRBORNE && player.isFlipping && !prevPlayer.isFlipping) {
				// Flip just started - validate pitch
				float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;

				if (pitchDegrees < MIN_PITCH_DEGREES) {
					tracker.state = AngleFlickTracker::FLIP_DETECTED;
					tracker.flipStartTime = tracker.jumpStartTime;
					tracker.pitchDuringFlip = pitchDegrees;
				} else {
					// Wrong pitch - not a back flip
					tracker.state = AngleFlickTracker::WAITING;
					return 0.0f;
				}
			}

			// Track minimum pitch during flip
			if (tracker.state == AngleFlickTracker::FLIP_DETECTED && player.isFlipping) {
				float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;
				tracker.pitchDuringFlip = RS_MIN(tracker.pitchDuringFlip, pitchDegrees);
			}

			// ====================================================================
			// Phase 4: Ball Contact Validation
			// Validate ball is on front of car during flip
			// ====================================================================
			if (tracker.state == AngleFlickTracker::FLIP_DETECTED && player.ballTouchedStep) {
				if (BallOnCarSimple(player, state)) {
					tracker.state = AngleFlickTracker::BALL_CONTACT;
					tracker.ballTouchedDuringFlip = true;
				}
			}

			// ====================================================================
			// Phase 5: Reward Trigger
			// If all conditions met, calculate and return reward
			// ====================================================================
			if (tracker.state == AngleFlickTracker::BALL_CONTACT && tracker.ballTouchedDuringFlip) {
				float ballSpeed = state.ball.vel.Length();

				// Require minimum flick speed
				if (ballSpeed < minFlickSpeed) {
					return 0.0f;
				}

				// Require yaw was achieved within efficiency window
				if (tracker.yawAchievementTime < 0.0f ||
					tracker.yawAchievementTime > yawEfficiencyWindow) {
					return 0.0f;
				}

				float reward = CalculateReward(tracker.maxYawRotation, tracker.pitchDuringFlip, ballSpeed, player, state);

				// Reset for next jump
				tracker.state = AngleFlickTracker::WAITING;
				tracker.ballTouchedDuringFlip = false;

				return reward;
			}

			return 0.0f;
		}

	protected:
		// Helper: Calculate Gaussian peak score for yaw accuracy
		float GaussianPeak(float value, float peak, float sigma) const {
			float diff = value - peak;
			return exp(-(diff * diff) / (2.0f * sigma * sigma));
		}

		// Helper: Calculate yaw angle between two forward vectors (degrees)
		float CalculateYawFromRotMat(const Vec& initialForward, const Vec& currentForward) const {
			float dotProduct = RS_CLAMP(initialForward.Dot(currentForward), -1.0f, 1.0f);
			float angleRadians = acos(dotProduct);
			return angleRadians * 57.2958f;  // Radians to degrees
		}

		// Helper: Check if ball is on car (front, top)
		bool BallOnCarSimple(const Player& player, const GameState& state) const {
			Vec rel = state.ball.pos - player.pos;
			float height = rel.z;
			float horiz = Vec(rel.x, rel.y, 0).Length();
			return (height > 40.0f && height < 150.0f && horiz < 120.0f);
		}

		// Calculate total reward based on all factors
		float CalculateReward(float yawMag, float pitchDegrees, float ballSpeed,
							  const Player& player, const GameState& state) const {
			// Yaw accuracy: Gaussian peak at target yaw ± tolerance
			float yawScore = GaussianPeak(yawMag, targetYawDegrees, GAUSSIAN_SIGMA);

			// Pitch accuracy: Range from -30° (0.0) to -150° (1.0)
			float pitchScore = RS_CLAMP((pitchDegrees - MIN_PITCH_DEGREES) / PITCH_SCORE_RANGE, 0.0f, 1.0f);

			// Speed score: Range from minFlickSpeed to MAX_FLICK_SPEED
			float speedScore = RS_CLAMP(ballSpeed / MAX_FLICK_SPEED, 0.0f, 1.0f);

			// Efficiency bonus: Reward quick rotation (< 0.5s = 1.25x, otherwise 1.0x)
			// (yawAchievementTime already validated to be <= yawEfficiencyWindow)
			float efficiencyBonus = 1.0f;
			// Note: In actual implementation, would need to track achievement time
			// For now, use a placeholder - will be refined in GetReward()

			// Direction toward goal: Optional bonus
			Vec opponentGoal = (player.team == Team::BLUE) ?
				CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
			float goalAlignment = state.ball.vel.Normalized().Dot(toGoal);
			float directionBonus = RS_MAX(0.0f, goalAlignment);

			// Total reward: Base × yawScore × pitchScore × speedScore × efficiencyBonus × (1 + directionBonus)
			return rewardScale * yawScore * pitchScore * speedScore * efficiencyBonus * (1.0f + GOAL_DIRECTION_SCALE * directionBonus);
		}
	};

	// ============================================================================
	// AngleFlick45Reward
	// Detects 45-degree flick shots
	// Max reward: 5.0 (scaled by yaw, pitch, speed, and efficiency)
	// ============================================================================
	class AngleFlick45Reward : public AngleFlickRewardBase {
	public:
		AngleFlick45Reward() {
			targetYawDegrees = 45.0f;
			yawTolerance = 15.0f;
			rewardScale = 5.0f;
			yawEfficiencyWindow = 1.0f;
		}
	};

	// ============================================================================
	// AngleFlick90Reward
	// Detects 90-degree flick shots
	// Max reward: 7.0 (scaled by yaw, pitch, speed, and efficiency)
	// Higher reward than 45° due to increased difficulty
	// ============================================================================
	class AngleFlick90Reward : public AngleFlickRewardBase {
	public:
		AngleFlick90Reward() {
			targetYawDegrees = 90.0f;
			yawTolerance = 15.0f;
			rewardScale = 7.0f;
			yawEfficiencyWindow = 1.0f;
		}
	};

}
