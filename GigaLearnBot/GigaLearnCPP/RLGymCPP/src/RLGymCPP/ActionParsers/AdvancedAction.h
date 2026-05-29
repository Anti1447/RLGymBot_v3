#pragma once
#include "DefaultAction.h"

namespace RLGC {
	// Discrete action parser with more bins than DefaultAction.
	// Ground actions: throttle × steer × boost × handbrake (5×9×5×5 pruned).
	// Air actions: pitch × yaw × roll × jump × boost (9^3 × 5^2 pruned).
	class AdvancedAction : public DefaultAction {
	public:
		AdvancedAction();
	};
}
