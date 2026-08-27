#include "MetricSender.h"

#include "Timer.h"

namespace py = pybind11;
using namespace GGL;

GGL::MetricSender::MetricSender(std::string _projectName, std::string _groupName, std::string _runName, std::string runID) :
	projectName(_projectName), groupName(_groupName), runName(_runName) {

	RG_LOG("Initializing MetricSender...");

	try {
		pyMod = py::module::import("python_scripts.metric_receiver");
	} catch (std::exception& e) {
		RG_ERR_CLOSE("MetricSender: Failed to import metrics receiver, exception: " << e.what());
	}

	try {
		auto returedRunID = pyMod.attr("init")(PY_EXEC_PATH, projectName, groupName, runName, runID);
		curRunID = returedRunID.cast<std::string>();
		RG_LOG(" > " << (runID.empty() ? "Starting" : "Continuing") << " run with ID : \"" << curRunID << "\"...");

	} catch (std::exception& e) {
		RG_ERR_CLOSE("MetricSender: Failed to initialize in Python, exception: " << e.what());
	}

	RG_LOG(" > MetricSender initalized.");
}

void GGL::MetricSender::Send(const Report& report) {
	py::dict reportDict = {};

	for (auto& pair : report.data)
		reportDict[pair.first.c_str()] = pair.second;

	try {
		pyMod.attr("add_metrics")(reportDict);
	} catch (std::exception& e) {
		// Non-fatal (2026-08-04): a dropped wandb upload (e.g. a transient network
		// reset in wandb's own asyncio networking layer) used to kill the entire
		// multi-day training run via RG_ERR_CLOSE. Metrics logging is not essential
		// to training progress -- skip this iteration's upload and keep going,
		// mirroring the existing non-fatal pattern used for on_checkpoint_saved()
		// just above this call site in Learner.cpp.
		RG_LOG("WARNING: MetricSender: Failed to add metrics (non-fatal, skipping this iteration's upload): " << e.what());
	}
}

GGL::MetricSender::~MetricSender() {
	
}