#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <cstdlib>
#include "../Framework.h"

namespace RLGC {

enum class KickoffMode {
    HARDCODED,
    STOCHASTIC
};

// Which of the 5 standard kickoff spawn slots the player is in,
// expressed relative to the player's own team perspective so that
// LEFT always means the same physical side regardless of team colour.
enum class KickoffPosition {
    LEFT_DIAGONAL  = 0,   // team-left corner  (~-2048, ±2560)
    LEFT_MID       = 1,   // team-left mid     (~-256,  ±3840)
    REAR           = 2,   // centre back       (~0,     ±4608)
    RIGHT_MID      = 3,   // team-right mid    (~+256,  ±3840)
    RIGHT_DIAGONAL = 4,   // team-right corner (~+2048, ±2560)
    UNKNOWN        = 5
};

enum class KickoffVariant {
    NORMAL  = 0,   // standard position-specific speed flip
    DELAYED = 1,   // normal + 3-frame slight brake, then hand back to NN
    FAKE    = 2    // diagonal only: weaker flip (no boost on dodge), bail toward own net
};

// Per-player state tracked across steps while a kickoff is active.
struct PlayerKickoffState {
    KickoffPosition position   = KickoffPosition::UNKNOWN;
    KickoffVariant  variant    = KickoffVariant::NORMAL;
    int             frameIndex = -1;  // -1 = uninitialised / waiting for kickoff start
                                      // -2 = not this player's kickoff (use NN)
};

class KickoffController {
    // Action frame: throttle, steer, pitch, yaw, roll, jump, boost, handbrake
    using Frame = std::array<float, 8>;
    using Seq   = std::vector<Frame>;

    // seqs[position][variant]  (5 positions × 3 variants)
    Seq seqs[5][3];
    KickoffMode mode_;

    // -----------------------------------------------------------------------
    // Sequence builder
    // -----------------------------------------------------------------------
    static Seq MakeDelayed(const Seq& base) {
        Seq d = base;
        // 3 frames of gentle brake so the bot pauses before NN resumes
        const Frame brake = {-0.3f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        d.push_back(brake);
        d.push_back(brake);
        d.push_back(brake);
        return d;
    }

    // Build one speed-flip sequence parameterised by approach steer direction.
    //   preSteer  : steer/yaw sign used in the 2-frame pre-position phase
    //   flipSteer : steer/yaw sign used for the actual dodge
    //   initFrames: frames of straight boost before the pre-position phase
    //   recRoll   : roll sign during the 2-frame recovery
    static Seq MakeNormal(float preSteer, float flipSteer, int initFrames, float recRoll) {
        Seq s;
        // initial boost toward ball
        for (int i = 0; i < initFrames; i++)
            s.push_back({1, 0,  0,  0, 0, 0, 1, 0});
        // pre-position: steer to set up the dodge direction
        for (int i = 0; i < 2; i++)
            s.push_back({1, preSteer, 0, preSteer, 0, 0, 1, 0});
        // first jump (2 frames)
        for (int i = 0; i < 2; i++)
            s.push_back({1, 0, 0, 0, 0, 1, 1, 0});
        // dead frame (release jump button, car rises)
        s.push_back({1, 0, 0, 0, 0, 0, 1, 0});
        // speed-flip dodge: forward pitch + lateral steer
        s.push_back({1, flipSteer, -0.7f, flipSteer, 0, 1, 1, 0});
        // flip cancel: nose up, maintain boost
        for (int i = 0; i < 6; i++)
            s.push_back({1, 0, 1, 0, 0, 0, 1, 0});
        // recovery
        for (int i = 0; i < 2; i++)
            s.push_back({1, 0, 0.5f, 0, recRoll, 0, 0, 0});
        return s;
    }

    void BuildSequences() {
        // -------------------------------------------------------------------
        // LEFT_DIAGONAL (pos 0)
        // Ball is ~38° to the car's right.
        // Pre-position steer: left (-1), flip: forward-right (+0.8).
        // -------------------------------------------------------------------
        {
            Seq normal = MakeNormal(-1.f, 0.8f, 5, 1.f);
            // 19 frames total: 5+2+2+1+1+6+2

            // FAKE: same approach but no boost on the dodge itself; after the
            // flip cancel bail left (back toward own net).
            Seq fake;
            for (int i = 0; i < 5; i++) fake.push_back({1,  0.f,   0,    0.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) fake.push_back({1, -1.f,   0,   -1.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) fake.push_back({1,  0.f,   0,    0.f,  0, 1, 1, 0});
            fake.push_back(                             {1,  0.f,   0,    0.f,  0, 0, 1, 0});
            fake.push_back(                             {1,  0.5f,-0.5f,  0.5f, 0, 1, 0, 0}); // weaker, no boost
            for (int i = 0; i < 3; i++) fake.push_back({1,  0.f,   1.f,  0.f,  0, 0, 0, 0}); // cancel no boost
            for (int i = 0; i < 5; i++) fake.push_back({0.5f,-1.f, 0,   -0.5f, 0, 0, 0, 0}); // bail left
            for (int i = 0; i < 3; i++) fake.push_back({0.f,-1.f,  0,    0.f,  0, 0, 0, 0}); // continue
            // 5+2+2+1+1+3+5+3 = 22 frames

            seqs[0][0] = normal;
            seqs[0][1] = MakeDelayed(normal);
            seqs[0][2] = fake;
        }

        // -------------------------------------------------------------------
        // LEFT_MID (pos 1)
        // Ball is ~4° to the car's right (nearly straight ahead).
        // Slight right steer throughout approach; modest flip right.
        // -------------------------------------------------------------------
        {
            Seq normal;
            for (int i = 0; i < 7; i++) normal.push_back({1, 0.2f, 0,    0.2f, 0, 0, 1, 0});
            for (int i = 0; i < 2; i++) normal.push_back({1, 0.f,  0,    0.f,  0, 1, 1, 0});
            normal.push_back(                             {1, 0.f,  0,    0.f,  0, 0, 1, 0});
            normal.push_back(                             {1, 0.5f,-0.7f, 0.5f, 0, 1, 1, 0});
            for (int i = 0; i < 6; i++) normal.push_back({1, 0.f,  1.f,  0.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) normal.push_back({1, 0.f,  0.5f, 0.f,  0.5f, 0, 0, 0});
            // 7+2+1+1+6+2 = 19 frames

            seqs[1][0] = normal;
            seqs[1][1] = MakeDelayed(normal);
            seqs[1][2] = MakeDelayed(normal); // no FAKE for mid — use DELAYED
        }

        // -------------------------------------------------------------------
        // REAR (pos 2)
        // Ball is dead straight, 4608 units away.
        // Longer initial boost phase before flip to build more speed.
        // -------------------------------------------------------------------
        {
            Seq normal;
            for (int i = 0; i < 9; i++) normal.push_back({1, 0.f,  0,    0.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) normal.push_back({1, 0.f,  0,    0.f,  0, 1, 1, 0});
            normal.push_back(                             {1, 0.f,  0,    0.f,  0, 0, 1, 0});
            normal.push_back(                             {1, 0.3f,-0.7f, 0.3f, 0, 1, 1, 0});
            for (int i = 0; i < 6; i++) normal.push_back({1, 0.f,  1.f,  0.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) normal.push_back({1, 0.f,  0.5f, 0.f,  0.5f, 0, 0, 0});
            // 9+2+1+1+6+2 = 21 frames

            seqs[2][0] = normal;
            seqs[2][1] = MakeDelayed(normal);
            seqs[2][2] = MakeDelayed(normal); // no FAKE for rear
        }

        // -------------------------------------------------------------------
        // RIGHT_MID (pos 3) — mirror of LEFT_MID
        // -------------------------------------------------------------------
        {
            Seq normal;
            for (int i = 0; i < 7; i++) normal.push_back({1,-0.2f, 0,   -0.2f, 0, 0, 1, 0});
            for (int i = 0; i < 2; i++) normal.push_back({1, 0.f,  0,    0.f,  0, 1, 1, 0});
            normal.push_back(                             {1, 0.f,  0,    0.f,  0, 0, 1, 0});
            normal.push_back(                             {1,-0.5f,-0.7f,-0.5f, 0, 1, 1, 0});
            for (int i = 0; i < 6; i++) normal.push_back({1, 0.f,  1.f,  0.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) normal.push_back({1, 0.f,  0.5f, 0.f, -0.5f, 0, 0, 0});

            seqs[3][0] = normal;
            seqs[3][1] = MakeDelayed(normal);
            seqs[3][2] = MakeDelayed(normal);
        }

        // -------------------------------------------------------------------
        // RIGHT_DIAGONAL (pos 4) — mirror of LEFT_DIAGONAL
        // Pre-position steer: right (+1), flip: forward-left (-0.8).
        // -------------------------------------------------------------------
        {
            Seq normal = MakeNormal(1.f, -0.8f, 5, -1.f);

            Seq fake;
            for (int i = 0; i < 5; i++) fake.push_back({1,  0.f,   0,    0.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) fake.push_back({1,  1.f,   0,    1.f,  0, 0, 1, 0});
            for (int i = 0; i < 2; i++) fake.push_back({1,  0.f,   0,    0.f,  0, 1, 1, 0});
            fake.push_back(                             {1,  0.f,   0,    0.f,  0, 0, 1, 0});
            fake.push_back(                             {1, -0.5f,-0.5f, -0.5f, 0, 1, 0, 0});
            for (int i = 0; i < 3; i++) fake.push_back({1,  0.f,   1.f,  0.f,  0, 0, 0, 0});
            for (int i = 0; i < 5; i++) fake.push_back({0.5f, 1.f, 0,    0.5f, 0, 0, 0, 0});
            for (int i = 0; i < 3; i++) fake.push_back({0.f,  1.f, 0,    0.f,  0, 0, 0, 0});

            seqs[4][0] = normal;
            seqs[4][1] = MakeDelayed(normal);
            seqs[4][2] = fake;
        }
    }

public:
    explicit KickoffController(KickoffMode mode = KickoffMode::HARDCODED)
        : mode_(mode)
    {
        BuildSequences();
    }

    KickoffMode GetMode() const { return mode_; }

    // Ball within 100 units of field centre = kickoff countdown / fresh kickoff.
    bool ShouldBeginKickoff(const GameState& gs) const {
        return std::abs(gs.ball.pos.x) < 50.f &&
               std::abs(gs.ball.pos.y) < 50.f &&
               gs.ball.pos.z < 100.f;
    }

    // True if this player is the kickoff taker for their team.
    // "Taker" = closest to ball on the same team.
    // Tiebreaker: leftmost player wins (min X for blue, max X for orange).
    bool IsPlayerTaker(const Player& player, const GameState& gs) const {
        float myDist = std::hypot(player.pos.x - gs.ball.pos.x,
                                  player.pos.y - gs.ball.pos.y);
        // team-normalised "leftness": lower value = more to the left
        float myLeft = (player.team == Team::BLUE) ? player.pos.x : -player.pos.x;

        for (const auto& other : gs.players) {
            if ((int)other.team != (int)player.team || other.index == player.index)
                continue;
            float theirDist = std::hypot(other.pos.x - gs.ball.pos.x,
                                         other.pos.y - gs.ball.pos.y);
            float theirLeft = (other.team == Team::BLUE) ? other.pos.x : -other.pos.x;
            if (theirDist < myDist - 1.f)
                return false;  // clearly closer teammate
            if (std::abs(theirDist - myDist) <= 1.f && theirLeft < myLeft)
                return false;  // tied on distance, they're more to the left
        }
        return true;
    }

    // Which of the 5 spawn slots is this player in?
    // Uses team-normalised X so LEFT always means the car's own left side.
    KickoffPosition DetectPosition(const Player& player) const {
        float normalX = (player.team == Team::BLUE) ? player.pos.x : -player.pos.x;
        float absY    = std::abs(player.pos.y);

        if (absY > 4200.f)
            return KickoffPosition::REAR;
        if (absY > 3500.f)
            return (normalX < 0.f) ? KickoffPosition::LEFT_MID : KickoffPosition::RIGHT_MID;
        return (normalX < 0.f) ? KickoffPosition::LEFT_DIAGONAL : KickoffPosition::RIGHT_DIAGONAL;
    }

    // Random variant selection weighted by position type.
    // Diagonals: 50 % NORMAL  |  25 % DELAYED  |  25 % FAKE
    // Mid / Rear: 75 % NORMAL  |  25 % DELAYED
    KickoffVariant SelectVariant(KickoffPosition pos) const {
        int r = std::rand() % 100;
        bool isDiag = (pos == KickoffPosition::LEFT_DIAGONAL ||
                       pos == KickoffPosition::RIGHT_DIAGONAL);
        if (isDiag) {
            if (r < 50) return KickoffVariant::NORMAL;
            if (r < 75) return KickoffVariant::DELAYED;
            return KickoffVariant::FAKE;
        } else {
            if (r < 75) return KickoffVariant::NORMAL;
            return KickoffVariant::DELAYED;
        }
    }

    int GetSequenceLength(KickoffPosition pos, KickoffVariant var) const {
        if (pos == KickoffPosition::UNKNOWN) return 0;
        return (int)seqs[(int)pos][(int)var].size();
    }

    // Returns the action for frame `frameIndex` with team mirroring applied.
    Action GetAction(KickoffPosition pos, KickoffVariant var, int frameIndex, Team team) const {
        Frame arr = seqs[(int)pos][(int)var][frameIndex];
        if (team == Team::ORANGE) {
            arr[1] = -arr[1];  // steer
            arr[3] = -arr[3];  // yaw
            arr[4] = -arr[4];  // roll
        }
        Action a;
        a.throttle  = arr[0];
        a.steer     = arr[1];
        a.pitch     = arr[2];
        a.yaw       = arr[3];
        a.roll      = arr[4];
        a.jump      = arr[5] > 0.5f;
        a.boost     = arr[6] > 0.5f;
        a.handbrake = arr[7] > 0.5f;
        return a;
    }

    // Reset per-player kickoff state (call on episode start or between kickoffs).
    static void ResetStates(std::vector<PlayerKickoffState>& states) {
        for (auto& s : states)
            s = PlayerKickoffState{};
    }
};

} // namespace RLGC
