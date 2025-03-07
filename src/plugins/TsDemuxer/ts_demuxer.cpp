// MPEG-TS push demuxer.
//
// The design goal here is to discard the biggest amount of input
// data (which is often erroneous) while still getting the job done.
//
#include "ts_demuxer.hpp"
#include "lib_modules/utils/factory.hpp" // registerModule
#include "lib_modules/utils/helper.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_utils/log_sink.hpp" // Error
#include "lib_utils/format.hpp"
#include "lib_utils/time_unwrapper.hpp"
#include "lib_utils/tools.hpp" // enforce
#include <vector>
using namespace std;

using namespace Modules;

#include "stream.hpp"
#include "pes_stream.hpp"
#include "psi_stream.hpp"

namespace {

auto const PTS_PERIOD = 1LL << 33;
auto const TS_PACKET_LEN = 188;
auto const PID_PAT = 0;

struct TsDemuxer : ModuleS, PsiStream::Listener, PesStream::Restamper {
		TsDemuxer(KHost* host, TsDemuxerConfig const& config)
			: m_host(host) {

			m_unwrapper.WRAP_PERIOD = PTS_PERIOD;

			m_streams.push_back(make_unique<PsiStream>(PID_PAT, m_host, this));

			for(auto& pid : config.pids)
				if(pid.type != TsDemuxerConfig::NONE)
					m_streams.push_back(make_unique<PesStream>(pid.pid, pid.type, this, m_host, addOutput()));
		}

		void processOne(Data data) override {
			auto buf = data->data();

			while(buf.len > 0) {
				if(buf.len < TS_PACKET_LEN) {
					m_host->log(Error, "Truncated TS packet");
					return;
				}

				try {
					processTsPacket({buf.ptr, TS_PACKET_LEN});
				} catch(exception const& e) {
					m_host->log(Error, e.what());
				}
				buf += TS_PACKET_LEN;
			}
		}

		void flush() override {
			for(auto& s : m_streams)
				s->flush();
		}

		// PsiStream::Listener implementation
		void onPat(span<int> pmtPids) override {
			m_host->log(Debug, format("Found PAT (%s programs)", pmtPids.len).c_str());
			for(auto pid : pmtPids)
				m_streams.push_back(make_unique<PsiStream>(pid, m_host, this));
		}

		void onPmt(span<PsiStream::EsInfo> esInfo) override {
			m_host->log(Debug, format("Found PMT (%s streams)", esInfo.len).c_str());
			for(auto es : esInfo) {
				if(auto stream = findMatchingStream(es)) {
					stream->pid = es.pid;
					if(stream->setType(es.mpegStreamType))
						m_host->log(Debug, format("[%s] MPEG stream type %s", es.pid, es.mpegStreamType).c_str());
					else
						m_host->log(Warning, format("[%s] unknown MPEG stream type: %s", es.pid, es.mpegStreamType).c_str());
				}
			}
		}

		// PesStream::Restamper implementation
		void restamp(int64_t& pts) override {

			pts = m_unwrapper.unwrap(pts);

			// make the timestamp start from zero
			{
				if(m_ptsOrigin == INT64_MAX)
					m_ptsOrigin = pts;

				pts -= m_ptsOrigin;
			}
		}

	private:
		void processTsPacket(SpanC pkt) {
			BitReader r = {pkt};
			const int syncByte = r.u(8);
			const int transportErrorIndicator = r.u(1);
			const int payloadUnitStartIndicator = r.u(1);
			/*const int priority =*/ r.u(1);
			const int packetId = r.u(13);
			const int scrambling = r.u(2);
			const int adaptationFieldControl = r.u(2);
			const int continuityCounter = r.u(4);

			if(syncByte != 0x47) {
				m_host->log(Error, "TS sync byte not found");
				return;
			}

			// skip adaptation field if any
			if(adaptationFieldControl & 0b10) {
				auto length = r.u(8);
				skip(r, length, "adaptation_field length in TS header");
			}

			auto stream = findStreamForPid(packetId);
			if(!stream)
				return; // we're not interested in this PID

			if(transportErrorIndicator) {
				m_host->log(Error, "Discarding TS packet with TEI=1");
				return;
			}

			if(scrambling) {
				m_host->log(Error, "Discarding scrambled TS packet");
				return;
			}

			if(continuityCounter == stream->cc)
				return; // discard duplicated packet

			stream->cc = continuityCounter;

			if(payloadUnitStartIndicator)
				stream->flush();

			if(adaptationFieldControl & 0b01) {
				stream->push(r.payload(), payloadUnitStartIndicator);
			}
		}

		PesStream* findMatchingStream(PsiStream::EsInfo es) {
			for(auto& s : m_streams) {
				if(auto stream = dynamic_cast<PesStream*>(s.get()))
					if(matches(stream, es))
						return stream;
			}

			return nullptr;
		}

		static bool matches(PesStream* stream, PsiStream::EsInfo es) {
			if(stream->pid == TsDemuxerConfig::ANY) {
				auto meta = createMetadata(es.mpegStreamType);
				if(!meta)
					return false;
				switch(stream->type) {
				case TsDemuxerConfig::AUDIO: return meta->type == AUDIO_PKT;
				case TsDemuxerConfig::VIDEO: return meta->type == VIDEO_PKT;
				default: // invalid configuration
					assert(0);
					return false;
				}
			} else {
				return stream->pid == es.pid;
			}
		}

		Stream* findStreamForPid(int packetId) {
			for(auto& s : m_streams) {
				if(s->pid == packetId)
					return s.get();
			}
			return nullptr;
		}

		KHost* const m_host;
		vector<unique_ptr<Stream>> m_streams;
		int64_t m_ptsOrigin = INT64_MAX;
		TimeUnwrapper m_unwrapper;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (TsDemuxerConfig*)va;
	enforce(host, "TsDemuxer: host can't be NULL");
	enforce(config, "TsDemuxer: config can't be NULL");
	return createModuleWithSize<TsDemuxer>(384, host, *config).release();
}
}

auto const registered = Factory::registerModule("TsDemuxer", &createObject);

