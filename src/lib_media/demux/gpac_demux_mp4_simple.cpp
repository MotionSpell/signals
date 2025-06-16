#include "gpac_demux_mp4_simple.hpp"
#include "lib_utils/tools.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/format.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "../common/gpacpp.hpp"

using namespace Modules;

namespace {

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


class GPACDemuxMP4Simple : public Module {
	public:
		GPACDemuxMP4Simple(KHost* host, Mp4DemuxConfig const* cfg)
			: m_host(host),
			  reader(new ISOFileReader) {

			m_host->activate(true);

			GF_ISOFile *movie;
			u64 missingBytes;
			GF_Err e = gf_isom_open_progressive(cfg->path.c_str(), 0, 0, &movie, &missingBytes);
			if ((e != GF_OK && e != GF_ISOM_INCOMPLETE_FILE) || movie == nullptr) {
				throw error(format("Could not open file %s for reading (%s).", cfg->path, gf_error_to_string(e)));
			}
			reader->init(movie);
			output = addOutput();
		}

		void process() override {
			auto const DTSOffset = reader->movie->getDTSOffset(reader->trackNumber);
			try {
				int sampleDescriptionIndex;
				std::unique_ptr<gpacpp::IsoSample> ISOSample = reader->movie->getSample(reader->trackNumber, reader->sampleIndex, sampleDescriptionIndex);

				m_host->log(Debug, format("Found sample #%s/%s of length %s, RAP %s, DTS: %s, CTS: %s",
				        reader->sampleIndex, reader->sampleCount, ISOSample->dataLength,
				        ISOSample->IsRAP, ISOSample->DTS + DTSOffset, ISOSample->DTS + DTSOffset + ISOSample->CTS_Offset).c_str());
				reader->sampleIndex++;

				auto out = output->allocData<DataRaw>(ISOSample->dataLength);
				memcpy(out->buffer->data().ptr, ISOSample->data, ISOSample->dataLength);
				out->setMediaTime(ISOSample->DTS + DTSOffset + ISOSample->CTS_Offset, reader->movie->getMediaTimescale(reader->trackNumber));
				output->post(out);
			} catch (gpacpp::Error const& err) {
				if (err.error_ == GF_ISOM_INCOMPLETE_FILE) {
					u64 missingBytes = reader->movie->getMissingBytes(reader->trackNumber);
					m_host->log(Error, format("Missing %s bytes on input file", missingBytes).c_str());
				} else {
					// no more input data
					m_host->activate(false);
				}
			}
		}

	private:
		KHost* const m_host;
		std::unique_ptr<ISOFileReader> reader;
		OutputDefault* output;
};


IModule* createObject(KHost* host, void* va) {
	auto config = (Mp4DemuxConfig*)va;
	enforce(host, "GPACDemuxMP4Simple: host can't be NULL");
	enforce(config, "GPACDemuxMP4Simple: config can't be NULL");
	return createModule<GPACDemuxMP4Simple>(host, config).release();
}

auto const registered = Factory::registerModule("GPACDemuxMP4Simple", &createObject);
}
