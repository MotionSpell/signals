#pragma once

#include "lib_modules/core/module.hpp"
#include <memory>
#include <string>

namespace Modules {
namespace Stream {

struct Quality {
	virtual ~Quality() {}

	std::shared_ptr<const MetadataFile> getMeta() const {
		return lastData ? safe_cast<const MetadataFile>(lastData->getMetadata()) : nullptr;
	};

	Data lastData;
	uint64_t avg_bitrate_in_bps = 0;
	std::string prefix; //typically a subdir, ending with a folder separator '/'
};

struct IAdaptiveStreamingCommon {
	virtual ~IAdaptiveStreamingCommon() noexcept(false) {}
	/*created each quality private data*/
	virtual std::unique_ptr<Quality> createQuality() const = 0;
	/*called each time segments are ready*/
	virtual void generateManifest() = 0;
	/*last manifest to be written: usually the VoD one*/
	virtual void finalizeManifest() = 0;
};

class AdaptiveStreamingCommon : public IAdaptiveStreamingCommon, public ModuleDynI {
public:
	enum Type {
		Static,
		Live,
		LiveNonBlocking,
	};
	enum AdaptiveStreamingCommonFlags {
		None = 0,
		SegmentsNotOwned     = 1 << 0, //don't touch files
		PresignalNextSegment = 1 << 1,
		ForceRealDurations   = 1 << 2
	};

	AdaptiveStreamingCommon(Type type, uint64_t segDurationInMs, const std::string &manifestDir, AdaptiveStreamingCommonFlags flags);
	virtual ~AdaptiveStreamingCommon() {}

	void process() override final;
	void flush() override final;

	static std::string getCommonPrefixAudio(size_t index) {
		return format("a_%s", index);
	}
	static std::string getCommonPrefixVideo(size_t index, unsigned width, unsigned height) {
		return format("v_%s_%sx%s", index, width, height);
	}
	static std::string getCommonPrefixSubtitle(size_t index) {
		return format("s_%s", index);
	}

protected:
	std::string getInitName(Quality const * const quality, size_t index) const;
	std::string getSegmentName(Quality const * const quality, size_t index, const std::string &segmentNumSymbol) const;
	uint64_t getCurSegNum() const;
	void endOfStream();

	const Type type;
	uint64_t startTimeInMs=-1, segDurationInMs, totalDurationInMs=0;
	const std::string manifestDir;
	const AdaptiveStreamingCommonFlags flags;
	std::vector<std::unique_ptr<Quality>> qualities;
	OutputDataDefault<DataRaw> *outputSegments, *outputManifest;

private:
	virtual void processInitSegment(Quality const * const quality, size_t index) = 0;
	void ensurePrefix(size_t index);
	std::string getPrefix(Quality const * const quality, size_t index) const;
	void threadProc();
	std::thread workingThread;
};

}
}
