#pragma once
#include "Report.h"
#include <pybind11/pybind11.h>

// Suppress C4251 warnings about STL types needing DLL interface
// These are safe to ignore when building everything together
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace GGL {
	struct RG_IMEXPORT MetricSender {
		std::string curRunID;
		std::string projectName, groupName, runName;
		pybind11::module pyMod;

		MetricSender(std::string projectName = {}, std::string groupName = {}, std::string runName = {}, std::string runID = {});
		
		RG_NO_COPY(MetricSender);

		void Send(const Report& report);

		~MetricSender();
	};
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif