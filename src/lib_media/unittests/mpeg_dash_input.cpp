#include "lib_media/in/mpeg_dash_input.hpp"

#include "lib_media/common/metadata.hpp" //MetadataPkt
#include "lib_modules/modules.hpp"
#include "tests/tests.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

using namespace Tests;
using namespace Modules;
using namespace In;
using namespace std::chrono;

struct NotOwningFilePuller : IFilePuller {
  NotOwningFilePuller(IFilePuller *source)
      : source(source) {}
  void wget(const char *szUrl, std::function<void(SpanC)> callback) override { source->wget(szUrl, callback); }
  void askToExit() override {}
  IFilePuller *source;
};

struct LocalFilesystem : IFilePuller, IFilePullerFactory {
  void wget(const char *szUrl, std::function<void(SpanC)> callback) override {
    std::unique_lock<std::mutex> lock(mutex);
    auto url = std::string(szUrl);
    requests.push_back(url);
    if(resources.find(url) == resources.end())
      return;

    callback({(uint8_t *)resources[url].data(), resources[url].size()});
  }
  void askToExit() override {}

  std::unique_ptr<IFilePuller> create() override { return std::make_unique<NotOwningFilePuller>(this); }

  std::map<std::string, std::string> resources;
  std::vector<std::string> requests;
  std::mutex mutex;
};

unittest("[DISABLED] mpeg_dash_input: fail to get MPD") {
  LocalFilesystem source;
  ASSERT_THROWN(createModule<MPEG_DASH_Input>(&NullHost, &source, "http://toto.mpd"));
}

unittest("[DISABLED] mpeg_dash_input: get MPD") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
      <ContentComponent id="1" />
      <SegmentTemplate initialization="video-init.mp4" media="video-$Number$.m4s" startNumber="1" />
      <Representation id="video" mimeType="video/mp4"/>
    </AdaptationSet>
    <AdaptationSet contentType="audio">
      <ContentComponent id="1"/>
      <SegmentTemplate initialization="audio-init.mp4" media="audio-$Number$.m4s" startNumber="1" />
      <Representation id="audio" />
    </AdaptationSet>
  </Period>
</MPD>)|";

  LocalFilesystem source;
  source.resources["http://toto.mpd"] = MPD;
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "http://toto.mpd");
  ASSERT_EQUALS(2, dash->getNumOutputs());
}

unittest("[DISABLED] mpeg_dash_input: get MPD, one input") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
      <ContentComponent id="1"/>
      <SegmentTemplate initialization="audio-init.mp4" media="audio-$Number$.m4s" startNumber="10" />
      <Representation id="audio" mimeType="audio/mp4" codecs="codec_name_test"/>
    </AdaptationSet>
  </Period>
</MPD>)|";
  LocalFilesystem source;
  source.resources["http://single.mpd"] = MPD;
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "http://single.mpd");

  auto meta = std::dynamic_pointer_cast<const MetadataPkt>(dash->getOutput(0)->getMetadata());
  ASSERT(meta);

  ASSERT_EQUALS("codec_name_test", meta->codec);
}

unittest("[DISABLED] mpeg_dash_input: retrieve codec name") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
      <ContentComponent id="1"/>
      <SegmentTemplate initialization="audio-init.mp4" media="audio-$Number$.m4s" startNumber="10" />
      <Representation id="audio" mimeType="audio/mp4" codecs="toto"/>
    </AdaptationSet>
  </Period>
</MPD>)|";
  LocalFilesystem source;
  source.resources["http://single.mpd"] = MPD;
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "http://single.mpd");
  ASSERT_EQUALS(1, dash->getNumOutputs());
  ASSERT_EQUALS("toto", std::dynamic_pointer_cast<const MetadataPkt>(dash->getOutput(0)->getMetadata())->codec);
}

unittest("[DISABLED] mpeg_dash_input: get chunks") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period duration="PT30S">
    <AdaptationSet>
      <Representation id="88" mimeType="unknown"/>
    </AdaptationSet>
    <AdaptationSet>
      <ContentComponent id="1"/>
      <SegmentTemplate initialization="init-$RepresentationID$.mp4" media="sub/x$Number$y$RepresentationID$z" startNumber="3" duration="10"/>
      <Representation id="77" mimeType="audio/mp4"/>
    </AdaptationSet>
  </Period>
</MPD>)|";
  LocalFilesystem source;
  source.resources["main/live.mpd"] = MPD;
  source.resources["main/init-77.mp4"] = "initdata";
  source.resources["main/sub/x3y77z"] = "data3";
  source.resources["main/sub/x4y77z"] = "data4";
  source.resources["main/sub/x5y77z"] = "data5";
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "main/live.mpd");
  int chunkCount = 0;
  auto receive = [&](Data data) {
    ++chunkCount;
    ASSERT(chunkCount < 10);
    (void)data;
  };
  ConnectOutput(dash->getOutput(0), receive);

  for(int i = 0; i < 100; ++i)
    dash->process();

  dash = nullptr;

  ASSERT_EQUALS(std::vector<std::string>({
                      "main/live.mpd",
                      "main/init-77.mp4",
                      "main/sub/x3y77z",
                      "main/sub/x4y77z",
                      "main/sub/x5y77z",
                }),
        source.requests);
}

unittest("[DISABLED] mpeg_dash_input: only get available segments") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
      <SegmentTemplate
        initialization="$RepresentationID$/init.mp4"
        media="$RepresentationID$/$Number$.m4s"
        duration="10"/>

      <Representation id="medium" mimeType="audio/mp4">
        <SegmentTemplate startNumber="3" />
      </Representation>

      <Representation id="high" mimeType="audio/mp4">
        <SegmentTemplate startNumber="5" />
      </Representation>

      <Representation id="low" mimeType="audio/mp4">
        <SegmentTemplate startNumber="4" />
      </Representation>

    </AdaptationSet>
  </Period>
</MPD>)|";
  LocalFilesystem source;
  source.resources["main/manifest.mpd"] = MPD;
  source.resources["main/high/init.mp4"] = "a";
  source.resources["main/high/5.m4s"] = "a";
  source.resources["main/high/6.m4s"] = "a";
  source.resources["main/high/7.m4s"] = "a";

  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "main/manifest.mpd");
  dash->enableStream(0, 1); // high

  // Process only enough times to get available segments
  for(int i = 0; i < 4; ++i) { // One for init, three for segments
    dash->process();
  }

  dash = nullptr;

  ASSERT_EQUALS(std::vector<std::string>({"main/manifest.mpd", "main/high/init.mp4", "main/high/5.m4s",
                      "main/high/6.m4s", "main/high/7.m4s"}),
        source.requests);
}

unittest("[DISABLED] mpeg_dash_input: get concurrent chunks") {
  struct BlockingSource : IFilePuller {
    BlockingSource()
        : counter(0) {}
    void wget(const char *szUrl, std::function<void(SpanC)> callback) override {
      if(counter == 0) {
        callback({(uint8_t *)MPD.data(), MPD.size()});
        counter++;
        return;
      }

      counter++;

      while(counter > 4)
        std::this_thread::sleep_for(1ms);

      {
        std::unique_lock<std::mutex> lock(mutex);
        auto url = std::string(szUrl);
        requests.push_back(url);
      }
    }
    void askToExit() override {}
    void unblock() {
      while(counter < 4)
        std::this_thread::sleep_for(1ms);
      counter = 1;
    }

    std::atomic<int> counter;
    std::mutex mutex;
    std::vector<std::string> requests;
    const std::string MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period duration="PT10S">
    <AdaptationSet>
      <SegmentTemplate initialization="init-$RepresentationID$.mp4" media="sub/x$Number$y$RepresentationID$z" startNumber="3" duration="0"/>
      <Representation id="77" mimeType="audio/mp4"/>
    </AdaptationSet>
    <AdaptationSet>
      <SegmentTemplate initialization="init-$RepresentationID$.mp4" media="sub/x$Number$y$RepresentationID$z" startNumber="3" duration="0"/>
	  <Representation id="88" mimeType="video/mp4"/>
    </AdaptationSet>
  </Period>
</MPD>)|";
  };

  struct FilePullerFactory : IFilePullerFactory {
    std::unique_ptr<IFilePuller> create() override { return std::make_unique<NotOwningFilePuller>(&source); }
    BlockingSource source;
  };
  FilePullerFactory sourceFactory;
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &sourceFactory, "main/live.mpd");

  dash->process(); // init
  dash->process(); // first segment
  sourceFactory.source.unblock();
  dash = nullptr;

  // reorder
  std::vector<std::string> a77, v88;
  for(auto &url : sourceFactory.source.requests) {
    if(url == "main/init-77.mp4" || url == "main/sub/x3y77z")
      a77.push_back(url);
    if(url == "main/init-88.mp4" || url == "main/sub/x3y88z")
      v88.push_back(url);
  }

  ASSERT_EQUALS(std::vector<std::string>({"main/init-77.mp4", "main/sub/x3y77z"}), a77);
  ASSERT_EQUALS(std::vector<std::string>({"main/init-88.mp4", "main/sub/x3y88z"}), v88);
}

unittest("[DISABLED] mpeg_dash_input: number of outputs is the number of adaptation sets with at least one "
         "representation") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet/>
    <AdaptationSet>
      <Representation mimeType="video/mp4"/>
      <Representation mimeType="video/mp4"/>
    </AdaptationSet>
    <AdaptationSet/>
  </Period>
</MPD>)|";
  LocalFilesystem source;
  source.resources["main/manifest.mpd"] = MPD;
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "main/manifest.mpd");

  ASSERT_EQUALS(1, dash->getNumOutputs());
}

namespace {
static void switchRep(const std::string &MPD) {
  LocalFilesystem source;
  source.resources["main/manifest.mpd"] = MPD;
  source.resources["main/low/init.mp4"] = "a";
  source.resources["main/high/init.mp4"] = "a";

  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "main/manifest.mpd"); // main/manifest.mpd
  ASSERT_EQUALS(1, dash->getNumAdaptationSets());
  ASSERT_EQUALS(2, dash->getNumRepresentationsInAdaptationSet(0));

  dash->process(); // main/low/init.mp4
  dash->process(); // main/low/5.m4s
  dash->enableStream(0, 1); // TODO: main/high/init.mp4
  dash->process(); // main/high/6.m4s
  dash->enableStream(0, 0); // nothing
  dash->process(); // main/low/7.m4s

  dash = nullptr;

  ASSERT_EQUALS(std::vector<std::string>({
                      "main/manifest.mpd",
                      "main/low/init.mp4",
                      "main/low/5.m4s",
                      //"main/high/init.mp4", //TODO
                      "main/high/6.m4s",
                      "main/low/7.m4s",
                }),
        source.requests);
}
}

unittest("[DISABLED] mpeg_dash_input: switch representations in adaption set") {
  // TODO: set different start numbers for each representation
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
      <SegmentTemplate
        initialization="$RepresentationID$/init.mp4"
        media="$RepresentationID$/$Number$.m4s"
        duration="10"/>
      <Representation id="low" mimeType="audio/mp4">
        <SegmentTemplate startNumber="5" />
      </Representation>
      <Representation id="high" mimeType="audio/mp4">
        <SegmentTemplate startNumber="5" />
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>)|";

  switchRep(MPD);
}

unittest("[DISABLED] mpeg_dash_input: ensure SegmentTemplate update when switching representations") {
  // TODO: set different start numbers for each representation
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
      <SegmentTemplate duration="10"/>
      <Representation id="low" mimeType="audio/mp4">
        <SegmentTemplate startNumber="5"
			initialization="low/init.mp4"
			media="low/$Number$.m4s" />
      </Representation>
      <Representation id="high" mimeType="audio/mp4">
        <SegmentTemplate startNumber="5"
        initialization="high/init.mp4"
        media="high/$Number$.m4s" />
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>)|";

  switchRep(MPD);
}

unittest("[DISABLED] mpeg_dash_input: get adaptation set SRD descriptor") {
  static auto const MPD = R"|(
<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet>
	  <SupplementalProperty schemeIdUri="urn:mpeg:dash:srd:2014" value="1,2,3,4,5,6,7"/>
	  <Representation mimeType="audio/mp4"/>
	</AdaptationSet>
  </Period>
</MPD>)|";

  LocalFilesystem source;
  source.resources["manifest.mpd"] = MPD;
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &source, "manifest.mpd");
  ASSERT_EQUALS(1, dash->getNumAdaptationSets());
  ASSERT_EQUALS("1,2,3,4,5,6,7", dash->getSRD(0));
}

std::unique_ptr<IFilePuller> createHttpSource();

secondclasstest("mpeg_dash_input: get MPD from remote server") {
  struct FilePullerFactory : IFilePullerFactory {
    std::unique_ptr<IFilePuller> create() override { return createHttpSource(); }
  };

  FilePullerFactory sourceFactory;
  auto url = "http://download.tsi.telecom-paristech.fr/gpac/DASH_CONFORMANCE/TelecomParisTech/mp4-live/"
             "mp4-live-mpd-AV-NBS.mpd";
  auto dash = createModule<MPEG_DASH_Input>(&NullHost, &sourceFactory, url);
  ASSERT_EQUALS(2, dash->getNumOutputs());
}

secondclasstest("mpeg_dash_input: non-existing MPD") {
  struct FilePullerFactory : IFilePullerFactory {
    std::unique_ptr<IFilePuller> create() override { return createHttpSource(); }
  };

  FilePullerFactory sourceFactory;
  auto url = "http://example.com/this_url_doesnt_exist_121324315235";
  ASSERT_THROWN(createModule<MPEG_DASH_Input>(&NullHost, &sourceFactory, url));
}

#include "lib_media/common/mpeg_dash_parser.hpp"

unittest("DashMpd: is copyable") {
  auto mpd = parseMpd({"", 0});
  auto copy = *mpd;
  (void)copy;
}
