#pragma once

#include "lib_utils/log_sink.hpp" // Warning, Debug
#include "lib_utils/format.hpp"
#include "lib_modules/utils/helper.hpp"
#include <thread>
#include <chrono>

namespace Modules {

class Regulator : public ModuleS {
	public:
		Regulator(KHost* host, std::shared_ptr<IClock> clock_)
			: clock(clock_), m_host(host) {
			m_output = addOutput<OutputDefault>();
		}

		void processOne(Data data) override {
			auto const dataTime = data->getMediaTime();
			auto const delayInMs = clockToTimescale(dataTime - fractionToClock(clock->now()), 1000);
			if (delayInMs > 0) {
				m_host->log(delayInMs < REGULATION_TOLERANCE_IN_MS ? Debug : Warning, format("received data for time %ss (will sleep for %s ms)", dataTime / (double)IClock::Rate, delayInMs).c_str());
				std::this_thread::sleep_for(std::chrono::milliseconds(delayInMs));
			} else if (delayInMs + REGULATION_TOLERANCE_IN_MS < 0) {
				m_host->log(dataTime > 0 ? Warning : Debug, format("received data for time %ss is late from %sms", dataTime / (double)IClock::Rate, -delayInMs).c_str());
			}
			m_output->post(data);
		}

		std::shared_ptr<IClock> const clock;

		static auto const REGULATION_TOLERANCE_IN_MS = 300;

	private:
		KHost* const m_host;
		KOutput* m_output;
};
}

