#include "gpac_mux_mp4.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/time.hpp"
#include "../common/gpacpp.hpp"
#include "../common/ffpp.hpp"
#include "../common/metadata_file.hpp"
#include "lib_modules/utils/factory.hpp"

extern "C" {
#include <gpac/base_coding.h>
#include <gpac/constants.h>
#include <gpac/internal/media_dev.h>
#include <libavcodec/avcodec.h> // AVCodecContext
}

auto const AVC_INBAND_CONFIG = 0;
auto const TIMESCALE_MUL = 100; // offers a tolerance on VFR or faulty streams

namespace Modules {


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
	GF_Err e = gf_isom_get_bs(iso, &bs);
	if (e)
		throw std::runtime_error(format("gf_isom_get_bs: %s", gf_error_to_string(e)));
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

static GF_Err import_extradata_avc(Span extradata, GF_AVCConfig *dstcfg) {
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
		u8 a = gf_bs_read_u8(bs), b = gf_bs_read_u8(bs), c = gf_bs_read_u8(bs);
		if ((a << 16) + (b << 8) + c != 0x000001) {
			u8 d = gf_bs_read_u8(bs);
			if ((a << 24) + (b << 16) + (c << 8) + d != 0x00000001) {
				return GF_NON_COMPLIANT_BITSTREAM;
			}
		}
	}

	//SPS
	u64 nalStart = gf_bs_get_position(bs);
	u8 nalSize = 0;
	{
		s32 idx = 0;
		char *buffer = nullptr;
parse_sps:
		nalSize = gf_media_nalu_next_start_code_bs(bs);
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
			GF_AVCConfigSlot *slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = nalSize;
			slc->id = idx;
			slc->data = buffer;
			gf_list_add(dstcfg->sequenceParameterSets, slc);
		}
	}

	//PPS
	{
		s32 idx = 0;
		char *buffer = nullptr;
		nalStart += 4 + nalSize;
		gf_bs_seek(bs, nalStart);
		nalSize = gf_media_nalu_next_start_code_bs(bs);
		if (nalStart + nalSize > extradata.len) {
			return GF_BAD_PARAM;
		}
		buffer = (char*)gf_malloc(nalSize);
		gf_bs_read_data(bs, buffer, nalSize);
		gf_bs_seek(bs, nalStart);
		if ((gf_bs_read_u8(bs) & 0x1F) != GF_AVC_NALU_PIC_PARAM) {
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		idx = gf_media_avc_read_pps(buffer, nalSize, avc.get());
		if (idx < 0) {
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		{
			GF_AVCConfigSlot *slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
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
static GF_Err import_extradata_hevc(Span extradata, GF_HEVCConfig *dstCfg) {
	GF_HEVCParamArray *vpss = nullptr, *spss = nullptr, *ppss = nullptr;

	char *buffer = nullptr;
	u32 bufferSize = 0;
	if (!extradata.ptr || (extradata.len < sizeof(u32)))
		return GF_BAD_PARAM;
	auto bs = gf_bs_new((const char*)extradata.ptr, extradata.len, GF_BITSTREAM_READ);
	if (!bs)
		return GF_BAD_PARAM;

	auto hevc = make_unique<HEVCState>();
	memset(hevc.get(), 0, sizeof(HEVCState));
	hevc->sps_active_idx = -1;

	while (gf_bs_available(bs)) {
		GF_AVCConfigSlot *slc = nullptr;
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

		if (NALSize > bufferSize) {
			buffer = (char*)gf_realloc(buffer, NALSize);
			bufferSize = NALSize;
		}
		gf_bs_read_data(bs, buffer, NALSize);
		gf_bs_seek(bs, NALStart);

		u8 NALUnitType, temporalId, layerId;
		gf_media_hevc_parse_nalu(buffer, NALSize, hevc.get(), &NALUnitType, &temporalId, &layerId);
		if (layerId) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		switch (NALUnitType) {
		case GF_HEVC_NALU_VID_PARAM: {
			auto const idx = gf_media_hevc_read_vps(buffer, NALSize, hevc.get());
			if (idx < 0) {
				gf_bs_del(bs);
				gf_free(buffer);
				return GF_BAD_PARAM;
			}

			assert(hevc->vps[idx].state == 1); //we don't expect multiple VPS
			if (hevc->vps[idx].state == 1) {
				hevc->vps[idx].state = 2;
				hevc->vps[idx].crc = gf_crc_32(buffer, NALSize);

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

				slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
				slc->size = NALSize;
				slc->id = idx;
				slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
				memcpy(slc->data, buffer, sizeof(char)*slc->size);

				gf_list_add(vpss->nalus, slc);
			}
			break;
		}
		case GF_HEVC_NALU_SEQ_PARAM: {
			auto const idx = gf_media_hevc_read_sps(buffer, NALSize, hevc.get());
			if (idx < 0) {
				gf_bs_del(bs);
				gf_free(buffer);
				return GF_BAD_PARAM;
			}

			assert(!(hevc->sps[idx].state & AVC_SPS_DECLARED)); //we don't expect multiple SPS
			if ((hevc->sps[idx].state & AVC_SPS_PARSED) && !(hevc->sps[idx].state & AVC_SPS_DECLARED)) {
				hevc->sps[idx].state |= AVC_SPS_DECLARED;
				hevc->sps[idx].crc = gf_crc_32(buffer, NALSize);
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

			slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
			slc->size = NALSize;
			slc->id = idx;
			slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
			memcpy(slc->data, buffer, sizeof(char)*slc->size);
			gf_list_add(spss->nalus, slc);
			break;
		}
		case GF_HEVC_NALU_PIC_PARAM: {
			auto const idx = gf_media_hevc_read_pps(buffer, NALSize, hevc.get());
			if (idx < 0) {
				gf_bs_del(bs);
				gf_free(buffer);
				return GF_BAD_PARAM;
			}

			assert(hevc->pps[idx].state == 1); //we don't expect multiple PPS
			if (hevc->pps[idx].state == 1) {
				hevc->pps[idx].state = 2;
				hevc->pps[idx].crc = gf_crc_32(buffer, NALSize);

				if (!ppss) {
					GF_SAFEALLOC(ppss, GF_HEVCParamArray);
					ppss->nalus = gf_list_new();
					gf_list_add(dstCfg->param_array, ppss);
					ppss->array_completeness = 1;
					ppss->type = GF_HEVC_NALU_PIC_PARAM;
				}

				slc = (GF_AVCConfigSlot*)gf_malloc(sizeof(GF_AVCConfigSlot));
				slc->size = NALSize;
				slc->id = idx;
				slc->data = (char*)gf_malloc(sizeof(char)*slc->size);
				memcpy(slc->data, buffer, sizeof(char)*slc->size);

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
	gf_free(buffer);

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

GPACMuxMP4::GPACMuxMP4(IModuleHost* host, Mp4MuxConfig const& cfg)
	: m_host(host),
	  m_utcStartTime(cfg.utcStartTime),
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
		throw error(format("Cannot make iso file %s interleaved", cfg.baseName));

	addInput(this);
	if (compatFlags & FlushFragMemory) {
		output = addOutputDynAlloc<OutputDataDefault<DataRaw>>(100 * ALLOC_NUM_BLOCKS_DEFAULT); //TODO: retrieve framerate, and multiply the allocator size
	} else {
		output = addOutput<OutputDataDefault<DataRaw>>();
	}
}

GPACMuxMP4::~GPACMuxMP4() {
	gf_isom_delete(isoCur);
}

void GPACMuxMP4::flush() {
	if (compatFlags & ExactInputDur) {
		process(lastData);
		lastData = nullptr;
	}
	closeSegment(true);

	if (segmentPolicy == IndependentSegment) {
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
		GF_Err e = gf_isom_start_segment(isoCur, segmentName.empty() ? nullptr : segmentName.c_str(), GF_TRUE);
		if (e != GF_OK)
			throw error(format("Impossible to start segment %s (%s): %s", segmentNum, segmentName, gf_error_to_string(e)));
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
		GF_Err e = gf_isom_close_segment(isoCur, 0, 0, 0, 0, 0, GF_FALSE, (Bool)isLastSeg, (Bool)(!initName.empty()),
		        (compatFlags & Browsers) ? 0 : GF_4CC('e', 'o', 'd', 's'), nullptr, nullptr, &lastSegmentSize);
		if (e != GF_OK) {
			if (m_DTS == 0)
				return;
			throw error(format("gf_isom_close_segment: %s", gf_error_to_string(e)));
		}
	}

	sendOutput(true);
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
				e = gf_isom_set_fragment_option(isoCur, trackId, GF_ISOM_TFHD_FORCE_MOOF_BASE_OFFSET, 1);
				if (e != GF_OK)
					throw error(format("Cannot force the use of moof base offsets: %s", gf_error_to_string(e)));
			} else {
				e = gf_isom_set_traf_base_media_decode_time(isoCur, trackId, DTS);
				if (e != GF_OK)
					throw error(format("Impossible to create TFDT %s: %s", DTS, gf_error_to_string(e)));
			}

			if (!(compatFlags & Browsers)) {
				auto utcPts = firstDataAbsTimeInMs + rescale(PTS, timeScale, 1000);
				e = gf_isom_set_fragment_reference_time(isoCur, trackId, UTC2NTP(utcPts), PTS);
				if (e != GF_OK)
					throw error(format("Impossible to create UTC marquer: %s", gf_error_to_string(e)));
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

		GF_Err e = gf_isom_set_traf_mss_timeext(isoCur, trackId, absTimeInTs, curFragmentDurInTs);
		if (e != GF_OK)
			throw error(format("Impossible to create UTC marker: %s", gf_error_to_string(e)));
	}

	if ((segmentPolicy == FragmentedSegment) || (segmentPolicy == SingleSegment)) {
		GF_Err e = gf_isom_flush_fragments(isoCur, GF_FALSE); //writes a 'styp'
		if (e != GF_OK)
			throw error("Can't flush fragments");

		if (compatFlags & FlushFragMemory) {
			sendOutput(false);
		}
	}

	curFragmentDurInTs = 0;
}

void GPACMuxMP4::setupFragments() {
	if (fragmentPolicy > NoFragment) {
		GF_Err e = gf_isom_setup_track_fragment(isoCur, trackId, 1, compatFlags & SmoothStreaming ? 0 : (u32)defaultSampleIncInTs, 0, 0, 0, 0);
		if (e != GF_OK)
			throw error(format("Cannot setup track as fragmented: %s", gf_error_to_string(e)));

		int mode;

		if (segmentPolicy == NoSegment || segmentPolicy == IndependentSegment)
			mode = 0;
		else if (segmentPolicy == SingleSegment)
			mode = 2;
		else
			mode = 1;

		e = gf_isom_finalize_for_fragment(isoCur, mode); //writes moov
		if (e != GF_OK)
			throw error(format("Cannot prepare track for movie fragmentation: %s", gf_error_to_string(e)));

		if (segmentPolicy == FragmentedSegment) {
			sendOutput(true); //init
		}
	}
}

void GPACMuxMP4::declareStreamAudio(const MetadataPktLibavAudio* metadata) {
	GF_Err e;
	u32 di=0;
	GF_M4ADecSpecInfo acfg {};

	auto deleteEsd = [](GF_ESD* p) {
		gf_odf_desc_del((GF_Descriptor*)p);
	};
	auto esd = std::shared_ptr<GF_ESD>(gf_odf_desc_esd_new(2), deleteEsd);
	if (!esd)
		throw error("Cannot create GF_ESD for audio");

	esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	timeScale = sampleRate = metadata->getSampleRate();
	m_host->log(Debug, format("TimeScale: %s", timeScale).c_str());
	defaultSampleIncInTs = metadata->getFrameSize();

	auto const trackNum = gf_isom_new_track(isoCur, esd->ESID, GF_ISOM_MEDIA_AUDIO, timeScale);
	if (!trackNum)
		throw error("Cannot create new track");

	trackId = gf_isom_get_track_id(isoCur, trackNum);

	esd->ESID = 1;
	if (metadata->getCodecName() == "aac") {
		codec4CC = "AACL";
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_AAC_MPEG4;
		esd->slConfig->timestampResolution = sampleRate;

		acfg.base_object_type = GF_M4A_AAC_LC;
		acfg.base_sr = sampleRate;
		acfg.nb_chan = metadata->getNumChannels();
		acfg.sbr_object_type = 0;
		acfg.audioPL = gf_m4a_get_profile(&acfg);

		e = gf_m4a_write_config(&acfg, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		assert(e == GF_OK);
	} else if (metadata->getCodecName() == "mp2")	{
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_MPEG1;
		esd->decoderConfig->bufferSizeDB = 20;
		esd->slConfig->timestampResolution = sampleRate;
		esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *)gf_odf_desc_new(GF_ODF_DSI_TAG);

		acfg.base_object_type = GF_M4A_LAYER2;
		acfg.base_sr = sampleRate;
		acfg.nb_chan = metadata->getNumChannels();
		acfg.sbr_object_type = 0;
		acfg.audioPL = gf_m4a_get_profile(&acfg);

		e = gf_m4a_write_config(&acfg, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		assert(e == GF_OK);
	} else if (metadata->getCodecName() == "ac3" || metadata->getCodecName() == "eac3") {
		bool is_EAC3 = metadata->getCodecName() == "eac3";

		auto extradata = metadata->getExtradata();
		auto bs2 = std::shared_ptr<GF_BitStream>(gf_bs_new((const char*)extradata.ptr, extradata.len, GF_BITSTREAM_READ), &gf_bs_del);
		auto bs = bs2.get();
		if (!bs)
			throw error(format("(E)AC-3: impossible to create extradata bitstream (\"%s\", size=%s)", metadata->getCodecName(), extradata.len));

		GF_AC3Header hdr  {};
		if (is_EAC3 || !gf_ac3_parser_bs(bs, &hdr, GF_TRUE)) {
			if (!gf_eac3_parser_bs(bs, &hdr, GF_TRUE)) {
				m_host->log(Error, format("Parsing: audio is neither AC3 or E-AC3 audio (\"%s\", size=%s)", metadata->getCodecName(), extradata.len).c_str());
			}
		}

		esd->decoderConfig->objectTypeIndication = is_EAC3 ? GPAC_OTI_AUDIO_EAC3 : GPAC_OTI_AUDIO_AC3;
		esd->decoderConfig->bufferSizeDB = 20;
		esd->slConfig->timestampResolution = sampleRate;

		GF_AC3Config cfg {};
		cfg.is_ec3 = is_EAC3;
		cfg.nb_streams = 1;
		cfg.brcode = hdr.brcode;
		cfg.streams[0].acmod = hdr.acmod;
		cfg.streams[0].bsid = hdr.bsid;
		cfg.streams[0].bsmod = hdr.bsmod;
		cfg.streams[0].fscod = hdr.fscod;
		cfg.streams[0].lfon = hdr.lfon;

		e = gf_isom_ac3_config_new(isoCur, trackNum, &cfg, nullptr, nullptr, &di);
		assert(e == GF_OK);
	} else
		throw error(format("Unsupported audio codec \"%s\"", metadata->getCodecName()));

	e = gf_isom_set_track_enabled(isoCur, trackNum, GF_TRUE);
	if (e != GF_OK)
		throw error(format("gf_isom_set_track_enabled: %s", gf_error_to_string(e)));

	e = gf_isom_new_mpeg4_description(isoCur, trackNum, esd.get(), nullptr, nullptr, &di);
	if (e != GF_OK)
		throw error(format("gf_isom_new_mpeg4_description: %s", gf_error_to_string(e)));

	esd.reset();

	auto const bitsPerSample = metadata->getBitsPerSample() >= 16 ? 16 : metadata->getBitsPerSample();
	e = gf_isom_set_audio_info(isoCur, trackNum, di, sampleRate, metadata->getNumChannels(), bitsPerSample);
	if (e != GF_OK)
		throw error(format("gf_isom_set_audio_info: %s", gf_error_to_string(e)));

	e = gf_isom_set_pl_indication(isoCur, GF_ISOM_PL_AUDIO, acfg.audioPL);
	if (e != GF_OK)
		throw error(format("Container format import failed: %s", gf_error_to_string(e)));

	if (!(compatFlags & SegmentAtAny)) {
		m_host->log(Info, "Audio detected: assuming all segments are RAPs.");
		compatFlags = compatFlags | SegmentAtAny;
	}
}

void GPACMuxMP4::declareStreamSubtitle(const MetadataPktLibavSubtitle* /*metadata*/) {
	timeScale = 10 * TIMESCALE_MUL;
	assert((timeScale % 1000) == 0); /*ms accuracy mandatory*/
	u32 trackNum = gf_isom_new_track(isoCur, 0, GF_ISOM_MEDIA_TEXT, timeScale);
	if (!trackNum)
		throw error("Cannot create new track");
	trackId = gf_isom_get_track_id(isoCur, trackNum);

	GF_Err e = gf_isom_set_track_enabled(isoCur, trackNum, GF_TRUE);
	if (e != GF_OK)
		throw error(format("gf_isom_set_track_enabled: %s", gf_error_to_string(e)));

	u32 di;
	e = gf_isom_new_xml_subtitle_description(isoCur, trackNum, "http://www.w3.org/ns/ttml", NULL, NULL, &di);
	if (e != GF_OK)
		throw error(format("gf_isom_new_xml_subtitle_description: %s", gf_error_to_string(e)));

	codec4CC = "TTML";
	if (!(compatFlags & SegmentAtAny)) {
		m_host->log(Info, "Subtitles detected: assuming all segments are RAPs.");
		compatFlags = compatFlags | SegmentAtAny;
	}
}

void GPACMuxMP4::declareStreamVideo(const MetadataPktLibavVideo* metadata) {
	timeScale = (uint32_t)(metadata->getTimeScale().num * TIMESCALE_MUL);
	u32 trackNum = gf_isom_new_track(isoCur, 0, GF_ISOM_MEDIA_VISUAL, timeScale);
	if (!trackNum)
		throw error("Cannot create new track");
	trackId = gf_isom_get_track_id(isoCur, trackNum);
	defaultSampleIncInTs = metadata->getTimeScale().den * TIMESCALE_MUL;
	resolution = metadata->getResolution();

	GF_Err e = gf_isom_set_track_enabled(isoCur, trackNum, GF_TRUE);
	if (e != GF_OK)
		throw error(format("Cannot enable track: %s", gf_error_to_string(e)));

	isAnnexB = true;
	auto extradata = metadata->getExtradata();

	u32 di = 0;
	if (metadata->getAVCodecContext()->codec_id == AV_CODEC_ID_H264) {
		codec4CC = "H264";
		std::shared_ptr<GF_AVCConfig> avccfg(gf_odf_avc_cfg_new(), &gf_odf_avc_cfg_del);
		if (!avccfg)
			throw error("Container format import failed (AVC)");

		e = import_extradata_avc(extradata, avccfg.get());
		if (e == GF_OK) {
			e = gf_isom_avc_config_new(isoCur, trackNum, avccfg.get(), nullptr, nullptr, &di);
			if (e != GF_OK)
				throw error(format("Cannot create AVC config: %s", gf_error_to_string(e)));
		}
	} else if (metadata->getAVCodecContext()->codec_id == AV_CODEC_ID_H265) {
		codec4CC = "H265";
		std::shared_ptr<GF_HEVCConfig> hevccfg(gf_odf_hevc_cfg_new(), &gf_odf_hevc_cfg_del);
		if (!hevccfg)
			throw error("Container format import failed (HEVC)");

		e = import_extradata_hevc(extradata, hevccfg.get());
		if (e == GF_OK) {
			e = gf_isom_hevc_config_new(isoCur, trackNum, hevccfg.get(), nullptr, nullptr, &di);
			if (e != GF_OK)
				throw error(format("Cannot create HEVC config: %s", gf_error_to_string(e)));
		}
	} else {
		m_host->log(Warning, "Unknown codec: using generic packaging.");
		e = GF_NON_COMPLIANT_BITSTREAM;
	}

	if (e) {
		if (e == GF_NON_COMPLIANT_BITSTREAM) {
			m_host->log(Debug, "Non Annex B: assume this is MP4 already");
			isAnnexB = false;

			GF_ESD *esd = (GF_ESD *)gf_odf_desc_esd_new(0);
			esd->ESID = 1; /*FIXME: only one track: set trackID?*/
			esd->decoderConfig->streamType = GF_STREAM_VISUAL;
			esd->decoderConfig->avgBitrate = esd->decoderConfig->maxBitrate = 0;
			esd->decoderConfig->objectTypeIndication = metadata->getAVCodecContext()->codec_id == AV_CODEC_ID_H264 ? GPAC_OTI_VIDEO_AVC : GPAC_OTI_VIDEO_HEVC;
			esd->decoderConfig->decoderSpecificInfo->dataLength = (u32)extradata.len;
			esd->decoderConfig->decoderSpecificInfo->data = (char*)gf_malloc(extradata.len);
			memcpy(esd->decoderConfig->decoderSpecificInfo->data, extradata.ptr, extradata.len);
			esd->slConfig->predefined = SLPredef_MP4;

			e = gf_isom_new_mpeg4_description(isoCur, trackNum, esd, nullptr, nullptr, &di);
			if (e != GF_OK)
				throw error(format("Cannot create MPEG-4 config: %s", gf_error_to_string(e)));
			gf_odf_desc_del((GF_Descriptor*)esd);
		} else
			throw error("Container format import failed");
	}

	resolution = metadata->getResolution();
	gf_isom_set_visual_info(isoCur, gf_isom_get_track_by_id(isoCur, trackId), di, resolution.width, resolution.height);
	gf_isom_set_sync_table(isoCur, trackNum);

	if(AVC_INBAND_CONFIG) {
		//inband SPS/PPS
		if (segmentPolicy != NoSegment) {
			e = gf_isom_avc_set_inband_config(isoCur, trackNum, di);
			if (e != GF_OK)
				throw error(format("Cannot set inband PPS/SPS for AVC track: %s", gf_error_to_string(e)));
		}
	}
}

void GPACMuxMP4::declareStream(const IMetadata* metadata) {
	if (auto video = dynamic_cast<const MetadataPktLibavVideo*>(metadata)) {
		declareStreamVideo(video);
	} else if (auto audio = dynamic_cast<const MetadataPktLibavAudio*>(metadata)) {
		declareStreamAudio(audio);
	} else if (auto subs = dynamic_cast<const MetadataPktLibavSubtitle*>(metadata)) {
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

void GPACMuxMP4::sendOutput(bool EOS) {
	if (segmentPolicy == IndependentSegment) {
		nextFragmentNum = gf_isom_get_next_moof_number(isoCur);
		GF_Err e = gf_isom_write(isoCur);
		if (e)
			throw error(format("gf_isom_write: %s", gf_error_to_string(e)));
	}

	auto out = output->getBuffer(0);
	if (gf_isom_get_filename(isoCur)) {
		lastSegmentSize = fileSize(segmentName);
	} else {
		auto contents = getBsContent(isoCur, (compatFlags & FlushFragMemory) && curFragmentDurInTs);
		if (!contents.len && !EOS) {
			assert((segmentPolicy == FragmentedSegment) && (fragmentPolicy > NoFragment));
			m_host->log(Debug, "Empty segment. Ignore.");
			return;
		}
		out->resize(contents.len);
		memcpy(out->data().ptr, contents.ptr, contents.len);
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
	auto const curSegmentStartInTs = m_DTS - curSegmentDurInTs;
	out->setMediaTime(rescale(firstDataAbsTimeInMs, 1000, timeScale) + curSegmentStartInTs, timeScale);
	output->emit(out);

	if (segmentPolicy == IndependentSegment) {
		gf_isom_delete(isoCur);
		isoCur = nullptr;
	}
}

void GPACMuxMP4::startChunk(gpacpp::IsoSample * const sample) {
	if (curSegmentDurInTs == 0) {
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

		GF_Err e = gf_isom_fragment_add_sample(isoCur, trackId, sample, 1, (u32)lastDataDurationInTs, 0, 0, GF_FALSE);
		if (e != GF_OK) {
			m_host->log(Error, format("gf_isom_fragment_add_sample: %s", gf_error_to_string(e)).c_str());
			return;
		}
		curFragmentDurInTs += lastDataDurationInTs;

		if (fragmentPolicy == OneFragmentPerFrame) {
			closeFragment();
		}
	} else {
		GF_Err e = gf_isom_add_sample(isoCur, trackId, 1, sample);
		if (e != GF_OK) {
			m_host->log(Error, format("gf_isom_add_sample: %s", gf_error_to_string(e)).c_str());
			return;
		}
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

void GPACMuxMP4::processSample(std::unique_ptr<gpacpp::IsoSample> sample, int64_t lastDataDurationInTs) {
	closeChunk(sample->isRap());
	startChunk(sample.get());
	addData(sample.get(), lastDataDurationInTs);
	closeChunk(false); //close it now if possible, otherwise wait for the next sample to be available
}

std::unique_ptr<gpacpp::IsoSample> GPACMuxMP4::fillSample(Data data_) {
	auto data = safe_cast<const DataAVPacket>(data_);
	auto sample = make_unique<gpacpp::IsoSample>();
	u32 bufLen = (u32)data->data().len;
	const u8 *bufPtr = data->data().ptr;

	const u32 mediaType = gf_isom_get_media_type(isoCur, gf_isom_get_track_by_id(isoCur, trackId));
	if (mediaType == GF_ISOM_MEDIA_VISUAL || mediaType == GF_ISOM_MEDIA_AUDIO || mediaType == GF_ISOM_MEDIA_TEXT) {
		if (isAnnexB) {
			annexbToAvcc({bufPtr, (size_t)bufLen}, *sample);
		} else {
			sample->data = (char*)bufPtr;
			sample->dataLength = bufLen;
			sample->setDataOwnership(false);
		}
	} else
		throw error("Only audio, video or text supported");

	if (segmentPolicy == IndependentSegment) {
		sample->DTS = curSegmentDurInTs + curSegmentDeltaInTs;
	} else {
		sample->DTS = m_DTS;
	}
	auto const &metaPkt = safe_cast<const MetadataPktLibav>(data->getMetadata());

	auto srcTimeScale = metaPkt->getTimeScale();

	if (data->getPacket()->pts != AV_NOPTS_VALUE) {
		auto const ctsOffset = data->getPacket()->pts - data->getPacket()->dts;
		sample->CTS_Offset = (s32)rescale(ctsOffset, srcTimeScale.num, srcTimeScale.den * timeScale);
	} else {
		m_host->log(Error, format("Missing PTS (input DTS=%s, ts=%s/%s): output MP4 may be incorrect.", data->getPacket()->dts, srcTimeScale.num, srcTimeScale.den).c_str());
	}
	sample->IsRAP = (SAPType)(data->getPacket()->flags & AV_PKT_FLAG_KEY);

	if(sample->CTS_Offset < 0)
		throw error("Negative CTS offset is not supported");

	return sample;
}

void GPACMuxMP4::updateFormat(Data data) {
	auto const metadata = safe_cast<const MetadataPktLibav>(data->getMetadata().get());
	declareStream(metadata);

	auto srcTimeScale = metadata->getTimeScale();

	if (!defaultSampleIncInTs) {
		auto pkt = safe_cast<const DataAVPacket>(data);
		if (pkt && pkt->getPacket()->duration) {
			defaultSampleIncInTs = rescale(pkt->getPacket()->duration, srcTimeScale.num, srcTimeScale.den * timeScale);
			m_host->log(Warning, format("Codec defaultSampleIncInTs=0 but first data contains a duration (%s/%s).", defaultSampleIncInTs, timeScale).c_str());
		} else {
			m_host->log(Warning, "Computed defaultSampleIncInTs=0, forcing the ExactInputDur flag.");
			compatFlags = compatFlags | ExactInputDur;
		}
	}

	if (!firstDataAbsTimeInMs) {
		firstDataAbsTimeInMs = clockToTimescale(m_utcStartTime->query(), 1000);
		initDTSIn180k = timescaleToClock(safe_cast<const DataAVPacket>(data)->getPacket()->dts * srcTimeScale.den, srcTimeScale.num);
		handleInitialTimeOffset();
	}

	setupFragments();
	if ((segmentDuration != 0) && !(compatFlags & SegNumStartsAtZero)) {
		segmentNum = firstDataAbsTimeInMs / clockToTimescale(fractionToClock(segmentDuration), 1000);
	}
	startSegment();
}

void GPACMuxMP4::process(Data data) {
	if (inputs[0]->updateMetadata(data)) {
		updateFormat(data);

		auto refData = std::dynamic_pointer_cast<const DataBaseRef>(data);
		if(refData && !refData->getData())
			return;
	}

	auto const srcTimeScale = safe_cast<const MetadataPktLibav>(data->getMetadata())->getTimeScale();
	auto const dataDTS = timescaleToClock(safe_cast<const DataAVPacket>(data)->getPacket()->dts * srcTimeScale.den, srcTimeScale.num);
	if (compatFlags & ExactInputDur) {
		if (lastData) {
			auto dataDurationInTs = clockToTimescale(dataDTS - initDTSIn180k, timeScale) - m_DTS;
			if (dataDurationInTs <= 0) {
				m_host->log(Warning, format("Computed duration is inferior or equal to zero (%s). Inferring to %s", dataDurationInTs, defaultSampleIncInTs).c_str());
				dataDurationInTs = defaultSampleIncInTs;
			}
			processSample(fillSample(lastData), dataDurationInTs);
		}

		lastData = data;
	} else {
		auto lastDataDurationInTs = clockToTimescale(dataDTS - initDTSIn180k, timeScale) + defaultSampleIncInTs - m_DTS;
		if (m_DTS > 0) {
			if (!dataDTS) {
				lastDataDurationInTs = defaultSampleIncInTs;
				m_host->log(Warning, format("Received time 0: inferring duration of %s", lastDataDurationInTs).c_str());
			}
			if (lastDataDurationInTs - defaultSampleIncInTs != 0) {
				if (lastDataDurationInTs <= 0) {
					lastDataDurationInTs = 1;
				}
				m_host->log(Debug, format("VFR: adding sample with duration %ss", lastDataDurationInTs / (double)timeScale).c_str());
			}
		}

		processSample(fillSample(data), lastDataDurationInTs);
	}
}

}
}

namespace {

using namespace Modules;

Modules::IModule* createObject(IModuleHost* host, va_list va) {
	auto config = va_arg(va, Mp4MuxConfig*);
	enforce(host, "GPACMuxMP4: host can't be NULL");
	enforce(config, "GPACMuxMP4: config can't be NULL");
	return Modules::create<Mux::GPACMuxMP4>(host, *config).release();
}

auto const registered = Factory::registerModule("GPACMuxMP4", &createObject);
}
