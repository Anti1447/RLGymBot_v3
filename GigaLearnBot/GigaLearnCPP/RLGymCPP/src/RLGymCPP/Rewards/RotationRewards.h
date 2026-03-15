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

	// Adaptive behavior
	bool enableAdaptiveRotation;

	// Phase C: Second-man offensive support (NEW)
	bool enableSecondManAdvance = true;
	bool enablePassingLaneSetup = true;
	bool enableSupportPressure = true;

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
			cfg.enableAdaptiveRotation = true;     // Enable adaptive behavior
			break;

		case RotationPhase::CHAMPION_PLUS:
			// Focus: Perfect rotation, high-speed flow
			cfg.minTeammateSpacing = 1800.0f;      // Strict spacing
			cfg.maxTeammateSpacing = 4000.0f;
			cfg.clusterPenaltyWeight = 2.0f;
			cfg.backPostProximity = 1200.0f;       // Very precise
			cfg.rotationEventReward = 1.2f;        // Maximum event reward
			cfg.rotationShapingWeight = 0.9f;
			cfg.minRoleTime = 1.0f;
			cfg.maxRoleTime = 12.0f;                // Strict max time
			cfg.enableRoleCycling = true;
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
		Vec velocity = player.vel;

		// Safety: Check if player is moving at all before normalizing
		if (velocity.Length() < 50.0f) return false;  // Stationary players aren't retreating

		velocity = velocity.Normalized();
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

			// SHAPING REWARD: Continuous reward for being near back-post (when ball is in defensive half)
			if (retreating) {
				// Back-post logic only applies when ball is in own defensive half
				float ballGoalY = (player.team == Team::BLUE) ?
					state.ball.pos.y : -state.ball.pos.y;  // Flip for ORANGE perspective

				if (ballGoalY < 0) {
					// Ball is in own defensive half - reward positioning near back-post
					Vec backPost = GetBackPost(player.team, state.ball.pos);
					float distToBackPost = (player.pos - backPost).Length();

					// Proximity reward (immediate feedback)
					float proximityRatio = 1.0f - RS_CLAMP(distToBackPost / config.backPostProximity, 0.0f, 1.0f);
					reward += proximityRatio * config.rotationShapingWeight;
				} else {
					// Ball in offensive half - reward general retreat away from ball
					float distFromBall = (player.pos - state.ball.pos).Length();
					float retreatRatio = RS_CLAMP(distFromBall / (config.backPostProximity * 1.5f), 0.0f, 1.0f);
					reward += (0.5f * retreatRatio) * config.rotationShapingWeight;
				}
			}
		}

		return reward;
	}

	void PreStep(const GameState& state) override {
		currentStep++;
	}
};

// =========================================================================
// PHASE C HELPER FUNCTIONS (NEW)
// =========================================================================
// Used by Phase C second-man support rewards to identify roles and relationships

// Named constants for Phase C spacing and positioning logic
namespace PhaseC {
	// Distance thresholds for second-man support
	static constexpr float FIRST_MAN_PRESSURED_DIST = 1500.0f;   // When first-man is "committed"
	static constexpr float SECOND_MAN_SUPPORT_RANGE_MIN = 1500.0f; // Minimum distance for support
	static constexpr float SECOND_MAN_SUPPORT_RANGE_MAX = 3500.0f; // Maximum distance for support
	static constexpr float GOAL_POST_BACK_DISTANCE = 2500.0f;    // "Back post" optimal position
	static constexpr float OPPONENT_CHALLENGE_DIST = 2000.0f;   // When opponent is challenging
}

// Role type enum - single source of truth for role determination
enum class RoleType {
	CLOSEST,      // Closest to ball
	MIDDLE,       // Middle distance
	FURTHEST      // Furthest from ball
};

// Unified role detection function - eliminates code duplication
// Computes velocity-adjusted distances and determines player's role relative to teammates
inline RoleType DetermineRoleCommon(const Player& player, const GameState& state) {
	float distToBall = (player.pos - state.ball.pos).Length();
	Vec ballVel = state.ball.vel;
	Vec toPlayer = (player.pos - state.ball.pos).Normalized();

	float velocityAlignment = 0.0f;
	if (ballVel.Length() > 100.0f) {
		velocityAlignment = ballVel.Normalized().Dot(toPlayer);
	}

	// Adjust distance based on velocity alignment
	// Players ahead of the ball's trajectory get lower adjusted distance (better positioned)
	// Players behind the ball's trajectory get higher adjusted distance (worse positioned)
	// Scalar 0.25 limits adjustment to 6% per role change, preventing overcorrection
	float adjustedDist = distToBall * (1.0f - velocityAlignment * 0.25f);

	// Collect adjusted teammate distances
	std::vector<float> adjustedTeammateDists;
	for (const auto& p : state.players) {
		if (p.team == player.team && p.carId != player.carId) {
			float dist = (p.pos - state.ball.pos).Length();
			Vec toTeammate = (p.pos - state.ball.pos).Normalized();
			float tmateAlignment = 0.0f;
			if (ballVel.Length() > 100.0f) {
				tmateAlignment = ballVel.Normalized().Dot(toTeammate);
			}
			float adjustedTmateDist = dist * (1.0f - tmateAlignment * 0.25f);
			adjustedTeammateDists.push_back(adjustedTmateDist);
		}
	}

	if (adjustedTeammateDists.empty()) {
		return RoleType::CLOSEST;
	}

	std::sort(adjustedTeammateDists.begin(), adjustedTeammateDists.end());

	// Determine role based on adjusted distances (velocity-aware)
	if (adjustedDist < adjustedTeammateDists[0]) {
		return RoleType::CLOSEST;
	}
	else if (adjustedTeammateDists.size() >= 2 && adjustedDist > adjustedTeammateDists[1]) {
		return RoleType::FURTHEST;
	}
	else {
		return RoleType::MIDDLE;
	}
}

inline bool IsSecondManRole(const Player& player, const GameState& state) {
	// Identify if player is in MIDDLE role (second-man)
	// Uses unified DetermineRoleCommon to ensure consistency with RoleAwarenessReward
	return DetermineRoleCommon(player, state) == RoleType::MIDDLE;
}

inline Player* GetFirstManTeammate(const Player& player, const GameState& state) {
	// Find closest teammate to ball (the first-man)
	float closestDistToBall = FLT_MAX;
	Player* firstMan = nullptr;

	for (const auto& p : state.players) {
		if (p.carId == player.carId) {
			continue;  // Skip self
		}

		if (p.team != player.team) {
			continue;  // Only teammates
		}

		float distToBall = (p.pos - state.ball.pos).Length();
		if (distToBall < closestDistToBall) {
			closestDistToBall = distToBall;
			firstMan = const_cast<Player*>(&p);
		}
	}

	return firstMan;
}

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

		// NEW: Event tracking for hybrid (time + event) role transitions
		bool lastFrameHadPossession = false;  // Did player possess ball last frame?
		// TODO: lastFrameVel was used for demo detection, disabled for now (too noisy)
	};

	std::unordered_map<uint32_t, RoleMemory> memory;
	RotationConfig config;

	// NEW: Check for events that should trigger immediate role transition
	bool CheckEventTriggers(const Player& player, const GameState& state, RoleMemory& mem) const {
		// Event 1: Ball possession lost (player touched last frame, but doesn't now)
		bool currentPossession = player.ballTouchedStep;
		if (mem.lastFrameHadPossession && !currentPossession) {
			// Just lost possession - should rotate out
			return true;
		}

		// TODO: Event 2 (demo detection via velocity spike) was too noisy, disabled

		return false;  // No events triggered
	}

	SimpleRole DetermineRole(const Player& player, const GameState& state) const {
		// Use unified role detection helper to ensure consistency across all rewards
		RoleType roleType = DetermineRoleCommon(player, state);

		// Convert RoleType to SimpleRole enum (same role, different type names)
		if (roleType == RoleType::CLOSEST) return SimpleRole::CLOSEST;
		if (roleType == RoleType::FURTHEST) return SimpleRole::FURTHEST;
		return SimpleRole::MIDDLE;
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

		// NEW: Check for event-based role transition triggers
		bool eventTriggered = CheckEventTriggers(player, state, mem);
		if (eventTriggered) {
			// Force immediate role transition capability
			mem.timeInRole = 0.0f;
		}

		SimpleRole currentRole = DetermineRole(player, state);

		// Detect role change
		if (currentRole != mem.lastRole) {
			// Allow transition if: minRoleTime satisfied OR event triggered
			if (mem.timeInRole >= config.minRoleTime || eventTriggered) {
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

		// Track possession for next frame's event detection
		mem.lastFrameHadPossession = player.ballTouchedStep;

		return reward;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// PHASE C: SECOND-MAN ADVANCE REWARD (NEW)
// =========================================================================
// Reward second-man for positioning closer to ball when first-man is pressured.
// Teaches the second-man to provide immediate support during challenges.
class SecondManAdvanceReward : public Reward {
public:
	void Reset(const GameState& state) override {
		// No state tracking required - stateless reward
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Only activates for second-man (MIDDLE role players)
		if (!IsSecondManRole(player, state)) {
			return 0;
		}

		Vec ballPos = state.ball.pos;
		Player* firstMan = GetFirstManTeammate(player, state);
		if (!firstMan) {
			return 0;
		}

		// Calculate distances
		float firstManDistToBall = (firstMan->pos - ballPos).Length();
		float secondManDistToBall = (player.pos - ballPos).Length();

		// Zone 1: First-man is pressured (opponent nearby)
		float closestOpponentDist = PositionAnalysis::ClosestOpponentDistance(player, ballPos, state.players);
		bool firstManPressured = (firstManDistToBall < PhaseC::FIRST_MAN_PRESSURED_DIST);

		// Reward advance positioning when first-man is under pressure
		if (firstManPressured && closestOpponentDist < PhaseC::OPPONENT_CHALLENGE_DIST) {
			// Reward being closer than GOAL_POST_BACK_DISTANCE from ball (positioned to support)
			if (secondManDistToBall < PhaseC::GOAL_POST_BACK_DISTANCE) {
				// Progressive reward: scaling by how close (but not too close)
				float baseReward = 0.3f;
				float proximityBonus = RS_MAX(0.0f, (3000.0f - secondManDistToBall) / 3000.0f) * 0.2f;
				return baseReward + proximityBonus;  // 0.3-0.5 scale
			}
		}

		return 0;
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// PHASE C: PASSING LANE SETUP REWARD (NEW)
// =========================================================================
// Reward second-man for positioning to receive passes from first-man.
// Teaches the second-man to create passing angles and maintain offensive options.
class PassingLaneSetupReward : public Reward {
public:
	void Reset(const GameState& state) override {
		// No state tracking required - stateless reward
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Only activates for second-man
		if (!IsSecondManRole(player, state)) {
			return 0;
		}

		// Only during offensive phase (ball in opponent half)
		// Check if player is attacking (in opponent goal area)
		if (!PositionAnalysis::InOpponentGoalArea(player)) {
			return 0;
		}

		Player* firstMan = GetFirstManTeammate(player, state);
		if (!firstMan) {
			return 0;
		}

		Vec ballPos = state.ball.pos;
		Vec firstManPos = firstMan->pos;
		Vec secondManPos = player.pos;

		// Calculate if second-man is in passable position
		// Passable = to the side (±45°) or behind (> 120°) from first-man's forward direction
		Vec firstManForward = firstMan->rotMat.forward;
		Vec toSecondMan = (secondManPos - firstManPos).Normalized();

		float angleAlignment = firstManForward.Dot(toSecondMan);
		bool isSideOrBehind = (angleAlignment < 0.707f);  // cos(45°) = 0.707

		if (!isSideOrBehind) {
			return 0;  // Too far in front, not a passable position
		}

		// Distance validation: SECOND_MAN_SUPPORT_RANGE from first-man
		float distToFirstMan = (secondManPos - firstManPos).Length();
		if (distToFirstMan < PhaseC::SECOND_MAN_SUPPORT_RANGE_MIN || distToFirstMan > PhaseC::SECOND_MAN_SUPPORT_RANGE_MAX) {
			return 0;  // Out of passing range
		}

		// Reward for maintaining passable distance and angle
		// Peak reward when at GOAL_POST_BACK_DISTANCE (optimal passing distance)
		float distanceFactor = 1.0f - (fabs(distToFirstMan - PhaseC::GOAL_POST_BACK_DISTANCE) / PhaseC::GOAL_POST_BACK_DISTANCE);
		float angleFactor = 1.0f - angleAlignment;  // Higher when more sideways/behind

		return RS_CLAMP(0.2f * distanceFactor * angleFactor, 0.0f, 0.4f);
	}

	void PreStep(const GameState& state) override {}
};

// =========================================================================
// PHASE C: SUPPORT PRESSURE REWARD (NEW)
// =========================================================================
// Reward second-man for staying close during first-man challenge.
// Teaches the second-man to provide support pressure and recovery options.
class SupportPressureReward : public Reward {
public:
	void Reset(const GameState& state) override {
		// No state tracking required - stateless reward
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Only activates for second-man
		if (!IsSecondManRole(player, state)) {
			return 0;
		}

		Player* firstMan = GetFirstManTeammate(player, state);
		if (!firstMan) {
			return 0;
		}

		// Check if first-man is actively challenging (high velocity + near ball)
		Vec ballPos = state.ball.pos;
		float firstManDistToBall = (firstMan->pos - ballPos).Length();
		float firstManVelocity = firstMan->vel.Length();

		bool isActiveChallenging = (firstManDistToBall < 1500.0f && firstManVelocity > 1000.0f);

		if (!isActiveChallenging) {
			return 0;  // First-man not in active challenge
		}

		// Reward second-man for staying close
		float distToFirstMan = (player.pos - firstMan->pos).Length();

		if (distToFirstMan < PhaseC::SECOND_MAN_SUPPORT_RANGE_MIN) {
			return 0;  // Too close, crowding the play
		}

		if (distToFirstMan > PhaseC::SECOND_MAN_SUPPORT_RANGE_MAX) {
			return 0;  // Too far, not providing support
		}

		// Optimal support distance centers at GOAL_POST_BACK_DISTANCE
		float distanceFactor = 1.0f - (fabs(distToFirstMan - PhaseC::GOAL_POST_BACK_DISTANCE) / 1500.0f);

		// Velocity bonus: reward matching first-man's speed commitment
		float secondManVelocity = player.vel.Length();
		float velocityAlignment = RS_CLAMP(secondManVelocity / firstManVelocity, 0.5f, 1.5f);

		return RS_CLAMP(0.15f * distanceFactor * velocityAlignment, 0.0f, 0.35f);
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

	RotationPhase phase;

public:
	ProgressiveRotationReward(RotationPhase phase_)
		: phase(phase_)
		, spacingReward(phase_)
		, retreatReward(phase_)
		, roleReward(phase_)
	{
	}

	void Reset(const GameState& state) override {
		spacingReward.Reset(state);
		retreatReward.Reset(state);
		roleReward.Reset(state);
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
			total += spacingReward.GetReward(player, state, isFinal) * 0.1f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.6f;
			total += roleReward.GetReward(player, state, isFinal) * 0.3f;
			break;

		case RotationPhase::DIAMOND:
			// Focus: Balanced rotation system
			total += spacingReward.GetReward(player, state, isFinal) * 0.5f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.8f;
			total += roleReward.GetReward(player, state, isFinal) * 0.8f;
			break;

		case RotationPhase::CHAMPION_PLUS:
			// Focus: Perfect execution
			total += spacingReward.GetReward(player, state, isFinal) * 0.4f;
			total += retreatReward.GetReward(player, state, isFinal) * 0.6f;
			total += roleReward.GetReward(player, state, isFinal) * 1.0f;
			break;
		}

		return total;
	}

	void PreStep(const GameState& state) override {
		spacingReward.PreStep(state);
		retreatReward.PreStep(state);
		roleReward.PreStep(state);
	}
};

// ============================================================================
// BOOST-AWARE ROTATION REWARD
// Incentivizes rotation based on boost availability:
// - Low-boost players should be in FURTHEST role (last man, defensive)
// - High-boost players should be in CLOSEST role (first man, offensive)
// Enables dynamic role switching based on resource availability.
//
// Disabled during kickoffs. Only applies in 2v2+ team play.
// Active from Platinum+ (when role awareness develops).
// ============================================================================
class BoostAwareRotationReward : public Reward {
private:
	enum class BoostRole {
		FURTHEST,      // Closest to own goal (should be low-boost player)
		MIDDLE,        // In between (3v3+ only)
		CLOSEST        // Furthest from own goal (should be high-boost player)
	};

	struct RoleMemory {
		BoostRole lastFrameRole = BoostRole::FURTHEST;
		int roleStepCount = 0;
		bool hasGivenEventReward = false;
		float lastBoostDisparity = 0.0f;
	};

	std::unordered_map<uint32_t, RoleMemory> memory;
	RotationConfig config;
	int currentStep = 0;

	// Constants
	static constexpr int ROLE_STABILITY_STEPS = 120;  // ~1 second at 120 Hz
	static constexpr float LOW_BOOST_THRESHOLD = 75.0f;
	static constexpr float HIGH_BOOST_THRESHOLD = 100.0f;
	static constexpr float BOOST_DISPARITY_THRESHOLD = 30.0f;

	// Get the player's goal position (own goal center)
	Vec GetOwnGoalPos(Team team) const {
		return (team == Team::BLUE)
			? CommonValues::BLUE_GOAL_CENTER
			: CommonValues::ORANGE_GOAL_CENTER;
	}

	// Determine player's role based on goal-line distance (X coordinate)
	// FURTHEST = closest to own goal, CLOSEST = furthest from own goal
	BoostRole DeterminePlayerRole(const Player& player, const GameState& state) const {
		float playerGoalDist = (player.pos - GetOwnGoalPos(player.team)).Length();

		std::vector<float> teammateDists;
		for (const auto& p : state.players) {
			if (p.team == player.team && p.carId != player.carId) {
				float dist = (p.pos - GetOwnGoalPos(player.team)).Length();
				teammateDists.push_back(dist);
			}
		}

		if (teammateDists.empty()) {
			return BoostRole::CLOSEST;  // Only player on team
		}

		std::sort(teammateDists.begin(), teammateDists.end());

		// Determine role based on distance to own goal
		if (playerGoalDist <= teammateDists[0]) {
			return BoostRole::FURTHEST;  // Closest to own goal = last man
		}
		else if (teammateDists.size() >= 2 && playerGoalDist >= teammateDists.back()) {
			return BoostRole::CLOSEST;   // Furthest from own goal = first man
		}
		else {
			return BoostRole::MIDDLE;    // In between (3v3+)
		}
	}

	// Get boost disparity between player and closest teammate
	float GetBoostDisparity(const Player& player, const GameState& state) const {
		float closestTeammateDist = FLT_MAX;
		float closestTeammateBoost = 0.0f;

		for (const auto& p : state.players) {
			if (p.team == player.team && p.carId != player.carId) {
				float dist = (player.pos - p.pos).Length();
				if (dist < closestTeammateDist) {
					closestTeammateDist = dist;
					closestTeammateBoost = p.boost;
				}
			}
		}

		if (closestTeammateDist == FLT_MAX) {
			return 0.0f;  // No teammates
		}

		return closestTeammateBoost - player.boost;
	}

	// Check if player is in the correct role based on boost
	bool IsCorrectRole(const Player& player, const GameState& state, BoostRole currentRole) const {
		float boostDisparity = GetBoostDisparity(player, state);

		// Low-boost player should be FURTHEST
		if (player.boost < LOW_BOOST_THRESHOLD) {
			return currentRole == BoostRole::FURTHEST;
		}

		// High-boost player should be CLOSEST
		if (player.boost > HIGH_BOOST_THRESHOLD && boostDisparity < -BOOST_DISPARITY_THRESHOLD) {
			return currentRole == BoostRole::CLOSEST;
		}

		// Balanced boost - no penalty for any role
		return true;
	}

	// Check if during kickoff (all players near center)
	bool IsKickoffPhase(const GameState& state) const {
		const float KICKOFF_CENTER_RADIUS = 3000.0f;
		Vec center(0, 0, 0);

		for (const auto& p : state.players) {
			if ((p.pos - center).Length() > KICKOFF_CENTER_RADIUS) {
				return false;  // Player outside kickoff zone
			}
		}
		return true;
	}

public:
	BoostAwareRotationReward(RotationPhase phase)
		: config(RotationConfig::ForPhase(phase)) {
	}

	void Reset(const GameState& state) override {
		memory.clear();
		currentStep = 0;
	}

	float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Only active in Platinum+
		if (!config.enableRoleCycling) {
			return 0.0f;
		}

		// Skip 1v1 matches
		if (state.players.size() < 4) {
			return 0.0f;
		}

		// Skip kickoff phase
		if (IsKickoffPhase(state)) {
			return 0.0f;
		}

		auto& mem = memory[player.carId];
		float reward = 0.0f;

		BoostRole currentRole = DeterminePlayerRole(player, state);
		float boostDisparity = GetBoostDisparity(player, state);
		mem.lastBoostDisparity = boostDisparity;

		// Check for role transition
		if (currentRole != mem.lastFrameRole) {
			mem.roleStepCount = 0;
			mem.hasGivenEventReward = false;
			mem.lastFrameRole = currentRole;
		}

		mem.roleStepCount++;

		// Only evaluate role correctness after stability window
		if (mem.roleStepCount >= ROLE_STABILITY_STEPS) {
			bool correctRole = IsCorrectRole(player, state, currentRole);

			// EVENT REWARD: Transition to correct role
			if (correctRole && !mem.hasGivenEventReward && mem.roleStepCount == ROLE_STABILITY_STEPS) {
				reward += 0.3f;
				mem.hasGivenEventReward = true;
			}

			// SHAPING REWARD: Maintain correct role
			if (correctRole) {
				// Amplify if there's meaningful boost disparity
				float amplifier = 1.0f;
				if (std::abs(boostDisparity) > BOOST_DISPARITY_THRESHOLD) {
					amplifier = 1.5f;
				}
				reward += 0.2f * amplifier;
			}
			else {
				// PENALTY: Low-boost player in CLOSEST role
				if (player.boost < LOW_BOOST_THRESHOLD && currentRole == BoostRole::CLOSEST) {
					reward -= 0.15f;
				}
			}
		}

		return reward;
	}

	void PreStep(const GameState& state) override {
		currentStep++;
	}
};

// ============================================================================
// DEFENSIVE POSITIONING CHALLENGE REWARD
// Context-aware defensive reward: non-last-man should challenge aggressively,
// last-man should maintain distance and delay opponent.
// Applies only to 2v2+ team play and defensive phases (ball in own half).
// ============================================================================
class DefensivePositioningChallengeReward : public Reward {
private:
	static constexpr float AGGRESSIVE_CHALLENGE_DIST = 2000.0f;   // Threshold for aggressive positioning
	static constexpr float DEFENSIVE_PATIENCE_DIST = 2500.0f;     // Threshold for patient positioning

	Vec GetOwnGoalPosition(Team team) const {
		return (team == Team::BLUE)
			? CommonValues::BLUE_GOAL_CENTER
			: CommonValues::ORANGE_GOAL_CENTER;
	}

	Vec GetOpponentGoalPosition(Team team) const {
		return (team == Team::BLUE)
			? CommonValues::ORANGE_GOAL_CENTER
			: CommonValues::BLUE_GOAL_CENTER;
	}

	bool IsTeamDefending(const Player& player, const GameState& state) const {
		// Check if ball is closer to own goal than opponent goal
		Vec ownGoal = GetOwnGoalPosition(player.team);
		Vec oppGoal = GetOpponentGoalPosition(player.team);

		float distToOwnGoal = state.ball.pos.Dist(ownGoal);
		float distToOppGoal = state.ball.pos.Dist(oppGoal);

		return distToOwnGoal < distToOppGoal;
	}

	bool IsLastManBack(const Player& player, const GameState& state) const {
		Vec ownGoal = GetOwnGoalPosition(player.team);
		float playerDistToGoal = player.pos.Dist(ownGoal);

		// Check all teammates' distances to own goal
		for (const auto& teammate : state.players) {
			if (teammate.team == player.team && teammate.carId != player.carId) {
				float teammateDistToGoal = teammate.pos.Dist(ownGoal);
				// If any teammate is closer to own goal, player is not last man
				if (teammateDistToGoal < playerDistToGoal) {
					return false;
				}
			}
		}

		return true;
	}

public:
	void Reset(const GameState& initialState) override {
		// No state tracking - stateless reward
	}

	virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
		// Skip in 1v1 (only apply in 2v2+)
		if (state.players.size() < 4) {
			return 0.0f;
		}

		// Only reward during defensive phase
		if (!IsTeamDefending(player, state)) {
			return 0.0f;
		}

		// Determine if player is last man back
		bool isLastMan = IsLastManBack(player, state);

		// Get distance to ball
		float distToBall = (player.pos - state.ball.pos).Length();

		if (isLastMan) {
			// Last man should maintain defensive patience (distance from ball)
			if (distToBall > DEFENSIVE_PATIENCE_DIST) {
				// Good positioning - waiting for teammates to rotate
				return 0.4f;
			}
			else if (distToBall > AGGRESSIVE_CHALLENGE_DIST) {
				// Adequate - could back up more for better positioning
				return 0.2f;
			}
			else {
				// Too close - should retreat to allow rotation
				return 0.0f;
			}
		}
		else {
			// Not last man - should challenge opponent aggressively
			if (distToBall < AGGRESSIVE_CHALLENGE_DIST) {
				// Excellent challenge positioning
				return 0.6f;
			}
			else if (distToBall < DEFENSIVE_PATIENCE_DIST) {
				// Adequate - could move up to challenge more
				return 0.3f;
			}
			else {
				// Too passive - should contest the ball
				return 0.0f;
			}
		}
	}
};

} // namespace RLGC
