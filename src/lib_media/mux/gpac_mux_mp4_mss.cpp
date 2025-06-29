#include "gpac_mux_mp4.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_utils/tools.hpp" // operator|
#include "lib_utils/string_tools.hpp" // string2hex
#include <sstream>

extern "C" {
#include <gpac/isomedia.h>
}

using namespace Modules;
using namespace Mux;

namespace {

class GPACMuxMP4MSS : public GPACMuxMP4 {
	public:
		GPACMuxMP4MSS(KHost* host, Mp4MuxConfigMss& cfg);

	private:
		void declareStreamVideo(const MetadataPktVideo* metadata) final;
		void declareStreamAudio(const MetadataPktAudio* metadata) final;
		void declareStreamSubtitle(const MetadataPktSubtitle* metadata) final;
		void startSegmentPostAction() final;

		std::string writeISMLManifest(std::string codec4CC, std::string codecPrivate, int64_t bitrate, int width, int height, uint32_t sampleRate, uint32_t channels, uint16_t bitsPerSample);
		std::string ISMLManifest;
		const std::string audioLang, audioName;
		std::string type;
};

GPACMuxMP4MSS::GPACMuxMP4MSS(KHost* host, Mp4MuxConfigMss& cfg)
	: GPACMuxMP4(host,
	      Mp4MuxConfig{
	          .baseName = cfg.baseName,
	          cfg.segmentDurationInMs,
	          IndependentSegment,
	          OneFragmentPerSegment,
	          SmoothStreaming | Browsers | NoEditLists | (!cfg.audioName.empty() ? SegConstantDur : None) | ((!cfg.audioLang.empty() || cfg.audioName.empty()) ? ExactInputDur : None),
	          cfg.utcStartTime,
	      }),
	  audioLang(cfg.audioLang),
	  audioName(cfg.audioName) {
}

void GPACMuxMP4MSS::declareStreamAudio(const MetadataPktAudio* metadata) {
	GPACMuxMP4::declareStreamAudio(metadata);
	type = "audio";

	auto extradata = metadata->getExtradata();

	auto const bitsPerSample = metadata->bitsPerSample >= 16 ? 16 : metadata->bitsPerSample;
	ISMLManifest = writeISMLManifest(codec4CC, string2hex(extradata.ptr, extradata.len), metadata->bitrate, 0, 0, metadata->sampleRate, metadata->numChannels, bitsPerSample);
	for(int k=0; k < 4; ++k)
		ISMLManifest[k] = 0;
}

void GPACMuxMP4MSS::declareStreamSubtitle(const MetadataPktSubtitle* metadata) {
	GPACMuxMP4::declareStreamSubtitle(metadata);
	type = "textstream";
	ISMLManifest = writeISMLManifest(codec4CC, "", metadata->bitrate, 0, 0, 0, 0, 0);
	for(int k=0; k < 4; ++k)
		ISMLManifest[k] = 0;
}

void GPACMuxMP4MSS::declareStreamVideo(const MetadataPktVideo* metadata) {
	GPACMuxMP4::declareStreamVideo(metadata);
	type = "video";

	auto extradata = metadata->getExtradata();
	auto const res = metadata->resolution;
	ISMLManifest = writeISMLManifest(codec4CC, string2hex(extradata.ptr, extradata.len), metadata->bitrate, res.width, res.height, 0, 0, 0);
	for(int k=0; k < 4; ++k)
		ISMLManifest[k] = 0;
}

void GPACMuxMP4MSS::startSegmentPostAction() {
	gf_isom_set_brand_info(isoCur, GF_4CC('i', 's', 'm', 'l'), 1);
	gf_isom_modify_alternate_brand(isoCur, GF_ISOM_BRAND_ISOM, 1);
	gf_isom_modify_alternate_brand(isoCur, GF_ISOM_BRAND_ISO2, 1);
	gf_isom_modify_alternate_brand(isoCur, GF_4CC('p', 'i', 'f', 'f'), 1);

	bin128 uuid = { 0xa5, 0xd4, 0x0b, 0x30, 0xe8, 0x14, 0x11, 0xdd, 0xba, 0x2f, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 };
	gf_isom_add_uuid(isoCur, -1, uuid, (char*)ISMLManifest.c_str(), (u32)ISMLManifest.size());
}

std::string GPACMuxMP4MSS::writeISMLManifest(std::string codec4CC, std::string codecPrivate, int64_t bitrate, int width, int height, uint32_t sampleRate, uint32_t channels, uint16_t bitsPerSample) {
	std::stringstream ss;
	ss << "    "; //four random chars - don't remove!
	ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
	ss << "<smil xmlns=\"http://www.w3.org/2001/SMIL20/Language\">\n";
	ss << "  <head>\n";
	ss << "    <meta name=\"creator\" content=\"" << "GPAC Licensing Signals, using GPAC " << GPAC_FULL_VERSION << "\" />\n";
	ss << "  </head>\n";
	ss << "  <body>\n";
	ss << "    <switch>\n";
	//TODO: multiple tracks for (i=0; i<nbTracks; ++i)
	{
		if(type == "")
			throw error("Only audio, video and subtitle are supported (2)");

		ss << "      <" << type << " src=\"Stream\" systemBitrate=\"" << bitrate << "\">\n";
		ss << "        <param name=\"trackID\" value=\"" << trackId << "\" valuetype=\"data\"/>\n";

		if (type == "audio") {
			ss << "        <param name=\"FourCC\" value=\"" << codec4CC << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"CodecPrivateData\" value=\"" << codecPrivate << "\" valuetype=\"data\"/>\n";
			if (!audioName.empty()) ss << "        <param name=\"trackName\" value=\"" << audioName << "\" valuetype=\"data\" />\n";
			if (!lang.empty()) ss << "        <param name=\"systemLanguage\" value=\"" << lang << "\" valuetype=\"data\" />\n";
			ss << "        <param name=\"AudioTag\"      value=\"" << 255 << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"Channels\"      value=\"" << channels << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"SamplingRate\"  value=\"" << sampleRate << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"BitsPerSample\" value=\"" << bitsPerSample << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"PacketSize\"    value=\"" << 4 << "\" valuetype=\"data\"/>\n";
		} else if (type == "video") {
			ss << "        <param name=\"FourCC\" value=\"" << codec4CC << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"CodecPrivateData\" value=\"" << codecPrivate << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"MaxWidth\"      value=\"" << width  << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"MaxHeight\"     value=\"" << height << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"DisplayWidth\"  value=\"" << width  << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"DisplayHeight\" value=\"" << height << "\" valuetype=\"data\"/>\n";
		} else if (type == "textstream") {
			ss << "        <param name=\"FourCC\" value=\"" << codec4CC << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"Subtype\" value=\"CAPT\" valuetype=\"data\"/>\n";
			if (!audioName.empty()) ss << "        <param name=\"trackName\" value=\"" << audioName << "\" valuetype=\"data\" />\n";
			if (!audioLang.empty()) ss << "        <param name=\"systemLanguage\" value=\"" << audioLang << "\" valuetype=\"data\" />\n";
		} else
			throw error("Only audio, video or textstream supported (3)");

		ss << "      </" << type << ">\n";
	}
	ss << "    </switch>\n";
	ss << "  </body>\n";
	ss << "</smil>\n";

	return ss.str();
}

}

namespace {

IModule* createObject(KHost* host, void* va) {
	auto config = (Mp4MuxConfigMss*)va;
	enforce(host, "GPACMuxMP4MSS: host can't be NULL");
	enforce(config, "GPACMuxMP4MSS: config can't be NULL");
	return createModule<GPACMuxMP4MSS>(host, *config).release();
}

auto const registered = Factory::registerModule("GPACMuxMP4MSS", &createObject);
}