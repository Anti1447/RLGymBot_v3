#include "PPOLearner.h"
#include "../Util/MixerModelWrapper.h"
#include "../Util/InferenceOptimizer.h"

#include <torch/nn/utils/convert_parameters.h>
#include <torch/nn/utils/clip_grad.h>
#include <torch/csrc/api/include/torch/serialize.h>
#include <public/GigaLearnCPP/Util/AvgTracker.h>

#include <fstream>
#include <sstream>

using namespace torch;

// ============================================================================
// 2025-12-21 | Rev 2 | NJH
// - Export RLBot-ready inference artifact: PPO_POLICY.pth (CPU) + PPO_POLICY_META.json
// - Does NOT overwrite POLICY.lt (training checkpoint uses that file)
// - Uses torch::save(nn::Sequential) so Python can load with:
//     torch.load('PPO_POLICY.pth', map_location='cpu', weights_only=False)
// Option 1: Save individual tensors (Recommended for RLBot)
// This is the most compatible approach for Python
// ============================================================================

static void ExportRLBOTPolicyStateDict(GGL::ModelSet& models, const std::filesystem::path& folderPath) {
	if (!models["policy"])
		RG_ERR_CLOSE("ExportRLBOTPolicyStateDict: models[\"policy\"] is null");
	auto* policy = dynamic_cast<GGL::Model*>(models["policy"]);
	auto* shared = dynamic_cast<GGL::Model*>(models["shared_head"]);

	if (!policy)
		RG_ERR_CLOSE("ExportRLBOTPolicyStateDict: policy is not a Model type");

	torch::NoGradGuard noGrad;
	torch::nn::Sequential combo;
	const int obsDim = shared ? shared->config.numInputs : policy->config.numInputs;
	const int actDim = policy->config.numOutputs;

	auto append_clones = [&](torch::nn::Sequential& src) {
		for (auto& mod : *src)
			combo->push_back(mod.clone());
		};

	if (shared) {
		shared->seq->eval();
		append_clones(shared->seq);
	}
	policy->seq->eval();
	append_clones(policy->seq);
	combo->eval();
	combo->to(torch::kCPU);

	std::filesystem::create_directories(folderPath);
	const auto outPath = folderPath / "PPO_POLICY_SD.pt";

	// Save as state_dict compatible with Python
	{
		torch::OrderedDict<std::string, torch::Tensor> sd;
		for (const auto& item : combo->named_parameters(/*recurse=*/true))
			sd.insert(item.key(), item.value().cpu());
		for (const auto& item : combo->named_buffers(/*recurse=*/true))
			sd.insert(item.key(), item.value().cpu());

		torch::serialize::OutputArchive archive;
		for (const auto& pair : sd) {
			archive.write(pair.key(), pair.value());
		}
		archive.save_to(outPath.string());

		RG_LOG("Saved RLBot policy state_dict to: " << outPath);
	}

	// Metadata
	{
		const auto metaPath = folderPath / "PPO_POLICY_SD_META.json";
		auto metaOut = std::ofstream(metaPath);
		metaOut
			<< "{\n"
			<< "  \"format\": \"rlbot_state_dict_v1\",\n"
			<< "  \"obs_dim\": " << obsDim << ",\n"
			<< "  \"act_dim\": " << actDim << ",\n"
			<< "  \"export_device\": \"cpu\"\n"
			<< "}\n";
		RG_LOG("Saved RLBot policy metadata to: " << metaPath);
	}

	RG_LOG("PPOLearner: Exported RLBot policy to " << outPath
		<< " (obs_dim=" << obsDim << ", act_dim=" << actDim << ")");
}

GGL::PPOLearner::PPOLearner(int obsSize, int numActions, PPOLearnerConfig _config, Device _device) : config(_config), device(_device) {

	// Initialize inference optimizations
	if (device.is_cpu()) {
		// CPU: MKL-DNN, threading, etc. (5-15x speedup)
		InferenceOptimizer::Initialize();
	} else {
		// GPU: cuDNN benchmark, TF32, etc. (8-12x speedup)
		if (torch::cuda::is_available()) {
			RG_LOG("=== Initializing CUDA Inference Optimizations ===");
			at::globalContext().setBenchmarkCuDNN(true);
			at::globalContext().setAllowTF32CuDNN(true);
			at::globalContext().setAllowTF32CuBLAS(true);
			RG_LOG("Enabled cuDNN benchmark & TF32 precision");
			RG_LOG("=== CUDA Optimizations Complete ===");
		}
	}

	if (config.miniBatchSize == 0)
		config.miniBatchSize = config.batchSize;

	if (config.batchSize % config.miniBatchSize != 0)
		RG_ERR_CLOSE("PPOLearner: config.batchSize (" << config.batchSize << ") must be a multiple of config.miniBatchSize (" << config.miniBatchSize << ")");

	if (config.networkArch == NetworkArchitecture::MLP_MIXER) {
		// For MLP-Mixer, we need to know numPlayers from observation structure
		// Standard AdvancedObs format: ball(6) + player(12*N) where N includes self and others
		// We infer numPlayers from obsSize
		// obsSize = 6 (ball) + 12 * numPlayers
		// numPlayers = (obsSize - 6) / 12
		int numPlayers = (obsSize - 6) / 12;
		if (numPlayers <= 0 || (obsSize - 6) % 12 != 0) {
			RG_ERR_CLOSE("PPOLearner: Cannot infer numPlayers from obsSize (" << obsSize << ") for MLP-Mixer. "
				<< "Expected obsSize = 6 (ball) + 12*numPlayers. Example: obsSize=30 for 2v2 (1+4 players)");
		}
		MakeMixerModels(true, obsSize, numPlayers, numActions, device, models, config.mixerConfig);
	} else {
		MakeModels(true, obsSize, numActions, config.sharedHead, config.policy, config.critic, device, models);
	}

	SetLearningRates(config.policyLR, config.criticLR);

	// Print param counts
	RG_LOG("Model parameter counts:");
	uint64_t total = 0;
	for (auto model : this->models) {
		uint64_t count = model->GetParamCount();
		RG_LOG("\t\"" << model->getModelName() << "\": " << Utils::NumToStr(count));
		total += count;
	}
	RG_LOG("\t[Total]: " << Utils::NumToStr(total));

	if (config.useGuidingPolicy) {
		RG_LOG("Guiding policy enabled, loading from " << config.guidingPolicyPath << "...");
		MakeModels(false, obsSize, numActions, config.sharedHead, config.policy, config.critic, device, guidingPolicyModels);
		guidingPolicyModels.Load(config.guidingPolicyPath, false, false);
	}
}

void GGL::PPOLearner::MakeModels(
	bool makeCritic,
	int obsSize, int numActions, 
	PartialModelConfig sharedHeadConfig, PartialModelConfig policyConfig, PartialModelConfig criticConfig,
	torch::Device device, 
	ModelSet& outModels) {

	ModelConfig fullPolicyConfig = policyConfig;
	fullPolicyConfig.numInputs = obsSize;
	fullPolicyConfig.numOutputs = numActions;

	ModelConfig fullCriticConfig = criticConfig;
	fullCriticConfig.numInputs = obsSize;
	fullCriticConfig.numOutputs = 1;

	if (sharedHeadConfig.IsValid()) {

		ModelConfig fullSharedHeadConfig = sharedHeadConfig;
		fullSharedHeadConfig.numInputs = obsSize;
		fullSharedHeadConfig.numOutputs = 0;

		RG_ASSERT(!sharedHeadConfig.addOutputLayer);

		fullPolicyConfig.numInputs = fullSharedHeadConfig.layerSizes.back();
		fullCriticConfig.numInputs = fullSharedHeadConfig.layerSizes.back();

		outModels.Add(new Model("shared_head", fullSharedHeadConfig, device));
	}

	outModels.Add(new Model("policy", fullPolicyConfig, device));

	if (makeCritic)
		outModels.Add(new Model("critic", fullCriticConfig, device));
}

void GGL::PPOLearner::MakeMixerModels(
	bool makeCritic,
	int obsSize, int numPlayers, int numActions,
	torch::Device device,
	ModelSet& outModels,
	const MixerConfig& mixerConfig) {

	// Create MLP-Mixer policy network
	// The MixerModelWrapper handles tokenization internally
	auto policyWrapper = new MixerModelWrapper(
		"policy",
		obsSize,
		numPlayers,
		numActions,
		mixerConfig,
		device,
		ModelOptimType::ADAMW
	);

	// Validate dimensions match our expectations
	TORCH_CHECK(policyWrapper->config.numInputs == obsSize,
		"Policy mixer obsSize mismatch: expected ", obsSize, ", got ", policyWrapper->config.numInputs);
	TORCH_CHECK(policyWrapper->config.numOutputs == numActions,
		"Policy mixer action space mismatch: expected ", numActions, ", got ", policyWrapper->config.numOutputs);

	// Store in ModelSet (now using IModel* for type safety)
	outModels.map["policy"] = policyWrapper;

	if (makeCritic) {
		auto criticWrapper = new MixerModelWrapper(
			"critic",
			obsSize,
			numPlayers,
			1,  // Value network outputs single value
			mixerConfig,
			device,
			ModelOptimType::ADAMW
		);

		TORCH_CHECK(criticWrapper->config.numInputs == obsSize,
			"Critic mixer obsSize mismatch");
		TORCH_CHECK(criticWrapper->config.numOutputs == 1,
			"Critic mixer must output single value");

		outModels.map["critic"] = criticWrapper;
	}

	RG_LOG("MixerModels created:");
	RG_LOG("\t  numPlayers: " << numPlayers);
	RG_LOG("\t  obs_size: " << obsSize);
	RG_LOG("\t  num_actions: " << numActions);
	RG_LOG("\t  mixer_blocks: " << mixerConfig.numMixerBlocks);
	RG_LOG("\t  hidden_dim: " << mixerConfig.hiddenDim);
	RG_LOG("\t  token_dim: " << mixerConfig.tokenDim);
}

torch::Tensor GGL::PPOLearner::InferPolicyProbsFromModels(
	ModelSet& models,
	torch::Tensor obs, torch::Tensor actionMasks,
	float temperature, bool halfPrec) {

	actionMasks = actionMasks.to(torch::kBool);

	constexpr float ACTION_MIN_PROB = 1e-11f;
	constexpr float ACTION_DISABLED_LOGIT = -1e10f;

	if (models["shared_head"])
		obs = models["shared_head"]->Forward(obs, halfPrec);

	auto logits = models["policy"]->Forward(obs, halfPrec) / temperature;

	auto* policy = dynamic_cast<GGL::Model*>(models["policy"]);
	if (!policy)
		RG_ERR_CLOSE("InferPolicyProbsFromModels: policy is not a Model type");

	auto result = torch::softmax(logits + ACTION_DISABLED_LOGIT * actionMasks.logical_not(), -1);
	return result.view({ -1, policy->config.numOutputs }).clamp(ACTION_MIN_PROB, 1);
}

void GGL::PPOLearner::InferActionsFromModels(
	ModelSet& models,
	torch::Tensor obs, torch::Tensor actionMasks, 
	bool deterministic, float temperature, bool halfPrec,
	torch::Tensor* outActions, torch::Tensor* outLogProbs) {

	auto probs = InferPolicyProbsFromModels(models, obs, actionMasks, temperature, halfPrec);

	if (deterministic) {
		auto action = probs.argmax(1);
		if (outActions)
			*outActions = action.flatten();
	} else {
		auto action = torch::multinomial(probs, 1, true);
		auto logProb = torch::log(probs).gather(-1, action);
		if (outActions)
			*outActions = action.flatten();

		if (outLogProbs)
			*outLogProbs = logProb.flatten();
	}
}

void GGL::PPOLearner::InferActions(torch::Tensor obs, torch::Tensor actionMasks, torch::Tensor* outActions, torch::Tensor* outLogProbs, ModelSet* models) {
	InferActionsFromModels(models ? *models : this->models, obs, actionMasks, config.deterministic, config.policyTemperature, config.useHalfPrecision, outActions, outLogProbs);
}

torch::Tensor GGL::PPOLearner::InferCritic(torch::Tensor obs) {

	if (models["shared_head"])
		obs = models["shared_head"]->Forward(obs, config.useHalfPrecision);

	return models["critic"]->Forward(obs, config.useHalfPrecision).flatten();
}

torch::Tensor ComputeEntropy(torch::Tensor probs, torch::Tensor actionMasks, bool maskEntropy) {
	// Compute log probs and entropy
	auto entropy = -(probs.log() * probs).sum(-1);

	if (maskEntropy) {
		// Account for action masking in entropy
		// We will effectively narrow the entropy to the scope of the valid actions
		// This way states with more masked actions don't just have inherently lower entropy
		entropy /= actionMasks.to(torch::kFloat32).sum(-1).log();
	} else {
		entropy /= logf(actionMasks.size(-1));
	}

	return entropy.mean();
}

void GGL::PPOLearner::Learn(ExperienceBuffer& experience, Report& report, bool isFirstIteration, GGL::Learner::ReanalysisTargets* reanalysisTargets) {
	auto mseLoss = torch::nn::MSELoss();

	// Get action count from policy model
	auto* policy = dynamic_cast<Model*>(models["policy"]);
	if (!policy)
		RG_ERR_CLOSE("Learn: policy is not a Model type");
	const int actionCount = policy->config.numOutputs;

	MutAvgTracker
		avgEntropy,
		avgDivergence,
		avgPolicyLoss,
		avgRelEntropyLoss,
		avgCriticLoss,
		avgGuidingLoss,
		avgRatio,
		avgClip;

	// Save parameters first
	auto policyBefore = models["policy"]->CopyParams();
	auto criticBefore = models["critic"]->CopyParams();

	bool trainPolicy = config.policyLR != 0;
	bool trainCritic = config.criticLR != 0;
	bool trainSharedHead = models["shared_head"] && (trainPolicy || trainCritic);

	for (int epoch = 0; epoch < config.epochs; epoch++) {

		// Get randomly-ordered timesteps for PPO
		auto batches = experience.GetAllBatchesShuffled(config.batchSize, config.overbatching);

		for (auto& batch : batches) {
			auto batchActs = batch.actions;
			auto batchOldProbs = batch.logProbs;
			auto batchObs = batch.states;
			auto batchActionMasks = batch.actionMasks;
			auto batchTargetValues = batch.targetValues;
			auto batchAdvantages = batch.advantages;

			auto fnRunMinibatch = [&](int start, int stop) {

				float batchSizeRatio = (stop - start) / (float)config.batchSize;

				// Send everything to the device and enforce correct shapes
				auto acts = batchActs.slice(0, start, stop).to(device, true, true);
				auto obs = batchObs.slice(0, start, stop).to(device, true, true);
				auto actionMasks = batchActionMasks.slice(0, start, stop).to(device, true, true);
				
				auto advantages = batchAdvantages.slice(0, start, stop).to(device, true, true);
				auto oldProbs = batchOldProbs.slice(0, start, stop).to(device, true, true);
				auto targetValues = batchTargetValues.slice(0, start, stop).to(device, true, true);

				torch::Tensor probs, logProbs, entropy, ratio, clipped, policyLoss, ppoLoss;
				if (trainPolicy) {

					// Get policy log probs and entropy
					float curEntropy;
					{
						probs = InferPolicyProbsFromModels(models, obs, actionMasks, config.policyTemperature, false);
						logProbs = probs.log().gather(-1, acts.unsqueeze(-1));
						entropy = ComputeEntropy(probs, actionMasks, config.maskEntropy);
						curEntropy = entropy.detach().cpu().item<float>();
						avgEntropy += curEntropy;
					}

					logProbs = logProbs.view_as(oldProbs);

					// Compute PPO loss
					ratio = exp(logProbs - oldProbs);
					avgRatio += ratio.mean().detach().cpu().item<float>();
					clipped = clamp(
						ratio, 1 - config.clipRange, 1 + config.clipRange
					);

					// Compute policy loss
					policyLoss = -min(
						ratio * advantages, clipped * advantages
					).mean();
					float curPolicyLoss = policyLoss.detach().cpu().item<float>();
					avgPolicyLoss += curPolicyLoss;

					avgRelEntropyLoss += (curEntropy * config.entropyScale) / curPolicyLoss;

					ppoLoss = (policyLoss - entropy * config.entropyScale) * batchSizeRatio;

					if (config.useGuidingPolicy) {
						torch::Tensor guidingProbs;
						{
							RG_NO_GRAD;
							guidingProbs = InferPolicyProbsFromModels(guidingPolicyModels, obs, actionMasks, config.policyTemperature, config.useHalfPrecision);
						}

						auto guidingLoss = (guidingProbs - probs).abs().mean();
						avgGuidingLoss.Add(guidingLoss.detach().cpu().item<float>());
						guidingLoss = guidingLoss * config.guidingStrength;
						ppoLoss = ppoLoss + guidingLoss;
					}
				}

				torch::Tensor criticLoss;
				if (trainCritic) {
					auto vals = InferCritic(obs);

					// Compute value loss
					vals = vals.view_as(targetValues);
					criticLoss = mseLoss(vals, targetValues) * batchSizeRatio;
					avgCriticLoss += criticLoss.detach().cpu().item<float>();
				}

				// ===== ADD REANALYSIS LOSS HERE =====
				// Phase 6: Add reanalysis loss if targets available
				if (reanalysisTargets && reanalysisTargets->hasTargets) {
					// Move reanalysis targets to device
					auto reanalysisObs = reanalysisTargets->observations.to(device);
					auto mctsValueTargets = reanalysisTargets->mctsValueTargets.to(device);
					auto mctsPolicyTargets = reanalysisTargets->mctsPolicyTargets.to(device);

					// Get current policy and value predictions for reanalysis states
					torch::Tensor reanalysisActionMasks = torch::ones({ reanalysisObs.size(0), actionCount }, device);

					// Policy reanalysis loss
					torch::Tensor policyReanalysisLoss;
					if (trainPolicy) {
						auto reanalysisPolicyProbs = InferPolicyProbsFromModels(models, reanalysisObs, reanalysisActionMasks, config.policyTemperature, false);

						// KL divergence: KL(MCTS || Policy) = sum(MCTS * log(MCTS / Policy))
						auto logPolicyProbs = torch::log(reanalysisPolicyProbs + 1e-8f);
						auto logMCTSProbs = torch::log(mctsPolicyTargets + 1e-8f);
						policyReanalysisLoss = torch::sum(
							mctsPolicyTargets * (logMCTSProbs - logPolicyProbs),
							-1
						).mean() * batchSizeRatio;
					}

					// Value reanalysis loss
					torch::Tensor valueReanalysisLoss;
					if (trainCritic) {
						auto reanalysisValuePreds = InferCritic(reanalysisObs).squeeze(-1);
						valueReanalysisLoss = torch::mse_loss(reanalysisValuePreds, mctsValueTargets) * batchSizeRatio;
					}

					// Add weighted reanalysis losses
					if (trainPolicy && policyReanalysisLoss.defined()) {
						ppoLoss = ppoLoss + config.policyReanalysisWeight * policyReanalysisLoss;
					}
					if (trainCritic && valueReanalysisLoss.defined()) {
						criticLoss = criticLoss + config.valueReanalysisWeight * valueReanalysisLoss;
					}

					// Track metrics (only once per epoch, not per minibatch)
					if (start == 0) {
						if (policyReanalysisLoss.defined())
							report["Reanalysis/Policy Loss"] = policyReanalysisLoss.detach().cpu().item<float>();
						if (valueReanalysisLoss.defined())
							report["Reanalysis/Value Loss"] = valueReanalysisLoss.detach().cpu().item<float>();
					}
				}
				// ===== END REANALYSIS LOSS =====

				if (trainPolicy) {
					// Compute KL divergence & clip fraction using SB3 method for reporting;
					{
						RG_NO_GRAD;

						auto logRatio = logProbs - oldProbs;
						auto klTensor = (exp(logRatio) - 1) - logRatio;
						avgDivergence += klTensor.mean().detach().cpu().item<float>();

						auto clipFraction = mean((abs(ratio - 1) > config.clipRange).to(kFloat));
						avgClip += clipFraction.cpu().item<float>();
					}
				}

				if (trainPolicy && trainCritic) {
					auto combinedLoss = ppoLoss + criticLoss;  // This now includes reanalysis!
					combinedLoss.backward();
				}
				else {
					if (trainPolicy)
						ppoLoss.backward();
					if (trainCritic)
						criticLoss.backward();
				}
			};

			
			if (device.is_cpu()) {
				// Just run one minibatch
				fnRunMinibatch(0, config.batchSize);
			} else {
				for (int mbs = 0; mbs < config.batchSize; mbs += config.miniBatchSize) {
					int start = mbs;
					int stop = start + config.miniBatchSize;
					fnRunMinibatch(start, stop);
				}
			}

			if (trainPolicy)
				nn::utils::clip_grad_norm_(models["policy"]->parameters(), 0.5f);
			if (trainCritic)
				nn::utils::clip_grad_norm_(models["critic"]->parameters(), 0.5f);

			if (trainSharedHead)
				nn::utils::clip_grad_norm_(models["shared_head"]->parameters(), 0.5f);

			models.StepOptims();
		}
	}

	// Compute magnitude of updates made to the policy and value estimator
	auto policyAfter = models["policy"]->CopyParams();
	auto criticAfter = models["critic"]->CopyParams();

	float policyUpdateMagnitude = (policyBefore - policyAfter).norm().item<float>();
	float criticUpdateMagnitude = (criticBefore - criticAfter).norm().item<float>();

	// Assemble and return report
	report["Policy Entropy"] = avgEntropy.Get();
	report["Mean KL Divergence"] = avgDivergence.Get();
	if (!isFirstIteration) {
		// These metrics give bad data on the first iteration, which will mess up graph scaling
		// So we'll just skip them for the first iteration
		report["Policy Loss"] = avgPolicyLoss.Get();
		report["Policy Relative Entropy Loss"] = avgRelEntropyLoss.Get();
		report["Critic Loss"] = avgCriticLoss.Get();

		if (config.useGuidingPolicy)
			report["Guiding Loss"] = avgGuidingLoss.Get();

		report["SB3 Clip Fraction"] = avgClip.Get();
		report["Policy Update Magnitude"] = policyUpdateMagnitude;
		report["Critic Update Magnitude"] = criticUpdateMagnitude;
	}
}

void GGL::PPOLearner::TransferLearn(
	ModelSet& oldModels,
	torch::Tensor newObs, torch::Tensor oldObs,
	torch::Tensor newActionMasks, torch::Tensor oldActionMasks,
	torch::Tensor actionMaps,
	Report& report,
	const TransferLearnConfig& tlConfig
) {

	torch::Tensor oldProbs;
	{ // No grad for old model inference
		RG_NO_GRAD;
		oldProbs = InferPolicyProbsFromModels(oldModels, oldObs, oldActionMasks, config.policyTemperature, config.useHalfPrecision);
		report["Old Policy Entropy"] = ComputeEntropy(oldProbs, oldActionMasks, config.maskEntropy).detach().cpu().item<float>();

		if (actionMaps.defined())
			oldProbs = oldProbs.gather(1, actionMaps);
	}

	for (auto& model : GetPolicyModels())
		model->SetOptimLR(tlConfig.lr);

	auto policyBefore = models["policy"]->CopyParams();
	
	for (int i = 0; i < tlConfig.epochs; i++) {
		torch::Tensor newProbs = InferPolicyProbsFromModels(models, newObs, newActionMasks, config.policyTemperature, false);

		// Non-summative KL div	loss
		torch::Tensor transferLearnLoss;
		if (tlConfig.useKLDiv) {
			transferLearnLoss = (oldProbs * torch::log(oldProbs / newProbs)).abs();
		} else {
			transferLearnLoss = (oldProbs - newProbs).abs();
		}
		transferLearnLoss = transferLearnLoss.pow(tlConfig.lossExponent);
		transferLearnLoss = transferLearnLoss.mean();
		transferLearnLoss *= tlConfig.lossScale;

		if (i == 0) {
			RG_NO_GRAD;
			torch::Tensor matchingActionsMask = (newProbs.detach().argmax(-1) == oldProbs.detach().argmax(-1));
			report["Transfer Learn Accuracy"] = matchingActionsMask.to(torch::kFloat).mean().cpu().item<float>();
			report["Transfer Learn Loss"] = transferLearnLoss.detach().cpu().item<float>();

			report["Policy Entropy"] = ComputeEntropy(newProbs, newActionMasks, config.maskEntropy).detach().cpu().item<float>();
		}

		transferLearnLoss.backward();

		models.StepOptims();
	}

	auto policyAfter = models["policy"]->CopyParams();
	report["Policy Update Magnitude"] = (policyBefore - policyAfter).norm().item<float>();
}

void GGL::PPOLearner::SaveTo(std::filesystem::path folderPath) {
	models.Save(folderPath);
	// RLBot export (separate artifact; must not overwrite POLICY.lt)
	ExportRLBOTPolicyStateDict(models, folderPath);
}

void GGL::PPOLearner::LoadFrom(std::filesystem::path folderPath)  {
	if (!std::filesystem::is_directory(folderPath))
		RG_ERR_CLOSE("PPOLearner:LoadFrom(): Path " << folderPath << " is not a valid directory");

	models.Load(folderPath, true, true);

	SetLearningRates(config.policyLR, config.criticLR);
}

void GGL::PPOLearner::SetLearningRates(float policyLR, float criticLR) {
	config.policyLR = policyLR;
	config.criticLR = criticLR;

	models["policy"]->SetOptimLR(policyLR);
	models["critic"]->SetOptimLR(criticLR);

	if (models["shared_head"])
		models["shared_head"]->SetOptimLR(RS_MIN(policyLR, criticLR));

	RG_LOG("PPOLearner: " << RS_STR(std::scientific << "Set learning rate to [" << policyLR << ", " << criticLR << "]"));
}

GGL::ModelSet GGL::PPOLearner::GetPolicyModels() {
	ModelSet result = {};
	for (IModel* model : models) {
		if (std::string(model->getModelName()) == "critic")
			continue;

		result.Add(model);
	}
	return result;
}