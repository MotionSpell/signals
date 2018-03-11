#include "apple_hls.hpp"
#include <fstream>
#include <sstream>

#ifdef _WIN32 
#include <sys/timeb.h>
#include <Winsock2.h>
#else
#include <sys/time.h>
#endif

namespace Modules {
namespace Stream {

/*
HLS Master:
https://gpactest.akamaized.net/hls/live/595451/ulldemo2802/master.m3u8

HLS Child:
v_0_640x360/playlist.m3u8

Segments:
/cmaf/live/595451/ulldemo2802/v_0_640x360/v_0_640x360-*.m4s
*/
Apple_HLS::Apple_HLS(const std::string &m3u8Dir, const std::string &m3u8Filename, Type type, uint64_t segDurationInMs, bool genVariantPlaylist, AdaptiveStreamingCommonFlags flags)
: AdaptiveStreamingCommon(type, segDurationInMs, m3u8Dir, flags | (genVariantPlaylist ? SegmentsNotOwned : None)), playlistMasterPath(format("%s%s", m3u8Dir, m3u8Filename)), genVariantPlaylist(genVariantPlaylist) {
	if (segDurationInMs % 1000)
		throw error("Segment duration must be an integer number of seconds.");
}

Apple_HLS::~Apple_HLS() {
	endOfStream();
}

std::unique_ptr<Quality> Apple_HLS::createQuality() const {
	return uptr<Quality>(safe_cast<Quality>(new HLSQuality));
}

std::string Apple_HLS::getVariantPlaylistName(HLSQuality const * const quality, const std::string &subDir, size_t index) {
	auto const &meta = quality->getMeta();
	switch (meta->getStreamType()) {
	case AUDIO_PKT:               return format("%s%s_.m3u8", subDir, getCommonPrefixAudio(index));
	case VIDEO_PKT: case SEGMENT: return format("%s%s_.m3u8", subDir, getCommonPrefixVideo(index, meta->resolution[0], meta->resolution[1]));
	case SUBTITLE_PKT:            return format("%s%s", subDir, getCommonPrefixSubtitle(index));
	default: assert(0); return "";
	}
}

void Apple_HLS::generateManifestMaster() {
	if (!masterManifestIsWritten) {
		std::stringstream playlistMaster;
		playlistMaster << "#EXTM3U" << std::endl;
		playlistMaster << "#EXT-X-VERSION:3" << std::endl;
		for (size_t i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			playlistMaster << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << quality->avg_bitrate_in_bps;
			auto const &meta = quality->getMeta();
			switch (meta->getStreamType()) {
			case AUDIO_PKT: playlistMaster << ",CODECS=" << meta->getCodecName() << std::endl; break;
			case VIDEO_PKT: playlistMaster << ",RESOLUTION=" << meta->resolution[0] << "x" << meta->resolution[1] << std::endl; break;
			case SEGMENT: playlistMaster << ",RESOLUTION=" << meta->resolution[0] << "x" << meta->resolution[1] << std::endl; break;
			default: assert(0);
			}
			playlistMaster << getVariantPlaylistName(quality, "", i) << std::endl;
		}
		std::ofstream mpl(playlistMasterPath, std::ofstream::out | std::ofstream::trunc);
		mpl << playlistMaster.str();
		mpl.close();
		masterManifestIsWritten = true;

		if (type != Static) {
			auto out = outputManifest->getBuffer(0);
			auto metadata = std::make_shared<MetadataFile>(playlistMasterPath, PLAYLIST, "", "", timescaleToClock(segDurationInMs, 1000), 0, 1, false, true);
			out->setMetadata(metadata);
			out->setMediaTime(totalDurationInMs, 1000);
			outputManifest->emit(out);
		}
	}
}

void Apple_HLS::updateManifestVariants() {
	if (genVariantPlaylist) {
		for (size_t i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			auto const &meta = quality->getMeta();
			auto fn = meta->getFilename();
			if (fn.empty()) {
				fn = getSegmentName(quality, i, std::to_string(getCurSegNum()));
			}
			auto const sepPos = fn.find_last_of(".");
			auto const ext = fn.substr(sepPos + 1);
			if (!version) {
				if (ext == "m4s") {
					version = 7;
					hasInitSeg = true;
				} else {
					version = 3;
				}
				auto const firstSegNumPos = fn.substr(0, sepPos).find_last_of("-");
				auto const firstSegNumStr = fn.substr(firstSegNumPos+1, sepPos-(firstSegNumPos+1));
				std::istringstream buffer(firstSegNumStr);
				buffer >> firstSegNum;
			}

			auto out = shptr(new DataBaseRef(quality->lastData));
			out->setMetadata(std::make_shared<MetadataFile>(format("%s%s", manifestDir, fn), SEGMENT, meta->getMimeType(), meta->getCodecName(), meta->getDuration(), meta->getSize(), meta->getLatency(), meta->getStartsWithRAP(), true));
			out->setMediaTime(totalDurationInMs, 1000);
			outputSegments->emit(out);

			if (flags & PresignalNextSegment) {
				if (quality->segments.empty()) {
					quality->segments.push_back({ fn, startTimeInMs + totalDurationInMs });
				}
				if (quality->segments.back().path != fn)
					throw error(format("PresignalNextSegment but segment names are inconsistent (\"%s\" versus \"%s\")", quality->segments.back().path, fn));

				auto const segNumPos = fn.substr(0, sepPos).find_last_of("-");
				auto const segNumStr = fn.substr(segNumPos + 1, sepPos - (segNumPos + 1));
				uint64_t segNum; std::istringstream buffer(segNumStr); buffer >> segNum;
				auto fnNext = format("%s%s%s", fn.substr(0, segNumPos+1), segNum+1, fn.substr(sepPos));
				quality->segments.push_back({ fnNext, startTimeInMs + totalDurationInMs + segDurationInMs });
			} else {
				quality->segments.push_back({ fn, startTimeInMs + totalDurationInMs });
			}
		}

		generateManifestVariantFull(false);
	}
}

void Apple_HLS::generateManifestVariantFull(bool isLast) {
	if (genVariantPlaylist) {
		for (size_t i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			quality->playlistVariant.str(std::string());
			quality->playlistVariant << "#EXTM3U" << std::endl;
			quality->playlistVariant << "#EXT-X-VERSION:" << version << std::endl;
			quality->playlistVariant << "#EXT-X-TARGETDURATION:" << (segDurationInMs + 500) / 1000 << std::endl;
			quality->playlistVariant << "#EXT-X-MEDIA-SEQUENCE:" << firstSegNum << std::endl;
			if (version >= 6) quality->playlistVariant << "#EXT-X-INDEPENDENT-SEGMENTS" << std::endl;
			if (hasInitSeg) quality->playlistVariant << "#EXT-X-MAP:URI=\"" << getInitName(quality, i) << "\"" << std::endl;
			quality->playlistVariant << "#EXT-X-PLAYLIST-TYPE:EVENT" << std::endl;

			for (auto &seg : quality->segments) {
				quality->playlistVariant << "#EXTINF:" << segDurationInMs / 1000.0 << std::endl;
				if (type != Static) {
					char cmd[100];
					struct timeval tv;
					tv.tv_sec = (long)(seg.startTimeInMs/1000);
					assert(!(tv.tv_sec & 0xFFFFFFFF00000000));
					tv.tv_usec = 0;
					time_t sec = tv.tv_sec;
					auto *tm = gmtime(&sec);
					if (!tm) {
						log(Warning, "Segment \"%s\": could not convert UTC start time %sms. Skippping PROGRAM-DATE-TIME.", seg.startTimeInMs, seg.path);
					} else {
						snprintf(cmd, sizeof(cmd), "%d-%02d-%02dT%02d:%02d:%02d.%03d+00:00", 1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)(seg.startTimeInMs % 1000));
						quality->playlistVariant << "#EXT-X-PROGRAM-DATE-TIME:" << cmd << std::endl;
					}
				}

				if (seg.path.empty())
					error("HLS segment path is empty. Even when using memory mode, you must set a valid path in the metadata.");
				quality->playlistVariant << seg.path << std::endl;
			}

			if (isLast) {
				quality->playlistVariant << "#EXT-X-ENDLIST" << std::endl;
			}

			std::ofstream vpl;
			auto const playlistCurVariantPath = getVariantPlaylistName(quality, manifestDir, i);
			vpl.open(playlistCurVariantPath, std::ofstream::out | std::ofstream::trunc);
			vpl << quality->playlistVariant.str();
			vpl.close();

			auto out = outputManifest->getBuffer(0);
			auto metadata = std::make_shared<MetadataFile>(playlistCurVariantPath, PLAYLIST, "", "", timescaleToClock(segDurationInMs, 1000), 0, 1, false, true);
			out->setMetadata(metadata);
			out->setMediaTime(totalDurationInMs, 1000);
			outputManifest->emit(out);
		}
	}
}

void Apple_HLS::generateManifest() {
	generateManifestMaster();
	updateManifestVariants();
}

void Apple_HLS::finalizeManifest() {
	generateManifestVariantFull(true);
}

}
}
