#pragma once

#include <string>
#include <vector>
#include "lib_media/common/resolution.hpp"
#include "lib_utils/fraction.hpp"
#include "lib_modules/core/metadata.hpp"
#include "span.hpp"
#include "pcm.hpp" // AudioSampleFormat
#include "pixel_format.hpp" // PixelFormat

namespace Modules {

//TODO: Raw should be picture and Pcm and return the same fields as MetadataPkt
struct MetadataRawVideo : IMetadata {
	MetadataRawVideo() : IMetadata(VIDEO_RAW) {}
};

struct MetadataRawAudio : IMetadata {
	MetadataRawAudio() : IMetadata(AUDIO_RAW) {}
};

struct MetadataRawSubtitle : IMetadata {
	MetadataRawSubtitle() : IMetadata(SUBTITLE_RAW) {}
};

struct MetadataPkt : public IMetadata {
	MetadataPkt(StreamType type) : IMetadata(type) {
	}
	std::string codec; // do not replace this with an enum!
	std::vector<uint8_t> codecSpecificInfo;
	int64_t bitrate = -1; // -1 if not available
	Fraction timeScale = {1, 1};

	SpanC getExtradata() const {
		return SpanC { codecSpecificInfo.data(), codecSpecificInfo.size() };
	}
};

struct MetadataPktVideo : MetadataPkt {
	MetadataPktVideo() : MetadataPkt(VIDEO_PKT) {}
	PixelFormat pixelFormat;
	Fraction sampleAspectRatio;
	Resolution resolution;
	Fraction framerate;
};

struct MetadataPktAudio : MetadataPkt {
	MetadataPktAudio() : MetadataPkt(AUDIO_PKT) {}
	uint32_t numChannels;
	uint32_t sampleRate;
	uint8_t bitsPerSample;
	uint32_t frameSize;
	bool planar;
	AudioSampleFormat format;
	AudioLayout layout;
};

struct MetadataPktSubtitle : MetadataPkt {
	MetadataPktSubtitle() : MetadataPkt(SUBTITLE_PKT) {}
};

inline PcmFormat toPcmFormat(std::shared_ptr<const MetadataPktAudio> meta) {
	PcmFormat cfg;
	cfg.sampleRate = meta->sampleRate;
	cfg.numChannels = meta->numChannels;
	cfg.sampleFormat = meta->format;
	cfg.numPlanes = meta->planar ? cfg.numChannels : 1;
	cfg.layout = meta->layout;
	return cfg;
}

}
