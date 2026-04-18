#pragma once
#include "Reward.h"
#include "../Math.h"
#include <algorithm>

namespace RLGC {

	/**
	 * \brief Rewards intelligent kickoff corner boost assignment in 3v3
	 *
	 * Trigger: 3v3 only (6+ players), ball velocity ≈ 0, ball.z ≈ 93 (starting height)
	 *
	 * Logic:
	 * - Identifies the 2 back corner boost pads on the player's team side
	 * - For each back boost pad, assigns it based on proximity
	 * - Excludes the player closest to ball (they should pursue ball, not boost)
	 * - Rewards the closest teammate for moving toward their assigned pad
	 *
	 * Anti-farming: Must actually reach the pad to get reward; ball contact overrides boost incentive
	 *
	 * Weight: 3.0 (moderate impact for phase 2+)
	 * Active: PHASE_2_SILVER+
	 */
	class KickoffCornerBoostAssignmentReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Only activate for 3v3 (6 players total)
			if (state.players.size() < 6) {
				return 0.0f;
			}

			// Kickoff detection: ball velocity ≈ 0 and z ≈ 93 UU (starting height)
			constexpr float BALL_START_Z = 93.0f;
			constexpr float VELOCITY_THRESHOLD = 100.0f;

			float ballVel = state.ball.vel.Length();
			bool isKickoff = ballVel < VELOCITY_THRESHOLD &&
						   std::abs(state.ball.pos.z - BALL_START_Z) < 50.0f;

			if (!isKickoff) {
				return 0.0f;
			}

			// Find the player on this team closest to the ball
			float minDistToBall = 10000.0f;
			uint32_t ballClosestCarId = UINT32_MAX;

			for (const auto& p : state.players) {
				if (p.team == player.team) {
					float dist = (p.pos - state.ball.pos).Length();
					if (dist < minDistToBall) {
						minDistToBall = dist;
						ballClosestCarId = p.carId;
					}
				}
			}

			// If this player is closest to ball, no boost reward (let them pursue ball)
			if (player.carId == ballClosestCarId) {
				return 0.0f;
			}

			// Get back corner boost pads for this team
			// Blue team: (-3072, -4096) and (3072, -4096)
			// Orange team: (-3072, 4096) and (3072, 4096)
			Vec backCornerPads[2];
			if (player.team == Team::BLUE) {
				backCornerPads[0] = Vec(-3072.0f, -4096.0f, 0.0f);
				backCornerPads[1] = Vec(3072.0f, -4096.0f, 0.0f);
			} else {
				backCornerPads[0] = Vec(-3072.0f, 4096.0f, 0.0f);
				backCornerPads[1] = Vec(3072.0f, 4096.0f, 0.0f);
			}

			// Find which back corner pad this player is closest to
			float distToPad0 = (player.pos - backCornerPads[0]).Length();
			float distToPad1 = (player.pos - backCornerPads[1]).Length();

			int assignedPadIdx = (distToPad0 < distToPad1) ? 0 : 1;
			Vec assignedPad = backCornerPads[assignedPadIdx];

			// Calculate distance to assigned pad
			float distToAssignedPad = (player.pos - assignedPad).Length();

			// Reward proximity to the assigned pad (smooth distance function)
			// At 0 distance: reward = 1.0
			// At 2000 distance: reward = 0.0
			// Linear falloff between 0-2000 UU
			if (distToAssignedPad > 2000.0f) {
				return 0.0f;
			}

			float proximityReward = (2000.0f - distToAssignedPad) / 2000.0f;
			return proximityReward;
		}
	};

	/**
	 * \brief Rewards passive boost awareness during rotations
	 *
	 * Trigger: Only during actual rotation (player moving toward own goal)
	 *
	 * Logic:
	 * - Detect rotation: player velocity toward own half (vel.y * team_direction < 0)
	 * - Check teammate boost levels
	 * - If teammate boost < 40 AND their distance to big boost > my distance: reward (+0.1)
	 * - If I take big boost when teammate < 30: penalize (-0.2)
	 *
	 * Anti-farming: Only applies during actual rotation phase, not every frame
	 *
	 * Weight: 0.5 (very low impact, passive awareness)
	 * Active: PHASE_3_GOLD+
	 */
	class RotationBoostAwarenessReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Only apply in multi-player modes
			if (state.players.size() < 4) {
				return 0.0f;
			}

			// Determine team direction (Blue = -y, Orange = +y)
			float teamDirection = (player.team == Team::BLUE) ? -1.0f : 1.0f;

			// Detect rotation: moving toward own half
			float velocityTowardOwnGoal = player.vel.y * teamDirection;
			if (velocityTowardOwnGoal >= 0.0f) {
				// Not rotating, moving away from goal or stationary
				return 0.0f;
			}

			// We're rotating. Check teammate boost situations
			float bestReward = 0.0f;

			for (const auto& teammate : state.players) {
				if (teammate.team != player.team || teammate.carId == player.carId) {
					continue;
				}

				// If teammate is very low on boost, we might want to defer
				if (teammate.boost < 30.0f) {
					// Penalize if we're taking big boost when teammate is critical
					float distToNearestBigBoost = GetDistToNearestBigBoost(player.pos);
					float teammateDist = GetDistToNearestBigBoost(teammate.pos);

					if (distToNearestBigBoost < 500.0f && teammateDist < 2000.0f) {
						// We're at a big boost and teammate is low
						bestReward = -0.2f;
						break;
					}
				} else if (teammate.boost < 40.0f) {
					// Teammate is moderately low. Reward if they're further from boost than us
					float distToNearestBigBoost = GetDistToNearestBigBoost(player.pos);
					float teammateDist = GetDistToNearestBigBoost(teammate.pos);

					if (teammateDist > distToNearestBigBoost + 1000.0f) {
						// Teammate is far from boost, we're closer. Let them have priority.
						bestReward = std::max(bestReward, 0.1f);
					}
				}
			}

			return bestReward;
		}

	private:
		// Quick approximation of distance to nearest big boost pad
		float GetDistToNearestBigBoost(const Vec& pos) const {
			constexpr float BIG_BOOST_POSITIONS[][3] = {
				// Blue team big boosts
				{-3072.0f, -4096.0f, 0.0f},
				{3072.0f, -4096.0f, 0.0f},
				{-3584.0f, 0.0f, 0.0f},
				{3584.0f, 0.0f, 0.0f},
				// Orange team big boosts
				{-3072.0f, 4096.0f, 0.0f},
				{3072.0f, 4096.0f, 0.0f},
			};

			float minDist = 10000.0f;
			for (size_t i = 0; i < 6; i++) {
				Vec padPos(BIG_BOOST_POSITIONS[i][0], BIG_BOOST_POSITIONS[i][1], BIG_BOOST_POSITIONS[i][2]);
				float dist = (pos - padPos).Length();
				minDist = std::min(minDist, dist);
			}
			return minDist;
		}
	};

	/**
	 * \brief Gentle reward for role-based boost preference positioning
	 *
	 * Trigger: Non-kickoff, in-game, 3v3 only, ball far away (> 1500 UU)
	 *
	 * Logic:
	 * - Only when NOT near ball (distance > 1500 UU)
	 * - Detect player role from typical position (back vs mid vs forward)
	 * - Gentle preference (low weight) for role-appropriate pads
	 * - Never overrides ball pursuit
	 *
	 * Anti-farming: Weight is low enough (0.3) that it doesn't distort behavior,
	 * only a passive nudge. Only applies when ball is far away.
	 *
	 * Weight: 0.3 (minimal, passive nudge only)
	 * Active: PHASE_4_PLATINUM+
	 */
	class RoleBasedBoostPreferenceReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Only in 3v3
			if (state.players.size() < 6) {
				return 0.0f;
			}

			// Only when ball is far away
			float distToBall = (player.pos - state.ball.pos).Length();
			if (distToBall < 1500.0f) {
				return 0.0f;
			}

			// Determine player role based on typical position
			// Back: y closer to own goal (negative y for Blue, positive y for Orange)
			float teamDirection = (player.team == Team::BLUE) ? -1.0f : 1.0f;
			float roleValue = player.pos.y * teamDirection;  // Negative = back, positive = forward

			// Detect role thresholds (empirical from typical positioning)
			// Back: < -2500, Mid: -2500 to 0, Forward: > 0
			bool isBack = roleValue < -2500.0f;
			bool isMid = roleValue >= -2500.0f && roleValue <= 0.0f;
			bool isForward = roleValue > 0.0f;

			// Get nearby boost pads based on role preference
			float backPadDist = GetDistToBackBoosts(player.pos, player.team);
			float midPadDist = GetDistToMidBoosts(player.pos, player.team);

			float reward = 0.0f;

			// Soft preference (very low magnitude, doesn't override other behaviors)
			if (isBack && backPadDist < 2000.0f) {
				reward = 0.05f * (2000.0f - backPadDist) / 2000.0f;
			} else if (isMid && midPadDist < 2000.0f) {
				reward = 0.05f * (2000.0f - midPadDist) / 2000.0f;
			}

			return reward;
		}

	private:
		float GetDistToBackBoosts(const Vec& pos, Team team) const {
			Vec backPads[2];
			if (team == Team::BLUE) {
				backPads[0] = Vec(-3072.0f, -4096.0f, 0.0f);
				backPads[1] = Vec(3072.0f, -4096.0f, 0.0f);
			} else {
				backPads[0] = Vec(-3072.0f, 4096.0f, 0.0f);
				backPads[1] = Vec(3072.0f, 4096.0f, 0.0f);
			}

			float dist0 = (pos - backPads[0]).Length();
			float dist1 = (pos - backPads[1]).Length();
			return std::min(dist0, dist1);
		}

		float GetDistToMidBoosts(const Vec& pos, Team team) const {
			Vec midPads[2];
			if (team == Team::BLUE) {
				midPads[0] = Vec(-3584.0f, 0.0f, 0.0f);
				midPads[1] = Vec(3584.0f, 0.0f, 0.0f);
			} else {
				midPads[0] = Vec(-3584.0f, 0.0f, 0.0f);
				midPads[1] = Vec(3584.0f, 0.0f, 0.0f);
			}

			float dist0 = (pos - midPads[0]).Length();
			float dist1 = (pos - midPads[1]).Length();
			return std::min(dist0, dist1);
		}
	};

}  // namespace RLGC
