#pragma once
#include "Reward.h"
#include "RewardMath.h"

namespace RLGC {

	// ============================================================================
	// OpenNetPushReward - reward pushing the ball into an OPEN net
	// 2026-06-04 | Rev 1.0 | NJH
	//
	// Fires only when the net is genuinely open: for every opponent, the ball is
	// goal-side of them in BOTH goal-ward Y AND absolute distance to the net.
	// Only the single player closest to the ball (any team) is rewarded, so just the
	// de-facto carrier gets the signal. Reward scales with how fast the ball is moving
	// toward the aim point.
	//
	// Aim point (best-accuracy finish): rear of the net, on the OPPOSITE X inner side
	// from the ball (ball on +X -> aim the -X inner wall), at far-bottom-corner height.
	//
	// Stateless / continuous shaping. ZeroSum-wrapped in config (an open-net finish is
	// directly competitive). Final scale comes from the WeightedReward weight.
	// ============================================================================
	class OpenNetPushReward : public Reward {
	private:
		static constexpr float PROX_FALLOFF = 9000.0f;  // distance (UU) over which proximity decays
		static constexpr float PROX_FLOOR   = 0.25f;    // minimum proximity multiplier (far shots)

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// goalSign: +1 = attack orange goal (+Y), -1 = attack blue goal (-Y)
			float goalSign = (player.team == Team::BLUE) ? +1.0f : -1.0f;

			// --- Gate A: this player is the single closest to the ball (any team) ---
			float myDist = (player.pos - state.ball.pos).Length();
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

			// --- Aim point: rear of net, opposite-X inner side from ball, low corner ---
			float targetY = goalSign * CommonValues::BACK_NET_Y;                         // rear of net
			float innerX  = CommonValues::GOAL_WIDTH_FROM_CENTER - CommonValues::BALL_RADIUS;
			float targetX = (state.ball.pos.x >= 0.0f) ? -innerX : +innerX;              // opposite inner wall
			float targetZ = CommonValues::BALL_RADIUS;                                    // far bottom corner
			Vec target = Vec(targetX, targetY, targetZ);

			// --- Push reward: ball velocity toward aim point, positive only ---
			Vec dir = (target - state.ball.pos).Normalized();
			float push = dir.Dot(state.ball.vel) / CommonValues::BALL_MAX_SPEED;
			if (push <= 0.0f) return 0.0f;

			// --- Gentle proximity scaling: closer to net = more reward ---
			float prox = RS_CLAMP(1.0f - ballDistToNet / PROX_FALLOFF, PROX_FLOOR, 1.0f);

			return push * prox;
		}
	};

}
