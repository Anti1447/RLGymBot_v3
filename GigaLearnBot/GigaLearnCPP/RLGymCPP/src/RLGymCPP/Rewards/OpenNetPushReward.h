#pragma once
#include "Reward.h"
#include "RewardMath.h"

namespace RLGC {

	// ============================================================================
	// OpenNetPushReward - reward pushing the ball into an OPEN net
	// 2026-06-04 | Rev 1.0 | NJH
	// 2026-06-28 | Rev 2.0 | NJH
	//   - Added MIN_BALL_SPEED gate (900 UU/s) to filter slow drifts/rolls
	//   - Added MAX_TOUCH_DIST (650 UU) so reward requires player near ball
	//   - Fixed aim point Z: now matches ball height instead of always floor
	//   - Removed proximity-to-net multiplier (was penalizing midfield shots)
	//   - Re-normalized push above MIN_PUSH floor for full dynamic range
	//
	// Fires only when the net is genuinely open: for every opponent, the ball is
	// goal-side of them in BOTH goal-ward Y AND absolute distance to the net.
	// Only the single player closest to the ball (any team) AND within
	// MAX_TOUCH_DIST is rewarded, so only the active carrier gets the signal.
	// Reward scales with how fast the ball is moving toward the aim point,
	// re-normalized above the MIN_PUSH floor so the full [0,1] range is used.
	//
	// Aim point (best-accuracy finish): rear of the net, on the OPPOSITE X inner
	// side from the ball (ball on +X -> aim -X inner wall), at the ball's height.
	//
	// Stateless / continuous shaping. ZeroSum-wrapped in config (an open-net
	// finish is directly competitive). Final scale comes from WeightedReward.
	// ============================================================================
	class OpenNetPushReward : public Reward {
	private:
		static constexpr float MIN_BALL_SPEED = 900.0f;   // UU/s raw speed floor (~15% of max)
		static constexpr float MIN_PUSH       = 0.15f;    // directional component floor (~900 UU/s)
		static constexpr float MAX_TOUCH_DIST = 650.0f;   // player must be near ball to "push" it

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// goalSign: +1 = attack orange goal (+Y), -1 = attack blue goal (-Y)
			float goalSign = (player.team == Team::BLUE) ? +1.0f : -1.0f;

			// --- Gate A: this player is the single closest to the ball AND near it ---
			float myDist = (player.pos - state.ball.pos).Length();
			if (myDist > MAX_TOUCH_DIST) return 0.0f;
			for (const Player& o : state.players) {
				if (o.carId == player.carId) continue;
				if ((o.pos - state.ball.pos).Length() < myDist) return 0.0f;
			}

			// --- Gate B: OPEN NET — every opponent is behind the ball in BOTH
			//             goal-ward Y and absolute distance to the net ---
			Vec goalCenter = (player.team == Team::BLUE)
				? CommonValues::ORANGE_GOAL_CENTER
				: CommonValues::BLUE_GOAL_CENTER;
			float ballGoalwardY = goalSign * state.ball.pos.y;
			float ballDistToNet = (state.ball.pos - goalCenter).Length();
			for (const Player& o : state.players) {
				if (o.team == player.team) continue;                     // opponents only
				float oppGoalwardY = goalSign * o.pos.y;
				if (oppGoalwardY > ballGoalwardY) return 0.0f;           // opponent closer to net in Y
				if ((o.pos - goalCenter).Length() < ballDistToNet) return 0.0f;  // closer in distance
			}

			// --- Raw speed gate (cheap early exit before aim-point math) ---
			if (state.ball.vel.Length() < MIN_BALL_SPEED) return 0.0f;

			// --- Aim point: rear of net, opposite-X inner side from ball, ball-height Z ---
			float targetY = goalSign * CommonValues::BACK_NET_Y;
			float innerX  = CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS;
			float targetX = (state.ball.pos.x >= 0.0f) ? -innerX : +innerX;
			float targetZ = RS_CLAMP(state.ball.pos.z,
			                         CommonValues::BALL_RADIUS,
			                         CommonValues::GOAL_HEIGHT - CommonValues::BALL_RADIUS);
			Vec target = Vec(targetX, targetY, targetZ);

			// --- Push reward: directional speed toward aim, re-normalized above floor ---
			Vec   dir  = (target - state.ball.pos).Normalized();
			float push = dir.Dot(state.ball.vel) / CommonValues::BALL_MAX_SPEED;
			if (push < MIN_PUSH) return 0.0f;

			return (push - MIN_PUSH) / (1.0f - MIN_PUSH);
		}
	};

}
