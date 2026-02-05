#pragma once
#include "Reward.h"
#include "../Math.h"
#include "RewardMath.h"

namespace RLGC {

	// ========================================================================
	// WALL DASH REWARD - Simplified 2025-02-04 | Rev 8.0 | NJH
	// ========================================================================
	// Detects wall dashes as rapid wavedashes near walls
	//
	// KEY INSIGHT:
	// - A wall dash is fundamentally a series of wavedashes while on/near a wall
	// - Wavedash detection: (isOnGround && prev->isFlipping && !prev->isOnGround)
	// - Wall proximity: Z position near ground/wall level (Z < 300)
	// - Reward rapid-fire chains with combo counter
	//
	// SIMPLIFICATION RATIONALE:
	// - Avoids frame-timing issues from per-frame action input analysis
	// - Uses proven wavedash detection pattern (from WavedashBasicReward)
	// - Game state booleans don't suffer from 8-frame action delays
	// - More reliable detection, easier to tune
	//
	// REWARD STRUCTURE:
	// 1. Base 1.0 for wall wavedash detection
	// 2. Combo bonus: +0.2 per consecutive wavedash (up to 2.5x at 5+ consecutive)
	// 3. Decay combo counter after 120 ticks (~1 second) of inactivity
	// ========================================================================

	class WallDashReward : public Reward {
	private:
		// Per-player state tracking
		mutable std::map<uint32_t, int> wavedashCountdown;      // Ticks since last wavedash
		mutable std::map<uint32_t, int> consecutiveWavedashes;  // Count of rapid wavedashes

		// Configuration constants
		static constexpr int RESET_TICKS = 120;           // ~1 second at 120Hz
		static constexpr float Z_HEIGHT_THRESHOLD = 300.0f;  // Near wall/ground level

	public:
		WallDashReward() = default;

		void Reset(const GameState& initialState) override {
			wavedashCountdown.clear();
			consecutiveWavedashes.clear();
		}

		float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			int& countdownRef = wavedashCountdown[player.carId];
			int& countRef = consecutiveWavedashes[player.carId];

			// ===================================================================
			// STEP 1: Detect wavedash using proven WavedashBasicReward logic
			// ===================================================================
			// Condition: landed this tick (isOnGround) AND was flipping while airborne
			bool wavedashOccurred = (player.isOnGround &&
									  player.prev &&
									  player.prev->isFlipping &&
									  !player.prev->isOnGround);

			// ===================================================================
			// STEP 2: Check if wavedash occurred near wall (low Z height)
			// ===================================================================
			if (wavedashOccurred && player.pos.z > Z_HEIGHT_THRESHOLD) {
				// Wavedash on wall detected!
				countRef++;
				countdownRef = RESET_TICKS;

				// Reward scales with consecutive count (encourages rapid wall dashes)
				float baseReward = 1.0f;
				float comboBonus = RS_MIN(countRef * 0.2f, 1.5f);  // Max 2.5x at 5+ consecutive
				return baseReward + comboBonus;
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
