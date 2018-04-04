#pragma once

#include "adaptive_streaming_common.hpp"

#define LIBAVMUXHLS //FIXME: see https://git.gpac-licensing.com/rbouqueau/fk-encode/issues/18
#include "../mux/libav_mux.hpp"
#include "../common/libav.hpp"
#include <lib_modules/utils/helper.hpp>
#include <vector>

namespace Modules {
namespace Stream {

#ifdef LIBAVMUXHLS
class LibavMuxHLSTS : public ModuleDynI {
public:
	LibavMuxHLSTS(bool isLowLatency, uint64_t segDurationInMs, const std::string &baseDir, const std::string &baseName, const std::string &options = "")
	: segDuration(timescaleToClock(segDurationInMs, 1000)), hlsDir(baseDir), segBasename(baseName) {
		delegate = create<Mux::LibavMux>(format("%s%s", hlsDir, baseName), "hls", options);
		addInput(new Input<DataAVPacket>(this));
		outputSegment  = addOutput<OutputDataDefault<DataRaw>>();
		outputManifest = addOutput<OutputDataDefault<DataRaw>>();
	}

	virtual ~LibavMuxHLSTS() {}

	void process() override {
		ensureDelegateInputs();

		size_t inputIdx = 0;
		Data data;
		while (!inputs[inputIdx]->tryPop(data)) {
			inputIdx++;
		}
		delegate->getInput(inputIdx)->push(data);
		delegate->process();

		if (data->getMetadata()->getStreamType() == VIDEO_PKT) {
			const int64_t DTS = data->getMediaTime();
			if (firstDTS == -1) {
				firstDTS = DTS;
			}
			if (DTS >= (segIdx + 1) * segDuration + firstDTS) {
				auto const fn = format("%s%s.ts", segBasename, segIdx);
				auto file = fopen(format("%s%s", hlsDir, fn).c_str(), "rt");
				if (!file)
					throw error(format("Can't open segment in read mode: %s", fn));
				fseek(file, 0, SEEK_END);
				auto const fsize = ftell(file);

				auto out = outputSegment->getBuffer(0);
				out->setMediaTime(timescaleToClock((uint64_t)DataBase::absUTCOffsetInMs, 1000) + data->getMediaTime());
				auto metadata = std::make_shared<MetadataFile>(hlsDir + fn, SEGMENT, "", "", segDuration, fsize, 1, false, true);
				switch (data->getMetadata()->getStreamType()) {
				case AUDIO_PKT: metadata->sampleRate = safe_cast<const MetadataPktLibavAudio>(data->getMetadata())->getSampleRate(); break;
				case VIDEO_PKT: {
					auto const res = safe_cast<const MetadataPktLibavVideo>(data->getMetadata())->getResolution();
					metadata->resolution[0] = res.width;
					metadata->resolution[1] = res.height;
					break;
				}
				default: assert(0);
				}
				out->setMetadata(metadata);
				outputSegment->emit(out);

				out = outputManifest->getBuffer(0);
				metadata = std::make_shared<MetadataFile>(format("%s%s.m3u8", hlsDir, segBasename), PLAYLIST, "", "", 0, 0, 1, false, true);
				out->setMetadata(metadata);
				outputManifest->emit(out);
				segIdx++;
			}
		}
	}

	IInput* getInput(size_t i) override {
		delegate->getInput(i);
		return ModuleDynI::getInput(i);
	}

private:
	void ensureDelegateInputs() {
		auto const inputs = getNumInputs();
		auto const delegateInputs = delegate->getNumInputs();
		for (auto i = delegateInputs; i < inputs; ++i) {
			delegate->getInput(i);
		}
	}
	std::unique_ptr<Modules::Mux::LibavMux> delegate;
	OutputDataDefault<DataRaw> *outputSegment, *outputManifest;
	int64_t firstDTS = -1, segDuration, segIdx = 0;
	std::string hlsDir, segBasename;
};
#endif /*LIBAVMUXHLS*/

class Apple_HLS : public AdaptiveStreamingCommon {
public:
	Apple_HLS(const std::string &m3u8Dir, const std::string &m3u8Filename, Type type, uint64_t segDurationInMs, uint64_t timeShiftBufferDepthInMs = 0, bool genVariantPlaylist = false, AdaptiveStreamingCommonFlags flags = None);
	virtual ~Apple_HLS();

private:
	std::unique_ptr<Quality> createQuality() const override;
	void generateManifest() override;
	void finalizeManifest() override;

	struct HLSQuality : public Quality {
		struct Segment {
			std::string path;
			uint64_t startTimeInMs;
		};
		HLSQuality() {}
		std::stringstream playlistVariant;
		std::vector<Segment> segments;
	};
	std::string getVariantPlaylistName(HLSQuality const * const quality, const std::string &subDir, size_t index);
	void updateManifestVariants();
	void generateManifestVariantFull(bool isLast);

	std::string getManifestMasterInternal();
	void generateManifestMaster();
	std::string playlistMasterPath;
	const bool genVariantPlaylist;

	unsigned version = 0;
	bool masterManifestIsWritten = false, isCMAF = false;
	std::vector<uint64_t> firstSegNums;
	uint64_t timeShiftBufferDepthInMs = 0;
};

}
}
