#pragma once

#include "lib_modules/modules.hpp"
#include "lib_utils/scheduler.hpp"
#include <list>
#include <memory>
#include <vector>

namespace Modules {

/*
This module is responsible for feeding the next modules with a clean signal.

A clean signal has the following properties:
1) its timings are continuous (no gaps, overlaps, or discontinuities),
2) the different media are synchronized.

The module needs to be sample accurate. It operates on raw data. Raw data requires a lot of memory ; however:
1) we store a short duration (typically 500ms) and the framework works by default with pre-allocated pools,
2) RAM is cheap ;)

The module works this way:
 - At each tick it pulls some data (like a mux would).
 - We rely on Clock times. Media times are considered non-reliable and only used to achieve sync.
 - Different media types are processed differently (video = lead, audio = pulled, subtitles = sparse).

Remarks:
 - This module acts as a transframerater for video (by skipping or repeating frames).
 - This module deprecates the AudioConvert class when used as a reframer (i.e. no sample rate conversion).
 - This module deprecates heartbeat mechanisms for sparse streams.
 - This module feeds compositors or mux with some clean data.
*/
class TimeRectifier : public ModuleDynI {
public:
	TimeRectifier(Fraction frameRate, uint64_t analyzeWindowIn180k = Clock::Rate / 2, std::unique_ptr<IScheduler> scheduler = uptr(new Scheduler));

	void process() override;
	void flush() override;

	size_t getNumOutputs() const override {
		return outputs.size();
	}
	IOutput* getOutput(size_t i) override {
		mimicOutputs();
		return outputs[i].get();
	}

private:
	void sanityChecks();
	void mimicOutputs();
	void fillInputQueues();
	void removeOutdated();
	void declareScheduler(Data data, std::unique_ptr<IInput> &input, std::unique_ptr<IOutput> &output);
	void awakeOnFPS(Fraction time);

	struct Stream {
		std::list<Data> data;
		//Data defaultTypeData; //TODO: black screen for video, etc.
	};

	Fraction frameRate;
	uint64_t analyzeWindowIn180k = 0;
	std::vector<std::unique_ptr<Stream>> input;
	std::unique_ptr<IScheduler> scheduler;
	bool hasVideo = false;
};

template <>
struct ModuleDefault<TimeRectifier> : public ClockCap, public TimeRectifier {
	template <typename ...Args>
	ModuleDefault(size_t allocatorSize, const std::shared_ptr<IClock> clock, Args&&... args)
	: ClockCap(clock), TimeRectifier(std::forward<Args>(args)...) {
		this->allocatorSize = allocatorSize;
	}
};

}
