#pragma once
#include "Reward.h"
#include <map>
#include <cmath>

namespace RLGC {

	// ============================================================================
	// DoubleJumpAerialReward - Reward double jump aerial setup
	// Detects and rewards the fundamental timing of double jumping in air
	// 2026-02-08 | Rev 1.0 | NJH
	//
	// Key features:
	// - Tracks first jump (transition from ground to air)
	// - Detects second jump within 2 seconds of first jump
	// - Rewards learning correct aerial timing
	// - Simple detection: just validates jump input sequence
	// - Bonus for reaching higher altitude with double jump
	// ============================================================================
	class DoubleJumpAerialReward : public Reward {
	private:
		struct AerialTracker {
			enum State { WAITING, FIRST_JUMP, SECOND_JUMP_READY, COMPLETED };

			State state = WAITING;
			float jumpStartTime = -1.0f;
			float firstJumpHeight = 0.0f;
			float maxHeightAfterSecondJump = 0.0f;
			bool secondJumpDetected = false;
		};

		mutable std::map<uint32_t, AerialTracker> trackers;

		// Configuration
		static constexpr float DOUBLE_JUMP_WINDOW = 2.0f;         // Max time between jumps (seconds)
		static constexpr float BASE_REWARD = 1.0f;                // Base reward for double jump
		static constexpr float HEIGHT_BONUS_SCALE = 0.003f;       // Bonus per unit of height gained
		static constexpr float MAX_HEIGHT_BONUS = 2.0f;           // Cap on height bonus

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];

			// ====================================================================
			// Phase 1: First Jump Detection
			// Detect transition from ground to air
			// ====================================================================
			if (prevPlayer.isOnGround && !player.isOnGround) {
				tracker.state = AerialTracker::FIRST_JUMP;
				tracker.jumpStartTime = 0.0f;
				tracker.firstJumpHeight = player.pos.z;
				tracker.secondJumpDetected = false;
				return 0.0f;
			}

			// Timeout: if in air for > 2.5 seconds without second jump, reset
			if (tracker.state != AerialTracker::WAITING) {
				tracker.jumpStartTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
				if (tracker.jumpStartTime > 2.5f) {
					tracker.state = AerialTracker::WAITING;
					return 0.0f;
				}
			}

			// ====================================================================
			// Phase 2: Second Jump Detection
			// Detect when player jumps again while airborne (within 2 second window)
			// ====================================================================
			if (tracker.state == AerialTracker::FIRST_JUMP &&
				tracker.jumpStartTime > 0.0f && tracker.jumpStartTime <= DOUBLE_JUMP_WINDOW) {

				// Check if car has flip available (hasFlipped = false means flip still available)
				// Flip activation means second jump is being performed
				if (player.isFlipping && !prevPlayer.isFlipping) {
					tracker.state = AerialTracker::SECOND_JUMP_READY;
					tracker.secondJumpDetected = true;
					tracker.maxHeightAfterSecondJump = player.pos.z;
				}
			}

			// Track maximum height after second jump
			if (tracker.state == AerialTracker::SECOND_JUMP_READY && player.pos.z > tracker.maxHeightAfterSecondJump) {
				tracker.maxHeightAfterSecondJump = player.pos.z;
			}

			// ====================================================================
			// Phase 3: Reward Trigger
			// Award reward for successful double jump within timing window
			// ====================================================================
			if (tracker.state == AerialTracker::SECOND_JUMP_READY && tracker.secondJumpDetected) {
				// Calculate height gained from second jump
				float heightGained = RS_MAX(0.0f, tracker.maxHeightAfterSecondJump - tracker.firstJumpHeight);

				// Height bonus: up to 2.0 for reaching 667+ units higher
				float heightBonus = RS_MIN(MAX_HEIGHT_BONUS, heightGained * HEIGHT_BONUS_SCALE);

				float reward = BASE_REWARD + heightBonus;

				// Reset for next aerial
				tracker.state = AerialTracker::WAITING;
				tracker.secondJumpDetected = false;

				return reward;
			}

			return 0.0f;
		}
	};

	// ============================================================================
	// PitchControlAerialReward - Reward fast aerial technique with pitch control
	// Detects and rewards jump → pitch up → jump sequence for quick aerial boost
	// 2026-02-08 | Rev 1.0 | NJH
	//
	// Key features:
	// - First jump off ground
	// - Pitch car nose upward (>15°) during first jump
	// - Second jump input within 2 seconds
	// - Higher reward than DoubleJumpAerial for executing full technique
	// - Bonus for optimal pitch angle (closer to 45° is better)
	// - Can include boost usage but not required
	// ============================================================================
	class PitchControlAerialReward : public Reward {
	private:
		struct FastAerialTracker {
			enum State { WAITING, FIRST_JUMP, PITCHED_UP, SECOND_JUMP, COMPLETED };

			State state = WAITING;
			float jumpStartTime = -1.0f;
			float firstJumpHeight = 0.0f;
			float maxPitchDuringFirstJump = 0.0f;
			bool isPitchedUpEnough = false;
			bool secondJumpDetected = false;
			float maxHeightAfterSecondJump = 0.0f;
			float sequenceStartBoost = -1.0f;  // NEW: Boost when sequence started (for linear scaling 0-30)
		};

		mutable std::map<uint32_t, FastAerialTracker> trackers;

		// Configuration
		static constexpr float DOUBLE_JUMP_WINDOW = 0.4f;         // Max time between jumps (0.25-0.5s window, now 0.4s)
		static constexpr float MIN_PITCH_DEGREES = 25.0f;         // Minimum pitch requirement (was 15.0f, now 25.0f for 45±20° range)
		static constexpr float MAX_PITCH_DEGREES = 65.0f;         // Maximum pitch requirement (NEW: enforce upper bound of 45±20° range)
		static constexpr float OPTIMAL_PITCH_DEGREES = 45.0f;     // Sweet spot for pitch
		static constexpr float PITCH_TOLERANCE = 15.0f;           // Tolerance around optimal
		static constexpr float BASE_REWARD = 2.0f;                // Base reward for fast aerial
		static constexpr float HEIGHT_BONUS_SCALE = 0.005f;       // Higher bonus per unit height
		static constexpr float MAX_HEIGHT_BONUS = 3.0f;           // Cap on height bonus
		static constexpr float PITCH_BONUS_SCALE = 0.02f;         // Bonus for good pitch angle
		static constexpr float MAX_PITCH_BONUS = 1.0f;            // Cap on pitch bonus
		static constexpr float MIN_BOOST_FOR_REWARD = 0.0f;       // NEW: Minimum boost gate (0.0f allows linear scaling from 0)
		static constexpr float BOOST_SCALE_THRESHOLD = 30.0f;     // NEW: Boost scaling: 0-30 maps to 0.0-1.0x multiplier

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];

			// ====================================================================
			// Phase 1: First Jump Detection
			// Detect transition from ground to air
			// ====================================================================
			if (prevPlayer.isOnGround && !player.isOnGround) {
				tracker.state = FastAerialTracker::FIRST_JUMP;
				tracker.jumpStartTime = 0.0f;
				tracker.firstJumpHeight = player.pos.z;
				tracker.maxPitchDuringFirstJump = 0.0f;
				tracker.isPitchedUpEnough = false;
				tracker.secondJumpDetected = false;
				tracker.sequenceStartBoost = player.boost;  // NEW: Capture boost at sequence start
				return 0.0f;
			}

			// Timeout: if in air for > 2.5 seconds without second jump, reset
			if (tracker.state != FastAerialTracker::WAITING) {
				tracker.jumpStartTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
				if (tracker.jumpStartTime > 2.5f) {
					tracker.state = FastAerialTracker::WAITING;
					return 0.0f;
				}
			}

			// ====================================================================
			// Phase 2: Pitch Validation
			// Track pitch angle during first jump to validate upward pitch
			// ====================================================================
			if (tracker.state == FastAerialTracker::FIRST_JUMP) {
				// Calculate pitch: positive = nose up, negative = nose down
				// Using: atan2(forward.z, (forward.x^2 + forward.y^2)^0.5)
				float pitchRadians = atan2(player.rotMat.forward.z,
					sqrt(player.rotMat.forward.x * player.rotMat.forward.x +
						 player.rotMat.forward.y * player.rotMat.forward.y));
				float pitchDegrees = pitchRadians * 57.2958f;  // Convert to degrees

				// Track maximum pitch achieved
				if (pitchDegrees > tracker.maxPitchDuringFirstJump) {
					tracker.maxPitchDuringFirstJump = pitchDegrees;
				}

				// Check if pitch is in proper range (25-65° for 45±20°)
				if (pitchDegrees >= MIN_PITCH_DEGREES && pitchDegrees <= MAX_PITCH_DEGREES) {
					tracker.isPitchedUpEnough = true;
					tracker.state = FastAerialTracker::PITCHED_UP;
				}
			}

			// ====================================================================
			// Phase 3: Second Jump Detection
			// Detect second jump after proper pitch has been achieved
			// ====================================================================
			if (tracker.state == FastAerialTracker::PITCHED_UP &&
				tracker.jumpStartTime > 0.0f && tracker.jumpStartTime <= DOUBLE_JUMP_WINDOW) {

				// Second jump triggered (flip input while airborne)
				if (player.isFlipping && !prevPlayer.isFlipping) {
					tracker.state = FastAerialTracker::SECOND_JUMP;
					tracker.secondJumpDetected = true;
					tracker.maxHeightAfterSecondJump = player.pos.z;
				}
			}

			// Track maximum height after second jump
			if (tracker.state == FastAerialTracker::SECOND_JUMP && player.pos.z > tracker.maxHeightAfterSecondJump) {
				tracker.maxHeightAfterSecondJump = player.pos.z;
			}

			// ====================================================================
			// Phase 4: Reward Trigger
			// Award reward for successful fast aerial with proper pitch
			// ====================================================================
			if (tracker.state == FastAerialTracker::SECOND_JUMP && tracker.secondJumpDetected && tracker.isPitchedUpEnough) {
				// Calculate height gained from second jump
				float heightGained = RS_MAX(0.0f, tracker.maxHeightAfterSecondJump - tracker.firstJumpHeight);

				// Height bonus: scales with altitude gained
				float heightBonus = RS_MIN(MAX_HEIGHT_BONUS, heightGained * HEIGHT_BONUS_SCALE);

				// Pitch accuracy bonus: reward angles close to 45° but accept 15-90°
				float pitchAccuracy = GetPitchAccuracyScore(tracker.maxPitchDuringFirstJump);
				float pitchBonus = RS_MIN(MAX_PITCH_BONUS, pitchAccuracy * PITCH_BONUS_SCALE * 100.0f);

				float reward = BASE_REWARD + heightBonus + pitchBonus;

				// NEW: Apply boost multiplier (linear scaling 0-30 boost)
				// Formula: 0 boost = 0.0x, 30 boost = 1.0x, 100+ boost = 1.0x
				float boostMultiplier = RS_CLAMP(tracker.sequenceStartBoost / BOOST_SCALE_THRESHOLD, 0.0f, 1.0f);
				reward *= boostMultiplier;

				// Reset for next aerial
				tracker.state = FastAerialTracker::WAITING;
				tracker.secondJumpDetected = false;

				return reward;
			}

			return 0.0f;
		}

	protected:
		// Helper: Calculate pitch accuracy score (0-1.0)
		// Higher score near OPTIMAL_PITCH_DEGREES, still good score in MIN_PITCH_DEGREES to 90°
		float GetPitchAccuracyScore(float pitchDegrees) const {
			// Clamp to valid range (15-90°)
			float clampedPitch = RS_CLAMP(pitchDegrees, MIN_PITCH_DEGREES, 90.0f);

			// Calculate distance from optimal (45°)
			float diffFromOptimal = fabs(clampedPitch - OPTIMAL_PITCH_DEGREES);

			// Gaussian-like scoring: peak at 45°, still good at 15° and 90°
			// Using simple formula: 1.0 - (diff / range)^2
			float normalized = diffFromOptimal / (OPTIMAL_PITCH_DEGREES - MIN_PITCH_DEGREES);
			float score = 1.0f - (normalized * normalized);

			return RS_CLAMP(score, 0.0f, 1.0f);
		}
	};

	// ============================================================================
	// AIR DRIBBLE SETUP REWARD - Generic soft-touch setup detection
	// Rewards soft touch sequences that establish air dribble positioning
	// Foundation for advanced air dribble mechanics (arc-specific handling)
	// 2026-02-10 | Rev 1.0 | NJH
	// ============================================================================
	class AirDribbleSetupReward : public Reward {
	private:
		struct SetupMemory {
			// Phase tracking
			bool isInSetupPhase = false;
			int setupStepsCount = 0;
			float setupStartHeight = 0.0f;
			float maxHeightInSetup = 0.0f;

			// Touch quality tracking
			bool firstTouchWasSoft = false;
			float accumulatedSoftness = 0.0f;
			int softTouchCount = 0;

			// Position tracking
			float lastProximityToball = 999999.0f;

			// NEW: Boost tracking
			float setupStartBoost = 0.0f;  // Boost level when setup started
		};

		mutable std::map<uint32_t, SetupMemory> trackers;

		// Configuration
		static constexpr float BALL_ELEVATION_THRESHOLD = 300.0f;  // Ball must be elevated
		static constexpr float PLAYER_ELEVATION_THRESHOLD = 400.0f; // Player must be in air
		static constexpr float SETUP_MAX_VELOCITY_DIFF = 400.0f;   // Max velocity diff for "soft" touch
		static constexpr float PROXIMITY_MIN = 50.0f;              // Minimum proximity to ball
		static constexpr float PROXIMITY_MAX = 300.0f;             // Maximum proximity for good spacing
		static constexpr float SETUP_BASE_REWARD = 0.2f;           // Base reward per frame in setup
		static constexpr float SOFTNESS_BONUS_PER_TOUCH = 0.3f;    // Per soft touch bonus
		static constexpr float POSITION_QUALITY_BONUS = 0.15f;     // For optimal positioning
		static constexpr float PERSISTENCE_BONUS = 0.05f;          // Per 30 steps of setup
		static constexpr int PERSISTENCE_STEP_INTERVAL = 30;       // Steps between persistence bonuses

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			auto& mem = trackers[player.carId];
			float reward = 0.0f;

			// Check if player and ball are in air at good heights
			bool playerInAir = player.pos.z > PLAYER_ELEVATION_THRESHOLD;
			bool ballElevated = state.ball.pos.z > BALL_ELEVATION_THRESHOLD;

			// === PHASE 1: SETUP INITIATION ===
			if (!mem.isInSetupPhase && playerInAir && ballElevated) {
				// Starting setup phase
				mem.isInSetupPhase = true;
				mem.setupStartHeight = player.pos.z;
				mem.setupStepsCount = 0;
				mem.accumulatedSoftness = 0.0f;
				mem.softTouchCount = 0;
				mem.maxHeightInSetup = player.pos.z;
				mem.setupStartBoost = player.boost;  // NEW: Capture boost at setup start
			}

			// === PHASE 2: SETUP MAINTENANCE ===
			if (mem.isInSetupPhase) {
				if (!playerInAir || !ballElevated) {
					// Setup phase ended (landed or ball fell)
					mem.isInSetupPhase = false;
					return 0.0f;
				}

				mem.setupStepsCount++;
				mem.maxHeightInSetup = RS_MAX(mem.maxHeightInSetup, player.pos.z);

				// === PHASE 3: SOFT TOUCH DETECTION ===
				if (player.ballTouchedStep) {
					Vec velDiff = player.vel - state.ball.vel;
					float velocityDifference = velDiff.Length();

					if (velocityDifference < SETUP_MAX_VELOCITY_DIFF) {
						// This is a soft touch!
						mem.softTouchCount++;
						mem.accumulatedSoftness += (1.0f - (velocityDifference / SETUP_MAX_VELOCITY_DIFF));

						if (mem.softTouchCount == 1) {
							mem.firstTouchWasSoft = true;
						}

						// Reward soft touches: stronger bonus on early touches
						float softnessMultiplier = (mem.softTouchCount <= 2) ? 1.0f : 0.5f;
						reward += SOFTNESS_BONUS_PER_TOUCH * softnessMultiplier;
					}
				}

				// === PHASE 4: POSITION QUALITY ===
				Vec toBall = state.ball.pos - player.pos;
				float proximityToBall = toBall.Length();
				mem.lastProximityToball = proximityToBall;

				if (proximityToBall >= PROXIMITY_MIN && proximityToBall <= PROXIMITY_MAX) {
					// Reward being in optimal spacing zone
					float proximityScore = 1.0f - (fabs(proximityToBall - PROXIMITY_MIN) / (PROXIMITY_MAX - PROXIMITY_MIN));
					reward += POSITION_QUALITY_BONUS * proximityScore;
				}

				// === PHASE 5: BASE SETUP REWARD ===
				reward += SETUP_BASE_REWARD;

				// === PHASE 6: PERSISTENCE BONUS ===
				if (mem.setupStepsCount % PERSISTENCE_STEP_INTERVAL == 0 && mem.setupStepsCount > 0) {
					reward += PERSISTENCE_BONUS;
				}
			}

			// NEW: Apply boost multiplier to all setup phase rewards
			if (mem.isInSetupPhase && reward > 0.0f) {
				float boostMultiplier = RS_CLAMP(mem.setupStartBoost / 50.0f, 0.0f, 1.0f);
				reward *= boostMultiplier;
			}

			return reward;
		}

		void PreStep(const GameState& state) override {
			// Cleanup opportunity (optional - could log metrics here)
		}
	};

	// ============================================================================
	// InvertedAerialReward — Rewards flying upside-down (wheels toward ceiling)
	// 2026-02-25 | Rev 1.0 | NJH
	//
	// Detects and rewards the car for maintaining an inverted aerial orientation
	// while airborne. This is the "wheels-up" / upside-down flying mechanic used
	// for ceiling approaches, unexpected redirects, and flip-reset setups.
	//
	// Gate conditions (all must pass):
	//   - Airborne (!isOnGround)
	//   - airTime >= 0.2s (ignore brief inverted flips/rolls off the ground)
	//   - pos.z >= 300 UU (no low-altitude inversions)
	//   - rotMat.up.z <= -0.7 (clearly inverted; threshold for "wheels mostly up")
	//
	// Reward structure:
	//   1. Per-frame inversion quality (framerate-independent via deltaTime):
	//        invertQuality = clamp((-upZ - 0.7) / 0.3, 0, 1)
	//        reward = REWARD_PER_SEC * invertQuality * dt
	//      up.z = -0.7 → quality 0, up.z = -1.0 → quality 1 (perfect upside-down)
	//
	//   2. Ball proximity multiplier (keeps inversion purposeful, not random):
	//      If distToBall < BALL_PROX_MAX (1500 UU):
	//        proxFactor = 1 - distToBall/BALL_PROX_MAX   (0 to 1 as dist approaches 0)
	//        reward *= (1.0 + proxFactor)                 (up to 2x near ball)
	//
	//   3. Ball touch while inverted (one-shot event):
	//        +BALL_TOUCH_BONUS (0.5) when ballTouchedStep && inverted
	//
	// Stateless — no PreStep, no stored state. All logic in GetReward.
	//
	// Typical exposure (Phase 6, 8-tick skip at 120Hz -> 15 steps/s):
	//   Max per step: ~0.067 * invertQuality * proxMultiplier (before weight)
	//   Near ball, perfectly inverted: ~0.133/step -> ~2 units/s at weight 20
	// ============================================================================
	class InvertedAerialReward : public Reward {
	private:
		static constexpr float INVERT_THRESHOLD  = -0.7f;   // up.z below this = inverted
		static constexpr float INVERT_MAX        = -1.0f;   // up.z at perfect upside-down
		static constexpr float MIN_HEIGHT        = 300.0f;  // UU, min altitude
		static constexpr float MIN_AIRTIME       = 0.2f;    // s, ignore brief inversions
		static constexpr float REWARD_PER_SEC    = 1.0f;    // raw reward/s at perfect inversion
		static constexpr float BALL_PROX_MAX     = 1500.0f; // UU, proximity multiplier range
		static constexpr float BALL_TOUCH_BONUS  = 0.5f;    // one-shot for touching while inverted

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Gate 1: must be airborne
			if (player.isOnGround) return 0.0f;

			// Gate 2: minimum airtime (filter out brief ground flips/rolls)
			if (player.airTime < MIN_AIRTIME) return 0.0f;

			// Gate 3: minimum height
			if (player.pos.z < MIN_HEIGHT) return 0.0f;

			// Gate 4: must be clearly inverted (wheels pointing toward ceiling)
			float upZ = player.rotMat.up.z;
			if (upZ > INVERT_THRESHOLD) return 0.0f;

			// Gate 5: only reward when climbing (+Z velocity) or already high up (Z >= 600)
			if (player.vel.z <= 0.0f && player.pos.z < 600.0f) return 0.0f;

			// Inversion quality: 0 at threshold (-0.7), 1 at perfect upside-down (-1.0)
			float invertQuality = RS_CLAMP(
				(INVERT_THRESHOLD - upZ) / (INVERT_THRESHOLD - INVERT_MAX),
				0.0f, 1.0f
			);

			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

			// Per-frame sustained reward, scaled by how upside-down the car is
			float reward = REWARD_PER_SEC * invertQuality * dt;

			// Ball proximity multiplier: up to 2x when very close to ball.
			// Encourages purposeful inversion toward the ball, not aimless spinning.
			float distToBall = (player.pos - state.ball.pos).Length();
			if (distToBall < BALL_PROX_MAX) {
				float proxFactor = 1.0f - RS_CLAMP(distToBall / BALL_PROX_MAX, 0.0f, 1.0f);
				reward *= (1.0f + proxFactor);
			}

			// Event: bonus for touching the ball while inverted
			if (player.ballTouchedStep) {
				reward += BALL_TOUCH_BONUS;
			}

			return reward;
		}
	};

}
