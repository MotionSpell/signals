#pragma once

#include <cstdint>
#include <memory>

extern "C" {
#include <gpac/tools.h>
#include <gpac/isomedia.h>
#include <gpac/media_tools.h>
#include <gpac/internal/mpd.h>
}

//#define GPAC_MEM_TRACKER

extern const char *g_version;

namespace gpacpp {

//------------------------------------------------
// GPAC error
//------------------------------------------------
class Error : public std::exception {
	public:
		Error(char const* msg, GF_Err error) throw() :  error_(error), msg_(msg) {
			msg_ += ": ";
			msg_ += gf_error_to_string(error_);
		}

		char const* what() const throw() {
			return msg_.c_str();
		}

		const GF_Err error_;
	private:
		std::string msg_;
};

#ifndef GPAC_DISABLE_ISOM

//------------------------------------------------
// wrapper for GF_ISOSample
//------------------------------------------------
class IsoSample : public GF_ISOSample {
	public:
		IsoSample() {
			data = NULL;
			dataLength = 0;
			DTS = 0;
			CTS_Offset = 0;
			IsRAP = RAP_NO;
			nb_pack = 0;
		}
		IsoSample(GF_ISOSample* pOther) {
			*((GF_ISOSample*)this) = *pOther;
			gf_free(pOther);
		}
		~IsoSample() {
			if (ownsData)
				gf_free(data);
		}
		void setDataOwnership(bool ownsData) {
			this->ownsData = ownsData;
		}

		bool isRap() const {
			return this->IsRAP == RAP;
		}

	private:
		IsoSample const& operator=(IsoSample const&) = delete;
		bool ownsData = true;
};

//------------------------------------------------
// wrapper for GF_ISOFile
//------------------------------------------------
class IsoFile {
	public:
		IsoFile(std::string const& url) {
			u64 missingBytes;
			GF_Err e = gf_isom_open_progressive(url.c_str(), 0, 0, &movie_, &missingBytes);
			if (e != GF_OK && e != GF_ISOM_INCOMPLETE_FILE)
				throw Error("Can't open fragmented MP4 file", e);
		}

		IsoFile(GF_ISOFile* movie) : movie_(movie) {
		}

		~IsoFile() {
			gf_isom_close(movie_);
		}

		IsoFile const& operator=(IsoFile const&) = delete;

		void setSingleMoofMode(bool enable) {
			gf_isom_set_single_moof_mode(movie_, (Bool)enable);
		}

		bool isFragmented() const {
			return gf_isom_is_fragmented(movie_);
		}

		uint64_t refreshFragmented(std::string const& url) {
			uint64_t missingBytes;
			GF_Err e = gf_isom_refresh_fragmented(movie_, &missingBytes, url.c_str());
			if (e != GF_OK && e != GF_ISOM_INCOMPLETE_FILE)
				throw Error("Can't refresh fragmented MP4", e);
			return missingBytes;
		}

		uint32_t getTrackId(int i) const {
			return gf_isom_get_track_id(movie_, i);
		}

		uint32_t getTrackById(uint32_t id) const {
			auto const number = gf_isom_get_track_by_id(movie_, id);
			if (number == 0)
				throw Error("Can't find track ID", GF_BAD_PARAM);
			return number;
		}

		uint32_t getSampleCount(int trackNumber) const {
			return gf_isom_get_sample_count(movie_, trackNumber);
		}

		std::unique_ptr<IsoSample> getSample(int trackNumber, int sampleIndex, int& descriptorIndex) const {
			GF_ISOSample* sample = gf_isom_get_sample(movie_, trackNumber, sampleIndex, (u32*)&descriptorIndex);
			if (!sample) {
				auto msg = "Sample with index " + std::to_string(sampleIndex) + " does not exist (trackNumber=" + std::to_string(trackNumber) + ")";
				throw Error(msg.c_str(), gf_isom_last_error(movie_));
			}
			return std::unique_ptr<IsoSample>(new IsoSample(sample));
		}

		uint32_t getMediaTimescale(int trackNumber) const {
			return gf_isom_get_media_timescale(movie_, trackNumber);
		}

		int64_t getDTSOffset(int trackNumber) const {
			s64 DTSOffset = 0;
			gf_isom_get_edit_list_type(movie_, trackNumber, &DTSOffset);
			return DTSOffset;
		}

		std::shared_ptr<GF_DecoderConfig> getDecoderConfig(int trackHandle, int descriptorIndex) const {
			return std::shared_ptr<GF_DecoderConfig>(
			        gf_isom_get_decoder_config(movie_, trackHandle, descriptorIndex),
			        &freeDescriptor
			    );
		}

		static void freeDescriptor(GF_DecoderConfig* desc) {
			gf_odf_desc_del((GF_Descriptor*)desc);
		}

		void resetTables(bool flag) {
			gf_isom_reset_tables(movie_, (Bool)flag);
		}

		void resetDataOffset(uint64_t& newBufferStart) {
			GF_Err e = gf_isom_reset_data_offset(movie_, &newBufferStart);
			if (e != GF_OK)
				throw gpacpp::Error("Could not release data", e);
		}

		uint64_t getMissingBytes(int trackNumber) {
			return gf_isom_get_missing_bytes(movie_, trackNumber);
		}

		GF_Err lastError() const {
			return gf_isom_last_error(movie_);
		}

	private:
		GF_ISOFile* movie_;
};

#endif /*GPAC_DISABLE_ISOM*/

}
