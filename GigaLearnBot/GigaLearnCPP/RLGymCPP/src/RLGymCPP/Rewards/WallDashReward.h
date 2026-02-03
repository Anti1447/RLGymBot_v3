#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"

namespace RLGC {

	// ========================================================================
	// WALL DASH REWARD - 2025-02-02 | Rev 7.1 | NJH
	// ========================================================================
	// Based on ACTUAL wall dash execution mechanics:
	//
	// EXECUTION PATTERN:
	// 1. Drive horizontally on wall (car normal perpendicular to wall normal)
	//    - Allows ±25 degrees nose angle (up/down tilt)
	// 2. Hold throttle continuously
	// 3. Hold air roll (direction: whichever makes hood roll towards +Z ceiling)
	// 4. Hold handbrake continuously
	// 5. Press jump in alternating rhythm: (1-2 frames), (0-2 frames), repeat
	//    Examples: 1 1 0 0 1 1 0 0 | 1 0 1 0 1 0 | 1 1 0 1 1 0 etc.
	//
	// CRITICAL CONSTRAINT - Rev 7.1:
	// - ONLY reward alternating jump patterns when nose is horizontal ±25°
	// - Prevents rewarding random flips/jumps off wall at bad angles
	// - Forces bot to learn proper orientation before attempting dashes
	//
	// WALL LIP DETECTION:
	// - Detects wall lip transitions where ground meets wall (100-200 UU height)
	// - Allows wall dash execution on the lip itself
	//
	// KEY PRINCIPLE:
	// - Reward ALTERNATING jump pattern (main reward source)
	// - ONLY when properly oriented (±25 degrees horizontal)
	// - Minimal rewards for input holding (let bot discover randomly)
	// - Focus on jump sequence detection as primary signal
	//
	// REWARD STRUCTURE:
	// 1. Nearly negligible reward for proper inputs (throttle + air roll + handbrake)
	//    - ONLY when horizontal orientation confirmed
	// 2. Detect alternating jump pattern (1-2 frames, 0-2 frames, repeat)
	//    - ONLY when horizontal orientation confirmed
	// 3. Bonus for maintaining good rhythm
	//    - ONLY when horizontal orientation confirmed
	// 4. Bonus for chaining multiple complete patterns (1-3 per wall contact)
	//    - ONLY when horizontal orientation confirmed
	// ========================================================================

	class WallDashReward : public Reward {
	private:
		// Per-player state tracking
		struct PlayerWallDashState {
			// Wall detection
			bool onWallLastFrame = false;
			Vec wallNormal = Vec(0, 0, 1);  // Current wall normal

			// Jump pattern tracking
			bool jumpPressedLastFrame = false;
			int currentJumpState = 0;  // Current value (0 or 1)
			int framesInCurrentState = 0;  // How many consecutive frames in this state
			int alternationCount = 0;  // How many times we've alternated
			int patternCompleted = 0;  // How many complete patterns detected

			// Timing tracking (for rhythm detection)
			float timeSinceLastPattern = 0.0f;

			// Duration tracking
			int framesOnWallContinuous = 0;
			bool isExecutingDash = false;
		};

		std::unordered_map<int, PlayerWallDashState> playerStates;

		// Configuration constants
		static constexpr float MIN_ENTRY_VEL = 500.0f;           // Min velocity to start dash
		static constexpr float WALL_ALIGNMENT_THRESHOLD = 0.7f;  // cos(45°) for wall normal alignment
		static constexpr float HORIZONTAL_ALIGNMENT_THRESHOLD = 0.85f;  // How horizontal car should be
		static constexpr float MAX_NOSE_ANGLE = 25.0f;           // Max ±25 degrees nose angle from Z-axis
		static constexpr float WALL_LIP_HEIGHT_MIN = 100.0f;     // Min height for wall lip detection
		static constexpr float WALL_LIP_HEIGHT_MAX = 200.0f;     // Max height for wall lip detection
		static constexpr int MAX_FRAMES_PER_STATE = 2;           // Max 1-2 frames per jump state (1 or 0)
		static constexpr int MIN_FRAMES_PER_STATE = 1;           // Min 1 frame per jump state
		static constexpr int MIN_FRAMES_ON_WALL = 10;            // Minimum frames to count as dash
		static constexpr float THROTTLE_THRESHOLD = 0.5f;        // Throttle input threshold
		static constexpr float AIR_ROLL_THRESHOLD = 0.5f;        // Air roll input threshold
		static constexpr float HANDBRAKE_THRESHOLD = 0.5f;       // Handbrake input threshold

	public:
		WallDashReward() = default;

		void Reset(const GameState& initialState) override {
			playerStates.clear();
		}

		float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			int playerId = player.carId;

			if (playerStates.find(playerId) == playerStates.end()) {
				playerStates[playerId] = PlayerWallDashState();
			}

			PlayerWallDashState& pState = playerStates[playerId];
			float reward = 0.0f;

			// ===================================================================
			// STEP 1: Detect if on wall
			// ===================================================================
			bool onWall = IsOnWall(player, pState);

			// ===================================================================
			// STEP 2: Handle wall entry/exit
			// ===================================================================
			if (onWall && !pState.onWallLastFrame) {
				// ENTERING WALL
				float wallVel = GetWallTangentVelocity(player, pState.wallNormal);
				if (wallVel >= MIN_ENTRY_VEL) {
					pState.isExecutingDash = true;
					pState.framesOnWallContinuous = 0;
					pState.alternationCount = 0;
					pState.patternCompleted = 0;
					pState.timeSinceLastPattern = 0.0f;
					pState.currentJumpState = 0;
					pState.framesInCurrentState = 0;
				}
			} else if (!onWall && pState.onWallLastFrame) {
				// EXITING WALL
				if (pState.isExecutingDash && pState.framesOnWallContinuous >= MIN_FRAMES_ON_WALL) {
					reward += EvaluateDashCompletion(pState);
				} else if (pState.isExecutingDash && pState.framesOnWallContinuous > 0) {
					// Fell off too quickly
					reward -= 3.0f;
				}
				pState.isExecutingDash = false;
				pState.framesOnWallContinuous = 0;
			}

			// ===================================================================
			// STEP 3: Detect wall dash execution pattern
			// ===================================================================
			if (onWall && pState.isExecutingDash) {
				pState.framesOnWallContinuous++;

				// Check orientation is horizontal on wall
				bool isHorizontal = IsHorizontalOnWall(player, pState);

				// Check continuous inputs
				bool throttlePressed = player.prevAction.throttle > THROTTLE_THRESHOLD;
				float airRoll = player.prevAction.roll;
				bool correctAirRollDirection = IsCorrectAirRollDirection(player, pState, airRoll);
				bool airRollPressed = std::abs(airRoll) > AIR_ROLL_THRESHOLD && correctAirRollDirection;
				bool handbrakePressed = std::abs(player.prevAction.handbrake) > HANDBRAKE_THRESHOLD;

				// ===============================================================
				// STEP 3A: MINIMAL reward for continuous inputs
				// These are nearly negligible - let bot discover through jump pattern
				// ONLY when properly oriented!
				// ===============================================================
				if (isHorizontal) {
					float inputReward = 0.0f;

					if (throttlePressed) {
						inputReward += 0.0005f;  // Nearly zero
					}

					if (airRollPressed) {
						inputReward += 0.0005f;  // Nearly zero
					}

					if (handbrakePressed) {
						inputReward += 0.0005f;  // Nearly zero
					}

					// Bonus for holding all three simultaneously
					if (throttlePressed && airRollPressed && handbrakePressed) {
						inputReward += 0.001f;  // Nearly zero bonus
					}

					reward += inputReward;
				}

				// ===============================================================
				// STEP 3B: Detect alternating jump pattern (MAIN REWARD)
				// Jump alternates: 1-2 frames, then 0-2 frames, then repeat
				// Examples: 1 1 0 0 1 1 | 1 0 1 0 | 1 1 0 1 1 0
				// CRITICAL FIX (Rev 7.1): ONLY reward when properly oriented!
				// ===============================================================
				// Only process jump patterns if car is oriented horizontally
				if (isHorizontal) {
					bool jumpPressed = player.prevAction.jump > 0.5f;
					int jumpState = jumpPressed ? 1 : 0;

					// Check if state changed
					if (jumpState != pState.currentJumpState) {
						// State transition
						if (pState.framesInCurrentState >= MIN_FRAMES_PER_STATE &&
						    pState.framesInCurrentState <= MAX_FRAMES_PER_STATE) {
							// Valid state duration, transition is good
							pState.alternationCount++;
							reward += 0.5f;  // Reward each valid transition

							// Check if we've completed a full pattern (alternation >= 2 means at least one pair)
							if (pState.alternationCount >= 2) {
								pState.patternCompleted++;
								reward += 3.0f;  // Reward complete pattern

								// Rhythm bonus
								float targetInterval = 4.0f * CommonValues::TICK_TIME;  // ~67ms for 2 frames per state
								float timingError = std::abs(pState.timeSinceLastPattern - targetInterval);

								if (timingError < targetInterval * 0.5f) {
									reward += 1.0f;  // Excellent rhythm
								}

								pState.alternationCount = 0;
								pState.timeSinceLastPattern = 0.0f;
							}
						} else if (pState.framesInCurrentState > MAX_FRAMES_PER_STATE) {
							// Held too long without alternating - small penalty
							reward -= 0.1f;
						}

						// Transition to new state
						pState.currentJumpState = jumpState;
						pState.framesInCurrentState = 1;
					} else {
						// Staying in same state
						pState.framesInCurrentState++;

						// Penalty if staying too long without transition
						if (pState.framesInCurrentState > MAX_FRAMES_PER_STATE * 2) {
							reward -= 0.05f;
						}
					}

					pState.timeSinceLastPattern += CommonValues::TICK_TIME;
				} else {
					// NOT properly oriented - reset the jump pattern state
					// to avoid accumulating pattern credits from random flips
					pState.currentJumpState = 0;
					pState.framesInCurrentState = 0;
					pState.alternationCount = 0;
				}
			} else if (onWall && !pState.isExecutingDash) {
				// On wall but not dashing - reset state
				pState.framesOnWallContinuous++;
			}

			pState.onWallLastFrame = onWall;
			return RS_MAX(reward, 0.0f);  // Clamp negative rewards to 0
		}

	private:
		// ====================================================================
		// WALL DETECTION
		// ====================================================================

		bool IsOnWall(const Player& player, PlayerWallDashState& pState) {
			// Must be marked as on ground
			if (!player.isOnGround) {
				return false;
			}

			// Check if car normal (up vector) is roughly perpendicular to gravity
			// Wall cars have up vectors mostly horizontal
			float gravityAlignment = std::abs(player.rotMat.up.z);
			if (gravityAlignment > 0.4f) {
				return false;  // Too vertical, not on wall
			}

			// Determine which wall and store the normal
			float absX = std::abs(player.pos.x);
			float absY = std::abs(player.pos.y);

			bool nearSideWall = absX > (CommonValues::SIDE_WALL_X - 300.0f);
			bool nearBackWall = absY > (CommonValues::BACK_WALL_Y - 300.0f);
			bool onWallLip = IsOnWallLip(player);

			if (!nearSideWall && !nearBackWall && !onWallLip) {
				return false;
			}

			// Store wall normal
			if (absX > absY) {
				// Side wall
				pState.wallNormal = player.pos.x > 0.0f ? Vec(1, 0, 0) : Vec(-1, 0, 0);
			} else {
				// Back wall
				pState.wallNormal = player.pos.y > 0.0f ? Vec(0, 1, 0) : Vec(0, -1, 0);
			}

			return true;
		}

		// ====================================================================
		// WALL LIP DETECTION (where ground meets wall)
		// ====================================================================

		bool IsOnWallLip(const Player& player) {
			// Wall lip is where the ground transitions into the wall (100-200 UU height)
			float carZ = player.pos.z;

			// Check if in the lip height range
			if (carZ < WALL_LIP_HEIGHT_MIN || carZ > WALL_LIP_HEIGHT_MAX) {
				return false;
			}

			// Check if near wall edges (both dimensions)
			float absX = std::abs(player.pos.x);
			float absY = std::abs(player.pos.y);

			// Side wall lips (near left/right edges)
			bool nearSideWallLip = absX > (CommonValues::SIDE_WALL_X - 300.0f);

			// Back wall lips (near top/bottom edges)
			bool nearBackWallLip = absY > (CommonValues::BACK_WALL_Y - 300.0f);

			return nearSideWallLip || nearBackWallLip;
		}

		// ====================================================================
		// HORIZONTAL ORIENTATION CHECK
		// ====================================================================

		bool IsHorizontalOnWall(const Player& player, const PlayerWallDashState& pState) {
			// Car should be oriented horizontally on the wall
			// Allows ±25 degrees nose angle from Z-axis (up/down tilt)
			Vec carForward = player.rotMat.forward;
			Vec carUp = player.rotMat.up;

			// Forward should be mostly parallel to wall tangent (perpendicular to wall normal)
			float forwardDotNormal = std::abs(carForward.Dot(pState.wallNormal));

			// Up should be roughly horizontal (away from gravity)
			// Allow ±25 degrees from horizontal
			float upDotGravity = carUp.z;  // NOT abs - allows both positive and negative tilt

			// Calculate max Z component for ±25 degrees nose angle
			// sin(25°) ≈ 0.423
			float maxNoseAngleRadians = MAX_NOSE_ANGLE * 3.14159f / 180.0f;
			float maxNoseZ = std::sin(maxNoseAngleRadians);

			// Both should be minimal for horizontal orientation
			// forwardDotNormal < 0.15 means car is roughly parallel to wall tangent
			// std::abs(upDotGravity) < 0.423 means nose angle is ±25 degrees max
			return forwardDotNormal < (1.0f - HORIZONTAL_ALIGNMENT_THRESHOLD) &&
				   std::abs(upDotGravity) < maxNoseZ;
		}

		// ====================================================================
		// AIR ROLL DIRECTION DETECTION
		// ====================================================================

		bool IsCorrectAirRollDirection(const Player& player, PlayerWallDashState& pState, float airRoll) {
			// Determine which air roll direction would rotate car towards +Z

			Vec wallNormal = pState.wallNormal;
			Vec carRight = player.rotMat.right;

			// Cross product: wall normal × car right = direction of air roll effect
			// We want the result to point towards +Z
			Vec rollEffect = wallNormal.Cross(carRight);

			// If rolling in positive direction (airRoll > 0) makes hood go up, good
			// Check if the intended roll direction aligns with +Z rotation
			float dotWithZ = rollEffect.Dot(Vec(0, 0, 1));

			if (airRoll > 0.0f) {
				// Positive air roll should make hood roll up
				return dotWithZ > 0.0f;
			} else if (airRoll < 0.0f) {
				// Negative air roll should make hood roll up
				return dotWithZ < 0.0f;
			}

			return false;
		}

		// ====================================================================
		// VELOCITY ALONG WALL TANGENT (for entry detection only)
		// ====================================================================

		float GetWallTangentVelocity(const Player& player, const Vec& wallNormal) {
			// Get velocity component perpendicular to wall normal (along tangent)
			float velDotNormal = player.vel.Dot(wallNormal);

			// Subtract normal component to get tangent velocity
			Vec tangentVel = player.vel - (wallNormal * velDotNormal);

			return tangentVel.Length();
		}

		// ====================================================================
		// DASH COMPLETION EVALUATION
		// ====================================================================

		float EvaluateDashCompletion(PlayerWallDashState& pState) {
			float reward = 0.0f;

			// Reward based on patterns completed
			if (pState.patternCompleted >= 1) {
				reward += float(pState.patternCompleted) * 4.0f;  // +4 per pattern
			}

			// Bonus for multiple patterns
			if (pState.patternCompleted >= 3) {
				reward += 10.0f;  // Major bonus for full chain (3 patterns)
			} else if (pState.patternCompleted >= 2) {
				reward += 5.0f;  // Bonus for double chain
			}

			// Duration bonus (stayed on wall long enough to execute properly)
			if (pState.framesOnWallContinuous >= 30) {
				reward += 2.0f;
			}

			return reward;
		}
	};

} // namespace RLGC
