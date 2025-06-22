#include "plugins/HlsDemuxer/hls_demux.hpp"
#include "tests/tests.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_modules/utils/helper.hpp" // NullHost
#include <vector>
#include <map>
#include <mutex>
#include <string>

using namespace std;
using namespace Tests;
using namespace Modules;
using namespace In;

namespace {
struct MemoryFileSystem : IFilePuller {
	void wget(const char* szUrl, std::function<void(SpanC)> callback) override {
		std::unique_lock<std::mutex> lock(mutex);
		auto url = string(szUrl);
		requests.push_back(url);
		if(resources.find(url) == resources.end())
			return;
		callback({(const uint8_t*)resources[url].data(), resources[url].size()});
	}
	void askToExit() override {}

	map<string, string> resources;
	vector<string> requests;
	std::mutex mutex;
};

}

unittest("hls demux: download main playlist") {
	MemoryFileSystem fs;
	fs.resources["http://test.com/playlist.m3u8"] = R"(
#EXTM3U
sub.m3u8
)";

	fs.resources["http://test.com/sub.m3u8"] = R"(
#EXTM3U
chunk-01.ts
chunk-next.ts
chunk-last.ts
#EXT-X-ENDLIST
)";

	HlsDemuxConfig cfg {};
	cfg.url = "http://test.com/playlist.m3u8";
	cfg.filePuller = &fs;
	auto demux = loadModule("HlsDemuxer", &NullHost, &cfg);
	for(int i=0;i < 100;++i)
		demux->process();
	ASSERT_EQUALS(
	    vector<string>({
	            "http://test.com/playlist.m3u8",
	            "http://test.com/sub.m3u8",
	            "http://test.com/chunk-01.ts",
	            "http://test.com/chunk-next.ts",
	            "http://test.com/chunk-last.ts",
	        }),
	    fs.requests);
}

