#pragma once
#include "Reward.h"
#include <map>
#include <unordered_map>
#include <atomic>
#include <cmath>

namespace RLGC {

	// ============================================================================
	// DoubleJumpAerialReward - Reward double jump aerial setup
	// Detects and rewards the fundamental timing of double jumping in air
	// 2026-02-08 | Rev 1.0 | NJH
	//
	// Key features:
	// - Tracks first jump (transition from ground to air)
	// - Detects second jump within 2 seconds of first jump
	// - Rewards learning correct aerial timing
	// - Simple detection: just validates jump input sequence
	// - Bonus for reaching higher altitude with double jump
	// ============================================================================
	class DoubleJumpAerialReward : public Reward {
	private:
		struct AerialTracker {
			enum State { WAITING, FIRST_JUMP, SECOND_JUMP_READY, COMPLETED };

			State state = WAITING;
			float jumpStartTime = -1.0f;
			float firstJumpHeight = 0.0f;
			float maxHeightAfterSecondJump = 0.0f;
			bool secondJumpDetected = false;
		};

		mutable std::map<uint32_t, AerialTracker> trackers;

		// Configuration
		static constexpr float DOUBLE_JUMP_WINDOW = 2.0f;         // Max time between jumps (seconds)
		static constexpr float BASE_REWARD = 1.0f;                // Base reward for double jump
		static constexpr float HEIGHT_BONUS_SCALE = 0.003f;       // Bonus per unit of height gained
		static constexpr float MAX_HEIGHT_BONUS = 2.0f;           // Cap on height bonus

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];

			// ====================================================================
			// Phase 1: First Jump Detection
			// Detect transition from ground to air
			// ====================================================================
			if (prevPlayer.isOnGround && !player.isOnGround) {
				tracker.state = AerialTracker::FIRST_JUMP;
				tracker.jumpStartTime = 0.0f;
				tracker.firstJumpHeight = player.pos.z;
				tracker.secondJumpDetected = false;
				return 0.0f;
			}

			// Timeout: if in air for > 2.5 seconds without second jump, reset
			if (tracker.state != AerialTracker::WAITING) {
				tracker.jumpStartTime += state.deltaTime > 0.0f ? state.deltaTime : 0.016f;
				if (tracker.jumpStartTime > 2.5f) {
					tracker.state = AerialTracker::WAITING;
					return 0.0f;
				}
			}

			// ====================================================================
			// Phase 2: Second Jump Detection
			// Detect when player jumps again while airborne (within 2 second window)
			// ====================================================================
			if (tracker.state == AerialTracker::FIRST_JUMP &&
				tracker.jumpStartTime > 0.0f && tracker.jumpStartTime <= DOUBLE_JUMP_WINDOW) {

				// Pure double jump (no directional flip) — hasDoubleJumped transitions false→true.
				// Flips do NOT set hasDoubleJumped (RocketSim guarantee), so this is exclusive.
				if (player.hasDoubleJumped && !prevPlayer.hasDoubleJumped) {
					tracker.state = AerialTracker::SECOND_JUMP_READY;
					tracker.secondJumpDetected = true;
					tracker.maxHeightAfterSecondJump = player.pos.z;
				}
			}

			// Track maximum height after second jump
			if (tracker.state == AerialTracker::SECOND_JUMP_READY && player.pos.z > tracker.maxHeightAfterSecondJump) {
				tracker.maxHeightAfterSecondJump = player.pos.z;
			}

			// ====================================================================
			// Phase 3: Reward Trigger
			// Award reward for successful double jump within timing window
			// ====================================================================
			if (tracker.state == AerialTracker::SECOND_JUMP_READY && tracker.secondJumpDetected) {
				// Calculate height gained from second jump
				float heightGained = RS_MAX(0.0f, tracker.maxHeightAfterSecondJump - tracker.firstJumpHeight);

				// Height bonus: up to 2.0 for reaching 667+ units higher
				float heightBonus = RS_MIN(MAX_HEIGHT_BONUS, heightGained * HEIGHT_BONUS_SCALE);

				float reward = BASE_REWARD + heightBonus;

				// Reset for next aerial
				tracker.state = AerialTracker::WAITING;
				tracker.secondJumpDetected = false;

				return reward;
			}

			return 0.0f;
		}
	};

	// ============================================================================
	// PitchControlAerialReward - Reward fast aerial technique with pitch control
	// Detects and rewards jump → pitch up → jump sequence for quick aerial boost
	// 2026-02-08 | Rev 2.0 | NJH
	//
	// Rev 2.0 fixes (6 bugs, 2 critical):
	// [CRITICAL] Boost gate zeroed all rewards — removed sequenceStartBoost/boostMultiplier
	// [CRITICAL] Height computed on same tick as 2nd jump (always 0) — deferred to next tick
	// [minor] COMPLETED state was unreachable — removed from enum
	// [minor] DOUBLE_JUMP_WINDOW 0.4s too tight for humans — widened to 0.55s
	// [minor] GetPitchAccuracyScore clamped to 90° instead of MAX_PITCH_DEGREES — fixed
	// [minor] No boost-during-ascent signal — added 1.5x multiplier for boost use in Phase 2
	// ============================================================================
	class PitchControlAerialReward : public Reward {
	private:
		struct FastAerialTracker {
			enum State { WAITING, FIRST_JUMP, PITCHED_UP, SECOND_JUMP };

			State state = WAITING;
			float jumpStartTime = 0.0f;
			float firstJumpHeight = 0.0f;
			float maxPitchDuringFirstJump = 0.0f;
			bool isPitchedUpEnough = false;
			bool secondJumpDetected = false;
			float maxHeightAfterSecondJump = 0.0f;
			bool rewardPending = false;        // deferred payout — pay on the tick AFTER 2nd jump
			bool boostedDuringAscent = false;  // tracks boost use in Phase 2 (FIRST_JUMP → PITCHED_UP)
		};

		mutable std::map<uint32_t, FastAerialTracker> trackers;

		// Metrics
		mutable std::atomic<uint64_t> s_totalAwardedCount{0};
		mutable std::atomic<uint64_t> s_boostBonusCount{0};
		mutable std::atomic<int64_t>  s_totalRewardSum{0};  // stored as reward * 1000 (milliunits)

		// Configuration
		static constexpr float DOUBLE_JUMP_WINDOW    = 0.55f;   // widened from 0.4s — gives humans more headroom
		static constexpr float MIN_PITCH_DEGREES     = 25.0f;
		static constexpr float MAX_PITCH_DEGREES     = 65.0f;
		static constexpr float OPTIMAL_PITCH_DEGREES = 45.0f;
		static constexpr float PITCH_TOLERANCE       = 15.0f;
		static constexpr float BASE_REWARD           = 2.0f;
		static constexpr float HEIGHT_BONUS_SCALE    = 0.005f;
		static constexpr float MAX_HEIGHT_BONUS      = 3.0f;
		static constexpr float PITCH_BONUS_SCALE     = 0.02f;
		static constexpr float MAX_PITCH_BONUS       = 1.0f;
		static constexpr float BOOST_ASCENT_MULTIPLIER = 1.5f;  // applied when boost used during ascent

	public:
		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!state.prev) return 0.0f;

			auto& tracker = trackers[player.carId];
			const Player& prevPlayer = state.prev->players[player.index];
			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

			// ====================================================================
			// Deferred payout: pay the reward the tick AFTER second jump so that
			// maxHeightAfterSecondJump has had at least one frame to accumulate.
			// ====================================================================
			if (tracker.rewardPending) {
				tracker.rewardPending = false;

				float heightGained = RS_MAX(0.0f, tracker.maxHeightAfterSecondJump - tracker.firstJumpHeight);
				float heightBonus  = RS_MIN(MAX_HEIGHT_BONUS, heightGained * HEIGHT_BONUS_SCALE);

				float pitchAccuracy = GetPitchAccuracyScore(tracker.maxPitchDuringFirstJump);
				float pitchBonus    = RS_MIN(MAX_PITCH_BONUS, pitchAccuracy * PITCH_BONUS_SCALE * 100.0f);

				float reward = BASE_REWARD + heightBonus + pitchBonus;

				if (tracker.boostedDuringAscent) {
					reward *= BOOST_ASCENT_MULTIPLIER;
					s_boostBonusCount.fetch_add(1);
				}

				tracker.state = FastAerialTracker::WAITING;
				tracker.secondJumpDetected = false;

				s_totalAwardedCount.fetch_add(1);
				s_totalRewardSum.fetch_add(static_cast<int64_t>(reward * 1000.0f));
				return reward;
			}

			// ====================================================================
			// Phase 1: First Jump Detection
			// ====================================================================
			if (prevPlayer.isOnGround && !player.isOnGround) {
				tracker.state                  = FastAerialTracker::FIRST_JUMP;
				tracker.jumpStartTime          = 0.0f;
				tracker.firstJumpHeight        = player.pos.z;
				tracker.maxPitchDuringFirstJump = 0.0f;
				tracker.isPitchedUpEnough      = false;
				tracker.secondJumpDetected     = false;
				tracker.boostedDuringAscent    = false;
				tracker.rewardPending          = false;
				return 0.0f;
			}

			// Global timeout: reset if airborne > 2.5s without completing sequence
			if (tracker.state != FastAerialTracker::WAITING) {
				tracker.jumpStartTime += dt;
				if (tracker.jumpStartTime > 2.5f) {
					tracker.state = FastAerialTracker::WAITING;
					return 0.0f;
				}
			}

			// ====================================================================
			// Phase 2: Pitch Validation (FIRST_JUMP state)
			// ====================================================================
			if (tracker.state == FastAerialTracker::FIRST_JUMP) {
				float pitchRadians = atan2(player.rotMat.forward.z,
					sqrt(player.rotMat.forward.x * player.rotMat.forward.x +
					     player.rotMat.forward.y * player.rotMat.forward.y));
				float pitchDegrees = pitchRadians * 57.2958f;

				if (pitchDegrees > tracker.maxPitchDuringFirstJump)
					tracker.maxPitchDuringFirstJump = pitchDegrees;

				if (pitchDegrees >= MIN_PITCH_DEGREES && pitchDegrees <= MAX_PITCH_DEGREES) {
					tracker.isPitchedUpEnough = true;
					tracker.state = FastAerialTracker::PITCHED_UP;
				}
			}

			// ====================================================================
			// Phase 3b: Boost-during-ascent tracking (FIRST_JUMP or PITCHED_UP)
			// ====================================================================
			if ((tracker.state == FastAerialTracker::FIRST_JUMP ||
			     tracker.state == FastAerialTracker::PITCHED_UP)
			    && player.prevAction.boost > 0.5f)
			{
				tracker.boostedDuringAscent = true;
			}

			// ====================================================================
			// Phase 3: Second Jump Detection (PITCHED_UP state)
			// ====================================================================
			if (tracker.state == FastAerialTracker::PITCHED_UP &&
			    tracker.jumpStartTime > 0.0f && tracker.jumpStartTime <= DOUBLE_JUMP_WINDOW)
			{
				// Pure double jump only — flips do NOT set hasDoubleJumped (RocketSim guarantee)
				if (player.hasDoubleJumped && !prevPlayer.hasDoubleJumped) {
					tracker.state              = FastAerialTracker::SECOND_JUMP;
					tracker.secondJumpDetected = true;
					tracker.maxHeightAfterSecondJump = player.pos.z;
					// Don't pay yet — defer to next tick so height can accumulate
					tracker.rewardPending = true;
					return 0.0f;
				}
			}

			// Track max height while in SECOND_JUMP (during deferred window)
			if (tracker.state == FastAerialTracker::SECOND_JUMP &&
			    player.pos.z > tracker.maxHeightAfterSecondJump)
			{
				tracker.maxHeightAfterSecondJump = player.pos.z;
			}

			return 0.0f;
		}

		struct Metrics {
			uint64_t totalAwarded;
			uint64_t boostBonus;
			double   rewardSum;
			double   boostBonusRate;
		};
		Metrics CollectMetrics() {
			Metrics m{};
			m.totalAwarded   = s_totalAwardedCount.exchange(0);
			m.boostBonus     = s_boostBonusCount.exchange(0);
			m.rewardSum      = double(s_totalRewardSum.exchange(0)) / 1000.0;
			m.boostBonusRate = (m.totalAwarded > 0) ? double(m.boostBonus) / m.totalAwarded : 0.0;
			return m;
		}

	protected:
		// Pitch accuracy score [0, 1]: peak at OPTIMAL_PITCH_DEGREES, zero outside [MIN, MAX].
		float GetPitchAccuracyScore(float pitchDegrees) const {
			float clampedPitch   = RS_CLAMP(pitchDegrees, MIN_PITCH_DEGREES, MAX_PITCH_DEGREES);
			float diffFromOptimal = fabs(clampedPitch - OPTIMAL_PITCH_DEGREES);
			float normalized     = diffFromOptimal / (OPTIMAL_PITCH_DEGREES - MIN_PITCH_DEGREES);
			return RS_CLAMP(1.0f - (normalized * normalized), 0.0f, 1.0f);
		}
	};

	/* RETIRED 2026-06-23: AirDribbleSetupReward Rev 1.0
	   Replaced by Rev 2.1 event-gated implementation below.
	   Rev 1.0 was a continuous frame-by-frame co-presence reward (player z>400, ball z>300).
	   Caused hover farming, anti-rotation gradient, teammate crowding, boost-penalized aerials.

	// ============================================================================
	// AIR DRIBBLE SETUP REWARD - Generic soft-touch setup detection
	// Rewards soft touch sequences that establish air dribble positioning
	// Foundation for advanced air dribble mechanics (arc-specific handling)
	// 2026-02-10 | Rev 1.0 | NJH
	// ============================================================================
	class AirDribbleSetupReward : public Reward {
	...
	};
	*/

	// ============================================================================
	// AirDribbleSetupReward Rev 2.1 — 9-gate event-gated setup touch detector
	// Entry event for air dribble: the touch that pops ball into carry position.
	// 2026-06-23 | Rev 2.1 | NJH
	//
	// Rev 1.0 (commented out above) was frame-by-frame co-presence — retired.
	// 9 gates at touch moment → reward stored → step +12 pursuit alignment check.
	// 5 cancellation conditions + hard timeout. No continuous firing.
	// ============================================================================
	class AirDribbleSetupReward : public Reward {
	public:
		struct Config {
			// Gate thresholds — cheapest first (matches evaluation order in GetReward)
			float minBallHeightAtTouch     = 140.0f;   // ball z at touch (UU)
			float minBoost                 = 20.0f;    // min boost to pass gate
			float minBallVelZ              = 150.0f;   // min upward ball vel post-touch (UU/s)
			float minTouchImpulse          = 100.0f;   // min ball speed delta from touch (UU/s)
			float ballSpeedMin             = 900.0f;   // post-touch ball speed lower bound (UU/s)
			float ballSpeedMax             = 1600.0f;  // post-touch ball speed upper bound (UU/s)
			float carSpeedMin              = 900.0f;   // car speed lower bound — airborne only (UU/s)
			float carSpeedMax              = 1600.0f;  // car speed upper bound — all setups (UU/s)
			float ceilingClearance         = 1800.0f;  // max predicted ball apex Z (UU)
			float opponentMinTTR           = 0.5f;     // min opponent closing TTR to ball (sec)
			float minOpponentApproachSpeed = 800.0f;   // floor for opponent closing speed in TTR calc
			// Confirmation window
			int   confirmationSteps        = 12;       // steps before pursuit check (~0.4s at tickSkip=4)
			float minPursuitAlignment      = 0.6f;     // min dot(playerVel, toBall) at confirmation
			float ballGroundCancelZ        = 150.0f;   // ball z below this → cancel pending
			float ceilingCancelZ           = 1900.0f;  // ball z above this → cancel pending
			// Reward magnitudes
			float baseSetupReward          = 0.4f;     // base before quality multipliers
			float boostFullQualityThresh   = 60.0f;    // boost level for full boostQuality (1.0)
			float ballVelZFull             = 500.0f;   // ball vel.z for full upwardQuality (1.0)
			float goalAlignQualityFloor    = 0.5f;     // min multiplier for poorly-aimed setups
		};

	private:
		struct SetupMemory {
			bool  pendingConfirm = false;
			float pendingReward  = 0.0f;
			int   confirmTimer   = 0;
		};

		Config config;
		mutable std::unordered_map<uint32_t, SetupMemory> trackers;

		// Metrics — atomic for thread safety across 310 parallel arenas
		mutable std::atomic<int64_t> m_touchesGated{0};
		mutable std::atomic<int64_t> m_confirmationsSucceeded{0};
		mutable std::atomic<int64_t> m_cancelledDemoed{0};
		mutable std::atomic<int64_t> m_cancelledLanded{0};
		mutable std::atomic<int64_t> m_cancelledBallGround{0};
		mutable std::atomic<int64_t> m_cancelledBallCeiling{0};
		mutable std::atomic<int64_t> m_cancelledOpponentTouch{0};
		mutable std::atomic<int64_t> m_cancelledTimeout{0};
		mutable std::atomic<int64_t> m_cancelledNoPursuit{0};
		mutable std::atomic<int64_t> m_speedQualitySum{0};
		mutable std::atomic<int64_t> m_boostQualitySum{0};
		mutable std::atomic<int64_t> m_upwardQualitySum{0};
		mutable std::atomic<int64_t> m_rewardMilli{0};

		float ComputeSpeedQuality(float ballSpeed) const {
			float center    = (config.ballSpeedMin + config.ballSpeedMax) * 0.5f;
			float halfRange = (config.ballSpeedMax - config.ballSpeedMin) * 0.5f;
			float raw       = 1.0f - (fabsf(ballSpeed - center) / halfRange);
			return RS_CLAMP(raw, 0.2f, 1.0f);
		}

		float ComputeBoostQuality(float boost) const {
			float range = config.boostFullQualityThresh - config.minBoost;
			return RS_CLAMP(0.5f + 0.5f * ((boost - config.minBoost) / range), 0.5f, 1.0f);
		}

		float ComputeUpwardQuality(float ballVelZ) const {
			return RS_CLAMP(
				(ballVelZ - config.minBallVelZ) / (config.ballVelZFull - config.minBallVelZ),
				0.0f, 1.0f
			);
		}

	public:
		explicit AirDribbleSetupReward(Config cfg = Config{}) : config(cfg) {}

		virtual void Reset(const GameState& initialState) override {
			trackers.clear();
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			auto& mem = trackers[player.carId];

			// ================================================================
			// Confirmation window — runs every step while a touch is pending
			// ================================================================
			if (mem.pendingConfirm) {
				// Hard timeout: cancel if window overran (e.g. skipped GetReward call)
				if (mem.confirmTimer > config.confirmationSteps) {
					mem.pendingConfirm = false; mem.pendingReward = 0.0f;
					m_cancelledTimeout.fetch_add(1);
					return 0.0f;
				}
				if (player.isDemoed) {
					mem.pendingConfirm = false; mem.pendingReward = 0.0f;
					m_cancelledDemoed.fetch_add(1);
					return 0.0f;
				}
				// isOnGround covers walls in this build (empirical — monitor CancelledLanded
				// for wall-chip false positives via W&B)
				if (player.isOnGround) {
					mem.pendingConfirm = false; mem.pendingReward = 0.0f;
					m_cancelledLanded.fetch_add(1);
					return 0.0f;
				}
				if (state.ball.pos.z < config.ballGroundCancelZ) {
					mem.pendingConfirm = false; mem.pendingReward = 0.0f;
					m_cancelledBallGround.fetch_add(1);
					return 0.0f;
				}
				if (state.ball.pos.z > config.ceilingCancelZ) {
					mem.pendingConfirm = false; mem.pendingReward = 0.0f;
					m_cancelledBallCeiling.fetch_add(1);
					return 0.0f;
				}
				for (const auto& other : state.players) {
					if (other.team != player.team && other.ballTouchedStep) {
						mem.pendingConfirm = false; mem.pendingReward = 0.0f;
						m_cancelledOpponentTouch.fetch_add(1);
						return 0.0f;
					}
				}

				mem.confirmTimer++;

				if (mem.confirmTimer >= config.confirmationSteps) {
					float playerSpeed = player.vel.Length();
					float toBallDist  = (state.ball.pos - player.pos).Length();
					Vec playerVelDir  = (playerSpeed > 1.0f) ? (player.vel / playerSpeed)
					                                         : Vec(0.0f, 0.0f, 0.0f);
					Vec toBallDir     = (toBallDist  > 1.0f)
					                  ? ((state.ball.pos - player.pos) / toBallDist)
					                  : Vec(0.0f, 0.0f, 1.0f);
					float align = playerVelDir.Dot(toBallDir);

					float fired = 0.0f;
					if (align >= config.minPursuitAlignment && !player.isOnGround) {
						fired = mem.pendingReward;
						m_confirmationsSucceeded.fetch_add(1);
						m_rewardMilli.fetch_add(static_cast<int64_t>(fired * 1000.0f));
					} else {
						m_cancelledNoPursuit.fetch_add(1);
					}
					mem.pendingConfirm = false;
					mem.pendingReward  = 0.0f;
					return fired;
				}
				return 0.0f;
			}

			// ================================================================
			// Gate evaluation — only runs on touch frames
			// ================================================================
			if (!player.ballTouchedStep) return 0.0f;
			if (!state.prev)             return 0.0f;

			// Gate 1: ball height at touch
			if (state.ball.pos.z < config.minBallHeightAtTouch) return 0.0f;
			// Gate 2: minimum boost
			if (player.boost < config.minBoost) return 0.0f;
			// Gate 3: ball going upward
			if (state.ball.vel.z < config.minBallVelZ) return 0.0f;
			// Gate 4: touch impulse (ball speed delta vs prev frame)
			if ((state.ball.vel.Length() - state.prev->ball.vel.Length()) < config.minTouchImpulse)
				return 0.0f;
			// Gate 5: post-touch ball speed in chaseable band
			float ballSpeed = state.ball.vel.Length();
			if (ballSpeed < config.ballSpeedMin || ballSpeed > config.ballSpeedMax) return 0.0f;
			// Gate 6: car speed band; lower bound waived when isOnGround (wall-driving chip)
			float carSpeed = player.vel.Length();
			if (player.isOnGround) {
				if (carSpeed > config.carSpeedMax) return 0.0f;
			} else {
				if (carSpeed < config.carSpeedMin || carSpeed > config.carSpeedMax) return 0.0f;
			}
			// Gate 7: ceiling clearance — analytic ballistic apex
			float apexZ = (state.ball.vel.z > 0.0f)
				? (state.ball.pos.z + (state.ball.vel.z * state.ball.vel.z) / (2.0f * 650.0f))
				: state.ball.pos.z;
			if (apexZ >= config.ceilingClearance) return 0.0f;

			// Gates 8 & 9 combined — single player loop (most expensive, runs last)
			float myDist = (player.pos - state.ball.pos).Length();
			for (const auto& other : state.players) {
				if (other.carId == player.carId || other.isDemoed) continue;
				// Gate 9: must be closest player on field (teammates included)
				if ((other.pos - state.ball.pos).Length() < myDist) return 0.0f;
				// Gate 8: opponent closing TTR must exceed minimum
				if (other.team != player.team) {
					Vec toBall   = state.ball.pos - other.pos;
					float dist   = toBall.Length();
					Vec dir      = (dist > 1.0f) ? (toBall / dist) : Vec(0.0f, 0.0f, 0.0f);
					float effSpd = RS_MAX(RS_MAX(other.vel.Dot(dir), 0.0f),
					                     config.minOpponentApproachSpeed);
					if ((dist / effSpd) < config.opponentMinTTR) return 0.0f;
				}
			}

			// All 9 gates passed — compute quality composite and store pending reward
			float speedQ  = ComputeSpeedQuality(ballSpeed);
			float boostQ  = ComputeBoostQuality(player.boost);
			float upwardQ = ComputeUpwardQuality(state.ball.vel.z);

			// Goal alignment quality: setups aimed toward the opponent goal are rewarded more
			Vec oppGoalPos = (player.team == Team::BLUE)
				? CommonValues::ORANGE_GOAL_BACK
				: CommonValues::BLUE_GOAL_BACK;
			float goalAlign = (ballSpeed > 1.0f)
				? RS_CLAMP((state.ball.vel / ballSpeed).Dot((oppGoalPos - state.ball.pos).Normalized()), 0.0f, 1.0f)
				: 0.0f;
			float goalAlignQ = RS_CLAMP(
				config.goalAlignQualityFloor + (1.0f - config.goalAlignQualityFloor) * goalAlign,
				config.goalAlignQualityFloor, 1.0f);

			mem.pendingReward  = config.baseSetupReward * speedQ * boostQ * upwardQ * goalAlignQ;
			mem.confirmTimer   = 0;
			mem.pendingConfirm = true;

			m_touchesGated.fetch_add(1);
			m_speedQualitySum.fetch_add(static_cast<int64_t>(speedQ  * 1000.0f));
			m_boostQualitySum.fetch_add(static_cast<int64_t>(boostQ  * 1000.0f));
			m_upwardQualitySum.fetch_add(static_cast<int64_t>(upwardQ * 1000.0f));

			return 0.0f;
		}

		struct Metrics {
			int64_t touchesGated, confirmationsSucceeded;
			int64_t cancelledDemoed, cancelledLanded, cancelledBallGround;
			int64_t cancelledBallCeiling, cancelledOpponentTouch;
			int64_t cancelledTimeout, cancelledNoPursuit;
			int64_t avgSpeedQualityMilli, avgBoostQualityMilli, avgUpwardQualityMilli;
			int64_t rewardMilli;
		};

		Metrics CollectMetrics() const {
			int64_t gated    = m_touchesGated.exchange(0);
			int64_t speedSum = m_speedQualitySum.exchange(0);
			int64_t boostSum = m_boostQualitySum.exchange(0);
			int64_t upSum    = m_upwardQualitySum.exchange(0);
			Metrics m{};
			m.touchesGated           = gated;
			m.confirmationsSucceeded = m_confirmationsSucceeded.exchange(0);
			m.cancelledDemoed        = m_cancelledDemoed.exchange(0);
			m.cancelledLanded        = m_cancelledLanded.exchange(0);
			m.cancelledBallGround    = m_cancelledBallGround.exchange(0);
			m.cancelledBallCeiling   = m_cancelledBallCeiling.exchange(0);
			m.cancelledOpponentTouch = m_cancelledOpponentTouch.exchange(0);
			m.cancelledTimeout       = m_cancelledTimeout.exchange(0);
			m.cancelledNoPursuit     = m_cancelledNoPursuit.exchange(0);
			m.avgSpeedQualityMilli   = (gated > 0) ? (speedSum / gated) : 0;
			m.avgBoostQualityMilli   = (gated > 0) ? (boostSum / gated) : 0;
			m.avgUpwardQualityMilli  = (gated > 0) ? (upSum    / gated) : 0;
			m.rewardMilli            = m_rewardMilli.exchange(0);
			return m;
		}
	};

	// ============================================================================
	// InvertedAerialReward — Rewards flying upside-down (wheels toward ceiling)
	// 2026-02-25 | Rev 1.0 | NJH
	// 2026-03-08 | Rev 1.1 | NJH — apply inverted-aerial mechanic definition:
	//   forward vector unchanged (same as normal aerial), up+right rotated 180°
	//
	// Detects and rewards the car for maintaining an inverted aerial orientation
	// while airborne. This is the "wheels-up" / upside-down flying mechanic used
	// for ceiling approaches, unexpected redirects, and flip-reset setups.
	//
	// Gate conditions (all must pass):
	//   - Airborne (!isOnGround)
	//   - airTime >= 0.2s (ignore brief inverted flips/rolls off the ground)
	//   - pos.z >= 300 UU (no low-altitude inversions)
	//   - rotMat.up.z <= -0.7 (clearly inverted; threshold for "wheels mostly up")
	//   - vel toward ball > 0 OR pos.z >= 600 (car must be approaching ball or committed)
	//
	// Reward structure:
	//   1. Per-frame quality score (framerate-independent via deltaTime):
	//        invertQuality = clamp((-upZ - 0.7) / 0.3, 0, 1)
	//        forwardAlign  = clamp(forward.Dot(toBallNorm), 0, 1)   (nose toward ball)
	//        quality       = invertQuality * (0.6 + 0.4 * forwardAlign)
	//        reward        = REWARD_PER_SEC * quality * dt
	//      Captures both: how upside-down the car is AND how well it is aimed at the ball.
	//
	//   2. Ball proximity multiplier (keeps inversion purposeful, not random):
	//      If distToBall < BALL_PROX_MAX (1500 UU):
	//        proxFactor = 1 - distToBall/BALL_PROX_MAX   (0 to 1 as dist approaches 0)
	//        reward *= (1.0 + proxFactor)                 (up to 2x near ball)
	//
	//   3. Ball touch while inverted (one-shot event):
	//        +BALL_TOUCH_BONUS (0.5) when ballTouchedStep && inverted
	//
	// Stateless — no PreStep, no stored state. All logic in GetReward.
	//
	// Typical exposure (Phase 6, 8-tick skip at 120Hz -> 15 steps/s):
	//   Max per step (perfect inversion, nose on ball, near ball):
	//     quality=1.0 * proxMult=2.0 * dt=0.067 -> ~0.133/step -> ~2 units/s at weight 20
	// ============================================================================
	class InvertedAerialReward : public Reward {
	private:
		static constexpr float INVERT_THRESHOLD  = -0.7f;   // up.z below this = inverted
		static constexpr float INVERT_MAX        = -1.0f;   // up.z at perfect upside-down
		static constexpr float MIN_HEIGHT        = 300.0f;  // UU, min altitude
		static constexpr float MIN_AIRTIME       = 0.2f;    // s, ignore brief inversions
		static constexpr float REWARD_PER_SEC    = 1.0f;    // raw reward/s at perfect inversion
		static constexpr float BALL_PROX_MAX     = 1500.0f; // UU, proximity multiplier range
		static constexpr float BALL_TOUCH_BONUS  = 0.5f;    // one-shot for touching while inverted

	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			// Gate 1: must be airborne
			if (player.isOnGround) return 0.0f;

			// Gate 2: minimum airtime (filter out brief ground flips/rolls)
			if (player.airTime < MIN_AIRTIME) return 0.0f;

			// Gate 3: minimum height
			if (player.pos.z < MIN_HEIGHT) return 0.0f;

			// Gate 4: must be clearly inverted (wheels pointing toward ceiling)
			float upZ = player.rotMat.up.z;
			if (upZ > INVERT_THRESHOLD) return 0.0f;

			// Gate 5: must be moving toward ball, or already high enough to be committed.
			// In a proper inverted aerial the car flies toward the ball (which may be falling),
			// so vel.z > 0 is too narrow — check velocity component toward ball instead.
			Vec toBallVec = state.ball.pos - player.pos;
			float distToBall = toBallVec.Length();
			Vec toBallNorm = (distToBall > 1.0f) ? (toBallVec / distToBall) : Vec(0, 0, 1);
			float velTowardBall = player.vel.Dot(toBallNorm);
			if (velTowardBall <= 0.0f && player.pos.z < 600.0f) return 0.0f;

			// Inversion quality: 0 at threshold (-0.7), 1 at perfect upside-down (-1.0)
			float invertQuality = RS_CLAMP(
				(INVERT_THRESHOLD - upZ) / (INVERT_THRESHOLD - INVERT_MAX),
				0.0f, 1.0f
			);

			// Forward-to-ball alignment: the defining property of an inverted aerial is that
			// the forward vector stays the same as a normal aerial — nose pointed at the ball.
			// 0 = perpendicular/away from ball, 1 = directly aimed at ball.
			float forwardAlign = RS_CLAMP(player.rotMat.forward.Dot(toBallNorm), 0.0f, 1.0f);

			// Combined quality: inversion depth * (base + forward alignment bonus).
			// 0.6 base ensures partial credit even when not perfectly aimed.
			float quality = invertQuality * (0.6f + 0.4f * forwardAlign);

			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

			// Per-frame sustained reward, scaled by combined inversion+alignment quality
			float reward = REWARD_PER_SEC * quality * dt;

			// Ball proximity multiplier: up to 2x when very close to ball.
			if (distToBall < BALL_PROX_MAX) {
				float proxFactor = 1.0f - RS_CLAMP(distToBall / BALL_PROX_MAX, 0.0f, 1.0f);
				reward *= (1.0f + proxFactor);
			}

			// Event: bonus for touching the ball while inverted
			if (player.ballTouchedStep) {
				reward += BALL_TOUCH_BONUS;
			}

			return reward;
		}
	};

}
