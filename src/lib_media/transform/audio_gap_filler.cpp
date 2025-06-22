#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "../common/attributes.hpp"
#include "../common/metadata.hpp"
#include "../common/pcm.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp" // enforce
#include <limits> // numeric_limits

using namespace std;
using namespace Modules;

namespace {

struct AudioGapFiller : public ModuleS {
	public:
		AudioGapFiller(KHost* host, int64_t toleranceInFrames)
			: m_host(host), toleranceInFrames(toleranceInFrames) {
			input->setMetadata(make_shared<MetadataRawAudio>());
			output = addOutput();
		}

		void processOne(Data data) override {
			auto audioData = safe_cast<const DataPcm>(data);
			auto const sampleRate = audioData->format.sampleRate;
			auto const timeInSR = clockToTimescale(data->get<PresentationTime>().time, sampleRate);
			if (accumulatedTimeInSR == std::numeric_limits<uint64_t>::max()) {
				accumulatedTimeInSR = timeInSR;
			}

			auto const srcNumSamples = audioData->getSampleCount();
			auto const diff = (int64_t)(timeInSR - accumulatedTimeInSR);
			if (std::abs(diff) >= srcNumSamples) {
				if (toleranceInFrames == -1 || (toleranceInFrames > 0 && std::abs(diff) <= srcNumSamples * (1 + (int64_t)toleranceInFrames))) {
					if (diff > 0) {
						m_host->log(abs(diff) > srcNumSamples ? Warning : Debug, format("Fixing gap of %s samples (input=%s, accumulation=%s)", diff, timeInSR, accumulatedTimeInSR).c_str());
						auto dataInThePast = data->clone();
						dataInThePast->set(PresentationTime { data->get<PresentationTime>().time - timescaleToClock(srcNumSamples, sampleRate) });
						processOne(dataInThePast);
					} else {
						return; /*small overlap: thrash current sample*/
					}
				} else {
					m_host->log(Warning, format("Discontinuity detected. Reset at time %s (previous: %s).", data->get<PresentationTime>().time, timescaleToClock(accumulatedTimeInSR, sampleRate)).c_str());
					accumulatedTimeInSR = timeInSR;
				}
			}

			auto dataOut = data->clone();
			dataOut->set(PresentationTime { timescaleToClock((int64_t)accumulatedTimeInSR, sampleRate) });
			output->post(dataOut);

			accumulatedTimeInSR += srcNumSamples;
		}

	private:
		KHost* const m_host;
		int64_t toleranceInFrames;
		uint64_t accumulatedTimeInSR = std::numeric_limits<uint64_t>::max();
		KOutput *output;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (int64_t*)va;
	enforce(host, "AudioGapFiller: host can't be NULL");
	enforce(config, "AudioGapFiller: config can't be NULL");
	return createModule<AudioGapFiller>(host, *config).release();
}

auto const registered = Factory::registerModule("AudioGapFiller", &createObject);
}

