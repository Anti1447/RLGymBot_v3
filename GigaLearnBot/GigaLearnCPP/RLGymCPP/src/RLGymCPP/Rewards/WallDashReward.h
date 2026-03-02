#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"

namespace RLGC {

	// ========================================================================
	// WALL DASH REWARD - Simplified 2025-02-04 | Rev 9.0 | NJH
	// ========================================================================
	// Detects wall dashes as rapid wavedashes near walls
	//
	// KEY INSIGHT:
	// - A wall dash is fundamentally a series of wavedashes while on/near a wall
	// - Wavedash detection: (isOnGround && prev->isFlipping && !prev->isOnGround)
	// - Wall proximity: within 400 UU of SIDE_WALL_X (±4096) or BACK_WALL_Y (±5120)
	// - Height scaling: reward scales linearly 0→1 from Z=0 to Z=1500
	//   (replaces hard Z > 300 gate that prevented signal before high wall play was learned)
	// - Reward rapid-fire chains with combo counter
	//
	// SIMPLIFICATION RATIONALE:
	// - Avoids frame-timing issues from per-frame action input analysis
	// - Uses proven wavedash detection pattern (from WavedashBasicReward)
	// - Game state booleans don't suffer from 8-frame action delays
	// - More reliable detection, easier to tune
	//
	// REWARD STRUCTURE:
	// 1. Base 1.0 * heightScale for wall wavedash detection
	// 2. Combo bonus: +0.2 per consecutive wavedash (up to 1.5 at 5+ consecutive), also scaled by height
	// 3. Decay combo counter after 120 ticks (~1 second) of inactivity
	//
	// REWARD SCALE REFERENCE:
	//   Z=0   → heightScale=0.00 → 0.00 base (ground, no reward)
	//   Z=150 → heightScale=0.10 → 0.10 base
	//   Z=300 → heightScale=0.20 → 0.20 base
	//   Z=750 → heightScale=0.50 → 0.50 base
	//   Z=1500→ heightScale=1.00 → 1.00 base (full reward)
	//
	// Rev 9.0 changes:
	//   - Replaced hard Z > 300 gate with linear height scale (Z / MAX_WALL_HEIGHT)
	//   - Added IsNearWall() proximity check (matches SmartWallPlayRewards.h convention)
	//   - Hard gate had been preventing all signal since bot hadn't yet learned high wall play
	// ========================================================================

	class WallDashReward : public Reward {
	private:
		// Per-player state tracking
		mutable std::map<uint32_t, int> wavedashCountdown;      // Ticks since last wavedash
		mutable std::map<uint32_t, int> consecutiveWavedashes;  // Count of rapid wavedashes

		// Configuration constants
		static constexpr int   RESET_TICKS        = 120;     // ~1 second at 120Hz
		static constexpr float WALL_DIST_THRESHOLD = 400.0f; // UU from wall surface — matches SmartWallPlayRewards.h
		static constexpr float MAX_WALL_HEIGHT     = 1500.0f; // Z at which heightScale reaches 1.0

		bool IsNearWall(const Vec& pos) const {
			float distX = fabsf(fabsf(pos.x) - CommonValues::SIDE_WALL_X);
			float distY = fabsf(fabsf(pos.y) - CommonValues::BACK_WALL_Y);
			return (distX < WALL_DIST_THRESHOLD || distY < WALL_DIST_THRESHOLD);
		}

	public:
		WallDashReward() = default;

		void Reset(const GameState& initialState) override {
			wavedashCountdown.clear();
			consecutiveWavedashes.clear();
		}

		float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			int& countdownRef = wavedashCountdown[player.carId];
			int& countRef    = consecutiveWavedashes[player.carId];

			// ===================================================================
			// STEP 1: Detect wavedash using proven WavedashBasicReward logic
			// ===================================================================
			// Condition: landed this tick (isOnGround) AND was flipping while airborne
			bool wavedashOccurred = (player.isOnGround &&
			                          player.prev &&
			                          player.prev->isFlipping &&
			                          !player.prev->isOnGround);

			// ===================================================================
			// STEP 2: Wall proximity + height scaling
			// ===================================================================
			if (wavedashOccurred && IsNearWall(player.pos)) {
				// Linear scale: 0.0 at Z=0 (ground), 1.0 at Z=MAX_WALL_HEIGHT (high wall)
				// Gives partial signal at low wall play, full signal at high wall play
				float heightScale = RS_CLAMP(player.pos.z / MAX_WALL_HEIGHT, 0.0f, 1.0f);

				countRef++;
				countdownRef = RESET_TICKS;

				// Reward scales with consecutive count (encourages rapid wall dashes)
				float baseReward = 1.0f;
				float comboBonus = RS_MIN(countRef * 0.2f, 1.5f);  // Max 2.5x at 5+ consecutive
				return (baseReward + comboBonus) * heightScale;
			}

			// ===================================================================
			// STEP 3: Decay combo counter
			// ===================================================================
			if (countdownRef > 0) {
				countdownRef--;
			} else {
				countRef = 0;  // Reset combo after inactivity
			}

			return 0.0f;
		}
	};

} // namespace RLGC
