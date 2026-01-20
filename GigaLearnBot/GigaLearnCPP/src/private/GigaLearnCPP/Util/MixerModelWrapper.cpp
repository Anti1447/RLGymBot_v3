#include "MixerModelWrapper.h"
#include "Models.h"

#include <torch/csrc/api/include/torch/serialize.h>

GGL::MixerModelWrapper::MixerModelWrapper(
	const char* modelName,
	int obsSize,
	int numPlayers,
	int numOutputs,
	const MixerConfig& mixerCfg,
	torch::Device device,
	ModelOptimType optimType
) :
	modelName(modelName),
	device(device),
	config(PartialModelConfig{}),
	mixerConfig(mixerCfg),
	tokenizer(TokenizerConfig{
		.obsSize = obsSize,
		.numPlayers = numPlayers,
		.includeBoostPads = false,
		.maxBoostPads = 0,
		.featuresPerToken = 12
	}),
	mixerNet(
		tokenizer.getTokenDim(),        // inputDim = token features
		tokenizer.getNumTokens(),       // numTokens
		mixerCfg.hiddenDim,             // hiddenDim for mixer
		numOutputs,                     // outputDim
		mixerCfg.numMixerBlocks         // numBlocks
	)
{
	// Set up config
	config.numInputs = obsSize;
	config.numOutputs = numOutputs;
	config.optimType = optimType;

	mixerNet->to(device);
	optim = MakeOptimizer(config.optimType, mixerNet->parameters(), 0);
}

torch::Tensor GGL::MixerModelWrapper::Forward(torch::Tensor input, bool halfPrec) {
	// input shape: [batch_size, obsSize]
	// Step 1: Tokenize the observations
	auto tokens = tokenizer.tokenize(input);
	// tokens shape: [batch_size, num_tokens, token_dim]

	if (torch::GradMode::is_enabled())
		halfPrec = false;

	if (halfPrec) {
		// Half-precision inference
		if (_mixerHalfOutdated) {
			_mixerHalfOutdated = false;

			if (!mixerNetHalf) {
				mixerNetHalf = std::make_shared<MixerNetwork>(
					tokenizer.getTokenDim(),
					tokenizer.getNumTokens(),
					mixerConfig.hiddenDim,
					config.numOutputs,
					mixerConfig.numMixerBlocks
				);
				(*mixerNetHalf)->to(device);
				(*mixerNetHalf)->to(RG_HALFPERC_TYPE, true);
			}

			auto fromParams = mixerNet->parameters();
			auto toParams = (*mixerNetHalf)->parameters();
			for (size_t i = 0; i < fromParams.size(); i++) {
				auto scaledParams = fromParams[i].to(device, RG_HALFPERC_TYPE, true);
				toParams[i].copy_(scaledParams, true);
			}
		}

		auto halfTokens = tokens.to(device, RG_HALFPERC_TYPE);
		auto halfOutput = (*mixerNetHalf)->forward(halfTokens);
		return halfOutput.to(device, torch::kFloat);
	} else {
		// Step 2: Pass tokens through mixer network
		return mixerNet->forward(tokens);
	}
}

void GGL::MixerModelWrapper::SetOptimLR(float newLR) {
	SetOptimizerLR(optim, config.optimType, newLR);
}

void GGL::MixerModelWrapper::StepOptim() {
	optim->step();
	optim->zero_grad();
	_mixerHalfOutdated = true;
}

void GGL::MixerModelWrapper::Save(std::filesystem::path folder, bool saveOptim) {
	std::filesystem::create_directories(folder);
	std::filesystem::path path = GetSavePath(folder);
	auto streamOut = std::ofstream(path, std::ios::binary);
	torch::save(mixerNet, streamOut);

	if (saveOptim) {
		torch::serialize::OutputArchive optimArchive;
		optim->save(optimArchive);
		optimArchive.save_to(GetOptimSavePath(folder).string());
	}
}

void GGL::MixerModelWrapper::Load(std::filesystem::path folder, bool allowNotExist, bool loadOptim) {
	std::filesystem::path path = GetSavePath(folder);

	if (!std::filesystem::exists(path)) {
		if (allowNotExist) {
			return;
		} else {
			RG_ERR_CLOSE("Failed to load MixerModel \"" << modelName << "\" from path: " << path.string());
		}
	}

	auto streamIn = std::ifstream(path, std::ios::binary);
	torch::load(mixerNet, streamIn);
	_mixerHalfOutdated = true;

	if (loadOptim) {
		std::filesystem::path optimPath = GetOptimSavePath(folder);
		if (std::filesystem::exists(optimPath)) {
			torch::serialize::InputArchive optimArchive;
			optimArchive.load_from(optimPath.string());
			optim->load(optimArchive);
		}
	}
}

torch::Tensor GGL::MixerModelWrapper::CopyParams() const {
	std::vector<torch::Tensor> params;
	for (auto& param : mixerNet->parameters()) {
		params.push_back(param.flatten());
	}

	if (params.empty()) {
		return torch::Tensor();
	}

	return torch::cat(params);
}
