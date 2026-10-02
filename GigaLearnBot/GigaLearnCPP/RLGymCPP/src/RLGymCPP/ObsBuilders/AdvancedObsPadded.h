#pragma once

#include "AdvancedObs.h"

namespace RLGC {
	// Advanced obs that supports variable player counts (1v1, 2v2, 3v3, etc.)
	// Combines the benefits of AdvancedObs with Nexto-inspired enhancements:
	// - Boost pad timers (respawn progress) and a big/small flag per pad (34 extra floats)
	// - Demo timers (respawn countdown)
	// - Relative coordinates (in car's local frame)
	// - Local angular velocity
	// - Dynamic player padding with shuffling to prevent slot bias
	//
	// Inspired by Nexto's observation builder for better training signals
	class AdvancedObsPadded : public AdvancedObs {
	public:

		int maxPlayers;

		// Demo timer tracking (updated each frame)
		std::vector<float> demoTimers;

		explicit AdvancedObsPadded(int maxPlayers = 6)
			: AdvancedObs(), maxPlayers(maxPlayers) {
		}

		virtual void Reset(const GameState& initialState) override;

		virtual FList BuildObs(const Player& player, const GameState& state) override;

	private:
		// Enhanced player observation that includes demo timers
		// Captures: position, rotation, velocity, angular velocity, local ball info,
		//           boost level, on_ground, has_flip, is_demoed, demo_timer
		void AddPlayerToObsWithDemoTimer(
			FList& obs,
			const Player& player,
			bool inv,
			const PhysState& ball,
			float demoTimer
		);
	};
}
