#pragma once
#include "Framework.h"

namespace RLGC {

	// ========================================================================
	// WALL DASH REWARD - 2025-01-30 | Rev 1.0 | NJH
	// ========================================================================
	// Detects and rewards proper wall dash execution:
	//
	// TECHNIQUE BREAKDOWN:
	// 1. Player on wall (isOnGround=true AND wall-facing normal)
	// 2. Forward momentum (vel >= 1000 uu/s along wall direction)
	// 3. Nose up (car forward vector pitched up 15-25° from wall normal)
	// 4. Handbrake + air roll (perpendicular to wall to maintain wall contact)
	// 5. Jump spam (every other frame for vertical boost)
	//
	// SUCCESS CRITERIA:
	// - Velocity maintained or increased while on wall
	// - Stay on wall continuously (< 0.5sec airborne total)
	//
	// FAILURE CRITERIA:
	// - Fall off wall (airborne for >= 0.5 seconds continuously)
	// - Velocity drops significantly
	//
	// STATE TRACKING:
	// - Per-player: airtime counter, previous state, velocity baseline
	// - Detects continuous wall contact vs interrupted wall dashes
	//
	// ========================================================================

	class WallDashReward : public Reward {
	private:
		// Per-player state tracking
		struct PlayerWallDashState {
			float continuousAirtimeCounter = 0.0f;  // How long continuously airborne
			bool wasOnWallLastFrame = false;        // Previous wall state
			float velocityBaseline = 0.0f;          // Vel when dash started
			bool isInDashAttempt = false;           // Currently attempting a dash
			int jumpActionCounter = 0;              // Track action cadence (jump every other)
			bool wasOnWall = false;                 // Track wall transitions
		};

		std::unordered_map<int, PlayerWallDashState> playerStates;

		// Configuration constants
		static constexpr float MIN_FORWARD_VEL = 1000.0f;      // Minimum wall velocity
		static constexpr float MIN_PITCH_DEGREES = 15.0f;      // Minimum nose-up angle
		static constexpr float MAX_PITCH_DEGREES = 25.0f;      // Maximum nose-up angle
		static constexpr float PITCH_TOLERANCE = 5.0f;         // ±tolerance around target
		static constexpr float MAX_CONTINUOUS_AIRTIME = 0.5f;  // 0.5 seconds = 60 ticks
		static constexpr float WALL_PROXIMITY_THRESHOLD = 200.0f; // Distance from wall edge
		static constexpr float HANDBRAKE_REWARD_THRESHOLD = 0.5f; // Input value threshold
		static constexpr float AIR_ROLL_REWARD_THRESHOLD = 0.5f;  // Input value threshold
		static constexpr float THROTTLE_THRESHOLD = 0.5f;         // Minimum throttle
		static constexpr float VELOCITY_MAINTENANCE_THRESHOLD = 0.95f; // 95% of baseline

	public:
		WallDashReward() = default;

		void Reset(const GameState& initialState) override {
			playerStates.clear();
		}

		float GetReward(const Player& player, const GameState& state) override {
			int playerId = player.carId;
			
			// Ensure player has state tracking
			if (playerStates.find(playerId) == playerStates.end()) {
				playerStates[playerId] = PlayerWallDashState();
			}

			PlayerWallDashState& pState = playerStates[playerId];
			float reward = 0.0f;

			// ===================================================================
			// STEP 1: Detect if on wall
			// ===================================================================
			bool onWall = IsOnWall(player, state);

			// ===================================================================
			// STEP 2: Handle wall state transitions
			// ===================================================================
			if (onWall && !pState.wasOnWallLastFrame) {
				// ENTERING WALL - Start potential dash attempt
				pState.isInDashAttempt = true;
				pState.velocityBaseline = GetWallVelocity(player);
				pState.jumpActionCounter = 0;
				pState.continuousAirtimeCounter = 0.0f;
			} else if (!onWall && pState.wasOnWallLastFrame) {
				// LEAVING WALL - Check if it was a successful dash
				if (pState.isInDashAttempt) {
					reward += EvaluateDashAttempt(player, state, pState);
					pState.isInDashAttempt = false;
				}
			}

			// ===================================================================
			// STEP 3: Continuous wall dash evaluation
			// ===================================================================
			if (onWall && pState.isInDashAttempt) {
				// Give reward for maintaining the dash
				reward += EvaluateActiveDash(player, state, pState);
				pState.continuousAirtimeCounter = 0.0f; // Reset airtime counter
			} else if (!onWall && pState.isInDashAttempt) {
				// Off-wall during dash attempt - accumulate airtime
				pState.continuousAirtimeCounter += CommonValues::TICK_TIME;

				// If airtime exceeds threshold, fail the attempt
				if (pState.continuousAirtimeCounter >= MAX_CONTINUOUS_AIRTIME) {
					reward -= 5.0f; // Penalty for falling off
					pState.isInDashAttempt = false;
				}
			}

			pState.wasOnWallLastFrame = onWall;
			return reward;
		}

	private:
		// ====================================================================
		// WALL DETECTION
		// ====================================================================

		bool IsOnWall(const Player& player, const GameState& state) {
			// Must be marked as on ground
			if (!player.isOnGround) {
				return false;
			}

			// Check if car normal (up vector) is horizontal (wall-facing)
			// Wall normal should be perpendicular to Z axis
			float verticalComponent = std::abs(player.rotMat.up.z);
			
			// If up vector is too vertical (> 0.3 in Z), not on wall
			// Wall-facing cars have up vectors mostly horizontal
			if (verticalComponent > 0.3f) {
				return false;
			}

			// Check proximity to wall boundaries
			float absX = std::abs(player.pos.x);
			float absY = std::abs(player.pos.y);

			// Near side walls (x = ±4096)
			bool nearSideWall = absX > (CommonValues::SIDE_WALL_X - WALL_PROXIMITY_THRESHOLD);

			// Near back walls (y = ±5120)
			bool nearBackWall = absY > (CommonValues::BACK_WALL_Y - WALL_PROXIMITY_THRESHOLD);

			return nearSideWall || nearBackWall;
		}

		// ====================================================================
		// VELOCITY CALCULATION
		// ====================================================================

		float GetWallVelocity(const Player& player) {
			// Get velocity component along wall surface
			// Walls run either along X or Y axis
			float xVel = std::abs(player.vel.x);
			float yVel = std::abs(player.vel.y);

			// Return dominant horizontal velocity (wall-tangent)
			return RS_MAX(xVel, yVel);
		}

		// ====================================================================
		// PITCH ANGLE EVALUATION
		// ====================================================================

		float GetPitchAngle(const Player& player) {
			// Pitch is rotation around Y axis
			// Get angle from forward vector's Z component
			// 0° = horizontal, +90° = pointing up, -90° = pointing down

			Vec forward = player.rotMat.forward;
			float pitchRad = std::asin(RS_CLAMP(forward.z, -1.0f, 1.0f));
			float pitchDeg = pitchRad * 180.0f / 3.14159265f;

			return pitchDeg;
		}

		// ====================================================================
		// ACTIVE DASH EVALUATION
		// ====================================================================

		float EvaluateActiveDash(const Player& player, const GameState& state, 
			PlayerWallDashState& pState) {
			float reward = 0.0f;

			// Reward 1: Maintaining forward velocity on wall
			float currentWallVel = GetWallVelocity(player);
			if (currentWallVel >= MIN_FORWARD_VEL) {
				reward += 0.5f;
			}

			// Reward 2: Proper pitch angle (nose up 15-25°)
			float pitch = GetPitchAngle(player);
			if (pitch >= MIN_PITCH_DEGREES && pitch <= MAX_PITCH_DEGREES) {
				reward += 1.0f;
			} else if (pitch >= (MIN_PITCH_DEGREES - PITCH_TOLERANCE) && 
					   pitch <= (MAX_PITCH_DEGREES + PITCH_TOLERANCE)) {
				reward += 0.5f; // Partial reward if close
			}

			// Reward 3: Handbrake usage
			// Check if handbrake input is significant
			if (std::abs(player.prevAction.handbrake) >= HANDBRAKE_REWARD_THRESHOLD) {
				reward += 0.5f;
			}

			// Reward 4: Air roll in correct direction (perpendicular to wall)
			Vec wallNormal = GetWallNormal(player);
			Vec carRight = player.rotMat.right;
			
			float rollAlignment = std::abs(carRight.Dot(wallNormal));
			if (rollAlignment > 0.7f) {
				// Good alignment - air rolling perpendicular to wall
				if (std::abs(player.prevAction.roll) >= AIR_ROLL_REWARD_THRESHOLD) {
					reward += 0.75f;
				}
			}

			// Reward 5: Jump cadence (not every frame, but periodic)
			pState.jumpActionCounter++;
			if (pState.jumpActionCounter % 2 == 0 && player.prevAction.jump > 0.5f) {
				// Jumping every other frame is ideal
				reward += 0.5f;
			} else if (player.prevAction.jump > 0.5f) {
				// Jumping too frequently
				reward -= 0.1f;
			}

			// Reward 6: Velocity maintenance
			float currentVel = currentWallVel;
			float minVel = pState.velocityBaseline * VELOCITY_MAINTENANCE_THRESHOLD;
			if (currentVel >= minVel) {
				reward += 0.5f;
			}

			return reward;
		}

		// ====================================================================
		// DASH ATTEMPT EVALUATION
		// ====================================================================

		float EvaluateDashAttempt(const Player& player, const GameState& state,
			PlayerWallDashState& pState) {
			float reward = 0.0f;

			// Get final velocity
			float finalVel = GetWallVelocity(player);
			float velGain = finalVel - pState.velocityBaseline;

			// Bonus for speed increase
			if (velGain > 0.0f) {
				reward += 2.0f + (velGain / 200.0f); // Up to +2 bonus
			}

			// Bonus for maintaining velocity
			if (finalVel >= (pState.velocityBaseline * VELOCITY_MAINTENANCE_THRESHOLD)) {
				reward += 1.5f;
			}

			// Bonus for clean execution (short duration, efficient)
			if (pState.continuousAirtimeCounter < (MAX_CONTINUOUS_AIRTIME * 0.5f)) {
				reward += 1.0f; // Very clean execution
			}

			return reward;
		}

		// ====================================================================
		// WALL NORMAL CALCULATION
		// ====================================================================

		Vec GetWallNormal(const Player& player) {
			// Return the normal vector pointing away from the wall
			float absX = std::abs(player.pos.x);
			float absY = std::abs(player.pos.y);

			// Determine which wall we're on
			if (absX > absY) {
				// Side wall (x = ±4096)
				return player.pos.x > 0.0f ? Vec(1, 0, 0) : Vec(-1, 0, 0);
			} else {
				// Back wall (y = ±5120)
				return player.pos.y > 0.0f ? Vec(0, 1, 0) : Vec(0, -1, 0);
			}
		}
	};

} // namespace RLGC
