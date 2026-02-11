#pragma once
#include "Reward.h"
#include <map>
#include <cmath>

namespace RLGC {

	// ============================================================================
	// BackFlipFlickReward - Simple back flip flick detection
	// Rewards back flip flicks (similar to FlickReward but with negative pitch validation)
	// 2026-02-08 | Rev 2.0 | NJH - Simplified version
	//
	// Key features:
	// - Ball on top of car (height 30-150 UU, horizontal < 120 UU)
	// - Back flip execution (negative pitch during flip)
	// - Ball velocity > 1500 UU/s after contact = good flick
	// - No complex state tracking, just simple flip detection
	// ============================================================================
	class BackFlipFlickReward : public Reward {
	private:
		// Simple configuration
		static constexpr float MIN_FLICK_SPEED = 1500.0f;         // Minimum ball speed
		static constexpr float BASE_REWARD = 6.0f;                // Base reward
		static constexpr float MIN_PITCH_DEGREES = -15.0f;        // Must have negative pitch (back flip)

		// Ball on car thresholds (simple checks like FlickReward)
		static constexpr float BALL_HEIGHT_MIN = 30.0f;
		static constexpr float BALL_HEIGHT_MAX = 150.0f;
		static constexpr float BALL_HORIZONTAL_MAX = 120.0f;

	public:
		virtual void Reset(const GameState& initialState) override {
			// No state tracking needed - just simple flick detection
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Gate 1: Player must have touched ball this step
			if (!player.ballTouchedStep) return 0.0f;

			// Gate 2: Ball must be moving fast (good flick)
			float ballSpeed = state.ball.vel.Length();
			if (ballSpeed < MIN_FLICK_SPEED) return 0.0f;

			// Gate 3: Must have been flipping (or flipping started this frame)
			if (!state.prev) return 0.0f;
			const Player& prevPlayer = state.prev->players[player.index];
			bool wasFlipping = (player.isFlipping || prevPlayer.isFlipping);
			if (!wasFlipping) return 0.0f;

			// Gate 4: Ball must be on top of car
			Vec relPos = state.ball.pos - player.pos;
			float height = relPos.Dot(player.rotMat.up);  // Car-relative height
			float horizontal = fabs(relPos.Dot(player.rotMat.right));

			bool ballOnCar = (height >= BALL_HEIGHT_MIN && height <= BALL_HEIGHT_MAX &&
							  horizontal <= BALL_HORIZONTAL_MAX);
			if (!ballOnCar) return 0.0f;

			// Gate 5: Must have negative pitch (back flip orientation)
			float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;
			if (pitchDegrees > MIN_PITCH_DEGREES) return 0.0f;

			// SUCCESS! Simple back flip flick detected
			return BASE_REWARD;
		}
	};

	// ============================================================================
	// MustyFlickReward - Musty/scooping back flip flick
	// Rewards back flip flicks when car pitch is 90-270° (nose up/down arc)
	// and ball is positioned in front and slightly above
	// 2026-02-08 | Rev 1.0 | NJH
	//
	// Key features:
	// - Pitch detection: 70-110° or 250-290° tolerance bands (binary check)
	// - Ball position: In front (>100 UU) and above (>50 UU) relative to car
	// - Detects back flip execution
	// - Creates scooping motion with high forward acceleration
	// - Validates forward velocity > vertical velocity
	// - Goal direction bonus (up to +30%)
	// ============================================================================
	class MustyFlickReward : public Reward {
	private:
		struct MustyTracker {
			enum State { WAITING, FLIP_DETECTED, BALL_CONTACT, COMPLETED };

			State state = WAITING;
			float flipStartTime = -1.0f;
			float minPitchDuringFlip = 0.0f;
			bool ballTouchedDuringFlip = false;
		};

		mutable std::map<uint32_t, MustyTracker> trackers;

		// Configuration
		static constexpr float PITCH_BAND_1_CENTER = 90.0f;        // First pitch band (nose up)
		static constexpr float PITCH_BAND_2_CENTER = 270.0f;       // Second pitch band (nose down)
		static constexpr float PITCH_TOLERANCE = 20.0f;            // ±20° tolerance around each center
		static constexpr float MIN_FLICK_SPEED = 1500.0f;          // Strict: minimum forward speed
		static constexpr float MAX_FLICK_SPEED = 2500.0f;          // Speed saturation
		static constexpr float BASE_REWARD = 7.0f;                 // Base reward for Musty flick
		static constexpr float GOAL_DIRECTION_SCALE = 0.3f;        // Goal direction bonus scale

		// Ball position relative to car (in car's reference frame)
		static constexpr float BALL_FORWARD_MIN = 100.0f;          // Relaxed: forward >100 UU
		static constexpr float BALL_FORWARD_MAX = 1500.0f;         // Reasonable distance
		static constexpr float BALL_HEIGHT_MIN = 50.0f;            // Relaxed: above >50 UU
		static constexpr float BALL_HEIGHT_MAX = 400.0f;           // Reasonable height
		static constexpr float BALL_HORIZONTAL_MAX = 200.0f;       // Not too wide

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];

			// ====================================================================
			// Phase 1: Flip Input Detection
			// Validate pitch is in 90-270° arc (nose up or down)
			// ====================================================================
			if (player.isFlipping && !prevPlayer.isFlipping) {
				// Flip just started - validate pitch
				float pitchDegrees = asin(RS_CLAMP(-player.rotMat.up.z, -1.0f, 1.0f)) * 57.2958f;

				if (IsValidMustyPitch(pitchDegrees)) {
					tracker.state = MustyTracker::FLIP_DETECTED;
					tracker.flipStartTime = 0.0f;
					tracker.minPitchDuringFlip = pitchDegrees;
				} else {
					// Wrong pitch - not a Musty setup
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}
			}

			// Timeout: if flipping for > 1.5 seconds, reset
			if (tracker.state != MustyTracker::WAITING) {
				tracker.flipStartTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
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

			// ====================================================================
			// Phase 2: Ball Contact Validation
			// Validate ball is in front and above car
			// ====================================================================
			if (tracker.state == MustyTracker::FLIP_DETECTED && player.ballTouchedStep) {
				if (IsBallInMustyZone(player, state)) {
					tracker.state = MustyTracker::BALL_CONTACT;
					tracker.ballTouchedDuringFlip = true;
				}
			}

			// ====================================================================
			// Phase 3: Reward Trigger
			// If all conditions met, calculate and return reward
			// ====================================================================
			if (tracker.state == MustyTracker::BALL_CONTACT && tracker.ballTouchedDuringFlip) {
				float ballSpeed = state.ball.vel.Length();

				// Require minimum forward speed (strict: 1500 UU/s)
				if (ballSpeed < MIN_FLICK_SPEED) {
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}

				// Validate forward acceleration > vertical acceleration
				if (!HasForwardDominantAccel(state)) {
					tracker.state = MustyTracker::WAITING;
					return 0.0f;
				}


			// ====================================================================
			float reward = CalculateReward(ballSpeed, player, state);

				// Reset for next flip
				tracker.state = MustyTracker::WAITING;
				tracker.ballTouchedDuringFlip = false;

				return reward;
			}

			return 0.0f;
		}

	protected:
		// Helper: Check if pitch is in valid Musty zone (90±20° or 270±20°)
		bool IsValidMustyPitch(float pitchDegrees) const {
			// Tolerance bands: 70-110° and 250-290°
			float band1Min = PITCH_BAND_1_CENTER - PITCH_TOLERANCE;  // 70°
			float band1Max = PITCH_BAND_1_CENTER + PITCH_TOLERANCE;  // 110°
			float band2Min = PITCH_BAND_2_CENTER - PITCH_TOLERANCE;  // 250°
			float band2Max = PITCH_BAND_2_CENTER + PITCH_TOLERANCE;  // 290°

			// Convert to 0-360 range if negative
			float normalizedPitch = pitchDegrees;
			if (normalizedPitch < 0.0f) {
				normalizedPitch += 360.0f;
			}

			// Check if in either tolerance band
			bool inBand1 = (normalizedPitch >= band1Min && normalizedPitch <= band1Max);
			bool inBand2 = (normalizedPitch >= band2Min && normalizedPitch <= band2Max);

			return inBand1 || inBand2;
		}

		// Helper: Check if ball is in Musty zone (in front, above)
		bool IsBallInMustyZone(const Player& player, const GameState& state) const {
			// Transform ball position to car's reference frame
			Vec relPos = state.ball.pos - player.pos;

			float height = relPos.z;
			float forward = relPos.Dot(player.rotMat.forward);        // Forward distance
			float horizontal = fabs(relPos.Dot(player.rotMat.right)); // Left-right distance

			// Check forward (>100 UU, reasonable max 1500)
			bool validForward = (forward > BALL_FORWARD_MIN && forward < BALL_FORWARD_MAX);

			// Check height (>50 UU, reasonable max 400)
			bool validHeight = (height > BALL_HEIGHT_MIN && height < BALL_HEIGHT_MAX);

			// Check horizontal (not too wide)
			bool validHorizontal = (horizontal < BALL_HORIZONTAL_MAX);

			return validForward && validHeight && validHorizontal;
		}

		// Helper: Validate that forward velocity component > vertical velocity component
		bool HasForwardDominantAccel(const GameState& state) const {
			Vec ballVel = state.ball.vel;
			float horizontalSpeed = Vec(ballVel.x, ballVel.y, 0).Length();
			float verticalSpeed = fabs(ballVel.z);

			return horizontalSpeed > verticalSpeed;
		}

		// Helper: Calculate final reward based on flick quality
		float CalculateReward(float ballSpeed, const Player& player, const GameState& state) const {
			// Speed score: Range from MIN_FLICK_SPEED to MAX_FLICK_SPEED
			float speedScore = RS_CLAMP(ballSpeed / MAX_FLICK_SPEED, 0.0f, 1.0f);

			// Direction toward goal: Optional bonus
			Vec opponentGoal = (player.team == Team::BLUE) ?
				CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
			float goalAlignment = state.ball.vel.Normalized().Dot(toGoal);
			float directionBonus = RS_MAX(0.0f, goalAlignment);

			// Total reward: Base × speedScore × (1 + directionBonus)
			return BASE_REWARD * speedScore * (1.0f + GOAL_DIRECTION_SCALE * directionBonus);
		}
	};

}
