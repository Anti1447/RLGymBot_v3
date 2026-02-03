#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"

namespace RLGC {

	// ========================================================================
	// OPTION CONTROL REWARD - 2025-02-01 | Rev 1.0 | NJH
	// ========================================================================
	// Encourages bots to move into positions with maximum strategic optionality
	// (i.e., positions with the most variety of advantageous plays available)
	//
	// SCORING OPTIONS ENUMERATED:
	// 1. Dribble: Ball on ground/low + player nearby (< 1500 UU)
	// 2. Flick: Ball at flick height (200-300 UU) + player nearby (< 2000 UU)
	// 3. Air Dribble: Ball airborne (> 150 UU) + player airborne + similar direction
	// 4. Bump/Demo: Any opponent within bump range (< 500 UU)
	// 5. Powershot: Ball kickable + target angle (±45° to goal) + reasonable speed
	// 6. Pass: At least one teammate within range (3000 UU)
	//
	// PASSING OPTIONS:
	// - Base: Any teammate within 3000 UU = +1 option
	// - Bonus: Well-positioned teammate (not behind, good angle) = +0.5 option
	//
	// GATING MULTIPLIERS:
	// - Scoring: 0.2x (0 options) → 1.5x (4+ options)
	// - Passing: 0.2x (0 options) → 1.2x (1.5+ options)
	//
	// INTEGRATION:
	// This reward can be used in two ways:
	// 1. Standalone: Reward bot for being in high-optionality states
	// 2. Gating: Other rewards query multipliers to modulate their signals
	//
	// ========================================================================

	class OptionControlReward : public Reward {
	private:
		// ====================================================================
		// CONFIGURATION CONSTANTS
		// ====================================================================

		// Scoring option thresholds
		static constexpr float DRIBBLE_RANGE = 1500.0f;
		static constexpr float FLICK_HEIGHT_MIN = 200.0f;
		static constexpr float FLICK_HEIGHT_MAX = 300.0f;
		static constexpr float FLICK_RANGE = 2000.0f;
		static constexpr float AIR_DRIBBLE_HEIGHT = 150.0f;
		static constexpr float AIR_DRIBBLE_RANGE = 2000.0f;
		static constexpr float BUMP_RANGE = 500.0f;
		static constexpr float POWERSHOT_ANGLE_RANGE = 45.0f;  // degrees from goal direction
		static constexpr float POWERSHOT_MIN_SPEED = 800.0f;
		static constexpr float POWERSHOT_MAX_SPEED = 2300.0f;

		// Passing option thresholds
		static constexpr float PASS_RANGE = 3000.0f;
		static constexpr float WELL_POSITIONED_BONUS = 0.5f;

		// Multiplier bounds
		static constexpr float MULTIPLIER_MIN = 0.2f;
		static constexpr float MULTIPLIER_MAX_SCORING = 1.5f;
		static constexpr float MULTIPLIER_MAX_PASSING = 1.2f;

		// Maximum possible options (for normalization)
		static constexpr int MAX_SCORING_OPTIONS = 6;
		static constexpr float MAX_PASS_OPTIONS = 3.0f;

		// Base reward value when options are available
		static constexpr float BASE_OPTION_REWARD = 1.0f;

		// ====================================================================
		// STATE TRACKING
		// ====================================================================

		// Track options per player for analysis/debugging
		mutable std::unordered_map<uint32_t, int> lastScoringOptions;
		mutable std::unordered_map<uint32_t, float> lastPassingOptions;

	public:
		OptionControlReward() = default;

		void Reset(const GameState& initialState) override {
			lastScoringOptions.clear();
			lastPassingOptions.clear();
		}

		// ====================================================================
		// MAIN REWARD FUNCTION (for standalone use)
		// ====================================================================

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Count total available options
			int scoringOptions = CountScoringOptions(player, state);
			float passingOptions = CountPassOptions(player, state);

			// Normalize to 0-1 range
			float scoringRatio = RS_CLAMP(float(scoringOptions) / float(MAX_SCORING_OPTIONS), 0.0f, 1.0f);
			float passingRatio = RS_CLAMP(passingOptions / MAX_PASS_OPTIONS, 0.0f, 1.0f);

			// Combine both option types (weight them equally)
			float totalOptionality = (scoringRatio + passingRatio) / 2.0f;

			// Store for potential debugging
			lastScoringOptions[player.carId] = scoringOptions;
			lastPassingOptions[player.carId] = passingOptions;

			// Return reward proportional to optionality
			return totalOptionality * BASE_OPTION_REWARD;
		}

		// ====================================================================
		// SCORING OPTIONS ENUMERATION
		// ====================================================================

		int CountScoringOptions(const Player& player, const GameState& state) const {
			int optionCount = 0;
			const Vec& ballPos = state.ball.pos;
			const Vec& ballVel = state.ball.vel;
			float distToBall = (ballPos - player.pos).Length();

			// OPTION 1: Dribble - ball on ground/low, player nearby
			if (ballPos.z < 300.0f && distToBall < DRIBBLE_RANGE) {
				optionCount++;
			}

			// OPTION 2: Flick - ball at flick height, player nearby
			if (ballPos.z > FLICK_HEIGHT_MIN && ballPos.z < FLICK_HEIGHT_MAX &&
			    distToBall < FLICK_RANGE) {
				optionCount++;
			}

			// OPTION 3: Air Dribble - ball airborne, player airborne, aligned
			if (ballPos.z > AIR_DRIBBLE_HEIGHT && !player.isOnGround &&
			    distToBall < AIR_DRIBBLE_RANGE) {
				// Check if both are moving in similar direction (alignment)
				Vec playerVelDir = player.vel.Normalized();
				Vec ballVelDir = ballVel.Normalized();
				float velAlignment = playerVelDir.Dot(ballVelDir);

				if (velAlignment > -0.5f) {  // Not opposing directions
					optionCount++;
				}
			}

			// OPTION 4: Bump/Demo - opponent nearby
			bool hasOpponentNearby = false;
			for (const auto& opponent : state.players) {
				if (opponent.team == player.team || opponent.isDemoed) continue;

				float distToOpponent = (opponent.pos - player.pos).Length();
				if (distToOpponent < BUMP_RANGE) {
					hasOpponentNearby = true;
					break;
				}
			}
			if (hasOpponentNearby) {
				optionCount++;
			}

			// OPTION 5: Powershot - ball kickable, good angle to goal, reasonable speed
			bool canPowershot = CanExecutePowershot(player, state);
			if (canPowershot) {
				optionCount++;
			}

			// OPTION 6: Pass to Teammate - at least one teammate in range
			bool hasPassOption = HasPassOption(player, state);
			if (hasPassOption) {
				optionCount++;
			}

			return RS_CLAMP(optionCount, 0, MAX_SCORING_OPTIONS);
		}

		// ====================================================================
		// PASSING OPTIONS ENUMERATION (with tiering)
		// ====================================================================

		float CountPassOptions(const Player& player, const GameState& state) const {
			float optionCount = 0.0f;

			for (const auto& teammate : state.players) {
				if (teammate.team != player.team || teammate.carId == player.carId) {
					continue;  // Skip opponents and self
				}

				float distToTeammate = (teammate.pos - player.pos).Length();

				// Base option: teammate within range
				if (distToTeammate < PASS_RANGE) {
					optionCount += 1.0f;

					// Bonus for well-positioned teammate
					if (IsTeammateWellPositioned(player, teammate, state)) {
						optionCount += WELL_POSITIONED_BONUS;
					}
				}
			}

			return RS_CLAMP(optionCount, 0.0f, MAX_PASS_OPTIONS);
		}

		// ====================================================================
		// MULTIPLIER FUNCTIONS (for gating mechanism)
		// ====================================================================

		float GetScoringMultiplier(int scoringOptions) const {
			if (scoringOptions == 0) return 0.2f;    // Very limited options
			if (scoringOptions < 2) return 0.6f;     // Some options
			if (scoringOptions < 4) return 1.0f;     // Good options
			return 1.5f;                             // Many options (bonus applies)
		}

		float GetPassingMultiplier(float passingOptions) const {
			if (passingOptions < 0.5f) return 0.2f;  // No good pass options
			if (passingOptions < 1.5f) return 0.7f;  // Limited options
			return 1.2f;                             // Good options (bonus applies)
		}

		// ====================================================================
		// HELPER FUNCTIONS
		// ====================================================================

	private:
		bool CanExecutePowershot(const Player& player, const GameState& state) const {
			const Vec& ballPos = state.ball.pos;
			float distToBall = (ballPos - player.pos).Length();

			// Ball must be kickable (not too high)
			if (ballPos.z > 300.0f) {
				return false;
			}

			// Ball must be in reasonable distance
			if (distToBall > DRIBBLE_RANGE) {
				return false;
			}

			// Ball must have reasonable speed
			float ballSpeed = state.ball.vel.Length();
			if (ballSpeed < POWERSHOT_MIN_SPEED || ballSpeed > POWERSHOT_MAX_SPEED) {
				return false;
			}

			// Ball direction must be roughly toward goal
			Vec toGoal = Vec(0, CommonValues::BACK_WALL_Y, 0) - ballPos;  // Simple goal center
			Vec ballDir = state.ball.vel.Normalized();
			Vec goalDir = toGoal.Normalized();

			float angleToGoal = std::acos(RS_CLAMP(ballDir.Dot(goalDir), -1.0f, 1.0f));
			float angleInDegrees = angleToGoal * 180.0f / 3.14159f;

			// Must be within ±45 degrees of goal direction
			return angleInDegrees < POWERSHOT_ANGLE_RANGE;
		}

		bool HasPassOption(const Player& player, const GameState& state) const {
			for (const auto& teammate : state.players) {
				if (teammate.team == player.team && teammate.carId != player.carId) {
					float distToTeammate = (teammate.pos - player.pos).Length();
					if (distToTeammate < PASS_RANGE) {
						return true;
					}
				}
			}
			return false;
		}

		bool IsTeammateWellPositioned(const Player& player, const Player& teammate,
		                                const GameState& state) const {
			// Teammate is well-positioned if:
			// 1. Not behind the player (Y-axis relative to player's forward direction)
			// 2. Not too far back defensively

			Vec toTeammate = teammate.pos - player.pos;
			Vec playerForward = player.rotMat.forward;

			// Check if teammate is ahead (positive Y for our goal)
			float dotProduct = toTeammate.Dot(playerForward);

			// Well-positioned if somewhat ahead and not too far back
			bool notBehind = dotProduct > -500.0f;  // Not too far behind

			// Additional check: teammate should be on offensive side of ball
			// (closer to opponent goal than the current player)
			const Vec& ballPos = state.ball.pos;
			float playerDistToGoal = std::abs(player.pos.y - CommonValues::BACK_WALL_Y);
			float tmDistToGoal = std::abs(teammate.pos.y - CommonValues::BACK_WALL_Y);

			bool onOffensiveSide = tmDistToGoal < playerDistToGoal;

			return notBehind && onOffensiveSide;
		}

	public:
		// ====================================================================
		// QUERY FUNCTIONS (for external use in gating)
		// ====================================================================

		int GetLastScoringOptions(uint32_t playerId) const {
			auto it = lastScoringOptions.find(playerId);
			return it != lastScoringOptions.end() ? it->second : 0;
		}

		float GetLastPassingOptions(uint32_t playerId) const {
			auto it = lastPassingOptions.find(playerId);
			return it != lastPassingOptions.end() ? it->second : 0.0f;
		}

		// For debugging: get string representation of available options
		std::string GetAvailableOptionsString(const Player& player, const GameState& state) const {
			int scoringOpts = CountScoringOptions(player, state);
			float passingOpts = CountPassOptions(player, state);

			std::string result = "Scoring: " + std::to_string(scoringOpts) + " | Passing: " +
			                     std::to_string(static_cast<int>(passingOpts * 2) / 2.0f);
			return result;
		}
	};

} // namespace RLGC
