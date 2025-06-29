#pragma once

#include "adaptive_streaming_common.hpp"
#include "lib_modules/utils/helper.hpp"

struct HlsMuxConfig {
	std::string m3u8Dir;
	std::string m3u8Filename;
	Modules::Stream::AdaptiveStreamingCommon::Type type;
	uint64_t segDurationInMs;
	uint64_t timeShiftBufferDepthInMs = 0;
	bool genVariantPlaylist = false;
	Modules::Stream::AdaptiveStreamingCommon::AdaptiveStreamingCommonFlags flags = Modules::Stream::AdaptiveStreamingCommon::None;
};

namespace Modules {
namespace Stream {

class Apple_HLS : public AdaptiveStreamingCommon {
	public:
		Apple_HLS(KHost* host, HlsMuxConfig* cfg);
		virtual ~Apple_HLS();

	private:
		std::unique_ptr<Quality> createQuality() const override;
		void generateManifest() override;
		void finalizeManifest() override;

		struct HLSQuality;

		std::string getVariantPlaylistName(HLSQuality const * const quality, const std::string &subDir, size_t index);
		void updateManifestVariants();
		void generateManifestVariantFull(bool isLast);

		std::string getManifestMasterInternal();
		void generateManifestMaster();

		KHost* const m_host;

		std::string playlistMasterPath;
		const bool genVariantPlaylist;

		unsigned version = 0;
		bool masterManifestIsWritten = false, isCMAF = false;
		std::vector<uint64_t> firstSegNums;
		uint64_t timeShiftBufferDepthInMs = 0;
};

}
}
