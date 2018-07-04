#include "gpac_demux_mp4_simple.hpp"
#include "lib_utils/tools.hpp"
#include "lib_gpacpp/gpacpp.hpp"

namespace Modules {
namespace Demux {

class ISOFileReader {
	public:
		void init(GF_ISOFile* m) {
			movie.reset(new gpacpp::IsoFile(m));
			u32 trackId = movie->getTrackId(1); //FIXME should be a parameter? hence not processed in constructor but in a stateful process? or a control module?
			trackNumber = movie->getTrackById(trackId);
			sampleCount = movie->getSampleCount(trackNumber);
			sampleIndex = 1;
		}

		std::unique_ptr<gpacpp::IsoFile> movie;
		uint32_t trackNumber;
		uint32_t sampleIndex, sampleCount;
};

GPACDemuxMP4Simple::GPACDemuxMP4Simple(std::string const& path)
	: reader(new ISOFileReader) {
	GF_ISOFile *movie;
	u64 missingBytes;
	GF_Err e = gf_isom_open_progressive(path.c_str(), 0, 0, &movie, &missingBytes);
	if ((e != GF_OK && e != GF_ISOM_INCOMPLETE_FILE) || movie == nullptr) {
		throw error(format("Could not open file %s for reading (%s).", path, gf_error_to_string(e)));
	}
	reader->init(movie);
	output = addOutput<OutputDefault>();
}

GPACDemuxMP4Simple::~GPACDemuxMP4Simple() {
}

void GPACDemuxMP4Simple::work() {
	auto const DTSOffset = reader->movie->getDTSOffet(reader->trackNumber);
	for (;;) {
		try {
			int sampleDescriptionIndex;
			std::unique_ptr<gpacpp::IsoSample> ISOSample = reader->movie->getSample(reader->trackNumber, reader->sampleIndex, sampleDescriptionIndex);

			log(Debug, "Found sample #%s/%s of length %s, RAP %s, DTS: %s, CTS: %s",
			    reader->sampleIndex, reader->sampleCount, ISOSample->dataLength,
			    ISOSample->IsRAP, ISOSample->DTS + DTSOffset, ISOSample->DTS + DTSOffset + ISOSample->CTS_Offset);
			reader->sampleIndex++;

			auto out = output->getBuffer(ISOSample->dataLength);
			memcpy(out->data(), ISOSample->data, ISOSample->dataLength);
			out->setMediaTime(ISOSample->DTS + DTSOffset + ISOSample->CTS_Offset, reader->movie->getMediaTimescale(reader->trackNumber));
			output->emit(out);
		} catch (gpacpp::Error const& err) {
			if (err.error_ == GF_ISOM_INCOMPLETE_FILE) {
				u64 missingBytes = reader->movie->getMissingBytes(reader->trackNumber);
				log(Error, "Missing %s bytes on input file", missingBytes);
			} else {
				return;
			}
		}
	}
}

}
}
