#include "adaptive_streaming_common.hpp"
#include "lib_utils/time.hpp"

namespace Modules {
namespace Stream {

AdaptiveStreamingCommon::AdaptiveStreamingCommon(Type type, uint64_t segDurationInMs, AdaptiveStreamingCommonFlags flags)
: type(type), segDurationInMs(segDurationInMs), flags(flags) {
	if ((flags & ForceRealDurations) && !segDurationInMs)
		throw error("Inconsistent parameters: ForceRealDurations flag requires a non-null segment duration.");
	addInput(new Input<DataRaw>(this));
	outputSegments = addOutput<OutputDataDefault<DataAVPacket>>();
	outputManifest = addOutput<OutputDataDefault<DataAVPacket>>();
}

std::string AdaptiveStreamingCommon::getSegmentName(Quality const * const quality, size_t index, u64 segmentNum) const {
	switch (quality->meta->getStreamType()) {
	case AUDIO_PKT:    return format("a_%s-%s.m4s", index, segmentNum);
	case VIDEO_PKT:    return format("v_%s_%sx%s-%s.m4s", index, quality->meta->resolution[0], quality->meta->resolution[1], segmentNum);
	case SUBTITLE_PKT: return format("s_%s-%s.m4s", index, segmentNum);
	default: return "";
	}
}

void AdaptiveStreamingCommon::endOfStream() {
	if (workingThread.joinable()) {
		for (size_t i = 0; i < inputs.size(); ++i) {
			inputs[i]->push(nullptr);
		}
		workingThread.join();
	}
}

void AdaptiveStreamingCommon::threadProc() {
	log(Info, "start processing at UTC: %sms.", (uint64_t)DataBase::absUTCOffsetInMs);

	auto const numInputs = getNumInputs() - 1;
	qualities.resize(numInputs);
	for (size_t i = 0; i < numInputs; ++i) {
		qualities[i] = createQuality();
	}

	Data data;
	uint64_t curSegDurInMs = 0;
	for (;;) {
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
				if (qualities[i]->meta->getDuration() == 0) {
					--i; data = nullptr; continue; //this is an in-memory initialization segment
				}
				qualities[i]->avg_bitrate_in_bps = ((qualities[i]->meta->getSize() * 8) / (segDurationInMs / 1000.0) + qualities[i]->avg_bitrate_in_bps * numSeg) / (numSeg + 1);
				if (!i) {
					if (flags & ForceRealDurations) {
						curSegDurInMs += clockToTimescale(qualities[i]->meta->getDuration(), 1000);
					} else {
						curSegDurInMs = segDurationInMs ? segDurationInMs : clockToTimescale(qualities[i]->meta->getDuration(), 1000);
					}
				}
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
		if (curSegDurInMs < segDurationInMs)
			continue;

		numSeg++;
		if (!curSegDurInMs) curSegDurInMs = segDurationInMs;
		if (!startTimeInMs) startTimeInMs = DataBase::absUTCOffsetInMs - curSegDurInMs;
		generateManifest();
		totalDurationInMs += curSegDurInMs;
		log(Info, "Processes segment (total processed: %ss, UTC: %sms (deltaAST=%s, deltaInput=%s).",
			(double)totalDurationInMs / 1000, getUTC().num, gf_net_get_utc() - startTimeInMs,
			data ? (int64_t)(gf_net_get_utc() - clockToTimescale(data->getMediaTime(), 1000)) : 0);
		data = nullptr;
		curSegDurInMs = 0;

		if (type != Static) {
			const int64_t durInMs = startTimeInMs + totalDurationInMs - getUTC().num;
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
		workingThread = std::thread(&AdaptiveStreamingCommon::threadProc, this);
	}
}

void AdaptiveStreamingCommon::flush() {
	if (type != Static) {
		endOfStream();
	}
}

}
}
