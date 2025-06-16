#include "adaptive_streaming_common.hpp"
#include "../common/attributes.hpp"
#include "lib_utils/time.hpp"
#include "lib_utils/os.hpp"
#include "lib_utils/system_clock.hpp"
#include <cstring> // memcpy
#include <cassert>
#include <thread>
#include <chrono>

#define MOVE_FILE_NUM_RETRY 3

namespace Modules {
namespace Stream {

void ensureDir(std::string path) {
	if(!dirExists(path))
		mkdir(path);
}

AdaptiveStreamingCommon::AdaptiveStreamingCommon(KHost* host, Type type, uint64_t segDurationInMs, const std::string &manifestDir, AdaptiveStreamingCommonFlags flags)
	: m_host(host),
	  type(type), segDurationInMs(segDurationInMs), manifestDir(manifestDir), flags(flags) {
	if ((flags & ForceRealDurations) && !segDurationInMs)
		throw error("Inconsistent parameters: ForceRealDurations flag requires a non-null segment duration.");
	if (!manifestDir.empty() && (flags & SegmentsNotOwned))
		throw error(format("Inconsistent parameters: manifestDir (%s) should be empty when segments are not owned.", manifestDir));
	addInput();
	outputSegments = addOutput();
	outputManifest = addOutput();
}

bool AdaptiveStreamingCommon::moveFile(const std::string &src, const std::string &dst) const {
	if (src.empty())
		return true;

	if(src == dst)
		return false; // nothing to do

	if (flags & SegmentsNotOwned)
		throw error(format("Segments not owned require similar filenames (%s != %s)", src, dst));

	auto subdir = dst.substr(0, dst.find_last_of("/") + 1);
	ensureDir(subdir);

	int retry = MOVE_FILE_NUM_RETRY + 1;
	while (--retry) {
		try {
			::moveFile(src, dst);
			break;
		} catch(...) {
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if (!retry) {
		return false;
	}
	return true;
}

void AdaptiveStreamingCommon::processInitSegment(Quality const * const quality, size_t index) {
	auto const &meta = quality->getMeta();
	switch (meta->type) {
	case AUDIO_PKT: case VIDEO_PKT: case SUBTITLE_PKT: {
		auto out = clone(quality->lastData);
		std::string initFn = safe_cast<const MetadataFile>(quality->lastData->getMetadata())->filename;
		if (initFn.empty()) {
			initFn = format("%s%s", manifestDir, getInitName(quality, index));
		} else if (!(flags & SegmentsNotOwned)) {
			auto const dst = format("%s%s", manifestDir, getInitName(quality, index));
			moveFile(initFn, dst);
			initFn = dst;
		}

		auto metaFn = make_shared<MetadataFile>(SEGMENT);
		metaFn->filename = initFn;
		metaFn->mimeType = meta->mimeType;
		metaFn->codecName = meta->codecName;
		metaFn->lang = meta->lang;
		metaFn->durationIn180k = meta->durationIn180k;
		metaFn->filesize = meta->filesize;
		metaFn->latencyIn180k = meta->latencyIn180k;
		metaFn->startsWithRAP = meta->startsWithRAP;

		out->setMetadata(metaFn);
		out->setMediaTime(totalDurationInMs, 1000);
		outputSegments->post(out);
		break;
	}
	default: break;
	}
}

std::string AdaptiveStreamingCommon::getInitName(Quality const * const quality, size_t index) const {
	switch (quality->getMeta()->type) {
	case AUDIO_PKT:
	case VIDEO_PKT:
	case SUBTITLE_PKT:
		return format("%s-init.mp4", getPrefix(quality, index));
	default: return "";
	}
}

std::string AdaptiveStreamingCommon::getPrefix(Quality const * const quality, size_t index) const {
	switch (quality->getMeta()->type) {
	case AUDIO_PKT:    return format("%s%s", quality->prefix, getCommonPrefixAudio(index));
	case VIDEO_PKT:    return format("%s%s", quality->prefix, getCommonPrefixVideo(index, quality->getMeta()->resolution));
	case SUBTITLE_PKT: return format("%s%s", quality->prefix, getCommonPrefixSubtitle(index));
	default: return "";
	}
}

std::string AdaptiveStreamingCommon::getSegmentName(Quality const * const quality, size_t index, const std::string &segmentNumSymbol) const {
	switch (quality->getMeta()->type) {
	case AUDIO_PKT:
	case VIDEO_PKT:
	case SUBTITLE_PKT:
		return format("%s-%s.m4s", getPrefix(quality, index), segmentNumSymbol);
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
			ensureDir(dir);
		}
	}
}

void AdaptiveStreamingCommon::endOfStream() {
	if (workingThread.joinable()) {
		for(auto& in : inputs)
			in->push(nullptr);
		workingThread.join();
	}
}

std::shared_ptr<DataBase> AdaptiveStreamingCommon::getPresignalledData(uint64_t size, Data &data, bool EOS) {
	if (!(flags & PresignalNextSegment)) {
		return clone(data);
	}
	if (!safe_cast<const MetadataFile>(data->getMetadata())->filename.empty() && !EOS) {
		return nullptr;
	}

	static constexpr uint8_t mp4StaticHeader[] = {
		0x00, 0x00, 0x00, 0x18, 0x73, 0x74, 0x79, 0x70,
		0x6d, 0x73, 0x64, 0x68, 0x00, 0x00, 0x00, 0x00,
		0x6d, 0x73, 0x64, 0x68, 0x6d, 0x73, 0x69, 0x78,
	};
	auto constexpr headerSize = sizeof(mp4StaticHeader);
	if (size == 0 && !EOS) {
		auto out = outputSegments->allocData<DataRaw>(0);
		out->buffer->resize(headerSize);
		memcpy(out->buffer->data().ptr, mp4StaticHeader, headerSize);
		return out;
	} else if (data->data().len >= headerSize && !memcmp(data->data().ptr, mp4StaticHeader, headerSize)) {
		auto out = outputSegments->allocData<DataRaw>(0);
		auto const size = (size_t)(data->data().len - headerSize);
		out->buffer->resize(size);
		memcpy(out->buffer->data().ptr, data->data().ptr + headerSize, size);
		return out;
	} else {
		assert(data->data().len < 8 || *(uint32_t*)(data->data().ptr + 4) != (uint32_t)0x70797473);
		return clone(data);
	}
}

void AdaptiveStreamingCommon::threadProc() {

	auto const numInputs = getNumInputs() - 1;
	for (int i = 0; i < numInputs; ++i) {
		qualities.push_back(createQuality());
	}

	Data data;
	std::vector<uint64_t> curSegDurIn180k(numInputs);
	int i;

	auto isComplete = [&]()->bool {
		uint64_t minIncompletSegDur = std::numeric_limits<uint64_t>::max();
		for (size_t idx = 0; idx < curSegDurIn180k.size(); ++idx) {
			auto const &segDur = curSegDurIn180k[idx];
			if ( (segDur < minIncompletSegDur) &&
			    ((segDur < timescaleToClock(segDurationInMs, 1000)) || (!qualities[idx]->getMeta() || !qualities[idx]->getMeta()->EOS))) {
				minIncompletSegDur = segDur;
			}
		}
		return (minIncompletSegDur == std::numeric_limits<uint64_t>::max()) || (curSegDurIn180k[i] > minIncompletSegDur);
	};
	auto ensureStartTime = [&]() {
		if (startTimeInMs == (uint64_t)-2) startTimeInMs = clockToTimescale(data->get<PresentationTime>().time, 1000);
	};
	auto ensureCurDur = [&]() {
		for (i = 0; i < numInputs; ++i) {
			if (!curSegDurIn180k[0])
				curSegDurIn180k[0] = segDurationInMs;
		}
	};
	auto sendLocalData = [&](uint64_t size, bool EOS) {
		ensureStartTime();
		auto out = getPresignalledData(size, data, EOS);
		if (out) {
			auto const &meta = qualities[i]->getMeta();

			auto metaFn = make_shared<MetadataFile>(SEGMENT);
			metaFn->filename = getSegmentName(qualities[i].get(), i, std::to_string(getCurSegNum()));
			metaFn->mimeType = meta->mimeType;
			metaFn->codecName = meta->codecName;
			metaFn->lang = meta->lang;
			metaFn->durationIn180k = meta->durationIn180k;
			metaFn->filesize = size;
			metaFn->latencyIn180k = meta->latencyIn180k;
			metaFn->startsWithRAP = meta->startsWithRAP;
			metaFn->EOS = EOS;

			out->setMetadata(metaFn);
			out->setMediaTime(totalDurationInMs + timescaleToClock(curSegDurIn180k[i], 1000));
			outputSegments->post(out);
		}
	};
	auto segmentReady = [&]()->bool {
		ensureCurDur();
		for (i = 0; i < numInputs; ++i) {
			if (curSegDurIn180k[i] < timescaleToClock(segDurationInMs, 1000)) {
				return false;
			}
			if (!qualities[i]->getMeta()->EOS) {
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
			if (isComplete()) {
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

				auto const curDurIn180k = meta->durationIn180k;
				if (curDurIn180k == 0 && curSegDurIn180k[i] == 0) {
					processInitSegment(qualities[i].get(), i);
					if (flags & PresignalNextSegment) {
						sendLocalData(0, false);
					}
					--i; data = nullptr; continue;
				}

				if (segDurationInMs && curDurIn180k) {
					auto const numSeg = totalDurationInMs / segDurationInMs;
					qualities[i]->avg_bitrate_in_bps = ((meta->filesize * 8 * IClock::Rate) / meta->durationIn180k + qualities[i]->avg_bitrate_in_bps * numSeg) / (numSeg + 1);
				}
				if (flags & ForceRealDurations) {
					curSegDurIn180k[i] += meta->durationIn180k;
				} else {
					curSegDurIn180k[i] = segDurationInMs ? timescaleToClock(segDurationInMs, 1000) : meta->durationIn180k;
				}
				if (curSegDurIn180k[i] < timescaleToClock(segDurationInMs, 1000) || !meta->EOS) {
					sendLocalData(meta->filesize, meta->EOS);
				}
			}
		}
		if (!data) {
			if (i != numInputs) {
				break;
			} else {
				assert((type == LiveNonBlocking) && ((int)qualities.size() < numInputs));
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				continue;
			}
		}
		const int64_t curMediaTimeInMs = clockToTimescale(data->get<PresentationTime>().time, 1000);
		ensureStartTime();
		data = nullptr;

		if (segmentReady()) {
			generateManifest();
			totalDurationInMs += segDurationInMs;
			auto utcInMs = int64_t(getUTC() * 1000);
			m_host->log(Info, format("Processes segment (total processed: %ss, UTC: %sms (deltaAST=%s, deltaInput=%s).",
			        (double)totalDurationInMs / 1000, utcInMs, utcInMs - startTimeInMs, (int64_t)(utcInMs - curMediaTimeInMs)).c_str());

			if (type != Static) {
				const int64_t durInMs = startTimeInMs + totalDurationInMs - utcInMs;
				if (durInMs > 0) {
					m_host->log(Debug, format("Going to sleep for %s ms.", durInMs).c_str());
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
				} else {
					m_host->log(Warning, format("Late from %s ms.", -durInMs).c_str());
				}
			}
		}
	}

	/*final rewrite of MPD in static mode*/
	finalizeManifest();
}

void AdaptiveStreamingCommon::process() {
	if (!workingThread.joinable() && (startTimeInMs==(uint64_t)-1)) {
		startTimeInMs = (uint64_t)-2;
		workingThread = std::thread(&AdaptiveStreamingCommon::threadProc, this);
	}
}

void AdaptiveStreamingCommon::flush() {
	endOfStream();
}

}
}
