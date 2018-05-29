#pragma once

#include "data.hpp"
#include "lib_utils/log.hpp"
#include <memory>
#include <typeinfo>

template<typename T, size_t N>
constexpr size_t NELEMENTS(T const (&array)[N]) {
	(void)array;
	return N;
}

namespace Modules {

enum StreamType {
	UNKNOWN_ST = -1,
	AUDIO_RAW,    //uncompressed audio
	VIDEO_RAW,    //uncompressed video
	AUDIO_PKT,    //compressed audio
	VIDEO_PKT,    //compressed video
	SUBTITLE_PKT, //subtitles and captions
	PLAYLIST,     //playlist and adaptive streaming manifests
	SEGMENT,      //adaptive streaming init and media segments
	SIZE_OF_ENUM_STREAM_TYPE
};

struct IMetadata {
	virtual ~IMetadata() {}
	virtual StreamType getStreamType() const = 0;
	bool isVideo() const {
		switch (getStreamType()) {
		case VIDEO_RAW: case VIDEO_PKT: return true;
		default: return false;
		}
	}
	bool isAudio() const {
		switch (getStreamType()) {
		case AUDIO_RAW: case AUDIO_PKT: return true;
		default: return false;
		}
	}
	bool isSubtitle() const {
		switch (getStreamType()) {
		case SUBTITLE_PKT: return true;
		default: return false;
		}
	}
};

struct IMetadataCap {
	virtual ~IMetadataCap() noexcept(false) {}
	virtual std::shared_ptr<const IMetadata> getMetadata() const = 0;
	virtual void setMetadata(std::shared_ptr<const IMetadata> metadata) = 0;
	virtual bool updateMetadata(Data&) {
		return false;
	};
};


}
