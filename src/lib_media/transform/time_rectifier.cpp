#include "time_rectifier.hpp"
#include "lib_utils/scheduler.hpp"
#include "../common/pcm.hpp"

namespace Modules {

#define TR_DEBUG Debug

static const int64_t ANALYZE_WINDOW_MAX = std::numeric_limits<int64_t>::max() / 2;

TimeRectifier::TimeRectifier(Fraction frameRate, uint64_t analyzeWindowIn180k)
	: frameRate(frameRate),
	  threshold(timescaleToClock(frameRate.den, frameRate.num)),
	  scheduler(new Scheduler(clock)) {
	if (clock->getSpeed() == 0.0) {
		this->analyzeWindowIn180k = ANALYZE_WINDOW_MAX;
	} else {
		this->analyzeWindowIn180k = (int64_t)(analyzeWindowIn180k * clock->getSpeed());
	}
}

void TimeRectifier::sanityChecks() {
	if (!hasVideo)
		throw error("requires to have one video stream connected");
}

void TimeRectifier::process() {
	std::unique_lock<std::mutex> lock(inputMutex);
	fillInputQueuesUnsafe();
	sanityChecks();
	removeOutdatedAllUnsafe((fractionToClock(clock->now()) - analyzeWindowIn180k));
}

void TimeRectifier::flush() {
	std::unique_lock<std::mutex> lock(inputMutex);
	flushing = true;
	auto const finalClockTime = std::max<int64_t>(maxClockTimeIn180k, fractionToClock(clock->now()));
	log(TR_DEBUG, "Schedule final removal at time %s (max:%s|%s)", finalClockTime, maxClockTimeIn180k, fractionToClock(clock->now()));
	scheduler->scheduleAt([this](Fraction f) {
		log(TR_DEBUG, "Final removal at time %s", fractionToClock(f));
		removeOutdatedAllUnsafe(fractionToClock(f)+1);
	}, Fraction(finalClockTime, IClock::Rate));
	flushedCond.wait(lock);
}

void TimeRectifier::mimicOutputs() {
	auto const numInputs = getNumInputs() - 1;
	while (outputs.size() < numInputs) {
		std::unique_lock<std::mutex> lock(inputMutex);
		addOutput<OutputDefault>();
		streams.push_back(Stream());
	}
}

void TimeRectifier::declareScheduler(std::unique_ptr<IInput> &input, std::unique_ptr<IOutput> &output) {
	auto const oMeta = output->getMetadata();
	if (!oMeta) {
		log(Debug, "Output isn't connected or doesn't expose a metadata: impossible to check.");
	} else if (input->getMetadata()->getStreamType() != oMeta->getStreamType())
		throw error("Metadata I/O inconsistency");

	if (input->getMetadata()->getStreamType() == VIDEO_RAW) {
		if (hasVideo)
			throw error("Only one video stream is allowed");
		hasVideo = true;
		scheduleEvery(scheduler.get(), std::bind(&TimeRectifier::awakeOnFPS, this, std::placeholders::_1), frameRate.inverse(), 0);
	}
}

void TimeRectifier::fillInputQueuesUnsafe() {
	Data data;
	for (size_t i = 0; i < getNumInputs() - 1; ++i) {
		auto &currInput = inputs[i];
		while (currInput->tryPop(data)) {
			maxClockTimeIn180k = std::max<int64_t>(maxClockTimeIn180k, data->getClockTime());
			streams[i].data.push_back(data);
			if (currInput->updateMetadata(data)) {
				declareScheduler(currInput, outputs[i]);
			}
		}
	}
}

void TimeRectifier::removeOutdatedAllUnsafe(int64_t removalClockTime) {
	for (size_t i = 0; i < getNumInputs() - 1; ++i) {
		removeOutdatedIndexUnsafe(i, removalClockTime);
	}
}

void TimeRectifier::removeOutdatedIndexUnsafe(size_t inputIdx, int64_t removalClockTime) {
	auto data = streams[inputIdx].data.begin();
	while (data != streams[inputIdx].data.end()) {
		if ((*data)->getClockTime() < removalClockTime) {
			if (streams[inputIdx].data.size() <= 1) {
				if (flushing) {
					log(TR_DEBUG, "Remove streams[%s] data time media=%s clock=%s (removalClockTime=%s)", inputIdx, (*data)->getMediaTime(), (*data)->getClockTime(), removalClockTime);
					data = streams[inputIdx].data.erase(data);
					flushedCond.notify_one();
				} else {
					break;
				}
			} else {
				log(TR_DEBUG, "Remove last streams[%s] data time media=%s clock=%s (removalClockTime=%s)", inputIdx, (*data)->getMediaTime(), (*data)->getClockTime(), removalClockTime);
				data = streams[inputIdx].data.erase(data);
			}
		} else {
			data++;
		}
	}
}

void TimeRectifier::awakeOnFPS(Fraction time) {
	std::unique_lock<std::mutex> lock(inputMutex);
	removeOutdatedAllUnsafe(fractionToClock(time) - analyzeWindowIn180k);

	Data refData;
	for (size_t i = 0; i < getNumInputs() - 1; ++i) {
		if (inputs[i]->getMetadata()->getStreamType() == VIDEO_RAW) {
			auto distClock = std::numeric_limits<int64_t>::max();
			int currDataIdx = -1, idx = -1;
			for (auto &currData : streams[i].data) {
				idx++;
				auto const currDistClock = currData->getClockTime() - fractionToClock(time);
				log(Debug, "Video: considering data (%s/%s) at time %s (currDist=%s, dist=%s, threshold=%s)", currData->getMediaTime(), currData->getClockTime(), fractionToClock(time), currDistClock, distClock, threshold);
				if (std::abs(currDistClock) < distClock) {
					/*timings are monotonic so check for a previous data with distance less than one frame*/
					if (currDistClock <= 0 || (currDistClock > 0 && distClock > threshold)) {
						distClock = std::abs(currDistClock);
						refData = currData;
						currDataIdx = idx;
					}
				}
			}
			if (!refData) {
				if ((streams[i].numTicks > 0) && !flushing)
					throw error(format("No reference data found but neither starting (%s) nor flushing (%s)", streams[i].numTicks, flushing));
				log(Warning, "No available reference data for clock time %s", fractionToClock(time));
				return;
			}
			if (streams[i].numTicks == 0) {
				log(Info, "First available reference clock time: %s", fractionToClock(time));
			}
			if ((streams[i].numTicks > 0) && (streams[i].data.size() >= 2) && (currDataIdx != 1)) {
				log(Debug, "[%s] Selected reference data is not contiguous to the last one (index=%s).", i, currDataIdx);
				//TODO: pass in error mode: flush all the data where the clock time removeOutdatedAllUnsafe(refData->getClockTime());
			}

			auto data = shptr(new DataBaseRef(refData));
			data->setMediaTime(fractionToClock(Fraction(streams[i].numTicks++ * frameRate.den, frameRate.num)));
			log(TR_DEBUG, "Video: send[%s:%s] t=%s (data=%s/%s) (ref %s/%s)", i, streams[i].data.size(), data->getMediaTime(), data->getMediaTime(), data->getClockTime(), refData->getMediaTime(), refData->getClockTime());
			outputs[i]->emit(data);
			removeOutdatedIndexUnsafe(i, data->getClockTime());
		}
	}

	//TODO: Notes:
	//DO WE NEED TO KNOW IF WE ARE ON ERROR STATE? => LOG IT
	//23MS OF DESYNC IS OK => KEEP TRACK OF CURRENT DESYNC
	//AUDIO: BE ABLE TO ASK FOR A LARGER BUFFER ALLOCATOR? => BACK TO THE APP + DYN ALLOCATOR SIZE?
	//VIDEO: HAVE ONLY A FEW DECODED FRAMES: THEY ARRIVE IN ADVANCE ANYWAY
	for (size_t i = 0; i < getNumInputs() - 1; ++i) {
		switch (inputs[i]->getMetadata()->getStreamType()) {
		case AUDIO_RAW: {
			Data selectedData;
			while (1) {
				int currDataIdx = -1, idx = -1;
				for (auto &currData : streams[i].data) {
					idx++;
					if (selectedData && !idx) { /*first data cannot be selected*/
						selectedData = nullptr;
						continue;
					}
					auto const currDistMedia = refData->getMediaTime() - currData->getMediaTime();
					log(Debug, "Other: considering data (%s/%s) at time %s (ref=%s/%s, currDist=%s)", currData->getMediaTime(), currData->getClockTime(), fractionToClock(time), refData->getMediaTime(), refData->getClockTime(), currDistMedia);
					if ((currDistMedia >= 0) && (currDistMedia < threshold)) {
						selectedData = currData;
						currDataIdx = idx;
						break;
					}
				}
				if (!selectedData) {
					break;
				}
				if ((streams[i].numTicks > 0) && (streams[i].data.size() >= 2) && (currDataIdx != 1)) {
					log(Warning, "[%s] Selected data is not contiguous to the last one (index=%s). Expect discontinuity in the signal.", i, currDataIdx);
				}

				auto const audioData = safe_cast<const DataPcm>(selectedData);
				auto data = shptr(new DataBaseRef(selectedData));
				data->setMediaTime(fractionToClock(Fraction(streams[i].numTicks++ * audioData->getPlaneSize(0) / audioData->getFormat().getBytesPerSample(), audioData->getFormat().sampleRate)));
				log(TR_DEBUG, "Other: send[%s:%s] t=%s (data=%s/%s) (ref %s/%s)", i, streams[i].data.size(), data->getMediaTime(), data->getMediaTime(), data->getClockTime(), refData->getMediaTime(), refData->getClockTime());
				outputs[i]->emit(data);
				removeOutdatedIndexUnsafe(i, data->getClockTime());
			}
			break;
		}
		case VIDEO_RAW: break;
		default: throw error("unhandled media type (awakeOnFPS)");
		}
	}
}

}
