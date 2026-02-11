#pragma once
#include "Reward.h"
#include <map>
#include <cmath>

namespace RLGC {

	// ============================================================================
	// DoomseeDisshShotReward - Advanced Passing/Scoring Mechanic
	// Specialized reward for "Doomsee Dish" shot setup and execution
	//
	// Doomsee Dish Sequence:
	// 1. Ball rolls slowly along backboard (low speed, low height)
	// 2. Ball positioning enables "dish" (pass) from backboard
	// 3. Player positioned at backboard, jumps + boosts as ball drops
	// 4. Ball drops in front of goal due to gravity (natural trajectory)
	// 5. Player aerials/dodges toward falling ball and shoots
	//
	// Key Behavioral Goals:
	// - TEACH: Delicate ball control along backboard (NOT fast clearing)
	// - TEACH: Reading ball drops and positioning for aerial conversion
	// - TEACH: Timing jumps to intercept falling ball
	// - PREVENT: High-speed clears that bypass the setup entirely
	//
	// 2026-02-05 | Rev 1.0 | NJH
	// ============================================================================

	class DoomseeDisshShotReward : public Reward {
	public:
		struct DoomseeState {
			enum Phase : uint8_t {
				IDLE = 0,
				BACKBOARD_APPROACH = 1,
				SLOW_ROLL_TRACKING = 2,
				DISH_SETUP = 3,
				GRAVITY_DROP = 4,
				AERIAL_CONVERSION = 5
			};

			// Current phase tracking
			Phase phase = IDLE;
			float phaseStartTime = 0.0f;
			float sequenceStartTime = 0.0f;
			float totalElapsedTime = 0.0f;

			// Sequence metrics
			float minBallSpeed = FLT_MAX;
			float maxBallHeightDuringDrop = 0.0f;
			float ballSpeedAtDish = 0.0f;
			int consecutiveSlowRollTicks = 0;

			// Action flags
			bool playerJumpedAfterDish = false;
			bool playerBoostedAfterDish = false;
			bool touchedBallInSlowRoll = false;

			// Outcome tracking
			bool goalScoredThisSequence = false;
			float accumulatedReward = 0.0f;
		};

	private:
		mutable std::map<uint32_t, DoomseeState> playerStates;
		mutable float globalTime = 0.0f;

		// Phase timeouts
		static constexpr float PHASE_1_TIMEOUT = 3.5f;
		static constexpr float PHASE_2_TIMEOUT = 4.0f;
		static constexpr float PHASE_3_TIMEOUT = 2.5f;
		static constexpr float PHASE_4_TIMEOUT = 3.0f;
		static constexpr float PHASE_5_TIMEOUT = 4.0f;
		static constexpr float MAX_SEQUENCE_TIME = 15.0f;

		// Distance thresholds
		static constexpr float APPROACH_DISTANCE = 800.0f;
		static constexpr float DISH_DISTANCE = 300.0f;
		static constexpr float CLOSE_ENOUGH_FOR_TOUCH = 500.0f;
		static constexpr float BACKBOARD_ZONE_Z = 500.0f;
		static constexpr float BACKBOARD_ZONE_DIST_TO_GOAL = 500.0f;

		// Speed thresholds
		static constexpr float SLOW_ROLL_SPEED_MAX = 300.0f;
		static constexpr float EXCESSIVE_SPEED = 600.0f;
		static constexpr float DROP_MIN_Z_VEL = -100.0f;
		static constexpr float DROP_MAX_Z_VEL = -800.0f;

		// Zones
		static constexpr float SCORING_ZONE_X_MIN = -892.755f;
		static constexpr float SCORING_ZONE_X_MAX = 892.755f;
		static constexpr float SCORING_ZONE_Y_MIN = 3000.0f;
		static constexpr float SCORING_ZONE_Y_MAX = 5120.0f;

		// Reward values
		static constexpr float ENTRY_BONUS_PHASE_3 = 1.2f;
		static constexpr float JUMP_BONUS = 0.8f;
		static constexpr float TOUCH_BONUS = 1.5f;
		static constexpr float GOAL_BONUS = 6.0f;
		static constexpr float SEQUENCE_REWARD_CAP = 25.0f;

		// Penalties
		static constexpr float HIGH_SPEED_PENALTY = -0.15f;
		static constexpr float DRIFT_PENALTY = -0.05f;
		static constexpr float EXCESSIVE_VERTICAL_PENALTY = -0.08f;

	public:
		virtual void Reset(const GameState& initialState) override {
			playerStates.clear();
			globalTime = 0.0f;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			float dt = state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
			globalTime += dt;

			auto& dState = playerStates[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];

			// ====================================================================
			// PHASE 0: IDLE - Detect doomsee sequence start
			// ====================================================================
			if (dState.phase == DoomseeState::IDLE) {
				if (ShouldEnterBackboardApproach(player, state)) {
					dState.phase = DoomseeState::BACKBOARD_APPROACH;
					dState.phaseStartTime = globalTime;
					dState.sequenceStartTime = globalTime;
					dState.accumulatedReward = 0.0f;
					return 0.0f;
				}
				return 0.0f;
			}

			// Check for phase timeout
			float phaseElapsed = globalTime - dState.phaseStartTime;
			float sequenceElapsed = globalTime - dState.sequenceStartTime;
			bool phaseTimedOut = false;

			switch (dState.phase) {
				case DoomseeState::BACKBOARD_APPROACH:
					phaseTimedOut = (phaseElapsed > PHASE_1_TIMEOUT);
					break;
				case DoomseeState::SLOW_ROLL_TRACKING:
					phaseTimedOut = (phaseElapsed > PHASE_2_TIMEOUT);
					break;
				case DoomseeState::DISH_SETUP:
					phaseTimedOut = (phaseElapsed > PHASE_3_TIMEOUT);
					break;
				case DoomseeState::GRAVITY_DROP:
					phaseTimedOut = (phaseElapsed > PHASE_4_TIMEOUT);
					break;
				case DoomseeState::AERIAL_CONVERSION:
					phaseTimedOut = (phaseElapsed > PHASE_5_TIMEOUT);
					break;
				default:
					phaseTimedOut = false;
			}

			if (phaseTimedOut || sequenceElapsed > MAX_SEQUENCE_TIME || ShouldExitSequence(player, state, dState)) {
				dState.phase = DoomseeState::IDLE;
				return 0.0f;
			}

			// ====================================================================
			// State-specific reward calculation
			// ====================================================================
			float phaseReward = 0.0f;

			switch (dState.phase) {
				case DoomseeState::BACKBOARD_APPROACH:
					phaseReward = GetBackboardApproachReward(player, state, dState);
					// Check transition
					if (ShouldEnterSlowRoll(player, state, dState)) {
						dState.phase = DoomseeState::SLOW_ROLL_TRACKING;
						dState.phaseStartTime = globalTime;
					}
					break;

				case DoomseeState::SLOW_ROLL_TRACKING:
					phaseReward = GetSlowRollReward(player, state, dState);
					// Check transition
					if (ShouldEnterDishSetup(player, state, dState)) {
						dState.phase = DoomseeState::DISH_SETUP;
						dState.phaseStartTime = globalTime;
						dState.ballSpeedAtDish = state.ball.vel.Length();
					}
					break;

				case DoomseeState::DISH_SETUP:
					phaseReward = GetDishSetupReward(player, state, dState);
					// Track jump/boost after entering phase
					if (!prevPlayer.isOnGround && player.isOnGround == false) {
						dState.playerJumpedAfterDish = true;
					}
					if (player.prevAction.boost > 0.5f) {
						dState.playerBoostedAfterDish = true;
					}
					// Check transition
					if (ShouldEnterGravityDrop(player, state, dState)) {
						dState.phase = DoomseeState::GRAVITY_DROP;
						dState.phaseStartTime = globalTime;
						dState.maxBallHeightDuringDrop = state.ball.pos.z;
					}
					break;

				case DoomseeState::GRAVITY_DROP:
					phaseReward = GetGravityDropReward(player, state, dState);
					// Track max height during drop
					dState.maxBallHeightDuringDrop = RS_MAX(dState.maxBallHeightDuringDrop, state.ball.pos.z);
					// Check transition
					if (ShouldEnterAerialConversion(player, state, dState)) {
						dState.phase = DoomseeState::AERIAL_CONVERSION;
						dState.phaseStartTime = globalTime;
					}
					break;

				case DoomseeState::AERIAL_CONVERSION:
					phaseReward = GetAerialConversionReward(player, state, dState);
					// Track goal
					if (state.goalScored) {
						dState.goalScoredThisSequence = true;
					}
					break;

				default:
					phaseReward = 0.0f;
			}

			// Accumulate rewards and check cap
			dState.accumulatedReward += phaseReward;
			if (dState.accumulatedReward > SEQUENCE_REWARD_CAP) {
				// Sequence exhausted, return to idle
				dState.phase = DoomseeState::IDLE;
				return 0.0f;
			}

			return phaseReward;
		}

		// Public accessor for other rewards to check if player is in doomsee sequence
		bool IsPlayerInDoomseeSequence(uint32_t carId) const {
			auto it = playerStates.find(carId);
			if (it == playerStates.end()) return false;
			return it->second.phase != DoomseeState::IDLE;
		}

	private:
		// ====================================================================
		// Detection Helpers
		// ====================================================================

		bool IsBallNearBackboard(const Vec& ballPos) const {
			bool inZone = ballPos.z < BACKBOARD_ZONE_Z;
			float distToGoalLine = RS_MIN(
				fabs(fabs(ballPos.y) - CommonValues::BACK_WALL_Y),
				fabs(fabs(ballPos.x) - CommonValues::SIDE_WALL_X)
			);
			bool nearGoalLine = distToGoalLine < BACKBOARD_ZONE_DIST_TO_GOAL;
			return inZone && nearGoalLine;
		}

		bool IsBallRollingSlowly(const Vec& ballVel) const {
			float speed = ballVel.Length();
			return (speed < SLOW_ROLL_SPEED_MAX) && (fabs(ballVel.z) < 50.0f);
		}

		bool IsBallDroppingFromBackboard(const Vec& ballVel, float prevZ, float currZ) const {
			bool zDecreasing = currZ < prevZ - 5.0f;
			bool velocityNegative = ballVel.z < DROP_MIN_Z_VEL;
			return zDecreasing && velocityNegative;
		}

		bool IsInScoringZone(const Vec& ballPos, Team playerTeam) const {
			float goalY = (playerTeam == Team::BLUE) ? -SCORING_ZONE_Y_MAX : SCORING_ZONE_Y_MAX;
			float distFromGoal = fabs(ballPos.y - goalY);
			bool inYRange = (distFromGoal < (SCORING_ZONE_Y_MAX - SCORING_ZONE_Y_MIN));
			bool inXRange = (fabs(ballPos.x) <= SCORING_ZONE_X_MAX);
			return inYRange && inXRange;
		}

		float DistToBackboardSurface(const Vec& pos) const {
			float distX = CommonValues::SIDE_WALL_X - fabs(pos.x);
			float distY = CommonValues::BACK_WALL_Y - fabs(pos.y);
			return RS_MAX(0.0f, RS_MIN(distX, distY));
		}

		bool IsPlayerSetupForDish(const Player& player, const GameState& state) const {
			// Must be on ground
			if (player.pos.z > 150.0f) return false;

			// Must be near ball
			float distToBall = (player.pos - state.ball.pos).Length();
			if (distToBall > DISH_DISTANCE) return false;

			// Must be on/near backboard
			if (DistToBackboardSurface(player.pos) > 250.0f) return false;

			// Bonus: Facing forward
			Vec goalDir = (player.team == Team::BLUE) ? Vec(0, 5120, 0) : Vec(0, -5120, 0);
			Vec dirToGoal = (goalDir - player.pos).Normalized();
			return player.rotMat.forward.Dot(dirToGoal) > 0.3f;
		}

		// ====================================================================
		// Transition Logic
		// ====================================================================

		bool ShouldEnterBackboardApproach(const Player& player, const GameState& state) const {
			if (!IsBallNearBackboard(state.ball.pos)) return false;
			float distToBall = (player.pos - state.ball.pos).Length();
			return distToBall < APPROACH_DISTANCE;
		}

		bool ShouldEnterSlowRoll(const Player& player, const GameState& state,
		                         const DoomseeState& dState) const {
			if (globalTime - dState.phaseStartTime + 0.05f > PHASE_1_TIMEOUT) return false;
			float distToBall = (player.pos - state.ball.pos).Length();
			if (distToBall > 300.0f) return false;
			return IsBallRollingSlowly(state.ball.vel);
		}

		bool ShouldEnterDishSetup(const Player& player, const GameState& state,
		                          const DoomseeState& dState) const {
			if (globalTime - dState.phaseStartTime + 0.05f > PHASE_2_TIMEOUT) return false;
			if (state.ball.vel.Length() > 150.0f) return false;
			return IsPlayerSetupForDish(player, state);
		}

		bool ShouldEnterGravityDrop(const Player& player, const GameState& state,
		                            const DoomseeState& dState) const {
			if (!state.prev) return false;
			return IsBallDroppingFromBackboard(state.ball.vel,
			                                  state.prev->ball.pos.z,
			                                  state.ball.pos.z);
		}

		bool ShouldEnterAerialConversion(const Player& player, const GameState& state,
		                                 const DoomseeState& dState) const {
			bool inZone = IsInScoringZone(state.ball.pos, player.team);
			bool dropping = state.ball.vel.z < DROP_MIN_Z_VEL;
			return inZone || dropping;
		}

		bool ShouldExitSequence(const Player& player, const GameState& state,
		                        const DoomseeState& dState) const {
			// Goal scored
			if (state.goalScored) return true;

			// Ball out of bounds
			if (fabs(state.ball.pos.x) > 4500.0f) return true;
			if (fabs(state.ball.pos.y) > 5500.0f) return true;

			// Ball went too high (lost control)
			if (state.ball.pos.z > 2000.0f) return true;

			return false;
		}

		// ====================================================================
		// Phase Reward Functions
		// ====================================================================

		float GetBackboardApproachReward(const Player& player, const GameState& state,
		                                  const DoomseeState& dState) const {
			float reward = 0.0f;

			float distToBall = (player.pos - state.ball.pos).Length();
			if (distToBall > APPROACH_DISTANCE) return 0.0f;

			float proximity = 1.0f - RS_CLAMP(distToBall / APPROACH_DISTANCE, 0.0f, 1.0f);
			reward += 0.08f * proximity;

			float backboardDist = DistToBackboardSurface(player.pos);
			if (backboardDist < 300.0f) {
				float backboardBonus = (1.0f - RS_CLAMP(backboardDist / 300.0f, 0.0f, 1.0f)) * 0.05f;
				reward += backboardBonus;
			}

			return reward;
		}

		float GetSlowRollReward(const Player& player, const GameState& state,
		                        const DoomseeState& dState) const {
			float reward = 0.0f;
			float ballSpeed = state.ball.vel.Length();

			// PENALTY: High-speed contact
			if (ballSpeed > EXCESSIVE_SPEED) {
				return HIGH_SPEED_PENALTY;
			}

			// REWARD: Slow rolling
			if (ballSpeed < SLOW_ROLL_SPEED_MAX) {
				float speedRatio = 1.0f - (ballSpeed / SLOW_ROLL_SPEED_MAX);
				reward += 0.12f * speedRatio;
			}

			// REWARD: Ball in goal width
			if (fabs(state.ball.pos.x) <= SCORING_ZONE_X_MAX) {
				reward += 0.08f;
			} else {
				return DRIFT_PENALTY;
			}

			// REWARD: Ball low and stable
			if (state.ball.pos.z < 300.0f && fabs(state.ball.vel.z) < 100.0f) {
				reward += 0.05f;
			}

			// PENALTY: Excessive vertical motion
			if (fabs(state.ball.vel.z) > 200.0f) {
				reward += EXCESSIVE_VERTICAL_PENALTY;
			}

			return reward;
		}

		float GetDishSetupReward(const Player& player, const GameState& state,
		                         const DoomseeState& dState) const {
			float reward = 0.0f;

			// One-time entry bonus
			if (globalTime - dState.phaseStartTime < 0.05f) {
				reward += ENTRY_BONUS_PHASE_3;
			}

			// Jump bonus
			if (!player.isOnGround && dState.playerJumpedAfterDish) {
				reward += JUMP_BONUS;
			}

			// Boost usage bonus
			if (player.prevAction.boost > 0.5f && player.vel.z > 0.0f) {
				reward += 0.4f;
			}

			// Timing bonus
			if (state.ball.vel.Length() < 100.0f) {
				reward += 0.3f;
			}

			return reward;
		}

		float GetGravityDropReward(const Player& player, const GameState& state,
		                           const DoomseeState& dState) const {
			float reward = 0.0f;

			// X-drift penalty
			float xDrift = fabs(state.ball.pos.x);
			if (xDrift > SCORING_ZONE_X_MAX) {
				return -0.2f;
			}

			// X-centering bonus
			if (xDrift < 600.0f) {
				float xQuality = 1.0f - (xDrift / 600.0f);
				reward += 0.3f * xQuality;
			}

			// Y-progress bonus
			float goalY = (player.team == Team::BLUE) ? -SCORING_ZONE_Y_MAX : SCORING_ZONE_Y_MAX;
			float distFromGoal = fabs(state.ball.pos.y - goalY);

			if (distFromGoal < 3500.0f && distFromGoal > 1000.0f) {
				float progress = 1.0f - RS_CLAMP(distFromGoal / 3500.0f, 0.0f, 1.0f);
				reward += 0.4f * progress;
			}

			// Natural drop bonus
			if (state.ball.vel.z < DROP_MAX_Z_VEL && state.ball.vel.z > DROP_MIN_Z_VEL) {
				reward += 0.2f;
			}

			return reward;
		}

		float GetAerialConversionReward(const Player& player, const GameState& state,
		                                 const DoomseeState& dState) const {
			if (player.isOnGround) return 0.0f;

			float reward = 0.0f;
			float distToBall = (player.pos - state.ball.pos).Length();

			// Proximity reward
			if (distToBall < CLOSE_ENOUGH_FOR_TOUCH) {
				float proximity = 1.0f - RS_CLAMP(distToBall / CLOSE_ENOUGH_FOR_TOUCH, 0.0f, 1.0f);
				reward += 0.5f * proximity;
			}

			// Directional accuracy
			Vec toBall = (state.ball.pos - player.pos).Normalized();
			Vec velDir = player.vel.Normalized();
			float dirAccuracy = RS_CLAMP(velDir.Dot(toBall), 0.0f, 1.0f);
			reward += 0.4f * dirAccuracy;

			// Touch reward
			if (player.ballTouchedStep) {
				reward += TOUCH_BONUS;

				// Shooting accuracy
				Vec oppGoal = (player.team == Team::BLUE) ?
					CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
				Vec toGoal = (oppGoal - state.ball.pos).Normalized();
				Vec ballVelDir = state.ball.vel.Normalized();
				float shootingAccuracy = RS_CLAMP(ballVelDir.Dot(toGoal), 0.0f, 1.0f);
				reward += 0.8f * shootingAccuracy;
			}

			// Goal bonus
			if (state.goalScored) {
				reward += GOAL_BONUS;
			}

			return reward;
		}
	};

}
