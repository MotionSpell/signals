/*
Feeds downstream modules with comparable media times.

This module delays dispatch until all data corresponding up to a media time is
available. Data won't be modified (e.g. added or removed). Make sure that
backward modules have sufficiently sized allocators.

Delaying compressed data takes way less memory than raw data. Thus this module
is complementary to rectifiers operating on raw data (e.g. in a downward module).

The module works this way:
 - At each data reception, evaluate if some data is dispatchable. Non-dispatchable data is queued in a fifo.
   The first data is dispatched immediately to allow downward modules to initialize.
 - We rely on clock times to handle drifts and discontinuities: flush and reset on error.
 - Different media types are processed differently (video&audio = continuous, subtitles = sparse).
*/
#include "regulator_multi.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper_dyn.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_utils/clock.hpp" // timescaleToClock
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp" // enforce
#include <algorithm> // max, remove_if
#include <cassert>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace Modules {

struct RegulatorMulti : public ModuleDynI {
		RegulatorMulti(KHost* host, RegulatorMultiConfig &rmCfg)
			: m_host(host), clock(rmCfg.clock),
			  maxMediaTimeDelay(timescaleToClock(rmCfg.maxMediaTimeDelayInMs, 1000)),
			  maxClockTimeDelay(timescaleToClock(rmCfg.maxMediaTimeDelayInMs + rmCfg.maxClockTimeDelayInMs, 1000)) {
		}

		void flush() override {
			dispatch([](Rec const&) {
				return true;
			});
		}

		void process() override {
			mimicOutputs();

			int id;
			auto data = popAny(id);

			if (isDeclaration(data)) {
				outputs[id]->post(data);
				return;
			}

			auto const now = fractionToClock(clock->now());
			auto const mediaTime = data->get<DecodingTime>().time;

			if (!streams[id].init) {
				// Signal discontinuity
				auto discontinuityData = std::make_shared<DataRaw>(0);
				CueFlags flags;
				flags.discontinuity = true;
				discontinuityData->set(flags);
				outputs[id]->post(discontinuityData);

				// Initial case: dispatch immediately
				outputs[id]->post(data);
				streams[id].init = true;
			} else {
				streams[id].push_back({now, data});
			}

			if (data->getMetadata())
				if (data->getMetadata()->isAudio() || data->getMetadata()->isVideo()) {
					auto const newMediaDispatchTime = std::max<int64_t>(mediaDispatchTime, data->get<DecodingTime>().time - maxMediaTimeDelay);
					m_host->log(Debug, format("Media dispatch time goes to %s", mediaDispatchTime).c_str());
					mediaDispatchTime = newMediaDispatchTime;
				}

			if (std::abs(mediaDispatchTime - mediaTime) > maxMediaTimeDelay + tolerance) {
				m_host->log(Warning, "Media time error detected. Flush queued data and reset offset.");
				flush();
				reset();
				return;
			}

			// Normal case
			dispatch([&](Rec const& rec) {
				return rec.data->get<DecodingTime>().time < mediaDispatchTime;
			});

			// Too old according to clock time
			for (int i = 0; i < (int)streams.size(); ++i)
				for (auto& rec : streams[i])
					if (rec.creationTime < now - maxClockTimeDelay) {
						m_host->log(Warning, "Clock error detected. Discard queued data and reset offset.");
						for (auto &stream : streams) {
							m_host->log(Info, format("\tDelete %s data entries.", (int)stream.size()).c_str());
							stream.clear();
							stream.init = false;
						}
						reset();
						return;
					}
		}

		int getNumOutputs() const override {
			{
				auto pThis = const_cast<RegulatorMulti*>(this);
				pThis->mimicOutputs();
			}
			return ModuleDynI::getNumOutputs();
		}

		IOutput* getOutput(int i) override {
			mimicOutputs();
			return ModuleDynI::getOutput(i);
		}

	private:
		struct Rec {
			int64_t creationTime;
			Data data;
		};
		struct Stream : std::vector<Rec> {
			bool init = false;
		};

		Data popAny(int& inputIdx) {
			Data data;
			inputIdx = 0;
			while (!inputs[inputIdx]->tryPop(data)) {
				inputIdx++;
			}
			return data;
		}

		void mimicOutputs() {
			while(streams.size() < getInputs().size()) {
				addOutput();
				streams.push_back({});
			}
		}

		void dispatch(std::function<bool(Rec const&)> predicate) {
			for (int i = 0; i < (int)streams.size(); ++i) {
				for (auto& rec : streams[i])
					if (predicate(rec))
						outputs[i]->post(rec.data);

				//remove dispatched
				streams[i].erase(std::remove_if(streams[i].begin(), streams[i].end(), predicate), streams[i].end());
			}
		}

		void reset() {
			mediaDispatchTime = std::numeric_limits<int64_t>::min();
		}

		KHost * const m_host;
		std::shared_ptr<IClock const> clock;
		const int64_t maxMediaTimeDelay, maxClockTimeDelay;

		std::vector<Stream> streams;
		int64_t mediaDispatchTime = std::numeric_limits<int64_t>::min();
		static auto const tolerance = 6 * IClock::Rate;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (RegulatorMultiConfig*)va;
	enforce(host, "RegulatorMulti: host can't be NULL");
	enforce(config, "RegulatorMulti: config can't be NULL");
	return createModule<RegulatorMulti>(host, *config).release();
}

auto const registered = Factory::registerModule("RegulatorMulti", &createObject);
}
