# Mechanic Spec — Tendency Memory Blocks A+B

**Date:** 2026-05-18  
**Author:** NJH  
**Target:** New bot, larger obs (261 baseline), tickSkip=4, network (1024,1024) shared / (1024,1024) policy  
**Status:** Spec — implementation-ready  
**Phase gating:** Active starting Phase 5 Diamond. Slots present but zero-filled in Phases 1–4.

---

## 1. Hypothesis

**Claim:** A compact set of hand-crafted summary features that encode short-to-medium-term opponent tendencies (Block A) and self/team flow state (Block B) will reduce the gradient-dilution burden on the shared trunk by giving it pre-computed temporal context. This frees the trunk's capacity to learn higher-order decision patterns rather than re-deriving "what just happened" from raw obs each step.

**Mechanism:** Frame stacking would supply temporal info implicitly but inefficiently (4× input replication, gradient smeared across copies). Hand-crafted summaries deliver the same information density in 16 floats with direct gradient flow to interpretable concepts.

**Expected behavioral effect at Phase 5+:**
- Bot adapts pressure response to opponent commitment level (back off vs. challenge)
- Possession-aware play (knows when to dribble vs. clear based on team flow state)
- Better long-horizon decisions (knows ball just left aerial regime)
- Reduced fixation on raw ball position when team state is the real driver

**This is NOT trying to solve:**
- Train→live distribution shift (deferred; library architecture would handle this post-fall)
- Individual opponent recognition (team-level granularity is sufficient for self-play)
- Long-horizon credit assignment beyond ~30 seconds

---

## 2. Superseded / Complemented Features

This is a pure obs addition. **No existing reward or obs feature is replaced.** It complements:

- `AdvancedObsPadded` existing temporal hints: `demoTimers`, `prevAction`, `padTimers` blend
- `KickoffRandomizationReward` history tracking (reward-side only; this is obs-side)
- Anything that currently has to re-derive ball state from one frame

Future rewards MAY consume tendency state via a shared observer interface (Stage 2+, out of scope here).

---

## 3. Gating Conditions

### Phase Gate (Hard)

- Phases 1–4 (Bronze through Platinum): `memoryBlocksEnabled = false`. Obs slots are present but zero-filled. Network sees consistent input shape but learns to ignore the inactive slots.
- Phase 5+ (Diamond onward): `memoryBlocksEnabled = true`. Slots populated with real values.

**Why zero-fill instead of omit:** Keeps the obs vector shape invariant across the entire training run. Checkpoints load cleanly across phase transitions. A/B testing on Machine 2 requires only a flag toggle, no rebuild.

### Cold-Start Gate (Soft, via confidence feature)

Within an active episode, the `confidence` scalar in Block A ramps from 0 → 1 as `sampleCount / 300` (≈10 seconds of samples at tickSkip=4). The network learns to discount low-confidence tendency values.

### Reset Conditions

All trackers reset on:
- `Reset(initialState)` — full episode boundary
- Detection that `state.elapsedTime < lastUpdateTime` (state rollback, also episode boundary)
- carId reassignment is NOT a reset trigger for team-level trackers (carIds may shuffle within a team without changing team behavior)

---

## 4. Feature Semantics (Block A — Opponent Team Tendency, 8 features)

All Block A features are tracked from the **perspective player's viewpoint** — "opponent team" means the team the perspective player is NOT on. Each obs builder instance is per-player, so perspective is fixed.

| # | Feature | Type | Window | α | Output Range |
|---|---|---|---|---|---|
| A1 | `commitmentEMA` | EMA on binary | 10s | 0.010 | [0, 1] |
| A2 | `aerialEMA` | EMA on fraction | 10s | 0.010 | [0, 1] |
| A3 | `depthMeanEMA` | EMA on signed position | 30s | 0.003 | [-1, 1] |
| A4 | `boostMeanEMA` | EMA on fraction | 30s | 0.003 | [0, 1] |
| A5 | `speedMeanEMA` | EMA on fraction | 10s | 0.010 | [0, 1] |
| A6 | `pressureEMA` | EMA on binary | 15s | 0.007 | [0, 1] |
| A7 | `challengeRateEMA` | EMA on binary touch event | 30s | 0.003 | [0, ~0.1] |
| A8 | `confidence` | Linear ramp | n/a | n/a | [0, 1] |

### Per-feature semantics

**A1 `commitmentEMA`** — Each step, observe `frac = (# opponents within 800uu of ball) / (# opponents)`. EMA-smooth. High value → opponents collapse on ball (exploitable for passes, back-post plays). Low value → opponents rotate, harder to break.

**A2 `aerialEMA`** — Each step, observe `frac = (# opponents with z > 200) / (# opponents)`. High value → air-heavy opposition (consider ground plays). Low value → opposition stays grounded (aerial plays may go uncontested).

**A3 `depthMeanEMA`** — Each step, observe `y_signed = nearest_opp.pos.y` in the perspective player's frame (already inverted at observation time via the existing `inv` flag pattern). Normalize by `CommonValues::BACK_WALL_Y`. Negative → opponents deep on defense. Positive → opponents pushed up (counter-attack opportunity).

**A4 `boostMeanEMA`** — Each step, observe `avg = (sum of opp.boost / 100) / (# opponents)`. Range [0, 1]. High → opponents have fuel (dangerous). Low → opponents starving (press them).

**A5 `speedMeanEMA`** — Each step, observe `avg = (sum of opp.vel.Length() / SUPERSONIC_THRESHOLD) / (# opponents)`. Tempo signal.

**A6 `pressureEMA`** — Each step, observe `binary = (at least one opponent has y in my half) ? 1 : 0`, in perspective frame. Fraction of recent time spent under pressure.

**A7 `challengeRateEMA`** — Each step, observe `binary = (any opponent touched ball this step) ? 1 : 0`. Detected by tracking `state.ball.lastTouch` carId across steps. EMA gives "how often does opposition engage the ball recently."

**A8 `confidence`** — `min(1.0, sampleCount / 300.0)`. Pure ramp; not exposed to any EMA.

---

## 5. Feature Semantics (Block B — Self/Team Self Summary, 8 features)

| # | Feature | Type | Window | Output Range |
|---|---|---|---|---|
| B1 | `secsSinceMyTouch` | Clamped time | 5s clamp | [0, 1] |
| B2 | `secsSinceTeamTouch` | Clamped time | 5s clamp | [0, 1] |
| B3 | `possessionEMA` | EMA on binary | 10s | [0, 1] |
| B4 | `myBoostSlope` | Window diff | 1s | [-1, 1] |
| B5 | `myPosVariance` | Window stddev | 2s | [0, 1] |
| B6 | `ballMaxZ` | Window max | 1.5s | [0, 1] |
| B7 | `ballSpeedMean` | Window mean | 1s | [0, 1] |
| B8 | `teamAvgDistToBallEMA` | EMA on fraction | 3s | [0, 1] |

### Per-feature semantics

**B1 `secsSinceMyTouch`** — `clamp((now - myLastTouchTime) / 5.0, 0, 1)`. Counts how long since *I* touched the ball.

**B2 `secsSinceTeamTouch`** — Same, but for any teammate (including self).

**B3 `possessionEMA`** — Each step on a touch event, observe `binary = (toucher.team == myTeam) ? 1 : 0`. EMA with α=0.010 gives smoothed possession fraction over ~10s.

**B4 `myBoostSlope`** — `(boost_now - boost_1s_ago) / 1.0`, clamped to [-1, 1]. Positive → refueling. Negative → burning. Detects "I'm depleting" without the policy having to remember.

**B5 `myPosVariance`** — Standard deviation of position magnitude over a 2s window, normalized by 2000uu. Captures "am I rotating or stuck pressing in one zone."

**B6 `ballMaxZ`** — Max ball z over last 1.5s, normalized by `CommonValues::CEILING_Z`. "Did this just leave aerial regime."

**B7 `ballSpeedMean`** — Mean ball speed over last 1s, normalized by 6000uu/s (well above max realistic). Tempo of the ball itself.

**B8 `teamAvgDistToBallEMA`** — Each step, observe `avg = (sum of teammate distances to ball) / (# teammates + 1)` (including self), normalized by 8000uu. EMA with α=0.030 (~3s) gives smoothed team spread relative to ball.

---

## 6. Anti-Farming / Goodhart Considerations

These features are **passive observations only**. No reward will be tied to them. This is non-negotiable: if any reward starts depending on tendency state, the agent will learn to manipulate its own tendency tracker rather than play well.

**Subtle hazard:** in self-play, the agent generates the tendencies it later conditions on. Early Phase 5, this is approximately circular — agent learns to attend to features it itself produces. This is acceptable because:
1. `trainAgainstOldVersions = true` (30% of matches) injects diversity from older policy snapshots
2. The features measure objective game-state properties, not subjective beliefs
3. Even with circular reference, the *correlation* between tendency and value-of-action is genuine — high opponent commitment really does open back-post opportunities regardless of who produced the commitment

**Audit trigger:** if W&B shows tendency feature distributions collapsing to near-constant values during Phase 5+ training, this indicates either circular degeneracy or a feature design bug. Investigate immediately.

---

## 7. Identity Persistence

Tendencies are **team-keyed, not carId-keyed.** Teams are stable across the entire episode (blue stays blue). carIds may shuffle when old-version checkpoints slot in players but the team-level aggregation is invariant to this.

Trackers persist across goals within an episode (no reset on goal). Trackers reset only on `Reset(initialState)` or detected state rollback.

---

## 8. W&B Health Metrics

### Per-iteration histograms (all 16 features)

| Feature | Expected shape | Alert if |
|---|---|---|
| `commitmentEMA` | Bimodal-to-uniform, mean ~0.3 | Mean drifts below 0.1 or above 0.7 |
| `aerialEMA` | Right-skewed, mean ~0.1 | Mean above 0.4 (training broken) |
| `depthMeanEMA` | Centered near 0, std ~0.3 | Mean magnitude > 0.6 (one-sided training) |
| `boostMeanEMA` | Centered ~0.5, std ~0.2 | Mean below 0.2 or above 0.8 |
| `speedMeanEMA` | Right-skewed, mean ~0.3 | Mean below 0.15 |
| `pressureEMA` | Centered ~0.5 | Mean below 0.2 or above 0.8 |
| `challengeRateEMA` | Right-skewed, mean ~0.02 | Mean above 0.1 (touch detection bug) |
| `confidence` | Should saturate to 1.0 within 10s of game time | Saturates < 0.8 (sample count bug) |
| `secsSinceMyTouch` | Right-skewed | Mean above 0.9 (touch detection bug) |
| `secsSinceTeamTouch` | Right-skewed | Mean above 0.9 |
| `possessionEMA` | Centered ~0.5 | Mean below 0.3 or above 0.7 (asymmetric training) |
| `myBoostSlope` | Centered ~0 | Mean magnitude > 0.3 |
| `myPosVariance` | Right-skewed, mean ~0.2 | Mean below 0.05 (frozen agent) |
| `ballMaxZ` | Right-skewed, mean ~0.2 | Mean above 0.6 |
| `ballSpeedMean` | Centered ~0.3 | Mean below 0.1 |
| `teamAvgDistToBallEMA` | Right-skewed, mean ~0.3 | Mean above 0.7 |

### Scalar metrics

- `memory/avgConfidenceAtEpisodeEnd` — should approach 1.0
- `memory/avgConfidenceAt5Sec` — should be ≈ 0.5 (sanity check on ramp rate)
- `memory/touchDetectionRate` — touches detected per second; should match ground-truth touch rate

### Policy/critic alerts at Phase 5 activation

- KL divergence spike expected on first iteration after activation (one-time)
- Value loss elevated for ~50–200M steps (re-fitting value head to new context)
- **Alert if KL elevated > 200M steps** — features may be noisy or wrongly inverted
- **Alert if value loss does not decay back to pre-activation baseline within 500M steps** — features carry no useful signal, investigate

---

## 9. RocketSimVis Confirmation Checklist

After Phase 5 activation, before promoting to Machine 1:

- [ ] Spawn 2v2 scenario, log tendency feature values to console each second
- [ ] Verify `commitmentEMA` rises when both opponents collapse on ball
- [ ] Verify `aerialEMA` rises during designed aerial scenarios
- [ ] Verify `depthMeanEMA` sign matches perspective (negative when opponents on defense)
- [ ] Verify `possessionEMA` reflects actual touch sequence
- [ ] Verify all features reset to defaults at episode start
- [ ] Verify `confidence` ramps to 1.0 within ~10 game-seconds
- [ ] Verify touch detection: `challengeRateEMA` increments on each opponent touch
- [ ] Verify inversion: blue-team observer and orange-team observer of the same scenario produce mirrored (not identical) `depthMeanEMA` values

---

## 10. Phase Rollout Plan

| Phase | Steps | Action | Validation |
|---|---|---|---|
| 1 Bronze | 0–200M | Slots present, zero-filled. No behavioral effect. | Obs shape stable across rebuilds |
| 2 Silver | 200M–300M | Same | Same |
| 3 Gold | 300M–5B | Same | Same |
| 4 Platinum | 5–40B | Same | Same |
| **5 Diamond entry** | 40B | **Flip `memoryBlocksEnabled = true`.** Monitor KL spike, value loss. | KL spike resolves within 1–2 iterations; value loss decays within 200M steps |
| 5 Diamond steady | 40–80B | Run with features active. Compare lift vs. no-memory control branch on Machine 2. | Win rate on skill tracker improves by ≥ 30 Elo vs. control |
| 6 Champion | 80–120B | Keep active. Begin Stage 2 offline clustering analysis on logged features. | Clusters are interpretable |

---

## 11. Open Questions → Resolved Decisions Log

| Question | Resolution |
|---|---|
| Per-opponent or team-level tendencies? | Team-level. Self-play opponents share policy snapshots, so individual variance is just positional role. |
| Persist across goals? | Yes. Only full episode boundary or state rollback resets trackers. |
| Frame stacking instead? | No. 16 explicit features deliver same information at 1/16th the input replication cost. |
| Recurrent network instead? | Not for fall. Reconsider Phase 7+. |
| EMA vs sliding window? | EMA for tendencies (smoother gradient). Sliding window for self-summary history queues (need actual values for slope/variance/max). |
| Block C (game flow)? | Deferred. Block A+B captures the highest-leverage features; Block C overlaps with what the trunk can compute internally. |
| Library architecture? | Deferred past fall competition. |
| Human replay seeding? | Deferred. User confirmed self-play is the primary training value driver. |
| Phase gating mechanism? | Zero-fill obs slots in Phases 1–4, flip flag at Phase 5 entry. Obs shape invariant. |
| Update frequency? | Once per `BuildObs` call (per-player perspective). Acceptable redundancy because each obs builder instance is per-player. |
| Touch detection API? | Assumed `state.ball.lastTouch` carId tracking. Verify exact RLGymCPP API during integration. |

---

## 12. Implementation — Header Additions

Add to `AdvancedObsPadded.h` inside the existing class, private section:

```cpp
#include <deque>

// ============================================================================
// MEMORY BLOCKS A+B — Team tendency and self-summary tracking
// 2026-05 | NJH
// Gated by memoryBlocksEnabled (set via SetMemoryBlocksEnabled at Phase 5+)
// ============================================================================

// Block A: Team-level tendency tracker (one for opponent team, one for own team)
struct TeamTendency {
    // EMA features (Block A: A1–A7)
    float commitmentEMA    = 0.0f;
    float aerialEMA        = 0.0f;
    float depthMeanEMA     = 0.0f;
    float boostMeanEMA     = 0.5f;
    float speedMeanEMA     = 0.5f;
    float pressureEMA      = 0.0f;
    float challengeRateEMA = 0.0f;

    // Auxiliary state
    // NOTE: lastTouchCarId removed — touch detection uses player.ballTouchedStep
    // (confirmed API from AdvancedRewards.h:1433-1435)
    int   sampleCount      = 0;
};

// Block B: Self & team-self summary tracker
struct SelfSummary {
    // Touch timestamps (game time)
    float myLastTouchTime   = -1000.0f;
    float teamLastTouchTime = -1000.0f;

    // EMA features
    float possessionEMA          = 0.5f;  // B3
    float teamAvgDistToBallEMA   = 0.0f;  // B8

    // Rolling history queues (timestamp, value)
    std::deque<std::pair<float, float>> boostHistory;     // for B4 (1s window)
    std::deque<std::pair<float, Vec>>   positionHistory;  // for B5 (2s window)
    std::deque<std::pair<float, float>> ballZHistory;     // for B6 (1.5s window)
    std::deque<std::pair<float, float>> ballSpeedHistory; // for B7 (1s window)
};

TeamTendency oppTendency;     // tracks the opposing team's behavior
TeamTendency myTendency;      // tracks own team's behavior (used by B3 possession)
SelfSummary  selfSummary;

float lastUpdateTime  = -1.0f;
bool  memoryBlocksEnabled = false;

// ===== Constants =====
static constexpr float ALPHA_FAST = 0.010f;  // ~10s @ 30 decisions/sec
static constexpr float ALPHA_MED  = 0.007f;  // ~15s
static constexpr float ALPHA_SLOW = 0.003f;  // ~30s
static constexpr float ALPHA_B8   = 0.030f;  // ~3s for team spread
static constexpr float CONFIDENCE_SAMPLES = 300.0f;
static constexpr float COMMITMENT_RADIUS  = 800.0f;
static constexpr float AERIAL_Z_THRESHOLD = 200.0f;
static constexpr float BOOST_HISTORY_SEC  = 1.0f;
static constexpr float POSITION_HISTORY_SEC = 2.0f;
static constexpr float BALL_Z_HISTORY_SEC   = 1.5f;
static constexpr float BALL_SPEED_HISTORY_SEC = 1.0f;
static constexpr float TOUCH_TIMEOUT_NORM  = 5.0f;
static constexpr float BALL_SPEED_NORM     = 6000.0f;
static constexpr float TEAM_DIST_NORM      = 8000.0f;
static constexpr float POS_VARIANCE_NORM   = 2000.0f;

// ===== Methods =====
void UpdateMemoryTrackers(const Player& perspective, const GameState& state);
void TrimHistoryScalar(std::deque<std::pair<float, float>>& q, float now, float window);
void TrimHistoryVec(std::deque<std::pair<float, Vec>>& q, float now, float window);
void AddMemoryBlocksAToObs(FList& obs, const GameState& state) const;
void AddMemoryBlocksBToObs(FList& obs, const GameState& state) const;

public:
void SetMemoryBlocksEnabled(bool enabled) { memoryBlocksEnabled = enabled; }
```

---

## 13. Implementation — .cpp Method Definitions

```cpp
// ============================================================================
// MEMORY BLOCKS — Update logic
// ============================================================================

void RLGC::AdvancedObsPadded::TrimHistoryScalar(
    std::deque<std::pair<float, float>>& q, float now, float window) {
    while (!q.empty() && (now - q.front().first) > window) {
        q.pop_front();
    }
}

void RLGC::AdvancedObsPadded::TrimHistoryVec(
    std::deque<std::pair<float, Vec>>& q, float now, float window) {
    while (!q.empty() && (now - q.front().first) > window) {
        q.pop_front();
    }
}

void RLGC::AdvancedObsPadded::UpdateMemoryTrackers(
    const Player& perspective, const GameState& state) {

    const float now = state.elapsedTime;

    // Detect episode reset (rollback or fresh episode)
    if (lastUpdateTime < 0.0f || now < lastUpdateTime) {
        oppTendency    = TeamTendency();
        myTendency     = TeamTendency();
        selfSummary    = SelfSummary();
    }
    lastUpdateTime = now;

    Team myTeam = perspective.team;
    Team oppTeam = (myTeam == Team::BLUE) ? Team::ORANGE : Team::BLUE;

    // Partition players
    std::vector<const Player*> opponents, teammates;
    for (const auto& p : state.players) {
        if (p.team == oppTeam)     opponents.push_back(&p);
        else if (p.carId != perspective.carId) teammates.push_back(&p);
    }

    // ===== Block A observations =====
    if (!opponents.empty()) {
        // A1 commitment
        int near = 0;
        for (auto* p : opponents) {
            if ((p->pos - state.ball.pos).Length() < COMMITMENT_RADIUS) near++;
        }
        float commitObs = (float)near / opponents.size();

        // A2 aerial
        int aerial = 0;
        for (auto* p : opponents) if (p->pos.z > AERIAL_Z_THRESHOLD) aerial++;
        float aerialObs = (float)aerial / opponents.size();

        // A3 depth — nearest opponent's signed Y in perspective frame
        //          (positive = pushed up toward perspective's goal)
        const Player* nearest = opponents[0];
        float bestD = (nearest->pos - state.ball.pos).Length();
        for (auto* p : opponents) {
            float d = (p->pos - state.ball.pos).Length();
            if (d < bestD) { bestD = d; nearest = p; }
        }
        float yPersp = (myTeam == Team::ORANGE) ? -nearest->pos.y : nearest->pos.y;
        float depthObs = RS_CLAMP(yPersp / CommonValues::BACK_WALL_Y, -1.0f, 1.0f);

        // A4 boost
        float boostSum = 0.0f;
        for (auto* p : opponents) boostSum += p->boost / 100.0f;
        float boostObs = boostSum / opponents.size();

        // A5 speed (normalized to supersonic)
        float speedSum = 0.0f;
        for (auto* p : opponents) {
            speedSum += RS_MIN(p->vel.Length() / CommonValues::SUPERSONIC_THRESHOLD, 1.0f);
        }
        float speedObs = speedSum / opponents.size();

        // A6 pressure (any opponent in my half, perspective frame)
        bool pressure = false;
        for (auto* p : opponents) {
            float yp = (myTeam == Team::ORANGE) ? -p->pos.y : p->pos.y;
            if (yp < 0.0f) { pressure = true; break; }
        }
        float pressureObs = pressure ? 1.0f : 0.0f;

        // EMA updates
        oppTendency.commitmentEMA = (1 - ALPHA_FAST) * oppTendency.commitmentEMA + ALPHA_FAST * commitObs;
        oppTendency.aerialEMA     = (1 - ALPHA_FAST) * oppTendency.aerialEMA     + ALPHA_FAST * aerialObs;
        oppTendency.depthMeanEMA  = (1 - ALPHA_SLOW) * oppTendency.depthMeanEMA  + ALPHA_SLOW * depthObs;
        oppTendency.boostMeanEMA  = (1 - ALPHA_SLOW) * oppTendency.boostMeanEMA  + ALPHA_SLOW * boostObs;
        oppTendency.speedMeanEMA  = (1 - ALPHA_FAST) * oppTendency.speedMeanEMA  + ALPHA_FAST * speedObs;
        oppTendency.pressureEMA   = (1 - ALPHA_MED)  * oppTendency.pressureEMA   + ALPHA_MED  * pressureObs;
        oppTendency.sampleCount++;
    }

    // ===== A7 challengeRate & B3 possession via touch detection =====
    // CONFIRMED API: player.ballTouchedStep (bool) is true on the exact frame
    // a player's car contacts the ball. Source: AdvancedRewards.h:1433-1435,
    // CommonRewards.h:161. There is no state.ball.lastTouch.carId field.
    float oppTouchObs = 0.0f;
    for (const auto& p : state.players) {
        if (!p.ballTouchedStep) continue;

        bool isOppTouch  = (p.team == oppTeam);
        bool isMyTouch   = (p.carId == perspective.carId);
        bool isTeamTouch = (p.team == myTeam);

        if (isOppTouch) oppTouchObs = 1.0f;

        // B3 possession update on touch event
        float possObs = isTeamTouch ? 1.0f : 0.0f;
        selfSummary.possessionEMA = (1 - ALPHA_FAST) * selfSummary.possessionEMA + ALPHA_FAST * possObs;

        // B1, B2 touch timestamps
        if (isMyTouch)   selfSummary.myLastTouchTime   = now;
        if (isTeamTouch) selfSummary.teamLastTouchTime = now;
    }
    oppTendency.challengeRateEMA = (1 - ALPHA_SLOW) * oppTendency.challengeRateEMA + ALPHA_SLOW * oppTouchObs;

    // ===== Block B observations =====
    // B4: boost history
    selfSummary.boostHistory.push_back({now, perspective.boost / 100.0f});
    TrimHistoryScalar(selfSummary.boostHistory, now, BOOST_HISTORY_SEC);

    // B5: position history
    selfSummary.positionHistory.push_back({now, perspective.pos});
    TrimHistoryVec(selfSummary.positionHistory, now, POSITION_HISTORY_SEC);

    // B6: ball Z history
    selfSummary.ballZHistory.push_back({now, state.ball.pos.z});
    TrimHistoryScalar(selfSummary.ballZHistory, now, BALL_Z_HISTORY_SEC);

    // B7: ball speed history
    selfSummary.ballSpeedHistory.push_back({now, state.ball.vel.Length()});
    TrimHistoryScalar(selfSummary.ballSpeedHistory, now, BALL_SPEED_HISTORY_SEC);

    // B8: team avg distance to ball (including self)
    float distSum = (perspective.pos - state.ball.pos).Length();
    int count = 1;
    for (auto* tm : teammates) {
        distSum += (tm->pos - state.ball.pos).Length();
        count++;
    }
    float teamDistObs = RS_CLAMP((distSum / count) / TEAM_DIST_NORM, 0.0f, 1.0f);
    selfSummary.teamAvgDistToBallEMA = (1 - ALPHA_B8) * selfSummary.teamAvgDistToBallEMA + ALPHA_B8 * teamDistObs;
}

// ============================================================================
// MEMORY BLOCKS — Obs emission
// ============================================================================

void RLGC::AdvancedObsPadded::AddMemoryBlocksAToObs(FList& obs, const GameState& state) const {
    // A1–A7 EMAs are already in [-1, 1] or [0, 1] from observation construction
    obs += oppTendency.commitmentEMA;     // A1 [0, 1]
    obs += oppTendency.aerialEMA;         // A2 [0, 1]
    obs += oppTendency.depthMeanEMA;      // A3 [-1, 1]
    obs += oppTendency.boostMeanEMA;      // A4 [0, 1]
    obs += oppTendency.speedMeanEMA;      // A5 [0, 1]
    obs += oppTendency.pressureEMA;       // A6 [0, 1]
    obs += oppTendency.challengeRateEMA;  // A7 [0, ~0.1]

    // A8 confidence
    float conf = RS_MIN(1.0f, oppTendency.sampleCount / CONFIDENCE_SAMPLES);
    obs += conf;
}

void RLGC::AdvancedObsPadded::AddMemoryBlocksBToObs(FList& obs, const GameState& state) const {
    const float now = state.elapsedTime;

    // B1: secs since my touch (clamped 5s, normalized)
    float dtMine = now - selfSummary.myLastTouchTime;
    obs += RS_CLAMP(dtMine / TOUCH_TIMEOUT_NORM, 0.0f, 1.0f);

    // B2: secs since team touch
    float dtTeam = now - selfSummary.teamLastTouchTime;
    obs += RS_CLAMP(dtTeam / TOUCH_TIMEOUT_NORM, 0.0f, 1.0f);

    // B3: possession EMA
    obs += selfSummary.possessionEMA;

    // B4: boost slope
    float slope = 0.0f;
    if (selfSummary.boostHistory.size() >= 2) {
        float bNow  = selfSummary.boostHistory.back().second;
        float bOld  = selfSummary.boostHistory.front().second;
        float tSpan = selfSummary.boostHistory.back().first - selfSummary.boostHistory.front().first;
        if (tSpan > 0.01f) slope = RS_CLAMP((bNow - bOld) / tSpan, -1.0f, 1.0f);
    }
    obs += slope;

    // B5: position variance (stddev of distance from window mean, normalized)
    float posVar = 0.0f;
    if (selfSummary.positionHistory.size() >= 3) {
        Vec mean(0, 0, 0);
        for (auto& p : selfSummary.positionHistory) mean += p.second;
        mean /= (float)selfSummary.positionHistory.size();
        float sumSq = 0.0f;
        for (auto& p : selfSummary.positionHistory) {
            float d = (p.second - mean).Length();
            sumSq += d * d;
        }
        float stddev = sqrtf(sumSq / selfSummary.positionHistory.size());
        posVar = RS_CLAMP(stddev / POS_VARIANCE_NORM, 0.0f, 1.0f);
    }
    obs += posVar;

    // B6: ball max Z
    float maxZ = 0.0f;
    for (auto& p : selfSummary.ballZHistory) if (p.second > maxZ) maxZ = p.second;
    obs += RS_CLAMP(maxZ / CommonValues::CEILING_Z, 0.0f, 1.0f);

    // B7: ball speed mean
    float speedSum = 0.0f;
    for (auto& p : selfSummary.ballSpeedHistory) speedSum += p.second;
    float speedMean = selfSummary.ballSpeedHistory.empty()
        ? 0.0f
        : speedSum / selfSummary.ballSpeedHistory.size();
    obs += RS_CLAMP(speedMean / BALL_SPEED_NORM, 0.0f, 1.0f);

    // B8: team avg dist to ball EMA
    obs += selfSummary.teamAvgDistToBallEMA;
}
```

---

## 14. Modifications to Existing `Reset()` and `BuildObs()`

### `Reset()` — add at end:

```cpp
void RLGC::AdvancedObsPadded::Reset(const GameState& initialState) {
    // ... existing demoTimers init ...

    // Memory blocks reset
    oppTendency    = TeamTendency();
    myTendency     = TeamTendency();
    selfSummary    = SelfSummary();
    lastUpdateTime = -1.0f;
}
```

### `BuildObs()` — add two sections:

**Near the top of `BuildObs`, after `inv` is determined:**

```cpp
// Update memory trackers BEFORE building obs so reads are fresh.
// Trackers are no-op-cheap when memoryBlocksEnabled is false (we still
// update so that if the flag flips mid-episode, state is consistent).
UpdateMemoryTrackers(player, state);
```

**At the END of `BuildObs`, after all existing features have been pushed:**

```cpp
// ========================================================================
// MEMORY BLOCKS A+B (16 features)
// Zero-filled when memoryBlocksEnabled is false (Phases 1-4)
// Active starting Phase 5 Diamond
// ========================================================================
if (memoryBlocksEnabled) {
    AddMemoryBlocksAToObs(obs, state);  // 8 features
    AddMemoryBlocksBToObs(obs, state);  // 8 features
} else {
    for (int i = 0; i < 16; i++) obs += 0.0f;
}
```

---

## 15. Activation in `RewardConfig.cpp` / `ExampleMain.cpp`

In `ExampleMain.cpp` or wherever the obs builder is instantiated:

```cpp
bool memoryActive = (CURRENT_PHASE >= TrainingPhase::PHASE_5_DIAMOND);
obsBuilder->SetMemoryBlocksEnabled(memoryActive);
```

Or fold into the `ConfigureLearnerConfig` flow if obs builders are configured there.

---

## 16. Integration Checklist

- [ ] ~~Verify `state.ball.lastTouch.carId` is the correct RLGymCPP API for last toucher~~ **RESOLVED:** Touch detection uses `player.ballTouchedStep` (bool per-player). See `AdvancedRewards.h:1433` and `CommonRewards.h:161`. The `lastTouchCarId` member has been removed from `TeamTendency`.
- [ ] Confirm `CommonValues::SUPERSONIC_THRESHOLD`, `CommonValues::BACK_WALL_Y`, `CommonValues::CEILING_Z` are accessible. Substitute if named differently.
- [ ] Add `#include <deque>` to AdvancedObsPadded.h if not already present.
- [ ] Add 16 new floats to the obs size accounting in any consumer that hardcodes obs dimension.
- [ ] Wire up `SetMemoryBlocksEnabled(CURRENT_PHASE >= PHASE_5_DIAMOND)` in env construction.
- [ ] Add W&B metric hooks for the 16 features as histograms.
- [ ] Smoke-test on Machine 2: spawn 1 env, verify no crashes, verify tendency values appear sensible in a printf-debug pass before launching full 310-env training.
- [ ] Update `RewardCatalog.md` and `Reward_Change_Log.md` (obs features, not rewards, but the catalog should reflect the new obs block).
- [ ] Update `CLAUDE.md` if the obs builder reference there needs revising.

---

## 17. Rollback Plan

If Phase 5 activation causes unrecoverable training instability:

1. **Immediate:** flip `SetMemoryBlocksEnabled(false)` — slots go back to zero, network gradually re-ignores them. No checkpoint surgery required because obs shape is invariant.
2. **Diagnose:** examine which feature histograms drifted outside expected bounds (section 8).
3. **Patch and re-activate:** fix the offending feature, repeat activation. The zero-fill approach means iteration is cheap.

If a specific feature is the problem but others are fine, gate per-feature with a bitmask flag rather than the whole-block flag.
