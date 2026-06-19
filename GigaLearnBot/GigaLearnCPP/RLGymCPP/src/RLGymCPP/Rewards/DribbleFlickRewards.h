#pragma once
#include "Reward.h"
#include <unordered_map>
#include <cmath>

namespace RLGC {

	// ============================================================================
	// DribbleFlickRewards - Two simple, standalone dribble-flick rewards
	// 2026-06-04 | Rev 1.0 | NJH
	//
	// FrontFlipFlickReward  - front-flip straight into the ball while dribbling
	// DiagonalFlickReward   - front-left / front-right dodge into the ball while dribbling
	//
	// Direction is read directly from RocketSim's CarState::flipRelTorque, which encodes
	// the dodge direction at flip start (see Car.cpp):
	//     dodgeDir     = (-pitch, yaw+roll).normalized()
	//     flipRelTorque = (-dodgeDir.y, dodgeDir.x, 0)   // magnitude ~1 for any real dodge
	//   => flipRelTorque.y = forward/back component   (forward flip has POSITIVE y)
	//      flipRelTorque.x = lateral (yaw+roll) component  (nonzero => diagonal/side dodge)
	//   Pure front flip:  y ~= 1, x ~= 0
	//   Front-diagonal:   y ~= 0.7, |x| ~= 0.7
	//
	// "Sweet spot" is modeled implicitly: reward scales with the resulting ball speed, so the
	// only way to maximize it is to contact near the sweet spot and send the ball fast.
	//
	// Anti-farming: requires a genuine carry (ball on roof) immediately before the flip, ball
	// contact DURING the flip, a minimum outgoing speed, and a per-player cooldown.
	// ============================================================================
	class DribbleFlickRewardBase : public Reward {
	protected:
		// Per-player timing state (CLAUDE.md 7C: float seconds, cleared in Reset, ticked every step)
		mutable std::unordered_map<uint32_t, float> _carryTime;    // accumulates while ball on roof
		mutable std::unordered_map<uint32_t, float> _carryCredit;  // grace window after a real carry
		mutable std::unordered_map<uint32_t, float> _cooldown;     // re-trigger lockout

		// Shared tunables
		static constexpr float MIN_CARRY_SECONDS   = 0.25f;  // how long ball must sit on roof to count as a dribble
		static constexpr float CARRY_GRACE_SECONDS = 0.6f;   // window after a carry in which a flick still counts
		static constexpr float MIN_FLICK_SPEED     = 1100.0f;
		static constexpr float MAX_FLICK_SPEED     = 2300.0f;
		static constexpr float COOLDOWN_SECONDS    = 0.75f;
		static constexpr float BASE_REWARD         = 1.0f;   // final scale comes from the WeightedReward weight
		static constexpr float GOAL_DIRECTION_SCALE = 0.3f;

		// Carry-detection box (ball on top of car)
		static constexpr float ROOF_UP_MIN    = 30.0f;
		static constexpr float ROOF_UP_MAX    = 170.0f;
		static constexpr float ROOF_RIGHT_MAX = 110.0f;
		static constexpr float ROOF_FWD_MAX   = 160.0f;

		// Contact box at flick moment (slightly looser than the carry box)
		static constexpr float CONTACT_UP_MIN    = 20.0f;
		static constexpr float CONTACT_UP_MAX    = 200.0f;
		static constexpr float CONTACT_RIGHT_MAX = 140.0f;

	public:
		virtual void Reset(const GameState& initialState) override {
			_carryTime.clear();
			_carryCredit.clear();
			_cooldown.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

			// --- ball-on-roof carry tracking (the "dribbling" precondition) ---
			Vec rel = state.ball.pos - player.pos;
			float up    = rel.Dot(player.rotMat.up);
			float fwd   = rel.Dot(player.rotMat.forward);
			float right = rel.Dot(player.rotMat.right);
			bool onRoof = (up > ROOF_UP_MIN && up < ROOF_UP_MAX &&
						   fabs(right) < ROOF_RIGHT_MAX && fabs(fwd) < ROOF_FWD_MAX);

			float& carry  = _carryTime[player.carId];
			float& credit = _carryCredit[player.carId];
			float& cd     = _cooldown[player.carId];

			if (onRoof) carry += dt; else carry = 0.0f;

			if (carry >= MIN_CARRY_SECONDS)
				credit = CARRY_GRACE_SECONDS;
			else
				credit = RS_MAX(0.0f, credit - dt);  // decremented every step regardless

			// Tick cooldown every step (CLAUDE.md 7C: must expire even during continuous contact)
			if (cd > 0.0f) { cd = RS_MAX(0.0f, cd - dt); return 0.0f; }

			// Gate 1: contacted ball this step during a flip
			if (!player.ballTouchedStep || !player.isFlipping) return 0.0f;

			// Gate 2: was genuinely dribbling just before the flick
			if (credit <= 0.0f) return 0.0f;

			// Gate 3: ball still near the car at contact (front/top)
			if (!(up > CONTACT_UP_MIN && up < CONTACT_UP_MAX && fabs(right) < CONTACT_RIGHT_MAX))
				return 0.0f;

			// Gate 4: meaningful outgoing speed (also the "sweet spot" proxy)
			float ballSpeed = state.ball.vel.Length();
			if (ballSpeed < MIN_FLICK_SPEED) return 0.0f;

			// Gate 5: dodge-direction discriminator (subclass-specific)
			if (!IsValidDodgeDirection(player.flipRelTorque)) return 0.0f;

			// SUCCESS - compute speed-scaled, goal-aligned reward
			float speedScore = RS_CLAMP(ballSpeed / MAX_FLICK_SPEED, 0.0f, 1.0f);

			Vec oppGoal = (player.team == Team::BLUE) ?
				CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;
			Vec toGoal = (oppGoal - state.ball.pos).Normalized();
			float goalAlign = RS_MAX(0.0f, state.ball.vel.Normalized().Dot(toGoal));

			float reward = BASE_REWARD * speedScore * (1.0f + GOAL_DIRECTION_SCALE * goalAlign);

			cd = COOLDOWN_SECONDS;  // set cooldown only when reward fires
			return reward;
		}

	protected:
		// Subclass returns true if the dodge direction matches the flick type
		virtual bool IsValidDodgeDirection(const Vec& flipRelTorque) const = 0;
	};

	// ============================================================================
	// FrontFlipFlickReward - front flip straight into the ball while dribbling
	// Dodge must be forward-dominant with little lateral component.
	// ============================================================================
	class FrontFlipFlickReward : public DribbleFlickRewardBase {
	protected:
		static constexpr float MIN_FORWARD = 0.85f;  // must be a forward dodge
		static constexpr float MAX_LATERAL = 0.30f;  // must NOT be diagonal/sideways

		virtual bool IsValidDodgeDirection(const Vec& t) const override {
			return (t.y >= MIN_FORWARD && fabs(t.x) <= MAX_LATERAL);
		}
	};

	// ============================================================================
	// DiagonalFlickReward - front-left / front-right dodge into the ball while dribbling
	// Dodge must have BOTH a forward component and a clear lateral component.
	// Sign of x distinguishes left vs right; both are rewarded equally.
	// ============================================================================
	class DiagonalFlickReward : public DribbleFlickRewardBase {
	protected:
		static constexpr float MIN_FORWARD = 0.30f;  // needs a forward component (front-diagonal)
		static constexpr float MIN_LATERAL = 0.35f;  // needs a clear lateral component (the "diagonal")

		virtual bool IsValidDodgeDirection(const Vec& t) const override {
			return (t.y >= MIN_FORWARD && fabs(t.x) >= MIN_LATERAL);
		}
	};

	// ============================================================================
	// SideFlickReward - pure sideways dodge into the ball while dribbling
	// Dodge must be strongly lateral with minimal forward component.
	// Covers left and right equally; yaw ±1, pitch 0.
	// ============================================================================
	class SideFlickReward : public DribbleFlickRewardBase {
	protected:
		static constexpr float MIN_LATERAL = 0.70f;  // strong sideways (|x| near 1.0 for pure side dodge)
		static constexpr float MAX_FORWARD = 0.30f;  // no/minimal forward — prevents diagonal overlap

		virtual bool IsValidDodgeDirection(const Vec& t) const override {
			return (fabs(t.x) >= MIN_LATERAL && fabs(t.y) <= MAX_FORWARD);
		}
	};

}
