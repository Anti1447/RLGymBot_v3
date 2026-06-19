#pragma once
// ============================================================================
// KickoffCounterReward
// 2026-06-04 | Rev 1.0 | NJH
//
// Category: PREDICTION / POSITIONING
//
// Win the kickoff EXCHANGE by countering. When the OPPOSING team gets the first
// touch on a kickoff, reward our player for meeting the ball a beat later and
// redirecting it back toward the opponent's goal within a short counter window.
// Reward scales with how COMPLETELY we countered: alignment toward their goal x
// resulting ball speed. A weak sideways tap pays little; a hard, on-target
// redirect pays full.
//
// Plus a tiny pre-touch "read & position to react" shaping term, active only at
// the very start of a kickoff and only for the player who is NOT the favorite to
// reach the ball first (i.e. the counter-er). Rewards staying goal-side and
// oriented to react rather than blindly committing.
//
// Opponent-agnostic (fires regardless of how the opponent approaches), one-shot
// per player per kickoff, ZeroSum-wrapped in config (a won kickoff exchange is
// directly competitive). Supersedes ReactiveKickoffReward.
//
// Timing follows CLAUDE.md 7C: epoch clock accumulated in PreStep only, never in
// GetReward (which runs once per player and would inflate in 2v2/3v3).
// ============================================================================

#include "Reward.h"
#include "AdvancedKickoffRewards.h"   // KickoffUtil::IsKickoff / OppGoal / ForwardSign
#include <atomic>
#include <unordered_map>

namespace RLGC {

	// NOTE: named KickoffCounterHitReward (not KickoffCounterReward) — a different,
	// older KickoffCounterReward (possession+goal flavored) already exists in
	// CommonRewards.h. This one is the opponent-agnostic counter-HIT/redirect reward.
	class KickoffCounterHitReward : public Reward {
	private:
		// --- tunables ---
		static constexpr float COUNTER_WINDOW  = 0.8f;     // s after opp first touch to land our counter
		static constexpr float MIN_ALIGN       = 0.3f;     // min ballVel-toward-their-goal to count as a counter
		static constexpr float REF_BALL_SPEED  = 2300.0f;  // ball speed (UU/s) that earns full speed factor
		static constexpr float SPEED_FACTOR_MAX= 1.3f;     // cap on speed factor
		static constexpr float COUNTER_BASE    = 1.0f;     // base event reward (final scale via WeightedReward)
		static constexpr float SHAPE_SCALE     = 0.02f;    // pre-touch read shaping magnitude (tiny; cut first if it conflicts)
		static constexpr float MAX_EPOCH       = 4.0f;     // hard cap on epoch length (s)

		// --- per-kickoff global state (managed in PreStep) ---
		bool     koEpochOpen       = false;
		float    koTime            = 0.0f;
		bool     firstTouchRecorded= false;
		int      firstTouchTeam    = -1;
		float    firstTouchTime    = 0.0f;
		uint32_t firstTouchCarId   = 0;

		// --- per-player one-shot guard ---
		mutable std::unordered_map<uint32_t, bool> paidCounter;

		// --- metrics (thread-safe across parallel envs) ---
		inline static std::atomic<int>       s_oppFirstTouch { 0 };
		inline static std::atomic<int>       s_won           { 0 };
		inline static std::atomic<long long> s_alignMilli    { 0 };  // x1000
		inline static std::atomic<long long> s_speedMilli    { 0 };  // x1000 (UU/s)

	public:
		struct Metrics {
			int    oppFirstTouch = 0;
			int    won           = 0;
			double avgAlign      = 0.0;
			double avgBallSpeed  = 0.0;
		};

		static Metrics GetAndResetMetrics() {
			Metrics m;
			m.oppFirstTouch = s_oppFirstTouch.exchange(0);
			m.won           = s_won.exchange(0);
			long long alignRaw = s_alignMilli.exchange(0);
			long long speedRaw = s_speedMilli.exchange(0);
			if (m.won > 0) {
				m.avgAlign     = double(alignRaw) / (1000.0 * m.won);
				m.avgBallSpeed = double(speedRaw) / (1000.0 * m.won);
			}
			return m;
		}

		virtual void Reset(const GameState& initialState) override {
			koEpochOpen        = false;
			koTime             = 0.0f;
			firstTouchRecorded = false;
			firstTouchTeam     = -1;
			firstTouchTime     = 0.0f;
			firstTouchCarId    = 0;
			paidCounter.clear();
		}

		virtual void PreStep(const GameState& state) override {
			float dt = (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
			bool isKickoffNow = KickoffUtil::IsKickoff(state);

			// Rising edge: open a new kickoff epoch
			if (isKickoffNow && !koEpochOpen) {
				koEpochOpen        = true;
				koTime             = 0.0f;
				firstTouchRecorded = false;
				firstTouchTeam     = -1;
				firstTouchTime     = 0.0f;
				firstTouchCarId    = 0;
				paidCounter.clear();
			}

			if (!koEpochOpen)
				return;

			koTime += dt;

			// Record the very first touch of the kickoff (either team)
			if (!firstTouchRecorded) {
				for (const Player& p : state.players) {
					if (p.ballTouchedStep) {
						firstTouchRecorded = true;
						firstTouchCarId    = p.carId;
						firstTouchTeam     = (int)p.team;
						firstTouchTime     = koTime;
						s_oppFirstTouch.fetch_add(1);  // one per kickoff that gets a first touch
						break;
					}
				}
			}

			// Close the epoch once the counter window is spent (or we time out)
			if (koTime > MAX_EPOCH ||
				(firstTouchRecorded && koTime > firstTouchTime + COUNTER_WINDOW)) {
				koEpochOpen = false;
			}
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (player.isDemoed)
				return 0.0f;

			uint32_t id = player.carId;

			// --- Counter event: opp touched first, we redirect toward their goal in-window ---
			if (koEpochOpen && firstTouchRecorded &&
				firstTouchTeam != (int)player.team &&
				!paidCounter[id] &&
				player.ballTouchedStep &&
				(koTime - firstTouchTime) <= COUNTER_WINDOW) {

				Vec dir   = (KickoffUtil::OppGoal(player) - state.ball.pos).Normalized();
				float align = state.ball.vel.Normalized().Dot(dir);

				if (align > MIN_ALIGN) {
					float ballSpeed = state.ball.vel.Length();
					float speedF    = RS_CLAMP(ballSpeed / REF_BALL_SPEED, 0.0f, SPEED_FACTOR_MAX);
					float reward    = COUNTER_BASE * RS_CLAMP(align, 0.0f, 1.0f) * speedF;

					paidCounter[id] = true;
					s_won.fetch_add(1);
					s_alignMilli.fetch_add((long long)(align * 1000.0f));
					s_speedMilli.fetch_add((long long)(ballSpeed * 1000.0f));
					return reward;  // event dominates this step
				}
			}

			// --- Pre-touch read shaping: only at kickoff start, before any touch,
			//     and only for the counter-er (an opponent is closer to the ball) ---
			if (KickoffUtil::IsKickoff(state) && !firstTouchRecorded) {
				float myDist = (player.pos - state.ball.pos).Length();
				bool oppCloser = false;
				for (const Player& o : state.players) {
					if (o.team == player.team) continue;
					if ((o.pos - state.ball.pos).Length() < myDist) { oppCloser = true; break; }
				}
				if (oppCloser) {
					// goal-side: staged between ball and our own net to react
					float ownSign  = -KickoffUtil::ForwardSign(player);  // toward own goal in Y
					float goalSide = RS_CLAMP((player.pos.y - state.ball.pos.y) * ownSign / 1000.0f, 0.0f, 1.0f);
					// facing the ball: oriented to challenge wherever it pops
					Vec toBall  = (state.ball.pos - player.pos).Normalized();
					float facing = RS_CLAMP(player.rotMat.forward.Dot(toBall), 0.0f, 1.0f);
					return SHAPE_SCALE * goalSide * facing;
				}
			}

			return 0.0f;
		}
	};

}
