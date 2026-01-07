#pragma once
#include "StateSetter.h"

namespace RLGC {
	class KickoffState : public StateSetter {
	public:
		void ResetArena(Arena* arena) {
			arena->ResetToRandomKickoff();
			
			// CRITICAL: Restore normal gravity to the ball
			// (In case previous episode was aerial training with zero gravity)
			float gravityBT = RLConst::GRAVITY_Z * UU_TO_BT;
			arena->ball->_rigidBody.setGravity(btVector3(0, 0, gravityBT));
		}
	};
}