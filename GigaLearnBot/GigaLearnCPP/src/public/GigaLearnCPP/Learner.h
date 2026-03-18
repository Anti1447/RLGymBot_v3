#pragma once

#include <torch/torch.h>
#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/RewardMetricsCollector.h>
#include <RLGymCPP/Rewards/RewardDiagnostician.h>
#include <RLGymCPP/Rewards/FlipResetMetricsCollector.h>
#include "RLGymCPP/Utils/KickoffController.h"
#include "Util/MetricSender.h"
#include "Util/RenderSender.h"
#include "LearnerConfig.h"
#include "PPO/TransferLearnConfig.h"

// Forward declarations for MCTS/reanalysis components
namespace GGL {
	class ReanalysisBuffer;
	struct ExperienceSample;
}

// Suppress C4251 warnings about STL types needing DLL interface
// These are safe to ignore when building everything together
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace GGL {

	typedef std::function<void(class Learner*, const std::vector<RLGC::GameState>& states, Report& report)> StepCallbackFn;

	// https://github.com/AechPro/rlgym-ppo/blob/main/rlgym_ppo/learner.py
	class RG_IMEXPORT Learner {
	public:
		LearnerConfig config;

		RLGC::EnvSet* envSet;

		class PPOLearner* ppo;
		class PolicyVersionManager* versionMgr;

		RLGC::EnvCreateFn envCreateFn;
		MetricSender* metricSender;
		RenderSender* renderSender;

		int obsSize;
		int numActions;

		struct WelfordStat* returnStat;
		struct BatchedWelfordStat* obsStat;

		std::string runID = {};

		uint64_t
			totalTimesteps = 0,
			totalIterations = 0;

		StepCallbackFn stepCallback = NULL;

		// Reward metrics collection for automated diagnostics
		RewardMetricsCollector metricsCollector;

		// Flip reset metrics collection
		FlipResetMetricsCollector flipResetMetricsCollector;

		// MuZero reanalysis components
		ReanalysisBuffer* reanalysisBuffer = nullptr;
		std::vector<ExperienceSample> tempExperienceBuffer;

		// Storage for latest reanalysis targets (for PPO loss integration)
		struct ReanalysisTargets {
			torch::Tensor observations;       // [N, obs_size]
			torch::Tensor mctsValueTargets;   // [N]
			torch::Tensor mctsPolicyTargets;  // [N, num_actions]
			bool hasTargets = false;
		};
		ReanalysisTargets latestReanalysisTargets;

		Learner(RLGC::EnvCreateFn envCreateFunc, LearnerConfig config, StepCallbackFn stepCallback = NULL);
		void Start();

		void StartTransferLearn(const TransferLearnConfig& transferLearnConfig);

		void StartQuitKeyThread(bool& quitPressed, std::thread& outThread);

		void Save();
		void Load();
		void SaveStats(std::filesystem::path path);
		void LoadStats(std::filesystem::path path);

		// MuZero reanalysis methods
		struct ReanalysisMetrics {
			float meanMCTSValue = 0.0f;
			float meanPPOValue = 0.0f;
			float valueDifference = 0.0f;
			size_t samplesProcessed = 0;
		};

		ReanalysisMetrics RunReanalysis();
		std::vector<std::pair<std::vector<float>, float>> BatchEvaluate(
			const std::vector<RLGC::GameState>& states,
			const std::vector<int>& playerIndices
		);

		RG_NO_COPY(Learner);

		~Learner();
	};
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
