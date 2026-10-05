#pragma once
#include "StateSetter.h"
#include "../Math.h"
#include <iterator>

namespace RLGC {
	// State setter for flip reset training
	// - Ball spawns high and slow, with normal gravity
	// - One car spawns airborne below the ball, moving toward it, with its wheels pointed
	//   within ~39 degrees of the ball (RolvFlipResetReward gates on 45), flip already spent
	// - All other cars keep their kickoff positions
	class FlipResetState : public StateSetter {
	public:
		constexpr static float
			XY_MAX = 2500.0f,
			Z_MIN_BALL = 900.0f,
			Z_MAX_BALL = 1600.0f,
			BALL_VEL_XY_MAX = 400.0f,
			BALL_VEL_Z_MAX = 300.0f,
			DIST_MIN = 600.0f,        // Car spawn distance from the ball
			DIST_MAX = 1000.0f,
			SIN_ELEV_MIN = 0.3f,      // How far below the ball the car spawns (sin of elevation)
			SIN_ELEV_MAX = 0.8f,
			CAR_Z_MIN = 250.0f,
			TIME_TO_BALL_MIN = 0.5f,  // Seconds for the car to reach the ball if it only coasts
			TIME_TO_BALL_MAX = 1.0f,
			WHEEL_ERROR_MAX = 0.8f;   // ~tan(39 deg), max wheel-direction error from the ball

		void ResetArena(Arena* arena) override {
			using RocketSim::Math::RandFloat;

			// Reset arena to default state (resets boost pads, places all cars at kickoff)
			arena->ResetToRandomKickoff();

			BallState bs = {};
			bs.pos = Math::RandVec(
				Vec(-XY_MAX, -XY_MAX, Z_MIN_BALL),
				Vec(XY_MAX, XY_MAX, Z_MAX_BALL)
			);
			bs.vel = Math::RandVec(
				Vec(-BALL_VEL_XY_MAX, -BALL_VEL_XY_MAX, 0),
				Vec(BALL_VEL_XY_MAX, BALL_VEL_XY_MAX, BALL_VEL_Z_MAX)
			);
			arena->ball->SetState(bs);

			// Pick one car to be the flip reset actor
			int actorIdx = RS_MIN((int)RandFloat(0, (float)arena->_cars.size()), (int)arena->_cars.size() - 1);
			auto it = arena->_cars.begin();
			std::advance(it, actorIdx);
			Car* actor = *it;

			// Spawn below the ball, in a random horizontal direction
			float yaw = RandFloat(-M_PI, M_PI);
			float sinElev = RandFloat(SIN_ELEV_MIN, SIN_ELEV_MAX);
			float horiz = sqrtf(1 - sinElev * sinElev);
			Vec offsetDir = Vec(cosf(yaw) * horiz, sinf(yaw) * horiz, -sinElev);

			CarState cs = {};
			cs.pos = bs.pos + offsetDir * RandFloat(DIST_MIN, DIST_MAX);
			cs.pos.z = RS_MAX(cs.pos.z, CAR_Z_MIN);

			Vec toBallVec = bs.pos - cs.pos;
			float dist = toBallVec.Length();
			Vec toBall = toBallVec / dist;

			// Car and ball both fall at the same gravity, so aiming the relative velocity
			// at the ball makes the car arrive in TIME_TO_BALL seconds (boost only helps)
			cs.vel = bs.vel + toBall * (dist / RandFloat(TIME_TO_BALL_MIN, TIME_TO_BALL_MAX));

			// Wheels (down = -up) point at the ball with a random error, random heading about that axis
			Vec randDir = Math::RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
			Vec wheelDir = (toBall + randDir * RandFloat(0, WHEEL_ERROR_MAX)).Normalized();
			Vec up = wheelDir * -1.0f;
			Vec heading = Math::RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
			Vec forward = heading - up * heading.Dot(up);
			cs.rotMat = RotMat::LookAt(forward, up);

			// Flip spent, so HasFlipReset() is false until the car actually earns a reset
			cs.isOnGround = false;
			cs.hasJumped = true;
			cs.hasDoubleJumped = true;

			cs.boost = RandFloat(50, 100);

			actor->SetState(cs);
		}
	};
}
