
#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/subtitle.hpp"
#include "../subtitle_encoder.hpp"
#include "lib_utils/format.hpp"

using namespace Tests;
using namespace Modules;

namespace {
struct OutStub : ModuleS {
	std::vector<int64_t> times;
	std::vector<std::string> ttml;
	void processOne(Data data) override {
		ttml.push_back(std::string((char*)data->data().ptr, data->data().len));
		times.push_back(data->get<PresentationTime>().time);
	}
};
}

unittest("ttml_encoder") {
	SubtitleEncoderConfig cfg;
	cfg.splitDurationInMs = 1000;
	cfg.maxDelayBeforeEmptyInMs = 2000;
	cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
	auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);
	Page page {0, IClock::Rate * 4,  std::vector<Page::Line>({{"toto", {}, {"#ffffff"}}, {"titi", {}, {"#ff0000"}}})};
	auto data = std::make_shared<DataSubtitle>(0);
	auto const time = page.showTimestamp + IClock::Rate * 4;
	data->set(DecodingTime{ time });
	data->set(PresentationTime{time});
	data->page = page;

	auto ttmlAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(m->getOutput(0), ttmlAnalyzer->getInput(0));

	m->getInput(0)->push(data);

	std::vector<int64_t> expectedTimes = {0, timescaleToClock(cfg.splitDurationInMs, 1000)};
	std::vector<std::string> expectedTtml = { R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:00.000" end="00:00:01.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto</span>
      </p>
      <p region="Region0_24" style="Style0_0" begin="00:00:00.000" end="00:00:01.000">
        <span tts:color="#ff0000" tts:backgroundColor="#000000c2">titi</span>
      </p>
    </div>
  </body>
</tt>

)|", R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:01.000" end="00:00:02.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto</span>
      </p>
      <p region="Region0_24" style="Style0_0" begin="00:00:01.000" end="00:00:02.000">
        <span tts:color="#ff0000" tts:backgroundColor="#000000c2">titi</span>
      </p>
    </div>
  </body>
</tt>

)|"};

	ASSERT_EQUALS(expectedTimes, ttmlAnalyzer->times);
	ASSERT_EQUALS(expectedTtml, ttmlAnalyzer->ttml);
}

unittest("[DISABLED] ttml_encoder: double height (teletext style)") {
	SubtitleEncoderConfig cfg;
	cfg.splitDurationInMs = 1000;
	cfg.maxDelayBeforeEmptyInMs = 2000;
	cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
	auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);

	Page page {0, IClock::Rate * 3,  std::vector<Page::Line>({{"titi", {}, {"#ff0000", "#000000", true}}})};
	auto data = std::make_shared<DataSubtitle>(0);
	auto const time = page.showTimestamp + IClock::Rate * 3;
	data->set(DecodingTime{ time });
	data->set(PresentationTime{time});
	data->page = page;

	auto ttmlAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(m->getOutput(0), ttmlAnalyzer->getInput(0));

	m->getInput(0)->push(data);

	std::vector<int64_t> expectedTimes = {0};
	std::vector<std::string> expectedTtml = { R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0_double" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0_double" begin="00:00:00.000" end="00:00:01.000">
        <span tts:color="#ff0000" tts:backgroundColor="#000000">titi</span>
      </p>
    </div>
  </body>
</tt>

)|"};
	ASSERT_EQUALS(expectedTimes, ttmlAnalyzer->times);
	ASSERT_EQUALS(expectedTtml, ttmlAnalyzer->ttml);
}

unittest("ttml_encoder: overlapping samples") {
	SubtitleEncoderConfig cfg;
	cfg.splitDurationInMs = 1000;
	cfg.maxDelayBeforeEmptyInMs = 2000;
	cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
	auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);

	auto ttmlAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(m->getOutput(0), ttmlAnalyzer->getInput(0));

	for (int i=0; i<2; ++i) {
		Page page {0, IClock::Rate * (i+4),  std::vector<Page::Line>({{ format("toto%s", i), {}, {"#ffffff"}}, {"titi", {}, {"#ff0000"}}})};
		auto const time = page.showTimestamp + IClock::Rate * (i+4);

		auto data = std::make_shared<DataSubtitle>(0);
		data->set(DecodingTime{ time });
		data->set(PresentationTime{time});
		data->page = page;
		m->getInput(0)->push(data);
	}

	std::vector<int64_t> expectedTimes = {0, timescaleToClock(cfg.splitDurationInMs, 1000), timescaleToClock(cfg.splitDurationInMs * 2, 1000)};
	std::vector<std::string> expectedTtml = { R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:00.000" end="00:00:01.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto0</span>
      </p>
      <p region="Region0_24" style="Style0_0" begin="00:00:00.000" end="00:00:01.000">
        <span tts:color="#ff0000" tts:backgroundColor="#000000c2">titi</span>
      </p>
    </div>
  </body>
</tt>

)|", R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:01.000" end="00:00:02.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto0</span>
      </p>
      <p region="Region0_24" style="Style0_0" begin="00:00:01.000" end="00:00:02.000">
        <span tts:color="#ff0000" tts:backgroundColor="#000000c2">titi</span>
      </p>
    </div>
  </body>
</tt>

)|", R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:02.000" end="00:00:03.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto1</span>
      </p>
      <p region="Region0_24" style="Style0_0" begin="00:00:02.000" end="00:00:03.000">
        <span tts:color="#ff0000" tts:backgroundColor="#000000c2">titi</span>
      </p>
    </div>
  </body>
</tt>

)|"};

	ASSERT_EQUALS(expectedTimes, ttmlAnalyzer->times);
	ASSERT_EQUALS(expectedTtml, ttmlAnalyzer->ttml);
}

unittest("ttml_encoder: segmentation and empty page") {
	SubtitleEncoderConfig cfg;
	cfg.splitDurationInMs = 1000;
	cfg.maxDelayBeforeEmptyInMs = 2000;
	cfg.forceEmptyPage = true;
	cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
	auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);

	Page page1 {IClock::Rate * 0 / 1, IClock::Rate * 1 / 2,  std::vector<Page::Line>({{"toto1"}})};
	Page page2 {IClock::Rate * 1 / 2, IClock::Rate * 3 / 4,  std::vector<Page::Line>({{"toto2"}})};
	Page page3 {IClock::Rate * 3 / 4, IClock::Rate * 5 / 4,  std::vector<Page::Line>({{"toto3"}})};

	auto makeData = [](Page &page, int64_t time) {
		auto data = std::make_shared<DataSubtitle>(0);
		data->set(DecodingTime{ time });
		data->set(PresentationTime{time});
		data->page = page;
		return data;
	};

	auto data1 = makeData(page1, IClock::Rate * 1);
	auto data2 = makeData(page2, IClock::Rate * 2);
	auto data3 = makeData(page3, IClock::Rate * 5);

	auto ttmlAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(m->getOutput(0), ttmlAnalyzer->getInput(0));

	m->getInput(0)->push(data1);
	m->getInput(0)->push(data2);
	m->getInput(0)->push(data3);

	std::vector<int64_t> expectedTimes = {0, timescaleToClock(cfg.splitDurationInMs, 1000), timescaleToClock(cfg.splitDurationInMs * 2, 1000)};
	std::vector<std::string> expectedTtml = { R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region1_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
      <region xml:id="Region2_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:00.000" end="00:00:00.500">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto1</span>
      </p>
      <p region="Region1_24" style="Style0_0" begin="00:00:00.500" end="00:00:00.750">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto2</span>
      </p>
      <p region="Region2_24" style="Style0_0" begin="00:00:00.750" end="00:00:01.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto3</span>
      </p>
    </div>
  </body>
</tt>

)|", R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:01.000" end="00:00:01.250">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2">toto3</span>
      </p>
    </div>
  </body>
</tt>

)|", R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="Style0_0" tts:fontSize="100%" tts:fontFamily="monospaceSansSerif" />
    </styling>
    <layout>
      <region xml:id="Region0_24" tts:origin="10% 95.8333%" tts:extent="80% 4.16667%" tts:displayAlign="center" tts:textAlign="center" />
    </layout>
  </head>
  <body>
    <div>
      <p region="Region0_24" style="Style0_0" begin="00:00:02.000" end="00:00:03.000">
        <span tts:color="#ffffff" tts:backgroundColor="#000000c2"></span>
      </p>
    </div>
  </body>
</tt>

)|"};

	ASSERT_EQUALS(expectedTimes, ttmlAnalyzer->times);
	ASSERT_EQUALS(expectedTtml, ttmlAnalyzer->ttml);
}

unittest("ttml_encoder: BasicDE styling") {
	SubtitleEncoderConfig cfg;
	cfg.splitDurationInMs = 1000;
	cfg.maxDelayBeforeEmptyInMs = 2000;
	cfg.forceEmptyPage = true;
	cfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
	auto m = loadModule("SubtitleEncoder", &NullHost, &cfg);

	Page page = { 0, 30 * IClock::Rate, {
	            { "A white sentence", {23}, { "#ffffff", "#000000c2", false, "Verdana, Arial, Tiresias", "160%", "125%" } },
	            { "in a two row subtitle", {}, { "#ff0000", "#000000c2", false, "Verdana, Arial, Tiresias", "160%", "125%" } }
	        }, 50, 30 };

	auto makeData = [](Page &page, int64_t time) {
		auto data = std::make_shared<DataSubtitle>(0);
		data->set(DecodingTime{ time });
		data->set(PresentationTime{time});
		data->page = page;
		return data;
	};

	auto data = makeData(page, IClock::Rate * 3);

	auto ttmlAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(m->getOutput(0), ttmlAnalyzer->getInput(0));

	m->getInput(0)->push(data);

	std::vector<int64_t> expectedTimes = {0};
	std::vector<std::string> expectedTtml = { R"|(<?xml version="1.0" encoding="utf-8"?>
<tt xmlns="http://www.w3.org/ns/ttml" xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttm="http://www.w3.org/ns/ttml#metadata" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xml:lang="en" ttp:cellResolution="50 30" >
  <head>
    <styling>
      <style xml:id="defaultStyle" tts:fontFamily="Verdana, Arial, Tiresias" tts:fontSize="160%" tts:lineHeight="125%" />
      <style xml:id="text_0" tts:color="#ffffff" tts:backgroundColor="#000000c2" />
      <style xml:id="text_1" tts:color="#ff0000" tts:backgroundColor="#000000c2" />
      <style xml:id="textCenter" tts:textAlign="center" />
    </styling>
    <layout>
      <region xml:id="Region0" tts:origin="10% 10%" tts:extent="80% 80%" tts:displayAlign="after" />
    </layout>
  </head>
  <body>
    <div style="defaultStyle">
      <p region="Region0" begin="00:00:00.000" end="00:00:01.000" style="textCenter">
        <span style="text_0">A white sentence</span>
        <br />
        <span style="text_1">in a two row subtitle</span>
      </p>
    </div>
  </body>
</tt>

)|"};

	ASSERT_EQUALS(expectedTimes, ttmlAnalyzer->times);
	ASSERT_EQUALS(expectedTtml, ttmlAnalyzer->ttml);
}
