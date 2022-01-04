// holds the chain: [dash/hls downloader] => ( [mp4/ts demuxer] => [restamper] )*
#include "lib_utils/log_sink.hpp"
#include "lib_modules/utils/factory.hpp"
#include "hls_demux.hpp"
#include "lib_modules/utils/helper.hpp" // ActiveModule
#include "lib_utils/time.hpp" // parseDate
#include "lib_utils/tools.hpp" // enforce
#include <sstream>
#include <memory>
#include <cstring> // memcpy

using namespace std;
using namespace Modules;
using namespace In;

unique_ptr<Modules::In::IFilePuller> createHttpSource();

namespace {

bool startsWith(string s, string prefix) {
	return s.substr(0, prefix.size()) == prefix;
}

string dirName(string path) {
	auto i = path.rfind('/');
	if(i != path.npos)
		path = path.substr(0, i);
	return path + "/";
}

string serverName(string path) {
	auto const prefixLen = startsWith(path, "https://") ? 8 : startsWith(path, "http://") ? 7 : 0 /*assume no prefix*/;
	auto const i = path.substr(prefixLen).find('/');
	if(i != path.npos)
		path = path.substr(0, prefixLen + i);
	return path;
}

class HlsDemuxer : public Module {
	public:
		HlsDemuxer(KHost* host, HlsDemuxConfig* cfg)
			: m_host(host), m_playlistUrl(cfg->url) {

			m_host->activate(true);

			m_puller = cfg->filePuller;
			if(!m_puller) {
				m_internalPuller = createHttpSource();
				m_puller = m_internalPuller.get();
			}

			m_dirName = dirName(cfg->url);
			m_output = addOutput();
		}

		void process() override {
			if(!doProcess())
				m_host->activate(false);
		}

	private:
		struct Entry {
			string url;
			int64_t timestamp;
		};

		bool doProcess() {
			if(!m_hasPlaylist) {
				auto main = downloadPlaylist(m_playlistUrl);
				if(main.empty()) {
					m_host->log(Error, "No main playlist");
					return false;
				}

				string subUrl;
				if (startsWith(main[0].url, "http"))
					subUrl = main[0].url;
				else if (startsWith(main[0].url, "/"))
					subUrl = serverName(m_playlistUrl) + main[0].url;
				else
					subUrl = m_dirName + main[0].url;

				m_chunks = downloadPlaylist(subUrl);
				m_hasPlaylist = true;
			}

			if(m_chunks.empty()) {
				m_host->log(Debug, "Stopping");
				return false;
			}

			if (m_live)
				m_chunks.erase(m_chunks.begin(), m_chunks.end() - 1);

			while (!m_chunks.empty()) {
				auto const chunkUrl = m_dirName + m_chunks[0].url;
				m_host->log(Debug, ("Download chunk: '" + chunkUrl + "'").c_str());
				auto const chunk = download(m_puller, chunkUrl.c_str());
				m_chunks.erase(m_chunks.begin());

				auto data = m_output->allocData<DataRaw>(chunk.size());
				data->setMediaTime(m_chunks[0].timestamp, 1/*in seconds*/);
				if(chunk.size())
					memcpy(data->buffer->data().ptr, chunk.data(), chunk.size());
				m_output->post(data);
			}

			return true;
		}

		vector<Entry> downloadPlaylist(string url) {
			auto contents = download(m_puller, url.c_str());
			vector<Entry> r;
			int64_t programDateTime = 0;
			int durInSec = 0;
			m_live = true;
			string line;
			stringstream ss(string(contents.begin(), contents.end()));
			while(getline(ss, line)) {
				if(line.empty())
					continue;

				if(line[0] == '#') {
					if (startsWith(line, "#EXT-X-PROGRAM-DATE-TIME:"))
						programDateTime = parseDate(line.substr(strlen("#EXT-X-PROGRAM-DATE-TIME:")));
					else if (startsWith(line, "#EXT-X-TARGETDURATION:"))
						durInSec = stoi(line.substr(strlen("#EXT-X-TARGETDURATION:")));
					else if (startsWith(line, "#EXTINF:"))
						durInSec = stoi(line.substr(strlen("#EXTINF:")));
					else if (startsWith(line, "#EXT-X-ENDLIST"))
						m_live = false;

					continue;
				}

				r.push_back( { line, programDateTime } );
				programDateTime += durInSec;
			}
			return r;
		}

		KHost* const m_host;
		string const m_playlistUrl;
		IFilePuller* m_puller;
		OutputDefault* m_output = nullptr;
		bool m_hasPlaylist = false;
		bool m_live = true;
		string m_dirName;
		vector<Entry> m_chunks;
		unique_ptr<IFilePuller> m_internalPuller;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (HlsDemuxConfig*)va;
	enforce(host, "HlsDemuxer: host can't be NULL");
	enforce(config, "HlsDemuxer: config can't be NULL");
	return createModule<HlsDemuxer>(host, config).release();
}

auto const registered = Factory::registerModule("HlsDemuxer", &createObject);
}
