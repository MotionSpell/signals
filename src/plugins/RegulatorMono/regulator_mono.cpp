#include "regulator_mono.hpp"
#include "lib_utils/log_sink.hpp" // Warning, Debug
#include "lib_utils/format.hpp"
#include "lib_utils/tools.hpp" // enforce
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_media/common/attributes.hpp"
#include <thread>
#include <chrono>

using namespace Modules;

namespace {

class RegulatorMono : public ModuleS {
	public:
		RegulatorMono(KHost* host, RegulatorMonoConfig &cfg)
			: m_host(host), clock(cfg.clock), resyncAllowed(cfg.resyncAllowed) {
			m_output = addOutput();
		}

		void processOne(Data data) override {
			auto const timeTarget = data->get<DecodingTime>().time;
			auto const timeNow = fractionToClock(clock->now());
			auto const delayInMs = clockToTimescale(timeTarget - timeNow, 1000) - m_offsetInMs;
			if (delayInMs > 0) {
				if (resyncAllowed && delayInMs > FWD_TOLERANCE_IN_MS) {
					m_host->log(Warning, format("forward discontinuity detected (%s ms)", delayInMs).c_str());
					m_offsetInMs += delayInMs;
					return processOne(data);
				}

				if (delayInMs > REGULATION_TOLERANCE_IN_MS)
					m_host->log(Debug, format("will sleep for %s ms", delayInMs).c_str());
				std::this_thread::sleep_for(std::chrono::milliseconds(delayInMs));
			} else if (delayInMs < -REGULATION_TOLERANCE_IN_MS) {
				if (resyncAllowed && delayInMs < -BWD_TOLERANCE_IN_MS) {
					m_host->log(Warning, format("backward discontinuity detected (%s ms)", -delayInMs).c_str());
					m_offsetInMs += delayInMs;
					return processOne(data);
				}

				if (-delayInMs > std::abs(m_lastDelayInMs)) {
					char msg[256];
					sprintf(msg, "late data (%.2fs)", -delayInMs/1000.0);
					m_host->log(Warning, msg);
				}
			}
			m_lastDelayInMs = delayInMs;
			m_output->post(data);
		}

	private:
		KHost* const m_host;
		KOutput* m_output;
		int64_t m_lastDelayInMs = 0, m_offsetInMs = 0;

		std::shared_ptr<IClock> const clock;

		static auto const REGULATION_TOLERANCE_IN_MS = 300;

		bool resyncAllowed;
		static auto const FWD_TOLERANCE_IN_MS = 20000;
		static auto const BWD_TOLERANCE_IN_MS = 6000;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (RegulatorMonoConfig*)va;
	enforce(host, "RegulatorMono: host can't be NULL");
	enforce(config, "RegulatorMono: config can't be NULL");
	return createModule<RegulatorMono>(host, *config).release();
}

auto const registered = Factory::registerModule("RegulatorMono", &createObject);
}

