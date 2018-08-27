#include "apple_hls.hpp"
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <sys/timeb.h>
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

namespace Modules {
namespace Stream {

Apple_HLS::Apple_HLS(IModuleHost* host, const std::string &m3u8Dir, const std::string &m3u8Filename, Type type, uint64_t segDurationInMs, uint64_t timeShiftBufferDepthInMs, bool genVariantPlaylist, AdaptiveStreamingCommonFlags flags)
	: AdaptiveStreamingCommon(type, segDurationInMs, m3u8Dir, flags | (genVariantPlaylist ? SegmentsNotOwned : None)),
	  m_host(host),
	  playlistMasterPath(format("%s%s", m3u8Dir, m3u8Filename)),
	  genVariantPlaylist(genVariantPlaylist), timeShiftBufferDepthInMs(timeShiftBufferDepthInMs) {
	if (segDurationInMs % 1000)
		throw error("Segment duration must be an integer number of seconds.");
}

Apple_HLS::~Apple_HLS() {
	endOfStream();
}

std::unique_ptr<Quality> Apple_HLS::createQuality() const {
	return make_unique<HLSQuality>();
}

std::string Apple_HLS::getVariantPlaylistName(HLSQuality const * const quality, const std::string &subDir, size_t index) {
	auto const &meta = quality->getMeta();
	switch (meta->type) {
	case AUDIO_PKT:               return format("%s%s_.m3u8", subDir, getCommonPrefixAudio(index));
	case VIDEO_PKT: case SEGMENT: return format("%s%s_.m3u8", subDir, getCommonPrefixVideo(index, meta->resolution));
	case SUBTITLE_PKT:            return format("%s%s", subDir, getCommonPrefixSubtitle(index));
	default: assert(0); return "";
	}
}

std::string Apple_HLS::getManifestMasterInternal() {
	std::stringstream playlistMaster;
	playlistMaster << "#EXTM3U" << std::endl;
	playlistMaster << "#EXT-X-VERSION:" << version << std::endl;
	if (isCMAF) playlistMaster << "#EXT-X-INDEPENDENT-SEGMENTS" << std::endl << std::endl;

	if (isCMAF) {
		auto const audioGroupName = "audio";
		struct AudioSpec {
			std::string codecName;
			uint64_t bandwidth;
		};
		std::vector<AudioSpec> audioSpecs;
		for (int i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			auto const &meta = quality->getMeta();
			if (meta->type == AUDIO_PKT) {
				audioSpecs.push_back({ meta->codecName, quality->avg_bitrate_in_bps });
				playlistMaster << "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"" << audioGroupName << "\",NAME=\"Main\",LANGUAGE=\"en\",AUTOSELECT=YES,URI=\"" << getVariantPlaylistName(quality, "", i) << "\"" << std::endl;
			}
		}
		if (!audioSpecs.empty()) {
			playlistMaster << std::endl;
		}
		if (audioSpecs.size() > 1)
			throw error("Several audio detected in CMAF mode. Not supported.");

		for (int i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			uint64_t bandwidth = quality->avg_bitrate_in_bps;
			if (!audioSpecs.empty()) {
				bandwidth += audioSpecs[0].bandwidth;
			}
			auto const &meta = quality->getMeta();
			switch (meta->type) {
			case AUDIO_PKT: break;
			case VIDEO_PKT:
				playlistMaster << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << bandwidth<< ",CODECS=\"" << meta->codecName;
				if (!audioSpecs.empty()) {
					playlistMaster << "," << audioSpecs[0].codecName;
					playlistMaster << "\",AUDIO=\"" << audioGroupName;
				}
				playlistMaster << "\"" << ",RESOLUTION=" << meta->resolution.width << "x" << meta->resolution.height << std::endl;
				playlistMaster << getVariantPlaylistName(quality, "", i) << std::endl;
				break;
			default: assert(0);
			}
		}
	} else {
		for (int i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			playlistMaster << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << quality->avg_bitrate_in_bps;
			auto const &meta = quality->getMeta();
			switch (meta->type) {
			case SEGMENT: playlistMaster << ",RESOLUTION=" << meta->resolution.width << "x" << meta->resolution.height << std::endl; break;
			default: assert(0);
			}
			playlistMaster << getVariantPlaylistName(quality, "", i) << std::endl;
		}
	}

	return playlistMaster.str();
}

void Apple_HLS::generateManifestMaster() {
	if (!masterManifestIsWritten) {
		std::ofstream mpl(playlistMasterPath, std::ofstream::out | std::ofstream::trunc);
		mpl << getManifestMasterInternal();
		mpl.close();
		masterManifestIsWritten = true;

		if (type != Static) {
			auto out = outputManifest->getBuffer(0);
			auto metadata = make_shared<const MetadataFile>(playlistMasterPath, PLAYLIST, "", "", timescaleToClock(segDurationInMs, 1000), 0, 1, false, true);
			out->setMetadata(metadata);
			out->setMediaTime(totalDurationInMs, 1000);
			outputManifest->emit(out);
		}
	}
}

void Apple_HLS::updateManifestVariants() {
	if (genVariantPlaylist) {
		firstSegNums.resize(getNumInputs());
		for (int i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			auto const &meta = quality->getMeta();
			auto fn = meta->filename;
			if (fn.empty()) {
				fn = getSegmentName(quality, i, std::to_string(getCurSegNum()));
			}
			auto const sepPos = fn.find_last_of(".");
			auto const ext = fn.substr(sepPos + 1);
			if (!version) {
				if (ext == "m4s") {
					version = 7;
					isCMAF = true;
				} else {
					version = 3;
				}
			}
			auto const firstSegNumPos = fn.substr(0, sepPos).find_last_of("-");
			auto const firstSegNumStr = fn.substr(firstSegNumPos + 1, sepPos - (firstSegNumPos + 1));
			std::istringstream buffer(firstSegNumStr);
			buffer >> firstSegNums[i];

			auto out = make_shared<DataBaseRef>(quality->lastData);
			out->setMetadata(make_shared<const MetadataFile>(format("%s%s", manifestDir, fn), SEGMENT, meta->mimeType, meta->codecName, meta->durationIn180k, meta->filesize, meta->latencyIn180k, meta->startsWithRAP, true));
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
		for (int i = 0; i < getNumInputs() - 1; ++i) {
			auto quality = safe_cast<HLSQuality>(qualities[i].get());
			quality->playlistVariant.str(std::string());
			quality->playlistVariant << "#EXTM3U" << std::endl;
			quality->playlistVariant << "#EXT-X-VERSION:" << version << std::endl;
			quality->playlistVariant << "#EXT-X-TARGETDURATION:" << (segDurationInMs + 500) / 1000 << std::endl;
			if ((int)firstSegNums.size() > i) quality->playlistVariant << "#EXT-X-MEDIA-SEQUENCE:" << firstSegNums[i] << std::endl;
			if (version >= 6) quality->playlistVariant << "#EXT-X-INDEPENDENT-SEGMENTS" << std::endl;
			if (isCMAF) quality->playlistVariant << "#EXT-X-MAP:URI=\"" << getInitName(quality, i) << "\"" << std::endl;
			if (!timeShiftBufferDepthInMs) quality->playlistVariant << "#EXT-X-PLAYLIST-TYPE:EVENT" << std::endl;

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
						m_host->log(Warning, format("Segment \"%s\": could not convert UTC start time %sms. Skippping PROGRAM-DATE-TIME.", seg.startTimeInMs, seg.path).c_str());
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

			if (timeShiftBufferDepthInMs) {
				auto seg = quality->segments.begin();
				while (seg != quality->segments.end()) {
					if ((*seg).startTimeInMs + timeShiftBufferDepthInMs < startTimeInMs + totalDurationInMs) {
						seg = quality->segments.erase(seg);
					} else {
						++seg;
					}
				}
			}

			std::ofstream vpl;
			auto const playlistCurVariantPath = getVariantPlaylistName(quality, manifestDir, i);
			vpl.open(playlistCurVariantPath, std::ofstream::out | std::ofstream::trunc);
			vpl << quality->playlistVariant.str();
			vpl.close();

			auto out = outputManifest->getBuffer(0);
			auto metadata = make_shared<const MetadataFile>(playlistCurVariantPath, PLAYLIST, "", "", timescaleToClock(segDurationInMs, 1000), 0, 1, false, true);
			out->setMetadata(metadata);
			out->setMediaTime(totalDurationInMs, 1000);
			outputManifest->emit(out);
		}
	}
}

void Apple_HLS::generateManifest() {
	updateManifestVariants();
	generateManifestMaster();
}

void Apple_HLS::finalizeManifest() {
	generateManifestVariantFull(true);
}

}
}
