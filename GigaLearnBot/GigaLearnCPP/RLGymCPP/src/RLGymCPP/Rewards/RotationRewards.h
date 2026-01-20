#pragma once
#include "../Framework.h"

namespace RLGC {

// =========================================================================
// PPO-FRIENDLY ROTATION REWARDS - PROGRESSIVE LEARNING
// =========================================================================
//
// DESIGN PHILOSOPHY:
// - Short feedback loops (< 2 seconds for most rewards)
// - Event-based rewards for milestone achievements
// - Continuous shaping rewards for gradual improvement
// - Progressive difficulty scaling from Bronze → SSL
//
// LEARNING PROGRESSION:
// Bronze-Gold:     Basic spacing, don't triple commit
// Platinum:        Recognize when to retreat after touch
// Diamond:         Basic back-post concepts, simple role awareness
// Champion:        Proper rotation cycles, adaptive positioning
// GC-SSL:          Advanced flow, teammate prediction, speed maintenance
//
// =========================================================================

// Phase-specific configuration
enum class RotationPhase {
	BRONZE_SILVER,    // Phase 1-2: Basic spacing only
	GOLD,             // Phase 3: Introduce retreat after touch
	PLATINUM,         // Phase 4: Back-post awareness
	DIAMOND,          // Phase 5: Role cycling basics
	CHAMPION_PLUS     // Phase 6+: Full rotation system
};

struct RotationConfig {
	// Spacing thresholds (progressive)
	float minTeammateSpacing;
	float maxTeammateSpacing;
	float clusterPenaltyWeight;

	// Back-post rotation
	float backPostProximity;
	float rotationEventReward;
	float rotationShapingWeight;

	// Role cycling
	float minRoleTime;
	float maxRoleTime;
	bool enableRoleCycling;

	// Momentum
	float minRotationSpeed;
	bool enableMomentumRewards;

	// Adaptive behavior
	bool enableAdaptiveRotation;

	static RotationConfig ForPhase(RotationPhase phase) {
		RotationConfig cfg;

		switch (phase) {
		case RotationPhase::BRONZE_SILVER:
			// Focus: Stop triple-committing, basic spacing
			cfg.minTeammateSpacing = 1200.0f;      // Very lenient - just don't be ON teammates
			cfg.maxTeammateSpacing = 6000.0f;      // Very lenient - don't worry about being too far
			cfg.clusterPenaltyWeight = 0.5f;       // Moderate penalty for clustering
			cfg.backPostProximity = 0.0f;          // Not enabled yet
			cfg.rotationEventReward = 0.0f;
			cfg.rotationShapingWeight = 0.0f;
			cfg.minRoleTime = 0.0f;
			cfg.maxRoleTime = 999.0f;
			cfg.enableRoleCycling = false;
			cfg.minRotationSpeed = 0.0f;
			cfg.enableMomentumRewards = false;
			cfg.enableAdaptiveRotation = false;
			break;

		case RotationPhase::GOLD:
			// Focus: Retreat after touching ball, better spacing
			cfg.minTeammateSpacing = 1500.0f;      // Slightly stricter spacing
			cfg.maxTeammateSpacing = 5500.0f;
			cfg.clusterPenaltyWeight = 0.8f;       // Higher penalty for clustering
			cfg.backPostProximity = 2000.0f;       // Very lenient back-post (just retreat somewhere)
			cfg.rotationEventReward = 0.3f;        // Small event reward for retreating
			cfg.rotationShapingWeight = 0.1f;      // Minimal shaping
			cfg.minRoleTime = 0.0f;
			cfg.maxRoleTime = 999.0f;
			cfg.enableRoleCycling = false;
			cfg.minRotationSpeed = 0.0f;
			cfg.enableMomentumRewards = false;
			cfg.enableAdaptiveRotation = false;
			break;

		case RotationPhase::PLATINUM:
			// Focus: Proper back-post rotation, role awareness begins
			cfg.minTeammateSpacing = 1800.0f;      // Good spacing
			cfg.maxTeammateSpacing = 5000.0f;
			cfg.clusterPenaltyWeight = 1.2f;       // Strong clustering penalty
			cfg.backPostProximity = 1500.0f;       // Stricter back-post
			cfg.rotationEventReward = 0.5f;        // Better event reward
			cfg.rotationShapingWeight = 0.3f;      // More shaping
			cfg.minRoleTime = 0.5f;                // Start role awareness (very short)
			cfg.maxRoleTime = 12.0f;               // Lenient max time
			cfg.enableRoleCycling = true;          // Enable basic role cycling
			cfg.minRotationSpeed = 600.0f;         // Lenient speed requirement
			cfg.enableMomentumRewards = true;
			cfg.enableAdaptiveRotation = false;
			break;

		case RotationPhase::DIAMOND:
			// Focus: Proper role cycling, faster rotations
			cfg.minTeammateSpacing = 2000.0f;      // Ideal spacing
			cfg.maxTeammateSpacing = 4500.0f;
			cfg.clusterPenaltyWeight = 1.5f;
			cfg.backPostProximity = 1200.0f;       // Precise back-post
			cfg.rotationEventReward = 0.8f;        // Strong event reward
			cfg.rotationShapingWeight = 0.5f;
			cfg.minRoleTime = 1.0f;                // Proper role time
			cfg.maxRoleTime = 10.0f;
			cfg.enableRoleCycling = true;
			cfg.minRotationSpeed = 800.0f;         // Good speed
			cfg.enableMomentumRewards = true;
			cfg.enableAdaptiveRotation = true;     // Enable adaptive behavior
			break;

		case RotationPhase::CHAMPION_PLUS:
			// Focus: Perfect rotation, high-speed flow
			cfg.minTeammateSpacing = 2200.0f;      // Strict spacing
			cfg.maxTeammateSpacing = 4200.0f;
			cfg.clusterPenaltyWeight = 2.0f;
			cfg.backPostProximity = 1000.0f;       // Very precise
			cfg.rotationEventReward = 1.0f;        // Maximum event reward
			cfg.rotationShapingWeight = 0.7f;
			cfg.minRoleTime = 1.2f;
			cfg.maxRoleTime = 8.0f;                // Strict max time
			cfg.enableRoleCycling = true;
			cfg.minRotationSpeed = 1000.0f;        // High speed
			cfg.enableMomentumRewards = true;
			cfg.enableAdaptiveRotation = true;
			break;
		}

		return cfg;
	}
};

// =========================================================================
// IMMEDIATE SPACING REWARD (PPO-FRIENDLY)
// =========================================================================
// Rewards/punishes INSTANTLY based on current spacing
// No memory, no delay - pure state-based reward
//
// Bronze-Gold: Just don't cluster
// Platinum+: Maintain optimal triangle spacing

class InstantSpacingReward : public Reward {
private:
	RotationConfig config;

public:
	InstantSpacingReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		float reward = 0.0f;
		int teammateCount = 0;

		for (const auto& p : state.players) {
			if (p.team == player.team && p.carId != player.carId) {
				float dist = (player.pos - p.pos).Length();

				// INSTANT PENALTY for being too close
				if (dist < config.minTeammateSpacing) {
					float ratio = dist / config.minTeammateSpacing;
					reward -= config.clusterPenaltyWeight * (1.0f - ratio);
				}
				// INSTANT PENALTY for being too far (only in advanced phases)
				else if (dist > config.maxTeammateSpacing && config.maxTeammateSpacing < 5500.0f) {
					float excessDist = dist - config.maxTeammateSpacing;
					reward -= 0.3f * RS_CLAMP(excessDist / 2000.0f, 0.0f, 1.0f);
				}
				// INSTANT REWARD for good spacing
				else {
					reward += 0.2f;
				}

				teammateCount++;
			}
		}

		return teammateCount > 0 ? reward / teammateCount : 0.0f;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// RETREAT AFTER TOUCH REWARD (PPO-FRIENDLY)
// =========================================================================
// SHORT-TERM MEMORY: Remembers touch for 120 steps (1 second) max
//
// Event reward: +0.5 when you start retreating after touch (INSTANT)
// Shaping reward: Continuous reward for retreating (< 1s feedback)
//
// Gold: Learn to retreat at all
// Platinum+: Retreat to back-post specifically

class RetreatAfterTouchReward : public Reward {
private:
	struct TouchMemory {
		bool justTouched = false;
		int touchStepCount = 0;
		Vec touchPos = Vec(0, 0, 0);
		bool hasGivenEventReward = false;
		bool isRetreating = false;
	};

	std::unordered_map<uint32_t, TouchMemory> memory;
	RotationConfig config;
	int currentStep = 0;

	// SHORT feedback window - only remember touch for 120 steps (~1 second at 120 ticks/sec)
	static constexpr int MEMORY_STEPS = 120;

	Vec GetBackPost(Team team, const Vec& ballPos) const {
		float goalY = (team == Team::BLUE) ?
			-CommonValues::GOAL_LINE_Y : CommonValues::GOAL_LINE_Y;

		// Back post is the post furthest from ball's X position
		float backPostX = (ballPos.x > 0) ?
			-CommonValues::GOAL_WIDTH_FROM_CENTER : CommonValues::GOAL_WIDTH_FROM_CENTER;

		return Vec(backPostX, goalY, 0.0f);
	}

	bool IsRetreating(const Player& player, const Vec& touchPos) const {
		// Are we moving away from where we touched?
		Vec awayFromTouch = (player.pos - touchPos).Normalized();
		Vec velocity = player.vel.Normalized();

		return velocity.Dot(awayFromTouch) > 0.3f;  // Moving away at reasonable angle
	}

public:
	RetreatAfterTouchReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {
		memory.clear();
		currentStep = 0;
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (config.backPostProximity < 1.0f) {
			return 0.0f;  // Not enabled in this phase
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;

		// Detect NEW touch
		if (player.ballTouchedStep && !mem.justTouched) {
			mem.justTouched = true;
			mem.touchStepCount = currentStep;
			mem.touchPos = player.pos;
			mem.hasGivenEventReward = false;
			mem.isRetreating = false;
		}

		// Check if memory has expired (PPO-FRIENDLY: short window)
		if (mem.justTouched && (currentStep - mem.touchStepCount) > MEMORY_STEPS) {
			mem.justTouched = false;
			return 0.0f;
		}

		// Process retreat if we recently touched
		if (mem.justTouched) {
			bool retreating = IsRetreating(player, mem.touchPos);

			// EVENT REWARD: First frame of retreating (INSTANT feedback)
			if (retreating && !mem.isRetreating && !mem.hasGivenEventReward) {
				reward += config.rotationEventReward;
				mem.hasGivenEventReward = true;
			}

			mem.isRetreating = retreating;

			// SHAPING REWARD: Continuous reward for being near back-post
			if (retreating) {
				Vec backPost = GetBackPost(player.team, state.ball.pos);
				float distToBackPost = (player.pos - backPost).Length();

				// Proximity reward (immediate feedback)
				float proximityRatio = 1.0f - RS_CLAMP(distToBackPost / config.backPostProximity, 0.0f, 1.0f);
				reward += proximityRatio * config.rotationShapingWeight;
			}
		}

		return reward;
	}

	void PreStep(const GameState& state) override {
		currentStep++;
	}
};

// =========================================================================
// ROLE AWARENESS REWARD (PPO-FRIENDLY)
// =========================================================================
// SHORT-TERM role tracking with INSTANT rewards
//
// Event reward: +0.3 for successful role transition (INSTANT)
// Penalty: -0.2 per step for staying too long in role (INSTANT feedback)
//
// Platinum: Basic role awareness (very short times)
// Diamond+: Proper role cycling

class RoleAwarenessReward : public Reward {
private:
	enum class SimpleRole {
		CLOSEST,      // Closest to ball
		MIDDLE,       // Middle distance
		FURTHEST      // Furthest from ball
	};

	struct RoleMemory {
		SimpleRole lastRole = SimpleRole::FURTHEST;
		float timeInRole = 0.0f;
		bool justTransitioned = false;
	};

	std::unordered_map<uint32_t, RoleMemory> memory;
	RotationConfig config;

	SimpleRole DetermineRole(const Player& player, const GameState& state) const {
		float distToBall = (player.pos - state.ball.pos).Length();

		std::vector<float> teammateDists;
		for (const auto& p : state.players) {
			if (p.team == player.team && p.carId != player.carId) {
				teammateDists.push_back((p.pos - state.ball.pos).Length());
			}
		}

		if (teammateDists.empty()) {
			return SimpleRole::CLOSEST;
		}

		std::sort(teammateDists.begin(), teammateDists.end());

		// Determine role based on relative distance
		if (distToBall < teammateDists[0]) {
			return SimpleRole::CLOSEST;
		}
		else if (teammateDists.size() >= 2 && distToBall > teammateDists[1]) {
			return SimpleRole::FURTHEST;
		}
		else {
			return SimpleRole::MIDDLE;
		}
	}

public:
	RoleAwarenessReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {
		memory.clear();
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!config.enableRoleCycling) {
			return 0.0f;
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;

		SimpleRole currentRole = DetermineRole(player, state);

		// Detect role change
		if (currentRole != mem.lastRole) {
			if (mem.timeInRole >= config.minRoleTime) {
				// EVENT REWARD: Successful transition (INSTANT)
				reward += 0.3f;
				mem.justTransitioned = true;
			}

			mem.lastRole = currentRole;
			mem.timeInRole = 0.0f;
		}
		else {
			mem.timeInRole += CommonValues::TICK_TIME;
			mem.justTransitioned = false;
		}

		// INSTANT PENALTY for staying too long (per-step feedback)
		if (mem.timeInRole > config.maxRoleTime) {
			reward -= 0.05f;  // Small per-step penalty
		}

		return reward;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// MOMENTUM REWARD (PPO-FRIENDLY)
// =========================================================================
// INSTANT feedback based on current speed
// No memory needed - pure state-based
//
// Platinum+: Maintain speed during rotation

class MomentumReward : public Reward {
private:
	RotationConfig config;

	bool IsMovingTowardsOwnGoal(const Player& player) const {
		Vec toGoal = ((player.team == Team::BLUE ?
			CommonValues::BLUE_GOAL_CENTER :
			CommonValues::ORANGE_GOAL_CENTER) - player.pos).Normalized();

		Vec velocity = player.vel.Normalized();

		return velocity.Dot(toGoal) > 0.5f;
	}

public:
	MomentumReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		if (!config.enableMomentumRewards) {
			return 0.0f;
		}

		float speed = player.vel.Length();
		float reward = 0.0f;

		// Only reward momentum when rotating back
		if (IsMovingTowardsOwnGoal(player)) {
			// INSTANT REWARD for maintaining speed
			if (speed > config.minRotationSpeed) {
				float speedRatio = RS_CLAMP(speed / CommonValues::SUPERSONIC_THRESHOLD, 0.0f, 1.0f);
				reward += 0.15f * speedRatio;
			}
			// INSTANT PENALTY for being too slow
			else {
				reward -= 0.1f;
			}
		}

		return reward;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// PROGRESSIVE ROTATION REWARD (MASTER CLASS)
// =========================================================================
// Combines all rewards with phase-appropriate weights
//
// Usage:
//   Bronze-Silver:  WeightedReward(new ProgressiveRotationReward(RotationPhase::BRONZE_SILVER), 5.0f)
//   Gold:           WeightedReward(new ProgressiveRotationReward(RotationPhase::GOLD), 8.0f)
//   Platinum:       WeightedReward(new ProgressiveRotationReward(RotationPhase::PLATINUM), 12.0f)
//   Diamond:        WeightedReward(new ProgressiveRotationReward(RotationPhase::DIAMOND), 15.0f)
//   Champion+:      WeightedReward(new ProgressiveRotationReward(RotationPhase::CHAMPION_PLUS), 20.0f)

class ProgressiveRotationReward : public Reward {
private:
	InstantSpacingReward spacingReward;
	RetreatAfterTouchReward retreatReward;
	RoleAwarenessReward roleReward;
	MomentumReward momentumReward;

	RotationPhase phase;

public:
	ProgressiveRotationReward(RotationPhase phase_)
		: phase(phase_)
		, spacingReward(phase_)
		, retreatReward(phase_)
		, roleReward(phase_)
		, momentumReward(phase_)
	{
	}

	void Reset(const GameState& state) override {
		spacingReward.Reset(state);
		retreatReward.Reset(state);
		roleReward.Reset(state);
		momentumReward.Reset(state);
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		float total = 0.0f;

		// Phase-specific weighting
		switch (phase) {
		case RotationPhase::BRONZE_SILVER:
			// Focus: Just spacing
			total += spacingReward.GetReward(player, state, isFinal) * 1.0f;
			break;

		case RotationPhase::GOLD:
			// Focus: Spacing + basic retreat
			total += spacingReward.GetReward(player, state, isFinal) * 0.7f;
			total += retreatReward.GetReward(player, state, isFinal) * 1.0f;
			break;

		case RotationPhase::PLATINUM:
			// Focus: All basics, introduce roles
			total += spacingReward.GetReward(player, state, isFinal) * 0.6f;
			total += retreatReward.GetReward(player, state, isFinal) * 1.0f;
			total += roleReward.GetReward(player, state, isFinal) * 0.5f;
			total += momentumReward.GetReward(player, state, isFinal) * 0.3f;
			break;

		case RotationPhase::DIAMOND:
			// Focus: Balanced rotation system
			total += spacingReward.GetReward(player, state, isFinal) * 0.5f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.8f;
			total += roleReward.GetReward(player, state, isFinal) * 0.8f;
			total += momentumReward.GetReward(player, state, isFinal) * 0.5f;
			break;

		case RotationPhase::CHAMPION_PLUS:
			// Focus: Perfect execution
			total += spacingReward.GetReward(player, state, isFinal) * 0.4f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.6f;
			total += roleReward.GetReward(player, state, isFinal) * 1.0f;
			total += momentumReward.GetReward(player, state, isFinal) * 0.7f;
			break;
		}

		return total;
	}

	void PreStep(const GameState& state) override {
		spacingReward.PreStep(state);
		retreatReward.PreStep(state);
		roleReward.PreStep(state);
		momentumReward.PreStep(state);
	}
};

} // namespace RLGC
