#include "adaptive_streaming_common.hpp"
#include "lib_utils/time.hpp"
#include "lib_gpacpp/gpacpp.hpp"

namespace Modules {
namespace Stream {

AdaptiveStreamingCommon::AdaptiveStreamingCommon(Type type, uint64_t segDurationInMs, const std::string &manifestDir, AdaptiveStreamingCommonFlags flags)
: type(type), segDurationInMs(segDurationInMs), manifestDir(manifestDir), flags(flags) {
	if ((flags & ForceRealDurations) && !segDurationInMs)
		throw error("Inconsistent parameters: ForceRealDurations flag requires a non-null segment duration.");
	if (!manifestDir.empty() && (flags & SegmentsNotOwned))
		throw error(format("Inconsistent parameters: manifestDir (%s) should be empty when segments are not owned.", manifestDir));
	addInput(new Input<DataRaw>(this));
	outputSegments = addOutput<OutputDataDefault<DataRaw>>();
	outputManifest = addOutput<OutputDataDefault<DataRaw>>();
}

std::string AdaptiveStreamingCommon::getInitName(Quality const * const quality, size_t index) const {
	switch (quality->getMeta()->getStreamType()) {
	case AUDIO_PKT: case VIDEO_PKT: case SUBTITLE_PKT: return format("%s-init.mp4", getPrefix(quality, index));
	default: return "";
	}
}

std::string AdaptiveStreamingCommon::getPrefix(Quality const * const quality, size_t index) const {
	switch (quality->getMeta()->getStreamType()) {
	case AUDIO_PKT:    return format("%s%s", quality->prefix, getCommonPrefixAudio(index));
	case VIDEO_PKT:    return format("%s%s", quality->prefix, getCommonPrefixVideo(index, quality->getMeta()->resolution[0], quality->getMeta()->resolution[1]));
	case SUBTITLE_PKT: return format("%s%s", quality->prefix, getCommonPrefixSubtitle(index));
	default: return "";
	}
}

std::string AdaptiveStreamingCommon::getSegmentName(Quality const * const quality, size_t index, const std::string &segmentNumSymbol) const {
	switch (quality->getMeta()->getStreamType()) {
	case AUDIO_PKT: case VIDEO_PKT: case SUBTITLE_PKT: return format("%s-%s.m4s", getPrefix(quality, index), segmentNumSymbol);
	default: return "";
	}
}

uint64_t AdaptiveStreamingCommon::getCurSegNum() const {
	return (startTimeInMs + totalDurationInMs) / segDurationInMs;
}

void AdaptiveStreamingCommon::ensurePrefix(size_t i) {
	if (qualities[i]->prefix.empty()) {
		qualities[i]->prefix = format("%s/", getPrefix(qualities[i].get(), i));
		//if (!(flags & SegmentsNotOwned)) //FIXME: HLS manifests still requires the subdir presence
		{
			auto const dir = format("%s%s", manifestDir, qualities[i]->prefix);
			if ((gf_dir_exists(dir.c_str()) == GF_FALSE) && gf_mkdir(dir.c_str()))
				throw std::runtime_error(format("couldn't create subdir %s: please check you have sufficient rights (1)", qualities[i]->prefix));
		}
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
	for (size_t i = 0; i < numInputs; ++i) {
		qualities.push_back(createQuality());
	}

	Data data;
	std::vector<uint64_t> curSegDurIn180k; curSegDurIn180k.resize(numInputs);
	size_t i;

	auto ensureStartTime = [&]() {
		if (!startTimeInMs) startTimeInMs = clockToTimescale(data->getMediaTime(), 1000);
	};
	auto ensureCurDur = [&]() {
		for (i = 0; i < numInputs; ++i) {
			if (!curSegDurIn180k[0])
				curSegDurIn180k[0] = segDurationInMs;
		}
	};
	auto sendLocalData = [&](uint64_t size) {
		ensureStartTime();
		auto out = size ? shptr<DataBase>(new DataBaseRef(data)) : outputSegments->getBuffer(0);
		auto const &meta = qualities[i]->getMeta();
		out->setMetadata(std::make_shared<MetadataFile>(getSegmentName(qualities[i].get(), i, std::to_string(getCurSegNum())), SEGMENT, meta->getMimeType(), meta->getCodecName(), meta->getDuration(), size, meta->getLatency(), meta->getStartsWithRAP(), meta->getEOS()));
		out->setMediaTime(totalDurationInMs + timescaleToClock(curSegDurIn180k[i], 1000));
		outputSegments->emit(out);
	};
	auto segmentReady = [&]()->bool {
		ensureCurDur();
		for (i = 0; i < numInputs; ++i) {
			if (curSegDurIn180k[i] < timescaleToClock(segDurationInMs, 1000)) {
				return false;
			}
			if (!qualities[i]->getMeta()->getEOS()) {
				return false;
			}
		}
		for (auto &d : curSegDurIn180k) {
			d -= timescaleToClock(segDurationInMs, 1000);
		}
		return true;
	};
	for (;;) {
		for (i = 0; i < numInputs; ++i) {
			if (curSegDurIn180k[i] > *std::min_element(curSegDurIn180k.begin(), curSegDurIn180k.end())) {
				continue;
			}

			if ((type == LiveNonBlocking) && (!qualities[i]->getMeta())) {
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
				qualities[i]->lastData = data;
				auto const &meta = qualities[i]->getMeta();
				if (!meta)
					throw error(format("Unknown data received on input %s", i));
				ensurePrefix(i);

				auto const curDurIn180k = meta->getDuration();
				if (curDurIn180k == 0 && curSegDurIn180k[i] == 0) {
					processInitSegment(qualities[i].get(), i);
					if (flags & PresignalNextSegment) {
						sendLocalData(0);
					}
					--i; data = nullptr; continue;
				}

				if (segDurationInMs && curDurIn180k) {
					auto const numSeg = totalDurationInMs / segDurationInMs;
					qualities[i]->avg_bitrate_in_bps = ((meta->getSize() * 8 * Clock::Rate) / meta->getDuration() + qualities[i]->avg_bitrate_in_bps * numSeg) / (numSeg + 1);
				}
				if (flags & ForceRealDurations) {
					curSegDurIn180k[i] += meta->getDuration();
				} else {
					curSegDurIn180k[i] = segDurationInMs ? timescaleToClock(segDurationInMs, 1000) : meta->getDuration();
				}
				if (curSegDurIn180k[i] < timescaleToClock(segDurationInMs, 1000) || !meta->getEOS()) {
					sendLocalData(meta->getSize());
				}
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
		const int64_t curMediaTimeInMs = clockToTimescale(data->getMediaTime(), 1000);
		ensureStartTime();
		data = nullptr;

		if (segmentReady()) {
			generateManifest();
			totalDurationInMs += segDurationInMs;
			log(Info, "Processes segment (total processed: %ss, UTC: %sms (deltaAST=%s, deltaInput=%s).",
				(double)totalDurationInMs / 1000, getUTC().num, gf_net_get_utc() - startTimeInMs, (int64_t)(gf_net_get_utc() - curMediaTimeInMs));

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
