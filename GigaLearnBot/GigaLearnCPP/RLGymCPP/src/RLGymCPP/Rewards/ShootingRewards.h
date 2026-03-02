#pragma once
#include "Reward.h"
#include "../Math.h"
// Note: BallPred::Cache (used by ShotPlacementReward at the bottom) is defined in
// BallPrediction.h, which is pulled in via CompleteMechanicsRewards.h before this
// file in RewardConfig.h. No direct include needed here.

namespace RLGC {

// ============================================================================
// SHOOTING REWARDS - Hard shots & outplay focus
// ============================================================================
// Progression:
// 1. Power Shot: Ball velocity, angle toward goal
// 2. Accurate Shot: Aiming (near post), distance from goal
// 3. Contested Shot: Shooting under pressure, beating defenders
// 4. Basic Outplay: Simple 1v1 beats, cutting past defenders
// ============================================================================

// ============================================================================
// PowerShotReward - Emphasizes hard, fast shots on goal
// Rewards shots with high ball velocity toward the goal
// Key: Ball speed (>1000 UU/s), direction toward goal, release from car
// ============================================================================
class PowerShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 1000.0f;      // Hard shot threshold
    static constexpr float OPTIMAL_SHOT_SPEED = 2500.0f;  // Ideal shot speed
    static constexpr float MAX_SHOT_SPEED = 4000.0f;      // Cap for scaling

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get opponent goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Ball must be moving toward goal after touch
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        // Must be a hard shot
        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        // Check direction toward goal
        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        // Must be aimed at goal
        if (goalAlignment < 0.4f) return 0.0f;

        // Reward based on shot speed and accuracy
        // Peak reward at OPTIMAL_SHOT_SPEED
        float speedFactor = 1.0f;
        if (ballSpeed < OPTIMAL_SHOT_SPEED) {
            speedFactor = ballSpeed / OPTIMAL_SHOT_SPEED;  // Scale up to optimal
        } else {
            speedFactor = 1.0f - RS_CLAMP((ballSpeed - OPTIMAL_SHOT_SPEED) / (MAX_SHOT_SPEED - OPTIMAL_SHOT_SPEED), 0.0f, 0.5f);
        }

        // Distance penalty (closer = better)
        float distToGoal = (state.ball.pos - opponentGoal).Length();
        float distanceFactor = 1.0f - RS_CLAMP(distToGoal / 5000.0f, 0.0f, 1.0f);  // Closer = better

        return 10.0f * speedFactor * goalAlignment * distanceFactor;  // 0-10 reward
    }
};

// ============================================================================
// AccurateShotReward - Rewards shooting with good aiming/placement
// High reward for shots aimed at goal posts (wide, high corners)
// Key: Post-side aiming, distance consideration, shot speed
// ============================================================================
class AccurateShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 800.0f;
    static constexpr float GOAL_WIDTH = 892.0f;  // Rocket League goal width

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get goal position (center)
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Ball must be moving toward goal
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        if (goalAlignment < 0.3f) return 0.0f;

        // Aim quality: reward shots aimed at posts (top corners, bottom corners)
        // Get goal line position
        float goalLineY = opponentGoal.y;
        float distToGoalLine = abs(state.ball.pos.y - goalLineY);

        // Lateral positioning (X-axis for posts)
        float lateralPos = abs(state.ball.pos.x);
        float postAiming = RS_CLAMP((GOAL_WIDTH * 0.4f) - abs(lateralPos - GOAL_WIDTH * 0.5f), 0.0f, GOAL_WIDTH * 0.4f) / (GOAL_WIDTH * 0.4f);

        // Height quality (reward high/low corners over center)
        float heightPos = state.ball.pos.z;
        float heightAiming = 1.0f - RS_CLAMP(abs(heightPos - 150.0f) / 300.0f, 0.0f, 1.0f);  // Avoid middle height

        // Distance factor (closer = more accurate needed)
        float distToGoal = (state.ball.pos - opponentGoal).Length();
        float accuracyRequirement = RS_CLAMP(distToGoal / 3000.0f, 0.5f, 1.0f);

        return 8.0f * (postAiming * 0.6f + heightAiming * 0.4f) * goalAlignment * accuracyRequirement;  // 0-8 reward
    }
};

// ============================================================================
// ContestedShotReward - Shooting while under pressure from defenders
// Rewards shots taken in dangerous situations (defenders close)
// Key: Defender proximity, shot speed, goal alignment under pressure
// ============================================================================
class ContestedShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 800.0f;
    static constexpr float DEFENDER_THREAT_RANGE = 1000.0f;  // Close defense

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Ball must be moving toward goal
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        if (goalAlignment < 0.3f) return 0.0f;

        // Check defender pressure
        float minDefenderDistance = 999999.0f;
        int defendersNearby = 0;

        for (const auto& other : state.players) {
            if (other.team == player.team) continue;  // Only enemy defenders

            float distToPlayer = (player.pos - other.pos).Length();
            minDefenderDistance = RS_MIN(minDefenderDistance, distToPlayer);

            if (distToPlayer < DEFENDER_THREAT_RANGE) {
                defendersNearby++;
            }
        }

        // Reward for shooting under pressure
        // More defenders = higher reward (if still on target)
        float pressureFactor = RS_CLAMP((float)defendersNearby / 2.0f, 0.0f, 1.0f);  // Cap at 2 defenders
        float defensePenalty = 1.0f - RS_CLAMP(minDefenderDistance / DEFENDER_THREAT_RANGE, 0.0f, 1.0f);  // Closer defender = higher reward

        return 9.0f * pressureFactor * goalAlignment * (0.5f + 0.5f * defensePenalty);  // 0-9 reward
    }
};

// ============================================================================
// BasicOutplayReward - Simple 1v1 outplays, cutting past defenders
// Rewards beating defenders with dribbling/speed, getting to better position
// Key: Defender proximity decrease, moving toward goal, possession
// ============================================================================
class BasicOutplayReward : public Reward {
private:
    mutable std::map<uint32_t, float> lastMinDefenderDist;
    static constexpr float OUTPLAY_SUCCESS_RANGE = 1500.0f;

public:
    virtual void Reset(const GameState& initialState) override {
        lastMinDefenderDist.clear();
    }

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;

        // Get opponent goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Check closest defender
        float minDefenderDistance = 999999.0f;
        for (const auto& other : state.players) {
            if (other.team == player.team) continue;

            float dist = (player.pos - other.pos).Length();
            minDefenderDistance = RS_MIN(minDefenderDistance, dist);
        }

        auto lastIt = lastMinDefenderDist.find(player.carId);
        float lastDefenderDistance = (lastIt != lastMinDefenderDist.end()) ? lastIt->second : minDefenderDistance;

        // Update for next step
        lastMinDefenderDist[player.carId] = minDefenderDistance;

        // Check if we're beating defender (getting closer to goal while maintaining distance from defender)
        float distToGoal = (player.pos - opponentGoal).Length();
        Vec toGoal = (opponentGoal - player.pos).Normalized();
        Vec ballVel = state.ball.vel.Normalized();
        float forwardProgress = ballVel.Dot(toGoal);

        // Reward for moving forward while maintaining ball control
        float defenderAvoidance = 0.0f;
        if (minDefenderDistance > lastDefenderDistance) {
            // Creating space from defender
            defenderAvoidance = RS_CLAMP((minDefenderDistance - lastDefenderDistance) / 200.0f, 0.0f, 1.0f);
        } else if (minDefenderDistance < lastDefenderDistance && minDefenderDistance > 500.0f) {
            // Moving toward goal while defender falls back
            defenderAvoidance = 0.5f;
        }

        // Reward only if progressing toward goal
        if (forwardProgress > 0.2f && minDefenderDistance < OUTPLAY_SUCCESS_RANGE) {
            float progressFactor = RS_CLAMP(forwardProgress, 0.0f, 1.0f);
            return 7.0f * defenderAvoidance * progressFactor;  // 0-7 reward
        }

        return 0.0f;
    }
};

// ============================================================================
// OpenGoalShotReward - Punishes missing easy opportunities
// High reward for quick, accurate shots on undefended goal
// Key: No defenders nearby, clear path to goal
// ============================================================================
class OpenGoalShotReward : public Reward {
private:
    static constexpr float MIN_SHOT_SPEED = 500.0f;
    static constexpr float OPEN_GOAL_DEFENDER_RANGE = 2000.0f;

public:
    virtual void Reset(const GameState& initialState) override {}

    virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
        if (!player.ballTouchedStep) return 0.0f;
        if (!state.prev) return 0.0f;

        // Get goal
        Vec opponentGoal = (player.team == Team::BLUE) ?
            CommonValues::ORANGE_GOAL_BACK : CommonValues::BLUE_GOAL_BACK;

        // Check if goal is undefended (no defenders nearby)
        int defendersNear = 0;
        for (const auto& other : state.players) {
            if (other.team == player.team) continue;

            float distToGoal = (other.pos - opponentGoal).Length();
            if (distToGoal < OPEN_GOAL_DEFENDER_RANGE) {
                defendersNear++;
            }
        }

        // Only reward if truly open (0-1 defenders nearby)
        if (defendersNear > 1) return 0.0f;

        // Ball toward goal
        Vec ballVel = state.ball.vel;
        float ballSpeed = ballVel.Length();

        if (ballSpeed < MIN_SHOT_SPEED) return 0.0f;

        Vec toGoal = (opponentGoal - state.ball.pos).Normalized();
        float goalAlignment = ballVel.Normalized().Dot(toGoal);

        if (goalAlignment < 0.5f) return 0.0f;

        // High reward for open shots (should be easy)
        return 12.0f * goalAlignment;  // 0-12 reward
    }
};

// ============================================================================
// NUMERICAL ADVANTAGE ATTACKING REWARD
// Incentivizes aggressive, coordinated attacking in 2v1+ situations
// Encourages demo/bump tactics and high-velocity passing plays
// ============================================================================
class NumericalAdvantageAttackingReward : public Reward {
private:
	enum class AttackerRole {
		NOT_ATTACKING,   // Not in numerical advantage
		PRIMARY,         // Closest to ball among attackers
		SUPPORT          // Second-closest to ball among attackers
	};

	struct AdvantageMemory {
		AttackerRole lastRole = AttackerRole::NOT_ATTACKING;
		int framesSinceDemoBump = 999;
		int framesSincePassing = 999;
		bool lastFrameHadPossession = false;
		uint32_t lastDemoTarget = UINT32_MAX;
	};

	std::unordered_map<uint32_t, AdvantageMemory> memory;
	int currentStep = 0;

	// Constants
	static constexpr float DEMO_RANGE = 1500.0f;
	static constexpr float PASS_QUALITY_THRESHOLD = 500.0f;  // Min velocity for quality pass
	static constexpr float ATTACKING_ZONE_DIST = 5000.0f;    // Distance from opponent goal
	static constexpr int POST_ACTION_WINDOW = 120;           // ~1 second at 120 Hz
	static constexpr float PASS_POSITION_MIN = 1500.0f;
	static constexpr float PASS_POSITION_MAX = 3500.0f;
	static constexpr float RECEPTION_POSITION_MIN = 1000.0f;
	static constexpr float RECEPTION_POSITION_MAX = 2500.0f;

	Vec GetOpponentGoalCenter(Team team) const {
		return (team == Team::BLUE)
			? CommonValues::ORANGE_GOAL_CENTER
			: CommonValues::BLUE_GOAL_CENTER;
	}

	bool IsInOpponentHalf(const Player& player) const {
		if (player.team == Team::BLUE) {
			return player.pos.y > 0;  // BLUE attacks toward +Y
		} else {
			return player.pos.y < 0;  // ORANGE attacks toward -Y
		}
	}

	bool IsInAttackingZone(const Player& player) const {
		if (!IsInOpponentHalf(player)) return false;

		Vec opponentGoal = GetOpponentGoalCenter(player.team);
		float distToGoal = (player.pos - opponentGoal).Length();
		return distToGoal < ATTACKING_ZONE_DIST;
	}

	// Count attacking vs defending players
	// Returns pair: (count in attacking zone, count in defending position)
	std::pair<int, int> CountNumericalAdvantage(const Player& player, const GameState& state) const {
		int attackingCount = 0;
		int defendingCount = 0;

		Vec opponentGoal = GetOpponentGoalCenter(player.team);

		// Count team players in attacking zone
		for (const auto& p : state.players) {
			if (p.team == player.team && p.carId != player.carId) {
				if (IsInAttackingZone(p)) {
					attackingCount++;
				}
			}
		}

		// Count opponent players defending
		for (const auto& p : state.players) {
			if (p.team != player.team) {
				if (!IsInOpponentHalf(p) || (p.pos - opponentGoal).Length() < ATTACKING_ZONE_DIST) {
					defendingCount++;
				}
			}
		}

		// Include self in attacking count if in attacking zone
		if (IsInAttackingZone(player)) {
			attackingCount++;
		}

		return std::make_pair(attackingCount, defendingCount);
	}

	bool HasNumericalAdvantage(const Player& player, const GameState& state) const {
		auto [attacking, defending] = CountNumericalAdvantage(player, state);
		return attacking >= (defending + 1);
	}

	// Get closest opponent player
	const Player* GetClosestOpponent(const Player& player, const GameState& state) const {
		const Player* closest = nullptr;
		float closestDist = FLT_MAX;

		for (const auto& p : state.players) {
			if (p.team != player.team) {
				float dist = (player.pos - p.pos).Length();
				if (dist < closestDist) {
					closestDist = dist;
					closest = &p;
				}
			}
		}

		return closest;
	}

	// Determine primary attacker (closest to ball among attacking teammates)
	AttackerRole DetermineRole(const Player& player, const GameState& state) const {
		if (!HasNumericalAdvantage(player, state)) {
			return AttackerRole::NOT_ATTACKING;
		}

		// Find closest attacking teammate to ball
		float playerDistToBall = (player.pos - state.ball.pos).Length();
		float closestTeammateDist = FLT_MAX;
		bool playerIsClosest = true;

		for (const auto& p : state.players) {
			if (p.team == player.team && p.carId != player.carId && IsInAttackingZone(p)) {
				float dist = (p.pos - state.ball.pos).Length();
				if (dist < closestTeammateDist) {
					closestTeammateDist = dist;
				}
				if (dist < playerDistToBall) {
					playerIsClosest = false;
				}
			}
		}

		// If no attacking teammates, player is primary
		if (closestTeammateDist == FLT_MAX) {
			return AttackerRole::PRIMARY;
		}

		// If player is closest, primary; otherwise support
		return playerIsClosest ? AttackerRole::PRIMARY : AttackerRole::SUPPORT;
	}

	bool IsDemoBumpOpportunity(const Player& player, const GameState& state) const {
		const Player* opponent = GetClosestOpponent(player, state);
		if (!opponent) return false;

		float distToOpponent = (player.pos - opponent->pos).Length();
		return distToOpponent < DEMO_RANGE;
	}

	bool IsInPassingPosition(const Player& player) const {
		Vec opponentGoal = GetOpponentGoalCenter(player.team);
		float distToGoal = (player.pos - opponentGoal).Length();
		return distToGoal >= PASS_POSITION_MIN && distToGoal <= PASS_POSITION_MAX;
	}

	bool IsInReceptionPosition(const Player& player, const Vec& ballPos) const {
		Vec opponentGoal = GetOpponentGoalCenter(player.team);
		float distToGoal = (player.pos - opponentGoal).Length();

		// Check distance to goal
		if (distToGoal < RECEPTION_POSITION_MIN || distToGoal > RECEPTION_POSITION_MAX) {
			return false;
		}

		// Check if positioned for good angle (not directly behind ball)
		Vec toPlayer = (player.pos - ballPos).Normalized();
		Vec toGoal = (opponentGoal - ballPos).Normalized();
		float angleAlignment = toPlayer.Dot(toGoal);

		return angleAlignment > 0.3f;  // Good angle to receive and shoot
	}

public:
	void Reset(const GameState& state) override {
		memory.clear();
		currentStep = 0;
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Skip 1v1 matches
		if (state.players.size() < 4) {
			return 0.0f;
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;

		AttackerRole currentRole = DetermineRole(player, state);
		bool inAdvantage = HasNumericalAdvantage(player, state);

		// ====== PRIMARY ATTACKER: DEMO/BUMP PATH ======
		if (currentRole == AttackerRole::PRIMARY && inAdvantage) {
			// Event reward for successful demo/bump
			const Player* opponent = GetClosestOpponent(player, state);
			if (opponent && IsDemoBumpOpportunity(player, state)) {
				// Check if opponent was just demoed or bumped
				if ((opponent->eventState.demo || opponent->eventState.bumped) && mem.lastDemoTarget == opponent->carId) {
					mem.framesSinceDemoBump = 0;
				}

				mem.lastDemoTarget = opponent->carId;
			}

			// Reward for positioning for demo
			if (IsDemoBumpOpportunity(player, state)) {
				reward += 0.1f;  // Shaping reward for being in demo range
			}

			// Event reward if demo just happened
			if (mem.framesSinceDemoBump == 0) {
				reward += 0.4f;  // Successful demo/bump
			}

			if (mem.framesSinceDemoBump < POST_ACTION_WINDOW) {
				mem.framesSinceDemoBump++;
			}
		}

		// ====== SUPPORT ATTACKER: POST-DEMO SHOT ======
		if (currentRole == AttackerRole::SUPPORT && inAdvantage) {
			// Reward for shooting after teammate demo (within window)
			if (mem.framesSinceDemoBump < POST_ACTION_WINDOW && player.ballTouchedStep) {
				reward += 0.5f;  // Reward for capitalizing on demo
				mem.framesSinceDemoBump = POST_ACTION_WINDOW;  // Reset window
			}
		}

		// ====== PRIMARY ATTACKER: PASSING PATH ======
		if (currentRole == AttackerRole::PRIMARY && inAdvantage && IsInPassingPosition(player)) {
			// Check for high-velocity pass to teammate
			if (player.ballTouchedStep && state.ball.vel.Length() > PASS_QUALITY_THRESHOLD) {
				// Verify pass is toward teammate in attacking zone
				bool validPass = false;
				for (const auto& p : state.players) {
					if (p.team == player.team && p.carId != player.carId && IsInAttackingZone(p)) {
						Vec toTeammate = (p.pos - player.pos).Normalized();
						Vec ballDir = state.ball.vel.Normalized();
						if (ballDir.Dot(toTeammate) > 0.3f) {
							validPass = true;
							break;
						}
					}
				}

				if (validPass) {
					reward += 0.4f;  // Reward for quality pass
					mem.framesSincePassing = 0;
				}
			}
		}

		// ====== SUPPORT ATTACKER: POST-PASS SHOT ======
		if (currentRole == AttackerRole::SUPPORT && inAdvantage && IsInReceptionPosition(player, state.ball.pos)) {
			// Reward for shooting after receiving pass
			if (mem.framesSincePassing < POST_ACTION_WINDOW && player.ballTouchedStep) {
				reward += 0.5f;  // Reward for capitalizing on pass
				mem.framesSincePassing = POST_ACTION_WINDOW;  // Reset window
			}
		}

		// ====== GENERAL AGGRESSION BONUS ======
		if (inAdvantage) {
			// Per-frame bonus for being in attacking zone
			if (IsInAttackingZone(player)) {
				auto [attacking, defending] = CountNumericalAdvantage(player, state);
				float advantageScale = (attacking - defending) / 2.0f;  // Scale by magnitude
				reward += 0.1f * advantageScale;
			}

			// Penalty for retreating during numerical advantage
			if (currentRole != AttackerRole::NOT_ATTACKING) {
				if (!IsInAttackingZone(player) && player.vel.Length() < 500.0f) {
					reward -= 0.05f;  // Light penalty for not pushing advantage
				}
			}
		}

		// Update timers
		if (mem.framesSinceDemoBump < POST_ACTION_WINDOW) {
			mem.framesSinceDemoBump++;
		}
		if (mem.framesSincePassing < POST_ACTION_WINDOW) {
			mem.framesSincePassing++;
		}

		mem.lastRole = currentRole;
		mem.lastFrameHadPossession = player.ballTouchedStep;

		return reward;
	}

	void PreStep(const GameState& state) override {
		currentStep++;
	}
};

// ============================================================================
// DRIBBLE BUMP REWARD
// Incentivizes bumping/demoing opponents while maintaining ground dribbles
// Teaches aggressive ground dribbling tactics toward defenders
// ============================================================================
class DribbleBumpReward : public Reward {
private:
	enum class DribbleApproachType {
		NOT_DRIBBLING,      // Not in a dribble
		DEFENSIVE_DRIBBLE,  // Dribbling away from pressure
		AGGRESSIVE_DRIBBLE  // Dribbling toward opponent
	};

	struct DribbleMemory {
		bool isDribbling = false;                    // Currently in dribble sequence
		int dribbleFrames = 0;                       // Frames in current sequence
		int ballOnCarFrames = 0;                     // Frames ball stayed on car
		DribbleApproachType lastApproachType = DribbleApproachType::NOT_DRIBBLING;

		uint32_t targetOpponentId = UINT32_MAX;     // Which opponent we're targeting
		uint32_t lastDefenderId = UINT32_MAX;       // Last defender (for prioritization)
		float closestApproachDist = FLT_MAX;        // Closest distance during approach

		int framesSinceBumpDemo = 999;              // Post-action window for bump
		bool hasGivenBumpReward = false;            // Only reward once per bump
	};

	std::unordered_map<uint32_t, DribbleMemory> memory;
	int currentStep = 0;

	// Constants
	static constexpr float BUMP_RANGE = 1500.0f;
	static constexpr float APPROACH_TRIGGER = 2000.0f;
	static constexpr float DRIBBLE_SPEED_MIN = 250.0f;
	static constexpr float BALL_ON_CAR_THRESHOLD = 0.8f;
	static constexpr float GOAL_BONUS_RANGE = 3000.0f;
	static constexpr int POST_BUMP_WINDOW = 120;
	static constexpr int MIN_DRIBBLE_TIME = 10;

	Vec GetOwnGoalPos(Team team) const {
		return (team == Team::BLUE)
			? CommonValues::BLUE_GOAL_CENTER
			: CommonValues::ORANGE_GOAL_CENTER;
	}

	Vec GetOpponentGoalPos(Team team) const {
		return (team == Team::BLUE)
			? CommonValues::ORANGE_GOAL_CENTER
			: CommonValues::BLUE_GOAL_CENTER;
	}

	// Check if ball on car using simple height/distance check
	bool IsBallOnCar(const Player& player, const GameState& state) const {
		Vec rel = state.ball.pos - player.pos;
		float height = rel.z;
		float horiz = Vec(rel.x, rel.y, 0).Length();
		return (height > 40.0f && height < 150.0f && horiz < 120.0f);
	}

	// Check if actively dribbling using carrystate
	bool IsDribblingCarry(const Player& player, const GameState& state) const {
		const float kMaxForwardOffset = 170.0f;
		const float kMinForwardOffset = -80.0f;
		const float kMaxLateralOffset = 140.0f;
		const float kMaxDistanceXY = 220.0f;
		const float kMinBallHeightAboveCar = 55.0f;
		const float kMaxBallHeightAboveCar = 220.0f;
		const float kMaxRelativeSpeed = 900.0f;
		const float kMinCarSpeed = 250.0f;

		if (!player.isOnGround) return false;

		Vec diff = state.ball.pos - player.pos;

		float dz = diff.z;
		if (dz < kMinBallHeightAboveCar || dz > kMaxBallHeightAboveCar) return false;

		Vec diffXY = diff; diffXY.z = 0.0f;
		if (diffXY.Length() > kMaxDistanceXY) return false;

		float forwardOffset = player.rotMat.forward.Dot(diff);
		float lateralOffset = player.rotMat.right.Dot(diff);

		if (forwardOffset > kMaxForwardOffset || forwardOffset < kMinForwardOffset) return false;
		if (std::fabs(lateralOffset) > kMaxLateralOffset) return false;

		if (player.vel.Length() < kMinCarSpeed) return false;
		if ((state.ball.vel - player.vel).Length() > kMaxRelativeSpeed) return false;

		return true;
	}

	// Find opponent closest to their own goal (last defender)
	const Player* FindLastDefender(const Player& player, const GameState& state) const {
		const Player* lastDefender = nullptr;
		float closestToGoal = FLT_MAX;

		Vec theirGoal = GetOwnGoalPos(player.team == Team::BLUE ? Team::ORANGE : Team::BLUE);

		for (const auto& p : state.players) {
			if (p.team != player.team) {
				float distToGoal = (p.pos - theirGoal).Length();
				if (distToGoal < closestToGoal) {
					closestToGoal = distToGoal;
					lastDefender = &p;
				}
			}
		}

		return lastDefender;
	}

	// Find closest opponent overall
	const Player* FindClosestOpponent(const Player& player, const GameState& state) const {
		const Player* closest = nullptr;
		float closestDist = FLT_MAX;

		for (const auto& p : state.players) {
			if (p.team != player.team) {
				float dist = (player.pos - p.pos).Length();
				if (dist < closestDist) {
					closestDist = dist;
					closest = &p;
				}
			}
		}

		return closest;
	}

	// Check if dribbling toward a specific opponent
	bool IsApproachingOpponent(const Player& player, const Player* opponent) const {
		if (!opponent) return false;

		Vec toOpponent = (opponent->pos - player.pos).Normalized();
		Vec ballVel = player.vel.Normalized();

		// Check if movement toward opponent
		return ballVel.Dot(toOpponent) > 0.2f;
	}

	// Check if in offensive context (dribble toward goal)
	bool IsOffensiveContext(const Player& player, const GameState& state) const {
		Vec oppGoal = GetOpponentGoalPos(player.team);
		Vec toGoal = (oppGoal - player.pos).Normalized();
		Vec velocity = player.vel.Normalized();

		return velocity.Dot(toGoal) > 0.0f;  // Headed toward opponent goal
	}

	// Calculate dribble quality (percentage of frames ball on car)
	float CalculateDribbleQuality(int ballOnCarFrames, int totalFrames) const {
		if (totalFrames == 0) return 0.0f;
		return static_cast<float>(ballOnCarFrames) / static_cast<float>(totalFrames);
	}

public:
	void Reset(const GameState& state) override {
		memory.clear();
		currentStep = 0;
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Skip 1v1 matches
		if (state.players.size() < 4) {
			return 0.0f;
		}

		// Skip during kickoff
		if ((player.pos).Length() < 4000.0f && state.ball.pos.Length() < 2000.0f) {
			// Ball and player both near center = likely kickoff
			memory[player.carId].isDribbling = false;
			return 0.0f;
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;

		bool isDribbling = IsDribblingCarry(player, state);
		bool ballOnCar = IsBallOnCar(player, state);

		// Update dribble state
		if (isDribbling) {
			mem.isDribbling = true;
			mem.dribbleFrames++;
			if (ballOnCar) {
				mem.ballOnCarFrames++;
			}
		} else {
			mem.isDribbling = false;
			mem.dribbleFrames = 0;
			mem.ballOnCarFrames = 0;
		}

		// Get opponent information
		const Player* lastDefender = FindLastDefender(player, state);
		const Player* closestOpponent = FindClosestOpponent(player, state);

		// ====== DRIBBLING LOGIC ======
		if (isDribbling && mem.dribbleFrames >= MIN_DRIBBLE_TIME) {
			// Determine approach type
			float distToLastDef = lastDefender ? (player.pos - lastDefender->pos).Length() : FLT_MAX;
			float distToClosest = closestOpponent ? (player.pos - closestOpponent->pos).Length() : FLT_MAX;

			bool approachingLastDef = (distToLastDef < APPROACH_TRIGGER);
			bool approachingAny = (distToClosest < APPROACH_TRIGGER);

			// ====== PATH A: LAST DEFENDER TARGETING (HIGHEST PRIORITY) ======
			if (approachingLastDef && lastDefender) {
				mem.targetOpponentId = lastDefender->carId;
				mem.lastApproachType = DribbleApproachType::AGGRESSIVE_DRIBBLE;
				mem.lastDefenderId = lastDefender->carId;

				// Shaping reward for approaching last defender while maintaining dribble
				reward += 0.15f;

				// Event reward for successful bump/demo on last defender
				if (lastDefender->eventState.demo || lastDefender->eventState.bumped) {
					if (mem.lastDefenderId == lastDefender->carId && !mem.hasGivenBumpReward) {
						reward += 0.6f;  // High reward for bumping last defender
						mem.hasGivenBumpReward = true;
						mem.framesSinceBumpDemo = 0;
					}
				}

				// Context bonus: dribble toward goal + bump = big advantage
				if (IsOffensiveContext(player, state)) {
					reward += 0.1f;
				}
			}
			// ====== PATH B: FLEXIBLE APPROACH (BUMPING OR WEAVING) ======
			else if (approachingAny && closestOpponent) {
				mem.targetOpponentId = closestOpponent->carId;
				mem.lastApproachType = DribbleApproachType::AGGRESSIVE_DRIBBLE;

				// Shaping reward for approaching any opponent
				reward += 0.1f;

				// Event reward for bumping/demoing non-primary opponent
				if (closestOpponent->eventState.demo || closestOpponent->eventState.bumped) {
					if (!mem.hasGivenBumpReward) {
						reward += 0.4f;  // Moderate reward for other bumps
						mem.hasGivenBumpReward = true;
						mem.framesSinceBumpDemo = 0;
					}
				}

				// Reward for weaving (maintaining dribble without contact)
				if (distToClosest > 1600.0f && distToClosest < 2000.0f && mem.dribbleFrames > 30) {
					reward += 0.05f;  // Small reward for successful weave
				}
			}
			// ====== PATH C: DEFENSIVE DRIBBLE (ESCAPE) ======
			else {
				mem.lastApproachType = DribbleApproachType::DEFENSIVE_DRIBBLE;
				// Small shaping reward for defensive dribbles
				reward += 0.05f;
			}

			// Quality bonus: Maintaining ball on car during approach
			float dribbleQuality = CalculateDribbleQuality(mem.ballOnCarFrames, mem.dribbleFrames);
			if (dribbleQuality > BALL_ON_CAR_THRESHOLD) {
				reward += 0.2f;
			}

			// Penalty: Dribble breaks during approach
			if (mem.dribbleFrames > 0 && dribbleQuality < 0.5f) {
				reward -= 0.1f;
			}
		}

		// ====== POST-BUMP FOLLOWUP WINDOW ======
		if (mem.framesSinceBumpDemo < POST_BUMP_WINDOW) {
			mem.framesSinceBumpDemo++;
		}

		return reward;
	}

	void PreStep(const GameState& state) override {
		currentStep++;
	}
};

// ============================================================================
// ShotPlacementReward — Defender-Aware Shot Quality
// ============================================================================
// Fires on ball touch when BallPred confirms the shot is heading into the goal
// mouth. Scores the shot by how hard the nearest defender must work to reach
// the landing point: rewards corners, shots behind defenders, and exploiting
// low-boost opponents.
//
// Score per defender = normalize(dist_to_shot_point) x aerial_boost_mult x behind_mult
// Overall = min over all defenders (hardest to beat) x MAX_REWARD
//
// 2026-03-01 | NJH
// ============================================================================
class ShotPlacementReward : public Reward {
private:
    // Goal geometry (ball-radius adjusted — same values as UnifiedAirDribbleReward)
    static constexpr float BALL_R        = 91.25f;
    static constexpr float MOUTH_HALF_W  = 892.755f - BALL_R;   // 801.505 UU
    static constexpr float MOUTH_H_MIN   = BALL_R;               //  91.25  UU
    static constexpr float MOUTH_H_MAX   = 642.775f - BALL_R;   // 551.525 UU

    // Gating
    static constexpr float MIN_BALL_SPEED = 500.0f;              // UU/s minimum to count as a shot

    // Reward scaling
    static constexpr float MAX_REWARD    = 3.0f;
    static constexpr float MAX_DIST      = 4000.0f;  // defender beyond this -> normalizedDist = 1.0

    // Aerial difficulty parameters
    static constexpr float AERIAL_Z      = 150.0f;   // shot Z above which boost matters
    static constexpr float LOW_BOOST     = 30.0f;    // defender boost below this = aerial penalty
    static constexpr float AERIAL_MULT   = 0.5f;     // extra multiplier headroom for aerial+low-boost

    // Behind-defender bonus
    static constexpr float BEHIND_BONUS  = 0.5f;     // extra multiplier headroom for behind-shot

    // Result of velocity-ray goal-crossing search
    struct GoalCrossing {
        float x = 0.f, z = 0.f, t = 0.f;
        bool  valid = false;
    };

    // Extrapolate where the ball will cross the opponent's goal plane using current
    // velocity + gravity (~650 UU/s²). Fires for any on-target shot regardless of
    // distance — no BallPred 4-second window limitation.
    GoalCrossing FindGoalCrossing(const Player& player, const GameState& state) const {
        GoalCrossing c;
        float goalY = (player.team == Team::BLUE)
                       ?  CommonValues::BACK_WALL_Y
                       : -CommonValues::BACK_WALL_Y;

        Vec ballPos = state.ball.pos;
        Vec ballVel = state.ball.vel;

        // Ball must be moving toward the opponent's goal
        bool movingToGoal = (player.team == Team::BLUE) ? (ballVel.y > 50.f) : (ballVel.y < -50.f);
        if (!movingToGoal) return c;

        // Time to reach goal plane
        float tToGoal = (goalY - ballPos.y) / ballVel.y;
        if (tToGoal < 0.f) return c;

        // Extrapolate x,z with gravity correction (~650 UU/s² in RocketSim)
        float xAtGoal = ballPos.x + ballVel.x * tToGoal;
        float zAtGoal = ballPos.z + ballVel.z * tToGoal - 0.5f * 650.f * tToGoal * tToGoal;

        if (fabsf(xAtGoal) < MOUTH_HALF_W &&
            zAtGoal        > MOUTH_H_MIN  &&
            zAtGoal        < MOUTH_H_MAX) {
            c = { xAtGoal, zAtGoal, tToGoal, true };
        }
        return c;
    }

public:
    float GetReward(const Player& player,
                    const GameState& state,
                    bool isFinal) override
    {
        if (!player.ballTouchedStep)                  return 0.0f;
        if (!state.prev)                              return 0.0f;
        if (state.ball.vel.Length() < MIN_BALL_SPEED) return 0.0f;

        // Confirm shot is aimed at the goal mouth via velocity ray + gravity
        GoalCrossing c = FindGoalCrossing(player, state);
        if (!c.valid) return 0.0f;

        float goalY = (player.team == Team::BLUE)
                       ?  CommonValues::BACK_WALL_Y
                       : -CommonValues::BACK_WALL_Y;
        Vec shotPoint(c.x, goalY, c.z);

        float worstScore = 1.0f;  // 1 = no defender can save it, 0 = guaranteed saved
        bool  hasOpp     = false;

        for (const auto& opp : state.players) {
            if (opp.team == player.team) continue;
            hasOpp = true;

            // Distance: how far must the defender travel to reach the shot?
            float dist     = (opp.pos - shotPoint).Length();
            float normDist = RS_CLAMP(dist / MAX_DIST, 0.0f, 1.0f);

            // Aerial difficulty: low-boost defenders can't reach high shots
            float boostMult = 1.0f;
            if (c.z > AERIAL_Z && opp.boost < LOW_BOOST) {
                float boostFrac = opp.boost / LOW_BOOST;               // 0 (empty) -> 1 (= LOW_BOOST)
                boostMult = 1.0f + (1.0f - boostFrac) * AERIAL_MULT;  // 1.0x -> 1.5x
            }

            // Behind bonus: shot lands behind the defender (they must turn)
            Vec   toShot     = (shotPoint - opp.pos).Normalized();
            float behindDot  = -opp.rotMat.forward.Dot(toShot);       // +1 = shot directly behind them
            float behindMult = 1.0f + RS_CLAMP(behindDot, 0.0f, 1.0f) * BEHIND_BONUS;

            // Combined difficulty for this defender (clamped to [0,1])
            float defScore = RS_CLAMP(normDist * boostMult * behindMult, 0.0f, 1.0f);

            // Most dangerous defender limits the shot quality (take min)
            worstScore = RS_MIN(worstScore, defScore);
        }

        if (!hasOpp) return MAX_REWARD;  // No opponents -> free goal

        return worstScore * MAX_REWARD;
    }
};

} // namespace RLGC
