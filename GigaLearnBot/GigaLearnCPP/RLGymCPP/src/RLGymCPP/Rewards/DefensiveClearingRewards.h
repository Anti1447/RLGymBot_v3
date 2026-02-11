#pragma once
#include "Reward.h"
#include "../Math.h"

namespace RLGC {

// ============================================================================
// BIG CLEAR REWARD - Simple Defensive Clear
// ============================================================================
// Rewards hard, high clears during defensive situations
// Simple version: In defensive zone + hard speed + ball elevated OR sent high
// 2026-02-08 | Rev 2.0 | NJH - Simplified
// ============================================================================

class BigClearReward : public Reward {
private:
	static constexpr float BASE_REWARD = 5.0f;

public:
	virtual void Reset(const GameState& initialState) override {}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Gate 1: Touch ball this frame
		if (!player.ballTouchedStep) return 0.0f;

		// Gate 2: In defensive half (Y < 0 for BLUE, Y > 0 for ORANGE)
		bool inDefensiveHalf = (player.team == Team::BLUE) ? (player.pos.y < 0) : (player.pos.y > 0);
		if (!inDefensiveHalf) return 0.0f;

		// Gate 3: Ball has significant velocity (hard hit)
		float ballVelMag = state.ball.vel.Length();
		if (ballVelMag < 300.0f) return 0.0f;

		// Gate 4: Ball velocity directed away from own goal
		Vec ownGoal = (player.team == Team::BLUE) ?
			CommonValues::BLUE_GOAL_BACK : CommonValues::ORANGE_GOAL_BACK;
		Vec awayFromGoal = (state.ball.pos - ownGoal).Normalized();
		Vec ballVelDir = state.ball.vel.Normalized();
		if (ballVelDir.Dot(awayFromGoal) < 0.0f) return 0.0f;

		return BASE_REWARD;
	}

	void PreStep(const GameState& state) override {}
};

} // namespace RLGC

// ============================================================================
// END BIG CLEAR REWARD
// ============================================================================
