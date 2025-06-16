#include "gpac_mux_mp4.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/time.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log.hpp" // g_Log
#include "../common/gpacpp.hpp"
#include "../common/attributes.hpp"
#include "../common/metadata_file.hpp"
#include "lib_modules/utils/factory.hpp"
#include <algorithm> //std::max

extern "C" {
#include <gpac/base_coding.h>
#include <gpac/constants.h>
#include <gpac/internal/media_dev.h>
}

auto const AVC_INBAND_CONFIG = 0;
auto const TIMESCALE_MUL = 100; // offers a tolerance on VFR or faulty streams

namespace Modules {

#define SAFE(call) \
  do { \
	auto error = call; \
	if (error) \
		throw std::runtime_error(format("[%s:%d] %s: %s", __FILE__, __LINE__, #call, gf_error_to_string(error))); \
  } while(0)

namespace {

uint64_t fileSize(const std::string &fn) {
	FILE *file = gf_fopen(fn.c_str(), "rb");
	if (!file) {
		return 0;
	}
	gf_fseek(file, 0, SEEK_END);
	auto const size = gf_ftell(file);
	gf_fclose(file);
	return size;
}

Span getBsContent(GF_ISOFile *iso, bool newBs) {
	GF_BitStream *bs = NULL;
	SAFE(gf_isom_get_bs(iso, &bs));

	char* output;
	u32 size;
	gf_bs_get_content(bs, &output, &size);

	if (newBs) {
		auto bsNew = gf_bs_new(nullptr, 0, GF_BITSTREAM_WRITE);
		memcpy(bs, bsNew, 2*sizeof(void*)); //HACK: GPAC GF_BitStream.original needs to be non-NULL
		memset(bsNew,  0, 2*sizeof(void*));
		gf_bs_del(bsNew);
	}

	return {(uint8_t*)output, (size_t)size};
}

static GF_Err import_extradata_avc(SpanC extradata, GF_AVCConfig *dstcfg) {
	if (!extradata.ptr || !extradata.len) {
		g_Log->log(Warning, "No initial SPS/PPS provided.");
		return GF_OK;
	}

	auto bs2 = std::shared_ptr<GF_BitStream>(gf_bs_new((const char*)extradata.ptr, extradata.len, GF_BITSTREAM_READ), &gf_bs_del);
	auto bs = bs2.get();
	if (!bs) {
		return GF_BAD_PARAM;
	}

	auto avc = make_unique<AVCState>();
	memset(avc.get(), 0, sizeof(*avc));

	//Find start code
	{
		auto a = gf_bs_read_u8(bs);
		auto b = gf_bs_read_u8(bs);
		auto c = gf_bs_read_u8(bs);
		if ((a << 16) + (b << 8) + c != 0x000001) {
			auto d = gf_bs_read_u8(bs);
			if ((a << 24) + (b << 16) + (c << 8) + d != 0x00000001) {
				return GF_NON_COMPLIANT_BITSTREAM;
			}
		}
	}

	//SPS
	u64 nalStart = gf_bs_get_position(bs);
	{
		s32 idx = 0;
		char *buffer = nullptr;
parse_sps:
		auto const nalSize = gf_media_nalu_next_start_code_bs(bs);
		if (nalStart + nalSize > extradata.len) {
			return GF_BAD_PARAM;
		}
		buffer = (char*)gf_malloc(nalSize);
		gf_bs_read_data(bs, buffer, nalSize);
		gf_bs_seek(bs, nalStart);
		auto const type = gf_bs_read_u8(bs) & 0x1F;
		if (type == GF_AVC_NALU_ACCESS_UNIT) {
			nalStart += nalSize + 4;
			gf_bs_seek(bs, nalStart);
			gf_free(buffer);
			goto parse_sps;
		}
		if (type != GF_AVC_NALU_SEQ_PARAM) {
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		idx = gf_media_avc_read_sps(buffer, nalSize, avc.get(), 0, nullptr);
		if (idx < 0) {
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		dstcfg->configurationVersion = 1;
		dstcfg->profile_compatibility = avc->sps[idx].prof_compat;
		dstcfg->AVCProfileIndication = avc->sps[idx].profile_idc;
		dstcfg->AVCLevelIndication = avc->sps[idx].level_idc;
		dstcfg->chroma_format = avc->sps[idx].chroma_format;
		dstcfg->luma_bit_depth = 8 + avc->sps[idx].luma_bit_depth_m8;
		dstcfg->chroma_bit_depth = 8 + avc->sps[idx].chroma_bit_depth_m8;

		{
			auto slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = nalSize;
			slc->id = idx;
			slc->data = buffer;
			gf_list_add(dstcfg->sequenceParameterSets, slc);
		}
		nalStart += 4 + nalSize;
	}

	//PPS
	{
		gf_bs_seek(bs, nalStart);
		auto const nalSize = gf_media_nalu_next_start_code_bs(bs);
		if (nalStart + nalSize > extradata.len) {
			return GF_BAD_PARAM;
		}
		char* buffer = (char*)gf_malloc(nalSize);
		gf_bs_read_data(bs, buffer, nalSize);
		gf_bs_seek(bs, nalStart);
		if ((gf_bs_read_u8(bs) & 0x1F) != GF_AVC_NALU_PIC_PARAM) {
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		auto const idx = gf_media_avc_read_pps(buffer, nalSize, avc.get());
		if (idx < 0) {
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		{
			auto slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = nalSize;
			slc->id = idx;
			slc->data = buffer;
			gf_list_add(dstcfg->pictureParameterSets, slc);
		}
	}

	return GF_OK;
}

/**
* A function which takes FFmpeg H265 extradata (SPS/PPS) and bring them ready to be pushed to the MP4 muxer.
* @param extradata
* @param dstcfg
* @returns GF_OK is the extradata was parsed and is valid, other values otherwise.
*/
static GF_Err import_extradata_hevc(SpanC extradata, GF_HEVCConfig *dstCfg) {
	GF_HEVCParamArray *vpss = nullptr, *spss = nullptr, *ppss = nullptr;

	if (!extradata.ptr || (extradata.len < sizeof(u32)))
		return GF_BAD_PARAM;
	auto bs = gf_bs_new((const char*)extradata.ptr, extradata.len, GF_BITSTREAM_READ);
	if (!bs)
		return GF_BAD_PARAM;

	auto hevc = make_unique<HEVCState>();
	memset(hevc.get(), 0, sizeof(HEVCState));
	hevc->sps_active_idx = -1;

	while (gf_bs_available(bs)) {
		u64 NALStart = 0;
		u32 NALSize = 0;
		const u32 startCode = gf_bs_read_u24(bs);
		if (!(startCode == 0x000001) && !(!startCode && gf_bs_read_u8(bs) == 1)) {
			gf_bs_del(bs);
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		NALStart = gf_bs_get_position(bs);
		NALSize = gf_media_nalu_next_start_code_bs(bs);
		if (NALStart + NALSize > extradata.len) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}

		std::vector<char> buffer(NALSize);

		gf_bs_read_data(bs, buffer.data(), NALSize);
		gf_bs_seek(bs, NALStart);

		u8 NALUnitType, temporalId, layerId;
		gf_media_hevc_parse_nalu(buffer.data(), NALSize, hevc.get(), &NALUnitType, &temporalId, &layerId);
		if (layerId) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}

		switch (NALUnitType) {
		case GF_HEVC_NALU_VID_PARAM: {
			auto const idx = gf_media_hevc_read_vps(buffer.data(), NALSize, hevc.get());
			if (idx < 0) {
				gf_bs_del(bs);
				return GF_BAD_PARAM;
			}

			assert(hevc->vps[idx].state == 1); //we don't expect multiple VPS
			if (hevc->vps[idx].state == 1) {
				hevc->vps[idx].state = 2;
				hevc->vps[idx].crc = gf_crc_32(buffer.data(), NALSize);

				dstCfg->avgFrameRate = hevc->vps[idx].rates[0].avg_pic_rate;
				dstCfg->constantFrameRate = hevc->vps[idx].rates[0].constand_pic_rate_idc;
				dstCfg->numTemporalLayers = hevc->vps[idx].max_sub_layers;
				dstCfg->temporalIdNested = hevc->vps[idx].temporal_id_nesting;

				if (!vpss) {
					GF_SAFEALLOC(vpss, GF_HEVCParamArray);
					vpss->nalus = gf_list_new();
					gf_list_add(dstCfg->param_array, vpss);
					vpss->array_completeness = 1;
					vpss->type = GF_HEVC_NALU_VID_PARAM;
				}

				auto slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
				slc->size = NALSize;
				slc->id = idx;
				slc->data = (char*)gf_malloc(slc->size);
				memcpy(slc->data, buffer.data(), slc->size);

				gf_list_add(vpss->nalus, slc);
			}
			break;
		}
		case GF_HEVC_NALU_SEQ_PARAM: {
			auto const idx = gf_media_hevc_read_sps(buffer.data(), NALSize, hevc.get());
			if (idx < 0) {
				gf_bs_del(bs);
				return GF_BAD_PARAM;
			}

			assert(!(hevc->sps[idx].state & AVC_SPS_DECLARED)); //we don't expect multiple SPS
			if ((hevc->sps[idx].state & AVC_SPS_PARSED) && !(hevc->sps[idx].state & AVC_SPS_DECLARED)) {
				hevc->sps[idx].state |= AVC_SPS_DECLARED;
				hevc->sps[idx].crc = gf_crc_32(buffer.data(), NALSize);
			}

			dstCfg->configurationVersion = 1;
			dstCfg->profile_space = hevc->sps[idx].ptl.profile_space;
			dstCfg->tier_flag = hevc->sps[idx].ptl.tier_flag;
			dstCfg->profile_idc = hevc->sps[idx].ptl.profile_idc;
			dstCfg->general_profile_compatibility_flags = hevc->sps[idx].ptl.profile_compatibility_flag;
			dstCfg->progressive_source_flag = hevc->sps[idx].ptl.general_progressive_source_flag;
			dstCfg->interlaced_source_flag = hevc->sps[idx].ptl.general_interlaced_source_flag;
			dstCfg->non_packed_constraint_flag = hevc->sps[idx].ptl.general_non_packed_constraint_flag;
			dstCfg->frame_only_constraint_flag = hevc->sps[idx].ptl.general_frame_only_constraint_flag;

			dstCfg->constraint_indicator_flags = hevc->sps[idx].ptl.general_reserved_44bits;
			dstCfg->level_idc = hevc->sps[idx].ptl.level_idc;

			dstCfg->chromaFormat = hevc->sps[idx].chroma_format_idc;
			dstCfg->luma_bit_depth = hevc->sps[idx].bit_depth_luma;
			dstCfg->chroma_bit_depth = hevc->sps[idx].bit_depth_chroma;

			if (!spss) {
				GF_SAFEALLOC(spss, GF_HEVCParamArray);
				spss->nalus = gf_list_new();
				gf_list_add(dstCfg->param_array, spss);
				spss->array_completeness = 1;
				spss->type = GF_HEVC_NALU_SEQ_PARAM;
			}

			auto slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = NALSize;
			slc->id = idx;
			slc->data = (char*)gf_malloc(NALSize);
			memcpy(slc->data, buffer.data(), NALSize);
			gf_list_add(spss->nalus, slc);
			break;
		}
		case GF_HEVC_NALU_PIC_PARAM: {
			auto const idx = gf_media_hevc_read_pps(buffer.data(), NALSize, hevc.get());
			if (idx < 0) {
				gf_bs_del(bs);
				return GF_BAD_PARAM;
			}

			assert(hevc->pps[idx].state == 1); //we don't expect multiple PPS
			if (hevc->pps[idx].state == 1) {
				hevc->pps[idx].state = 2;
				hevc->pps[idx].crc = gf_crc_32(buffer.data(), NALSize);

				if (!ppss) {
					GF_SAFEALLOC(ppss, GF_HEVCParamArray);
					ppss->nalus = gf_list_new();
					gf_list_add(dstCfg->param_array, ppss);
					ppss->array_completeness = 1;
					ppss->type = GF_HEVC_NALU_PIC_PARAM;
				}

				auto slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
				slc->size = NALSize;
				slc->id = idx;
				slc->data = (char*)gf_malloc(NALSize);
				memcpy(slc->data, buffer.data(), NALSize);

				gf_list_add(ppss->nalus, slc);
			}
			break;
		}
		default:
			break;
		}

		if (gf_bs_seek(bs, NALStart + NALSize)) {
			assert(NALStart + NALSize <= gf_bs_get_size(bs));
			break;
		}
	}

	gf_bs_del(bs);

	return GF_OK;
}

void annexbToAvcc(SpanC buf, GF_ISOSample &sample) {
	u32 startCodeSize = 0;

	GF_BitStream* out_bs = gf_bs_new(nullptr, 2 * buf.len, GF_BITSTREAM_WRITE);
	auto NALUSize = gf_media_nalu_next_start_code(buf.ptr, buf.len, &startCodeSize);
	if (NALUSize != 0) {
		gf_bs_write_u32(out_bs, NALUSize);
		gf_bs_write_data(out_bs, (const char*)buf.ptr, NALUSize);
	}
	if (startCodeSize) {
		buf.ptr += (NALUSize + startCodeSize);
		buf.len -= (NALUSize + startCodeSize);
	}

	while (buf.len) {
		NALUSize = gf_media_nalu_next_start_code(buf.ptr, buf.len, &startCodeSize);
		if (NALUSize != 0) {
			gf_bs_write_u32(out_bs, NALUSize);
			gf_bs_write_data(out_bs, (const char*)buf.ptr, NALUSize);
		}

		buf.ptr += NALUSize;

		if (!startCodeSize || (buf.len < NALUSize + startCodeSize))
			break;
		buf.len -= NALUSize + startCodeSize;
		buf.ptr += startCodeSize;
	}
	gf_bs_get_content(out_bs, &sample.data, &sample.dataLength);
	gf_bs_del(out_bs);
}
}

namespace Mux {

GPACMuxMP4::GPACMuxMP4(KHost* host, Mp4MuxConfig const& cfg)
	: lang(cfg.lang),
	  m_host(host),
	  m_utcStartTime(cfg.utcStartTime),
	  MP4_4CC(cfg.MP4_4CC),
	  compatFlags(cfg.compatFlags),
	  fragmentPolicy(cfg.fragmentPolicy),
	  segmentPolicy(cfg.segmentPolicy),
	  segmentDuration(cfg.segmentDurationInMs, 1000) {
	if ((cfg.segmentDurationInMs == 0) != (segmentPolicy == NoSegment || segmentPolicy == SingleSegment))
		throw error(format("Inconsistent parameters: segment duration is %sms but no segment.", cfg.segmentDurationInMs));
	if ((cfg.segmentDurationInMs == 0) && (fragmentPolicy == OneFragmentPerSegment))
		throw error("Inconsistent parameters: segment duration is 0 ms but requested one fragment by segment.");
	if ((segmentPolicy == SingleSegment || segmentPolicy == FragmentedSegment) && (fragmentPolicy == NoFragment))
		throw error("Inconsistent parameters: segmented policies require fragmentation to be enabled.");
	if ((compatFlags & SmoothStreaming) && (segmentPolicy != IndependentSegment))
		throw error("Inconsistent parameters: SmoothStreaming compatibility requires IndependentSegment policy.");
	if ((compatFlags & FlushFragMemory) && (!cfg.baseName.empty() || segmentPolicy != FragmentedSegment))
		throw error("Inconsistent parameters: FlushFragMemory requires an empty segment name and FragmentedSegment policy.");

	const char* pInitName = nullptr;

	baseName = cfg.baseName;

	if (!baseName.empty()) {
		if (segmentPolicy > NoSegment) {
			initName = baseName + "-init.mp4";
		} else {
			initName = baseName + ".mp4";
		}

		pInitName = initName.c_str();

		m_host->log(Warning, "File mode is deprecated");
	}

	isoInit = gf_isom_open(pInitName, GF_ISOM_OPEN_WRITE, nullptr);
	if (!isoInit)
		throw error(format("Cannot open isoInit file %s", segmentName));

	isoCur = isoInit;
	segmentName = initName;

	GF_Err e = gf_isom_set_storage_mode(isoCur, GF_ISOM_STORE_INTERLEAVED);
	if (e != GF_OK)
		throw error(format("Cannot make iso file %s interleaved", baseName));

	if (compatFlags & FlushFragMemory) {
		//TODO: retrieve framerate, and multiply the allocator size
		this->allocatorSize = 100 * ALLOC_NUM_BLOCKS_DEFAULT;
	}

	output = addOutput();
}

GPACMuxMP4::~GPACMuxMP4() {
	gf_isom_delete(isoCur);
}

void GPACMuxMP4::flush() {
	if (compatFlags & ExactInputDur) {
		if (lastData) {
			processOne(lastData);
			lastData = nullptr;
		}
	}
	closeSegment(true);

	if (segmentPolicy == IndependentSegment) {
		assert(isoInit);
		gf_isom_delete(isoInit);
		isoInit = nullptr;
		if (!initName.empty())
			gf_delete_file(initName.c_str());
	} else {
		GF_Err e = gf_isom_close(isoCur);
		if (e != GF_OK && e != GF_ISOM_INVALID_FILE)
			throw error(format("gf_isom_close: %s", gf_error_to_string(e)));
		isoCur = nullptr;
	}
}

void GPACMuxMP4::updateSegmentName() {
	if (!initName.empty()) {
		auto ss = baseName + "-" + std::to_string(segmentNum);
		if (segmentPolicy == FragmentedSegment)
			ss += ".m4s";
		else
			ss += ".mp4";
		segmentName = ss;
	}
}

void GPACMuxMP4::startSegment() {

	switch(segmentPolicy) {
	case NoSegment:
	case SingleSegment:
		break;
	case IndependentSegment:
		updateSegmentName();
		isoCur = gf_isom_open(segmentName.empty() ? nullptr : segmentName.c_str(), GF_ISOM_OPEN_WRITE, nullptr);
		if (!isoCur)
			throw error("Cannot open isoCur file");
		declareStream(inputs[0]->getMetadata().get());
		startSegmentPostAction();
		setupFragments();
		gf_isom_set_next_moof_number(isoCur, (u32)nextFragmentNum);
		break;
	case FragmentedSegment:
		updateSegmentName();
		SAFE(gf_isom_start_segment(isoCur, segmentName.empty() ? nullptr : segmentName.c_str(), GF_TRUE));
		break;
	}
}

void GPACMuxMP4::closeSegment(bool isLastSeg) {
	if (curFragmentDurInTs) {
		if(fragmentPolicy != NoFragment)
			closeFragment();
	}

	if (!isLastSeg && segmentPolicy <= SingleSegment) {
		return;
	}

	if (segmentPolicy == FragmentedSegment) {
		GF_Err e = gf_isom_close_segment(isoCur, 0, 0, 0, 0, 0, GF_FALSE, GF_FALSE, (Bool)isLastSeg, (Bool)(!initName.empty()),
		        (compatFlags & Browsers) ? 0 : GF_4CC('e', 'o', 'd', 's'), nullptr, nullptr, &lastSegmentSize);
		if (e != GF_OK) {
			if (m_DTS == 0)
				return;
			throw error(format("gf_isom_close_segment: %s", gf_error_to_string(e)));
		}
	}

	sendSegmentToOutput(true);
	m_host->log(Debug, format("Segment %s completed (size %s) (startsWithSAP=%s)", segmentName.empty() ? "[in memory]" : segmentName, lastSegmentSize, segmentStartsWithRAP).c_str());

	curSegmentDurInTs = 0;
}

void GPACMuxMP4::startFragment(uint64_t DTS, uint64_t PTS) {
	if (fragmentPolicy > NoFragment) {
		curFragmentDurInTs = 0;

		GF_Err e = gf_isom_start_fragment(isoCur, GF_TRUE);
		if (e != GF_OK)
			throw error(format("Impossible to create the moof starting the fragment: %s", gf_error_to_string(e)));

		if (segmentPolicy >= IndependentSegment) {
			if (compatFlags & SmoothStreaming) {
				SAFE(gf_isom_set_fragment_option(isoCur, trackId, GF_ISOM_TFHD_FORCE_MOOF_BASE_OFFSET, 1));
			} else {
				auto const baseMediaDecodeTime = DTS + rescale(firstDataAbsTimeInMs, 1000, timeScale);
				SAFE(gf_isom_set_traf_base_media_decode_time(isoCur, trackId, baseMediaDecodeTime));
			}

			if (!(compatFlags & Browsers)) {
				auto utcPts = firstDataAbsTimeInMs + rescale(PTS, timeScale, 1000);
				SAFE(gf_isom_set_fragment_reference_time(isoCur, trackId, UTC2NTP(utcPts), PTS));
			}
		}
	}
}

void GPACMuxMP4::closeFragment() {
	if (!curFragmentDurInTs) {
		m_host->log((compatFlags & Browsers) ? Error : Warning, "Writing an empty fragment. Some players may stop playing here.");
	}

	if (compatFlags & SmoothStreaming) {
		if (timeScale == 0) {
			m_host->log(Warning, "Media timescale is 0. Fragment cannot be closed.");
			return;
		}

		auto const curFragmentStartInTs = m_DTS - curFragmentDurInTs;
		auto const absTimeInTs = rescale(firstDataAbsTimeInMs, 1000, timeScale) + curFragmentStartInTs;
		auto const deltaRealTimeInMs = 1000 * (double)(getUTC() - Fraction(absTimeInTs, timeScale));

		{
			auto const isSuspicious = deltaRealTimeInMs < 0 || deltaRealTimeInMs > curFragmentStartInTs || curFragmentDurInTs != fractionToTimescale(segmentDuration, timeScale);
			m_host->log(isSuspicious ? Warning : Debug,
			    format("Closing MSS fragment with absolute time %s %s UTC and duration %s (timescale %s, time=%s, deltaRT=%s)",
			        getDay(), getTimeFromUTC(), curFragmentDurInTs, timeScale, absTimeInTs, deltaRealTimeInMs).c_str());
		}

		SAFE(gf_isom_set_traf_mss_timeext(isoCur, trackId, absTimeInTs, curFragmentDurInTs));
	}

	if ((segmentPolicy == FragmentedSegment) || (segmentPolicy == SingleSegment)) {
		GF_Err e = gf_isom_flush_fragments(isoCur, GF_FALSE); //writes a 'styp'
		if (e != GF_OK)
			throw error("Can't flush fragments");

		if (compatFlags & FlushFragMemory) {
			sendSegmentToOutput(false);
		}
	}

	curFragmentDurInTs = 0;
}

void GPACMuxMP4::setupFragments() {
	if (fragmentPolicy > NoFragment) {
		SAFE(gf_isom_setup_track_fragment(isoCur, trackId, 1, compatFlags & SmoothStreaming ? 0 : (u32)defaultSampleIncInTs, 0, 0, 0, 0, 0));

		int mode;

		if (segmentPolicy == NoSegment || segmentPolicy == IndependentSegment)
			mode = 0;
		else if (segmentPolicy == SingleSegment)
			mode = 2;
		else
			mode = 1;

		SAFE(gf_isom_finalize_for_fragment(isoCur, mode)); //writes moov

		if (segmentPolicy == FragmentedSegment) {
			if (gf_isom_get_filename(isoCur)) {
				SAFE(gf_isom_flush_fragments(isoCur, GF_FALSE)); //writes init to disk
			}
			sendSegmentToOutput(true); //init
		}
	}
}

static auto deleteEsd = [](GF_ESD* p) {
	gf_odf_desc_del((GF_Descriptor*)p);
};

void GPACMuxMP4::declareStreamAudio(const MetadataPktAudio* metadata) {
	u32 di=0;
	GF_M4ADecSpecInfo acfg {};

	auto esd = std::shared_ptr<GF_ESD>(gf_odf_desc_esd_new(2), deleteEsd);
	if (!esd)
		throw error("Cannot create GF_ESD for audio");

	esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	timeScale = sampleRate = metadata->sampleRate;
	m_host->log(Debug, format("TimeScale: %s", timeScale).c_str());
	defaultSampleIncInTs = metadata->frameSize;

	auto const trackNum = gf_isom_new_track(isoCur, esd->ESID, GF_ISOM_MEDIA_AUDIO, timeScale);
	if (!trackNum)
		throw error("Cannot create new track");

	trackId = gf_isom_get_track_id(isoCur, trackNum);

	esd->ESID = 1;
	if (metadata->codec == "aac_raw") {
		codec4CC = "AACL";
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_AAC_MPEG4;
		esd->slConfig->timestampResolution = sampleRate;

		acfg.base_object_type = GF_M4A_AAC_LC;
		acfg.base_sr = sampleRate;
		acfg.nb_chan = metadata->numChannels;
		acfg.sbr_object_type = 0;
		acfg.audioPL = gf_m4a_get_profile(&acfg);

		SAFE(gf_m4a_write_config(&acfg, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength));
		SAFE(gf_isom_new_mpeg4_description(isoCur, trackNum, esd.get(), nullptr, nullptr, &di));

	} else if (metadata->codec == "mp2")	{
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_MPEG1;
		esd->decoderConfig->bufferSizeDB = 20;
		esd->slConfig->timestampResolution = sampleRate;
		esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *)gf_odf_desc_new(GF_ODF_DSI_TAG);

		acfg.base_object_type = GF_M4A_LAYER2;
		acfg.base_sr = sampleRate;
		acfg.nb_chan = metadata->numChannels;
		acfg.sbr_object_type = 0;
		acfg.audioPL = gf_m4a_get_profile(&acfg);

		SAFE(gf_m4a_write_config(&acfg, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength));
		SAFE(gf_isom_new_mpeg4_description(isoCur, trackNum, esd.get(), nullptr, nullptr, &di));

	} else if (metadata->codec == "ac3" || metadata->codec == "eac3") {
		bool is_EAC3 = metadata->codec == "eac3";

		auto extradata = metadata->getExtradata();
		auto bs2 = std::shared_ptr<GF_BitStream>(gf_bs_new((const char*)extradata.ptr, extradata.len, GF_BITSTREAM_READ), &gf_bs_del);
		auto bs = bs2.get();
		if (!bs)
			throw error(format("(E)AC-3: impossible to create extradata bitstream (\"%s\", size=%s)", metadata->codec, extradata.len));

		GF_AC3Header hdr  {};
		if (is_EAC3 || !gf_ac3_parser_bs(bs, &hdr, GF_TRUE)) {
			if (!gf_eac3_parser_bs(bs, &hdr, GF_TRUE)) {
				m_host->log(Error, format("Parsing: audio is neither AC3 or E-AC3 audio (\"%s\", size=%s)", metadata->codec, extradata.len).c_str());
			}
		}

		GF_AC3Config cfg {};
		cfg.is_ec3 = is_EAC3;
		cfg.nb_streams = 1;
		cfg.brcode = hdr.brcode;
		cfg.streams[0].acmod = hdr.acmod;
		cfg.streams[0].bsid = hdr.bsid;
		cfg.streams[0].bsmod = hdr.bsmod;
		cfg.streams[0].fscod = hdr.fscod;
		cfg.streams[0].lfon = hdr.lfon;

		SAFE(gf_isom_ac3_config_new(isoCur, trackNum, &cfg, nullptr, nullptr, &di));
	} else
		throw error(format("Unsupported audio codec \"%s\"", metadata->codec));

	auto const bitsPerSample = std::min(16, (int)metadata->bitsPerSample);

	if (!lang.empty())
		SAFE(gf_isom_set_media_language(isoCur, trackNum, (char*)lang.c_str()));

	SAFE(gf_isom_set_track_enabled(isoCur, trackNum, GF_TRUE));
	SAFE(gf_isom_set_audio_info(isoCur, trackNum, di, sampleRate, metadata->numChannels, bitsPerSample, GF_IMPORT_AUDIO_SAMPLE_ENTRY_NOT_SET));
	SAFE(gf_isom_set_pl_indication(isoCur, GF_ISOM_PL_AUDIO, acfg.audioPL));

	if (!(compatFlags & SegmentAtAny)) {
		m_host->log(Info, "Audio detected: assuming all segments are RAPs.");
		compatFlags = compatFlags | SegmentAtAny;
	}
}

void GPACMuxMP4::declareStreamSubtitle(const MetadataPktSubtitle* /*metadata*/) {
	timeScale = 10 * TIMESCALE_MUL;
	assert((timeScale % 1000) == 0); /*ms accuracy mandatory*/
	u32 trackNum = gf_isom_new_track(isoCur, 0, GF_ISOM_MEDIA_TEXT, timeScale);
	if (!trackNum)
		throw error("Cannot create new track");
	trackId = gf_isom_get_track_id(isoCur, trackNum);

	SAFE(gf_isom_set_track_enabled(isoCur, trackNum, GF_TRUE));

	u32 di;
	SAFE(gf_isom_new_xml_subtitle_description(isoCur, trackNum, "http://www.w3.org/ns/ttml", NULL, NULL, &di));

	codec4CC = "TTML";
	if (!(compatFlags & SegmentAtAny)) {
		m_host->log(Info, "Subtitles detected: assuming all segments are RAPs.");
		compatFlags = compatFlags | SegmentAtAny;
	}
}

void GPACMuxMP4::declareStreamVideo(const MetadataPktVideo* metadata) {
	timeScale = (uint32_t)(metadata->timeScale.num * TIMESCALE_MUL);
	u32 trackNum = gf_isom_new_track(isoCur, 0, GF_ISOM_MEDIA_VISUAL, timeScale);
	if (!trackNum)
		throw error("Cannot create new track");
	trackId = gf_isom_get_track_id(isoCur, trackNum);
	defaultSampleIncInTs = metadata->timeScale.den * TIMESCALE_MUL;
	resolution = metadata->resolution;

	SAFE(gf_isom_set_track_enabled(isoCur, trackNum, GF_TRUE));

	isAnnexB = true;
	auto extradata = metadata->getExtradata();

	GF_Err e;

	u32 di = 0;
	if (metadata->codec == "h264_annexb" || metadata->codec == "h264_avcc") {
		isAnnexB = metadata->codec == "h264_annexb";
		codec4CC = "H264";
		std::shared_ptr<GF_AVCConfig> avccfg(gf_odf_avc_cfg_new(), &gf_odf_avc_cfg_del);
		if (!avccfg)
			throw error("Container format import failed (AVC)");

		e = import_extradata_avc(extradata, avccfg.get());
		if (e == GF_OK) {
			SAFE(gf_isom_avc_config_new(isoCur, trackNum, avccfg.get(), nullptr, nullptr, &di));
		}
	} else if (metadata->codec == "hevc_annexb" || metadata->codec == "hevc_avcc") {
		isAnnexB = metadata->codec == "hevc_annexb";
		codec4CC = "H265";
		std::shared_ptr<GF_HEVCConfig> hevccfg(gf_odf_hevc_cfg_new(), &gf_odf_hevc_cfg_del);
		if (!hevccfg)
			throw error("Container format import failed (HEVC)");

		e = import_extradata_hevc(extradata, hevccfg.get());
		if (e == GF_OK) {
			SAFE(gf_isom_hevc_config_new(isoCur, trackNum, hevccfg.get(), nullptr, nullptr, &di));
		}
	} else if (MP4_4CC != 0) {
		GF_GenericSampleDescription sdesc = {};
		sdesc.codec_tag = MP4_4CC;
		isAnnexB = false;
		m_host->log(Warning, format("Using generic packaging for codec '%s%s%s%s'",
		        (char)((MP4_4CC>>24)&0xff), (char)((MP4_4CC>>16)&0xff), (char)((MP4_4CC>>8)&0xff), (char)(MP4_4CC&0xff)).c_str());

		sdesc.extension_buf = (char*)gf_malloc(extradata.len);
		memcpy(sdesc.extension_buf, extradata.ptr, extradata.len);
		sdesc.extension_buf_size = (u32)extradata.len;
		if (!sdesc.vendor_code) sdesc.vendor_code = GF_VENDOR_GPAC;

		e = gf_isom_new_generic_sample_description(isoCur, trackNum, NULL, NULL, &sdesc, &di);
		if (e != GF_OK)
			throw error(format("Cannot create generic sample config: %s", gf_error_to_string(e)));
	} else {
		m_host->log(Warning, format("Unknown codec '%s': using generic packaging.", metadata->codec).c_str());
		e = GF_NON_COMPLIANT_BITSTREAM;
	}

	if (e) {
		if (e == GF_NON_COMPLIANT_BITSTREAM) {
			m_host->log(Debug, "Non Annex B: assume this is MP4 already");
			isAnnexB = false;

			auto esdPtr = std::shared_ptr<GF_ESD>(gf_odf_desc_esd_new(0), deleteEsd);
			auto& esd = *esdPtr;
			esd.ESID = 1; /*FIXME: only one track: set trackID?*/
			esd.decoderConfig->streamType = GF_STREAM_VISUAL;
			esd.decoderConfig->objectTypeIndication = metadata->codec == "h264_annexb" || metadata->codec == "h264_avcc" ? GPAC_OTI_VIDEO_AVC : GPAC_OTI_VIDEO_HEVC;
			esd.decoderConfig->decoderSpecificInfo->dataLength = (u32)extradata.len;
			esd.decoderConfig->decoderSpecificInfo->data = (char*)gf_malloc(extradata.len);
			memcpy(esd.decoderConfig->decoderSpecificInfo->data, extradata.ptr, extradata.len);
			esd.slConfig->predefined = SLPredef_MP4;

			SAFE(gf_isom_new_mpeg4_description(isoCur, trackNum, &esd, nullptr, nullptr, &di));
		} else
			throw error("Container format import failed");
	}

	resolution = metadata->resolution;
	gf_isom_set_visual_info(isoCur, gf_isom_get_track_by_id(isoCur, trackId), di, resolution.width, resolution.height);
	gf_isom_set_sync_table(isoCur, trackNum);

	if(AVC_INBAND_CONFIG) {
		//inband SPS/PPS
		if (segmentPolicy != NoSegment) {
			SAFE(gf_isom_avc_set_inband_config(isoCur, trackNum, di));
		}
	}
}

void GPACMuxMP4::declareStream(const IMetadata* metadata) {
	if (auto video = dynamic_cast<const MetadataPktVideo*>(metadata)) {
		declareStreamVideo(video);
	} else if (auto audio = dynamic_cast<const MetadataPktAudio*>(metadata)) {
		declareStreamAudio(audio);
	} else if (auto subs = dynamic_cast<const MetadataPktSubtitle*>(metadata)) {
		declareStreamSubtitle(subs);
	} else
		throw error("Stream creation failed: unknown type.");
}

void GPACMuxMP4::handleInitialTimeOffset() {
	//FIXME: we use DTS here: that's convenient (because the DTS is monotonic) and *most of the time* right when we control the encoding
	if (initDTSIn180k) { /*first timestamp is not zero*/
		m_host->log(Info, format("Initial offset: %ss (4CC=%s, \"%s\", timescale=%s/%s)", initDTSIn180k / (double)IClock::Rate, codec4CC, segmentName, timeScale, gf_isom_get_timescale(isoCur)).c_str());
		if (compatFlags & NoEditLists) {
			firstDataAbsTimeInMs += clockToTimescale(initDTSIn180k, 1000);
		} else {
			auto const edtsInMovieTs = clockToTimescale(initDTSIn180k, gf_isom_get_timescale(isoCur));
			auto const edtsInMediaTs = clockToTimescale(initDTSIn180k, timeScale);
			if (edtsInMovieTs > 0) {
				gf_isom_append_edit_segment(isoCur, gf_isom_get_track_by_id(isoCur, trackId), edtsInMovieTs, 0, GF_ISOM_EDIT_EMPTY);
				gf_isom_append_edit_segment(isoCur, gf_isom_get_track_by_id(isoCur, trackId), edtsInMovieTs, 0, GF_ISOM_EDIT_NORMAL);
				curSegmentDeltaInTs = edtsInMediaTs;
			} else {
				gf_isom_append_edit_segment(isoCur, gf_isom_get_track_by_id(isoCur, trackId), 0, -edtsInMediaTs, GF_ISOM_EDIT_NORMAL);
			}
		}
	}
}

void GPACMuxMP4::sendSegmentToOutput(bool EOS) {
	if (segmentPolicy == IndependentSegment) {
		nextFragmentNum = gf_isom_get_next_moof_number(isoCur);
		SAFE(gf_isom_write(isoCur));
	}

	auto out = output->allocData<DataRaw>(0);
	if (gf_isom_get_filename(isoCur)) {
		lastSegmentSize = fileSize(segmentName);
	} else {
		auto const newBsNeeded = EOS || ( (compatFlags & FlushFragMemory) && curFragmentDurInTs );
		auto contents = getBsContent(isoCur, newBsNeeded);
		if (!contents.len && !EOS) {
			assert((segmentPolicy == FragmentedSegment) && (fragmentPolicy > NoFragment));
			m_host->log(Debug, "Empty segment. Ignore.");
			return;
		}
		out->buffer->resize(contents.len);
		if(contents.len)
			memcpy(out->buffer->data().ptr, contents.ptr, contents.len);
		gf_free(contents.ptr);
		lastSegmentSize = contents.len;
	}

	StreamType streamType;
	std::string mimeType;
	auto const mediaType = gf_isom_get_media_type(isoCur, gf_isom_get_track_by_id(isoCur, trackId));
	switch (mediaType) {
	case GF_ISOM_MEDIA_VISUAL: streamType = VIDEO_PKT; mimeType = "video/mp4"; break;
	case GF_ISOM_MEDIA_AUDIO: streamType = AUDIO_PKT; mimeType = "audio/mp4"; break;
	case GF_ISOM_MEDIA_TEXT: streamType = SUBTITLE_PKT; mimeType = "application/mp4"; break;
	default: throw error(format("Unknown media type for segment: %s", (int)mediaType));
	}
	Bool isInband = AVC_INBAND_CONFIG ?  GF_TRUE : GF_FALSE;
	char codecName[40];
	GF_Err e = gf_media_get_rfc_6381_codec_name(isoCur, gf_isom_get_track_by_id(isoCur, trackId), codecName, isInband, GF_FALSE);
	if (e)
		throw error("Could not compute codec name (RFC 6381)");

	auto const consideredDurationInTs = (compatFlags & FlushFragMemory) ? curFragmentDurInTs : curSegmentDurInTs;
	auto const consideredDurationIn180k = timescaleToClock(consideredDurationInTs, timeScale);
	auto const containerLatency =
	    fragmentPolicy == OneFragmentPerFrame ? timescaleToClock(defaultSampleIncInTs, timeScale) : std::min<uint64_t>(consideredDurationIn180k, fractionToClock(segmentDuration));

	auto metadata = make_shared<MetadataFile>(streamType);
	metadata->filename = segmentName;
	metadata->mimeType = mimeType;
	metadata->codecName = codecName;
	metadata->lang = lang;
	metadata->durationIn180k = consideredDurationIn180k;
	metadata->filesize = lastSegmentSize;
	metadata->latencyIn180k = containerLatency;
	metadata->startsWithRAP = segmentStartsWithRAP;
	metadata->EOS = EOS;

	switch (mediaType) {
	case GF_ISOM_MEDIA_VISUAL: metadata->resolution = resolution; break;
	case GF_ISOM_MEDIA_AUDIO: metadata->sampleRate = sampleRate; break;
	case GF_ISOM_MEDIA_TEXT: break;
	default: throw error(format("Unknown media type for segment: %s", (int)mediaType));
	}

	out->setMetadata(metadata);
	//FIXME: this mediaTime should be a PTS (is currently a DTS)
	//FIXME: this mediaTime is already shifted by the absolute start time (also shifted according the edit lists)
	auto curSegmentStartInTs = m_DTS - curSegmentDurInTs;
	if (!(compatFlags & SegNumStartsAtZero)) {
		curSegmentStartInTs += rescale(firstDataAbsTimeInMs, 1000, timeScale);
	}
	out->setMediaTime(curSegmentStartInTs, timeScale);
	output->post(out);

	if (segmentPolicy == IndependentSegment) {
		gf_isom_delete(isoCur);
		isoCur = nullptr;
	}
}

void GPACMuxMP4::startChunk(gpacpp::IsoSample * const sample) {
	segmentStartsWithRAP = sample->isRap();
	if (segmentPolicy > SingleSegment) {
		const u64 oneSegDurInTs = clockToTimescale(fractionToClock(segmentDuration), timeScale);
		if (oneSegDurInTs * (m_DTS / oneSegDurInTs) == 0) { /*initial delay*/
			curSegmentDeltaInTs = curSegmentDurInTs + curSegmentDeltaInTs - oneSegDurInTs * ((curSegmentDurInTs + curSegmentDeltaInTs) / oneSegDurInTs);
		} else {
			auto const num = (curSegmentDurInTs + curSegmentDeltaInTs) / oneSegDurInTs;
			auto const rem = m_DTS - (num ? num - 1 : 0) * oneSegDurInTs;
			curSegmentDeltaInTs = m_DTS - oneSegDurInTs * (rem / oneSegDurInTs);
		}
		if (segmentPolicy == IndependentSegment) {
			sample->DTS = 0;
		}
	}
	if (fragmentPolicy > NoFragment) {
		startFragment(sample->DTS, sample->DTS + sample->CTS_Offset);
	}
}

void GPACMuxMP4::addData(gpacpp::IsoSample const * const sample, int64_t lastDataDurationInTs) {
	if (fragmentPolicy > NoFragment) {
		if (curFragmentDurInTs && (fragmentPolicy == OneFragmentPerRAP) && (sample->isRap())) {
			closeFragment();
			startFragment(sample->DTS, sample->DTS + sample->CTS_Offset);
		}
		if (curSegmentDurInTs && (fragmentPolicy == OneFragmentPerFrame)) {
			startFragment(sample->DTS, sample->DTS + sample->CTS_Offset);
		}

		SAFE(gf_isom_fragment_add_sample(isoCur, trackId, sample, 1, (u32)lastDataDurationInTs, 0, 0, GF_FALSE));
		curFragmentDurInTs += lastDataDurationInTs;

		if (fragmentPolicy == OneFragmentPerFrame) {
			closeFragment();
		}
	} else {
		SAFE(gf_isom_add_sample(isoCur, trackId, 1, sample));
	}

	m_DTS += lastDataDurationInTs;
	if (segmentPolicy > SingleSegment) {
		curSegmentDurInTs += lastDataDurationInTs;
	}
}

void GPACMuxMP4::closeChunk(bool nextSampleIsRAP) {
	if (segmentPolicy <= SingleSegment)
		return;

	auto chunkBoundaryAllowedHere = nextSampleIsRAP || (compatFlags & SegmentAtAny);
	auto segmentNextDuration = Fraction(curSegmentDurInTs + curSegmentDeltaInTs, timeScale);

	if ((!(compatFlags & Browsers) || curFragmentDurInTs > 0 || fragmentPolicy == OneFragmentPerFrame) && /*avoid 0-sized mdat interpreted as EOS in browsers*/
	    segmentNextDuration >= segmentDuration &&
	    chunkBoundaryAllowedHere) {
		if ((compatFlags & SegConstantDur) && (segmentNextDuration != segmentDuration) && (curSegmentDurInTs != 0)) {
			if ((m_DTS / clockToTimescale(fractionToClock(segmentDuration), timeScale)) <= 1) {
				segmentDuration = segmentNextDuration;
			}
		}
		closeSegment(false);
		segmentNum++;
		startSegment();
	}
}

static bool isRap(Data data) {
	return data->get<CueFlags>().keyframe;
}

void GPACMuxMP4::processSample(Data data, int64_t lastDataDurationInTs) {
	auto rap = isRap(data);
	closeChunk(rap);
	{
		gpacpp::IsoSample sample {};
		fillSample(data, &sample, rap);

		if (curSegmentDurInTs == 0)
			startChunk(&sample);

		addData(&sample, lastDataDurationInTs);
	}
	closeChunk(false); //close it now if possible, otherwise wait for the next sample to be available
}

void GPACMuxMP4::fillSample(Data data, gpacpp::IsoSample* sample, bool isRap) {
	const u32 mediaType = gf_isom_get_media_type(isoCur, gf_isom_get_track_by_id(isoCur, trackId));
	if (mediaType == GF_ISOM_MEDIA_VISUAL || mediaType == GF_ISOM_MEDIA_AUDIO || mediaType == GF_ISOM_MEDIA_TEXT) {
		if (isAnnexB) {
			annexbToAvcc(data->data(), *sample);
		} else {
			sample->data = (char*)data->data().ptr;
			sample->dataLength = (u32)data->data().len;
			sample->setDataOwnership(false);
		}
	} else
		throw error("Only audio, video or text supported");

	if (segmentPolicy == IndependentSegment) {
		sample->DTS = curSegmentDurInTs + curSegmentDeltaInTs;
	} else {
		sample->DTS = m_DTS;
	}

	auto srcTimeScale = safe_cast<const MetadataPkt>(data->getMetadata())->timeScale;

	if (data->get<PresentationTime>().time != INT64_MAX) {
		auto const ctsOffset = data->get<PresentationTime>().time - data->get<DecodingTime>().time;
		sample->CTS_Offset = clockToTimescale(ctsOffset, timeScale);
	} else {
		m_host->log(Error, format("Missing PTS (input DTS=%s, ts=%s/%s): output MP4 may be incorrect.", data->get<DecodingTime>().time, srcTimeScale.num, srcTimeScale.den).c_str());
	}
	sample->IsRAP = isRap ? RAP : RAP_NO;

	if(sample->CTS_Offset < 0)
		throw error("Negative CTS offset is not supported");
}

void GPACMuxMP4::updateFormat(Data data) {
	if (!defaultSampleIncInTs) {
		m_host->log(Warning, "Computed defaultSampleIncInTs=0, forcing the ExactInputDur flag.");
		compatFlags = compatFlags | ExactInputDur;
	}

	if (!firstDataAbsTimeInMs) {
		firstDataAbsTimeInMs = clockToTimescale(m_utcStartTime->query(), 1000);
		initDTSIn180k = data->get<DecodingTime>().time;
		handleInitialTimeOffset();
	}

	setupFragments();
	if ((segmentDuration != 0) && !(compatFlags & SegNumStartsAtZero)) {
		segmentNum = firstDataAbsTimeInMs / clockToTimescale(fractionToClock(segmentDuration), 1000);
	}
	startSegment();
}

void GPACMuxMP4::processOne(Data data) {
	if(isDeclaration(data))
		return;

	auto const updated = inputs[0]->updateMetadata(data);

	if(updated)
		declareStream(data->getMetadata().get());

	if(updated)
		updateFormat(data);

	auto const dataDTS = data->get<DecodingTime>().time;
	auto dataDurationInTs = clockToTimescale(dataDTS - initDTSIn180k, timeScale) - m_DTS;

	if (compatFlags & ExactInputDur) {
		if (lastData) {
			if (dataDurationInTs <= 0) {
				m_host->log(Warning, format("Computed duration is inferior or equal to zero (%s). Inferring to %s", dataDurationInTs, defaultSampleIncInTs).c_str());
				dataDurationInTs = defaultSampleIncInTs;
			}
			processSample(lastData, dataDurationInTs);
		}

		lastData = data;
	} else {
		auto lastDataDurationInTs = dataDurationInTs + defaultSampleIncInTs;
		if (m_DTS > 0) {
			if (!dataDTS) {
				lastDataDurationInTs = defaultSampleIncInTs;
				m_host->log(Warning, format("Received time 0: inferring duration of %s", lastDataDurationInTs).c_str());
			}
			if (lastDataDurationInTs != defaultSampleIncInTs) {
				lastDataDurationInTs = std::max<int64_t>(lastDataDurationInTs, 1);
				m_host->log(Debug, format("VFR: adding sample with duration %ss", lastDataDurationInTs / (double)timeScale).c_str());
			}
		}

		processSample(data, lastDataDurationInTs);
	}
}

}
}

namespace {

using namespace Modules;

IModule* createObject(KHost* host, void* va) {
	auto config = (Mp4MuxConfig*)va;
	enforce(host, "GPACMuxMP4: host can't be NULL");
	enforce(config, "GPACMuxMP4: config can't be NULL");
	return createModule<Mux::GPACMuxMP4>(host, *config).release();
}

auto const registered = Factory::registerModule("GPACMuxMP4", &createObject);
}
