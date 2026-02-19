#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <cmath>
#include <ctime>
#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>
#include "Reward.h"

struct PerRewardTracker {
	std::string rewardName;
	float weight = 0.0f;

	// Welford's online statistics
	int64_t sampleCount = 0;
	int64_t activeSamples = 0;    // |reward| > ACTIVATION_EPSILON
	double welfordMean = 0.0;
	double welfordM2 = 0.0;       // Sum of squared differences from mean

	static constexpr double ACTIVATION_EPSILON = 0.001;

	void RecordSample(float value) {
		sampleCount++;

		if (std::abs(value) > ACTIVATION_EPSILON)
			activeSamples++;

		// Welford's online algorithm
		double delta = (double)value - welfordMean;
		welfordMean += delta / sampleCount;
		double delta2 = (double)value - welfordMean;
		welfordM2 += delta * delta2;
	}

	float GetActivationRate() const {
		if (sampleCount == 0) return 0.0f;
		return (float)activeSamples / (float)sampleCount;
	}

	float GetMean() const {
		if (sampleCount == 0) return 0.0f;
		return (float)welfordMean;
	}

	float GetVariance() const {
		if (sampleCount < 2) return 0.0f;
		return (float)(welfordM2 / (sampleCount - 1));
	}

	nlohmann::json ToJSON() const {
		nlohmann::json j;
		j["weight"] = weight;
		j["activation_rate"] = GetActivationRate();
		j["mean_value"] = GetMean();
		j["variance"] = GetVariance();
		j["sample_count"] = sampleCount;
		return j;
	}

	void Reset() {
		sampleCount = 0;
		activeSamples = 0;
		welfordMean = 0.0;
		welfordM2 = 0.0;
	}
};

class RewardMetricsCollector {
public:
	std::unordered_map<std::string, PerRewardTracker> trackers;

	// Skill rating snapshot for delta tracking
	std::map<std::string, float> snapshotRatings;
	std::map<std::string, float> ratingDeltas;

	bool initialized = false;

	void Initialize(const std::vector<RLGC::WeightedReward>& rewards) {
		trackers.clear();
		for (const auto& wr : rewards) {
			std::string name = wr.reward->GetName();
			PerRewardTracker tracker;
			tracker.rewardName = name;
			tracker.weight = wr.weight;
			trackers[name] = tracker;
		}
		initialized = true;
	}

	// Feed from the existing avgRewards map computed in Learner.cpp
	// The map contains reward name -> AvgTracker (which has total/count)
	// We extract the average value per reward and record it
	template<typename AvgTrackerMap>
	void RecordRewards(const AvgTrackerMap& avgRewards) {
		if (!initialized) return;

		for (const auto& pair : avgRewards) {
			auto it = trackers.find(pair.first);
			if (it != trackers.end()) {
				it->second.RecordSample(pair.second.Get());
			}
		}
	}

	// Snapshot current skill ratings for delta tracking
	void SnapshotSkillRatings(const std::map<std::string, float>& currentRatings) {
		snapshotRatings = currentRatings;
		ratingDeltas.clear();
	}

	// Compute deltas from snapshot and write metrics to JSON
	void WriteMetricsJSON(
		const std::filesystem::path& path,
		const std::map<std::string, float>& currentRatings,
		uint64_t totalTimesteps,
		const std::string& phaseName
	) {
		if (!initialized) return;

		// Compute rating deltas
		ratingDeltas.clear();
		for (const auto& pair : currentRatings) {
			auto snapIt = snapshotRatings.find(pair.first);
			if (snapIt != snapshotRatings.end()) {
				ratingDeltas[pair.first] = pair.second - snapIt->second;
			} else {
				ratingDeltas[pair.first] = 0.0f;
			}
		}

		nlohmann::json j;
		j["timestamp_utc"] = std::time(nullptr);
		j["total_timesteps"] = totalTimesteps;
		j["phase"] = phaseName;
		j["reward_count"] = trackers.size();

		// Skill ratings
		nlohmann::json ratingsJson;
		for (const auto& pair : currentRatings)
			ratingsJson[pair.first] = pair.second;
		j["skill_ratings"] = ratingsJson;

		nlohmann::json deltasJson;
		for (const auto& pair : ratingDeltas)
			deltasJson[pair.first] = pair.second;
		j["skill_rating_deltas"] = deltasJson;

		// Per-reward metrics
		nlohmann::json rewardsJson;
		for (const auto& pair : trackers)
			rewardsJson[pair.first] = pair.second.ToJSON();
		j["rewards"] = rewardsJson;

		std::ofstream fOut(path);
		if (fOut.is_open()) {
			fOut << j.dump(4);
		}
	}

	void Reset() {
		for (auto& pair : trackers)
			pair.second.Reset();
		snapshotRatings.clear();
		ratingDeltas.clear();
	}
};
