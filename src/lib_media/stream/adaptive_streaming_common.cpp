#include "adaptive_streaming_common.hpp"
#include "lib_utils/time.hpp"

namespace Modules {
namespace Stream {

AdaptiveStreamingCommon::AdaptiveStreamingCommon(Type type, uint64_t segDurationInMs)
: type(type), segDurationInMs(segDurationInMs) {
	addInput(new Input<DataRaw>(this));
	outputSegments = addOutput<OutputDataDefault<DataAVPacket>>();
	outputManifest = addOutput<OutputDataDefault<DataAVPacket>>();
}

void AdaptiveStreamingCommon::endOfStream() {
	if (workingThread.joinable()) {
		for (size_t i = 0; i < inputs.size(); ++i) {
			inputs[i]->push(nullptr);
		}
		workingThread.join();
	}
}

//needed because of the use of system time for live - otherwise awake on data as for any multi-input module
//TODO: add clock to the scheduler, see #14
void AdaptiveStreamingCommon::threadProc() {
	log(Info, "start processing at UTC: %sms.", (uint64_t)DataBase::absUTCOffsetInMs);

	auto const numInputs = getNumInputs() - 1;
	qualities.resize(numInputs);
	for (size_t i = 0; i < numInputs; ++i) {
		qualities[i] = createQuality();
	}

	Data data;
	for (;;) {
		uint64_t curSegDurInMs = 0;
		size_t i;
		for (i = 0; i < numInputs; ++i) {
			if ((type == LiveNonBlocking) && (!qualities[i]->meta)) {
				if (inputs[i]->tryPop(data) && !data) {
					break;
				}
			} else {
				data = inputs[i]->pop();
				if (!data) {
					break;
				}
			}
			
			if (data) {
				qualities[i]->meta = safe_cast<const MetadataFile>(data->getMetadata());
				if (!qualities[i]->meta)
					throw error(format("Unknown data received on input %s", i));
				qualities[i]->avg_bitrate_in_bps = ((qualities[i]->meta->getSize() * 8) / (segDurationInMs / 1000.0) + qualities[i]->avg_bitrate_in_bps * numSeg) / (numSeg + 1);
				if (!i) curSegDurInMs = segDurationInMs ? segDurationInMs : clockToTimescale(qualities[i]->meta->getDuration(), 1000);
				if (!startTimeInMs) startTimeInMs = clockToTimescale(data->getMediaTime(), 1000);
			}
		}
		if (!data) {
			if (i != numInputs) {
				break;
			} else {
				assert((type == LiveNonBlocking) && (qualities.size() < numInputs));
				g_DefaultClock->sleep(Fraction(1, 1000));
				continue;
			}
		}

		numSeg++;
		if (!curSegDurInMs) curSegDurInMs = segDurationInMs;
		if (!startTimeInMs) startTimeInMs = DataBase::absUTCOffsetInMs - curSegDurInMs;
		generateManifest();
		totalDurationInMs += curSegDurInMs;
		log(Info, "Processes segment (total processed: %ss, UTC: %sms (deltaAST=%s, deltaInput=%s).",
			(double)totalDurationInMs / 1000, getUTC().num, gf_net_get_utc() - startTimeInMs,
			data ? (int64_t)(gf_net_get_utc() - clockToTimescale(data->getMediaTime(), 1000)) : 0);
		data = nullptr;

		if (type != Static) {
			const int64_t durInMs = startTimeInMs + totalDurationInMs - DataBase::absUTCOffsetInMs;
			if (durInMs > 0) {
				log(Debug, "Going to sleep for %s ms.", durInMs);
				clock->sleep(Fraction(durInMs, 1000));
			} else {
				log(Warning, "Late from %s ms.", -durInMs);
			}
		}
	}

	/*final rewrite of MPD in static mode*/
	finalizeManifest();
}

void AdaptiveStreamingCommon::process() {
	if (!workingThread.joinable() && (startTimeInMs==(uint64_t)-1)) {
		startTimeInMs = 0;
		numDataQueueNotify = (int)getNumInputs() - 1; //FIXME: connection/disconnection cannot occur dynamically. Lock inputs?
		workingThread = std::thread(&AdaptiveStreamingCommon::threadProc, this);
	}
}

void AdaptiveStreamingCommon::flush() {
	numDataQueueNotify--;
	if ((type != Static) && (numDataQueueNotify == 0)) {
		endOfStream();
	}
}

}
}
