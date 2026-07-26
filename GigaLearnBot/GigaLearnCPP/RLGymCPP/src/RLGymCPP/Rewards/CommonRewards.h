#pragma once
#include "Reward.h"
#include "../Math.h"
#include "PositionAnalysis.h"
#include "RewardMath.h"
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


	// NOTE: PlayerGoalReward is DEAD — not used in RewardConfig. Live goal reward flows through
	// GoalReward / OwnGoalPenalty below. Do not wire this typedef up thinking it's the goal reward.
	typedef PlayerDataEventReward<&PlayerEventState::goal, false> PlayerGoalReward;
	typedef PlayerDataEventReward<&PlayerEventState::assist, false> AssistReward;
	typedef PlayerDataEventReward<&PlayerEventState::shot, false> ShotReward;
	typedef PlayerDataEventReward<&PlayerEventState::shotPass, false> ShotPassReward;
	typedef PlayerDataEventReward<&PlayerEventState::save, false> SaveReward;
	typedef PlayerDataEventReward<&PlayerEventState::bump, false> BumpReward;
	typedef PlayerDataEventReward<&PlayerEventState::demo, false> DemoReward;
	// NOTE: BumpedPenalty / DemoedPenalty are intentionally left DISABLED in RewardConfig.
	// BumpReward/DemoReward are wrapped in ZeroSumReward, which already subtracts the opponent's
	// bump/demo reward from the victim's team — enabling these would double-penalize. Do not re-enable.
	typedef PlayerDataEventReward<&PlayerEventState::bumped, true> BumpedPenalty;
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

	// Penalizes any player on the team that last touched the ball into their own goal.
	// - Fires only when state.goalScored and the last toucher was on the conceding team.
	// - Every player on the responsible team receives -penalty (default -1.0).
	// - Opposing team gets 0 (GoalReward already handles their payout).
	// - Does NOT fire on regular goals — only when the own team caused it.
	// Usage: WeightedReward(new OwnGoalPenalty(), 600.0f)  [no ZeroSum needed — already one-sided]
	class OwnGoalPenalty : public Reward {
	public:
		float penalty;
		OwnGoalPenalty(float penalty = -1.0f) : penalty(penalty) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.goalScored)
				return 0.0f;

			// Conceding team = team whose goal the ball is in
			Team concedingTeam = RS_TEAM_FROM_Y(state.ball.pos.y);

			// Only applies if the last touch was by someone on the conceding team
			bool lastTouchWasOwnTeam = false;
			for (const auto& p : state.players) {
				if (p.carId == (uint32_t)state.lastTouchCarID) {
					lastTouchWasOwnTeam = (p.team == concedingTeam);
					break;
				}
			}

			if (!lastTouchWasOwnTeam)
				return 0.0f;

			// Penalize every player on the responsible team
			return (player.team == concedingTeam) ? penalty : 0.0f;
		}
	};

	// 2025-12-17 | Rev 1.1 | NJH
	// 2026-03-08 | Rev 1.2 | NJH — replaced SpeedScale_50to140kph with SpeedScale_PowerShot
	// Speed-scaled Shot / Save / Goal rewards.
	inline float BallSpeedKph(const GameState& state) {
		// uu/s; uu = 1 cm => 1 uu/s = 0.01 m/s = 0.036 kph
		return state.ball.vel.Length() * 0.036f;
	}

	// Legacy linear scale (50-140 kph, 0.5x-1.0x). Kept for any future use.
	inline float SpeedScale_50to140kph(const GameState& state) {
		float kph = BallSpeedKph(state);
		if (kph <= 50.0f) return 0.5f;
		if (kph >= 140.0f) return 1.0f;
		float t = (kph - 50.0f) / (140.0f - 50.0f); // 0..1
		return 0.5f + 0.5f * t;
	}

	// Power shot scale: three-segment linear ramp.
	// 2026-04-16 | NJH | Rev 1.3 — extended upper cap (140 kph -> 180 kph, 1.5x -> 2.0x)
	// to keep rewarding faster shots up to RL's realistic max.
	// <= 50 kph  => 0.1x  (tap shots get almost nothing)
	// >= 100 kph => 1.0x  (baseline full reward)
	// >= 140 kph => 1.5x  (hard shot bonus)
	// >= 180 kph => 2.0x  (max bonus for power shots)
	inline float SpeedScale_PowerShot(const GameState& state) {
		float kph = BallSpeedKph(state);
		if (kph <= 50.0f)  return 0.1f;
		if (kph >= 180.0f) return 2.0f;
		if (kph >= 140.0f) {
			float t = (kph - 140.0f) / (180.0f - 140.0f); // 0..1
			return 1.5f + 0.5f * t;                        // 1.5 -> 2.0
		}
		if (kph >= 100.0f) {
			float t = (kph - 100.0f) / (140.0f - 100.0f); // 0..1
			return 1.0f + 0.5f * t;                        // 1.0 -> 1.5
		}
		float t = (kph - 50.0f) / (100.0f - 50.0f); // 0..1
		return 0.1f + 0.9f * t;                      // 0.1 -> 1.0
	}

	// 2D (XY) proximity scale from ball to opponent goal center.
	// 1.0 within 1000 UU, linear decay to 0.0 at 5120 UU, 0.0 beyond.
	// Used by SpeedScaledShotReward, VelocityBallToGoalReward, EnhancedVelocityBallToGoalReward.
	inline float GoalProximityScale2D(const Vec& ballPos, Team team) {
		static constexpr float FULL_DIST = 1000.0f;
		static constexpr float ZERO_DIST = 5120.0f;
		float goalY = (team == Team::BLUE) ? 5120.0f : -5120.0f;
		float dx = ballPos.x;          // opponent goal center X is always 0
		float dy = ballPos.y - goalY;
		float dist2D = std::sqrt(dx * dx + dy * dy);
		if (dist2D <= FULL_DIST) return 1.0f;
		if (dist2D >= ZERO_DIST) return 0.0f;
		return (ZERO_DIST - dist2D) / (ZERO_DIST - FULL_DIST);
	}

	class SpeedScaledShotReward : public Reward {
	private:
		ShotReward inner;
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			float r = inner.GetReward(player, state, isFinal);
			if (r == 0.0f) return 0.0f;
			return r * SpeedScale_PowerShot(state) * GoalProximityScale2D(state.ball.pos, player.team);
		}
	};

	class SpeedScaledSaveReward : public Reward {
	private:
		SaveReward inner;
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			float r = inner.GetReward(player, state, isFinal);
			if (r == 0.0f) return 0.0f;
			return r * SpeedScale_PowerShot(state);
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
				return r * SpeedScale_PowerShot(state);
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
			// Possession gate: in our half, skip offensive signal if opponent last touched
			bool inOurHalf = (player.team == Team::BLUE)
				? (state.ball.pos.y < 0.0f) : (state.ball.pos.y > 0.0f);
			if (inOurHalf && state.lastTouchCarID >= 0) {
				for (const auto& p : state.players)
					if (p.carId == (uint32_t)state.lastTouchCarID && p.team != player.team)
						return 0.0f;
			}

			bool targetOrangeGoal = player.team == Team::BLUE;
			if (ownGoal)
				targetOrangeGoal = !targetOrangeGoal;

			Vec targetPos = targetOrangeGoal ? CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

			Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
			float base = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);
			return base * GoalProximityScale2D(state.ball.pos, player.team);
		}
	};

	// ============================================================================
	// EnhancedVelocityBallToGoalReward (Improved variant with distance + intent weighting)
	// 2026-03-06 | Rev 1.0 | Claude / NJH
	// Strategy 4 (Composite Enhanced): Distance weighting + Intent gating + Alignment threshold
	// - Favors closer shots (distance multiplier: 1.0x at 2000 UU, 0.4x at 5000 UU)
	// - Rewards active touches with +0.25 bonus
	// - Requires MIN_ALIGNMENT >= 0.15 (~82°) to avoid rewarding sideways motion
	// - Gradual migration from original variant over 20B steps
	// ============================================================================
	class EnhancedVelocityBallToGoalReward : public Reward {
	private:
		static constexpr float TOUCH_BONUS = 0.25f;             // +25% for active touches
		static constexpr float MIN_ALIGNMENT = 0.15f;           // ~82° angle min

		// 2026-06-16 | NJH/Claude | Off-target scale-down
		// MIN_ALIGNMENT alone (~82° cone to goal center) lets a ball curling wide past
		// either post keep earning near-full reward all the way to the goal line. Scale
		// (not zero) the offensive reward when the current trajectory won't actually
		// cross between the posts, so this stays directional shaping rather than a hard
		// shot-quality gate (that's ShotPlacementReward's job).
		static constexpr float OFF_TARGET_SCALE = 0.3f;         // multiplier when trajectory misses goal mouth

		// NEW: Proximity-based alignment weighting (direction-prioritizing near goal)
		static constexpr float CLOSE_RANGE = 1500.0f;           // < 1500 UU: boost zone
		static constexpr float MID_RANGE = 3500.0f;             // 1500-3500 UU: balanced zone
		static constexpr float FAR_RANGE = 5000.0f;             // 3500-5000 UU: decay zone
		static constexpr float CLOSE_BOOST = 1.5f;              // Max boost at close range
		static constexpr float DEFENSIVE_ZONE = 2500.0f;        // Defensive reward triggers within this distance

		// Returns true if our team made the most recent ball touch
		bool OurTeamHasLastTouch(const Player& player, const GameState& state) const {
			if (state.lastTouchCarID < 0) return false;
			uint32_t toucherId = (uint32_t)state.lastTouchCarID;
			for (const auto& p : state.players)
				if (p.carId == toucherId)
					return p.team == player.team;
			return false;
		}

		// Returns true when every opponent is both far from their own goal (>3000 UU)
		// and far from the ball (>2000 UU) — i.e. their net is genuinely open
		bool OpponentOvercommitted(const Player& player, const GameState& state) const {
			Vec oppGoal = (player.team == Team::BLUE)
				? CommonValues::ORANGE_GOAL_BACK
				: CommonValues::BLUE_GOAL_BACK;
			for (const auto& p : state.players) {
				if (p.team == player.team) continue;
				if ((p.pos - oppGoal).Length() <= 3000.0f) return false;
				if ((p.pos - state.ball.pos).Length() <= 2000.0f) return false;
			}
			return true;
		}

	public:
		bool ownGoal = false;
		EnhancedVelocityBallToGoalReward(bool ownGoal = false) : ownGoal(ownGoal) {}

		// NEW: Proximity-based alignment weight function
		// Returns weight multiplier based on distance to goal
		// Close shots (< 1500 UU): 1.5x boost, prioritizes direction over speed
		// Mid shots (1500-3500 UU): 1.2x to 0.8x, balanced
		// Far shots (3500+ UU): 0.8x to 0.4x, requires precision
		static inline float GetAlignmentWeightByDistance(float distToGoal) {
			if (distToGoal < CLOSE_RANGE) {
				// Close-range boost: linear from 1.5x at 0 to 1.2x at 1500
				return CLOSE_BOOST - (0.3f * distToGoal / CLOSE_RANGE);
			} else if (distToGoal < MID_RANGE) {
				// Mid-range balanced: linear from 1.2x at 1500 to 0.8x at 3500
				float progress = (distToGoal - CLOSE_RANGE) / (MID_RANGE - CLOSE_RANGE);
				return 1.2f - (0.4f * progress);
			} else if (distToGoal < FAR_RANGE) {
				// Far-range decay: linear from 0.8x at 3500 to 0.4x at 5000
				float progress = (distToGoal - MID_RANGE) / (FAR_RANGE - MID_RANGE);
				return 0.8f - (0.4f * progress);
			} else {
				return 0.0f;  // No reward beyond 5000 UU
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			Vec ownGoalPos = (player.team == Team::BLUE)
				? CommonValues::BLUE_GOAL_BACK
				: CommonValues::ORANGE_GOAL_BACK;

			float distToOwnGoal = (state.ball.pos - ownGoalPos).Length();

			if (distToOwnGoal < DEFENSIVE_ZONE) {
				// Defensive component — always fires regardless of who last touched
				Vec ballDirAwayFromOwn = (state.ball.pos - ownGoalPos).Normalized();
				float defensiveAlignment = RS_CLAMP(
					ballDirAwayFromOwn.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED),
					0.0f, 1.0f
				);

				float defensiveReward = 0.0f;
				if (defensiveAlignment > MIN_ALIGNMENT) {
					float defensiveWeight = GetAlignmentWeightByDistance(distToOwnGoal) * 0.5f;
					defensiveReward = defensiveAlignment * defensiveWeight;
					if (player.ballTouchedStep)
						defensiveReward = RS_CLAMP(defensiveReward + TOUCH_BONUS, 0.0f, 1.5f);
				}

				// D3: opponent overcommit — we have possession and their net is open,
				// so pointing the ball at their goal is now the correct call even from deep defense
				if (!ownGoal && OurTeamHasLastTouch(player, state) && OpponentOvercommitted(player, state)) {
					Vec oppGoalPos = (player.team == Team::BLUE)
						? CommonValues::ORANGE_GOAL_BACK
						: CommonValues::BLUE_GOAL_BACK;
					Vec ballDirToOpp = (oppGoalPos - state.ball.pos).Normalized();
					float offensiveAlignment = RS_CLAMP(
						ballDirToOpp.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED),
						0.0f, 1.0f
					);
					if (offensiveAlignment > MIN_ALIGNMENT) {
						float distToOppGoal = (state.ball.pos - oppGoalPos).Length();
						float offensiveReward = offensiveAlignment * GetAlignmentWeightByDistance(distToOppGoal);
						if (player.ballTouchedStep)
							offensiveReward = RS_CLAMP(offensiveReward + TOUCH_BONUS, 0.0f, 1.5f);
						offensiveReward *= 1.4f;  // overcommit boost
						return RS_MAX(defensiveReward, offensiveReward);
					}
				}

				return defensiveReward;
			}

			// Offensive component: possession gate when ball is still in our half
			// (outside the DEFENSIVE_ZONE radius but Y-side still ours)
			bool inOurHalf = (player.team == Team::BLUE)
				? (state.ball.pos.y < 0.0f) : (state.ball.pos.y > 0.0f);
			if (!ownGoal && inOurHalf && !OurTeamHasLastTouch(player, state))
				return 0.0f;

			// Standard offensive reward calculation (unchanged)
			bool targetOrangeGoal = player.team == Team::BLUE;
			if (ownGoal)
				targetOrangeGoal = !targetOrangeGoal;

			Vec targetPos = targetOrangeGoal
				? CommonValues::ORANGE_GOAL_BACK
				: CommonValues::BLUE_GOAL_BACK;

			Vec ballDirToGoal = (targetPos - state.ball.pos).Normalized();
			float goalAlignment = ballDirToGoal.Dot(state.ball.vel / CommonValues::BALL_MAX_SPEED);

			if (goalAlignment < MIN_ALIGNMENT) return 0.0f;

			float baseAlignment = RS_CLAMP(goalAlignment, 0.0f, 1.0f);
			float distToGoal = (state.ball.pos - targetPos).Length();
			float alignmentWeight = GetAlignmentWeightByDistance(distToGoal);

			if (alignmentWeight <= 0.0f) return 0.0f;

			float reward = baseAlignment * alignmentWeight;

			if (player.ballTouchedStep)
				reward = RS_CLAMP(reward + TOUCH_BONUS, 0.0f, 1.5f);

			bool onTarget = RewardMath::BallIntersectsOpponentGoalMouth(player, state, 2.0f, false, true);
			if (!onTarget)
				reward *= OFF_TARGET_SCALE;

			return reward * GoalProximityScale2D(state.ball.pos, player.team);
		}
	};

	// ============================================================================
	// CornerToLaneReward
	// Fires when the ball is in the opponent's corner (high |x|, opponent half).
	// Rewards ball x-velocity directed toward the center lane (reducing |x|).
	//
	// Motivation: bots tend to pinch or blast the ball corner-to-corner without
	// creating a real scoring chance. This reward teaches controlled touches that
	// move the ball to the center lane where shots are actually viable.
	//
	// Only the x-component toward center is rewarded — the existing
	// VelocityBallToGoalReward handles the y-component (toward goal) separately.
	// Wrap in ZeroSumReward so both teams compete for center lane possession.
	//
	// Deep-corner zone (|y| > 4500, |x| > GOAL_WIDTH_FROM_CENTER): when the ball
	// is already near the goal line but outside the posts, switch to full 3D
	// alignment toward the opponent goal center. This prevents the bot from blasting
	// the ball into the back wall/corner when VelocityBallToGoalReward's y-push
	// bias would otherwise overshoot the narrow goal opening.
	// ============================================================================
	class CornerToLaneReward : public Reward {
	private:
		static constexpr float CORNER_X          = 2000.0f;
		static constexpr float CORNER_Y          = 1500.0f;
		// Deep-corner zone thresholds
		static constexpr float DEEP_CORNER_Y     = 4500.0f;
		static constexpr float DEEP_CORNER_SCALE = 2.5f;

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			const Vec& bpos = state.ball.pos;
			const Vec& bvel = state.ball.vel;

			// Ball must be in opponent's half
			bool inOpponentHalf = (player.team == Team::BLUE)
				? (bpos.y > CORNER_Y)
				: (bpos.y < -CORNER_Y);
			if (!inOpponentHalf) return 0.0f;

			// Deep-corner zone: ball near the goal line but outside the posts.
			// Full 3D alignment toward goal center — supercedes the CORNER_X gate
			// so it also catches narrower-corner positions (892 < |x| < 2000).
			if (fabsf(bpos.y) > DEEP_CORNER_Y && fabsf(bpos.x) > CommonValues::GOAL_WIDTH_FROM_CENTER) {
				Vec goalCenter = (player.team == Team::BLUE)
					? CommonValues::ORANGE_GOAL_CENTER
					: CommonValues::BLUE_GOAL_CENTER;
				Vec toGoal = (goalCenter - bpos).Normalized();
				float alignment = RS_CLAMP(toGoal.Dot(bvel / CommonValues::BALL_MAX_SPEED), 0.0f, 1.0f);
				return alignment * DEEP_CORNER_SCALE;
			}

			// Standard zone: ball must be well into the corner (near side wall)
			if (fabsf(bpos.x) < CORNER_X) return 0.0f;

			// Reward x-velocity directed toward the center lane.
			// -sign(bpos.x) * vel.x is positive when ball moves toward x = 0.
			float sign = (bpos.x > 0.0f) ? 1.0f : -1.0f;
			float towardCenter = -sign * bvel.x / CommonValues::BALL_MAX_SPEED;
			return RS_CLAMP(towardCenter, 0.0f, 1.0f);
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

	// Kickoff-only variant of VelocityPlayerToBallReward.
	// Returns the standard vel-toward-ball reward only while the ball is at kickoff position
	// (pos.Length() < 280 UU && vel.Length() < 200 UU/s). Returns 0 otherwise.
	class KickoffVelocityPlayerToBallReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			if (state.ball.pos.Length() >= 280.0f || state.ball.vel.Length() >= 200.0f)
				return 0.0f;
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
	// 2026-2-3 | Rev 1.5 | NJH
	// - Added directional speed gain reward (only reward forward-facing wavedashes)
	// - Teaches intentional acceleration, not random bounces
	// ============================================================================
	//class WavedashBasicReward : public Reward {
	//private:
	//	static constexpr float MIN_SPEED_GAIN = 25.0f;     // Minimum forward speed gain to reward
	//	static constexpr float MAX_BONUS_SPEED = 500.0f;   // Speed gain for max bonus
	//	static constexpr float MAX_BONUS = 0.5f;           // Maximum bonus amplitude
	//	static constexpr float BASE_REWARD = 1.0f;         // Base reward for landing flip

	//public:
	//	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
	//		if (!player.prev)
	//			return 0;

	//		// Check if landing from flip
	//		if (!(player.isOnGround && (player.prev->isFlipping && !player.prev->isOnGround))) {
	//			return 0;
	//		}

	//		// Calculate speed gain in forward direction
	//		Vec oldVel = player.prev->vel;
	//		Vec newVel = player.vel;
	//		Vec carForward = player.rotMat.forward;

	//		// Speed gain projected onto car's forward direction
	//		float oldSpeedForward = oldVel.Dot(carForward);
	//		float newSpeedForward = newVel.Dot(carForward);
	//		float speedGainForward = newSpeedForward - oldSpeedForward;

	//		// Only reward positive forward acceleration
	//		if (speedGainForward < MIN_SPEED_GAIN) {
	//			return 0.0f;  // Didn't gain enough forward speed
	//		}

	//		// Scale reward: base 1.0 + bonus for good acceleration
	//		// 25 UU/s gain = 1.0, 500+ UU/s = 1.5 max
	//		float bonus = RS_CLAMP(speedGainForward / MAX_BONUS_SPEED, 0.0f, MAX_BONUS);
	//		return BASE_REWARD + bonus;
	//	}
	//};
	// ============================================================================
	// WavedashReward
	// 2026-2-4 | Rev 1.6 | NJH
	// - Basic GigaLearn-style wavedash reward (no speed bonus)
	// ============================================================================
	class WavedashBasicReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			if (!player.prev)
				return 0;

			// Skip reward during kickoff (all players bunched together, no real wavedash learning)
			if (PositionAnalysis::IsKickoffFormation(player, state)) {
				return 0;
			}

			if (player.isOnGround && (player.prev->isFlipping && !player.prev->isOnGround)) {
				return 1;
			}
			else {
				return 0;
			}
		}
	};

	// WaveDashHeightReward — Rev 1.0 (2026-02-24, NJH)
	// Height-scaled variant of WavedashBasicReward.
	// Identical detection logic and kickoff gate. Reward scales linearly:
	//   0.5x at Z=0  →  2.0x at Z=CEILING_Z (2044)
	// Formula: reward = 0.5 + 1.5 * clamp(pos.z / CEILING_Z, 0, 1)
	// Encourages wavedashes high on the wall and near the ceiling.
	class WaveDashHeightReward : public Reward {
	private:
		static constexpr float HEIGHT_MULT_MIN = 0.5f;  // multiplier at Z = 0
		static constexpr float HEIGHT_MULT_MAX = 2.0f;  // multiplier at Z = CEILING_Z

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0.0f;

			if (PositionAnalysis::IsKickoffFormation(player, state))
				return 0.0f;

			if (player.isOnGround && (player.prev->isFlipping && !player.prev->isOnGround)) {
				float t = RS_CLAMP(player.pos.z / CommonValues::CEILING_Z, 0.0f, 1.0f);
				return HEIGHT_MULT_MIN + (HEIGHT_MULT_MAX - HEIGHT_MULT_MIN) * t;
			}
			return 0.0f;
		}
	};

	// ============================================================================
	// RolvWavedashReward — Rev 1.0 (2026-06-27, NJH)
	// Ported from rolv's WavedashReward (rlgym-tools)
	// Source: https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/reward_functions/wavedash_reward.py
	//
	// scaleByAcceleration=true (default):
	//   Records velocity delta magnitude when flip starts.
	//   At landing (wavedash), reward = stored_accel / CAR_MAX_SPEED → [0, ~1]
	//   Rewards wavedashes that generated real speed gain.
	// scaleByAcceleration=false:
	//   Returns 1.0 on any wavedash landing (identical to WavedashBasicReward).
	// ============================================================================
	class RolvWavedashReward : public Reward {
		bool scaleByAcceleration;
		mutable std::unordered_map<uint32_t, float> prevAcceleration;

	public:
		RolvWavedashReward(bool scaleByAcceleration = true)
			: scaleByAcceleration(scaleByAcceleration) {}

		virtual void Reset(const GameState& initialState) override {
			prevAcceleration.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev) return 0.0f;
			const Player& prev = *player.prev;

			bool landedThisFrame = player.isOnGround && !prev.isOnGround;
			bool wavedash = landedThisFrame && (player.isFlipping || prev.isFlipping);

			if (!scaleByAcceleration)
				return wavedash ? 1.0f : 0.0f;

			float& storedAcc = prevAcceleration[player.carId];

			// Record velocity delta when flip starts
			if (player.isFlipping && !prev.isFlipping)
				storedAcc = (player.vel - prev.vel).Length();

			if (wavedash) {
				float acc = storedAcc;
				storedAcc = 0.0f;
				return acc / CommonValues::CAR_MAX_SPEED;
			}

			if (!player.isFlipping)
				storedAcc = 0.0f;

			return 0.0f;
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
			reward += 0.05f * airPrep;  // REDUCED from 1.0f

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

			reward += 0.1f * wheelOrient * tiltReward;  // REDUCED from 2.0f

			// ====================================================================
			// 4. Detect 2-wheel contact event
			// ====================================================================
			int wheelCountNow = CountWheelsInContact(player);
			int wheelCountPrev = CountWheelsInContact(*player.prev);

			bool twoWheelEvent = (wheelCountNow == 2 && wheelCountPrev < 2);

			if (twoWheelEvent) {
				reward += 0.25f;  // REDUCED from 5.0f
			}

			// ====================================================================
			// 5. Flip timing reward (flip input near 2-wheel contact)
			// ====================================================================
			bool flipInputNow = player.isFlipping;
			bool flipInputPrev = player.prev->isFlipping;

			if (twoWheelEvent && flipInputNow) {
				reward += 0.4f;  // REDUCED from 8.0f
			}

			// ====================================================================
			// 6. Flip cancel reward (flip interrupted by surface contact)
			// ====================================================================
			bool flipCanceled = (flipInputPrev && !flipInputNow && wheelCountNow >= 1);

			if (flipCanceled) {
				reward += 0.5f;  // REDUCED from 10.0f
			}

			// ====================================================================
			// 7. Tangential velocity gain reward
			// ====================================================================
			float speedGain = vTangent.Length() - prevTangent.Length();
			if (speedGain > 0.0f) {
				reward += 0.002f * speedGain;  // DRASTICALLY REDUCED from 2.0f (was 800 for 400 UU/s!)
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
					reward += 0.15f * gainFrac;  // REDUCED from 3.0f
				}
			}

			// ====================================================================
			// 9. Penalties
			// ====================================================================

			// Too much airtime (encourages ground contact)
			if (!onGroundNow && onGroundPrev) {
				float dt = (state.deltaTime > 0.0f) ? state.deltaTime : 0.016f;
				reward -= 0.005f * dt;  // REDUCED from 0.1f
			}

			// Full flip that wasn't canceled (wasted flip)
			if (flipInputPrev && !flipInputNow && wheelCountNow < 1) {
				reward -= 0.25f;  // REDUCED from 5.0f
			}

			// Flat 4-wheel landing (bad geometry for wavedash)
			if (wheelCountNow == 4 && wheelCountPrev < 4) {
				reward -= 0.15f;  // REDUCED from 3.0f
			}

			// Discourage boosting during dash (wavedash shouldn't need boost)
			if (player.prevAction.boost > 0.5f) {
				reward -= 0.025f;  // REDUCED from 0.5f
			}

			s.wasFlipping = flipInputNow;
			s.prevVel = player.vel;

			return reward;
		}
	};

	class PickupBoostReward : public Reward {
	public:
		// lossWeight defaults to 0 (no penalty on boost spend) to preserve existing
		// behavior in every phase that doesn't explicitly opt in. See BoostManagementRewards.h
		// for the paired PadCenterAccuracyReward.
		float lossWeight;
		PickupBoostReward(float lossWeight = 0.0f) : lossWeight(lossWeight) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.prev)
				return 0;

			float delta = sqrtf(player.boost / 100.f) - sqrtf(player.prev->boost / 100.f);
			if (delta > 0) return delta;
			if (delta < 0) return delta * lossWeight;
			return 0;
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

	// Classic Touch accel reward from Gigalearn repo
	// Mostly based on the classic Necto rewards
	// Total reward output for speeding the ball up to MAX_REWARDED_BALL_SPEED is 1.0
	// The bot can do this slowly (putting) or quickly (shooting)
	// Rev 2.0 2026-07-07 NJH — added per-player cooldown (CLAUDE.md §7C pattern):
	// previously fired every single step during a sustained dribble/carry, diluting
	// the "discrete power touch" signal. See Reward_Change_Log.md.
	class TouchAccelReward : public Reward {
	public:
		constexpr static float MAX_REWARDED_BALL_SPEED = RLGC::Math::KPHToVel(140);

		TouchAccelReward(float cooldownMs = 500.0f) : cooldownSeconds(cooldownMs / 1000.0f) {}

		virtual void Reset(const GameState&) override {
			_cooldownRemaining.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev)
				return 0;

			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
			float& cd = _cooldownRemaining[player.carId];
			if (cd > 0.0f) {
				cd = RS_MAX(0.0f, cd - dt);
				return 0;
			}

			if (player.ballTouchedStep) {
				float prevSpeedFrac = RS_MIN(1, state.prev->ball.vel.Length() / MAX_REWARDED_BALL_SPEED);
				float curSpeedFrac = RS_MIN(1, state.ball.vel.Length() / MAX_REWARDED_BALL_SPEED);

				if (curSpeedFrac > prevSpeedFrac) {
					float reward = curSpeedFrac - prevSpeedFrac;
					cd = cooldownSeconds;
					return reward;
				}
				else {
					// Not speeding up the ball so we don't care
					return 0;
				}
			}
			else {
				return 0;
			}
		}

	private:
		float cooldownSeconds;
		mutable std::unordered_map<uint32_t, float> _cooldownRemaining;
	};

	// Attacks the problem: "Accelerate the ball smartly"
	// Combines: impulse-awareness (from NEW) + directional intent (novel)
	// Anti-farms naturally: dribbles are lateral/evasive, not goal-directed
	class DirectionalAccelerationReward : public Reward {
	public:
		float maxRewardedBallSpeed;
		float maxRewardedImpulse;

		DirectionalAccelerationReward(
			float maxBallSpeed = RLGC::Math::KPHToVel(140),
			float maxImpulse = RLGC::Math::KPHToVel(83)
		) : maxRewardedBallSpeed(maxBallSpeed),
			maxRewardedImpulse(maxImpulse)
		{
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;
			if (!player.ballTouchedStep) return 0.0f;

			// Gate 1: Did we speed up the ball?
			const float prevSpeed = state.prev->ball.vel.Length();
			const float curSpeed = state.ball.vel.Length();
			float speedDelta = curSpeed - prevSpeed;
			if (speedDelta <= 0.0f) return 0.0f;

			// Gate 2: How much force did we apply?
			Vec impulseVec = state.ball.vel - state.prev->ball.vel;
			float impulse = impulseVec.Length();
			float impulseFrac = RS_CLAMP(impulse / maxRewardedImpulse, 0.0f, 1.0f);

			// Gate 3: Is the new velocity pointed toward the opponent goal?
			// FIX 2026-07-07: was a fixed world axis (pure +Y/-Y), which let a bot near
			// the sideline/corner get full credit for blasting the ball at the back wall,
			// nowhere near the goal mouth. Now uses the ball-relative goal vector, matching
			// FlipIntoContactReward's established pattern (this file).
			const Vec& oppGoal = (player.team == Team::BLUE)
				? RLGC::CommonValues::ORANGE_GOAL_CENTER
				: RLGC::CommonValues::BLUE_GOAL_CENTER;
			Vec ballVelDir = state.ball.vel.Normalized();
			float goalAlignment = RS_CLAMP(
				ballVelDir.Dot((oppGoal - state.ball.pos).Normalized()),
				0.0f, 1.0f
			);

			// Base speed reward (like classic)
			float prevSpeedFrac = RS_CLAMP(prevSpeed / maxRewardedBallSpeed, 0.0f, 1.0f);
			float curSpeedFrac = RS_CLAMP(curSpeed / maxRewardedBallSpeed, 0.0f, 1.0f);
			float speedReward = curSpeedFrac - prevSpeedFrac;

			// Combined: speed × effort × direction
			// Only reward if we're both accelerating AND pointing toward goal
			float reward = speedReward * impulseFrac * goalAlignment;
			return (reward > 0.0f) ? reward : 0.0f;
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
			}
			else {
				return 0;
			}
		}
	};


	// Rewards airborne flip touches directed toward the opponent goal.
	// Provides gradient signal for moderate-power flip contacts that fall below
	// StrongTouchReward's threshold (≥5 kph vs ≥100 kph in Phase 6).
	// Gate: player.isFlipping && !player.isOnGround && ball touched → impulse × dirMult.
	class FlipIntoContactReward : public Reward {
	public:
		float minRewardedVel, maxRewardedVel, cooldownSeconds;

		FlipIntoContactReward(float minKPH = 5.0f, float maxKPH = 100.0f, float cooldownMs = 500.0f) {
			minRewardedVel  = RLGC::Math::KPHToVel(minKPH);
			maxRewardedVel  = RLGC::Math::KPHToVel(maxKPH);
			cooldownSeconds = cooldownMs / 1000.0f;
		}

		virtual void Reset(const GameState&) override { _cooldown.clear(); }

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;
			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
			float& cd = _cooldown[player.carId];
			if (cd > 0.0f) { cd = RS_MAX(0.0f, cd - dt); return 0.0f; }

			if (!player.ballTouchedStep || !player.isFlipping || player.isOnGround) return 0.0f;

			float hitForce = (state.ball.vel - state.prev->ball.vel).Length();
			if (hitForce < minRewardedVel) return 0.0f;

			const Vec& oppGoal = (player.team == Team::BLUE)
				? RLGC::CommonValues::ORANGE_GOAL_CENTER
				: RLGC::CommonValues::BLUE_GOAL_CENTER;
			float dirMult = RS_CLAMP(state.ball.vel.Normalized().Dot(
				(oppGoal - state.ball.pos).Normalized()), 0.0f, 1.0f);

			float result = RS_MIN(1.0f, hitForce / maxRewardedVel) * dirMult;
			if (result > 0.0f) cd = cooldownSeconds;
			return result;
		}

	private:
		mutable std::unordered_map<uint32_t, float> _cooldown;
	};


	// Rewards a flip contact that occurs within halfVolleyWindowSec of the ball
	// bouncing off the ground. Bounce detection runs in PreStep (once per step).
	// A half-volley combines the ball's rising momentum with the flip's linear
	// velocity for maximum power.
	class HalfVolleyReward : public Reward {
	public:
		float minRewardedVel, maxRewardedVel, cooldownSeconds, halfVolleyWindowSec;

		HalfVolleyReward(float windowSec = 0.4f, float minKPH = 5.0f,
			float maxKPH = 130.0f, float cooldownMs = 500.0f) {
			halfVolleyWindowSec = windowSec;
			minRewardedVel      = RLGC::Math::KPHToVel(minKPH);
			maxRewardedVel      = RLGC::Math::KPHToVel(maxKPH);
			cooldownSeconds     = cooldownMs / 1000.0f;
		}

		virtual void Reset(const GameState&) override {
			_cooldown.clear();
			_timeSinceBounce = 99.0f;
		}

		virtual void PreStep(const GameState& state) override {
			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
			_timeSinceBounce += dt;
			// Ground bounce: ball was meaningfully falling, now rising, near floor
			if (state.prev &&
				state.prev->ball.vel.z < -100.0f &&
				state.ball.vel.z > 0.0f &&
				state.ball.pos.z < 250.0f) {
				_timeSinceBounce = 0.0f;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev || _timeSinceBounce > halfVolleyWindowSec) return 0.0f;
			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
			float& cd = _cooldown[player.carId];
			if (cd > 0.0f) { cd = RS_MAX(0.0f, cd - dt); return 0.0f; }

			if (!player.ballTouchedStep || !player.isFlipping || player.isOnGround) return 0.0f;

			float hitForce = (state.ball.vel - state.prev->ball.vel).Length();
			if (hitForce < minRewardedVel) return 0.0f;

			const Vec& oppGoal = (player.team == Team::BLUE)
				? RLGC::CommonValues::ORANGE_GOAL_CENTER
				: RLGC::CommonValues::BLUE_GOAL_CENTER;
			float dirMult = RS_CLAMP(state.ball.vel.Normalized().Dot(
				(oppGoal - state.ball.pos).Normalized()), 0.0f, 1.0f);

			float result = RS_MIN(1.0f, hitForce / maxRewardedVel) * dirMult;
			if (result > 0.0f) cd = cooldownSeconds;
			return result;
		}

	private:
		float _timeSinceBounce = 99.0f;
		mutable std::unordered_map<uint32_t, float> _cooldown;
	};


	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/reward_functions/common_rewards/player_ball_rewards.py
	// Rewards movement toward closest opponent (light demo/bump shaping)
	class PlayerVelocityToOpponentReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) {
			// Find closest opponent
			float closestDist = FLT_MAX;
			int closestIdx = -1;

			for (size_t i = 0; i < state.players.size(); i++) {
				const Player& opponent = state.players[i];
				if (opponent.team == player.team)
					continue;

				float dist = (opponent.pos - player.pos).Length();
				if (dist < closestDist) {
					closestDist = dist;
					closestIdx = (int)i;
				}
			}

			// No opponents found
			if (closestIdx == -1)
				return 0.0f;

			Vec dirToOpponent = (state.players[closestIdx].pos - player.pos).Normalized();
			Vec normVel = player.vel / CommonValues::CAR_MAX_SPEED;
			return dirToOpponent.Dot(normVel);
		}
	};

	// ClosestPlayerSpeedReward (2025-02-17)
	// Rewards ONLY the closest non-demoed teammate to the ball for moving toward it.
	// Fires every step in open play and kickoffs (use ClosestPlayerKickoffSpeedReward
	// for a kickoff-only variant).
	// minPlayers: 4 = 2v2+ (default), 6 = 3v3 only, 2 = all modes.
	class ClosestPlayerSpeedReward : public Reward {
	private:
		int _minPlayers;
	public:
		ClosestPlayerSpeedReward(int minPlayers = 4) : _minPlayers(minPlayers) {}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if ((int)state.players.size() < _minPlayers)
				return 0.0f;

			// Find the closest non-demoed teammate to the ball
			float closestDist = std::numeric_limits<float>::max();
			int closestIdx = -1;

			for (size_t i = 0; i < state.players.size(); i++) {
				const Player& teammate = state.players[i];

				if (teammate.team != player.team || teammate.isDemoed)
					continue;

				float dist = (teammate.pos - state.ball.pos).Length();

				if (dist < closestDist) {
					closestDist = dist;
					closestIdx = (int)i;
				}
			}

			// No teammates found (shouldn't happen) or not the closest player
			if (closestIdx == -1 || state.players[closestIdx].carId != player.carId)
				return 0.0f;

			// Reward this player's velocity toward ball (same as VelocityPlayerToBallReward)
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			Vec normVel = player.vel / CommonValues::CAR_MAX_SPEED;
			return dirToBall.Dot(normVel);
		}
	};

	// ClosestPlayerKickoffSpeedReward (2026-02-23)
	// Kickoff-only variant of ClosestPlayerSpeedReward.
	// Identical logic, but gated: only fires when ball is within 500 UU of center.
	// Use this in kickoff-focused phases; use ClosestPlayerSpeedReward for open play.
	class ClosestPlayerKickoffSpeedReward : public Reward {
	private:
		// Ball within this radius of center = kickoff active (matches KickoffNoChaseTeamPenalty.h)
		static constexpr float KICKOFF_BALL_RADIUS = 500.0f;

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Gate: only active during kickoff
			if (state.ball.pos.Length() >= KICKOFF_BALL_RADIUS)
				return 0.0f;

			// Find the closest teammate to the ball
			float closestDist = std::numeric_limits<float>::max();
			int closestIdx = -1;

			for (size_t i = 0; i < state.players.size(); i++) {
				const Player& teammate = state.players[i];

				// Only consider teammates
				if (teammate.team != player.team)
					continue;

				float dist = (teammate.pos - state.ball.pos).Length();

				// Update if closer, or same distance but more to the left (smaller x)
				if (dist < closestDist || (dist == closestDist && closestIdx != -1 && teammate.pos.x < state.players[closestIdx].pos.x)) {
					closestDist = dist;
					closestIdx = (int)i;
				}
			}

			// No teammates found (shouldn't happen) or not the closest player
			if (closestIdx == -1 || state.players[closestIdx].carId != player.carId)
				return 0.0f;

			// Reward this player's velocity toward ball (same as VelocityPlayerToBallReward)
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			Vec normVel = player.vel / CommonValues::CAR_MAX_SPEED;
			return dirToBall.Dot(normVel);
		}
	};

	// ============================================================================
	// ClosestPlayerKickoffSideReward
	// Same as ClosestPlayerKickoffSpeedReward, but with team-aware side preference:
	// - Blue team: Favor +X side (right side from their goal perspective)
	// - Orange team: Favor -X side (left side from their goal perspective)
	// When equidistant, this ensures the "correct" starting side player gets credit
	// ============================================================================
	class ClosestPlayerKickoffSideReward : public Reward {
	private:
		// Ball within this radius of center = kickoff active (matches KickoffNoChaseTeamPenalty.h)
		static constexpr float KICKOFF_BALL_RADIUS = 500.0f;

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Gate: only active during kickoff
			if (state.ball.pos.Length() >= KICKOFF_BALL_RADIUS)
				return 0.0f;

			// Determine which X side to favor based on team
			// Blue team (defending -Y): favor +X side (right side)
			// Orange team (defending +Y): favor -X side (left side)
			float favoredXSide = (player.team == Team::BLUE) ? 1.0f : -1.0f;

			// Find the closest teammate to the ball, with side preference
			float closestDist = std::numeric_limits<float>::max();
			int closestIdx = -1;
			bool closestIsOnFavored = false;

			for (size_t i = 0; i < state.players.size(); i++) {
				const Player& teammate = state.players[i];

				// Only consider teammates
				if (teammate.team != player.team)
					continue;

				float dist = (teammate.pos - state.ball.pos).Length();
				bool isOnFavored = (teammate.pos.x * favoredXSide > 0.0f);

				// Update if:
				// 1. Closer distance, OR
				// 2. Same distance AND on favored side (while old one wasn't), OR
				// 3. Same distance AND both on favored side AND further toward favored X direction
				if (dist < closestDist ||
					(dist == closestDist && isOnFavored && !closestIsOnFavored) ||
					(dist == closestDist && isOnFavored && closestIsOnFavored &&
					 teammate.pos.x * favoredXSide > state.players[closestIdx].pos.x * favoredXSide)) {
					closestDist = dist;
					closestIdx = (int)i;
					closestIsOnFavored = isOnFavored;
				}
			}

			// No teammates found (shouldn't happen) or not the closest player
			if (closestIdx == -1 || state.players[closestIdx].carId != player.carId)
				return 0.0f;

			// Reward this player's velocity toward ball (same as ClosestPlayerKickoffSpeedReward)
			Vec dirToBall = (state.ball.pos - player.pos).Normalized();
			Vec normVel = player.vel / CommonValues::CAR_MAX_SPEED;
			return dirToBall.Dot(normVel);
		}
	};

	// RETIRED 2026-04-26: Replaced by SpeedFlipKickoffReward in AdvancedKickoffRewards.h
	// (Multi-phase: boost collect + flip quality + contact evaluation. Weight: 20.0f -> 2.0f)
	/*
	class SpeedflipKickoffReward : public Reward {
	private:
		static constexpr float KICKOFF_BALL_RADIUS = 500.0f;
		static constexpr float BASE_REWARD = 1.0f;

		struct FlipMemory {
			bool usedFlipInKickoff = false;
			bool ballWasHit = false;
		};

		mutable std::map<uint32_t, FlipMemory> flipMemory;

	public:
		virtual void Reset(const GameState& initialState) override {
			flipMemory.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			bool inKickoff = state.ball.pos.Length() < KICKOFF_BALL_RADIUS;
			if (!flipMemory.count(player.carId)) {
				flipMemory[player.carId] = FlipMemory();
			}
			auto& mem = flipMemory[player.carId];
			if (!inKickoff) {
				mem.usedFlipInKickoff = false;
				mem.ballWasHit = false;
				return 0.0f;
			}
			if (player.isFlipping || (state.prev && state.prev->players[player.carId].isFlipping)) {
				mem.usedFlipInKickoff = true;
			}
			if (player.ballTouchedStep) {
				mem.ballWasHit = true;
				if (mem.usedFlipInKickoff) {
					return BASE_REWARD;
				}
			}
			return 0.0f;
		}
	};
	*/

	// ============================================================================
	// KickoffDoubleCommitPenalty
	// Penalizes multiple teammates converging on the ball at the same TIME during kickoff.
	// Returns 0 in 1v1 (only 1 player per team).
	// Returns 0 when teammates have meaningfully different estimated arrival times — one
	// player is clearly the kicker and others are hanging back (no double commit).
	// Fires only when 2+ teammates are on track to reach the ball at a similar moment.
	//
	// Rev 1.1 (2026-07-19, NJH + Claude): was gated on raw approach-SPEED similarity,
	// which a policy could dodge by holding one teammate's speed >20% off the other's
	// while both still substantively raced for the ball — and speed similarity is the
	// wrong proxy anyway (same speed from very different distances doesn't mean a
	// simultaneous arrival, and different speeds from different distances can still
	// collide at the ball at the same moment). Switched to estimated time-to-reach-ball
	// similarity, which is what actually determines whether two cars are converging on
	// the ball together and can't be dodged by speed alone.
	// ============================================================================
	class KickoffDoubleCommitPenalty : public Reward {
	private:
		static constexpr float KICKOFF_BALL_RADIUS  = 500.0f;
		static constexpr float MIN_APPROACH_SPEED   = 200.0f;  // UU/s — must be actively approaching
		static constexpr float TIME_DIFF_THRESHOLD  = 0.20f;   // fraction — within 20% arrival time = "converging together"
		static constexpr float PENALTY_PER_COMMIT   = -2.0f;

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Gate: only during kickoff
			if (state.ball.pos.Length() >= KICKOFF_BALL_RADIUS)
				return 0.0f;

			// Count teammates on this team (including self)
			int teamSize = 0;
			for (const auto& p : state.players)
				if (p.team == player.team && !p.isDemoed) teamSize++;

			// 1v1: only 1 player per team — no double commit possible
			if (teamSize <= 1)
				return 0.0f;

			// Compute this player's approach speed toward the ball
			Vec toBall = (state.ball.pos - player.pos);
			float dist = toBall.Length();
			if (dist < 1.0f) return 0.0f;
			Vec toBallN = toBall / dist;
			float myApproach = player.vel.Dot(toBallN);

			// Not approaching the ball — can't be part of a double commit
			if (myApproach < MIN_APPROACH_SPEED)
				return 0.0f;

			float myTimeToReach = dist / RS_MAX(myApproach, 1.0f);

			// Count teammates on track to reach the ball at a similar time
			int similarCount = 0;
			for (const auto& p : state.players) {
				if (p.team != player.team || p.isDemoed || p.carId == player.carId) continue;

				Vec theirToBall = state.ball.pos - p.pos;
				float theirDist = theirToBall.Length();
				if (theirDist < 1.0f) continue;
				float theirApproach = p.vel.Dot(theirToBall / theirDist);
				if (theirApproach < MIN_APPROACH_SPEED) continue;

				float theirTimeToReach = theirDist / RS_MAX(theirApproach, 1.0f);

				// Are they on track to arrive at a similar time? — within TIME_DIFF_THRESHOLD fraction
				float fasterTime = RS_MIN(myTimeToReach, theirTimeToReach);
				float diff       = fabsf(myTimeToReach - theirTimeToReach) / RS_MAX(fasterTime, 0.01f);
				if (diff < TIME_DIFF_THRESHOLD)
					similarCount++;
			}

			// If all teammates arrive at meaningfully different times, no double commit
			if (similarCount == 0)
				return 0.0f;

			return PENALTY_PER_COMMIT * similarCount;
		}
	};

	// ============================================================================
	// KickoffCounterReward
	// Rewards winning the kickoff and converting possession into a goal
	// Specifically designed to reward:
	// 1. Getting possession off kickoff (first touch by our team)
	// 2. Scoring directly from that kickoff possession
	// 3. Countering against fast-moving opponents (higher opponent speed = harder counter)
	// ============================================================================
	class KickoffCounterReward : public Reward {
	private:
		static constexpr float KICKOFF_BALL_RADIUS = 500.0f;
		static constexpr float BASE_POSSESSION_REWARD = 6.0f;     // Base reward for winning kickoff
		static constexpr float GOAL_BONUS = 8.0f;                 // Bonus for scoring from kickoff counter
		static constexpr float POSSESSION_LOST_PENALTY = -1.0f;   // Penalty if we lost kickoff
		static constexpr float GOAL_CONCEDED_PENALTY = -8.0f;     // Penalty if they scored from our loss
		static constexpr float OPPONENT_SPEED_SCALAR = 0.02f;     // Velocity bonus multiplier (harder counters = more reward)
		static constexpr float MAX_SPEED_BONUS = 3.0f;            // Cap on velocity bonus

		struct CounterMemory {
			bool wasInKickoff = false;
			bool ourTeamGotFirstTouch = false;      // Did our team get first touch in this kickoff?
			int framesWithOurPossession = 0;        // How many frames have we had possession?
			float maxOpponentVelAtCounter = 0.0f;   // Max opponent speed during this counter
			bool hasScored = false;                 // Did we score from this possession?
			bool hasConceded = false;               // Did they score from our loss?
		};

		mutable std::map<uint32_t, CounterMemory> counterMemory;

	public:
		virtual void Reset(const GameState& initialState) override {
			counterMemory.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			bool inKickoff = state.ball.pos.Length() < KICKOFF_BALL_RADIUS;

			// Initialize memory for this player if not present
			if (!counterMemory.count(player.carId)) {
				counterMemory[player.carId] = CounterMemory();
			}

			auto& mem = counterMemory[player.carId];

			// PHASE 1: Entering kickoff
			if (inKickoff && !mem.wasInKickoff) {
				// Kickoff just started - reset tracking
				mem.wasInKickoff = true;
				mem.ourTeamGotFirstTouch = false;
				mem.framesWithOurPossession = 0;
				mem.maxOpponentVelAtCounter = 0.0f;
				mem.hasScored = false;
				mem.hasConceded = false;
				return 0.0f;
			}

			// PHASE 2: During kickoff - track first touch
			if (inKickoff) {
				// Only track first touch once per kickoff
				if (!mem.ourTeamGotFirstTouch) {
					// Check if anyone touched the ball this frame
					for (const auto& p : state.players) {
						if (p.ballTouchedStep) {
							// First touch in this kickoff
							if (p.team == player.team) {
								mem.ourTeamGotFirstTouch = true;

								// Track max opponent velocity at time of counter
								for (const auto& opp : state.players) {
									if (opp.team != player.team) {
										float oppSpeed = opp.vel.Length();
										mem.maxOpponentVelAtCounter = std::max(mem.maxOpponentVelAtCounter, oppSpeed);
									}
								}
							}
							break;
						}
					}
				}

				// If we have possession, track frames and check for opponent velocities
				if (mem.ourTeamGotFirstTouch && !mem.hasScored) {
					mem.framesWithOurPossession++;

					// Continue tracking opponent speed for velocity bonus calculation
					for (const auto& opp : state.players) {
						if (opp.team != player.team) {
							float oppSpeed = opp.vel.Length();
							mem.maxOpponentVelAtCounter = std::max(mem.maxOpponentVelAtCounter, oppSpeed);
						}
					}
				}

				return 0.0f;
			}

			// PHASE 3: Exiting kickoff - evaluate outcome
			if (mem.wasInKickoff && !inKickoff) {
				mem.wasInKickoff = false;

				float reward = 0.0f;

				// Check if goal was scored on the current or previous step
				if (state.goalScored) {
					// Determine which team scored
					Team scoringTeam = RS_TEAM_FROM_Y(state.ball.pos.y);

					if (mem.ourTeamGotFirstTouch) {
						// We won the kickoff and scored -> big reward!
						mem.hasScored = true;
						float speedBonus = std::min(mem.maxOpponentVelAtCounter * OPPONENT_SPEED_SCALAR, MAX_SPEED_BONUS);
						reward = BASE_POSSESSION_REWARD + GOAL_BONUS + speedBonus;
					} else if (scoringTeam != player.team) {
						// We lost the kickoff and they scored -> penalty
						mem.hasConceded = true;
						reward = GOAL_CONCEDED_PENALTY;
					}
				} else {
					// No goal yet, just possession outcome
					if (mem.ourTeamGotFirstTouch) {
						// We won the kickoff - give possession reward + opponent speed bonus
						float speedBonus = std::min(mem.maxOpponentVelAtCounter * OPPONENT_SPEED_SCALAR, MAX_SPEED_BONUS);
						reward = BASE_POSSESSION_REWARD + speedBonus;
					} else {
						// We lost the kickoff - small penalty
						reward = POSSESSION_LOST_PENALTY;
					}
				}

				// Reset for next kickoff
				mem.ourTeamGotFirstTouch = false;
				mem.framesWithOurPossession = 0;
				mem.maxOpponentVelAtCounter = 0.0f;

				return reward;
			}

			return 0.0f;
		}
	};
}
