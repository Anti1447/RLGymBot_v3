#pragma once
#include "Reward.h"
#include "../Math.h"
#include <map>
#include <cmath>

namespace RLGC {

// ============================================================================
// INTELLIGENT WAVEDASH REWARD - Context-Aware Wavedashing
// ============================================================================
// Only rewards intelligent wavedashing in 4 high-value situations:
// 1. Wall transitions (ground↔wall) - maintain speed during surface changes
// 2. Post-50/50 advantage - wavedash immediately after ball challenge
// 3. Aerial recovery - wavedash landing after aerial with flip remaining
// 4. ZapDash - advanced flick mechanic (placeholder, 0.0 scoring)
//
// Filters out wavedash spam: speed < 800 UU/s = NO REWARD
// 2026-02-08 | NJH
// ============================================================================

class IntelligentWavedashReward : public Reward {
private:
	// Per-player state tracking
	struct WavedashContextState {
		// Landing/Aerial Recovery State
		enum LandingPhase { FLYING, LANDED_WAITING, LANDING_WINDOW_CLOSED };
		LandingPhase landingPhase = FLYING;
		float timeSinceLanding = 0.0f;
		bool hasFlipRemaining = true;

		// 50/50 Challenge State
		enum ChallengePhase { NO_CHALLENGE, CHALLENGE_ACTIVE, CHALLENGE_RESOLVED };
		ChallengePhase challengePhase = NO_CHALLENGE;
		float timeSinceChallenge = 0.0f;

		// Wall Transition State
		bool lastFrameOnGround = false;

		// General Tracking
		bool wasFlipping = false;
		float prevSpeed = 0.0f;
	};

	mutable std::map<uint32_t, WavedashContextState> playerStates;

	// Universal speed gate
	static constexpr float MIN_USEFUL_SPEED = 800.0f;

	// Condition-specific base rewards
	static constexpr float WALL_TRANSITION_BASE = 3.0f;
	static constexpr float POST_5050_BASE = 4.0f;
	static constexpr float AERIAL_RECOVERY_BASE = 3.5f;

	// Speed gain bonus thresholds
	static constexpr float SPEED_GAIN_THRESHOLD = 100.0f;
	static constexpr float SPEED_GAIN_SATURATION = 300.0f;

	// Time windows
	static constexpr float POST_5050_WINDOW = 0.3f;
	static constexpr float LANDING_WINDOW = 0.2f;

	// Wall proximity detection
	static constexpr float WALL_PROXIMITY_THRESHOLD = 150.0f;

public:
	virtual void Reset(const GameState& initialState) override {
		playerStates.clear();
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!state.prev) return 0.0f;

		auto& playerState = playerStates[player.carId];
		const Player& prevPlayer = state.prev->players[player.index];

		float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
		float curSpeed = player.vel.Length();

		// Universal speed gate - apply to ALL conditions
		if (curSpeed < MIN_USEFUL_SPEED) {
			UpdateAerialState(playerState, player, prevPlayer, dt);
			playerState.wasFlipping = player.isFlipping;
			playerState.prevSpeed = curSpeed;
			return 0.0f;
		}

		// Check conditions in priority order
		float reward = 0.0f;

		// Condition 1: Wall Transitions (highest priority - most common)
		if (IsWallTransitionWavedash(player, prevPlayer, playerState)) {
			reward = CalculateReward(player, state, WALL_TRANSITION_BASE, playerState);
		}
		// Condition 2: Post-50/50 Advantage
		else if (IsPost5050Wavedash(player, state, playerState, dt)) {
			reward = CalculateReward(player, state, POST_5050_BASE, playerState);
		}
		// Condition 3: Aerial Recovery
		else if (IsAerialRecoveryWavedash(player, prevPlayer, playerState)) {
			reward = CalculateReward(player, state, AERIAL_RECOVERY_BASE, playerState);
		}

		// Update state for next frame
		UpdateAerialState(playerState, player, prevPlayer, dt);
		playerState.wasFlipping = player.isFlipping;
		playerState.prevSpeed = curSpeed;
		playerState.lastFrameOnGround = player.isOnGround;

		return reward;
	}

private:
	// ====================================================================
	// Condition 1: Wall Transitions
	// ====================================================================
	bool IsWallTransitionWavedash(const Player& player, const Player& prevPlayer,
								  WavedashContextState& state) const {
		bool groundNow = player.isOnGround;
		bool groundPrev = state.lastFrameOnGround;
		bool landedThisFrame = (groundNow && !groundPrev);

		if (!landedThisFrame) return false;

		// Check if near wall
		float wallDist = GetDistanceToNearestWall(player.pos);
		bool nearWall = (wallDist < WALL_PROXIMITY_THRESHOLD);

		// Flip near landing
		bool flipNearLanding = (player.isFlipping || prevPlayer.isFlipping);

		return nearWall && flipNearLanding;
	}

	// ====================================================================
	// Condition 2: Post-50/50 Wavedash
	// ====================================================================
	bool IsPost5050Wavedash(const Player& player, const GameState& state,
							WavedashContextState& state_ref, float dt) const {
		// Phase 1: Detect if 50/50 just happened
		if (state_ref.challengePhase == WavedashContextState::ChallengePhase::NO_CHALLENGE &&
			player.ballTouchedStep) {
			// Check if opponent also touched (50/50 definition)
			bool is5050 = false;
			for (const auto& opp : state.players) {
				if (opp.team == player.team) continue;
				if (opp.ballTouchedStep) {
					is5050 = true;
					break;
				}
			}

			if (is5050 && state.ball.pos.z < 400.0f) {  // Low contact (ground level)
				state_ref.challengePhase = WavedashContextState::ChallengePhase::CHALLENGE_ACTIVE;
				state_ref.timeSinceChallenge = 0.0f;
			}
		}

		// Phase 2: Countdown challenge window
		if (state_ref.challengePhase == WavedashContextState::ChallengePhase::CHALLENGE_ACTIVE) {
			state_ref.timeSinceChallenge += dt;

			// Window closed
			if (state_ref.timeSinceChallenge > POST_5050_WINDOW) {
				state_ref.challengePhase = WavedashContextState::ChallengePhase::CHALLENGE_RESOLVED;
				return false;
			}

			// Phase 3: Detect wavedash within window
			bool landedThisFrame = (player.isOnGround && !state.prev->players[player.index].isOnGround);
			bool flipNearLanding = (player.isFlipping || state.prev->players[player.index].isFlipping);

			if (landedThisFrame && flipNearLanding) {
				state_ref.challengePhase = WavedashContextState::ChallengePhase::CHALLENGE_RESOLVED;
				return true;
			}
		}

		return false;
	}

	// ====================================================================
	// Condition 3: Aerial Recovery Wavedash
	// ====================================================================
	bool IsAerialRecoveryWavedash(const Player& player, const Player& prevPlayer,
								  WavedashContextState& state) const {
		// Must be landing from aerial with flip remaining
		bool groundNow = player.isOnGround;
		bool groundPrev = state.lastFrameOnGround;
		bool landedThisFrame = (groundNow && !groundPrev);

		if (!landedThisFrame) return false;

		// Must have flip remaining
		if (!state.hasFlipRemaining) return false;

		// Must be flipping at landing
		bool flipNearLanding = (player.isFlipping || prevPlayer.isFlipping);
		if (!flipNearLanding) return false;

		return true;
	}

	// ====================================================================
	// Aerial State Tracking Helper
	// ====================================================================
	void UpdateAerialState(WavedashContextState& state, const Player& player,
						  const Player& prevPlayer, float dt) const {
		bool groundNow = player.isOnGround;
		bool groundPrev = state.lastFrameOnGround;

		// Detect jump (ground to air)
		if (groundPrev && !groundNow) {
			state.landingPhase = WavedashContextState::LandingPhase::FLYING;
			state.hasFlipRemaining = true;
		}

		// Track if flip was used during flight
		if (state.landingPhase == WavedashContextState::LandingPhase::FLYING) {
			if (player.isFlipping && !state.wasFlipping) {
				state.hasFlipRemaining = false;
			}
		}

		// Detect landing
		if (groundNow && !groundPrev) {
			state.landingPhase = WavedashContextState::LandingPhase::LANDED_WAITING;
			state.timeSinceLanding = 0.0f;
		}

		// Close landing window after 0.2s
		if (state.landingPhase == WavedashContextState::LandingPhase::LANDED_WAITING) {
			state.timeSinceLanding += dt;
			if (state.timeSinceLanding > LANDING_WINDOW) {
				state.landingPhase = WavedashContextState::LandingPhase::LANDING_WINDOW_CLOSED;
			}
		}
	}

	// ====================================================================
	// Reward Calculation
	// ====================================================================
	float CalculateReward(const Player& player, const GameState& state,
						 float baseReward, WavedashContextState& playerState) const {
		// Speed gain bonus (0-2.0x)
		float curSpeed = player.vel.Length();
		float speedGain = curSpeed - playerState.prevSpeed;
		float speedGainBonus = 0.0f;

		if (speedGain > SPEED_GAIN_THRESHOLD) {
			float gainFactor = RS_CLAMP((speedGain - SPEED_GAIN_THRESHOLD) / SPEED_GAIN_SATURATION,
										0.0f, 1.0f);
			speedGainBonus = 2.0f * gainFactor;
		} else if (speedGain >= 0.0f) {
			speedGainBonus = 0.5f * (speedGain / SPEED_GAIN_THRESHOLD);
		}

		// Geometry bonus (0-0.5x) - reward for 2-wheel contact
		int wheelCount = 0;
		for (int i = 0; i < 4; i++) {
			if (player.wheelsWithContact[i]) wheelCount++;
		}

		float geometryBonus = 0.0f;
		if (wheelCount == 2) {
			geometryBonus = 0.5f;  // Perfect 2-wheel contact
		} else if (wheelCount == 3) {
			geometryBonus = 0.2f;  // Nearly perfect
		}

		// Total reward (capped at 10.0)
		float totalReward = baseReward + speedGainBonus + geometryBonus;
		return RS_MIN(totalReward, 10.0f);
	}

	// ====================================================================
	// Helper Functions
	// ====================================================================
	float GetDistanceToNearestWall(const Vec& pos) const {
		// Distance to side walls
		float dx = std::abs(std::abs(pos.x) - CommonValues::SIDE_WALL_X);

		// Distance to back walls
		float dy = std::abs(std::abs(pos.y) - CommonValues::BACK_WALL_Y);

		// Distance to ceiling
		float dz = std::abs(CommonValues::CEILING_Z - pos.z);

		// Return minimum distance to any wall
		float minDist = RS_MIN(dx, dy);
		return RS_MIN(minDist, dz);
	}
};

} // namespace RLGC
