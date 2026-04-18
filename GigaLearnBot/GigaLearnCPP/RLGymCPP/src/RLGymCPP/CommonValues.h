#pragma once
#include "../Framework.h"

namespace RLGC {
	// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/common_values.py
	// https://wiki.rlbot.org/v5/botmaking/useful-game-values/
	namespace CommonValues {
		// Could just use RocketSim's RLConst but I'll just copy to stay faithful

		//constexpr float TICK_TIME = 1 / 120.f; // 1.0x speed
		constexpr float TICK_TIME = 1 / 120.f; // 0.5x speed to try out


		// 2025-12-18 | Rev 2.0 | NJH
		// - Major revision based on RLBot wiki reference
		// - Fixed goal geometry for accurate "on goal" detection
		// - Added ball radius consideration for goal scoring
		// - Added boost pad timings and car physics constants
		// - Clarified goal line vs back wall vs back net distinctions

		// 2025-12-28 | Rev 3.0 | NJH
		// - Added ACTUAL kickoff spawn positions (verified from screenshot)
		// - Added demolition respawn locations
		// - Added kickoff detection constants
		// - Corrected invalid ±2560 assumptions
		// - References for enhanced KickoffContactPunishmentReward v2

		constexpr float
			// Coordinate system:
			//  - Z is up
			//  - (0,0) is center field
			//  - Negative Y is toward Blue goal (team 0)
			//  - Positive Y is toward Orange goal (team 1)
			FLOOR_Z = 0.0f,

			// Main walls / bounds
			SIDE_WALL_X = 4096.0f,      // x = ±4096
			BACK_WALL_Y = 5120.0f,      // y = ±5120 (where goals begin)
			CEILING_Z = 2044.0f,        // z = 2044

			// Goal geometry (critical for scoring detection)
			GOAL_HEIGHT = 642.775f,     // crossbar height above ground
			GOAL_WIDTH_FROM_CENTER = 892.755f,  // center to post (half-width)
			GOAL_TOTAL_WIDTH = 1785.51f,        // full width (2 * 892.755)

			// Goal depth and planes
			GOAL_LINE_Y = 5120.0f,      // front of goal (same as BACK_WALL_Y)
			GOAL_DEPTH = 880.0f,        // distance from goal line to back net
			BACK_NET_Y = 6000.0f,       // y = ±6000 (back net plane: 5120 + 880)

			// Goal structure dimensions
			POST_RADIUS_APPROX = 80.0f, // approximate goal post thickness

			// Straight wall lengths (ignoring curve fillets at joins)
			SIDE_WALL_LENGTH = 7936.0f,
			BACK_WALL_LENGTH = 5888.0f,

			// Corner geometry (diagonal corner planes are at ~45°)
			CORNER_AXIS_INTERCEPT = 8064.0f,    // corner planes intersect axes at ±8064
			CORNER_WALL_LENGTH = 1629.174f,     // 1152*sqrt(2) ≈ 1629.174

			// Bottom ramp radius note (not actually perfect circles)
			WALL_BOTTOM_RAMP_RADIUS_APPROX = 256.0f;

		// 2025-12-18 | Rev 2.0 | NJH
		// - Goal post and crossbar positions (corners of goal opening rectangle)
		// - These define the actual goal opening where balls can score
		// - REQUIRED: These constants are used by existing reward functions

		constexpr Vec
			// Goal line centers (front plane of goal at y = ±GOAL_LINE_Y)
			ORANGE_GOAL_LINE_CENTER = Vec(0.0f, GOAL_LINE_Y, 0.0f),
			BLUE_GOAL_LINE_CENTER = Vec(0.0f, -GOAL_LINE_Y, 0.0f),

			// Goal "center" points (midpoint of goal opening - useful for aiming)
			// REQUIRED: Used by CommonRewards.h and AdvancedRewards.h
			ORANGE_GOAL_CENTER = Vec(0.0f, GOAL_LINE_Y, GOAL_HEIGHT * 0.5f),
			BLUE_GOAL_CENTER = Vec(0.0f, -GOAL_LINE_Y, GOAL_HEIGHT * 0.5f),

			// Back net centers (deep inside goal - useful for "guaranteed goal" checks)
			// REQUIRED: Used by CommonRewards.h, AdvancedRewards.h, and CompleteMechanicsRewards.h
			ORANGE_GOAL_BACK = Vec(0.0f, BACK_NET_Y, GOAL_HEIGHT * 0.5f),
			BLUE_GOAL_BACK = Vec(0.0f, -BACK_NET_Y, GOAL_HEIGHT * 0.5f),

			// Posts at goal line (bottom corners of goal opening)
			ORANGE_GOAL_LEFT_POST = Vec(-GOAL_WIDTH_FROM_CENTER, GOAL_LINE_Y, 0.0f),
			ORANGE_GOAL_RIGHT_POST = Vec(GOAL_WIDTH_FROM_CENTER, GOAL_LINE_Y, 0.0f),
			BLUE_GOAL_LEFT_POST = Vec(-GOAL_WIDTH_FROM_CENTER, -GOAL_LINE_Y, 0.0f),
			BLUE_GOAL_RIGHT_POST = Vec(GOAL_WIDTH_FROM_CENTER, -GOAL_LINE_Y, 0.0f),

			// Crossbar endpoints at goal line (top corners of goal opening)
			ORANGE_GOAL_LEFT_BAR = Vec(-GOAL_WIDTH_FROM_CENTER, GOAL_LINE_Y, GOAL_HEIGHT),
			ORANGE_GOAL_RIGHT_BAR = Vec(GOAL_WIDTH_FROM_CENTER, GOAL_LINE_Y, GOAL_HEIGHT),
			BLUE_GOAL_LEFT_BAR = Vec(-GOAL_WIDTH_FROM_CENTER, -GOAL_LINE_Y, GOAL_HEIGHT),
			BLUE_GOAL_RIGHT_BAR = Vec(GOAL_WIDTH_FROM_CENTER, -GOAL_LINE_Y, GOAL_HEIGHT);


		// =====================================================================
		// KICKOFF SPAWN POSITIONS - 2025-12-28 | Rev 1.0 | CORRECTED
		// =====================================================================
		// VERIFIED FROM SCREENSHOT - These are the ONLY valid kickoff positions
		// All other positions are INVALID
		// 
		// Yaw values provided for reference but not used in detection
		// Z coordinate is always 17.01 (standard car height)

		constexpr float KICKOFF_CAR_Z = 17.01f;  // Standard car height at spawn

		// KICKOFF FORMATIONS - Verified positions only

		constexpr Vec
			// RIGHT CORNER - Blue team
			// loc: (-2048, -2560), yaw: 0.25 pi
			BLUE_KICKOFF_RIGHT_CORNER = Vec(-2048.0f, -2560.0f, KICKOFF_CAR_Z),

			// RIGHT CORNER - Orange team
			// loc: (2048, 2560), yaw: -0.75 pi
			ORANGE_KICKOFF_RIGHT_CORNER = Vec(2048.0f, 2560.0f, KICKOFF_CAR_Z),

			// LEFT CORNER - Blue team
			// loc: (2048, -2560), yaw: 0.75 pi
			BLUE_KICKOFF_LEFT_CORNER = Vec(2048.0f, -2560.0f, KICKOFF_CAR_Z),

			// LEFT CORNER - Orange team
			// loc: (-2048, 2560), yaw: -0.25 pi
			ORANGE_KICKOFF_LEFT_CORNER = Vec(-2048.0f, 2560.0f, KICKOFF_CAR_Z),

			// BACK RIGHT - Blue team
			// loc: (-256.0, -3840), yaw: 0.5 pi
			BLUE_KICKOFF_BACK_RIGHT = Vec(-256.0f, -3840.0f, KICKOFF_CAR_Z),

			// BACK RIGHT - Orange team
			// loc: (256.0, 3840), yaw: -0.5 pi
			ORANGE_KICKOFF_BACK_RIGHT = Vec(256.0f, 3840.0f, KICKOFF_CAR_Z),

			// BACK LEFT - Blue team
			// loc: (256.0, -3840), yaw: 0.5 pi
			BLUE_KICKOFF_BACK_LEFT = Vec(256.0f, -3840.0f, KICKOFF_CAR_Z),

			// BACK LEFT - Orange team
			// loc: (-256.0, 3840), yaw: -0.5 pi
			ORANGE_KICKOFF_BACK_LEFT = Vec(-256.0f, 3840.0f, KICKOFF_CAR_Z),

			// FAR BACK CENTER - Blue team
			// loc: (0.0, -4608), yaw: 0.5 pi
			BLUE_KICKOFF_FAR_BACK_CENTER = Vec(0.0f, -4608.0f, KICKOFF_CAR_Z),

			// FAR BACK CENTER - Orange team
			// loc: (0.0, 4608), yaw: -0.5 pi
			ORANGE_KICKOFF_FAR_BACK_CENTER = Vec(0.0f, 4608.0f, KICKOFF_CAR_Z);

		// NOTE: There is NO "CENTER" / "STRAIGHT" formation at (0, ±2560)
		// The closest formation to center is the BACK positions
		// Distance from center varies by formation:
		//   Right/Left Corner: sqrt(2048² + 2560²) ≈ 3279 units
		//   Back Right/Left: sqrt(256² + 3840²) ≈ 3850 units
		//   Far Back Center: 4608 units


		// =====================================================================
		// DEMOLITION RESPAWN POSITIONS - 2025-12-28 | Rev 1.0
		// =====================================================================
		// Positions where demolished cars respawn
		// All positions are for Blue team (negative Y)
		// Orange positions: mirror Y (negate and swap X appropriately)
		// Z coordinate is always 17.01 (standard car height)

		constexpr Vec
			// RIGHT INSIDE - Blue team
			// loc: (-2304, -4608)
			BLUE_DEMOLISH_RIGHT_INSIDE = Vec(-2304.0f, -4608.0f, KICKOFF_CAR_Z),

			// RIGHT INSIDE - Orange team
			// loc: (2304, 4608)
			ORANGE_DEMOLISH_RIGHT_INSIDE = Vec(2304.0f, 4608.0f, KICKOFF_CAR_Z),

			// RIGHT OUTSIDE - Blue team
			// loc: (-2688, -4608)
			BLUE_DEMOLISH_RIGHT_OUTSIDE = Vec(-2688.0f, -4608.0f, KICKOFF_CAR_Z),

			// RIGHT OUTSIDE - Orange team
			// loc: (2688, 4608)
			ORANGE_DEMOLISH_RIGHT_OUTSIDE = Vec(2688.0f, 4608.0f, KICKOFF_CAR_Z),

			// LEFT INSIDE - Blue team
			// loc: (2304, -4608)
			BLUE_DEMOLISH_LEFT_INSIDE = Vec(2304.0f, -4608.0f, KICKOFF_CAR_Z),

			// LEFT INSIDE - Orange team
			// loc: (-2304, 4608)
			ORANGE_DEMOLISH_LEFT_INSIDE = Vec(-2304.0f, 4608.0f, KICKOFF_CAR_Z),

			// LEFT OUTSIDE - Blue team
			// loc: (2688, -4608)
			BLUE_DEMOLISH_LEFT_OUTSIDE = Vec(2688.0f, -4608.0f, KICKOFF_CAR_Z),

			// LEFT OUTSIDE - Orange team
			// loc: (-2688, 4608)
			ORANGE_DEMOLISH_LEFT_OUTSIDE = Vec(-2688.0f, 4608.0f, KICKOFF_CAR_Z);


		// =====================================================================
		// KICKOFF DETECTION CONSTANTS - 2025-12-28
		// =====================================================================
		// Used by KickoffContactPunishmentReward v2 for formation detection

		constexpr float
			// Formation detection threshold
			// IMPORTANT: There is no "center" formation!
			// All positions are either corners or back positions
			// 
			// Distance from center by formation:
			// - Right/Left corners: ~3279 units
			// - Back right/left: ~3850 units
			// - Far back center: 4608 units
			//
			// Therefore: Any corner position is < 3300 units
			// Use 3300 as threshold: < 3300 = CORNER, >= 3300 = BACK
			KICKOFF_FORMATION_THRESHOLD = 3300.0f,

			// Distance references (for physics validation)
			CORNER_FORMATION_BALL_DISTANCE = 3279.0f,    // sqrt(2048² + 2560²)
			BACK_FORMATION_BALL_DISTANCE = 3850.0f;      // sqrt(256² + 3840²)

		// Ball position during kickoff
		constexpr Vec KICKOFF_BALL_CENTER = Vec(0.0f, 0.0f, 93.0f);  // Ball height ~93 units


		// Ball physics constants
		constexpr float
			BALL_RADIUS = 92.75f,       // ball radius (91.25 in some sources, 92.75 more common)
			BALL_MAX_SPEED = 6000.0f,   // theoretical max ball speed
			BALL_DRAG = 0.03f,          // air drag coefficient (approximate)
			BALL_GRAVITY = -650.0f;     // gravity acceleration in z-direction

		// Car physics constants
		constexpr float
			CAR_MAX_SPEED = 2300.0f,              // absolute max speed (with boosting)
			SUPERSONIC_THRESHOLD = 2200.0f,       // speed needed for supersonic
			CAR_MAX_SPEED_NO_BOOST = 1410.0f,     // max driving speed (throttle only, no boost)
			CAR_MASS = 180.0f,                    // car mass (arbitrary units)
			CAR_MAX_ANG_VEL = 5.5f,               // max angular velocity (rad/s)

			// Boost constants
			BOOST_CONSUMPTION_RATE = 33.3f,       // boost consumption per second
			BOOST_ACCEL_GROUND = 991.667f,        // boost acceleration on ground
			BOOST_ACCEL_AIR = 1058.333f,          // boost acceleration in air

			// Ground driving constants (without boost)
			BRAKE_ACCEL = -3500.0f,               // deceleration when braking
			COASTING_ACCEL = -525.0f,             // deceleration during coast (zero throttle)

			// Jump constants
			JUMP_IMMEDIATE_FORCE = 292.0f,     // initial jump velocity
			JUMP_HOLD_FORCE = 1458.0f,         // additional force per second while holding
			JUMP_MIN_DURATION = 0.025f,        // minimum jump hold time
			JUMP_MAX_DURATION = 0.2f,          // maximum jump hold time

			// Dodge constants
			DODGE_IMPULSE = 500.0f,            // forward/backward dodge impulse
			DODGE_SIDE_IMPULSE = 260.0f,       // side dodge impulse
			DODGE_TORQUE = 260.0f;             // angular impulse from dodge

		// Boost pad timings (seconds)
		constexpr float
			BOOST_PAD_COOLDOWN_BIG = 10.0f,    // large boost pad respawn time
			BOOST_PAD_COOLDOWN_SMALL = 4.0f;   // small boost pad respawn time

		// Team constants
		constexpr int
			BLUE_TEAM = 0,
			ORANGE_TEAM = 1;

		constexpr int NUM_ACTIONS = 8;

		// Boost pad locations
		constexpr int BOOST_LOCATIONS_AMOUNT = 34;
		constexpr Vec BOOST_LOCATIONS[BOOST_LOCATIONS_AMOUNT] = {
				{0.f, -4240.0, 70.0},
				{-1792.0, -4184.0, 70.0},
				{1792.0, -4184.0, 70.0},
				{-3072.0, -4096.0, 73.0}, // big boost blue
				{3072.0, -4096.0, 73.0}, // big boost blue
				{-940.0, -3308.0, 70.0},
				{940.0, -3308.0, 70.0},
				{0.0, -2816.0, 70.0},
				{-3584.0, -2484.0, 70.0},
				{3584.0, -2484.0, 70.0},
				{-1788.0, -2300.0, 70.0},
				{1788.0, -2300.0, 70.0},
				{-2048.0, -1036.0, 70.0},
				{0.0, -1024.0, 70.0},
				{2048.0, -1036.0, 70.0},
				{-3584.0, 0.0, 73.0}, // Big boost mid
				{-1024.0, 0.0, 70.0},
				{1024.0, 0.0, 70.0},
				{3584.0, 0.0, 73.0}, // Big boost mid
				{-2048.0, 1036.0, 70.0},
				{0.0, 1024.0, 70.0},
				{2048.0, 1036.0, 70.0},
				{-1788.0, 2300.0, 70.0},
				{1788.0, 2300.0, 70.0},
				{-3584.0, 2484.0, 70.0},
				{3584.0, 2484.0, 70.0},
				{0.0, 2816.0, 70.0},
				{-940.0, 3310.0, 70.0},
				{940.0, 3308.0, 70.0},
				{-3072.0, 4096.0, 73.0}, // Big boost orange
				{3072.0, 4096.0, 73.0}, // Big boost orange
				{-1792.0, 4184.0, 70.0},
				{1792.0, 4184.0, 70.0},
				{0.0, 4240.0, 70.0}
		};

		// Large boost pad indices (100 boost)
		constexpr int LARGE_BOOST_INDICES[] = {3, 4, 15, 18, 29, 30};
		constexpr int NUM_LARGE_BOOSTS = 6;

		// Small boost pad indices (12 boost) - all others
		constexpr int NUM_SMALL_BOOSTS = 28;
	}
}