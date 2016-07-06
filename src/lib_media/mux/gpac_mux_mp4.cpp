#include "gpac_mux_mp4.hpp"
#include "lib_utils/tools.hpp"

extern "C" {
#include <gpac/constants.h>
#include <gpac/isomedia.h>
#include <gpac/internal/media_dev.h>
}

#include "lib_gpacpp/gpacpp.hpp"
#include "lib_ffpp/ffpp.hpp"
#include "../common/libav.hpp"

//#define AVC_INBAND_CONFIG
#define TIMESCALE_MUL 1000
//#define CHROME_DASHJS_2_0_COMPAT

namespace Modules {

namespace {

static GF_Err avc_import_ffextradata(const u8 *extradata, const u64 extradataSize, GF_AVCConfig *dstcfg) {
	u8 nalSize;
	AVCState avc;
	GF_BitStream *bs;
	if (!extradata || !extradataSize) {
		Log::msg(Warning, "No initial SPS/PPS provided.");
		return GF_OK;
	}
	bs = gf_bs_new((const char*)extradata, extradataSize, GF_BITSTREAM_READ);
	if (!bs) {
		return GF_BAD_PARAM;
	}
	if (gf_bs_read_u32(bs) != 0x00000001) {
		gf_bs_del(bs);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	//SPS
	{
		s32 idx;
		char *buffer = nullptr;
		const u64 nalStart = 4;
		nalSize = gf_media_nalu_next_start_code_bs(bs);
		if (nalStart + nalSize > extradataSize) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}
		buffer = (char*)gf_malloc(nalSize);
		gf_bs_read_data(bs, buffer, nalSize);
		gf_bs_seek(bs, nalStart);
		if ((gf_bs_read_u8(bs) & 0x1F) != GF_AVC_NALU_SEQ_PARAM) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		idx = gf_media_avc_read_sps(buffer, nalSize, &avc, 0, nullptr);
		if (idx < 0) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		dstcfg->configurationVersion = 1;
		dstcfg->profile_compatibility = avc.sps[idx].prof_compat;
		dstcfg->AVCProfileIndication = avc.sps[idx].profile_idc;
		dstcfg->AVCLevelIndication = avc.sps[idx].level_idc;
		dstcfg->chroma_format = avc.sps[idx].chroma_format;
		dstcfg->luma_bit_depth = 8 + avc.sps[idx].luma_bit_depth_m8;
		dstcfg->chroma_bit_depth = 8 + avc.sps[idx].chroma_bit_depth_m8;

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
		s32 idx;
		char *buffer = nullptr;
		const u64 nalStart = 4 + nalSize + 4;
		gf_bs_seek(bs, nalStart);
		nalSize = gf_media_nalu_next_start_code_bs(bs);
		if (nalStart + nalSize > extradataSize) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}
		buffer = (char*)gf_malloc(nalSize);
		gf_bs_read_data(bs, buffer, nalSize);
		gf_bs_seek(bs, nalStart);
		if ((gf_bs_read_u8(bs) & 0x1F) != GF_AVC_NALU_PIC_PARAM) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		idx = gf_media_avc_read_pps(buffer, nalSize, &avc);
		if (idx < 0) {
			gf_bs_del(bs);
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

	gf_bs_del(bs);
	return GF_OK;
}

/**
* A function which takes FFmpeg H265 extradata (SPS/PPS) and bring them ready to be pushed to the MP4 muxer.
* @param extradata
* @param extradata_size
* @param dstcfg
* @returns GF_OK is the extradata was parsed and is valid, other values otherwise.
*/
static GF_Err hevc_import_ffextradata(const u8 *extradata, const u64 extradata_size, GF_HEVCConfig *dstCfg) {
	HEVCState hevc;
	GF_HEVCParamArray *vpss = nullptr, *spss = nullptr, *ppss = nullptr;
	GF_BitStream *bs;
	char *buffer = nullptr;
	u32 bufferSize = 0;
	if (!extradata || (extradata_size < sizeof(u32)))
		return GF_BAD_PARAM;
	bs = gf_bs_new((const char*)extradata, extradata_size, GF_BITSTREAM_READ);
	if (!bs)
		return GF_BAD_PARAM;

	memset(&hevc, 0, sizeof(HEVCState));
	hevc.sps_active_idx = -1;

	while (gf_bs_available(bs)) {
		s32 idx;
		GF_AVCConfigSlot *slc;
		u8 NALUnitType, temporalId, layerId;
		u64 NALStart;
		u32 NALSize;

		if (gf_bs_read_u32(bs) != 0x00000001) {
			gf_bs_del(bs);
			return GF_NON_COMPLIANT_BITSTREAM;
		}
		NALStart = gf_bs_get_position(bs);
		NALSize = gf_media_nalu_next_start_code_bs(bs);
		if (NALStart + NALSize > extradata_size) {
			gf_bs_del(bs);
			return GF_BAD_PARAM;
		}

		if (NALSize > bufferSize) {
			buffer = (char*)gf_realloc(buffer, NALSize);
			bufferSize = NALSize;
		}
		gf_bs_read_data(bs, buffer, NALSize);
		gf_bs_seek(bs, NALStart);

		gf_media_hevc_parse_nalu(bs, &hevc, &NALUnitType, &temporalId, &layerId);
		if (layerId) {
			gf_bs_del(bs);
			gf_free(buffer);
			return GF_BAD_PARAM;
		}

		switch (NALUnitType) {
		case GF_HEVC_NALU_VID_PARAM:
			idx = gf_media_hevc_read_vps(buffer, NALSize, &hevc);
			if (idx < 0) {
				gf_bs_del(bs);
				gf_free(buffer);
				return GF_BAD_PARAM;
			}

			assert(hevc.vps[idx].state == 1); //we don't expect multiple VPS
			if (hevc.vps[idx].state == 1) {
				hevc.vps[idx].state = 2;
				hevc.vps[idx].crc = gf_crc_32(buffer, NALSize);

				dstCfg->avgFrameRate = hevc.vps[idx].rates[0].avg_pic_rate;
				dstCfg->constantFrameRate = hevc.vps[idx].rates[0].constand_pic_rate_idc;
				dstCfg->numTemporalLayers = hevc.vps[idx].max_sub_layers;
				dstCfg->temporalIdNested = hevc.vps[idx].temporal_id_nesting;

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
		case GF_HEVC_NALU_SEQ_PARAM:
			idx = gf_media_hevc_read_sps(buffer, NALSize, &hevc);
			if (idx < 0) {
				gf_bs_del(bs);
				gf_free(buffer);
				return GF_BAD_PARAM;
			}

			assert(!(hevc.sps[idx].state & AVC_SPS_DECLARED)); //we don't expect multiple SPS
			if ((hevc.sps[idx].state & AVC_SPS_PARSED) && !(hevc.sps[idx].state & AVC_SPS_DECLARED)) {
				hevc.sps[idx].state |= AVC_SPS_DECLARED;
				hevc.sps[idx].crc = gf_crc_32(buffer, NALSize);
			}

			dstCfg->configurationVersion = 1;
			dstCfg->profile_space = hevc.sps[idx].ptl.profile_space;
			dstCfg->tier_flag = hevc.sps[idx].ptl.tier_flag;
			dstCfg->profile_idc = hevc.sps[idx].ptl.profile_idc;
			dstCfg->general_profile_compatibility_flags = hevc.sps[idx].ptl.profile_compatibility_flag;
			dstCfg->progressive_source_flag = hevc.sps[idx].ptl.general_progressive_source_flag;
			dstCfg->interlaced_source_flag = hevc.sps[idx].ptl.general_interlaced_source_flag;
			dstCfg->non_packed_constraint_flag = hevc.sps[idx].ptl.general_non_packed_constraint_flag;
			dstCfg->frame_only_constraint_flag = hevc.sps[idx].ptl.general_frame_only_constraint_flag;

			dstCfg->constraint_indicator_flags = hevc.sps[idx].ptl.general_reserved_44bits;
			dstCfg->level_idc = hevc.sps[idx].ptl.level_idc;

			dstCfg->chromaFormat = hevc.sps[idx].chroma_format_idc;
			dstCfg->luma_bit_depth = hevc.sps[idx].bit_depth_luma;
			dstCfg->chroma_bit_depth = hevc.sps[idx].bit_depth_chroma;

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
		case GF_HEVC_NALU_PIC_PARAM:
			idx = gf_media_hevc_read_pps(buffer, NALSize, &hevc);
			if (idx < 0) {
				gf_bs_del(bs);
				gf_free(buffer);
				return GF_BAD_PARAM;
			}

			assert(hevc.pps[idx].state == 1); //we don't expect multiple PPS
			if (hevc.pps[idx].state == 1) {
				hevc.pps[idx].state = 2;
				hevc.pps[idx].crc = gf_crc_32(buffer, NALSize);

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

void fillVideoSampleData(const u8 *bufPtr, u32 bufLen, GF_ISOSample &sample) {
	u32 scSize = 0, NALUSize = 0;
	GF_BitStream *out_bs = gf_bs_new(nullptr, 2 * bufLen, GF_BITSTREAM_WRITE);
	NALUSize = gf_media_nalu_next_start_code(bufPtr, bufLen, &scSize);
	if (NALUSize != 0) {
		gf_bs_write_u32(out_bs, NALUSize);
		gf_bs_write_data(out_bs, (const char*)bufPtr, NALUSize);
	}
	if (scSize) {
		bufPtr += (NALUSize + scSize);
		bufLen -= (NALUSize + scSize);
	}

	while (bufLen) {
		NALUSize = gf_media_nalu_next_start_code(bufPtr, bufLen, &scSize);
		if (NALUSize != 0) {
			gf_bs_write_u32(out_bs, NALUSize);
			gf_bs_write_data(out_bs, (const char*)bufPtr, NALUSize);
		}

		bufPtr += NALUSize;

		if (!scSize || (bufLen < NALUSize + scSize))
			break;
		bufLen -= NALUSize + scSize;
		bufPtr += scSize;
	}
	gf_bs_get_content(out_bs, &sample.data, &sample.dataLength);
	gf_bs_del(out_bs);
}
}

namespace Mux {

GPACMuxMP4::GPACMuxMP4(const std::string &baseName, uint64_t chunkDurationInMs, bool useSegments)
	: m_useFragments(useSegments),
	  m_useSegments(useSegments), m_chunkDuration(timescaleToClock(chunkDurationInMs, 1000)) {
	if (m_chunkDuration == 0) {
		log(Debug, "Configuration: single file.");
		assert(!useSegments);
	} else {
		if (useSegments)
			log(Debug, "Configuration: segmented.");
		else
			log(Info, "Configuration: chunks (independent ISOBMF files, not segmented).");
	}

	std::stringstream fileName;
	fileName << baseName;
	fileName << ".mp4";

	if (baseName == "") {
		throw error("Unsupported memory output"); //open in memory - apparently we have to use the gmem:// protocol
	} else {
		m_iso = gf_isom_open(fileName.str().c_str(), GF_ISOM_OPEN_WRITE, nullptr);
		if (!m_iso)
			throw error(format("Cannot open iso file %s"));
	}

	GF_Err e = gf_isom_set_storage_mode(m_iso, GF_ISOM_STORE_INTERLEAVED);
	if (e != GF_OK)
		throw error(format("Cannot make iso file %s interleaved", fileName.str()));

	output = addOutput<OutputDataDefault<DataAVPacket>>();
}

void GPACMuxMP4::closeSegment(bool isLastSeg) {
	gf_isom_flush_fragments(m_iso, (Bool)isLastSeg);

	if (m_useSegments) {
#ifdef CHROME_DASHJS_2_0_COMPAT
		GF_Err e = gf_isom_close_segment(m_iso, 0, 0, 0, 0, 0, GF_FALSE, (Bool)isLastSeg, 0, nullptr, nullptr);
#else
		GF_Err e = gf_isom_close_segment(m_iso, 0, 0, 0, 0, 0, GF_FALSE, (Bool)isLastSeg, GF_4CC('e', 'o', 'd', 's'), nullptr, nullptr);
#endif
		if (e != GF_OK)
			throw error(format("%s: gf_isom_close_segment", gf_error_to_string(e)));

		m_lastChunkSize = gf_isom_get_file_size(m_iso);

		sendOutput();
		log(Info, "Segment %s completed (size %s) (startsWithSAP=%s)", m_chunkName, m_lastChunkSize, m_chunkStartsWithRAP);
	}
}

void GPACMuxMP4::flush() {
	if (m_useFragments) {
		gf_isom_flush_fragments(m_iso, GF_TRUE);
	}
	closeSegment(true);
}

GPACMuxMP4::~GPACMuxMP4() {
	GF_Err e = gf_isom_close(m_iso);
	if (e != GF_OK && e != GF_ISOM_INVALID_FILE)
		throw error(format("%s: gf_isom_close", gf_error_to_string(e)));
}

void GPACMuxMP4::setupFragments() {
	GF_Err e;

	if (m_useFragments) {
		e = gf_isom_setup_track_fragment(m_iso, m_trackId, 1, 1, 0, 0, 0, 0);
		if (e != GF_OK)
			throw error(format("Cannot setup track as fragmented: %s", gf_error_to_string(e)));
	}

	//gf_isom_add_track_to_root_od(video_output_file->isof, 1);

	if (m_useFragments) {
		if (m_useSegments) {
			e = gf_isom_finalize_for_fragment(m_iso, 1);
			if (e != GF_OK)
				throw error(format("Cannot prepare track for movie fragmentation: %s", gf_error_to_string(e)));

			std::stringstream ss;
			ss << gf_isom_get_filename(m_iso) << "_" << m_chunkNum+1;
			m_chunkName = ss.str();
			e = gf_isom_start_segment(m_iso, (char*)m_chunkName.c_str(), GF_TRUE);
			if (e != GF_OK)
				throw error(format("Impossible to start segment %s (%s): %s", m_chunkNum, m_chunkName, gf_error_to_string(e)));
		} else {
			e = gf_isom_finalize_for_fragment(m_iso, 0);
			if (e != GF_OK)
				throw error(format("Cannot prepare track for movie fragmentation: %s", gf_error_to_string(e)));
		}

		e = gf_isom_start_fragment(m_iso, GF_TRUE);
		if (e != GF_OK)
			throw error(format("Impossible to create the moof: %s", gf_error_to_string(e)));

		e = gf_isom_set_traf_base_media_decode_time(m_iso, m_trackId, m_DTS);
		if (e != GF_OK)
			throw error(format("Impossible to create TFDT %s: %s", gf_net_get_ntp_ts(), gf_error_to_string(e)));

#ifndef CHROME_DASHJS_2_0_COMPAT
		e = gf_isom_set_fragment_reference_time(m_iso, m_trackId, gf_net_get_ntp_ts(), 0);
		if (e != GF_OK)
			throw error(format("Impossible to create UTC marquer: %s", gf_error_to_string(e)));
#endif
	}
}

void GPACMuxMP4::declareStreamAudio(std::shared_ptr<const MetadataPktLibavAudio> metadata) {
	GF_Err e;
	u32 di, trackNum;
	GF_M4ADecSpecInfo acfg;

	GF_ESD *esd = gf_odf_desc_esd_new(2);
	if (!esd)
		throw error(format("Cannot create GF_ESD"));

	esd->decoderConfig = (GF_DecoderConfig *)gf_odf_desc_new(GF_ODF_DCD_TAG);
	esd->slConfig = (GF_SLConfig *)gf_odf_desc_new(GF_ODF_SLC_TAG);
	esd->decoderConfig->streamType = GF_STREAM_AUDIO;
	if (metadata->getCodecName() == "aac") { //TODO: find an automatic table, we only know about MPEG1 Layer 2 and AAC-LC
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_AAC_MPEG4;

		esd->decoderConfig->bufferSizeDB = 20;
		esd->slConfig->timestampResolution = metadata->getSampleRate();
		esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *)gf_odf_desc_new(GF_ODF_DSI_TAG);
		esd->ESID = 1;

		const uint8_t *extradata;
		size_t extradataSize;
		metadata->getExtradata(extradata, extradataSize);
		esd->decoderConfig->decoderSpecificInfo->dataLength = (u32)extradataSize;
		esd->decoderConfig->decoderSpecificInfo->data = (char*)gf_malloc(extradataSize);
		memcpy(esd->decoderConfig->decoderSpecificInfo->data, extradata, extradataSize);

		memset(&acfg, 0, sizeof(GF_M4ADecSpecInfo));
		acfg.base_object_type = GF_M4A_AAC_LC;
		acfg.base_sr = metadata->getSampleRate();
		acfg.nb_chan = metadata->getNumChannels();
		acfg.sbr_object_type = 0;
		acfg.audioPL = gf_m4a_get_profile(&acfg);

		/*e = gf_m4a_write_config(&acfg, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		assert(e == GF_OK);*/
	} else {
		if (metadata->getCodecName() != "mp2") {
			log(Warning, "Unlisted codec, setting GPAC_OTI_AUDIO_MPEG1 descriptor.");
		}
		esd->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_MPEG1;
		esd->decoderConfig->bufferSizeDB = 20;
		esd->slConfig->timestampResolution = metadata->getSampleRate();
		esd->decoderConfig->decoderSpecificInfo = (GF_DefaultDescriptor *)gf_odf_desc_new(GF_ODF_DSI_TAG);
		esd->ESID = 1;

		memset(&acfg, 0, sizeof(GF_M4ADecSpecInfo));
		acfg.base_object_type = GF_M4A_LAYER2;
		acfg.base_sr = metadata->getSampleRate();
		acfg.nb_chan = metadata->getNumChannels();
		acfg.sbr_object_type = 0;
		acfg.audioPL = gf_m4a_get_profile(&acfg);

		e = gf_m4a_write_config(&acfg, &esd->decoderConfig->decoderSpecificInfo->data, &esd->decoderConfig->decoderSpecificInfo->dataLength);
		assert(e == GF_OK);
	}

	trackNum = gf_isom_new_track(m_iso, esd->ESID, GF_ISOM_MEDIA_AUDIO, metadata->getSampleRate());
	log(Warning, "TimeScale: %s", metadata->getSampleRate());
	if (!trackNum)
		throw error(format("Cannot create new track"));

	m_trackId = gf_isom_get_track_id(m_iso, trackNum);

	e = gf_isom_set_track_enabled(m_iso, trackNum, GF_TRUE);
	if (e != GF_OK)
		throw error(format("gf_isom_set_track_enabled: %s", gf_error_to_string(e)));

	e = gf_isom_new_mpeg4_description(m_iso, trackNum, esd, nullptr, nullptr, &di);
	if (e != GF_OK)
		throw error(format("gf_isom_new_mpeg4_description: %s", gf_error_to_string(e)));

	gf_odf_desc_del((GF_Descriptor *)esd);
	esd = nullptr;

	e = gf_isom_set_audio_info(m_iso, trackNum, di, metadata->getSampleRate(), metadata->getNumChannels(), metadata->getBitsPerSample());
	if (e != GF_OK)
		throw error(format("gf_isom_set_audio_info: %s", gf_error_to_string(e)));

	e = gf_isom_set_pl_indication(m_iso, GF_ISOM_PL_AUDIO, acfg.audioPL);
	if (e != GF_OK)
		throw error(format("Container format import failed: %s", gf_error_to_string(e)));

	sampleRate = metadata->getSampleRate();

	setupFragments();

	auto input = addInput(new Input<DataAVPacket>(this));
	input->setMetadata(new MetadataPktLibavAudio(metadata->getAVCodecContext()));
}

void GPACMuxMP4::declareStreamVideo(std::shared_ptr<const MetadataPktLibavVideo> metadata) {
	u32 trackNum = gf_isom_new_track(m_iso, 0, GF_ISOM_MEDIA_VISUAL, metadata->getTimeScale() * TIMESCALE_MUL);
	if (!trackNum)
		throw error(format("Cannot create new track"));
	m_trackId = gf_isom_get_track_id(m_iso, trackNum);

	GF_Err e = gf_isom_set_track_enabled(m_iso, trackNum, GF_TRUE);
	if (e != GF_OK)
		throw error(format("Cannot enable track: %s", gf_error_to_string(e)));

	const uint8_t *extradata;
	size_t extradataSize;
	metadata->getExtradata(extradata, extradataSize);

	u32 di;
	if (metadata->getAVCodecContext()->codec_id == AV_CODEC_ID_H264) {
		GF_AVCConfig *avccfg = gf_odf_avc_cfg_new();
		if (!avccfg)
			throw error(format("Container format import failed (AVC)"));

		e = avc_import_ffextradata(extradata, extradataSize, avccfg);
		if (e == GF_OK) {
			e = gf_isom_avc_config_new(m_iso, trackNum, avccfg, nullptr, nullptr, &di);
			if (e != GF_OK)
				throw error(format("Cannot create AVC config: %s", gf_error_to_string(e)));
			gf_odf_avc_cfg_del(avccfg);
		}
	} else if (metadata->getAVCodecContext()->codec_id == AV_CODEC_ID_H265) {
		GF_HEVCConfig *hevccfg = gf_odf_hevc_cfg_new();
		if (!hevccfg)
			throw error(format("Container format import failed (HEVC)"));

		e = hevc_import_ffextradata(extradata, extradataSize, hevccfg);
		if (e == GF_OK) {
			e = gf_isom_hevc_config_new(m_iso, trackNum, hevccfg, nullptr, nullptr, &di);
			if (e != GF_OK)
				throw error(format("Cannot create AVC config: %s", gf_error_to_string(e)));
			gf_odf_hevc_cfg_del(hevccfg);
		}
	} else {
		throw error(format("Unknown codec"));
	}
	if (e) {
		if (e == GF_NON_COMPLIANT_BITSTREAM) {
			/*non Annex B: assume this is AVCC*/
			GF_ESD *esd = (GF_ESD *)gf_odf_desc_esd_new(0);
			esd->ESID = 1; /*FIXME: only one track: set trackID?*/
			esd->decoderConfig->streamType = GF_STREAM_VISUAL;
			esd->decoderConfig->avgBitrate = esd->decoderConfig->maxBitrate = 0;
			esd->decoderConfig->objectTypeIndication = metadata->getAVCodecContext()->codec_id == AV_CODEC_ID_H264 ? GPAC_OTI_VIDEO_AVC : GPAC_OTI_VIDEO_HEVC;
			esd->decoderConfig->decoderSpecificInfo->dataLength = (u32)extradataSize;
			esd->decoderConfig->decoderSpecificInfo->data = (char*)gf_malloc(extradataSize);
			memcpy(esd->decoderConfig->decoderSpecificInfo->data, extradata, extradataSize);
			esd->slConfig->predefined = SLPredef_MP4;

			e = gf_isom_new_mpeg4_description(m_iso, trackNum, esd, nullptr, nullptr, &di);
			if (e != GF_OK)
				throw error(format("Cannot create MPEG-4 config: %s", gf_error_to_string(e)));
			gf_odf_desc_del((GF_Descriptor*)esd);
			isAnnexB = false;
		} else {
			throw error(format("Container format import failed"));
		}
	}

	auto const res = metadata->getResolution();
	gf_isom_set_visual_info(m_iso, trackNum, di, res.width, res.height);
	resolution[0] = res.width;
	resolution[1] = res.height;
	gf_isom_set_sync_table(m_iso, trackNum);

#ifdef AVC_INBAND_CONFIG
	//inband SPS/PPS
	if (m_useSegments) {
		e = gf_isom_avc_set_inband_config(m_iso, trackNum, di);
		if (e != GF_OK)
			throw error(format("Cannot set inband PPS/SPS for AVC track: %s", gf_error_to_string(e)));
	}
#endif

	setupFragments();

	auto input = addInput(new Input<DataAVPacket>(this));
	input->setMetadata(new MetadataPktLibavVideo(metadata->getAVCodecContext()));
}

void GPACMuxMP4::declareStream(Data data) {
	auto const metadata = data->getMetadata();
	if (auto video = std::dynamic_pointer_cast<const MetadataPktLibavVideo>(metadata)) {
		declareStreamVideo(video);
	} else if (auto audio = std::dynamic_pointer_cast<const MetadataPktLibavAudio>(metadata)) {
		declareStreamAudio(audio);
	} else {
		throw error(format("Stream creation failed: unknown type."));
	}
}

void GPACMuxMP4::sendOutput() {
	StreamType streamType;
	std::string mimeType;
	switch (gf_isom_get_media_type(m_iso, gf_isom_get_track_by_id(m_iso, m_trackId))) {
	case GF_ISOM_MEDIA_VISUAL: streamType = VIDEO_PKT; mimeType = "video/mp4"; break;
	case GF_ISOM_MEDIA_AUDIO: streamType = AUDIO_PKT; mimeType = "audio/mp4"; break;
	default: throw error(format("Segment contains neither audio nor video"));
	}
	Bool isInband =
#ifdef AVC_INBAND_CONFIG
	    GF_TRUE;
#else
	    GF_FALSE;
#endif
	char codecName[256]; //FIXME: security issue on the GPAC API
	GF_Err e = gf_media_get_rfc_6381_codec_name(m_iso, gf_isom_get_track_by_id(m_iso, m_trackId), codecName, isInband, GF_FALSE);
	if (e) throw error(format("Could not compute codec name (RFC 6381)"));

	auto out = output->getBuffer(0);
	auto metadata = std::make_shared<MetadataFile>(m_chunkName, streamType, mimeType, gf_strdup(codecName), m_curFragDur, m_lastChunkSize, m_chunkStartsWithRAP);
	out->setMetadata(metadata);
	auto const mediaTimescale = gf_isom_get_media_timescale(m_iso, gf_isom_get_track_by_id(m_iso, m_trackId));
	switch (gf_isom_get_media_type(m_iso, gf_isom_get_track_by_id(m_iso, m_trackId))) {
	case GF_ISOM_MEDIA_VISUAL: metadata->resolution[0] = resolution[0]; metadata->resolution[1] = resolution[1]; break;
	case GF_ISOM_MEDIA_AUDIO: metadata->sampleRate = sampleRate; break;
	default: throw error(format("Segment contains neither audio nor video"));
	}
	out->setTime(m_DTS, mediaTimescale);
	output->emit(out);
}

void GPACMuxMP4::addSample(gpacpp::IsoSample &sample, const uint64_t dataDurationInTs) {
	m_DTS += dataDurationInTs;

	auto const mediaTimescale = gf_isom_get_media_timescale(m_iso, gf_isom_get_track_by_id(m_iso, m_trackId));
	if (m_useFragments) {
		m_curFragDur += dataDurationInTs;

		GF_Err e;
		if ((m_curFragDur * IClock::Rate) > (mediaTimescale * m_chunkDuration)) {
			closeSegment(false);
			if (m_useSegments) {
				m_chunkNum++;
				m_chunkStartsWithRAP = sample.IsRAP == RAP;

				std::stringstream ss;
				ss << gf_isom_get_filename(m_iso) << "_" << m_chunkNum+1;
				m_chunkName = ss.str();
				e = gf_isom_start_segment(m_iso, (char*)m_chunkName.c_str(), GF_TRUE);
				if (e != GF_OK)
					throw error(format("Impossible to start the segment %s (%s): %s", m_chunkNum, m_chunkName, gf_error_to_string(e)));
			}

			e = gf_isom_start_fragment(m_iso, GF_TRUE);
			if (e != GF_OK)
				throw error(format("Impossible to start the fragment: %s", gf_error_to_string(e)));

			e = gf_isom_set_traf_base_media_decode_time(m_iso, m_trackId, sample.DTS);
			if (e != GF_OK)
				throw error(format("Impossible to create TFDT %s: %s", gf_net_get_ntp_ts(), gf_error_to_string(e)));

#ifndef CHROME_DASHJS_2_0_COMPAT
			e = gf_isom_set_fragment_reference_time(m_iso, m_trackId, gf_net_get_ntp_ts(), sample.DTS + sample.CTS_Offset);
			if (e != GF_OK)
				throw error(format("Impossible to set the UTC marquer %s: %s", m_chunkNum, gf_error_to_string(e)));
#endif

			const u64 oneFragDurInTimescale = clockToTimescale(m_chunkDuration, mediaTimescale);
			m_curFragDur = m_DTS - oneFragDurInTimescale * (m_DTS / oneFragDurInTimescale);
		}

		e = gf_isom_fragment_add_sample(m_iso, m_trackId, &sample, 1, (u32)dataDurationInTs, 0, 0, GF_FALSE);
		if (e != GF_OK) {
			log(Error, "%s: gf_isom_fragment_add_sample", gf_error_to_string(e));
			return;
		}
	} else {
		GF_Err e = gf_isom_add_sample(m_iso, m_trackId, 1, &sample);
		if (e != GF_OK) {
			log(Error, "%s: gf_isom_add_sample", gf_error_to_string(e));
			return;
		}
	}
}

void GPACMuxMP4::process() {
	//FIXME: reimplement with multiple inputs
	Data data_ = inputs[0]->pop();
	if (inputs[0]->updateMetadata(data_))
		declareStream(data_);
	auto data = safe_cast<const DataAVPacket>(data_);

	gpacpp::IsoSample sample;

	{
		u32 bufLen = (u32)data->size();
		const u8 *bufPtr = data->data();

		u32 mediaType = gf_isom_get_media_type(m_iso, 1);
		if (mediaType == GF_ISOM_MEDIA_VISUAL) {
			if (isAnnexB) {
				fillVideoSampleData(bufPtr, bufLen, sample);
			} else {
				sample.data = (char*)bufPtr;
				sample.dataLength = bufLen;
				sample.setDataOwnership(false);
			}
		} else if (mediaType == GF_ISOM_MEDIA_AUDIO) {
			sample.data = (char*)bufPtr;
			sample.dataLength = bufLen;
			sample.setDataOwnership(false);
		} else {
			log(Warning, "only audio or video supported yet");
			return;
		}

		sample.DTS = m_DTS;
		sample.IsRAP = (SAPType)(data->getPacket()->flags & AV_PKT_FLAG_KEY);
	}

	auto const mediaTimescale = gf_isom_get_media_timescale(m_iso, gf_isom_get_track_by_id(m_iso, m_trackId));
	int64_t dataDurationInTs = clockToTimescale(data->getTime() - m_lastInputTimeIn180k, mediaTimescale);
	m_lastInputTimeIn180k = data->getTime();
	//TODO: make tests and integrate in a module, see #18
#if 1
	if (dataDurationInTs - m_DTS != 0) {
		/*VFR: computing current sample duration from previous*/
		dataDurationInTs = clockToTimescale(data->getTime(), mediaTimescale) - m_DTS + dataDurationInTs;
		if (dataDurationInTs <= 0) {
			dataDurationInTs = 1;
		}
		log(Debug, "VFR: adding sample with duration %ss", dataDurationInTs / (double)mediaTimescale);
	}
#else
	/*wait to have two samples - FIXME: should be in a separate class + mast segment is never processed (should be in flush())*/
	static std::shared_ptr<const DataAVPacket> lastData = nullptr;
	if (lastData) {
		dataDurationInTs = clockToTimescale(data->getTime()-lastData->getTime(), mediaTimescale);
	} else {
		lastData = data;
		return;
	}
	lastData = data;
#endif
	if (dataDurationInTs == 0) {
		dataDurationInTs = TIMESCALE_MUL;
	}
	addSample(sample, dataDurationInTs);
}

}
}
