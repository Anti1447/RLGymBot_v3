#pragma once

#include "AdvancedObs.h"
#include <deque>
#include <unordered_map>

namespace RLGC {
	// Enhanced version of AdvancedObsPadded for SixSevenBotv2.
	// Per-player obs: 33 floats (vs 30 in V1).
	//
	// New fields vs V1:
	//   hasDoubleJumped  — whether double jump has been consumed
	//   isSupersonic     — moving fast enough to demolish
	//   airTimeSinceJump — proxy for dodge window (normalized by 1.25s)
	//
	// Demo timer simplified: uses player.demoRespawnTimer directly instead of
	// manual per-frame tracking. No demoTimers vector needed.
	//
	// Supports solo-freeplay (0 opponents) via zero-padding — obs size is always
	// fixed at 249 floats regardless of game mode (with maxPlayers=3).
	// Breakdown: 9 ball + 8 action + 34 pads + 33 self + 2×33 mates + 3×33 opps
	//
	// Memory Blocks A+B: 16 additional floats appended after the base 249.
	// Zero-filled in Phases 1-4, active Phase 5+. Total post-activation: 265.
	// Call SetMemoryBlocksEnabled(CURRENT_PHASE >= PHASE_5_DIAMOND) at construction.
	//
	// ARCHITECTURE NOTE: one obs builder instance is shared across all players in
	// an env. BuildObs is called once per player per step with different `player`
	// arguments. Tracker state is therefore keyed by perspective.carId so each
	// player gets independent tracking. Time comes from EnvClock (thread_local,
	// advances exactly once per step regardless of how many players call Update).
	class AdvancedObsPaddedV2 : public AdvancedObs {
	public:

		int maxPlayers;

		explicit AdvancedObsPaddedV2(int maxPlayers = 3)
			: AdvancedObs(), maxPlayers(maxPlayers) {
		}

		virtual void Reset(const GameState& initialState) override;

		virtual FList BuildObs(const Player& player, const GameState& state) override;

		void SetMemoryBlocksEnabled(bool enabled) { memoryBlocksEnabled = enabled; }

	private:
		// Builds per-player obs: base 29 floats + demo timer + 3 new fields = 33 total
		void AddPlayerToObsV2(
			FList& obs,
			const Player& player,
			bool inv,
			const PhysState& ball
		);

		// ====================================================================
		// MEMORY BLOCKS A+B — 2026-05 | NJH
		// ====================================================================

		// Block A: opponent-team behavioral tendencies (A1–A8, 8 floats emitted)
		struct TeamTendency {
			float commitmentEMA    = 0.0f;   // A1 [0,1]
			float aerialEMA        = 0.0f;   // A2 [0,1]
			float depthMeanEMA     = 0.0f;   // A3 [-1,1] nearest opp Y in persp frame
			float boostMeanEMA     = 0.5f;   // A4 [0,1]
			float speedMeanEMA     = 0.5f;   // A5 [0,1]
			float pressureEMA      = 0.0f;   // A6 [0,1]
			float challengeRateEMA = 0.0f;   // A7 [0,~0.1]
			int   sampleCount      = 0;      // A8 source  (conf = min(1, count/300))
		};

		// Block B: self and own-team summary state (B1–B8, 8 floats emitted)
		struct SelfSummary {
			float myLastTouchTime        = -1000.0f;  // B1 source
			float teamLastTouchTime      = -1000.0f;  // B2 source
			float possessionEMA          = 0.5f;      // B3 [0,1]
			float teamAvgDistToBallEMA   = 0.0f;      // B8 [0,1]

			// Rolling history queues — (timestamp, value) pairs trimmed by window
			std::deque<std::pair<float, float>> boostHistory;      // B4 slope (1s)
			std::deque<std::pair<float, Vec>>   positionHistory;   // B5 variance (2s)
			std::deque<std::pair<float, float>> ballZHistory;      // B6 max Z (1.5s)
			std::deque<std::pair<float, float>> ballSpeedHistory;  // B7 mean speed (1s)
		};

		// Per-perspective tracker maps — keyed by perspective player's carId.
		// Prevents cross-contamination when BuildObs is called for multiple players
		// in the same env step (each player sees its own independent tracker).
		std::unordered_map<uint32_t, TeamTendency> _oppTendencyByPlayer;
		std::unordered_map<uint32_t, SelfSummary>  _selfSummaryByPlayer;

		bool memoryBlocksEnabled = false;

		// ===== EMA alpha constants (tickSkip=4 → 30 decisions/sec) =====
		// Derived as α = 1/(window_seconds × 30): gives center-of-mass window
		static constexpr float ALPHA_FAST = 0.0033f;  // ~10s window (300 steps)
		static constexpr float ALPHA_MED  = 0.0022f;  // ~15s window (450 steps)
		static constexpr float ALPHA_SLOW = 0.0011f;  // ~30s window (900 steps)
		static constexpr float ALPHA_B8   = 0.0111f;  // ~3s window  (90 steps)

		// ===== Normalization / window constants =====
		static constexpr float CONFIDENCE_SAMPLES     = 300.0f;   // ~10s at 30/sec → confidence = 1
		static constexpr float COMMITMENT_RADIUS      = 800.0f;   // UU from ball to count as committed
		static constexpr float AERIAL_Z_THRESHOLD     = 200.0f;   // UU minimum Z to count as aerial
		static constexpr float BOOST_HISTORY_SEC      = 1.0f;
		static constexpr float POSITION_HISTORY_SEC   = 2.0f;
		static constexpr float BALL_Z_HISTORY_SEC     = 1.5f;
		static constexpr float BALL_SPEED_HISTORY_SEC = 1.0f;
		static constexpr float TOUCH_TIMEOUT_NORM     = 5.0f;     // secs-since-touch clamp
		static constexpr float BALL_SPEED_NORM        = 6000.0f;  // UU/s  (above realistic max)
		static constexpr float TEAM_DIST_NORM         = 8000.0f;  // UU
		static constexpr float POS_VARIANCE_NORM      = 2000.0f;  // UU

		// 29 base (AddPlayerToObs: 8 Vec×3 + 5 scalar) + 4 new fields
		static constexpr int   PLAYER_OBS_SIZE        = 33;

		// ===== Memory block methods =====
		void UpdateMemoryTrackers(const Player& perspective, const GameState& state);
		void TrimHistoryScalar(std::deque<std::pair<float, float>>& q, float now, float window);
		void TrimHistoryVec(std::deque<std::pair<float, Vec>>& q, float now, float window);
		void AddMemoryBlocksAToObs(FList& obs, const Player& perspective);
		void AddMemoryBlocksBToObs(FList& obs, const Player& perspective);
	};
}
