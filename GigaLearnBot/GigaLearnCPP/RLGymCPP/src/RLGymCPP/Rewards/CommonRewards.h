#pragma once
#include "Reward.h"
#include "../Math.h"
#include <unordered_map>

namespace RLGC {

	template<bool PlayerEventState::* VAR, bool NEGATIVE>
	class PlayerDataEventReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			bool val = player.eventState.*VAR;

			if (NEGATIVE) {
				return -(float)val;
			}
			else {
				return (float)val;
			}
		}
	};


	typedef PlayerDataEventReward<&PlayerEventState::goal, false> PlayerGoalReward; // NOTE: Given only to the player who last touched the ball on the opposing team
	typedef PlayerDataEventReward<&PlayerEventState::assist, false> AssistReward;
	typedef PlayerDataEventReward<&PlayerEventState::shot, false> ShotReward;
	typedef PlayerDataEventReward<&PlayerEventState::shotPass, false> ShotPassReward;
	typedef PlayerDataEventReward<&PlayerEventState::save, false> SaveReward;
	typedef PlayerDataEventReward<&PlayerEventState::bump, false> BumpReward;
	typedef PlayerDataEventReward<&PlayerEventState::bumped, true> BumpedPenalty;
	typedef PlayerDataEventReward<&PlayerEventState::demo, false> DemoReward;
	typedef PlayerDataEventReward<&PlayerEventState::demoed, true> DemoedPenalty;

	// Rewards a goal by anyone on the team
	// NOTE: Already zero-sum
	class GoalReward : public Reward {
	public:
		float concedeScale;
		GoalReward(float concedeScale = -1) : concedeScale(concedeScale) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			if (!state.goalScored)
				return 0;

			bool scored = (player.team != RS_TEAM_FROM_Y(state.ball.pos.y));
			return scored ? 1 : concedeScale;
		}
	};

	// 2025-12-17 | Rev 1.1 | NJH
// - Speed-scaled Shot / Save / Goal rewards.
// - <= 50 kph => 0.5x
// - >= 120 kph => 1.0x
// - linear ramp in-between
	inline float BallSpeedKph(const GameState& state) {
		// uu/s; uu = 1 cm => 1 uu/s = 0.01 m/s = 0.036 kph
		return state.ball.vel.Length() * 0.036f;
	}

	inline float SpeedScale_50to140kph(const GameState& state) {
		float kph = BallSpeedKph(state);
		if (kph <= 50.0f) return 0.5f;
		if (kph >= 140.0f) return 1.0f;
		float t = (kph - 50.0f) / (140.0f - 50.0f); // 0..1
		return 0.5f + 0.5f * t;
	}

	class SpeedScaledShotReward : public Reward {
	private:
		ShotReward inner;
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			float r = inner.GetReward(player, state, isFinal);
			if (r == 0.0f) return 0.0f;
			return r * SpeedScale_50to140kph(state);
		}
	};

	class SpeedScaledSaveReward : public Reward {
	private:
		SaveReward inner;
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			float r = inner.GetReward(player, state, isFinal);
			if (r == 0.0f) return 0.0f;
			return r * SpeedScale_50to140kph(state);
		}
	};

	// Team goal reward, speed-scaled
	class SpeedScaledGoalReward : public Reward {
	private:
		GoalReward inner;
	public:
		SpeedScaledGoalReward(float concedeScale = -1.0f) : inner(concedeScale) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			float r = inner.GetReward(player, state, isFinal);
			if (r == 0.0f) return 0.0f;

			// Only scale the positive "we scored" side (leave concedeScale untouched)
			if (r > 0.0f) {
				return r * SpeedScale_50to140kph(state);
			}
			return r;
		}
	};


	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/misc_rewards.py
	class VelocityReward : public Reward {
	public:
		bool isNegative;
		VelocityReward(bool isNegative = false) : isNegative(isNegative) {}
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return player.vel.Length() / CommonValues::CAR_MAX_SPEED * (1 - 2 * isNegative);
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/ball_goal_rewards.py
	class VelocityBallToGoalReward : public Reward {
	public:
		bool ownGoal = false;
		VelocityBallToGoalReward(bool ownGoal = false) : ownGoal(ownGoal) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			bool targetOrangeGoal = player.team == Team::BLUE;
			if (ownGoal)
				targetOrangeGoal = !targetOrangeGoal;

			Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			
			Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
			return ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/player_ball_rewards.py
	class VelocityPlayerToBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			Vec normVel = player.vel / CommonValues::CAR_MAX_SPEED;
			return dirToBall.Dot(normVel);
		}
	};

	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/player_ball_rewards.py
	class FaceBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			return player.rotMat.forward.Dot(dirToBall);
		}
	};

	class TouchBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return player.ballTouchedStep;
		}
	};

	class SpeedReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			return player.vel.Length() / CommonValues::CAR_MAX_SPEED;
		}
	};

	// ============================================================================
	// WavedashReward
	// 2026-1-5 | Rev 1.4 | NJH
	// - Simplified back to original reward
	// ============================================================================
	class WavedashBasicReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			if (!player.prev)
				return 0;

			if (player.isOnGround && (player.prev->isFlipping && !player.prev->isOnGround)) {
				return 1;
			}
			else {
				return 0;
			}
		}
	};
	// ============================================================================
// WavedashReward
// 2025-12-16 | Rev 1.3 | NJH
// - Add metrics (AirDribble-style) for:
//   * landFlip candidates
//   * goodGeom passes
//   * rewarded wavedash events
//   * rewardSum
// ============================================================================
	class WavedashReward : public Reward {
	public:
		struct WavedashMetrics {
			int events = 0;        // detected wavedash event (passes gates)
			int landFlip = 0;      // landed + flip (raw candidates)
			int goodGeom = 0;      // wheels-facing + tilt checks pass
			int landed = 0; // landed ticks (raw)
			double rewardSum = 0.0;
		};

		inline static std::atomic<int> s_events{ 0 };
		inline static std::atomic<int> s_landFlip{ 0 };
		inline static std::atomic<int> s_goodGeom{ 0 };
		inline static std::atomic<long long> s_rewardMilli{ 0 };
		inline static std::atomic<int> s_landed{ 0 };

		static WavedashMetrics GetAndResetWavedashMetrics() {
			WavedashMetrics m;
			m.events = s_events.exchange(0);
			m.landFlip = s_landFlip.exchange(0);
			m.goodGeom = s_goodGeom.exchange(0);
			long long r = s_rewardMilli.exchange(0);
			m.rewardSum = double(r) / 1000.0;
			m.landed = s_landed.exchange(0);
			return m;
		}

	private:
		struct WDState {
			bool wasOnGround = true;
			Vec  prevVel = Vec(0, 0, 0);
		};

		mutable std::map<uint32_t, WDState> st;

		// Tunables (forgiving)
		float nearSurfaceEps = 140.0f;

		float wheelsFacingThresh = 0.55f; // carDown dot normal must be <= -0.65
		float tiltThresh = 0.08f;         // forward/right have some component along normal
		float minSpeedGain = 25.0f;       // reject spam flips on landing

		Vec EstimateSurfaceNormal(const Vec& pos) const {
			Vec n(0, 0, 1);

			const float dx = CommonValues::SIDE_WALL_X - fabsf(pos.x);
			const float dy = CommonValues::BACK_WALL_Y - fabsf(pos.y);
			const float dzCeil = CommonValues::CEILING_Z - pos.z;

			if (dx < nearSurfaceEps) {
				n = Vec((pos.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
			}
			if (dy < nearSurfaceEps && dy < dx) {
				n = Vec(0.0f, (pos.y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
			}
			if (dzCeil < nearSurfaceEps && dzCeil < dx && dzCeil < dy) {
				n = Vec(0.0f, 0.0f, -1.0f);
			}

			return n;
		}

	public:
		virtual void Reset(const GameState& initialState) override {
			st.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev) return 0.0f;

			auto& s = st[player.carId];

			const bool onGroundNow = player.isOnGround;
			const bool onGroundPrev = s.wasOnGround;
			s.wasOnGround = onGroundNow;

			const bool landedThisTick = (onGroundNow && !onGroundPrev);
			if (!landedThisTick) {
				s.prevVel = player.vel;
				return 0.0f;
			}

			s_landed.fetch_add(1);

			// More forgiving: the dodge often begins slightly BEFORE the first ground contact tick.
			const bool flipNearLanding = (player.isFlipping || player.prev->isFlipping);
			if (!flipNearLanding) {
			    s.prevVel = player.vel;
			    return 0.0f;
			}

			// Candidate: landed + flip near landing
			s_landFlip.fetch_add(1);

			Vec surfaceN = EstimateSurfaceNormal(player.prev->pos);

			Vec carUp = player.rotMat.up;
			Vec carDown = carUp * -1.0f;
			Vec carFwd = player.rotMat.forward;
			Vec carRight = player.rotMat.right;

			float downDotN = carDown.Dot(surfaceN);
			if (downDotN > -wheelsFacingThresh) {
				s.prevVel = player.vel;
				return 0.0f;
			}

			float fwdN = carFwd.Dot(surfaceN);
			float rightN = carRight.Dot(surfaceN);
			float tilt = RS_MAX(fabsf(fwdN), fabsf(rightN));
			if (tilt < tiltThresh) {
				s.prevVel = player.vel;
				return 0.0f;
			}

			// Geometry passed (wheels facing + tilted)
			s_goodGeom.fetch_add(1);

			float prevSpeed = player.prev->vel.Length();
			float curSpeed = player.vel.Length();
			float gain = curSpeed - prevSpeed;
			if (gain < minSpeedGain) {
				s.prevVel = player.vel;
				return 0.0f;
			}

			float gainFrac = RS_CLAMP((gain - minSpeedGain) / 350.0f, 0.0f, 1.0f);
			float reward = 0.5f + 0.7f * gainFrac;

			// Success
			s_events.fetch_add(1);
			s_rewardMilli.fetch_add(static_cast<long long>(reward * 1000.0f));

			s.prevVel = player.vel;
			return reward;
		}
	};

	// ============================================================================
	// AdvancedWavedashReward - Rev 1.0 (2025-02-02)
	// Comprehensive wavedash detection with multi-factor scoring:
	// - Controlled airtime (small hop/fall)
	// - 2-wheel contact geometry detection
	// - Flip timing and cancellation
	// - Tangential velocity gain
	// - Penalties for failed attempts
	// ============================================================================
	class AdvancedWavedashReward : public Reward {
	private:
		struct WDState {
			bool wasOnGround = true;
			bool wasFlipping = false;
			Vec prevVel = Vec(0, 0, 0);
		};

		mutable std::map<uint32_t, WDState> st;

		// Geometry thresholds
		float nearSurfaceEps = 140.0f;      // Distance to detect near-surface
		float wheelsFacingThresh = 0.55f;   // carDown dot normal threshold
		float tiltThresh = 0.08f;           // Minimum forward/right component along normal
		float minSpeedGain = 25.0f;         // Minimum speed gain to reward

		Vec EstimateSurfaceNormal(const Vec& pos) const {
			Vec n(0, 0, 1);

			const float dx = CommonValues::SIDE_WALL_X - fabsf(pos.x);
			const float dy = CommonValues::BACK_WALL_Y - fabsf(pos.y);
			const float dzCeil = CommonValues::CEILING_Z - pos.z;

			if (dx < nearSurfaceEps) {
				n = Vec((pos.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
			}
			if (dy < nearSurfaceEps && dy < dx) {
				n = Vec(0.0f, (pos.y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
			}
			if (dzCeil < nearSurfaceEps && dzCeil < dx && dzCeil < dy) {
				n = Vec(0.0f, 0.0f, -1.0f);
			}

			return n;
		}

		// Count wheels in contact
		int CountWheelsInContact(const Player& player) const {
			int count = 0;
			if (player.wheelsWithContact[0]) count++;
			if (player.wheelsWithContact[1]) count++;
			if (player.wheelsWithContact[2]) count++;
			if (player.wheelsWithContact[3]) count++;
			return count;
		}

	public:
		virtual void Reset(const GameState& initialState) override {
			st.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev) return 0.0f;

			auto& s = st[player.carId];
			float reward = 0.0f;

			const bool onGroundNow = player.isOnGround;
			const bool onGroundPrev = s.wasOnGround;
			const bool landedThisTick = (onGroundNow && !onGroundPrev);

			s.wasOnGround = onGroundNow;

			// ====================================================================
			// 1. Detect surface normal under the car (any arena surface)
			// ====================================================================
			Vec surfaceN = EstimateSurfaceNormal(player.prev->pos);

			// Velocity components
			float vPerp = player.vel.Dot(surfaceN);
			Vec vTangent = player.vel - surfaceN * vPerp;
			Vec prevTangent = player.prev->vel - surfaceN * (player.prev->vel.Dot(surfaceN));

			// ====================================================================
			// 2. Controlled airtime (small hop or fall)
			// ====================================================================
			// Small perpendicular velocity (going into surface, not away) is good
			float airPrep = expf(-fabsf(vPerp));
			reward += 1.0f * airPrep;

			// ====================================================================
			// 3. Orientation reward for setting up a 2-wheel landing
			// ====================================================================
			Vec carDown = player.rotMat.up * -1.0f;
			Vec carFwd = player.rotMat.forward;
			Vec carRight = player.rotMat.right;

			float downDotN = carDown.Dot(surfaceN);
			float fwdDotN = carFwd.Dot(surfaceN);
			float rightDotN = carRight.Dot(surfaceN);

			// Reward when wheels face surface and car is tilted
			float wheelOrient = expf(-(downDotN + 0.55f) * (downDotN + 0.55f));  // Peaks near -0.55
			float carTilt = RS_MAX(fabsf(fwdDotN), fabsf(rightDotN));
			float tiltReward = expf(-(carTilt - tiltThresh) * (carTilt - tiltThresh));

			reward += 2.0f * wheelOrient * tiltReward;

			// ====================================================================
			// 4. Detect 2-wheel contact event
			// ====================================================================
			int wheelCountNow = CountWheelsInContact(player);
			int wheelCountPrev = CountWheelsInContact(*player.prev);

			bool twoWheelEvent = (wheelCountNow == 2 && wheelCountPrev < 2);

			if (twoWheelEvent) {
				reward += 5.0f;
			}

			// ====================================================================
			// 5. Flip timing reward (flip input near 2-wheel contact)
			// ====================================================================
			bool flipInputNow = player.isFlipping;
			bool flipInputPrev = player.prev->isFlipping;

			if (twoWheelEvent && flipInputNow) {
				reward += 8.0f;
			}

			// ====================================================================
			// 6. Flip cancel reward (flip interrupted by surface contact)
			// ====================================================================
			bool flipCanceled = (flipInputPrev && !flipInputNow && wheelCountNow >= 1);

			if (flipCanceled) {
				reward += 10.0f;
			}

			// ====================================================================
			// 7. Tangential velocity gain reward
			// ====================================================================
			float speedGain = vTangent.Length() - prevTangent.Length();
			if (speedGain > 0.0f) {
				reward += 2.0f * speedGain;
			}

			// ====================================================================
			// 8. Landing success bonus (landed + had good geometry + good outcome)
			// ====================================================================
			if (landedThisTick && flipInputPrev) {
				// Successfully landed after a flip
				float prevSpeed = player.prev->vel.Length();
				float curSpeed = player.vel.Length();
				float netGain = curSpeed - prevSpeed;

				if (netGain > minSpeedGain) {
					float gainFrac = RS_CLAMP((netGain - minSpeedGain) / 350.0f, 0.0f, 1.0f);
					reward += 3.0f * gainFrac;  // Bonus for efficient wavedash
				}
			}

			// ====================================================================
			// 9. Penalties
			// ====================================================================

			// Too much airtime (encourages ground contact)
			if (!onGroundNow && onGroundPrev) {
				float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
				reward -= 0.1f * dt;
			}

			// Full flip that wasn't canceled (wasted flip)
			if (flipInputPrev && !flipInputNow && wheelCountNow < 1) {
				reward -= 5.0f;
			}

			// Flat 4-wheel landing (bad geometry for wavedash)
			if (wheelCountNow == 4 && wheelCountPrev < 4) {
				reward -= 3.0f;
			}

			// Discourage boosting during dash (wavedash shouldn't need boost)
			if (player.prevAction.boost > 0.5f) {
				reward -= 0.5f;
			}

			s.wasFlipping = flipInputNow;
			s.prevVel = player.vel;

			return reward;
		}
	};

	class PickupBoostReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			if (player.boost > player.prev->boost) {
				return sqrtf(player.boost / 100.f) - sqrtf(player.prev->boost / 100.f);
			} else {
				return 0;
			}
		}
	};
	// Replaced with better version in MovementAndKickoffRewards_Enhanced.h
	//// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/misc_rewards.py
	//class SaveBoostReward : public Reward {
	//public:
	//	float exponent;
	//	SaveBoostReward(float exponent = 0.5f) : exponent(exponent) {}

	//	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
	//		return RS_CLAMP(powf(player.boost / 100, exponent), 0, 1);
	//	}
	//};


	class AirReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			return !player.isOnGround;
		}
	};

	class TurtledPenaltyReward : public Reward {
	private:
		mutable std::unordered_map<uint32_t, float> turtledTime;
		mutable std::unordered_map<uint32_t, bool> wasTurtled;

		// Tunables
		float graceTime = 1.0f;        // seconds before penalty starts
		float penaltyPerSecond = -0.5f; // applied after grace
		float recoveryReward = 1.0f;   // one-time reward on recovery

	public:
		virtual void Reset(const GameState& initialState) override {
			turtledTime.clear();
			wasTurtled.clear();
		}

		virtual float GetReward(const Player& player,
			const GameState& state,
			bool isFinal) override {
			float reward = 0.0f;

			// Upside-down check: car's up vector points downward
			bool upsideDown = player.rotMat.up.z < 0.0f;
			bool onGround = player.isOnGround;
			bool isTurtled = upsideDown && onGround;

			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;

			if (isTurtled) {
				turtledTime[player.carId] += dt;
				wasTurtled[player.carId] = true;

				// Penalize only after grace period
				if (turtledTime[player.carId] > graceTime) {
					reward += penaltyPerSecond * dt;
				}
			}
			else {
				// Just recovered
				if (wasTurtled[player.carId]) {
					reward += recoveryReward;
				}
				turtledTime[player.carId] = 0.0f;
				wasTurtled[player.carId] = false;
			}

			return reward;
		}
	};

	// Rev 3 (2025-01-10): Direction-aware touch acceleration
	// - Rewards touches that increase ball speed toward opponent goal
	// - Distinguishes offensive vs neutral vs defensive touches
	// - Better reward scaling for training signal strength
	class TouchAccelReward : public Reward {
	public:
		// Tuning parameters
		float maxRewardedBallSpeed;
		float maxRewardedImpulse;
		float directionWeight;      // How much to weight goal-direction vs raw speed
		bool rewardRedirects;       // Whether to reward speed-maintaining redirects toward goal

		TouchAccelReward(
			float maxBallSpeed = RLGC::Math::KPHToVel(110),
			float maxImpulse = RLGC::Math::KPHToVel(80),
			float dirWeight = 0.3f,
			bool allowRedirects = true
		) : maxRewardedBallSpeed(maxBallSpeed),
			maxRewardedImpulse(maxImpulse),
			directionWeight(dirWeight),
			rewardRedirects(allowRedirects) {
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;
			if (!player.ballTouchedStep) return 0.0f;

			const Vec& ballPos = state.ball.pos;
			const Vec& ballVel = state.ball.vel;
			const Vec& prevBallVel = state.prev->ball.vel;

			const float prevSpeed = prevBallVel.Length();
			const float curSpeed = ballVel.Length();

			// Get goal positions based on player team
			const Vec& oppGoal = (player.team == Team::BLUE)
				? RLGC::CommonValues::ORANGE_GOAL_CENTER
				: RLGC::CommonValues::BLUE_GOAL_CENTER;

			// Direction to opponent goal (normalized)
			Vec toGoal = (oppGoal - ballPos).Normalized();

			// Speed toward goal (before and after)
			float prevGoalSpeed = prevBallVel.Dot(toGoal);
			float curGoalSpeed = ballVel.Dot(toGoal);
			float goalSpeedDelta = curGoalSpeed - prevGoalSpeed;

			// Raw speed delta
			float speedDelta = curSpeed - prevSpeed;

			// Impulse magnitude
			float impulse = (ballVel - prevBallVel).Length();
			float impulseFrac = RS_CLAMP(impulse / maxRewardedImpulse, 0.0f, 1.0f);

			// Base reward: speed increase
			float baseReward = 0.0f;

			if (speedDelta > 0) {
				// Ball sped up - original behavior
				float prevSpeedFrac = RS_CLAMP(prevSpeed / maxRewardedBallSpeed, 0.0f, 1.0f);
				float curSpeedFrac = RS_CLAMP(curSpeed / maxRewardedBallSpeed, 0.0f, 1.0f);
				baseReward = (curSpeedFrac - prevSpeedFrac);
			}
			else if (rewardRedirects && goalSpeedDelta > 0) {
				// Ball didn't speed up overall, but IS going more toward goal
				// Give partial credit for good redirects
				float redirectBonus = RS_CLAMP(goalSpeedDelta / maxRewardedBallSpeed, 0.0f, 0.5f);
				baseReward = redirectBonus * 0.5f;  // Half credit for redirects
			}
			else {
				return 0.0f;  // Neither sped up nor redirected toward goal
			}

			// Direction bonus: extra reward if ball is now heading toward goal
			float directionBonus = 0.0f;
			if (curGoalSpeed > 0) {
				// Ball heading toward opponent goal
				float goalSpeedFrac = RS_CLAMP(curGoalSpeed / maxRewardedBallSpeed, 0.0f, 1.0f);
				directionBonus = goalSpeedFrac * directionWeight;
			}

			// Combine: base reward scaled by impulse quality, plus direction bonus
			float reward = (baseReward * impulseFrac) + (directionBonus * impulseFrac);

			// Scale up for better training signal (optional)
			return reward * 2.0f;  // Doubles effective magnitude
		}
	};


	class StrongTouchReward : public Reward {
	public:
		float minRewardedVel, maxRewardedVel;
		StrongTouchReward(float minSpeedKPH = 20, float maxSpeedKPH = 130) {
			minRewardedVel = RLGC::Math::KPHToVel(minSpeedKPH);
			maxRewardedVel = RLGC::Math::KPHToVel(maxSpeedKPH);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			if (player.ballTouchedStep) {
				float hitForce = (state.ball.vel - state.prev->ball.vel).Length();
				if (hitForce < minRewardedVel)
					return 0;

				return RS_MIN(1, hitForce / maxRewardedVel);
			} else {
				return 0;
			}
		}
	};
}