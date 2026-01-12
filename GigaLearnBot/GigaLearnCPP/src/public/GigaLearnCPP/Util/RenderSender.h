#pragma once
#include "Report.h"
#include <pybind11/pybind11.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/BasicTypes/Action.h>
#include <GigaLearnCPP/Util/Timer.h>

// Suppress C4251 warnings about types needing DLL interface
// These are safe to ignore when building everything together
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif

namespace GGL {
	struct RG_IMEXPORT RenderSender {
		pybind11::module pyMod;

		float timeScale;
		double adaptiveRenderDelay = -1;
		Timer renderTimer = {};

		RenderSender(float timeScale);

		RG_NO_COPY(RenderSender);

		void Send(const RLGC::GameState& state);

		~RenderSender();
	};
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif