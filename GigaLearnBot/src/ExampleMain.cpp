#include "RewardConfig.h"
#include "ModelConfig.h"
#include "GameModeConfig.h"
#include "DribbleFlickMetrics.h"

#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/TerminalConditions/DemoCountCondition.h>
#include <RLGymCPP/OBSBuilders/AdvancedObsPaddedV2.h>
#include <RLGymCPP/StateSetters/FuzzedKickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/StateSetters/AerialTrainingState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/ActionParsers/AdvancedAction.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/Rewards/AirDribbleAccuracyReward.h>
#include <RLGymCPP/Rewards/AirDribbleClearingReward.h>
#include <RLGymCPP/Rewards/HookshotReward.h>


using namespace GGL;
using namespace RLGC;

// ============================================================================
// TRAINING CONFIGURATION
// ============================================================================
// Phase configurations are in RewardConfig.h and ModelConfig.cpp
// To change phase: Update CURRENT_PHASE in RewardConfig.h
// To change rewards: Update reward_phases.json (data-driven) or GetPhasedRewards() (legacy)
// To change model/training: Update ConfigureLearnerConfig() in ModelConfig.cpp
// ============================================================================

// Global flag to indicate if we're in render mode (used for fixed game modes)
bool g_renderMode = false;
// Which mode to render when g_renderMode is true (set via --1v1/--2v2/--3v3)
GameModeType g_renderGameMode = GameModeType::MODE_1V1;

// ============================================================================
// ENVIRONMENT CREATION
// ============================================================================

EnvCreateResult EnvCreateFunc(int index) {
    // Use the new data-driven reward configuration from JSON
    // Falls back to hardcoded GetPhasedRewards if JSON loading fails
    std::vector<WeightedReward> rewards = GeneratePhasedRewardsFromJSON();
    PhaseConfig phaseCfg = GetPhaseConfig();

    std::vector<TerminalCondition*> terminalConditions = {
        new NoTouchCondition(phaseCfg.noTouchTimeout),
        new GoalScoreCondition(),
		//NEW: Add more terminal conditions here if needed
		//new DemoCountCondition(3)  // End episode on 3 demos
    };

    // Sample game mode based on environment index
    GameModeType mode;
    if (g_renderMode) {
        // Render mode only ever creates 1 arena (see Learner.cpp: numArenas = renderMode ? 1 : numGames),
        // so the mode is fixed by the --1v1/--2v2/--3v3 flag rather than cycling by index.
        mode = g_renderGameMode;
    } else {
        // In training mode: random sampling with 1:2:3 distribution
        mode = GameModeConfig::SampleMode();
    }
    int playersPerTeam = GameModeConfig::GetPlayersPerTeam(mode);

    auto arena = Arena::Create(GameMode::SOCCAR);
    if (mode == GameModeType::MODE_SOLO) {
        arena->AddCar(Team::BLUE);
    } else {
        for (int i = 0; i < playersPerTeam; i++) {
            arena->AddCar(Team::BLUE);
            arena->AddCar(Team::ORANGE);
        }
    }

    EnvCreateResult result = {};
    result.actionParser = new AdvancedAction(); // uses more discrete bins instead of the original DefaultAction.
    auto* obsBuilder = new AdvancedObsPaddedV2(3);  // Max 3 players per team
    obsBuilder->SetMemoryBlocksEnabled(CURRENT_PHASE >= TrainingPhase::PHASE_5_DIAMOND);
    result.obsBuilder = obsBuilder;

    // Create state setters based on phase configuration
    std::vector<std::pair<StateSetter*, float>> stateSetters;

    // Always include kickoff state as the base
    float kickoffWeight = 1.0f;

    // Calculate weights based on phase configuration
    if (phaseCfg.useRandomState) {
        kickoffWeight *= (1.0f - phaseCfg.randomStateChance);
    }
    if (phaseCfg.useAerialTraining) {
        kickoffWeight *= (1.0f - phaseCfg.aerialTrainingChance);
    }

    stateSetters.push_back({ new FuzzedKickoffState(), kickoffWeight });

    // Add random state if enabled
    if (phaseCfg.useRandomState) {
        float randomWeight = phaseCfg.randomStateChance;
        if (phaseCfg.useAerialTraining) {
            randomWeight *= (1.0f - phaseCfg.aerialTrainingChance);
        }
        stateSetters.push_back({ new RandomState(), randomWeight });
    }

    // Add aerial training state if enabled
    if (phaseCfg.useAerialTraining) {
        stateSetters.push_back({ new AerialTrainingState(), phaseCfg.aerialTrainingChance });
    }

    // Use CombinedState to randomly choose between the state setters
    result.stateSetter = new CombinedState(stateSetters);

    result.terminalConditions = terminalConditions;
    result.rewards = rewards;
    result.arena = arena;

    return result;
}

// ============================================================================
// METRICS CALLBACK
// ============================================================================

void StepCallback(Learner* learner, const std::vector<GameState>& states, Report& report) {
    // To prevent expensive metrics from eating at performance, we will only run them on 1/4th of steps
    // This doesn't really matter unless you have expensive metrics (which this example doesn't)
    bool doExpensiveMetrics = (rand() % 4) == 0;

    // ========================================================================
    // AIR DRIBBLE METRICS (collected every step)
    // ========================================================================
    auto airDribbleMetrics = AirDribbleAccuracyReward::GetAndResetMetrics();

    if (airDribbleMetrics.attempts > 0) {
        // Only report if there were actual attempts this step
        report.AddAvg("AirDribble/Avg Sequence Length", airDribbleMetrics.avgSequenceLength);
        report.AddAvg("AirDribble/Soft Touch Ratio %", airDribbleMetrics.softTouchRatio);
        report.AddAvg("AirDribble/Completion Rate %", airDribbleMetrics.completionRate);
        report.AddAvg("AirDribble/Zone Efficiency %", airDribbleMetrics.scoringZoneEfficiency);
        report.AddAvg("AirDribble/Quality Score", airDribbleMetrics.touchQualityScore);

        // Also include raw counters for debugging
        report.AddAvg("AirDribble/Attempts", airDribbleMetrics.attempts);
        report.AddAvg("AirDribble/On Target", airDribbleMetrics.onTarget);
        report.AddAvg("AirDribble/Overshot Count", airDribbleMetrics.overshotCount);
        report.AddAvg("AirDribble/Total Reward", airDribbleMetrics.rewardSum);
    }

    // ========================================================================
    // SHADOW DEFENSE METRICS (collected every step)
    // ========================================================================
    auto shadowMetrics = ShadowDefenseVsPossessionReward::GetAndResetMetrics();
    if (shadowMetrics.fireCount > 0) {
        report.AddAvg("ShadowDefense/Attempts",      (float)shadowMetrics.fireCount);
        report.AddAvg("ShadowDefense/AvgReward",     shadowMetrics.avgReward);
        report.AddAvg("ShadowDefense/AvgTargetDist", shadowMetrics.avgTargetDist);
        report.AddAvg("ShadowDefense/AvgVelMatch",   shadowMetrics.avgVelMatch);
    }

    // ========================================================================
    // AIR DRIBBLE CLEARING METRICS (collected every step)
    // ========================================================================
    auto clearingMetrics = AirDribbleClearingReward::GetAndResetMetrics();

    if (clearingMetrics.attempts > 0) {
        // Only report if there were actual clearing attempts this step
        report.AddAvg("AirDribbleClearing/Avg Sequence Length", clearingMetrics.avgSequenceLength);
        report.AddAvg("AirDribbleClearing/Soft Touch Ratio %", clearingMetrics.softTouchRatio);
        report.AddAvg("AirDribbleClearing/Clearing Rate %", clearingMetrics.clearingRate);
        report.AddAvg("AirDribbleClearing/Zone Efficiency %", clearingMetrics.zoneEfficiency);
        report.AddAvg("AirDribbleClearing/Quality Score", clearingMetrics.touchQualityScore);

        // Also include raw counters for debugging
        report.AddAvg("AirDribbleClearing/Attempts", clearingMetrics.attempts);
        report.AddAvg("AirDribbleClearing/Successful", clearingMetrics.successful);
        report.AddAvg("AirDribbleClearing/In Clearing Zone", clearingMetrics.inClearingZone);
        report.AddAvg("AirDribbleClearing/Total Reward", clearingMetrics.rewardSum);
    }

    // ========================================================================
    // DOUBLE JUMP POP METRICS (collected every step)
    // ========================================================================
    {
        auto m = DoubleJumpPopReward::GetAndResetMetrics();
        if (m.carriesStarted > 0 || m.doubleJumps > 0) {
            report.AddAvg("DoubleJumpPop/CarriesStarted",   (float)m.carriesStarted);
            report.AddAvg("DoubleJumpPop/JumpsFromCarry",   (float)m.jumpsFromCarry);
            report.AddAvg("DoubleJumpPop/DoubleJumps",      (float)m.doubleJumps);
            report.AddAvg("DoubleJumpPop/PopsQualified",    (float)m.popsQualified);
            report.AddAvg("DoubleJumpPop/PopsRejectedFar",  (float)m.popsRejectedFar);
            report.AddAvg("DoubleJumpPop/PopsRejectedMag",  (float)m.popsRejectedMag);
            report.AddAvg("DoubleJumpPop/FollowThroughHit", (float)m.followThroughHit);
            report.AddAvg("DoubleJumpPop/JumpRate",          (float)m.jumpRate);
            report.AddAvg("DoubleJumpPop/PopQualifyRate",    (float)m.popQualifyRate);
            report.AddAvg("DoubleJumpPop/FollowThroughRate", (float)m.followThroughRate);
            report.AddAvg("DoubleJumpPop/AvgRewardPerPop",   (float)m.avgRewardPerPop);
        }
    }

    // ========================================================================
    // POSSESSION REDIRECT METRICS - Direction Change & Hook Shot
    // ========================================================================
    auto directionChangeMetrics = DirectionChangeReward::GetAndResetMetrics();
    if (directionChangeMetrics.attempts > 0) {
        report.AddAvg("PossessionRedirect/DirectionChange/Attempts", (float)directionChangeMetrics.attempts);
        report.AddAvg("PossessionRedirect/DirectionChange/SuccessRate %", directionChangeMetrics.GetSuccessRate() * 100.0f);
        report.AddAvg("PossessionRedirect/DirectionChange/Avg Reward", directionChangeMetrics.GetAverageReward());
    }

    auto hookShotMetrics = HookShotReward::GetAndResetMetrics();
    if (hookShotMetrics.attempts > 0) {
        report.AddAvg("PossessionRedirect/HookShot/Attempts", (float)hookShotMetrics.attempts);
        report.AddAvg("PossessionRedirect/HookShot/OnGoalRate %", hookShotMetrics.GetSuccessRate() * 100.0f);
        report.AddAvg("PossessionRedirect/HookShot/Avg Reward", hookShotMetrics.GetAverageReward());
    }

    // ========================================================================
    // AIR DRIBBLE BUMP CONVERSION METRICS (shadow mode — collected every step)
    // ========================================================================
    auto adBumpMetrics = AirDribbleBumpConversionReward::GetAndResetMetrics();
    if (adBumpMetrics.airBumpContacts > 0) {
        report.AddAvg("AirDribbleBumpConv/AirBumpContacts",   (float)adBumpMetrics.airBumpContacts);
        report.AddAvg("AirDribbleBumpConv/Qualified",         (float)adBumpMetrics.qualified);
        report.AddAvg("AirDribbleBumpConv/QualifyRate",       (float)adBumpMetrics.qualifyRate);
        report.AddAvg("AirDribbleBumpConv/ConversionRate",    (float)adBumpMetrics.conversionRate);
        report.AddAvg("AirDribbleBumpConv/BallClipRate",      (float)adBumpMetrics.ballClipRate);
        report.AddAvg("AirDribbleBumpConv/AvgAirTouchAtQual", (float)adBumpMetrics.avgAirTouchAtQual);
        report.AddAvg("AirDribbleBumpConv/DemoShare",         (float)adBumpMetrics.demoShare);
        report.AddAvg("AirDribbleBumpConv/FrontBlockRate",
            adBumpMetrics.postBumpGoalBound > 0
                ? (float)adBumpMetrics.frontBlocks / adBumpMetrics.postBumpGoalBound : 0.0f);
        report.AddAvg("AirDribbleBumpConv/LastDefenderRate",
            adBumpMetrics.postBumpGoalBound > 0
                ? (float)adBumpMetrics.lastDefenderHits / adBumpMetrics.postBumpGoalBound : 0.0f);
        report.AddAvg("AirDribbleBumpConv/AvgRewardPerConv",  (float)adBumpMetrics.avgRewardPerConv);
    }

    // ========================================================================
    // PREDICTIVE DEMO REWARDS — Shadow Mode Metrics
    // ========================================================================
    {
        auto m = MomentumLeadDemoReward::GetAndResetMetrics();
        if (m.trackingEntered > 0) {
            report.AddAvg("PredictiveDemo/MomentumLead/TrackingEntered", (float)m.trackingEntered);
            report.AddAvg("PredictiveDemo/MomentumLead/DemosLed",        (float)m.demosLed);
            report.AddAvg("PredictiveDemo/MomentumLead/AvgLeadAlign",    (float)m.avgLeadAlign);
            report.AddAvg("PredictiveDemo/MomentumLead/AvgSpeedFactor",  (float)m.avgSpeedFactor);
            report.AddAvg("PredictiveDemo/MomentumLead/RewardSum",       (float)m.rewardSum);
        }
    }
    {
        auto m = BallChaserAmbushReward::GetAndResetMetrics();
        if (m.ambushSetups > 0) {
            report.AddAvg("PredictiveDemo/BallChaserAmbush/Setups",          (float)m.ambushSetups);
            report.AddAvg("PredictiveDemo/BallChaserAmbush/DemosConverted",  (float)m.demosConverted);
            report.AddAvg("PredictiveDemo/BallChaserAmbush/AvgBonusQuality", (float)m.avgBonusQuality);
            report.AddAvg("PredictiveDemo/BallChaserAmbush/RewardSum",       (float)m.rewardSum);
        }
    }
    {
        auto m = PostTouchShadowDemoReward::GetAndResetMetrics();
        if (m.touchesDetected > 0) {
            report.AddAvg("PredictiveDemo/PostTouchShadow/TouchesDetected",   (float)m.touchesDetected);
            report.AddAvg("PredictiveDemo/PostTouchShadow/PreReadWindows",    (float)m.preReadWindows);
            report.AddAvg("PredictiveDemo/PostTouchShadow/WindowsExpired",    (float)m.windowsExpired);
            report.AddAvg("PredictiveDemo/PostTouchShadow/BaseRewardFired",   (float)m.baseRewardFired);
            report.AddAvg("PredictiveDemo/PostTouchShadow/PreReadBonusFired", (float)m.preReadBonusFired);
            report.AddAvg("PredictiveDemo/PostTouchShadow/AvgPredAccuracy",   (float)m.avgPredictionAccuracy);
            report.AddAvg("PredictiveDemo/PostTouchShadow/RewardSum",         (float)m.rewardSum);
        }
    }
    {
        auto m = AerialVulnerabilityDemoReward::GetAndResetMetrics();
        if (m.demosInVulnWindow > 0) {
            report.AddAvg("PredictiveDemo/AerialVuln/DemosInWindow",  (float)m.demosInVulnWindow);
            report.AddAvg("PredictiveDemo/AerialVuln/FlipBonusCount", (float)m.flipBonusCount);
            report.AddAvg("PredictiveDemo/AerialVuln/AvgHeightBonus", (float)m.avgHeightBonus);
            report.AddAvg("PredictiveDemo/AerialVuln/RewardSum",      (float)m.rewardSum);
        }
    }
    {
        auto m = RespawnHuntReward::GetAndResetMetrics();
        if (m.huntsStarted > 0) {
            report.AddAvg("PredictiveDemo/RespawnHunt/HuntsStarted",        (float)m.huntsStarted);
            report.AddAvg("PredictiveDemo/RespawnHunt/RoutedToSpawn",       (float)m.routedToSpawn);
            report.AddAvg("PredictiveDemo/RespawnHunt/AmbushPresenceCount", (float)m.ambushPresenceCount);
            report.AddAvg("PredictiveDemo/RespawnHunt/DoubleDemoCount",     (float)m.doubleDemoCount);
            report.AddAvg("PredictiveDemo/RespawnHunt/RewardSum",           (float)m.rewardSum);
        }
    }
    {
        auto m = FunnelExitDemoReward::GetAndResetMetrics();
        if (m.cornerEntries > 0) {
            report.AddAvg("PredictiveDemo/FunnelExit/CornerEntries",  (float)m.cornerEntries);
            report.AddAvg("PredictiveDemo/FunnelExit/AtExitPosition", (float)m.atExitPosition);
            report.AddAvg("PredictiveDemo/FunnelExit/DemosInWindow",  (float)m.demosInWindow);
            report.AddAvg("PredictiveDemo/FunnelExit/AvgTimeliness",  (float)m.avgTimeliness);
            report.AddAvg("PredictiveDemo/FunnelExit/RewardSum",      (float)m.rewardSum);
        }
    }
    {
        auto m = DoubleCommitDemoReward::GetAndResetMetrics();
        if (m.allCommittedFrames > 0) {
            report.AddAvg("PredictiveDemo/DoubleCommit/CommittedFrames", (float)m.allCommittedFrames);
            report.AddAvg("PredictiveDemo/DoubleCommit/DemosInCommit",   (float)m.demosInCommit);
            report.AddAvg("PredictiveDemo/DoubleCommit/AvgCommitBonus",  (float)m.avgCommitBonus);
            report.AddAvg("PredictiveDemo/DoubleCommit/RewardSum",       (float)m.rewardSum);
        }
    }

    // ========================================================================
    // WALL/CEILING REDIRECT REWARD — Shadow Mode Metrics
    // ========================================================================
    {
        auto m = WallCeilingRedirectReward::GetAndResetMetrics();
        if (m.bouncesDetected > 0 || m.predSequences > 0) {
            report.AddAvg("WallCeilingRedirect/PredSequences",     (float)m.predSequences);
            report.AddAvg("WallCeilingRedirect/BouncesDetected",   (float)m.bouncesDetected);
            report.AddAvg("WallCeilingRedirect/AerialIntercepts",  (float)m.aerialIntercepts);
            report.AddAvg("WallCeilingRedirect/GroundedIntercepts",(float)m.groundedIntercepts);
            report.AddAvg("WallCeilingRedirect/ShotRedirects",     (float)m.shotRedirects);
            report.AddAvg("WallCeilingRedirect/Continuations",     (float)m.continuations);
            report.AddAvg("WallCeilingRedirect/OwnGoalRedirects",  (float)m.ownGoalRedirects);
            report.AddAvg("WallCeilingRedirect/AvgShotAlign",      (float)m.avgShotAlign);
            report.AddAvg("WallCeilingRedirect/AvgPowerRestore",   (float)m.avgPowerRestore);
            report.AddAvg("WallCeilingRedirect/AvgEnergyRetained", (float)m.avgEnergyRetained);
            report.AddAvg("WallCeilingRedirect/RewardSum",         (float)m.rewardSum);
            report.AddAvg("WallCeilingRedirect/ShapingSum",        (float)m.shapingSum);
        }
    }

    // ========================================================================
    // COUNTER-ATTACK REWARD — Fast-Break Window Metrics
    // ========================================================================
    {
        auto m = CounterAttackReward::GetAndResetMetrics();
        if (m.windowsOpened > 0) {
            report.AddAvg("CounterAttack/WindowsOpened",      (float)m.windowsOpened);
            report.AddAvg("CounterAttack/WindowsCapitalized", (float)m.windowsCapitalized);
            report.AddAvg("CounterAttack/AvgAdvantage",       (float)m.avgAdvantage);
            report.AddAvg("CounterAttack/RewardSum",          (float)m.rewardSum);
        }
    }

    // ========================================================================
    // POSSESSION CONTROL v2 — Threat-gated possession metrics
    // ========================================================================
    {
        auto m = PossessionControlRewardv2::GetAndResetMetrics();
        report.AddAvg("Possession/V2/ThreatActiveRate %", (float)(m.threatActiveRate * 100.0));
        report.AddAvg("Possession/V2/DurationBonusMean",  (float)m.durationBonusMean);
        report.AddAvg("Possession/V2/GiveawayEvents",     (float)m.giveawayEvents);
        report.AddAvg("Possession/V2/DangerGiveawayRatio %", (float)(m.dangerGiveawayRatio * 100.0));
        report.AddAvg("Possession/V2/ActivationRate %",   (float)(m.activationRate * 100.0));
        report.AddAvg("Possession/V2/MeanValue",          (float)m.meanValue);
    }

    // ========================================================================
    // FAKE CHALLENGE REWARDS — Defensive fake + Offensive fake shot (Rev 2)
    // ========================================================================
    {
        auto m = FakeChallengeDefenseReward::GetAndResetMetrics();
        if (m.sells > 0 || m.zoneRejects > 0) {
            report.AddAvg("FakeChallenge/Def/Sells",       (float)m.sells);
            report.AddAvg("FakeChallenge/Def/Aborts",      (float)m.aborts);
            report.AddAvg("FakeChallenge/Def/OppCommits",  (float)m.oppCommits);
            report.AddAvg("FakeChallenge/Def/Fires",       (float)m.fires);
            report.AddAvg("FakeChallenge/Def/ZoneRejects", (float)m.zoneRejects);
            if (m.fires > 0)
                report.AddAvg("FakeChallenge/Def/AvgReward", (float)(m.rewardSum / m.fires));
        }
    }
    {
        auto m = FakeChallengeOffenseReward::GetAndResetMetrics();
        if (m.sells > 0) {
            report.AddAvg("FakeChallenge/Off/Sells",      (float)m.sells);
            report.AddAvg("FakeChallenge/Off/Aborts",     (float)m.aborts);
            report.AddAvg("FakeChallenge/Off/OppCommits", (float)m.oppCommits);
            report.AddAvg("FakeChallenge/Off/Fires",      (float)m.fires);
            if (m.fires > 0)
                report.AddAvg("FakeChallenge/Off/AvgReward", (float)(m.rewardSum / m.fires));
        }
    }

    // ========================================================================
    // BALL LANDING POSITION — bounce-landing-spot positioning (replaces
    // BallTrajectoryInterceptionReward, Rev 1.0 2026-07-07)
    // ========================================================================
    {
        auto m = BallLandingPositionReward::GetAndResetMetrics();
        report.AddAvg("BallLandingPosition/ActivationRate", (float)m.activeFrames);
        report.AddAvg("BallLandingPosition/MeanReward", (float)m.totalReward);
    }

    // ========================================================================
    // THIRD-MAN TACTICS — Shadow Mode Metrics
    // ========================================================================
    {
        std::unordered_map<std::string, float> m;
        ThirdManStallReward::CollectMetrics(m);
        if (m["ThirdManStall/frames_active"] > 0.0f)
            for (auto& [k, v] : m) report.AddAvg(k, v);
    }
    {
        std::unordered_map<std::string, float> m;
        CounterLaunchDiagonalReward::CollectMetrics(m);
        if (m["CounterLaunch/launches_fired"] > 0.0f
            || m["CounterLaunch/teammate_upfield_gate_failures"] > 0.0f)
            for (auto& [k, v] : m) report.AddAvg(k, v);
    }

    // ========================================================================
    // DOUBLE COMMIT PENALTY METRICS
    // ========================================================================
    {
        auto m = DoubleCommitPenalty::GetAndResetMetrics();
        if (m.penaltyFrames > 0) {
            report.AddAvg("Penalty/DoubleCommit/PenaltyFrames",  (float)m.penaltyFrames);
            report.AddAvg("Penalty/DoubleCommit/AvgOverlapCount",
                (float)m.commitCountSum / (float)m.penaltyFrames);
            report.AddAvg("Penalty/DoubleCommit/PenaltySum",     (float)m.penaltySum);
        }
    }
    {
        auto m = AntiLayeringPenalty::GetAndResetMetrics();
        if (m.penaltyFrames > 0) {
            report.AddAvg("Penalty/AntiLayering/PenaltyFrames", (float)m.penaltyFrames);
            report.AddAvg("Penalty/AntiLayering/AvgPairsPerFrame",
                (float)m.pairsSum / (float)m.penaltyFrames);
        }
    }
    {
        auto m = DefensiveZoneOverlapPenalty::GetAndResetMetrics();
        if (m.penaltyFrames > 0) {
            report.AddAvg("Penalty/DefZoneOverlap/PenaltyFrames",   (float)m.penaltyFrames);
            report.AddAvg("Penalty/DefZoneOverlap/AvgPairsPerFrame",
                (float)m.pairsSum / (float)m.penaltyFrames);
        }
    }
    {
        auto m = OverflipPenalty::GetAndResetMetrics();
        if (m.flipCount > 0) {
            report.AddAvg("Penalty/Overflip/Count",        (float)m.flipCount);
            report.AddAvg("Penalty/Overflip/AvgDistToBall",(float)m.avgDistToBall);
        }
    }
    {
        auto m = DiagonalZoneCoverageReward::GetAndResetMetrics();
        if (m.totalFrames > 0) {
            report.AddAvg("Positioning/DiagonalZone/CorrectFrames", (float)m.correctFrames);
            report.AddAvg("Positioning/DiagonalZone/TotalEligible", (float)m.totalFrames);
        }
    }
    {
        auto m = NoMansLandPenalty::GetAndResetMetrics();
        if (m.penaltyFrames > 0)
            report.AddAvg("Penalty/NoMansLand/PenaltyFrames", (float)m.penaltyFrames);
    }
    {
        auto m = SoloPlayComboReward::GetAndResetMetrics();
        if (m.qualified > 0) {
            report.AddAvg("SoloPlay/Combo/Qualified2Touch",     (float)m.qualified);
            report.AddAvg("SoloPlay/Combo/Converted2Touch",     (float)m.converted2);
            report.AddAvg("SoloPlay/Combo/Converted3PlusTouch", (float)m.converted3p);
            report.AddAvg("SoloPlay/Combo/ConversionRate",
                (float)(m.converted2 + m.converted3p) / (float)m.qualified);
            int total = m.converted2 + m.converted3p;
            if (total > 0) {
                report.AddAvg("SoloPlay/Combo/AvgTouchCount",
                    (float)m.touchCountSum / (float)total);
                report.AddAvg("SoloPlay/Combo/AvgBallSpeedAtThreat",
                    (float)m.ballSpeedSum / (float)total);
            }
        }
    }
    {
        auto m = DualThreatReward::GetAndResetMetrics();
        if (m.eligFrames > 0) {
            report.AddAvg("SoloPlay/DualThreat/Frames",        (float)m.dualFrames);
            report.AddAvg("SoloPlay/DualThreat/EligibleFrames",(float)m.eligFrames);
            report.AddAvg("SoloPlay/DualThreat/ActivationRate",
                (float)m.dualFrames / (float)m.eligFrames);
        }
    }

    // ========================================================================
    // ADVANCED KICKOFF REWARDS METRICS
    // ========================================================================
    {
        auto m = SpeedFlipKickoffReward::GetAndResetMetrics();
        if (m.flipDetected > 0 || m.contactSuccess > 0) {
            report.AddAvg("Kickoff/SpeedFlip/FlipDetected",   (float)m.flipDetected);
            report.AddAvg("Kickoff/SpeedFlip/ContactSuccess", (float)m.contactSuccess);
        }
    }
    {
        auto m = DelayedKickoffReward::GetAndResetMetrics();
        if (m.delaySuccess > 0)
            report.AddAvg("Kickoff/Delayed/Success", (float)m.delaySuccess);
    }
    {
        auto m = FakeKickoffReward::GetAndResetMetrics();
        if (m.fakeSuccess > 0)
            report.AddAvg("Kickoff/Fake/Success", (float)m.fakeSuccess);
    }
    {
        auto m = ReactiveKickoffReward::GetAndResetMetrics();
        if (m.scenarioA + m.scenarioB + m.scenarioC > 0) {
            report.AddAvg("Kickoff/Reactive/ScenarioA", (float)m.scenarioA);
            report.AddAvg("Kickoff/Reactive/ScenarioB", (float)m.scenarioB);
            report.AddAvg("Kickoff/Reactive/ScenarioC", (float)m.scenarioC);
        }
    }
    {
        auto m = PredictiveSecondManReward::GetAndResetMetrics();
        if (m.correctRead > 0) {
            report.AddAvg("Kickoff/SecondMan/CorrectRead",  (float)m.correctRead);
            report.AddAvg("Kickoff/SecondMan/PrereadBonus", (float)m.prereadBonus);
        }
    }
    {
        auto m = DiagonalBoostKickoffReward::GetAndResetMetrics();
        if (m.partialReward + m.fullReward > 0) {
            report.AddAvg("Kickoff/DiagBoost/PartialReward", (float)m.partialReward);
            report.AddAvg("Kickoff/DiagBoost/FullReward",    (float)m.fullReward);
        }
    }
    {
        auto m = SpanishDemoKickoffReward::GetAndResetMetrics();
        if (m.demoFired > 0) {
            report.AddAvg("Kickoff/SpanishDemo/DemoFired",     (float)m.demoFired);
            report.AddAvg("Kickoff/SpanishDemo/PayoffSuccess", (float)m.payoffSuccess);
        }
    }
    {
        auto m = KickoffCounterHitReward::GetAndResetMetrics();
        if (m.oppFirstTouch > 0) {
            report.AddAvg("Kickoff/Counter/OppFirstTouch", (float)m.oppFirstTouch);
            report.AddAvg("Kickoff/Counter/Won",           (float)m.won);
            report.AddAvg("Kickoff/Counter/AvgAlign",      (float)m.avgAlign);
            report.AddAvg("Kickoff/Counter/AvgBallSpeed",  (float)m.avgBallSpeed);
        }
    }
    {
        auto m = KickoffCounterpositionReward::GetAndResetMetrics();
        report.AddAvg("Kickoff/CounterPos/SpeedflipDetected", (float)m.speedflipDetected);
        report.AddAvg("Kickoff/CounterPos/PreshadeCorrect",   (float)m.preshadeCorrect);
        report.AddAvg("Kickoff/CounterPos/TouchBonus",        (float)m.touchBonus);
    }

    // Add our metrics
    for (auto& state : states) {
        // Determine game mode from current state
        int numPlayersPerTeam = state.players.size() / 2;  // Assuming balanced teams
        const char* modeStr = nullptr;
        switch (numPlayersPerTeam) {
            case 1: modeStr = "1v1"; break;
            case 2: modeStr = "2v2"; break;
            case 3: modeStr = "3v3"; break;
            default: modeStr = "unknown";
        }

        if (doExpensiveMetrics) {
            for (auto& player : state.players) {
                // Per-mode metrics
                std::string modePrefix = std::string("Mode/") + modeStr + "/";

                report.AddAvg(modePrefix + "Player/In Air Ratio", !player.isOnGround);
                report.AddAvg(modePrefix + "Player/Ball Touch Ratio", player.ballTouchedStep);
                report.AddAvg(modePrefix + "Player/Demoed Ratio", player.isDemoed);

                report.AddAvg(modePrefix + "Player/Speed", player.vel.Length());
                Vec dirToBall = (state.ball.pos - player.pos).Normalized();
                report.AddAvg(modePrefix + "Player/Speed Towards Ball", RS_MAX(0, player.vel.Dot(dirToBall)));

                report.AddAvg(modePrefix + "Player/Boost", player.boost);

                if (player.ballTouchedStep)
                    report.AddAvg(modePrefix + "Player/Touch Height", state.ball.pos.z);
            }
        }

        if (state.goalScored) {
            std::string modePrefix = std::string("Mode/") + modeStr + "/";
            report.AddAvg(modePrefix + "Game/Goal Speed", state.ball.vel.Length());
            report.AddAvg("Game/Goal Speed", state.ball.vel.Length());  // Also track overall
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    RocketSim::Init("C:\\Projects\\RLGymBot_v3\\collision_meshes");

    // Parse command-line arguments for render mode
    bool renderMode = false;
    GameModeType renderGameMode = GameModeType::MODE_1V1;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--render") {
            renderMode = true;
        } else if (arg == "--1v1") {
            renderGameMode = GameModeType::MODE_1V1;
        } else if (arg == "--2v2") {
            renderGameMode = GameModeType::MODE_2V2;
        } else if (arg == "--3v3") {
            renderGameMode = GameModeType::MODE_3V3;
        }
    }
    if (renderMode) {
        std::cout << "[CONFIG] Render mode enabled (" << GameModeConfig::GetModeName(renderGameMode) << ")\n";
    }

    // Set global render flags (used by EnvCreateFunc since render mode only creates 1 arena)
    g_renderMode = renderMode;
    g_renderGameMode = renderGameMode;

    // HARDCODED: taker runs a position-specific speed-flip sequence; NN resumes after.
    // STOCHASTIC: NN controls every frame including kickoffs (useful for debugging).
    // Variant probabilities per position are set in KickoffController::SelectVariant().
    //const RLGC::KickoffMode KICKOFF_MODE = RLGC::KickoffMode::HARDCODED;
    const RLGC::KickoffMode KICKOFF_MODE = RLGC::KickoffMode::STOCHASTIC;
    //std::cout << "[CONFIG] Kickoff mode: " <<
        //(KICKOFF_MODE == RLGC::KickoffMode::HARDCODED ? "HARDCODED" : "STOCHASTIC") << std::endl;

    // Get phase configuration
    PhaseConfig phaseCfg = GetPhaseConfig();
    LearnerConfig cfg = {};

    // Configure all training settings from ModelConfig
    ConfigureLearnerConfig(cfg, phaseCfg);

    // Use standard MLP architecture
    cfg.ppo.networkArch = NetworkArchitecture::MLP;

    // Configure render mode settings
    if (renderMode) {
        cfg.sendMetrics = false;           // No need to log metrics from render instance
        cfg.renderMode = true;             // Enable visualization
        cfg.ppo.deterministic = true;      // Use deterministic policy (best actions, no exploration)
        cfg.numGames = 1;                  // Render mode only ever creates 1 arena (see Learner.cpp)
        std::cout << "[CONFIG] Rendering with deterministic policy (" << GameModeConfig::GetModeName(renderGameMode) << ")\n";
    } else {
        cfg.sendMetrics = true;
        cfg.renderMode = false;
    }

    cfg.renderTimeScale = 3.f;
    cfg.kickoffMode = KICKOFF_MODE;  // SET HERE

    // Print current phase info
    const char* phaseNames[] = {    
        "PHASE 1: BRONZE (0-200M)",
        "PHASE 2: SILVER (200M-300M)",
        "PHASE 3: GOLD (300M-5B)",
        "PHASE 4: PLATINUM (5-40B)",
        "PHASE 5: DIAMOND (40-80B)",
        "PHASE 6: CHAMPION (80-120B)",
        "PHASE 7: GRAND CHAMPION (120-160B)",
        "PHASE 8: SSL (160-200B)",
        "PHASE 9: PRO ESPORTS (200B+)"
    };
    printf("\n=== TRAINING PHASE: %s ===\n", phaseNames[(int)CURRENT_PHASE]);
    printf("Entropy: %.4f, Policy LR: %.2e\n", phaseCfg.entropyScale, phaseCfg.policyLR);
    printf("Network: Shared[1024, 1024] Policy[1024, 1024] Critic[1024, 1024]\n\n");

    Learner* learner = new Learner(EnvCreateFunc, cfg, StepCallback);
    learner->Start();

    return EXIT_SUCCESS;
}