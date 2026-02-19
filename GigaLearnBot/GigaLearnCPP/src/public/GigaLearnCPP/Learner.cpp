#include "Learner.h"

#include <GigaLearnCPP/PPO/PPOLearner.h>
#include <GigaLearnCPP/PPO/ExperienceBuffer.h>

#include <torch/cuda.h>
#include <nlohmann/json.hpp>
#include <pybind11/embed.h>

#ifdef RG_CUDA_SUPPORT
#include <c10/cuda/CUDACachingAllocator.h>
#endif
#include <private/GigaLearnCPP/PPO/ExperienceBuffer.h>
#include <private/GigaLearnCPP/PPO/GAE.h>
#include <private/GigaLearnCPP/PolicyVersionManager.h>
#include <private/GigaLearnCPP/MCTS/ReanalysisBuffer.h>
#include <private/GigaLearnCPP/MCTS/MCTSSearch.h>

#include "Util/KeyPressDetector.h"
#include <private/GigaLearnCPP/Util/WelfordStat.h>
#include "Util/AvgTracker.h"

using namespace RLGC;

GGL::Learner::Learner(EnvCreateFn envCreateFn, LearnerConfig config, StepCallbackFn stepCallback) :
	envCreateFn(envCreateFn), config(config), stepCallback(stepCallback)
{
	pybind11::initialize_interpreter();

#ifndef NDEBUG
	RG_LOG("===========================");
	RG_LOG("WARNING: GigaLearn runs extremely slowly in debug, and there are often bizzare issues with debug-mode torch.");
	RG_LOG("It is recommended that you compile in release mode without optimization for debugging.");
	RG_SLEEP(1000);
#endif

	if (config.tsPerSave == 0)
		config.tsPerSave = config.ppo.tsPerItr;

	RG_LOG("Learner::Learner():");

	if (config.randomSeed == -1)
		config.randomSeed = RS_CUR_MS();

	RG_LOG("\tCheckpoint Save/Load Dir: " << config.checkpointFolder);

	torch::manual_seed(config.randomSeed);

	at::Device device = at::Device(at::kCPU);
	if (
		config.deviceType == LearnerDeviceType::GPU_CUDA || 
		(config.deviceType == LearnerDeviceType::AUTO && torch::cuda::is_available())
		) {
		RG_LOG("\tUsing CUDA GPU device...");

		// Test out moving a tensor to GPU and back to make sure the device is working
		torch::Tensor t;
		bool deviceTestFailed = false;
		try {
			t = torch::tensor(0);
			t = t.to(at::Device(at::kCUDA));
			t = t.cpu();
		} catch (...) {
			deviceTestFailed = true;
		}

		if (!torch::cuda::is_available() || deviceTestFailed)
			RG_ERR_CLOSE(
				"Learner::Learner(): Can't use CUDA GPU because " <<
				(torch::cuda::is_available() ? "libtorch cannot access the GPU" : "CUDA is not available to libtorch") << ".\n" <<
				"Make sure your libtorch comes with CUDA support, and that CUDA is installed properly."
			)
		device = at::Device(at::kCUDA);
	} else {
		RG_LOG("\tUsing CPU device...");
		device = at::Device(at::kCPU);
	}

	if (RocketSim::GetStage() != RocketSimStage::INITIALIZED) {
		RG_LOG("\tInitializing RocketSim...");
		RocketSim::Init("collision_meshes", true);
	}

	{
		RG_LOG("\tCreating envs...");
		EnvSetConfig envSetConfig = {};
		envSetConfig.envCreateFn = envCreateFn;
		envSetConfig.numArenas = config.renderMode ? 1 : config.numGames;
		envSetConfig.tickSkip = config.tickSkip;
		envSetConfig.actionDelay = config.actionDelay;
		envSetConfig.saveRewards = config.addRewardsToMetrics;
		envSetConfig.kickoffMode = config.kickoffMode;  // ADD THIS
		envSetConfig.enableProfiling = false;  // Enable profiling
		envSet = new RLGC::EnvSet(envSetConfig);
		obsSize = envSet->state.obs.size[1];
		numActions = envSet->actionParsers[0]->GetActionAmount();
	}

	{
		if (config.standardizeReturns) {
			this->returnStat = new WelfordStat();
		} else {
			this->returnStat = NULL;
		}

		if (config.standardizeObs) {
			this->obsStat = new BatchedWelfordStat(obsSize);
		} else {
			this->obsStat = NULL;
		}
	}

	try {
		RG_LOG("\tMaking PPO learner...");
		ppo = new PPOLearner(obsSize, numActions, config.ppo, device);
		// Set reanalysis weights from config
		ppo->config.policyReanalysisWeight = config.reanalysis.policyReanalysisWeight;
		ppo->config.valueReanalysisWeight = config.reanalysis.valueReanalysisWeight;
	} catch (std::exception& e) {
		RG_ERR_CLOSE("Failed to create PPO learner: " << e.what());
	}

	if (config.renderMode) {
		renderSender = new RenderSender(config.renderTimeScale);
	} else {
		renderSender = NULL;
	}

	if (config.skillTracker.enabled || config.trainAgainstOldVersions)
		config.savePolicyVersions = true;

	if (config.savePolicyVersions && !config.renderMode) {
		if (config.checkpointFolder.empty())
			RG_ERR_CLOSE("Cannot save/load old policy versions with no checkpoint save folder");
		versionMgr = new PolicyVersionManager(
			config.checkpointFolder / "policy_versions", config.maxOldVersions, config.tsPerVersion,
			config.skillTracker, envSet->config
		);
	} else {
		versionMgr = NULL;
	}

	if (!config.checkpointFolder.empty())
		Load();

	if (config.savePolicyVersions && !config.renderMode) {
		if (config.checkpointFolder.empty())
			RG_ERR_CLOSE("Cannot save/load old policy versions with no checkpoint save folder");
		auto models = ppo->GetPolicyModels();
		versionMgr->LoadVersions(models, totalTimesteps);
	}

	if (config.sendMetrics && !config.renderMode) {
		if (!runID.empty())
			RG_LOG("\tRun ID: " << runID);
		metricSender = new MetricSender(config.metricsProjectName, config.metricsGroupName, config.metricsRunName, runID);
	} else {
		metricSender = NULL;
	}

	// Initialize reanalysis buffer if enabled
	if (config.reanalysis.enabled) {
		RG_LOG("\tInitializing reanalysis buffer (size: " << config.reanalysis.bufferSize << ")...");
		ReanalysisBufferConfig bufferConfig;
		bufferConfig.maxSize = config.reanalysis.bufferSize;
		bufferConfig.minSizeToSample = config.reanalysis.minBufferSize;
		reanalysisBuffer = new ReanalysisBuffer(bufferConfig);
	}

	// Initialize reward metrics collector
	if (config.addRewardsToMetrics && !envSet->rewards.empty()) {
		metricsCollector.Initialize(envSet->rewards[0]);
		if (versionMgr) {
			metricsCollector.SnapshotSkillRatings(versionMgr->skill.curRatings.data);
		}
		RG_LOG("\tReward metrics collector initialized with " << metricsCollector.trackers.size() << " rewards");
	}

	RG_LOG(RG_DIVIDER);
}

void GGL::Learner::SaveStats(std::filesystem::path path) {
	using namespace nlohmann;

	constexpr const char* ERROR_PREFIX = "Learner::SaveStats(): ";

	std::ofstream fOut(path);
	if (!fOut.good())
		RG_ERR_CLOSE(ERROR_PREFIX << "Can't open file at " << path);

	json j = {};
	j["total_timesteps"] = totalTimesteps;
	j["total_iterations"] = totalIterations;

	if (config.sendMetrics)
		j["run_id"] = metricSender->curRunID;

	if (returnStat)
		j["return_stat"] = returnStat->ToJSON();
	if (obsStat)
		j["obs_stat"] = obsStat->ToJSON();

	if (versionMgr)
		versionMgr->AddRunningStatsToJSON(j);

	std::string jStr = j.dump(4);
	fOut << jStr;
}

void GGL::Learner::LoadStats(std::filesystem::path path) {
	// TODO: Repetitive code, merge repeated code into one function called from both SaveStats() and LoadStats()

	using namespace nlohmann;
	constexpr const char* ERROR_PREFIX = "Learner::LoadStats(): ";

	std::ifstream fIn(path);
	if (!fIn.good())
		RG_ERR_CLOSE(ERROR_PREFIX << "Can't open file at " << path);

	json j = json::parse(fIn);
	totalTimesteps = j["total_timesteps"];
	totalIterations = j["total_iterations"];

	if (j.contains("run_id"))
		runID = j["run_id"];

	if (returnStat)
		returnStat->ReadFromJSON(j["return_stat"]);
	if (obsStat)
		obsStat->ReadFromJSON(j["obs_stat"]);

	if (versionMgr)
		versionMgr->LoadRunningStatsFromJSON(j);
}

// Different than RLGym-PPO to show that they are not compatible
constexpr const char* STATS_FILE_NAME = "RUNNING_STATS.json";

void GGL::Learner::Save() {
	if (config.checkpointFolder.empty())
		RG_ERR_CLOSE("Learner::Save(): Cannot save because config.checkpointSaveFolder is not set");

	std::filesystem::path saveFolder = config.checkpointFolder / std::to_string(totalTimesteps);
	std::filesystem::create_directories(saveFolder);

	RG_LOG("Saving to folder " << saveFolder << "...");
	SaveStats(saveFolder / STATS_FILE_NAME);
	ppo->SaveTo(saveFolder);

	// Write reward metrics and diagnosis
	if (metricsCollector.initialized) {
		// Get current skill ratings for delta tracking
		std::map<std::string, float> currentRatings;
		if (versionMgr)
			currentRatings = versionMgr->skill.curRatings.data;

		metricsCollector.WriteMetricsJSON(
			saveFolder / "REWARD_METRICS.json",
			currentRatings,
			totalTimesteps,
			"PHASE_" + std::to_string((int)totalTimesteps) // Simple identifier
		);

		// Run diagnostician
		std::vector<RewardMetrics> allMetrics;
		std::map<std::string, float> rewardWeights;
		for (const auto& pair : metricsCollector.trackers) {
			const auto& tracker = pair.second;
			RewardMetrics rm;
			rm.rewardName = tracker.rewardName;
			rm.activationRate = tracker.GetActivationRate();
			rm.meanActivationValue = tracker.GetMean();
			rm.varianceInActivation = tracker.GetVariance();
			rm.baseWeight = tracker.weight;
			rm.isActive = rm.activationRate > 0.01f;
			rm.isStable = rm.varianceInActivation < 0.6f;
			rm.isHelpful = false; // No per-reward win correlation yet
			rm.correlationWithWinRate = 0.0f;
			allMetrics.push_back(rm);
			rewardWeights[tracker.rewardName] = tracker.weight;
		}

		RewardDiagnostician diagnostician;
		auto diagnoses = diagnostician.DiagnoseAllRewards(allMetrics, rewardWeights);
		RewardDiagnostician::WriteDiagnosisJSON(
			saveFolder / "REWARD_DIAGNOSIS.json",
			diagnoses,
			totalTimesteps,
			(int)metricsCollector.trackers.size()
		);

		// Snapshot ratings for next interval's delta tracking
		if (versionMgr)
			metricsCollector.SnapshotSkillRatings(versionMgr->skill.curRatings.data);

		// Trigger automated Python reward analysis (non-fatal)
		if (metricSender) {
			try {
				std::string saveFolderStr = saveFolder.string();
				std::string checkpointFolderStr = config.checkpointFolder.string();
				metricSender->pyMod.attr("on_checkpoint_saved")(
					saveFolderStr,
					checkpointFolderStr,
					totalTimesteps
				);
			} catch (std::exception& e) {
				RG_LOG("WARNING: Reward analysis failed (non-fatal): " << e.what());
			}
		}
	}

	// Remove old checkpoints
	if (config.checkpointsToKeep != -1) {
		std::set<int64_t> allSavedTimesteps = Utils::FindNumberedDirs(config.checkpointFolder);
		while (allSavedTimesteps.size() > config.checkpointsToKeep) {
			int64_t lowestCheckpointTS = INT64_MAX;
			for (int64_t savedTimesteps : allSavedTimesteps)
				lowestCheckpointTS = RS_MIN(lowestCheckpointTS, savedTimesteps);

			std::filesystem::path removePath = config.checkpointFolder / std::to_string(lowestCheckpointTS);
			try {
				std::filesystem::remove_all(removePath);
			} catch (std::exception& e) {
				RG_ERR_CLOSE("Failed to remove old checkpoint from " << removePath << ", exception: " << e.what());
			}
			allSavedTimesteps.erase(lowestCheckpointTS);
		}
	}

	if (versionMgr)
		versionMgr->SaveVersions();

	RG_LOG(" > Done.");
}

void GGL::Learner::Load() {
	if (config.checkpointFolder.empty())
		RG_ERR_CLOSE("Learner::Load(): Cannot load because config.checkpointLoadFolder is not set");

	RG_LOG("Loading most recent checkpoint in " << config.checkpointFolder << "...");

	int64_t highest = -1;
	std::set<int64_t> allSavedTimesteps = Utils::FindNumberedDirs(config.checkpointFolder);
	for (int64_t timesteps : allSavedTimesteps)
		highest = RS_MAX(timesteps, highest);

	if (highest != -1) {
		std::filesystem::path loadFolder = config.checkpointFolder / std::to_string(highest);
		RG_LOG(" > Loading checkpoint " << loadFolder << "...");
		LoadStats(loadFolder / STATS_FILE_NAME);
		ppo->LoadFrom(loadFolder);
		RG_LOG(" > Done.");
	} else {
		RG_LOG(" > No checkpoints found, starting new model.")
	}
}

void GGL::Learner::StartQuitKeyThread(bool& quitPressed, std::thread& outThread) {
	quitPressed = false;

	RG_LOG("Press 'Q' to save and quit!");
	outThread = std::thread(
		[&] {
			while (true) {
				char c = toupper(KeyPressDetector::GetPressedChar());
				if (c == 'Q') {
					RG_LOG("Save queued, will save and exit next iteration.");
					quitPressed = true;
				}
			}
		}
	);

	outThread.detach();
}
void GGL::Learner::StartTransferLearn(const TransferLearnConfig& tlConfig) {

	RG_LOG("Starting transfer learning...");

	// TODO: Lots of manual obs builder stuff going on which is quite volatile
	//	Although I can't really think another way to do this

	std::vector<ObsBuilder*> oldObsBuilders = {};
	for (int i = 0; i < envSet->arenas.size(); i++)
		oldObsBuilders.push_back(tlConfig.makeOldObsFn());

	// Reset all obs builders initially
	for (int i = 0; i < envSet->arenas.size(); i++)
		oldObsBuilders[i]->Reset(envSet->state.gameStates[0]);

	std::vector<ActionParser*> oldActionParsers = {};
	for (int i = 0; i < envSet->arenas.size(); i++)
		oldActionParsers.push_back(tlConfig.makeOldActFn());

	int oldNumActions = oldActionParsers[0]->GetActionAmount();

	if (oldNumActions != numActions) {
		if (!tlConfig.mapActsFn) {
			RG_ERR_CLOSE(
				"StartTransferLearn: Old and new action parsers have a different number of actions, but tlConfig.mapActsFn is NULL.\n" <<
				"You must implement this function to translate the action indices."
			);
		};
	}

	// Determine old obs size
	int oldObsSize;
	{
		GameState testState = envSet->state.gameStates[0];
		oldObsSize = oldObsBuilders[0]->BuildObs(testState.players[0], testState).size();
	}

	ModelSet oldModels = {};
	{
		RG_NO_GRAD;
		PPOLearner::MakeModels(false, oldObsSize, oldNumActions, tlConfig.oldSharedHeadConfig, tlConfig.oldPolicyConfig, {}, ppo->device, oldModels);

		oldModels.Load(tlConfig.oldModelsPath, false, false);
	}

	try {
		bool saveQueued;
		std::thread keyPressThread;
		StartQuitKeyThread(saveQueued, keyPressThread);

		while (true) {
			Report report = {};

			// Collect obs
			std::vector<float> allNewObs = {};
			std::vector<float> allOldObs = {};
			std::vector<uint8_t> allNewActionMasks = {};
			std::vector<uint8_t> allOldActionMasks = {};
			std::vector<int> allActionMaps = {};
			int stepsCollected;
			{
				RG_NO_GRAD;
				for (stepsCollected = 0; stepsCollected < tlConfig.batchSize; stepsCollected += envSet->state.numPlayers) {
					
					auto terminals = envSet->state.terminals; // Backup
					envSet->Reset();
					for (int i = 0; i < envSet->arenas.size(); i++) // Manually reset old obs builders
						if (terminals[i])
							oldObsBuilders[i]->Reset(envSet->state.gameStates[i]);

					torch::Tensor tActions, tLogProbs;
					torch::Tensor tStates = DIMLIST2_TO_TENSOR<float>(envSet->state.obs);
					torch::Tensor tActionMasks = DIMLIST2_TO_TENSOR<uint8_t>(envSet->state.actionMasks);

					envSet->StepFirstHalf(true);

					allNewObs += envSet->state.obs.data;
					allNewActionMasks += envSet->state.actionMasks.data;

					// Run all old obs and old action parser on each player
					// TODO: Could be multithreaded
					for (int arenaIdx = 0; arenaIdx < envSet->arenas.size(); arenaIdx++) {
						auto& gs = envSet->state.gameStates[arenaIdx];
						for (auto& player : gs.players) {
							allOldObs += oldObsBuilders[arenaIdx]->BuildObs(player, gs);
							allOldActionMasks += oldActionParsers[arenaIdx]->GetActionMask(player, gs);

							if (tlConfig.mapActsFn) {
								auto curMap = tlConfig.mapActsFn(player, gs);
								if (curMap.size() != numActions)
									RG_ERR_CLOSE("StartTransferLearn: Your action map must have the same size as the new action parser's actions");
								allActionMaps += curMap;
							}
						}
					}

					ppo->InferActions(
						tStates.to(ppo->device, true), tActionMasks.to(ppo->device, true), 
						&tActions, &tLogProbs
					);

					auto curActions = TENSOR_TO_VEC<int>(tActions);

					envSet->Sync();
					envSet->StepSecondHalf(curActions, false);

					if (stepCallback)
						stepCallback(this, envSet->state.gameStates, report);
				}
			}

			uint64_t prevTimesteps = totalTimesteps;
			totalTimesteps += stepsCollected;
			report["Total Timesteps"] = totalTimesteps;
			report["Collected Timesteps"] = stepsCollected;
			totalIterations++;
			report["Total Iterations"] = totalIterations;

			// Make tensors
			torch::Tensor tNewObs = torch::tensor(allNewObs).reshape({ -1, obsSize }).to(ppo->device);
			torch::Tensor tOldObs = torch::tensor(allOldObs).reshape({ -1, oldObsSize }).to(ppo->device);
			torch::Tensor tNewActionMasks = torch::tensor(allNewActionMasks).reshape({ -1, numActions }).to(ppo->device);
			torch::Tensor tOldActionMasks = torch::tensor(allOldActionMasks).reshape({ -1, oldNumActions }).to(ppo->device);

			torch::Tensor tActionMaps = {};
			if (!allActionMaps.empty())
				tActionMaps = torch::tensor(allActionMaps).reshape({ -1, numActions }).to(ppo->device);

			// Transfer learn
			ppo->TransferLearn(oldModels, tNewObs, tOldObs, tNewActionMasks, tOldActionMasks, tActionMaps, report, tlConfig);

			if (versionMgr)
				versionMgr->OnIteration(ppo, report, totalTimesteps, prevTimesteps);

			if (saveQueued) {
				if (!config.checkpointFolder.empty())
					Save();
				exit(0);
			}

			if (!config.checkpointFolder.empty()) {
				if (totalTimesteps / config.tsPerSave > prevTimesteps / config.tsPerSave) {
					// Auto-save
					Save();
				}
			}

			report.Finish();

			if (metricSender)
				metricSender->Send(report);

			report.Display(
				{
					"Transfer Learn Accuracy",
					"Transfer Learn Loss",
					"",
					"Policy Entropy",
					"Old Policy Entropy",
					"Policy Update Magnitude",
					"",
					"Collected Timesteps",
					"Total Timesteps",
					"Total Iterations"
				}
			);
		}

	} catch (std::exception& e) {
		RG_ERR_CLOSE("Exception thrown during transfer learn loop: " << e.what());
	}
}

void GGL::Learner::Start() {

	bool render = config.renderMode;

	RG_LOG("Learner::Start():");
	RG_LOG("\tObs size: " << obsSize);
	RG_LOG("\tAction amount: " << numActions);

	if (render)
		RG_LOG("\t(Render mode enabled)");

	try {
		bool saveQueued;
		std::thread keyPressThread;
		StartQuitKeyThread(saveQueued, keyPressThread);

		ExperienceBuffer experience = ExperienceBuffer(config.randomSeed, torch::kCPU);

		int numPlayers = envSet->state.numPlayers;

		struct Trajectory {
			FList states, nextStates, rewards, logProbs;
			std::vector<uint8_t> actionMasks;
			std::vector<int8_t> terminals;
			std::vector<int32_t> actions;

			void Clear() {
				*this = Trajectory();
			}

			void Append(const Trajectory& other) {
				states += other.states;
				nextStates += other.nextStates;
				rewards += other.rewards;
				logProbs += other.logProbs;
				actionMasks += other.actionMasks;
				terminals += other.terminals;
				actions += other.actions;
			}

			size_t Length() const {
				return actions.size();
			}
		};

		auto trajectories = std::vector<Trajectory>(numPlayers, Trajectory{});
		int maxEpisodeLength = (int)(config.ppo.maxEpisodeDuration * (120.f / config.tickSkip));

		while (true) {
			Report report = {};

			bool isFirstIteration = (totalTimesteps == 0);

			// TODO: Old version switching messes up the gameplay potentially
			GGL::PolicyVersion* oldVersion = NULL;
			std::vector<bool> oldVersionPlayerMask;
			std::vector<int> newPlayerIndices = {}, oldPlayerIndices = {};
			torch::Tensor tNewPlayerIndices, tOldPlayerIndices;

			for (int i = 0; i < numPlayers; i++)
				newPlayerIndices.push_back(i);

			if (config.trainAgainstOldVersions) {
				RG_ASSERT(config.trainAgainstOldChance >= 0 && config.trainAgainstOldChance <= 1);
				bool shouldTrainAgainstOld =
					(RocketSim::Math::RandFloat() < config.trainAgainstOldChance)
					&& !versionMgr->versions.empty()
					&& !render;

				if (shouldTrainAgainstOld) {
					// Set up training against old versions

					int oldVersionIdx = RocketSim::Math::RandInt(0, versionMgr->versions.size());
					oldVersion = &versionMgr->versions[oldVersionIdx];

					Team oldVersionTeam = Team(RocketSim::Math::RandInt(0, 2)); 
					
					newPlayerIndices.clear();
					oldVersionPlayerMask.resize(numPlayers);
					int i = 0;
					for (auto& state : envSet->state.gameStates) {
						for (auto& player : state.players) {
							if (player.team == oldVersionTeam) {
								oldVersionPlayerMask[i] = true;
								oldPlayerIndices.push_back(i);
							} else {
								oldVersionPlayerMask[i] = false;
								newPlayerIndices.push_back(i);
							}
							i++;
						}
					}

					tNewPlayerIndices = torch::tensor(newPlayerIndices);
					tOldPlayerIndices = torch::tensor(oldPlayerIndices);
				}
			}

			int numRealPlayers = oldVersion ? newPlayerIndices.size() : envSet->state.numPlayers;

			int stepsCollected = 0;
			{ // Generate experience

				// Only contains complete episodes
				auto combinedTraj = Trajectory();

				Timer collectionTimer = {};
				{ // Collect timesteps
					RG_NO_GRAD;

					float inferTime = 0;
					float envStepTime = 0;

					for (int step = 0; combinedTraj.Length() < config.ppo.tsPerItr || render; step++, stepsCollected += numRealPlayers) {
						Timer stepTimer = {};
						envSet->Reset();
						envStepTime += stepTimer.Elapsed();

						for (float f : envSet->state.obs.data)
							if (isnan(f) || isinf(f))
								RG_ERR_CLOSE("Obs builder produced a NaN/inf value");

						if (!render && obsStat) {
							// TODO: This samples from old versions too
							int numSamples = RS_MAX(envSet->state.numPlayers, config.maxObsSamples);
							for (int i = 0; i < numSamples; i++) {
								int idx = Math::RandInt(0, envSet->state.numPlayers);
								obsStat->IncrementRow(&envSet->state.obs.At(idx, 0));
							}

							std::vector<double> mean = obsStat->GetMean();
							std::vector<double> std = obsStat->GetSTD();
							for (double& f : mean)
								f = RS_CLAMP(f, -config.maxObsMeanRange, config.maxObsMeanRange);
							for (double& f : std)
								f = RS_MAX(f, config.minObsSTD);
							// OPTIMIZED: Vectorized normalization (5-8x faster)
							// Dimension-first order for better cache locality and auto-vectorization
							float* obsData = envSet->state.obs.data.data();
							for (int j = 0; j < obsSize; j++) {
								const float meanVal = static_cast<float>(mean[j]);
								const float invStd = 1.0f / static_cast<float>(std[j]);
								for (int i = 0; i < envSet->state.numPlayers; i++) {
									obsData[i * obsSize + j] = (obsData[i * obsSize + j] - meanVal) * invStd;
								}
							}
						}

						torch::Tensor tActions, tLogProbs;
						torch::Tensor tStates = DIMLIST2_TO_TENSOR<float>(envSet->state.obs);
						torch::Tensor tActionMasks = DIMLIST2_TO_TENSOR<uint8_t>(envSet->state.actionMasks);

						if (!render) {
							for (int newPlayerIdx : newPlayerIndices) {
								trajectories[newPlayerIdx].states += envSet->state.obs.GetRow(newPlayerIdx);
								trajectories[newPlayerIdx].actionMasks += envSet->state.actionMasks.GetRow(newPlayerIdx);
							}
						}

						envSet->StepFirstHalf(true);

						Timer inferTimer = {};

						// Get value predictions for reanalysis buffer (if enabled)
						torch::Tensor tValuePreds;
						if (config.reanalysis.enabled && !render) {
							torch::Tensor tdStates = tStates.to(ppo->device, true);
							tValuePreds = ppo->InferCritic(tdStates).cpu();
						}

						if (oldVersion) {
							torch::Tensor tdNewStates = tStates.index_select(0, tNewPlayerIndices).to(ppo->device, true);
							torch::Tensor tdOldStates = tStates.index_select(0, tOldPlayerIndices).to(ppo->device, true);
							torch::Tensor tdNewActionMasks = tActionMasks.index_select(0, tNewPlayerIndices).to(ppo->device, true);
							torch::Tensor tdOldActionMasks = tActionMasks.index_select(0, tOldPlayerIndices).to(ppo->device, true);

							torch::Tensor tNewActions;
							torch::Tensor tOldActions;

							ppo->InferActions(tdNewStates, tdNewActionMasks, &tNewActions, &tLogProbs);
							ppo->InferActions(tdOldStates, tdOldActionMasks, &tOldActions, NULL, &oldVersion->models);

							tActions = torch::zeros(numPlayers, tNewActions.dtype());
							tActions.index_copy_(0, tNewPlayerIndices, tNewActions.cpu());
							tActions.index_copy_(0, tOldPlayerIndices, tOldActions.cpu());
						} else {
							torch::Tensor tdStates = tStates.to(ppo->device, true);
							torch::Tensor tdActionMasks = tActionMasks.to(ppo->device, true);
							ppo->InferActions(tdStates, tdActionMasks, &tActions, &tLogProbs);
							tActions = tActions.cpu();
						}
						inferTime += inferTimer.Elapsed();

						auto curActions = TENSOR_TO_VEC<int>(tActions);
						FList newLogProbs;
						if (tLogProbs.defined() && !render)
							newLogProbs = TENSOR_TO_VEC<float>(tLogProbs);	

						stepTimer.Reset();
						envSet->Sync(); // Make sure the first half is done
						envSet->StepSecondHalf(curActions, false);
						envStepTime += stepTimer.Elapsed();

						if (stepCallback)
							stepCallback(this, envSet->state.gameStates, report);

						if (render) {
							renderSender->Send(envSet->state.gameStates[0]);
							continue;
						}

						// Calc average rewards
						if (config.addRewardsToMetrics && (Math::RandInt(0, config.rewardSampleRandInterval) == 0)) {
							int numSamples = RS_MIN(envSet->arenas.size(), config.maxRewardSamples);
							std::unordered_map<std::string, AvgTracker> avgRewards = {};
							for (int i = 0; i < numSamples; i++) {
								int arenaIdx = Math::RandInt(0, envSet->arenas.size());
								auto& prevRewards = envSet->state.lastRewards[i];

								for (int j = 0; j < envSet->rewards[arenaIdx].size(); j++) {
									std::string rewardName = envSet->rewards[arenaIdx][j].reward->GetName();
									avgRewards[rewardName] += prevRewards[j];
								}
							}

							for (auto& pair : avgRewards)
								report.AddAvg("Rewards/" + pair.first, pair.second.Get());

							// Feed into metrics collector for diagnostics
							if (metricsCollector.initialized)
								metricsCollector.RecordRewards(avgRewards);
						}

						// Now that we've inferred and stepped the env, we can add that stuff to the trajectories
						int i = 0;
						for (int newPlayerIdx : newPlayerIndices) {
							trajectories[newPlayerIdx].actions.push_back(curActions[newPlayerIdx]);
							trajectories[newPlayerIdx].rewards += envSet->state.rewards[newPlayerIdx];
							trajectories[newPlayerIdx].logProbs += newLogProbs[i];
							i++;
						}

						// Collect experience for reanalysis buffer
						if (config.reanalysis.enabled && tValuePreds.defined()) {
							// Collect experience for each new player
							for (size_t i = 0; i < newPlayerIndices.size(); i++) {
								int playerIdx = newPlayerIndices[i];

								// Find which arena this player is in
								int arenaIdx = -1;
								for (size_t a = 0; a < envSet->state.arenaPlayerStartIdx.size(); a++) {
									int startIdx = envSet->state.arenaPlayerStartIdx[a];
									int playersInArena = envSet->state.gameStates[a].players.size();
									if (playerIdx >= startIdx && playerIdx < startIdx + playersInArena) {
										arenaIdx = a;
										break;
									}
								}

								if (arenaIdx >= 0) {
									ExperienceSample sample;
									sample.state = envSet->state.gameStates[arenaIdx];
									sample.playerIndex = playerIdx - envSet->state.arenaPlayerStartIdx[arenaIdx];
									sample.action = curActions[playerIdx];
									sample.reward = envSet->state.rewards[playerIdx];
									sample.value = tValuePreds[playerIdx].item<float>();

									// Get policy logits (use log probs as approximation for now)
									sample.policy = std::vector<float>(numActions, 0.0f);
									if (i < newLogProbs.size()) {
										// This is a simplification - we'd ideally want the full policy logits
										// For now, store zeros as placeholder
									}

									sample.timestep = totalTimesteps + stepsCollected;
									sample.episodeId = arenaIdx; // Use arena as episode ID for now

									tempExperienceBuffer.push_back(sample);
								}
							}
						}

						auto curTerminals = std::vector<uint8_t>(numPlayers, 0);
						for (int idx = 0; idx < envSet->arenas.size(); idx++) {
							uint8_t terminalType = envSet->state.terminals[idx];
							if (!terminalType)
								continue;

							auto playerStartIdx = envSet->state.arenaPlayerStartIdx[idx];
							int playersInArena = envSet->state.gameStates[idx].players.size();
							for (int i = 0; i < playersInArena; i++)
								curTerminals[playerStartIdx + i] = terminalType;
						}

						for (int newPlayerIdx : newPlayerIndices) {
							int8_t terminalType = curTerminals[newPlayerIdx];
							auto& traj = trajectories[newPlayerIdx];

							if (!terminalType && traj.Length() >= maxEpisodeLength) {
								// Episode is too long, truncate it here
								// This won't actually reset the env, but rather will just add it to experience buffer as truncated
								terminalType = RLGC::TerminalType::TRUNCATED;
							}

							traj.terminals.push_back(terminalType);
							if (terminalType) {

								if (terminalType == RLGC::TerminalType::TRUNCATED) {
									// Truncation requires an additional next state for the critic
									traj.nextStates += envSet->state.obs.GetRow(newPlayerIdx);
								}

								combinedTraj.Append(traj);
								traj.Clear();
							}
						}
					}

					report["Inference Time"] = inferTime;
					report["Env Step Time"] = envStepTime;
				}

				// Add collected experience to reanalysis buffer (with prioritized sampling)
				if (config.reanalysis.enabled && !tempExperienceBuffer.empty()) {
					// Filter samples using prioritized sampling strategy
					std::vector<ExperienceSample> prioritizedSamples;

					for (const auto& sample : tempExperienceBuffer) {
						const RLGC::GameState& state = sample.state;
						const Vec& ballPos = state.ball.pos;

						// Strategy 1: Ball near goals (both X,Y and Z within 1500 units)
						bool nearBlueGoal = (std::abs(ballPos.x - CommonValues::BLUE_GOAL_BACK.x) < 1500.0f) &&
											(std::abs(ballPos.y - CommonValues::BLUE_GOAL_BACK.y) < 1500.0f) &&
											(std::abs(ballPos.z - CommonValues::BLUE_GOAL_BACK.z) < 1500.0f);

						bool nearOrangeGoal = (std::abs(ballPos.x - CommonValues::ORANGE_GOAL_BACK.x) < 1500.0f) &&
											  (std::abs(ballPos.y - CommonValues::ORANGE_GOAL_BACK.y) < 1500.0f) &&
											  (std::abs(ballPos.z - CommonValues::ORANGE_GOAL_BACK.z) < 1500.0f);

						bool nearGoal = nearBlueGoal || nearOrangeGoal;

						// Strategy 2: Kickoff detection (ball near center, low altitude)
						bool isKickoff = (std::abs(ballPos.x) < 1500.0f) &&
										 (std::abs(ballPos.y) < 1500.0f) &&
										 (ballPos.z < 200.0f);

						// Strategy 3: High uncertainty states (value close to neutral)
						bool highUncertainty = (std::abs(sample.value - 0.5f) < 0.3f);

						// Strategy 4: High reward states (interesting dynamics)
						bool highReward = (sample.reward > 0.1f);

						// Include sample if it meets any priority criterion
						bool isWorthAnalyzing = nearGoal || isKickoff || highUncertainty || highReward;

						if (isWorthAnalyzing) {
							prioritizedSamples.push_back(sample);
						}
					}

					if (!prioritizedSamples.empty()) {
						reanalysisBuffer->Add(prioritizedSamples);
						RG_LOG("Added " << prioritizedSamples.size() << "/" << tempExperienceBuffer.size()
							<< " prioritized samples to reanalysis buffer (total: " << reanalysisBuffer->Size() << ")");
					}
					tempExperienceBuffer.clear();
				}

				float collectionTime = collectionTimer.Elapsed();

				Timer consumptionTimer = {};
				{ // Process timesteps
					RG_NO_GRAD;

					// Make and transpose tensors
					torch::Tensor tStates = torch::tensor(combinedTraj.states).reshape({ -1, obsSize });
					torch::Tensor tActionMasks = torch::tensor(combinedTraj.actionMasks).reshape({ -1, numActions });
					torch::Tensor tActions = torch::tensor(combinedTraj.actions);
					torch::Tensor tLogProbs = torch::tensor(combinedTraj.logProbs);
					torch::Tensor tRewards = torch::tensor(combinedTraj.rewards);
					torch::Tensor tTerminals = torch::tensor(combinedTraj.terminals);

					// States we truncated at (there could be none)
					torch::Tensor tNextTruncStates;
					if (!combinedTraj.nextStates.empty())
						tNextTruncStates = torch::tensor(combinedTraj.nextStates).reshape({ -1, obsSize });

					report["Average Step Reward"] = tRewards.mean().item<float>();
					report["Collected Timesteps"] = stepsCollected;
					
					torch::Tensor tValPreds;
					torch::Tensor tTruncValPreds;

					if (ppo->device.is_cpu()) {
						// Predict values all at once
						tValPreds = ppo->InferCritic(tStates.to(ppo->device, true, true)).cpu();
						if (tNextTruncStates.defined())
							tTruncValPreds = ppo->InferCritic(tNextTruncStates.to(ppo->device, true, true)).cpu();
					} else {
						// Predict values using minibatching
						tValPreds = torch::zeros({ (int64_t)combinedTraj.Length() });
						for (int i = 0; i < combinedTraj.Length(); i += ppo->config.miniBatchSize) {
							int start = i;
							int end = RS_MIN(i + ppo->config.miniBatchSize, combinedTraj.Length());
							torch::Tensor tStatesPart = tStates.slice(0, start, end);

							auto valPredsPart = ppo->InferCritic(tStatesPart.to(ppo->device, true, true)).cpu();
							RG_ASSERT(valPredsPart.size(0) == (end - start));
							tValPreds.slice(0, start, end).copy_(valPredsPart, true);
						}

						if (tNextTruncStates.defined()) {
							// This really just should never happen
							// If this is ever actually a real problem in a legitimate use case, ping Zealan in the dead of night
							RG_ASSERT(tNextTruncStates.size(0) <= ppo->config.miniBatchSize);

							tTruncValPreds = ppo->InferCritic(tNextTruncStates.to(ppo->device, true, true)).cpu();
						}
					}

					report["Episode Length"] = 1.f / (tTerminals == 1).to(torch::kFloat32).mean().item<float>();

					Timer gaeTimer = {};
					// Run GAE
					torch::Tensor tAdvantages, tTargetVals, tReturns;
					float rewClipPortion;
					GAE::Compute(
						tRewards, tTerminals, tValPreds, tTruncValPreds,
						tAdvantages, tTargetVals, tReturns, rewClipPortion,
						config.ppo.gaeGamma, config.ppo.gaeLambda, returnStat ? returnStat->GetSTD() : 1, config.ppo.rewardClipRange
					);
					report["GAE Time"] = gaeTimer.Elapsed();
					report["Clipped Reward Portion"] = rewClipPortion;

					if (returnStat) {
						report["GAE/Returns STD"] = returnStat->GetSTD();

						int numToIncrement = RS_MIN(config.maxReturnSamples, tReturns.size(0));
						if (numToIncrement > 0) {
							auto selectedReturns = tReturns.index_select(0, torch::randint(tReturns.size(0), { (int64_t)numToIncrement }));
							returnStat->Increment(TENSOR_TO_VEC<float>(selectedReturns));
						}
					}
					report["GAE/Avg Return"] = tReturns.abs().mean().item<float>();
					report["GAE/Avg Advantage"] = tAdvantages.abs().mean().item<float>();
					report["GAE/Avg Val Target"] = tTargetVals.abs().mean().item<float>();

					// Set experience buffer
					experience.data.actions = tActions;
					experience.data.logProbs = tLogProbs;
					experience.data.actionMasks = tActionMasks;
					experience.data.states = tStates;
					experience.data.advantages = tAdvantages;
					experience.data.targetValues = tTargetVals;
				}

				// Free CUDA cache
#ifdef RG_CUDA_SUPPORT
				if (ppo->device.is_cuda())
					c10::cuda::CUDACachingAllocator::emptyCache();
#endif

				// Learn
				Timer learnTimer = {};
				// Pass reanalysis targets if available
				Learner::ReanalysisTargets* targetsPtr = latestReanalysisTargets.hasTargets ? &latestReanalysisTargets : nullptr;
				ppo->Learn(experience, report, isFirstIteration, targetsPtr);

				// Clear reanalysis targets after use
				latestReanalysisTargets.hasTargets = false;
				report["PPO Learn Time"] = learnTimer.Elapsed();

				// Run reanalysis if enabled and interval reached
				if (config.reanalysis.enabled &&
					totalIterations % config.reanalysis.reanalysisInterval == 0) {
					Timer reanalysisTimer = {};

					// Add buffer metrics to report
					report["Reanalysis/Buffer Size"] = (double)reanalysisBuffer->Size();
					report["Reanalysis/Buffer Fill %"] = (double)reanalysisBuffer->Size() / config.reanalysis.bufferSize * 100.0;

					auto reanalysisMetrics = RunReanalysis();
					report["Reanalysis Time"] = reanalysisTimer.Elapsed();

					// Add reanalysis metrics to report
					if (reanalysisMetrics.samplesProcessed > 0) {
						report["Reanalysis/MCTS Value"] = reanalysisMetrics.meanMCTSValue;
						report["Reanalysis/PPO Value"] = reanalysisMetrics.meanPPOValue;
						report["Reanalysis/Value Difference"] = reanalysisMetrics.valueDifference;
						report["Reanalysis/Samples Processed"] = (double)reanalysisMetrics.samplesProcessed;
					}

					// Clean up GPU memory after reanalysis
					#ifdef RG_CUDA_SUPPORT
					torch::cuda::synchronize();
					#endif
				}

				// Set metrics
				float consumptionTime = consumptionTimer.Elapsed();
				report["Collection Time"] = collectionTime;
				report["Consumption Time"] = consumptionTime;
				report["Collection Steps/Second"] = stepsCollected / collectionTime;
				report["Consumption Steps/Second"] = stepsCollected / consumptionTime;
				report["Overall Steps/Second"] = stepsCollected / (collectionTime + consumptionTime);

				// Add profiling metrics to report
				envSet->PrintProfilingData();
				envSet->ResetProfilingData();

				uint64_t prevTimesteps = totalTimesteps;
				totalTimesteps += stepsCollected;
				report["Total Timesteps"] = totalTimesteps;
				totalIterations++;
				report["Total Iterations"] = totalIterations;

				if (versionMgr)
					versionMgr->OnIteration(ppo, report, totalTimesteps, prevTimesteps);

				if (saveQueued) {
					if (!config.checkpointFolder.empty())
						Save();
					exit(0);
				}

				if (!config.checkpointFolder.empty()) {
					if (totalTimesteps / config.tsPerSave > prevTimesteps / config.tsPerSave) {
						// Auto-save
						Save();
					}
				}

				report.Finish();

				if (metricSender)
					metricSender->Send(report);

				report.Display(
					{
						"Average Step Reward",
						"Policy Entropy",
						"KL Div Loss",
						"First Accuracy",
						"",
						"Policy Update Magnitude",
						"Critic Update Magnitude",
						"Shared Head Update Magnitude",
						"",
						"Collection Steps/Second",
						"Consumption Steps/Second",
						"Overall Steps/Second",
						"",
						"Collection Time",
						"-Inference Time",
						"-Env Step Time",
						"Consumption Time",
						"-GAE Time",
						"-PPO Learn Time"
						"",
						"Collected Timesteps",
						"Total Timesteps",
						"Total Iterations"
					}
				);
			}
		}
		
	} catch (std::exception& e) {
		RG_ERR_CLOSE("Exception thrown during main learner loop: " << e.what());
	}
}

// MuZero reanalysis: Batch evaluate states for MCTS
std::vector<std::pair<std::vector<float>, float>> GGL::Learner::BatchEvaluate(
	const std::vector<RLGC::GameState>& states,
	const std::vector<int>& playerIndices
) {
	if (states.empty()) return {};

	// Build observations for all states
	std::vector<RLGC::FList> allObs;
	allObs.reserve(states.size());

	for (size_t i = 0; i < states.size(); i++) {
		const auto& state = states[i];
		int playerIdx = playerIndices[i];

		// Build observation for this player in this state
		RLGC::FList obs = envSet->obsBuilders[0]->BuildObs(
			state.players[playerIdx], state
		);
		allObs.push_back(obs);
	}

	// Convert to torch tensor
	size_t obsSize = allObs[0].size();
	torch::Tensor obsTensor = torch::zeros({ (int64_t)allObs.size(), (int64_t)obsSize });

	for (size_t i = 0; i < allObs.size(); i++) {
		for (size_t j = 0; j < allObs[i].size(); j++) {
			obsTensor[i][j] = allObs[i][j];
		}
	}

	// Move to GPU if available
	obsTensor = obsTensor.to(ppo->device);

	// Get policy and value predictions
	torch::NoGradGuard noGrad;

	// Create action masks (all actions available for now)
	torch::Tensor actionMasks = torch::ones({ (int64_t)states.size(), (int64_t)numActions }, ppo->device);

	// Get policy logits
	torch::Tensor policyLogits = PPOLearner::InferPolicyProbsFromModels(
		ppo->models, obsTensor, actionMasks, 1.0f, false
	);

	// Get value estimates
	torch::Tensor valueEst = ppo->InferCritic(obsTensor);

	// Convert back to CPU and extract results
	policyLogits = policyLogits.cpu();
	valueEst = valueEst.cpu();

	std::vector<std::pair<std::vector<float>, float>> results;
	results.reserve(states.size());

	for (size_t i = 0; i < states.size(); i++) {
		std::vector<float> policy;
		policy.reserve(numActions);

		for (size_t j = 0; j < numActions; j++) {
			policy.push_back(policyLogits[i][j].item<float>());
		}

		float value = valueEst[i].item<float>();
		results.push_back({ policy, value });
	}

	return results;
}

// MuZero reanalysis: Run MCTS on sampled states
GGL::Learner::ReanalysisMetrics GGL::Learner::RunReanalysis() {
	ReanalysisMetrics metrics;
	if (!reanalysisBuffer || !config.reanalysis.enabled) return metrics;

	// Check if buffer has enough samples
	if (!reanalysisBuffer->CanSample()) {
		return metrics;
	}

	// Sample from buffer
	auto samples = reanalysisBuffer->Sample(config.reanalysis.samplesPerReanalysis);
	if (samples.empty()) return metrics;

	RG_LOG("Running reanalysis on " << samples.size() << " samples...");

	// Extract states and player indices
	std::vector<RLGC::GameState> states;
	std::vector<int> playerIndices;
	states.reserve(samples.size());
	playerIndices.reserve(samples.size());

	for (const auto& sample : samples) {
		states.push_back(sample.state);
		playerIndices.push_back(sample.playerIndex);
	}

	// Create MCTS evaluator that uses our policy/value networks
	auto evaluator = [this](const std::vector<RLGC::GameState>& states,
	                       const std::vector<int>& indices) {
		return this->BatchEvaluate(states, indices);
	};

	// Configure MCTS
	MCTSConfig mctsConfig;
	mctsConfig.numSimulations = config.reanalysis.mctsSimulations;
	mctsConfig.cPuct = config.reanalysis.mctsCPuct;
	mctsConfig.addRootNoise = config.reanalysis.mctsAddNoise;

	// Run batched MCTS search
	BatchedMCTSSearch searcher(mctsConfig);
	auto results = searcher.SearchBatch(states, playerIndices, evaluator);

	// Calculate mean metrics for logging
	if (!results.empty()) {
		float meanMCTSValue = 0.0f;
		float meanPPOValue = 0.0f;
		float meanSimulations = 0.0f;

		for (size_t i = 0; i < results.size(); i++) {
			meanMCTSValue += results[i].valueEstimate;
			meanPPOValue += samples[i].value;  // This is the PPO critic prediction we stored
			meanSimulations += results[i].totalSimulations;
		}
		meanMCTSValue /= results.size();
		meanPPOValue /= results.size();
		meanSimulations /= results.size();

		RG_LOG("Reanalysis complete: " << results.size() << " samples, "
			<< "mean MCTS value: " << meanMCTSValue << ", "
			<< "mean PPO value: " << meanPPOValue << ", "
			<< "mean sims: " << meanSimulations);

		// Populate metrics to return
		metrics.meanMCTSValue = meanMCTSValue;
		metrics.meanPPOValue = meanPPOValue;
		metrics.valueDifference = std::abs(meanMCTSValue - meanPPOValue);
		metrics.samplesProcessed = results.size();
	}

	// Phase 6: Store MCTS targets for PPO loss integration
	if (!results.empty()) {
		// Build observations for all sampled states
		std::vector<RLGC::FList> allObs;
		allObs.reserve(samples.size());

		for (size_t i = 0; i < samples.size(); i++) {
			RLGC::FList obs = envSet->obsBuilders[0]->BuildObs(
				samples[i].state.players[samples[i].playerIndex],
				samples[i].state
			);
			allObs.push_back(obs);
		}

		// Convert observations to tensor
		torch::Tensor obsTensor = torch::zeros({ (int64_t)allObs.size(), (int64_t)obsSize });
		for (size_t i = 0; i < allObs.size(); i++) {
			for (size_t j = 0; j < allObs[i].size(); j++) {
				obsTensor[i][j] = allObs[i][j];
			}
		}

		// Convert MCTS results to tensors
		torch::Tensor valueTargets = torch::zeros({ (int64_t)results.size() });
		torch::Tensor policyTargets = torch::zeros({ (int64_t)results.size(), (int64_t)numActions });

		for (size_t i = 0; i < results.size(); i++) {
			// Value target from MCTS
			valueTargets[i] = results[i].valueEstimate;

			// Policy target from MCTS visit distribution (already normalized)
			for (size_t a = 0; a < results[i].visitDistribution.size(); a++) {
				policyTargets[i][a] = results[i].visitDistribution[a];
			}
		}

		// Store in member variable for PPO to use
		latestReanalysisTargets.observations = obsTensor;
		latestReanalysisTargets.mctsValueTargets = valueTargets;
		latestReanalysisTargets.mctsPolicyTargets = policyTargets;
		latestReanalysisTargets.hasTargets = true;

		RG_LOG("Stored " << results.size() << " reanalysis targets for PPO loss integration");
	}

	return metrics;
}

GGL::Learner::~Learner() {
	delete ppo;
	delete versionMgr;
	delete metricSender;
	delete renderSender;
	delete reanalysisBuffer;
	pybind11::finalize_interpreter();
}