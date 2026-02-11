#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <cmath>
#include <vector>

namespace RLGC {

// ============================================================================
// KICKOFF STRATEGY REWARD - Multi-Strategy Kickoff Learning
// ============================================================================
// Rewards learning diverse kickoff strategies based on game mode and team composition
// Supports: 1v1, 2v2, 3v3 with position-specific behaviors
// 2026-02-08 | NJH
//
// Strategy Types:
// 1v1:
//   - Standard (speedflip + flip into ball)
//   - Standard variations (unusual dodge/jump patterns)
//   - Fake kickoff (cover net, don't challenge)
//   - Corner boost grab (diagonal only)
//   - Wavedash kickoff (jump + wavedash into ball)
//
// 2v2/3v3 (First Player - Challenger):
//   - Standard (speedflip boost kick)
//   - Demo rush (go for opponent demo)
//   - Fake/hesitation
//   - Corner boost grab
//
// 2v2/3v3 (Second+ Player - Support):
//   - Play it safe (between ball and net)
//   - Corner boost grab
//   - Boost grab + support
//   - Spanish (support demo)
// ============================================================================

class KickoffStrategyReward : public Reward {
private:
	// Kickoff behavior classification
	enum KickoffStrategy {
		STRATEGY_UNKNOWN,
		STRATEGY_STANDARD,           // Speedflip + boost into ball
		STRATEGY_FAKE,               // Don't go for ball, cover net
		STRATEGY_CORNER_BOOST,       // Go grab corner boost instead
		STRATEGY_DEMO,               // Rush opponent for demo
		STRATEGY_WAVEDASH,           // Jump + wavedash into ball
		STRATEGY_VARIATION,          // Unusual dodge/jump patterns
		STRATEGY_SUPPORT_SAFE,       // Stay between ball and net (2v2+)
		STRATEGY_SUPPORT_BOOST,      // Grab boost while supporting
		STRATEGY_SUPPORT_DEMO,       // Support teammate demo attempt
	};

	struct KickoffData {
		KickoffStrategy strategy = STRATEGY_UNKNOWN;
		float kickoffTime = 0.0f;
		bool inKickoff = false;
		bool isFirstPlayer = true;  // First or second+ player

		// Tracking metrics
		Vec startPos;
		float maxSpeedAchieved = 0.0f;
		float minDistToOpponent = 999999.0f;
		bool touchedBall = false;
		bool touchedBoost = false;
		int touchedBoostCount = 0;
		Vec lastPos;
	};

	mutable std::map<uint32_t, KickoffData> playerKickoffs;

	// Configuration
	static constexpr float KICKOFF_RADIUS = 300.0f;
	static constexpr float MAX_BALL_SPEED_KICKOFF = 500.0f;
	static constexpr float KICKOFF_TIMEOUT = 10.0f;
	static constexpr float CORNER_BOOST_DISTANCE = 2200.0f;  // Distance threshold for corner boost
	static constexpr float DEMO_THRESHOLD = 800.0f;          // Close enough for demo attempt
	static constexpr float SUPPORT_SAFE_DISTANCE = 1500.0f;  // Stay back this far

	// Reward values
	static constexpr float REWARD_STANDARD = 3.0f;
	static constexpr float REWARD_VARIATION = 2.5f;
	static constexpr float REWARD_FAKE = 2.0f;
	static constexpr float REWARD_CORNER_BOOST = 2.0f;
	static constexpr float REWARD_DEMO = 3.5f;
	static constexpr float REWARD_WAVEDASH = 3.5f;
	static constexpr float REWARD_SUPPORT_SAFE = 2.0f;
	static constexpr float REWARD_SUPPORT_BOOST = 2.5f;
	static constexpr float REWARD_SUPPORT_DEMO = 3.0f;

public:
	virtual void Reset(const GameState& initialState) override {
		playerKickoffs.clear();
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!state.prev) return 0.0f;

		auto& data = playerKickoffs[player.carId];
		bool inKickoff = IsInKickoffPhase(state);

		// Kickoff phase ended - evaluate strategy
		if (data.inKickoff && !inKickoff) {
			float reward = EvaluateKickoffStrategy(player, state, data);
			data.inKickoff = false;
			data.strategy = STRATEGY_UNKNOWN;
			return reward;
		}

		// Starting kickoff
		if (!data.inKickoff && inKickoff) {
			data.inKickoff = true;
			data.kickoffTime = 0.0f;
			data.startPos = player.pos;
			data.maxSpeedAchieved = 0.0f;
			data.minDistToOpponent = 999999.0f;
			data.touchedBall = false;
			data.touchedBoost = false;
			data.touchedBoostCount = 0;
			data.lastPos = player.pos;
			data.isFirstPlayer = IsFirstPlayer(player, state);
		}

		// Track metrics during kickoff
		if (data.inKickoff) {
			data.kickoffTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;

			if (data.kickoffTime > KICKOFF_TIMEOUT) {
				data.inKickoff = false;
				return 0.0f;
			}

			// Track speed, boost touches, ball touches, opponent proximity
			float speed = player.vel.Length();
			data.maxSpeedAchieved = RS_MAX(data.maxSpeedAchieved, speed);

			// Track nearest opponent
			for (const auto& other : state.players) {
				if (other.team == player.team) continue;
				float dist = (other.pos - player.pos).Length();
				data.minDistToOpponent = RS_MIN(data.minDistToOpponent, dist);
			}

			// Detect ball touch
			if (player.ballTouchedStep) {
				data.touchedBall = true;
			}

			// Detect boost pads touched (simplified: track if near boost locations)
			if (IsNearBoostPad(player.pos)) {
				if ((player.pos - data.lastPos).Length() < 100.0f) {
					data.touchedBoost = true;
					data.touchedBoostCount++;
				}
			}

			data.lastPos = player.pos;
		}

		return 0.0f;
	}

	void PreStep(const GameState& state) override {}

private:
	// ====================================================================
	// Core Detection Functions
	// ====================================================================

	bool IsInKickoffPhase(const GameState& state) const {
		Vec ballPos = state.ball.pos;
		float ballDist = ballPos.Length();
		float ballSpeed = state.ball.vel.Length();
		return (ballDist < KICKOFF_RADIUS) && (ballSpeed < MAX_BALL_SPEED_KICKOFF);
	}

	bool IsFirstPlayer(const Player& player, const GameState& state) const {
		// First player is the one closest to the ball at kickoff start
		float distToBall = player.pos.Length();
		for (const auto& other : state.players) {
			if (other.team != player.team) continue;
			if (other.carId == player.carId) continue;
			float otherDist = other.pos.Length();
			if (otherDist < distToBall) {
				return false;  // Other teammate is closer, so we're second player
			}
		}
		return true;
	}

	bool IsNearBoostPad(const Vec& pos) const {
		// Check proximity to corner boost locations (~2200 units from center at corners)
		float distFromCenter = pos.Length();
		// Rough heuristic: between 1800-2400 units from center = near a boost location
		return distFromCenter > 1800.0f && distFromCenter < 2400.0f;
	}

	bool IsWavedashAttempt(const KickoffData& data) const {
		// Wavedash kickoff: touchedBall while speed is moderate (jump + wavedash pattern)
		return data.touchedBall && data.maxSpeedAchieved > 1200.0f && data.maxSpeedAchieved < 2200.0f;
	}

	bool IsStandardSpeedflip(const KickoffData& data) const {
		// Standard: high speed achieved + ball touched
		return data.touchedBall && data.maxSpeedAchieved > 2200.0f;
	}

	bool IsVariation(const KickoffData& data) const {
		// Variation: ball touched but unusual speed profile (not standard speedflip)
		return data.touchedBall && data.maxSpeedAchieved > 1500.0f && data.maxSpeedAchieved < 2200.0f;
	}

	bool IsFakeKickoff(const KickoffData& data) const {
		// Fake: didn't touch ball, relatively low speed
		return !data.touchedBall && data.maxSpeedAchieved < 1500.0f;
	}

	bool IsCornerBoostGrab(const KickoffData& data) const {
		// Corner boost: touched boost instead of going for ball
		return data.touchedBoost && !data.touchedBall;
	}

	bool IsDemoAttempt(const KickoffData& data) const {
		// Demo: got very close to opponent and touched ball
		return data.minDistToOpponent < DEMO_THRESHOLD && data.touchedBall;
	}

	bool IsSupportSafe(const KickoffData& data, const Player& player,
					   const GameState& state) const {
		// Support safe: stayed back, between ball and net, low speed
		Vec ballPos = state.ball.pos;
		float distToBall = (player.pos - ballPos).Length();
		// Stayed well back from ball
		return !data.touchedBall && distToBall > SUPPORT_SAFE_DISTANCE &&
			   data.maxSpeedAchieved < 1500.0f;
	}

	// ====================================================================
	// Strategy Evaluation
	// ====================================================================

	float EvaluateKickoffStrategy(const Player& player, const GameState& state,
								  KickoffData& data) const {
		KickoffStrategy strategy = STRATEGY_UNKNOWN;
		float reward = 0.0f;

		// Classify strategy
		if (data.isFirstPlayer) {
			// First player (challenger) strategies
			if (IsWavedashAttempt(data)) {
				strategy = STRATEGY_WAVEDASH;
				reward = REWARD_WAVEDASH;
			} else if (IsDemoAttempt(data)) {
				strategy = STRATEGY_DEMO;
				reward = REWARD_DEMO;
			} else if (IsStandardSpeedflip(data)) {
				strategy = STRATEGY_STANDARD;
				reward = REWARD_STANDARD;
			} else if (IsVariation(data)) {
				strategy = STRATEGY_VARIATION;
				reward = REWARD_VARIATION;
			} else if (IsCornerBoostGrab(data)) {
				strategy = STRATEGY_CORNER_BOOST;
				reward = REWARD_CORNER_BOOST;
			} else if (IsFakeKickoff(data)) {
				strategy = STRATEGY_FAKE;
				reward = REWARD_FAKE;
			}
		} else {
			// Second+ player (support) strategies
			if (IsSupportSafe(data, player, state)) {
				strategy = STRATEGY_SUPPORT_SAFE;
				reward = REWARD_SUPPORT_SAFE;
			} else if (data.touchedBoost && !data.touchedBall) {
				strategy = STRATEGY_SUPPORT_BOOST;
				reward = REWARD_SUPPORT_BOOST;
			} else if (IsDemoAttempt(data)) {
				strategy = STRATEGY_SUPPORT_DEMO;
				reward = REWARD_SUPPORT_DEMO;
			}
		}

		// Apply speed-based bonus (encourage faster execution)
		if (reward > 0.0f && data.maxSpeedAchieved > 2000.0f) {
			float speedBonus = RS_MIN(0.5f, (data.maxSpeedAchieved - 2000.0f) / 1000.0f);
			reward *= (1.0f + speedBonus * 0.3f);
		}

		return reward;
	}
};

} // namespace RLGC
