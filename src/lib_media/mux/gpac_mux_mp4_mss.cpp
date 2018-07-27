#include "gpac_mux_mp4_mss.hpp"

extern "C" {
#include <gpac/isomedia.h>
}

namespace Modules {
namespace Mux {

GPACMuxMP4MSS::GPACMuxMP4MSS(IModuleHost* host, const std::string &baseName, uint64_t segmentDurationInMs, const std::string &audioLang, const std::string &audioName)
	: GPACMuxMP4(host,
	      Mp4MuxConfig{
	baseName, segmentDurationInMs,
	IndependentSegment, OneFragmentPerSegment,
	SmoothStreaming | Browsers | NoEditLists | (!audioName.empty() ? SegConstantDur : None) | ((!audioLang.empty() || audioName.empty()) ? ExactInputDur : None)}),
audioLang(audioLang), audioName(audioName) {
}

void GPACMuxMP4MSS::declareStreamAudio(const std::shared_ptr<const MetadataPktLibavAudio> &metadata) {
	GPACMuxMP4::declareStreamAudio(metadata);

	auto extradata = metadata->getExtradata();

	auto const bitsPerSample = metadata->getBitsPerSample() >= 16 ? 16 : metadata->getBitsPerSample();
	ISMLManifest = writeISMLManifest(codec4CC, string2hex(extradata.ptr, extradata.len), metadata->getBitrate(), 0, 0, metadata->getSampleRate(), metadata->getNumChannels(), bitsPerSample);
	for(int k=0; k < 4; ++k)
		ISMLManifest[k] = 0;
}

void GPACMuxMP4MSS::declareStreamSubtitle(const std::shared_ptr<const MetadataPktLibavSubtitle> &metadata) {
	GPACMuxMP4::declareStreamSubtitle(metadata);
	ISMLManifest = writeISMLManifest(codec4CC, "", metadata->getBitrate(), 0, 0, 0, 0, 0);
	for(int k=0; k < 4; ++k)
		ISMLManifest[k] = 0;
}

void GPACMuxMP4MSS::declareStreamVideo(const std::shared_ptr<const MetadataPktLibavVideo> &metadata) {
	GPACMuxMP4::declareStreamVideo(metadata);

	auto extradata = metadata->getExtradata();
	auto const res = metadata->getResolution();
	ISMLManifest = writeISMLManifest(codec4CC, string2hex(extradata.ptr, extradata.len), metadata->getBitrate(), res.width, res.height, 0, 0, 0);
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
		std::string type;
		if (inputs[0]->getMetadata()->isAudio()) type = "audio";
		else if (inputs[0]->getMetadata()->isVideo()) type = "video";
		else if (inputs[0]->getMetadata()->isSubtitle()) type = "textstream";
		else throw error("Only audio, video and subtitle are supported (2)");

		ss << "      <" << type << " src=\"Stream\" systemBitrate=\"" << bitrate << "\">\n";
		ss << "        <param name=\"trackID\" value=\"" << trackId << "\" valuetype=\"data\"/>\n";

		if (type == "audio") {
			ss << "        <param name=\"FourCC\" value=\"" << codec4CC << "\" valuetype=\"data\"/>\n";
			ss << "        <param name=\"CodecPrivateData\" value=\"" << codecPrivate << "\" valuetype=\"data\"/>\n";
			if (!audioName.empty()) ss << "        <param name=\"trackName\" value=\"" << audioName << "\" valuetype=\"data\" />\n";
			if (!audioLang.empty()) ss << "        <param name=\"systemLanguage\" value=\"" << audioLang << "\" valuetype=\"data\" />\n";
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
}
