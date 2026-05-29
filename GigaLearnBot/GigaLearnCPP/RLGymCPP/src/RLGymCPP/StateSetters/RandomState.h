#pragma once
#include "StateSetter.h"
#include <vector>

namespace RLGC {
	class RandomState : public StateSetter {
	public:
		bool
			randBallSpeed, randCarSpeed, carsOnGround;

		// Probability a car spawns airborne when carsOnGround == false.
		// Ignored when carsOnGround == true. Tune per curriculum phase to
		// control aerial-start density. Default 0.5 preserves the original
		// coin-flip behavior.
		float airborneProbability = 0.5f;

		// Default constructor: enable random speeds and keep cars on ground by default
		RandomState() : RandomState(true, true, true) {}

		RandomState(bool randBallSpeed, bool randCarSpeed, bool carsOnGround) :
			randBallSpeed(randBallSpeed), randCarSpeed(randCarSpeed), carsOnGround(carsOnGround) {
		}

		RandomState(bool randBallSpeed, bool randCarSpeed, bool carsOnGround, float airborneProbability) :
			randBallSpeed(randBallSpeed), randCarSpeed(randCarSpeed), carsOnGround(carsOnGround),
			airborneProbability(airborneProbability) {
		}

		virtual void ResetArena(Arena* arena);
	};
}