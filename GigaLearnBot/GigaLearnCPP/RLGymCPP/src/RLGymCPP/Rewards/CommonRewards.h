#pragma once
#include "Reward.h"
#include "../Math.h"

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

	// Mostly based on the classic Necto rewards
	// Rev 2 (2025-12-15, NJH): reward impulse magnitude while still requiring net speed-up.
	class TouchAccelReward : public Reward {
	public:
		constexpr static float MAX_REWARDED_BALL_SPEED = RLGC::Math::KPHToVel(110);
		constexpr static float MAX_REWARDED_IMPULSE = RLGC::Math::KPHToVel(80); // tuning knob

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;
			if (!player.ballTouchedStep) return 0.0f;

			const float prevSpeed = state.prev->ball.vel.Length();
			const float curSpeed = state.ball.vel.Length();

			// Must actually speed the ball up (original intent)
			if (curSpeed <= prevSpeed) return 0.0f;

			// Impulse from the touch (vector change)
			const float impulse = (state.ball.vel - state.prev->ball.vel).Length();

			const float impulseFrac = RS_CLAMP(impulse / MAX_REWARDED_IMPULSE, 0.0f, 1.0f);
			const float prevSpeedFrac = RS_CLAMP(prevSpeed / MAX_REWARDED_BALL_SPEED, 0.0f, 1.0f);
			const float curSpeedFrac = RS_CLAMP(curSpeed / MAX_REWARDED_BALL_SPEED, 0.0f, 1.0f);

			// Keep original delta, but scale by "how real" the hit was
			const float delta = (curSpeedFrac - prevSpeedFrac);
			return delta * impulseFrac;
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