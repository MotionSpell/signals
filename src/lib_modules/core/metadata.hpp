#pragma once

#include <typeinfo>

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
};

struct IMetadata {
	IMetadata(StreamType type_) : type(type_) {
	}
	virtual ~IMetadata() {}
	inline bool operator==(const IMetadata &right) const {
		return typeid(*this) == typeid(right) && type == right.type;
	}
	bool isVideo() const {
		switch (type) {
		case VIDEO_RAW: case VIDEO_PKT: return true;
		default: return false;
		}
	}
	bool isAudio() const {
		switch (type) {
		case AUDIO_RAW: case AUDIO_PKT: return true;
		default: return false;
		}
	}
	bool isSubtitle() const {
		switch (type) {
		case SUBTITLE_PKT: return true;
		default: return false;
		}
	}
	StreamType const type;
};
}


// FIXME: remove this
#include <memory>

namespace Modules {
class DataBase;
typedef std::shared_ptr<const DataBase> Data;

struct IMetadataCap {
	virtual ~IMetadataCap() {}
	virtual std::shared_ptr<const IMetadata> getMetadata() const = 0;
	virtual void setMetadata(std::shared_ptr<const IMetadata> metadata) = 0;
	virtual bool updateMetadata(Data&) {
		return false;
	};
};


}
