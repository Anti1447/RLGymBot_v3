#pragma once
#include "Reward.h"
#include <map>
#include <cmath>
#include "RewardMath.h"

namespace RLGC {

	// ============================================================================
	// CompositeFlickReward - Unified flick detection combining all flick types
	//
	// Merges the following flick rewards into a single comprehensive system:
	// 1. FlickExplorationReward - Rewards non-standard, exploration-focused flicks
	// 2. MustyFlickReward - Back flip flicks with specific pitch validation
	// 3. BackFlipFlickReward - Simple back flip flick detection
	// 4. AdvancedFlickReward - High-speed flicks with directional emphasis
	// 5. FlickOnGoalReward - Flicks directed toward the opponent goal
	// 6. FlickReward - Basic flick detection with acceleration-based rewards
	//
	// 2026-02-16 | Rev 1.0 | Composite Design
	// ============================================================================
	class CompositeFlickReward : public Reward {
	private:
		// ====================================================================
		// STATE TRACKING
		// ====================================================================
		mutable std::map<uint32_t, float> ballOnCarTime;
		mutable std::map<uint32_t, bool> ballWasOnCar;
		mutable std::map<uint32_t, Vec> lastCarForward;
		mutable std::map<uint32_t, Vec> lastCarUp;
		mutable std::map<uint32_t, float> lastCarPitch;
		mutable std::map<uint32_t, float> lastCarYaw;

		// Musty-specific tracking
		struct MustyTracker {
			enum State { WAITING, FLIP_DETECTED, BALL_CONTACT, COMPLETED };
			State state = WAITING;
			float flipStartTime = -1.0f;
			float minPitchDuringFlip = 0.0f;
			bool ballTouchedDuringFlip = false;
		};
		mutable std::map<uint32_t, MustyTracker> mustyTrackers;

		// FlickOnGoal tracking
		struct FlickOnGoalPending {
			float timeLeft = 0.0f;
			bool armed = false;
		};
		mutable std::map<uint32_t, FlickOnGoalPending> flickOnGoalPending;

		// ====================================================================
		// GEOMETRY GATES (shared across all flick types)
		// ====================================================================
		static constexpr float MIN_HEIGHT_ABOVE_CAR = 30.0f;
		static constexpr float MAX_HEIGHT_ABOVE_CAR = 155.0f;
		static constexpr float MAX_HORIZ_DIST = 120.0f;

		// ====================================================================
		// BASIC FLICK CONFIGURATION
		// ====================================================================
		static constexpr float MIN_ON_CAR_TIME = 0.25f;  // Time ball must be on car to count as dribble
		static constexpr float MIN_FLICK_SPEED = 600.0f; // Minimum ball speed for flick detection

		// ====================================================================
		// STANDARD FLICK REWARD PARAMETERS
		// ====================================================================
		static constexpr float MIN_ACCEL = 15000.0f;
		static constexpr float GOOD_ACCEL = 80000.0f;
		static constexpr float GREAT_ACCEL = 140000.0f;
		static constexpr float INSANE_ACCEL = 200000.0f;
		static constexpr float MAX_BASIC_FLICK_REWARD = 25.0f;

		// ====================================================================
		// ADVANCED FLICK PARAMETERS
		// ====================================================================
		static constexpr float MIN_ADVANCED_SPEED = 1600.0f;
		static constexpr float GOOD_ADVANCED_SPEED = 2200.0f;
		static constexpr float GREAT_ADVANCED_SPEED = 2500.0f;
		static constexpr float INSANE_ADVANCED_SPEED = 3000.0f;
		static constexpr float MAX_ADVANCED_FLICK_REWARD = 35.0f;
		static constexpr float ANGLE_TOLERANCE = 30.0f;

		// ====================================================================
		// BACK FLIP FLICK PARAMETERS
		// ====================================================================
		static constexpr float MIN_BACKFLIP_SPEED = 1500.0f;
		static constexpr float BACKFLIP_BASE_REWARD = 6.0f;
		static constexpr float MIN_PITCH_DEGREES = -15.0f;  // Negative pitch for back flip

		// ====================================================================
		// MUSTY FLICK PARAMETERS
		// ====================================================================
		static constexpr float PITCH_BAND_1_CENTER = 90.0f;
		static constexpr float PITCH_BAND_2_CENTER = 270.0f;
		static constexpr float PITCH_TOLERANCE = 20.0f;
		static constexpr float MUSTY_MIN_FLICK_SPEED = 1500.0f;
		static constexpr float MUSTY_MAX_FLICK_SPEED = 2500.0f;
		static constexpr float MUSTY_BASE_REWARD = 7.0f;
		static constexpr float MUSTY_GOAL_DIRECTION_SCALE = 0.3f;
		static constexpr float MUSTY_BALL_FORWARD_MIN = 100.0f;
		static constexpr float MUSTY_BALL_FORWARD_MAX = 1500.0f;
		static constexpr float MUSTY_BALL_HEIGHT_MIN = 50.0f;
		static constexpr float MUSTY_BALL_HEIGHT_MAX = 400.0f;
		static constexpr float MUSTY_BALL_HORIZONTAL_MAX = 200.0f;

		// ====================================================================
		// FLICK ON GOAL PARAMETERS
		// ====================================================================
		static constexpr float FOG_PENDING_WINDOW = 0.12f;
		static constexpr float FOG_SLOW_KPH = 60.0f;
		static constexpr float FOG_FAST_KPH = 120.0f;
		static constexpr float FOG_MIN_REWARD = 9.0f;
		static constexpr float FOG_MAX_REWARD = 18.0f;
		static constexpr float FOG_MAX_FLICK_HEIGHT_AT_GOAL = 580.0f;
		static constexpr float FOG_PARTIAL_TOWARD_GOAL = 1200.0f;
		static constexpr float FOG_PARTIAL_MIN = 3.0f;
		static constexpr float FOG_PARTIAL_MAX = 7.0f;

	public:
		virtual void Reset(const GameState& initialState) override {
			ballOnCarTime.clear();
			ballWasOnCar.clear();
			lastCarForward.clear();
			lastCarUp.clear();
			lastCarPitch.clear();
			lastCarYaw.clear();
			mustyTrackers.clear();
			flickOnGoalPending.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			const float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
			const uint32_t id = player.carId;

			// ====================================================================
			// SHARED GEOMETRY & STATE TRACKING
			// ====================================================================
			Vec relBallPos = state.ball.pos - player.pos;
			float heightAboveCar = relBallPos.z;
			float horizDist = Vec(relBallPos.x, relBallPos.y, 0).Length();

			bool ballOnCar =
				(heightAboveCar > MIN_HEIGHT_ABOVE_CAR &&
					heightAboveCar < MAX_HEIGHT_ABOVE_CAR &&
					horizDist < MAX_HORIZ_DIST);

			// Track ball on car time
			auto& tOnCar = ballOnCarTime[id];
			if (ballOnCar && player.isOnGround) {
				tOnCar += dt;
			} else if (!ballOnCar) {
				tOnCar = 0.0f;
			}

			// Update orientation tracking for exploration/musty flicks
			auto& wasOnCar = ballWasOnCar[id];
			lastCarForward[id] = player.rotMat.forward;
			lastCarUp[id] = player.rotMat.up;
			lastCarPitch[id] = player.angVel.x;
			lastCarYaw[id] = player.angVel.z;
			bool wasPrev = wasOnCar;
			wasOnCar = ballOnCar;

			// ====================================================================
			// CHANNEL 1: MUSTY FLICK DETECTION
			// ====================================================================
			const Player& prevPlayer = state.prev->players[player.index];
			float mustyReward = EvaluateMustyFlick(player, prevPlayer, state, dt, id, tOnCar);
			if (mustyReward > 0.0f) {
				tOnCar = 0.0f;
				return mustyReward;
			}

			// ====================================================================
			// CHANNEL 2: FLICK TRIGGER (ball leaves car after dribble)
			// ====================================================================
			if (player.ballTouchedStep && !ballOnCar && tOnCar >= MIN_ON_CAR_TIME) {
				float ballSpeed = state.ball.vel.Length();

				// ====================================================================
				// CHANNEL 2A: BACK FLIP FLICK
				// ====================================================================
				if (ballSpeed >= MIN_BACKFLIP_SPEED) {
					float backflipReward = EvaluateBackFlipFlick(player, state, prevPlayer);
					if (backflipReward > 0.0f) {
						tOnCar = 0.0f;
						return backflipReward;
					}
				}

				// ====================================================================
				// CHANNEL 2B: EXPLORATION FLICK (non-standard tricks)
				// ====================================================================
				if (wasPrev) {
					float explorationReward = EvaluateExplorationFlick(
						player, state, lastCarForward[id], lastCarUp[id],
						lastCarPitch[id], lastCarYaw[id], ballSpeed);
					if (explorationReward > 0.0f) {
						tOnCar = 0.0f;
						return explorationReward;
					}
				}

				// ====================================================================
				// CHANNEL 2C: ADVANCED FLICK (speed + direction emphasis)
				// ====================================================================
				float advancedReward = EvaluateAdvancedFlick(player, state, ballSpeed);
				if (advancedReward > 0.0f) {
					tOnCar = 0.0f;
					return advancedReward;
				}

				// ====================================================================
				// CHANNEL 2D: BASIC FLICK (acceleration-based)
				// ====================================================================
				float basicReward = EvaluateBasicFlick(player, state, dt);
				if (basicReward > 0.0f) {
					tOnCar = 0.0f;
					return basicReward;
				}
			}

			// ====================================================================
			// CHANNEL 3: FLICK ON GOAL (pending window tracking)
			// ====================================================================
			if (player.ballTouchedStep && tOnCar >= MIN_ON_CAR_TIME) {
				flickOnGoalPending[id] = FlickOnGoalPending{ FOG_PENDING_WINDOW, true };
			}

			auto itPending = flickOnGoalPending.find(id);
			if (itPending != flickOnGoalPending.end()) {
				FlickOnGoalPending& p = itPending->second;
				p.timeLeft -= dt;

				if (!ballOnCar) {
					float ballSpeed = state.ball.vel.Length();
					if (ballSpeed >= MIN_FLICK_SPEED) {
						float fogReward = EvaluateFlickOnGoal(player, state, ballSpeed);
						if (fogReward > 0.0f) {
							flickOnGoalPending.erase(itPending);
							tOnCar = 0.0f;
							return fogReward;
						}
					}

					// Pending window expired
					if (p.timeLeft <= 0.0f) {
						flickOnGoalPending.erase(itPending);
					}
				}
			}

			// ====================================================================
			// MINIMAL DRIBBLE SUSTAIN SHAPING
			// ====================================================================
			if (ballOnCar && player.isOnGround && tOnCar > 0.05f) {
				return 0.12f;  // Minimal shaping to encourage possession
			}

			return 0.0f;
		}

	private:
		// ====================================================================
		// CHANNEL 1: MUSTY FLICK EVALUATION
		// ====================================================================
		float EvaluateMustyFlick(const Player& player, const Player& prevPlayer,
			const GameState& state, float dt, uint32_t id, float tOnCar) const {

			auto& tracker = mustyTrackers[id];

			// Phase 1: Flip Input Detection
			if (player.isFlipping && !prevPlayer.isFlipping) {
				float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;

				if (IsValidMustyPitch(pitchDegrees)) {
					tracker.state = MustyTracker::FLIP_DETECTED;
					tracker.flipStartTime = 0.0f;
					tracker.minPitchDuringFlip = pitchDegrees;
				} else {
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}
			}

			// Timeout: if flipping for > 1.5 seconds, reset
			if (tracker.state != MustyTracker::WAITING) {
				tracker.flipStartTime += dt;
				if (tracker.flipStartTime > 1.5f) {
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}
			}

			// Track minimum pitch during flip
			if (tracker.state == MustyTracker::FLIP_DETECTED && player.isFlipping) {
				float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;
				if (IsValidMustyPitch(pitchDegrees)) {
					tracker.minPitchDuringFlip = RS_MIN(tracker.minPitchDuringFlip, fabs(pitchDegrees));
				}
			}

			// Phase 2: Ball Contact Validation
			if (tracker.state == MustyTracker::FLIP_DETECTED && player.ballTouchedStep) {
				if (IsBallInMustyZone(player, state)) {
					tracker.state = MustyTracker::BALL_CONTACT;
					tracker.ballTouchedDuringFlip = true;
				}
			}

			// Phase 3: Reward Trigger
			if (tracker.state == MustyTracker::BALL_CONTACT && tracker.ballTouchedDuringFlip) {
				float ballSpeed = state.ball.vel.Length();

				if (ballSpeed < MUSTY_MIN_FLICK_SPEED) {
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}

				if (!HasForwardDominantAccel(state)) {
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}

				float reward = CalculateMustyReward(ballSpeed, player, state);
				tracker.state = MustyTracker::WAITING;
				tracker.ballTouchedDuringFlip = false;

				return reward;
			}

			return 0.0f;
		}

		bool IsValidMustyPitch(float pitchDegrees) const {
			float band1Min = PITCH_BAND_1_CENTER - PITCH_TOLERANCE;
			float band1Max = PITCH_BAND_1_CENTER + PITCH_TOLERANCE;
			float band2Min = PITCH_BAND_2_CENTER - PITCH_TOLERANCE;
			float band2Max = PITCH_BAND_2_CENTER + PITCH_TOLERANCE;

			float normalizedPitch = pitchDegrees;
			if (normalizedPitch < 0.0f) {
				normalizedPitch += 360.0f;
			}

			bool inBand1 = (normalizedPitch >= band1Min && normalizedPitch <= band1Max);
			bool inBand2 = (normalizedPitch >= band2Min && normalizedPitch <= band2Max);

			return inBand1 || inBand2;
		}

		bool IsBallInMustyZone(const Player& player, const GameState& state) const {
			Vec relPos = state.ball.pos - player.pos;

			float height = relPos.z;
			float forward = relPos.Dot(player.rotMat.forward);
			float horizontal = fabs(relPos.Dot(player.rotMat.right));

			bool validForward = (forward > MUSTY_BALL_FORWARD_MIN && forward < MUSTY_BALL_FORWARD_MAX);
			bool validHeight = (height > MUSTY_BALL_HEIGHT_MIN && height < MUSTY_BALL_HEIGHT_MAX);
			bool validHorizontal = (horizontal < MUSTY_BALL_HORIZONTAL_MAX);

			return validForward && validHeight && validHorizontal;
		}

		bool HasForwardDominantAccel(const GameState& state) const {
			Vec ballVel = state.ball.vel;
			float horizontalSpeed = Vec(ballVel.x, ballVel.y, 0).Length();
			float verticalSpeed = fabs(ballVel.z);

			return horizontalSpeed > verticalSpeed;
		}

		float CalculateMustyReward(float ballSpeed, const Player& player, const GameState& state) const {
			float speedScore = RS_CLAMP(ballSpeed / MUSTY_MAX_FLICK_SPEED, 0.0f, 1.0f);

			Vec opponentGoal = (player.team == Team::BLUE) ?
				CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
			float goalAlignment = state.ball.vel.Normalized().Dot(toGoal);
			float directionBonus = RS_MAX(0.0f, goalAlignment);

			return MUSTY_BASE_REWARD * speedScore * (1.0f + MUSTY_GOAL_DIRECTION_SCALE * directionBonus);
		}

		// ====================================================================
		// CHANNEL 2A: BACK FLIP FLICK EVALUATION
		// ====================================================================
		float EvaluateBackFlipFlick(const Player& player, const GameState& state, const Player& prevPlayer) const {
			// Gate 1: Must have been flipping
			bool wasFlipping = (player.isFlipping || prevPlayer.isFlipping);
			if (!wasFlipping) return 0.0f;

			// Gate 2: Ball must be on top of car
			Vec relPos = state.ball.pos - player.pos;
			float height = relPos.Dot(player.rotMat.up);
			float horizontal = fabs(relPos.Dot(player.rotMat.right));

			bool ballOnCar = (height >= MIN_HEIGHT_ABOVE_CAR && height <= MAX_HEIGHT_ABOVE_CAR &&
						      horizontal <= MAX_HORIZ_DIST);
			if (!ballOnCar) return 0.0f;

			// Gate 3: Must have negative pitch (back flip orientation)
			float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;
			if (pitchDegrees > MIN_PITCH_DEGREES) return 0.0f;

			// SUCCESS! Scale with ball speed — same pattern as musty flick.
			// Range: 3.0 (slow/weak) → 6.0 (2000+ UU/s). Max unchanged.
			float speedScale = RS_CLAMP(state.ball.vel.Length() / 2000.0f, 0.0f, 1.0f);
			return BACKFLIP_BASE_REWARD * (0.5f + 0.5f * speedScale);
		}

		// ====================================================================
		// CHANNEL 2B: EXPLORATION FLICK EVALUATION
		// ====================================================================
		float EvaluateExplorationFlick(const Player& player, const GameState& state,
			Vec prevForward, Vec prevUp, float prevPitch, float prevYaw, float ballSpeed) const {

			Vec curForward = player.rotMat.forward;
			Vec curUp = player.rotMat.up;

			float forwardAlignment = prevForward.Dot(curForward);
			float upAlignment = prevUp.Dot(curUp);

			float pitchChange = std::abs(player.angVel.x - prevPitch);
			float yawChange = std::abs(player.angVel.z - prevYaw);

			float reward = 0.0f;

			// Check for standard front flip flick
			bool isFrontFlip = (forwardAlignment > 0.85f && yawChange < 1.5f && pitchChange > 2.0f);

			// Check for standard diagonal flick
			bool isDiagonalFlip = (forwardAlignment > 0.6f && forwardAlignment <= 0.85f &&
				yawChange > 1.5f && yawChange < 3.5f);

			// Musty flick: backward flip
			bool isMusty = (forwardAlignment < -0.3f && pitchChange > 2.5f);

			// Side flick: mostly yaw rotation
			bool isSideFlick = (std::abs(forwardAlignment) < 0.5f && yawChange > 3.0f);

			// Tornado flick: high yaw + pitch change
			bool isTornado = (yawChange > 4.0f && pitchChange > 3.0f);

			// Upside-down flicks
			bool isInverted = (upAlignment < -0.3f);

			// Reward structure
			if (isFrontFlip || isDiagonalFlip) {
				reward = 0.5f;
			}
			else if (isMusty) {
				reward = 30.0f * (ballSpeed / 2000.0f);
			}
			else if (isSideFlick) {
				reward = 25.0f * (ballSpeed / 2000.0f);
			}
			else if (isTornado) {
				reward = 35.0f * (ballSpeed / 2000.0f);
			}
			else if (isInverted) {
				reward = 28.0f * (ballSpeed / 2000.0f);
			}
			else {
				reward = 20.0f * (ballSpeed / 2000.0f);
			}

			// High-speed bonus scaled by how far above 2200 UU/s (not a flat cliff)
			if (ballSpeed > 2200.0f && !isFrontFlip && !isDiagonalFlip) {
				float speedOver = (ballSpeed - 2200.0f) / 800.0f;  // 0.0 at 2200, 1.0 at 3000+
				reward += 8.0f * RS_CLAMP(speedOver, 0.0f, 1.0f);
			}

			return reward;
		}

		// ====================================================================
		// CHANNEL 2C: ADVANCED FLICK EVALUATION
		// ====================================================================
		float EvaluateAdvancedFlick(const Player& player, const GameState& state, float ballSpeed) const {
			if (!state.prev) return 0.0f;

			Vec ballVel = state.ball.vel;
			Vec prevBallVel = state.prev->ball.vel;
			float prevBallSpeed = prevBallVel.Length();

			// Use vector delta magnitude — captures redirects (90° side flick at same speed)
			// not just speed increases. Old scalar (ballSpeed - prevBallSpeed) was 0 for redirects.
			Vec dv_adv = state.ball.vel - state.prev->ball.vel;
			float velocityChange = dv_adv.Length();

			if (velocityChange < 200.0f) {
				return 0.0f;
			}

			// Speed-based reward
			float speedReward = 0.0f;
			const float minAccel = 200.0f;
			const float goodAccel = 1000.0f;
			const float greatAccel = 1500.0f;
			const float insaneAccel = 2000.0f;

			if (velocityChange >= insaneAccel) {
				speedReward = MAX_ADVANCED_FLICK_REWARD;
			}
			else if (velocityChange >= greatAccel) {
				float t = (velocityChange - greatAccel) / (insaneAccel - greatAccel);
				speedReward = MAX_ADVANCED_FLICK_REWARD * (0.70f + 0.30f * t);
			}
			else if (velocityChange >= goodAccel) {
				float t = (velocityChange - goodAccel) / (greatAccel - goodAccel);
				speedReward = MAX_ADVANCED_FLICK_REWARD * (0.45f + 0.25f * t);
			}
			else {
				float t = (velocityChange - minAccel) / (goodAccel - minAccel);
				speedReward = MAX_ADVANCED_FLICK_REWARD * (0.20f + 0.25f * t);
			}

			// Directional analysis
			Vec carForward = player.rotMat.forward;
			Vec carRight = player.rotMat.right;
			Vec upVector = Vec(0, 0, 1);

			Vec ballDir = (ballSpeed > 0.1f) ? ballVel / ballSpeed : carForward;

			float forwardComponent = ballDir.Dot(carForward);
			float rightComponent = ballDir.Dot(carRight);
			float upComponent = ballDir.Dot(upVector);

			float horizontalAngle = std::atan2(rightComponent, forwardComponent) * 180.0f / 3.14159f;

			// Target angle rewards
			float angleBonus = 0.0f;

			auto CheckAngle = [&](float targetAngle) -> float {
				float diff = std::abs(std::abs(horizontalAngle) - std::abs(targetAngle));
				if (diff > 180.0f) diff = 360.0f - diff;

				if (diff < ANGLE_TOLERANCE) {
					return 1.0f - (diff / ANGLE_TOLERANCE) * 0.3f;
				}
				return 0.0f;
			};

			float angle45 = CheckAngle(45.0f);
			float angle90 = CheckAngle(90.0f);
			float angle135 = CheckAngle(135.0f);

			angleBonus = RS_MAX(RS_MAX(angle45, angle90), angle135);

			if (upComponent > 0.3f) {
				float popBonus = RS_CLAMP(upComponent, 0.0f, 1.0f) * 0.25f;
				angleBonus += popBonus;
			}

			float directionMultiplier = 1.0f + angleBonus;
			float reward = speedReward * directionMultiplier;

			float angVelMag = state.ball.angVel.Length();
			if (angVelMag < 2.0f) {
				reward *= 1.15f;
			}

			return reward;
		}

		// ====================================================================
		// CHANNEL 2D: BASIC FLICK EVALUATION
		// ====================================================================
		float EvaluateBasicFlick(const Player& player, const GameState& state, float dt) const {
			if (!state.prev) return 0.0f;

			Vec dv = state.ball.vel - state.prev->ball.vel;
			float totalAccel = dv.Length() / RS_MAX(dt, 1e-3f);

			Vec carForward = player.rotMat.forward;
			Vec upVector = Vec(0, 0, 1);

			float forwardAccel = dv.Dot(carForward) / RS_MAX(dt, 1e-3f);
			float upwardAccel = dv.Dot(upVector) / RS_MAX(dt, 1e-3f);

			float accelReward = 0.0f;
			if (totalAccel >= INSANE_ACCEL) {
				accelReward = MAX_BASIC_FLICK_REWARD;
			}
			else if (totalAccel >= GREAT_ACCEL) {
				float t = (totalAccel - GREAT_ACCEL) / (INSANE_ACCEL - GREAT_ACCEL);
				accelReward = MAX_BASIC_FLICK_REWARD * (0.75f + 0.25f * t);
			}
			else if (totalAccel >= GOOD_ACCEL) {
				float t = (totalAccel - GOOD_ACCEL) / (GREAT_ACCEL - GOOD_ACCEL);
				accelReward = MAX_BASIC_FLICK_REWARD * (0.45f + 0.30f * t);
			}
			else if (totalAccel >= MIN_ACCEL) {
				float t = (totalAccel - MIN_ACCEL) / (GOOD_ACCEL - MIN_ACCEL);
				accelReward = MAX_BASIC_FLICK_REWARD * (0.15f + 0.30f * t);
			}

			float sauceMultiplier = 1.0f;

			// Forward / upward sauce (lowered thresholds so they fire on more flicks)
			if (forwardAccel > 30000.0f) {
				float forwardBonus = RS_CLAMP((forwardAccel - 30000.0f) / 80000.0f, 0.0f, 0.4f);
				sauceMultiplier += forwardBonus;
			}

			if (upwardAccel > 25000.0f) {
				float upwardBonus = RS_CLAMP((upwardAccel - 25000.0f) / 60000.0f, 0.0f, 0.35f);
				sauceMultiplier += upwardBonus;
			}

			if (forwardAccel > 30000.0f && upwardAccel > 20000.0f) {
				sauceMultiplier += 0.3f;
			}

			// Perpendicular sauce: component of dv perpendicular to the ball’s previous trajectory.
			// Maximum (0.5 bonus) for a pure 90-degree side-flick redirect; 0 for a straight push.
			// This is the core "saucy" metric — how dramatically the flick redirects the ball.
			Vec prevBallVel = state.prev->ball.vel;
			float prevBallSpeed = prevBallVel.Length();
			if (prevBallSpeed > 1.0f) {
				Vec prevBallVelNorm = prevBallVel / prevBallSpeed;
				Vec dvParallel      = prevBallVelNorm * dv.Dot(prevBallVelNorm);
				Vec dvPerp          = dv - dvParallel;
				float perpAccel     = dvPerp.Length() / RS_MAX(dt, 1e-3f);
				float saucePerpBonus = RS_CLAMP(perpAccel / INSANE_ACCEL, 0.0f, 0.5f);
				sauceMultiplier += saucePerpBonus;
			}

			float reward = accelReward * sauceMultiplier;

			// Vector delta gate: opens for any large velocity change including pure redirects.
			// Replaces old scalar speedGate (dSpeed = curSpeed - prevSpeed) which crushed
			// side flicks (pure redirects with ~0 speed delta) to 25% of deserved reward.
			float dvGate = RS_CLAMP(dv.Length() / 400.0f, 0.0f, 1.0f);
			reward *= (0.3f + 0.7f * dvGate);

			return reward;
		}

		// ====================================================================
		// CHANNEL 3: FLICK ON GOAL EVALUATION
		// ====================================================================
		float EvaluateFlickOnGoal(const Player& player, const GameState& state, float ballSpeed) const {
			// Check if flick trajectory will be at valid height when crossing goal line
			float goalLineY = (player.team == Team::BLUE) ? CommonValues::GOAL_LINE_Y : -CommonValues::GOAL_LINE_Y;

			float vyTowardGoal = (player.team == Team::BLUE) ? state.ball.vel.y : -state.ball.vel.y;
			if (vyTowardGoal <= 0.0f) return 0.0f;

			float timeToGoal = (goalLineY - state.ball.pos.y) / state.ball.vel.y;
			if (timeToGoal <= 0.0f || timeToGoal > 2.0f) return 0.0f;

			float heightAtGoal = state.ball.pos.z + state.ball.vel.z * timeToGoal +
								 0.5f * CommonValues::BALL_GRAVITY * timeToGoal * timeToGoal;

			if (heightAtGoal < BALL_RADIUS || heightAtGoal > FOG_MAX_FLICK_HEIGHT_AT_GOAL) {
				return 0.0f;
			}

			// Check if this is NOT an open net scenario
			Vec oppGoal = (player.team == Team::BLUE) ? CommonValues::ORANGE_GOAL_BACK
														: CommonValues::BLUE_GOAL_BACK;
			float playerDistToGoal = (player.pos - oppGoal).Length();

			bool shouldFlick = false;
			for (const auto& other : state.players) {
				if (other.team == player.team) continue;

				float oppDistToGoal = (other.pos - oppGoal).Length();
				if (oppDistToGoal < playerDistToGoal - 300.0f) {
					shouldFlick = true;
					break;
				}
			}

			if (!shouldFlick) return 0.0f;

			// FULL CREDIT if it intersects goal
			if (RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, true, true)) {
				const float slowUU = Math::KPHToVel(FOG_SLOW_KPH);
				const float fastUU = Math::KPHToVel(FOG_FAST_KPH);
				float frac = RS_CLAMP((ballSpeed - slowUU) / (fastUU - slowUU), 0.0f, 1.0f);
				return FOG_MIN_REWARD + (FOG_MAX_REWARD - FOG_MIN_REWARD) * frac;
			}

			// Partial credit if strongly toward opponent goal
			Vec opponentGoal = (player.team == Team::BLUE) ?
				CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
			float ballTowardGoal = state.ball.vel.Dot(toGoal);

			if (ballTowardGoal > FOG_PARTIAL_TOWARD_GOAL) {
				return FOG_PARTIAL_MIN + (FOG_PARTIAL_MAX - FOG_PARTIAL_MIN) *
					   RS_CLAMP((ballTowardGoal - FOG_PARTIAL_TOWARD_GOAL) / 500.0f, 0.0f, 1.0f);
			}

			return 0.0f;
		}
	};

}
