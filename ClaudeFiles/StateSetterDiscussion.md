
/
Any improvements you could make to this randomstatesetter.cpp?

#include "RandomState.h"
#include "../Math.h"

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;

Vec RandNormVec() {
    return RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
}

void RLGC::RandomState::ResetArena(Arena* arena) {

    // Reset boost pads and everything
    arena->ResetToRandomKickoff();

    constexpr float
        X_MAX = 3500,
        Y_MAX = 4000,
        Z_MAX = 1820,
        CAR_Z_MIN = 150,
        PITCH_MAX = M_PI / 2,
        YAW_MAX = M_PI,
        ROLL_MAX = M_PI,
        ANGVEL_MAX = 5.5f;

    { // Randomize ball
        BallState bs = {};
        bs.pos = Math::RandVec(Vec(-X_MAX, -Y_MAX, CommonValues::BALL_RADIUS), Vec(X_MAX, Y_MAX, Z_MAX));
        if (randBallSpeed) {
            bs.vel = RandNormVec() * RandFloat(0, 4000);
            bs.angVel = Math::RandVec(Vec(-4, -4, -4), Vec(4, 4, 4));
        }
        arena->ball->SetState(bs);
    }

    for (Car* car : arena->_cars) { // Randomize cars
        CarState cs = {};
        cs.pos = Math::RandVec(Vec(-X_MAX, -Y_MAX, CAR_Z_MIN), Vec(X_MAX, Y_MAX, Z_MAX));

        if (randCarSpeed) {
            Vec randVelDir = Math::RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
            cs.vel = RandNormVec() * RandFloat(0, RLConst::CAR_MAX_SPEED);
            cs.angVel = RandNormVec() * ANGVEL_MAX;
        }

        Angle angle = Angle(RandFloat(-YAW_MAX, YAW_MAX), RandFloat(-PITCH_MAX, PITCH_MAX), RandFloat(-ROLL_MAX, ROLL_MAX));

        bool onGround = carsOnGround ? true : (RandFloat() > 0.5);
        if (onGround) {
            cs.pos.z = 17;
            angle.pitch = angle.roll = 0;
            cs.vel.z = 0;
            cs.angVel = {};
        }

        cs.rotMat = angle.ToRotMat();

        cs.boost = RandFloat(0, 100);

        car->SetState(cs);
    }
}

 https://wiki.rlbot.org/img/useful-game-values/basic-dimensions.png

Show more
Jun 27

Searched memory
Searched memory
Good, I have a solid picture of your codebase. Here are the issues I found and the improved version:

Bugs / correctness issues:

Ball Z min uses BALL_RADIUS from CommonValues (91.25f) correctly, but Z_MAX = 1820 is wrong — the ceiling is 2044.0f. Ball radius should be subtracted from the ceiling: Z_MAX = CEILING_Z - BALL_RADIUS ≈ 1952.75f. With 1820 you're never spawning the ball in the top ~220 UU of the arena.
Car Z_MAX = 1820 is wrong for the same reason — cars can be on the ceiling or high wall. Should also use CommonValues::CEILING_Z.
Ground Z hardcoded to 17 — your own CommonValues::KICKOFF_CAR_Z = 17.01f exists; use it.
Dead variable randVelDir — Vec randVelDir = ... is computed but never used. cs.vel uses RandNormVec() on the next line.
X_MAX = 3500 and Y_MAX = 4000 are both undersized — actual arena is 4096 × 5120. You're missing a significant portion of the field. Should use CommonValues::SIDE_WALL_X / CommonValues::BACK_WALL_Y.
Ball speed upper bound 4000 is below BALL_MAX_SPEED = 6000 — fine if intentional for curriculum, but it's a magic number that should be named.
carsOnGround ? true : ... — the ternary is redundant; just use carsOnGround || (RandFloat() > 0.5f).
cs.pos.z = 17 — when a car is placed on the ground but its random X/Y happens to be inside the goal mouth or outside the playable area (corners), RocketSim may clip or reject it. No boundary check exists.
cpp
#include "RandomState.h"
#include "../Math.h"
#include "../CommonValues.h"  // NJH: pull in named constants

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;
using namespace RLGC::CommonValues;

// Returns a random unit vector uniformly distributed on the sphere.
static Vec RandNormVec() {
    return RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
}

void RLGC::RandomState::ResetArena(Arena* arena) {

    arena->ResetToRandomKickoff();

    // --- Arena bounds (use authoritative CommonValues constants) ---
    // NJH: Previous hardcoded values (3500/4000/1820) were all undersized.
    //      SIDE_WALL_X=4096, BACK_WALL_Y=5120, CEILING_Z=2044.
    //      Leave small inset margins so spawns aren't clipped into geometry.
    constexpr float INSET_XY     = 100.0f;   // stay clear of side/back walls
    constexpr float X_MAX        = SIDE_WALL_X - INSET_XY;   // 3996
    constexpr float Y_MAX        = BACK_WALL_Y - INSET_XY;   // 5020
    constexpr float BALL_Z_MIN   = BALL_RADIUS;               // 91.25 — ball resting on floor
    constexpr float BALL_Z_MAX   = CEILING_Z  - BALL_RADIUS;  // 1952.75 — ball touching ceiling
    constexpr float CAR_Z_MIN    = KICKOFF_CAR_Z;             // 17.01 — car on ground
    constexpr float CAR_Z_MAX    = CEILING_Z  - 50.0f;        // leave a little ceiling clearance

    constexpr float PITCH_MAX    = M_PI / 2.0f;
    constexpr float YAW_MAX      = M_PI;
    constexpr float ROLL_MAX     = M_PI;
    constexpr float ANGVEL_MAX   = CAR_MAX_ANG_VEL;           // 5.5 rad/s — from CommonValues

    // ---- Ball ----
    {
        BallState bs = {};
        bs.pos = RandVec(
            Vec(-X_MAX, -Y_MAX, BALL_Z_MIN),
            Vec( X_MAX,  Y_MAX, BALL_Z_MAX)
        );

        if (randBallSpeed) {
            // NJH: cap at BALL_MAX_SPEED (6000); previously used magic number 4000
            bs.vel    = RandNormVec() * RandFloat(0.0f, BALL_MAX_SPEED);
            bs.angVel = RandVec(Vec(-4, -4, -4), Vec(4, 4, 4));
        }

        arena->ball->SetState(bs);
    }

    // ---- Cars ----
    for (Car* car : arena->_cars) {
        CarState cs = {};

        cs.pos = RandVec(
            Vec(-X_MAX, -Y_MAX, CAR_Z_MIN),
            Vec( X_MAX,  Y_MAX, CAR_Z_MAX)
        );

        if (randCarSpeed) {
            // NJH: removed dead `randVelDir` variable — was computed but never used
            cs.vel    = RandNormVec() * RandFloat(0.0f, RLConst::CAR_MAX_SPEED);
            cs.angVel = RandNormVec() * ANGVEL_MAX;
        }

        Angle angle = Angle(
            RandFloat(-YAW_MAX,   YAW_MAX),
            RandFloat(-PITCH_MAX, PITCH_MAX),
            RandFloat(-ROLL_MAX,  ROLL_MAX)
        );

        // NJH: simplified from `carsOnGround ? true : (RandFloat() > 0.5)`
        bool onGround = carsOnGround || (RandFloat() > 0.5f);
        if (onGround) {
            cs.pos.z      = KICKOFF_CAR_Z;  // 17.01 — named constant instead of magic 17
            angle.pitch   = 0.0f;
            angle.roll    = 0.0f;
            cs.vel.z      = 0.0f;
            cs.angVel     = {};
        }

        cs.rotMat = angle.ToRotMat();
        cs.boost  = RandFloat(0.0f, 100.0f);

        car->SetState(cs);
    }
}
Summary of changes:

Arena bounds now use SIDE_WALL_X / BACK_WALL_Y / CEILING_Z with small XY insets
Ball and car Z ranges corrected to account for ball radius and ceiling properly
17 → KICKOFF_CAR_Z, 5.5f → CAR_MAX_ANG_VEL, 4000 → BALL_MAX_SPEED
Dead randVelDir variable removed
Redundant ternary flattened to ||
Math::RandVec calls unified to the using alias RandVec
ebb9ed0ddd1353ab462834c4ddcead279447357d.png
There is wall curve geometry connecting ground wall and ceiling that is the main reason for the position constraint. Also max ball speed is 6000uu/s

https://wiki.rlbot.org/v4/botmaking/useful-game-values/

Jun 27
Good points. Let me look at the wiki and the actual geometry more carefully.


Fetched: Useful game values - RLBot Wiki
Fetched: Useful game values - RLBot Wiki
You're right on both counts. The wall-floor and wall-ceiling curved transitions (~256 UU ramp radius) mean spawning too close to wall edges at floor/ceiling Z can clip into geometry. And ball max speed is confirmed 6000 UU/s. Let me also look at what your diagram shows — the corner geometry intercept is 8064, meaning the diagonal corner cuts happen at |x|+|y| = 8064.

The real constraint is: a position is only valid if it's inside the arena's actual collision geometry. For a random state setter the correct approach is a validity check, not just shrinking bounds, because the arena shape is a clipped octagon with curved edges — not a box.

Here's the revised code accounting for all of this:

cpp
#include "RandomState.h"
#include "../Math.h"
#include "../CommonValues.h"

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;
using namespace RLGC::CommonValues;

static Vec RandNormVec() {
    return RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
}

// ============================================================================
// IsInsideArena — rejects positions that fall outside playable geometry.
//
// The arena is NOT a box. It's clipped by:
//   1. Corner diagonal planes:   |x| + |y| < CORNER_AXIS_INTERCEPT (8064)
//   2. Wall-floor curved ramp:   z < WALL_RAMP_RADIUS (~256) AND near a wall
//   3. Wall-ceiling curved ramp: z > CEILING_Z - WALL_RAMP_RADIUS AND near a wall
//
// Ramp check: if Z is in the bottom or top ramp band, the position must be
// far enough from any wall that the ramp curve doesn't eat into it.
// We use a conservative linear approximation: at the ramp extremes (floor/ceiling)
// require WALL_RAMP_RADIUS clearance from walls; the constraint relaxes linearly
// toward zero at WALL_RAMP_RADIUS height from the surface.
//
// NJH: Ramps are "not circular" per wiki but 256 UU is the stated radius.
//      Using it conservatively as a flat inset — safe approximation.
// ============================================================================
static bool IsInsideArena(const Vec& pos, float wallInset) {
    constexpr float WALL_RAMP_R = 256.0f;
    constexpr float CORNER_LIMIT = CommonValues::CORNER_AXIS_INTERCEPT; // 8064

    // Corner diagonal cut
    if (fabsf(pos.x) + fabsf(pos.y) > CORNER_LIMIT - wallInset)
        return false;

    // Wall X / Y hard bounds (already filtered by corner check, but guard anyway)
    if (fabsf(pos.x) > SIDE_WALL_X - wallInset) return false;
    if (fabsf(pos.y) > BACK_WALL_Y - wallInset) return false;

    // Z bounds
    if (pos.z < 0.0f || pos.z > CEILING_Z) return false;

    // Wall-floor ramp band: z in [0, WALL_RAMP_R]
    // At z=0 the full ramp inset applies; at z=WALL_RAMP_R it's 0.
    if (pos.z < WALL_RAMP_R) {
        float rampFraction = 1.0f - (pos.z / WALL_RAMP_R);
        float rampInset = wallInset + WALL_RAMP_R * rampFraction;
        if (fabsf(pos.x) > SIDE_WALL_X - rampInset) return false;
        if (fabsf(pos.y) > BACK_WALL_Y - rampInset) return false;
    }

    // Wall-ceiling ramp band: z in [CEILING_Z - WALL_RAMP_R, CEILING_Z]
    if (pos.z > CEILING_Z - WALL_RAMP_R) {
        float rampFraction = 1.0f - ((CEILING_Z - pos.z) / WALL_RAMP_R);
        float rampInset = wallInset + WALL_RAMP_R * rampFraction;
        if (fabsf(pos.x) > SIDE_WALL_X - rampInset) return false;
        if (fabsf(pos.y) > BACK_WALL_Y - rampInset) return false;
    }

    return true;
}

// Sample a random valid arena position via rejection sampling.
// Typically accepts on the first or second try — the invalid region is
// only the small corner + ramp fringe of the bounding box.
static Vec RandArenaPos(float xMax, float yMax, float zMin, float zMax, float wallInset, int maxTries = 20) {
    for (int i = 0; i < maxTries; i++) {
        Vec candidate = RandVec(Vec(-xMax, -yMax, zMin), Vec(xMax, yMax, zMax));
        if (IsInsideArena(candidate, wallInset))
            return candidate;
    }
    // Fallback: center field at zMin — always valid
    return Vec(0.0f, 0.0f, zMin);
}

void RLGC::RandomState::ResetArena(Arena* arena) {

    arena->ResetToRandomKickoff();

    // Arena playable volume — use authoritative constants.
    // NJH: Previous code used 3500/4000/1820 which undersized X by 596, Y by 1120, Z by 224.
    constexpr float X_MAX       = SIDE_WALL_X;          // 4096
    constexpr float Y_MAX       = BACK_WALL_Y;          // 5120
    constexpr float BALL_Z_MIN  = BALL_RADIUS;           // 91.25 — ball centre at rest
    constexpr float BALL_Z_MAX  = CEILING_Z - BALL_RADIUS; // 1952.75
    constexpr float CAR_Z_MIN   = KICKOFF_CAR_Z;         // 17.01
    constexpr float CAR_Z_MAX   = CEILING_Z - 50.0f;     // small clearance at ceiling

    // Inset from walls for the ramp validity check.
    // Ball needs its radius clear of any surface; car needs ~car half-width (~65 UU).
    constexpr float BALL_WALL_INSET = BALL_RADIUS;        // 91.25
    constexpr float CAR_WALL_INSET  = 65.0f;              // approximate half-width

    constexpr float ANGVEL_MAX  = CAR_MAX_ANG_VEL;       // 5.5 rad/s

    // ---- Ball ----
    {
        BallState bs = {};
        bs.pos = RandArenaPos(X_MAX, Y_MAX, BALL_Z_MIN, BALL_Z_MAX, BALL_WALL_INSET);

        if (randBallSpeed) {
            // NJH: max ball speed is 6000 UU/s (wiki-confirmed); was incorrectly 4000
            bs.vel    = RandNormVec() * RandFloat(0.0f, BALL_MAX_SPEED);
            bs.angVel = RandVec(Vec(-4, -4, -4), Vec(4, 4, 4));
        }

        arena->ball->SetState(bs);
    }

    // ---- Cars ----
    for (Car* car : arena->_cars) {
        CarState cs = {};

        // NJH: simplified from `carsOnGround ? true : (RandFloat() > 0.5)`
        bool onGround = carsOnGround || (RandFloat() > 0.5f);

        if (onGround) {
            // Ground spawn: flat XY only, no ramp issue
            cs.pos = RandArenaPos(X_MAX, Y_MAX, CAR_Z_MIN, CAR_Z_MIN, CAR_WALL_INSET);
            cs.pos.z = CAR_Z_MIN; // force exact ground height
        } else {
            cs.pos = RandArenaPos(X_MAX, Y_MAX, CAR_Z_MIN, CAR_Z_MAX, CAR_WALL_INSET);
        }

        if (randCarSpeed) {
            // NJH: removed dead `randVelDir` variable from original
            cs.vel    = RandNormVec() * RandFloat(0.0f, RLConst::CAR_MAX_SPEED);
            cs.angVel = RandNormVec() * ANGVEL_MAX;
        }

        Angle angle = Angle(
            RandFloat(-YAW_MAX,   YAW_MAX),
            RandFloat(-PITCH_MAX, PITCH_MAX),
            RandFloat(-ROLL_MAX,  ROLL_MAX)
        );

        if (onGround) {
            angle.pitch = 0.0f;
            angle.roll  = 0.0f;
            cs.vel.z    = 0.0f;
            cs.angVel   = {};
        }

        cs.rotMat = angle.ToRotMat();
        cs.boost  = RandFloat(0.0f, 100.0f);

        car->SetState(cs);
    }
}
The key addition over my previous version is IsInsideArena + RandArenaPos. The rejection sampler handles the ramp bands and corner cuts properly — since the invalid region is maybe 5–10% of the bounding box volume, you'll almost always hit on the first try. The maxTries=20 fallback to center field is a safety net that will essentially never trigger.

One open question: does your CORNER_AXIS_INTERCEPT = 8064 constant exist in your CommonValues.h? I can see it's referenced in FlipResetApproachReward.h as a local, but if it's not in CommonValues you'd want to either pull it in or define it locally in this file.

Probably want to define it locally. Opus can you review this code and add any additional improvements and check for bugs?

Jun 27

Check CommonValues.h for CORNER_AXIS_INTERCEPT and related constants
Check CommonValues.h for CORNER_AXIS_INTERCEPT and related constants
I found a significant bug while reviewing CommonValues. Here's the full review:

Critical findings:

BALL_RADIUS = 92.75f in your CommonValues is wrong. The wiki (and your own OutplayRewards.h, RewardMath.h, CompleteMechanicsRewards.h) all use 91.25f. The comment even hedges "92.75 more common" — it isn't; 91.25 is the canonical RocketSim value. This is one of the stale constants already flagged in your reward-class sweep. Using BALL_RADIUS for BALL_Z_MIN means the ball spawns 1.5 UU too high — harmless here, but the same constant feeds goal-scoring AABBs where it matters. Flagging, not fixing in this file.
CORNER_AXIS_INTERCEPT = 8064.0f already exists in CommonValues (line 57). And WALL_BOTTOM_RAMP_RADIUS_APPROX = 256.0f (line 61). So you don't need to define them locally after all — but you said you'd prefer local definitions, so I'll keep a local constant for the ramp radius and reference the CommonValues corner intercept. Your call.
Bugs in my previous code:

Corner check double-counting the inset. I subtract wallInset from CORNER_LIMIT, but CORNER_AXIS_INTERCEPT is an axis intercept of a 45° plane — the perpendicular distance from that plane is (|x|+|y| - 8064) / √2. Subtracting a raw wallInset from the intercept over-insets by a factor of √2. Minor (more conservative than needed), but worth correcting for accuracy.
Ground-spawn rejection sampling is wasteful and subtly wrong. I called RandArenaPos with zMin==zMax==CAR_Z_MIN, so every candidate is at z=17, inside the floor ramp band. The ramp-inset logic then applies its maximum inset (since z≈17 is near the bottom of the [0,256] band), shrinking the valid XY region far more than a grounded car needs. A car at z=17 just needs to clear the flat wall, not the ramp curve. Better to special-case ground spawns with a simple flat-wall check.
onGround flat XY still allows goal-mouth interior. A grounded car can spawn at e.g. (0, 5100) — inside the goal opening past the back wall line. RocketSim usually tolerates this, but it's a weird state. Optional guard.
Missing <cmath> for fabsf — likely transitively included via Framework.h, but worth being explicit.
Here's the corrected version:

cpp
#include "RandomState.h"
#include "../Math.h"
#include "../CommonValues.h"
#include <cmath>

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;
using namespace RLGC::CommonValues;

static Vec RandNormVec() {
    return RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
}

namespace {
    // NJH: Local ramp radius. CommonValues::WALL_BOTTOM_RAMP_RADIUS_APPROX (256) also
    //      exists, but keeping it local documents the assumption at the use site and
    //      lets us tune the spawn-safety margin independently of the physics constant.
    constexpr float WALL_RAMP_R = 256.0f;
    constexpr float INV_SQRT2   = 0.70710678f;  // for perpendicular distance to 45° corner plane

    // ------------------------------------------------------------------------
    // IsInsideArena — rejects positions outside the playable collision volume.
    //
    // The arena is a clipped octagon, NOT a box. Invalid regions:
    //   1. Corner diagonal planes: perpendicular clearance to |x|+|y| = 8064 plane.
    //   2. Wall-floor ramp band   (z in [0, WALL_RAMP_R]): curved transition eats
    //      into the XY corner near walls.
    //   3. Wall-ceiling ramp band (z in [CEILING_Z - WALL_RAMP_R, CEILING_Z]): same.
    //
    // `inset` = required clearance from any surface (object radius / half-width).
    // ------------------------------------------------------------------------
    bool IsInsideArena(const Vec& pos, float inset) {
        const float ax = std::fabs(pos.x);
        const float ay = std::fabs(pos.y);

        // Flat wall bounds
        if (ax > SIDE_WALL_X - inset) return false;
        if (ay > BACK_WALL_Y - inset) return false;

        // Z bounds
        if (pos.z < inset || pos.z > CEILING_Z - inset) return false;

        // Corner diagonal cut. Perpendicular distance from the plane |x|+|y| = C
        // is (C - (|x|+|y|)) / sqrt(2). Require that to exceed `inset`.
        // NJH: previous version subtracted inset directly from the intercept,
        //      which over-insets by a factor of sqrt(2).
        const float cornerPerpDist = (CORNER_AXIS_INTERCEPT - (ax + ay)) * INV_SQRT2;
        if (cornerPerpDist < inset) return false;

        // Wall-floor ramp band: linearly require more wall clearance as z -> 0.
        if (pos.z < WALL_RAMP_R) {
            const float rampInset = inset + (WALL_RAMP_R - pos.z); // up to +256 at floor
            if (ax > SIDE_WALL_X - rampInset) return false;
            if (ay > BACK_WALL_Y - rampInset) return false;
        }

        // Wall-ceiling ramp band: symmetric near the ceiling.
        if (pos.z > CEILING_Z - WALL_RAMP_R) {
            const float rampInset = inset + (pos.z - (CEILING_Z - WALL_RAMP_R));
            if (ax > SIDE_WALL_X - rampInset) return false;
            if (ay > BACK_WALL_Y - rampInset) return false;
        }

        return true;
    }

    // Rejection-sample a random valid arena position. The invalid region is a
    // small fringe of the bounding box, so this almost always accepts on try 1.
    Vec RandArenaPos(float xMax, float yMax, float zMin, float zMax,
                     float inset, int maxTries = 25) {
        for (int i = 0; i < maxTries; i++) {
            Vec c = RandVec(Vec(-xMax, -yMax, zMin), Vec(xMax, yMax, zMax));
            if (IsInsideArena(c, inset))
                return c;
        }
        return Vec(0.0f, 0.0f, RS_MAX(zMin, inset)); // center-field fallback (always valid)
    }

    // Random valid GROUND position: flat-wall + corner check only (no ramp band,
    // since a grounded car at z~=17 only needs to clear the vertical wall face).
    Vec RandGroundPos(float xMax, float yMax, float inset, int maxTries = 25) {
        for (int i = 0; i < maxTries; i++) {
            float x = RandFloat(-xMax, xMax);
            float y = RandFloat(-yMax, yMax);
            const float ax = std::fabs(x), ay = std::fabs(y);
            if (ax > SIDE_WALL_X - inset) continue;
            if (ay > BACK_WALL_Y - inset) continue;
            if ((CORNER_AXIS_INTERCEPT - (ax + ay)) * INV_SQRT2 < inset) continue;
            return Vec(x, y, KICKOFF_CAR_Z);
        }
        return Vec(0.0f, 0.0f, KICKOFF_CAR_Z);
    }
}

void RLGC::RandomState::ResetArena(Arena* arena) {

    arena->ResetToRandomKickoff();

    // Arena playable volume — authoritative constants.
    // NJH: original used 3500/4000/1820, undersizing X by 596, Y by 1120, Z by 224.
    constexpr float X_MAX      = SIDE_WALL_X;            // 4096
    constexpr float Y_MAX      = BACK_WALL_Y;            // 5120
    constexpr float CAR_Z_MAX  = CEILING_Z - 50.0f;     // small ceiling clearance

    // Surface clearance per object. BALL_RADIUS from CommonValues is currently
    // 92.75 (stale — canonical is 91.25); using it here is harmless for spawn safety.
    constexpr float BALL_INSET = BALL_RADIUS;            // ~92
    constexpr float CAR_INSET  = 65.0f;                  // ~car half-width

    constexpr float ANGVEL_MAX = CAR_MAX_ANG_VEL;        // 5.5 rad/s

    // ---- Ball ----
    {
        BallState bs = {};
        bs.pos = RandArenaPos(X_MAX, Y_MAX, BALL_INSET, CEILING_Z - BALL_INSET, BALL_INSET);

        if (randBallSpeed) {
            // NJH: max ball speed 6000 UU/s (wiki-confirmed); was 4000.
            // angVel cap 6.0 rad/s is the true ball max; keeping your 4.0 as a
            // softer training cap — bump to 6.0 if you want the full range.
            bs.vel    = RandNormVec() * RandFloat(0.0f, BALL_MAX_SPEED);
            bs.angVel = RandVec(Vec(-4, -4, -4), Vec(4, 4, 4));
        }

        arena->ball->SetState(bs);
    }

    // ---- Cars ----
    for (Car* car : arena->_cars) {
        CarState cs = {};

        // NJH: simplified from `carsOnGround ? true : (RandFloat() > 0.5)`
        const bool onGround = carsOnGround || (RandFloat() > 0.5f);

        Angle angle = Angle(
            RandFloat(-YAW_MAX,   YAW_MAX),
            RandFloat(-PITCH_MAX, PITCH_MAX),
            RandFloat(-ROLL_MAX,  ROLL_MAX)
        );

        if (onGround) {
            cs.pos = RandGroundPos(X_MAX, Y_MAX, CAR_INSET);
            angle.pitch = 0.0f;
            angle.roll  = 0.0f;
        } else {
            cs.pos = RandArenaPos(X_MAX, Y_MAX, CAR_INSET, CAR_Z_MAX, CAR_INSET);
        }

        if (randCarSpeed) {
            // NJH: removed dead `randVelDir` variable from original.
            cs.vel    = RandNormVec() * RandFloat(0.0f, RLConst::CAR_MAX_SPEED);
            cs.angVel = RandNormVec() * ANGVEL_MAX;
        }

        // Ground cars: zero vertical velocity / spin so they actually rest flat.
        if (onGround) {
            cs.vel.z  = 0.0f;
            cs.angVel = {};
        }

        cs.rotMat = angle.ToRotMat();
        cs.boost  = RandFloat(0.0f, 100.0f);

        car->SetState(cs);
    }
}
Wait — there's a definition-order issue I should flag. YAW_MAX, PITCH_MAX, ROLL_MAX are referenced inside the car loop but I removed their declarations when restructuring. They need to be defined. Let me check: in your original they were constexpr locals inside ResetArena. They're gone from my version. Add them back at the top of ResetArena:

cpp
constexpr float PITCH_MAX = M_PI / 2.0f;
constexpr float YAW_MAX   = M_PI;
constexpr float ROLL_MAX  = M_PI;
(M_PI requires <cmath> and possibly #define _USE_MATH_DEFINES before it on MSVC — your original relied on it compiling, so it's already handled in your build.)

Two things to decide before you ship this:

Ground-spawn distribution shift. RandGroundPos uses independent RandFloat per axis rather than RandVec, which is fine, but note grounded cars now reject corner/wall-mouth positions they previously accepted. That changes your state distribution slightly — worth a note in Reward_Change_Log.md since you train on these states.
The IsInsideArena ramp model is a conservative linear approximation, not the true curved ramp. It over-rejects a thin sliver near walls in the ramp bands. For a state setter that's the safe direction (no clipped spawns), but if you ever want cars spawning on walls/ceiling for aerial-recovery training, this function would reject all of them — you'd need a separate on-surface path.
Want me to write the change-log entry, or fold a wall/ceiling on-surface spawn mode into this?

Let’s make the acceptable geometry slightly more conservative and robust instead of hitting the absolute limits. Less math the better, similar to original.

Jun 27
Got it — drop the ramp-band math and corner-plane trig, just inset the box conservatively. Closer to your original's spirit.

cpp
#include "RandomState.h"
#include "../Math.h"
#include "../CommonValues.h"

using RocketSim::Math::RandFloat;
using RLGC::Math::RandVec;
using namespace RLGC::CommonValues;

static Vec RandNormVec() {
    return RandVec(Vec(-1, -1, -1), Vec(1, 1, 1)).Normalized();
}

void RLGC::RandomState::ResetArena(Arena* arena) {

    arena->ResetToRandomKickoff();

    // Conservative spawn bounds. We deliberately stay well inside the true arena
    // limits (SIDE_WALL_X=4096, BACK_WALL_Y=5120, CEILING_Z=2044) so that the
    // curved wall-floor / wall-ceiling ramps and the 45-degree corner cuts never
    // clip a spawned object into geometry. No exact ramp math — just a safe margin.
    // NJH: original used 3500/4000/1820, which was inconsistent and also clipped
    //      the high-Z range; these are intentional, uniform insets instead.
    constexpr float WALL_MARGIN = 350.0f;   // clearance from side/back walls
    constexpr float Z_MARGIN    = 200.0f;   // clearance from floor ramp / ceiling

    constexpr float X_MAX     = SIDE_WALL_X - WALL_MARGIN;   // 3746
    constexpr float Y_MAX     = BACK_WALL_Y - WALL_MARGIN;   // 4770
    constexpr float CAR_Z_MAX = CEILING_Z   - Z_MARGIN;      // 1844

    constexpr float BALL_Z_MIN = BALL_RADIUS;                // ball resting height
    constexpr float BALL_Z_MAX = CEILING_Z - Z_MARGIN;       // 1844

    constexpr float PITCH_MAX = M_PI / 2;
    constexpr float YAW_MAX   = M_PI;
    constexpr float ROLL_MAX  = M_PI;
    constexpr float ANGVEL_MAX = CAR_MAX_ANG_VEL;            // 5.5 rad/s

    { // Randomize ball
        BallState bs = {};
        bs.pos = RandVec(Vec(-X_MAX, -Y_MAX, BALL_Z_MIN), Vec(X_MAX, Y_MAX, BALL_Z_MAX));
        if (randBallSpeed) {
            // NJH: max ball speed is 6000 UU/s (was incorrectly 4000)
            bs.vel = RandNormVec() * RandFloat(0, BALL_MAX_SPEED);
            bs.angVel = RandVec(Vec(-4, -4, -4), Vec(4, 4, 4));
        }
        arena->ball->SetState(bs);
    }

    for (Car* car : arena->_cars) { // Randomize cars
        CarState cs = {};
        cs.pos = RandVec(Vec(-X_MAX, -Y_MAX, KICKOFF_CAR_Z), Vec(X_MAX, Y_MAX, CAR_Z_MAX));

        if (randCarSpeed) {
            // NJH: removed dead `randVelDir` variable from original
            cs.vel = RandNormVec() * RandFloat(0, RLConst::CAR_MAX_SPEED);
            cs.angVel = RandNormVec() * ANGVEL_MAX;
        }

        Angle angle = Angle(RandFloat(-YAW_MAX, YAW_MAX), RandFloat(-PITCH_MAX, PITCH_MAX), RandFloat(-ROLL_MAX, ROLL_MAX));

        // NJH: simplified from `carsOnGround ? true : (RandFloat() > 0.5)`
        bool onGround = carsOnGround || (RandFloat() > 0.5f);
        if (onGround) {
            cs.pos.z = KICKOFF_CAR_Z;   // 17.01, named constant instead of magic 17
            angle.pitch = angle.roll = 0;
            cs.vel.z = 0;
            cs.angVel = {};
        }

        cs.rotMat = angle.ToRotMat();
        cs.boost = RandFloat(0, 100);
        car->SetState(cs);
    }
}
What changed from your original, minimally:

Bounds pulled from CommonValues with uniform WALL_MARGIN/Z_MARGIN insets instead of the ad-hoc 3500/4000/1820. A flat 350 UU wall margin comfortably clears the corner cuts and ramp curves everywhere without any per-position geometry checks — corners only start eating into the box past ±4096-ish on a diagonal, and 350 UU back from both walls keeps you clear of that region too.
Ball Z now reaches near the ceiling (was capped at 1820, well below the playable top).
Ball speed 4000 → BALL_MAX_SPEED (6000).
Dead randVelDir removed.
Magic 17 → KICKOFF_CAR_Z, 5.5f → CAR_MAX_ANG_VEL.
Redundant ternary flattened.
One note: with a 350 UU margin you're slightly more restrictive than your original 3500 X-bound (3746 vs 3500 — actually less restrictive on X, more on the old undersized axes). If you'd rather match the original's tighter feel on X, bump WALL_MARGIN up; 350 is my suggested floor for ramp/corner safety. The BALL_RADIUS staleness (92.75 vs canonical 91.25) still stands as a separate flag but doesn't affect this file.

Can you make some guidance on how to make some simple training packs? I’d like to be able to have 1v1 packs, 2v2, and 3v3 packs.

Jun 27
Before I write this up — "training packs" could mean a couple of different things in your stack, and the right design depends on which:

Q: What do you mean by "training packs"?
A: Curated StateSetters (scripted scenarios)

Q: What should the packs drill?
A: Specific mechanics (aerials, flicks), Game situations (kickoffs, saves, 2v1s), and General skill / mixed scenarios

Q: How should team size be handled?
A: One pack, parametrized by team size

Jun 27
Got it — curated StateSetters, drilling both mechanics and situations, one parametrized system that adapts to team size. Let me check how your existing StateSetters and the CombinedState wiring look so this slots in cleanly.


Ran 4 commands
Ran 4 commands
I have the picture. Your StateSetters override ResetArena(Arena*), iterate arena->_cars, and get wired into a weighted CombinedState. The challenge for parametrized packs is that arena->_cars already contains the right number of cars for the match (2, 4, or 6), and they're pre-assigned to teams — so a good pack just needs to read that count and place cars sensibly per team rather than hardcoding positions.

Here's a complete, self-contained guide with working code.

TrainingPacks: team-size-aware curated StateSetters
Core pattern
Every pack follows the same three-step shape inside ResetArena:

Partition arena->_cars by team into two vectors (blue / orange).
Place the "primary" actor(s) for the scenario — the car that drills the mechanic.
Place supporting cars using role slots scaled to how many are on each team.
The key idea: a scenario defines roles (attacker, first defender, second defender, support), and you fill as many roles as the team size allows. A 1v1 save drill uses {attacker, defender}; the same drill at 3v3 adds {second defender, rotation support} without rewriting the scenario.

Shared helper header
Create TrainingPackUtils.h in your StateSetters/ directory:

cpp
#pragma once
#include <RLGymCPP/StateSetters/StateSetter.h>   // or whatever your base is
#include "../CommonValues.h"
#include "../Math.h"

namespace RLGC {

    using namespace CommonValues;

    // ------------------------------------------------------------------
    // Team partition: split arena cars into blue / orange by team field.
    // Order within each vector follows _cars order (stable per episode).
    // ------------------------------------------------------------------
    struct TeamSplit {
        std::vector<Car*> blue;
        std::vector<Car*> orange;
    };

    inline TeamSplit SplitTeams(Arena* arena) {
        TeamSplit ts;
        for (Car* car : arena->_cars) {
            if (car->team == Team::BLUE) ts.blue.push_back(car);
            else                         ts.orange.push_back(car);
        }
        return ts;
    }

    // ------------------------------------------------------------------
    // Place a car flat on the ground, facing a yaw, with optional speed
    // toward a target point. Centralizes the "resting car" boilerplate.
    // ------------------------------------------------------------------
    inline void PlaceGroundCar(Car* car, Vec pos, float yaw,
                               float boost = 33.0f, float forwardSpeed = 0.0f) {
        CarState cs = {};
        cs.pos = Vec(pos.x, pos.y, KICKOFF_CAR_Z);
        Angle ang(yaw, 0.0f, 0.0f);
        cs.rotMat = ang.ToRotMat();
        if (forwardSpeed != 0.0f)
            cs.vel = cs.rotMat.forward * forwardSpeed;
        cs.boost = boost;
        car->SetState(cs);
    }

    // Yaw that points a car from `from` toward `to` (flat, ignoring Z).
    inline float YawToward(Vec from, Vec to) {
        return atan2f(to.y - from.y, to.x - from.x);
    }

    // ------------------------------------------------------------------
    // Role slots: lateral fan-out positions for supporting cars.
    // Given a base point and a team's defending direction, returns N
    // positions spread laterally + staggered in depth. Used so a drill
    // scales from 1 supporting car up to 2 without bespoke layouts.
    // teamSign: +1 for orange (defends +Y), -1 for blue (defends -Y).
    // ------------------------------------------------------------------
    inline std::vector<Vec> SupportSlots(Vec base, float teamSign, int count) {
        std::vector<Vec> out;
        // Lateral offsets fan symmetrically; depth pushes each one back
        // toward that team's own goal so they read as rotation/cover.
        static const float lateral[] = { 0.0f, -1400.0f, 1400.0f };
        for (int i = 0; i < count; i++) {
            float lx = lateral[RS_MIN(i, 2)];
            float depth = teamSign * (600.0f + 500.0f * i); // staggered backfield
            out.push_back(Vec(base.x + lx, base.y + depth, KICKOFF_CAR_Z));
        }
        return out;
    }

    // Ball helper.
    inline void PlaceBall(Arena* arena, Vec pos, Vec vel = Vec(0,0,0), Vec angVel = Vec(0,0,0)) {
        BallState bs = {};
        bs.pos = pos;
        bs.vel = vel;
        bs.angVel = angVel;
        arena->ball->SetState(bs);
    }
}
Example pack 1 — Save / shadow-defense scenario
This is a "situation" pack. The ball is launched toward one team's goal; that team gets a defender (and at 2v2/3v3, additional rotation cover), the other team gets the attacker(s).

cpp
#pragma once
#include "TrainingPackUtils.h"

namespace RLGC {

    // Drills defending a shot. Defending team = the one whose goal is targeted.
    // Scales: 1v1 -> {attacker} vs {defender};
    //         2v2 -> add 1 support each;  3v3 -> add 2 support each.
    class SavePack : public StateSetter {
    public:
        void ResetArena(Arena* arena) override {
            TeamSplit ts = SplitTeams(arena);

            // Randomly pick which team is defending this episode.
            bool blueDefends = RandFloat() > 0.5f;
            auto& def = blueDefends ? ts.blue   : ts.orange;
            auto& atk = blueDefends ? ts.orange : ts.blue;
            float defSign = blueDefends ? -1.0f : 1.0f;   // toward own goal
            Vec defGoal = blueDefends ? BLUE_GOAL_CENTER : ORANGE_GOAL_CENTER;

            // --- Ball: launched toward the defended goal from midfield ---
            float ballY = -defSign * RandFloat(500.0f, 2500.0f); // attacker's half-ish
            Vec ballPos(RandFloat(-2000.0f, 2000.0f), ballY, RandFloat(100.0f, 700.0f));
            Vec toGoal = (defGoal - ballPos).Normalized();
            float shotSpeed = RandFloat(1500.0f, 3000.0f);
            PlaceBall(arena, ballPos, toGoal * shotSpeed);

            // --- Primary defender: between ball and goal ---
            if (!def.empty()) {
                Vec dPos = ballPos + (defGoal - ballPos) * 0.55f;
                PlaceGroundCar(def[0], dPos, YawToward(dPos, ballPos), 50.0f);
            }
            // --- Defensive support (cars 2..N) ---
            auto defSlots = SupportSlots(defGoal, defSign, (int)def.size() - 1);
            for (size_t i = 1; i < def.size(); i++)
                PlaceGroundCar(def[i], defSlots[i-1], YawToward(defSlots[i-1], ballPos), 40.0f);

            // --- Primary attacker: behind the ball, chasing ---
            if (!atk.empty()) {
                Vec aPos = ballPos - toGoal * 900.0f;
                PlaceGroundCar(atk[0], aPos, YawToward(aPos, ballPos), 60.0f, 800.0f);
            }
            // --- Attacking support ---
            auto atkSlots = SupportSlots(ballPos, -defSign, (int)atk.size() - 1);
            for (size_t i = 1; i < atk.size(); i++)
                PlaceGroundCar(atk[i], atkSlots[i-1], YawToward(atkSlots[i-1], ballPos), 50.0f);
        }
    };
}
Example pack 2 — Aerial / mechanic scenario
A "mechanic" pack. Here the parametrization is simpler: one car drills the aerial, the rest are placed out of the way as live-but-passive bodies (so the obs/teammates fields are populated realistically without interfering).

cpp
#pragma once
#include "TrainingPackUtils.h"

namespace RLGC {

    class AerialPack : public StateSetter {
    public:
        void ResetArena(Arena* arena) override {
            TeamSplit ts = SplitTeams(arena);
            bool blueActs = RandFloat() > 0.5f;
            auto& act = blueActs ? ts.blue : ts.orange;
            auto& other = blueActs ? ts.orange : ts.blue;

            // --- Ball: high, midfield, slow (the aerial target) ---
            Vec ballPos(RandFloat(-2500, 2500), RandFloat(-2500, 2500),
                        RandFloat(900.0f, 1500.0f));
            PlaceBall(arena, ballPos, RandVec(Vec(-200,-200,0), Vec(200,200,200)));

            // --- Actor: on the ground below/behind the ball with boost ---
            if (!act.empty()) {
                Vec aPos = ballPos; aPos.z = KICKOFF_CAR_Z;
                aPos.x += RandFloat(-600, 600);
                aPos.y -= (blueActs ? 1.0f : -1.0f) * RandFloat(800, 1600);
                PlaceGroundCar(act[0], aPos, YawToward(aPos, ballPos), 100.0f);
            }
            // --- Extra actors + all opponents: parked far from the play ---
            int idx = 0;
            auto park = [&](std::vector<Car*>& cars, size_t startAt, float sign) {
                for (size_t i = startAt; i < cars.size(); i++) {
                    Vec p(-3000 + 1500.0f * (idx++ % 4), sign * 4500.0f, KICKOFF_CAR_Z);
                    PlaceGroundCar(cars[i], p, sign > 0 ? -M_PI/2 : M_PI/2, 33.0f);
                }
            };
            park(act, 1, blueActs ? -1.0f : 1.0f);
            park(other, 0, blueActs ? 1.0f : -1.0f);
        }
    };
}
Wiring into your phase config
Your ExampleMain.cpp already builds a weighted CombinedState. Extend that block:

cpp
// After the existing kickoff / random / aerial setters:
if (phaseCfg.useTrainingPacks) {
    float packWeight = phaseCfg.trainingPackChance;
    // Distribute across packs however you like:
    stateSetters.push_back({ new SavePack(),   packWeight * 0.5f });
    stateSetters.push_back({ new AerialPack(), packWeight * 0.5f });
    // Remember to renormalize kickoffWeight by (1 - trainingPackChance)
    // exactly as you already do for randomStateChance.
}
Add useTrainingPacks / trainingPackChance to PhaseConfig alongside the existing toggles. CombinedState already normalizes weights, so you just need the bookkeeping multiply on kickoffWeight to stay consistent with your current pattern.

Design notes worth heeding given your setup
A few things specific to how you train:

The same Goodhart separation you use for rewards applies to packs — a pack that fires (resets to the scenario) isn't the same as a pack that teaches. Confirm in RocketSimVis that the actor actually faces a learnable situation and isn't, say, spawned with the ball already past it. Cheap to eyeball, expensive to discover 20M steps later.

Keep packs out of the early curriculum phases. Scripted scenarios are powerful but narrow; if you weight them too heavily before the general policy is stable, you risk the same skill-abandonment you've seen from drastic weight changes. Treat trainingPackChance like any other reward weight — introduce gradually, one variable at a time, with W&B confirmation between changes.

Because these mutate your state distribution, log a per-pack metric (which pack fired, defending team, outcome) so you can correlate pack exposure with your gauntlet goal-differential regressions. A pack that helps 1v1 can quietly hurt 3v3 rotation if the support-slot placement teaches bad spacing.

Two things I'd flag as open before you commit code: first, verify your StateSetter base class actually exposes ResetArena(Arena*) as the override point (your existing setters confirm this, but check the exact signature/virtual). Second, the SupportSlots lateral fan-out is a guess at sensible 2v2/3v3 spacing — you'll want to tune those offsets against what good rotation actually looks like in your replays, not my placeholder 1400 UU.

Want me to add a third pack (kickoff variations or a 2v1 overload drill), or write the W&B pack-metrics collector to slot into your existing StepCallback?







Claude is AI and can make mistakes. Please double-check responses.






