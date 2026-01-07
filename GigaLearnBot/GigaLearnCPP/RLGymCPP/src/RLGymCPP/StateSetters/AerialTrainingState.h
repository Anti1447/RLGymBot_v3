#pragma once
#include "StateSetter.h"
#include "../Math.h"
#include <unordered_map>
#include <mutex>

namespace RLGC {
	// State setter for aerial training
	// - Ball spawns randomly in the air
	// - Ball has no gravity until touched by a car (cars always have normal gravity)
	// - Cars spawn randomly anywhere in the field (including in the air)
	//
	// IMPORTANT: Call AerialTrainingState::UpdateAll(states) in your StepCallback
	// to restore ball gravity when it is touched!
	class AerialTrainingState : public StateSetter {
	private:
		struct TrackingData {
			bool ballTouched;
			uint64_t lastBallUpdateCounter;

			TrackingData() : ballTouched(false), lastBallUpdateCounter(0) {}
		};

		// Global tracking map for all arenas
		static std::unordered_map<Arena*, TrackingData> trackingMap;
		static std::mutex trackingMutex;

	public:
		// Field boundaries for spawning
		constexpr static float
			X_MAX = 3500.0f,
			Y_MAX = 4000.0f,
			Z_MIN_BALL = 500.0f,   // Minimum height for ball spawn
			Z_MAX_BALL = 1800.0f,  // Maximum height for ball spawn
			Z_MIN_CAR = 300.0f,    // Minimum height for car spawn (can be on ground or in air)
			Z_MAX_CAR = 1500.0f;   // Maximum height for car spawn

		// Angular velocity limits
		constexpr static float
			PITCH_MAX = M_PI / 2,
			YAW_MAX = M_PI,
			ROLL_MAX = M_PI,
			CAR_ANGVEL_MAX = 5.5f,
			BALL_ANGVEL_MAX = 6.0f;

		// Velocity limits
		constexpr static float
			BALL_VEL_MAX = 2000.0f,
			CAR_VEL_MAX = 2300.0f;

		void ResetArena(Arena* arena) override {
			using RocketSim::Math::RandFloat;

			// Reset arena to default state (resets boost pads, etc)
			arena->ResetToRandomKickoff();

			// Get or create tracking data for this arena
			std::lock_guard<std::mutex> lock(trackingMutex);
			TrackingData& data = trackingMap[arena];
			data.ballTouched = false;
			data.lastBallUpdateCounter = 0;

			// CRITICAL: Set gravity to zero for the BALL ONLY, not the arena
			// Cars maintain normal gravity always
			arena->ball->_rigidBody.setGravity(btVector3(0, 0, 0));

			// Randomize ball position in the air
			{
				BallState bs = {};
				bs.pos = Math::RandVec(
					Vec(-X_MAX, -Y_MAX, Z_MIN_BALL),
					Vec(X_MAX, Y_MAX, Z_MAX_BALL)
				);

				// Ball starts stationary (zero velocity and angular velocity)
				bs.vel = Vec(0, 0, 0);
				bs.angVel = Vec(0, 0, 0);

				arena->ball->SetState(bs);
				data.lastBallUpdateCounter = bs.updateCounter;
			}

			// Randomize car positions - can be on ground or in air
			for (Car* car : arena->_cars) {
				CarState cs = {};

				// Randomly decide if car should be on ground or in air
				bool onGround = RandFloat() > 0.5f;

				if (onGround) {
					// Spawn on ground
					cs.pos = Math::RandVec(
						Vec(-X_MAX, -Y_MAX, 17.0f),
						Vec(X_MAX, Y_MAX, 17.0f)
					);

					// Random yaw only when on ground
					Angle angle = Angle(RandFloat(-YAW_MAX, YAW_MAX), 0, 0);
					cs.rotMat = angle.ToRotMat();

					// Ground velocity
					Vec randVelDir = Math::RandVec(Vec(-1, -1, 0), Vec(1, 1, 0)).Normalized();
					cs.vel = randVelDir * RandFloat(0, CAR_VEL_MAX);
					cs.vel.z = 0;
					cs.angVel = Vec(0, 0, 0);

				}
				else {
					// Spawn in air
					cs.pos = Math::RandVec(
						Vec(-X_MAX, -Y_MAX, Z_MIN_CAR),
						Vec(X_MAX, Y_MAX, Z_MAX_CAR)
					);

					// Full random rotation in air
					Angle angle = Angle(
						RandFloat(-YAW_MAX, YAW_MAX),
						RandFloat(-PITCH_MAX, PITCH_MAX),
						RandFloat(-ROLL_MAX, ROLL_MAX)
					);
					cs.rotMat = angle.ToRotMat();

					// Random velocity
					Vec randVelDir = Math::RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
					cs.vel = randVelDir * RandFloat(0, CAR_VEL_MAX);

					// Random angular velocity
					cs.angVel = Math::RandVec(
						Vec(-CAR_ANGVEL_MAX, -CAR_ANGVEL_MAX, -CAR_ANGVEL_MAX),
						Vec(CAR_ANGVEL_MAX, CAR_ANGVEL_MAX, CAR_ANGVEL_MAX)
					);
				}

				// Random boost amount
				cs.boost = RandFloat(0, 100);

				car->SetState(cs);
			}
		}

		// Call this in StepCallback to restore ball gravity when it is touched
		// This works even when using CombinedState
		static void UpdateAll(const std::vector<GameState>& states) {
			std::lock_guard<std::mutex> lock(trackingMutex);

			for (const auto& state : states) {
				Arena* arena = state.lastArena;
				if (!arena) continue;

				auto it = trackingMap.find(arena);
				if (it == trackingMap.end()) continue;  // This arena not using aerial training

				TrackingData& data = it->second;

				// Check if state was reset (new episode started)
				BallState currentBall = arena->ball->GetState();
				if (currentBall.updateCounter < data.lastBallUpdateCounter) {
					// State was reset, will be handled in ResetArena
					continue;
				}
				data.lastBallUpdateCounter = currentBall.updateCounter;

				// If ball was already touched, nothing to do
				if (data.ballTouched) continue;

				// Check if any player touched the ball this step
				for (const auto& player : state.players) {
					if (player.ballTouchedStep) {
						data.ballTouched = true;

						// Restore normal gravity for the BALL ONLY
						// Convert from UU to Bullet units
						float gravityBT = RLConst::GRAVITY_Z * UU_TO_BT;
						arena->ball->_rigidBody.setGravity(btVector3(0, 0, gravityBT));

						break;
					}
				}
			}
		}

		// Clean up tracking data for removed arenas
		static void CleanupArena(Arena* arena) {
			std::lock_guard<std::mutex> lock(trackingMutex);
			trackingMap.erase(arena);
		}
	};

	// Initialize static members
	inline std::unordered_map<Arena*, AerialTrainingState::TrackingData> AerialTrainingState::trackingMap;
	inline std::mutex AerialTrainingState::trackingMutex;
}