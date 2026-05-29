#pragma once
#include "StateSetter.h"

namespace RLGC {
	class RandomState : public StateSetter {
	public:
		bool
			randBallSpeed, randCarSpeed, carsOnGround;

		// Default constructor: enable random speeds and keep cars on ground by default
		RandomState() : RandomState(true, true, true) {}

		RandomState(bool randBallSpeed, bool randCarSpeed, bool carsOnGround) :
			randBallSpeed(randBallSpeed), randCarSpeed(randCarSpeed), carsOnGround(carsOnGround) {
		}

		virtual void ResetArena(Arena* arena);
	};
}