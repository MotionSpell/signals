// holds the chain: [dash downloader] => ( [mp4demuxer] => [restamper] )*
#include "lib_utils/log_sink.hpp"
#include "lib_modules/utils/factory.hpp"
#include "hls_demux.hpp"
#include "lib_media/in/mpeg_dash_input.hpp"
#include "lib_media/out/null.hpp"
#include "lib_media/transform/restamp.hpp"

std::unique_ptr<Modules::In::IFilePuller> createHttpSource();

using namespace Modules;
using namespace In;
using namespace Transform;

namespace {

class HlsDemuxer : public ActiveModule {
	public:
		HlsDemuxer(KHost* host, HlsDemuxConfig* cfg)
			: m_host(host) {
			m_puller = cfg->filePuller;
			if(!m_puller) {
				m_internalPuller = createHttpSource();
				m_puller = m_internalPuller.get();
			}
		}

		virtual bool work() override {
			m_host->log(Error, "Not implemented");
			return true;
		}

	private:
		KHost* const m_host;
		IFilePuller* m_puller;
		std::unique_ptr<IFilePuller> m_internalPuller;
};

Modules::IModule* createObject(KHost* host, void* va) {
	auto config = (HlsDemuxConfig*)va;
	enforce(host, "HlsDemuxer: host can't be NULL");
	enforce(config, "HlsDemuxer: config can't be NULL");
	return Modules::create<HlsDemuxer>(host, config).release();
}

auto const registered = Factory::registerModule("HlsDemuxer", &createObject);
}
