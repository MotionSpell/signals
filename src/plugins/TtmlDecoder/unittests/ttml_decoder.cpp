#include "../ttml_decoder.hpp"

#include "lib_media/common/attributes.hpp"
#include "lib_media/common/subtitle.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "plugins/SubtitleEncoder/subtitle_encoder.hpp"

using namespace Modules;

// allows ASSERT_EQUALS on Page
static std::ostream &operator<<(std::ostream &o, const Page &p) {
  o << p.toString();
  return o;
}

static bool operator!=(const Page::Region &lhs, const Page::Region &rhs) {
  return lhs.col != rhs.col || lhs.row != rhs.row;
}

static bool operator!=(const Page::Style &lhs, const Page::Style &rhs) {
  return lhs.bgColor != rhs.bgColor || lhs.color != rhs.color || lhs.doubleHeight != rhs.doubleHeight ||
        lhs.fontFamily != rhs.fontFamily || lhs.fontSize != rhs.fontSize || lhs.lineHeight != rhs.lineHeight;
}

static bool operator!=(const Page::Line &lhs, const Page::Line &rhs) {
  return lhs.text != rhs.text || lhs.style != rhs.style || lhs.region != rhs.region;
}

static bool operator!=(const Page &lhs, const Page &rhs) {
  if(lhs.lines.size() != rhs.lines.size())
    return true;

  for(size_t i = 0; i < lhs.lines.size(); ++i)
    if(lhs.lines[i] != rhs.lines[i])
      return true;

  return lhs.showTimestamp != rhs.showTimestamp || lhs.hideTimestamp != rhs.hideTimestamp ||
        lhs.numCols != rhs.numCols || lhs.numRows != rhs.numRows;
}

struct ZeroClock : IClock {
  Fraction now() const override { return 0; };
};

// operator!= shall be declared before this include
#include "tests/tests.hpp"

unittest("ttml_decoder: ttml_encoder sample") {
  SubtitleEncoderConfig encCfg;
  encCfg.splitDurationInMs = 1000;
  encCfg.maxDelayBeforeEmptyInMs = 2000;
  encCfg.timingPolicy = SubtitleEncoderConfig::RelativeToMedia;
  auto enc = loadModule("SubtitleEncoder", &NullHost, &encCfg);

  TtmlDecoderConfig decCfg;
  auto zc = std::make_shared<ZeroClock>();
  decCfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &decCfg);

  ConnectOutputToInput(enc->getOutput(0), dec->getInput(0));

  auto const pageNum = 2;
  auto const pageDurationIn180k =
        timescaleToClock(pageNum * encCfg.splitDurationInMs + encCfg.maxDelayBeforeEmptyInMs, 1000);
  Page pageSent{0, pageDurationIn180k,
        std::vector<Page::Line>(
              {{"toto", {26, 22}, {"#ffffff", "#000000c2", false, "Verdana, Arial, Tiresias", "160%", "125%"}},
                    {"titi", {27, 22}, {"#ff0000", "#000000c2", false, "Verdana, Arial, Tiresias", "160%", "125%"}}}),
        50, 30};
  auto data = std::make_shared<DataSubtitle>(0);
  auto const time = pageSent.showTimestamp + pageDurationIn180k;
  data->set(PresentationTime{time});
  data->page = pageSent;

  int received = 0;
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    received++;
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    pageSent.hideTimestamp = 30 * IClock::Rate; // there is no way to deduce this value here
    ASSERT_EQUALS(pageSent, pageReceived);
  });

  enc->getInput(0)->push(data);

  ASSERT_EQUALS(pageNum, received);
}

unittest("ttml_decoder: ebu-tt-live (WDR sample)") {
  std::string ttml = R"|(<?xml version="1.0" encoding="UTF-8"?>
<tt:tt xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ebuttm="urn:ebu:tt:metadata" xmlns:ebuttp="urn:ebu:tt:parameters" xmlns:ebutts="urn:ebu:tt:style" xml:lang="de" ttp:cellResolution="50 30" ttp:timeBase="clock" ttp:clockMode="local" ebuttp:sequenceIdentifier="TestSequence1" ebuttp:sequenceNumber="1636064848635">
    <tt:head>
        <tt:metadata>
            <ebuttm:documentMetadata>
                <ebuttm:documentEbuttVersion>v1.0</ebuttm:documentEbuttVersion>
            </ebuttm:documentMetadata>
        </tt:metadata>
        <tt:styling>
            <tt:style xml:id="defaultStyle" tts:fontFamily="Verdana,Arial,Tiresias" tts:fontSize="160%" tts:lineHeight="125%"/>
            <tt:style xml:id="textWhite" tts:color="#FFFFFF" tts:backgroundColor="#000000c2"/>
            <tt:style xml:id="textCenter" tts:textAlign="center"/>
        </tt:styling>
        <tt:layout>
            <tt:region xml:id="bottom" tts:origin="10% 10%" tts:extent="80% 80%" tts:displayAlign="after"/>
        </tt:layout>
    </tt:head>
    <tt:body dur="00:00:60.000">
        <tt:div style="defaultStyle">
            <tt:p xml:id="sub1" style="textCenter" region="bottom">
                <tt:span style="textWhite">Sample of a EBU-TT-LIVE document - line 1</tt:span>
                <tt:br/>
                <tt:span style="textWhite">Sample of a EBU-TT-LIVE document - line 2</tt:span>
            </tt:p>
        </tt:div>
    </tt:body>
</tt:tt>)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 60 * IClock::Rate,
        {{"Sample of a EBU-TT-LIVE document - line 1", {26, 22},
               {"#FFFFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}},
              {"Sample of a EBU-TT-LIVE document - line 2", {27, 22},
                    {"#FFFFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}}},
        50, 30};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("ttml_decoder: ebu-tt-live (EBU LIT User Input Producer sample)") {
  // http://ebu.github.io/ebu-tt-live-toolkit/ui/user_input_producer/index.html
  std::string ttml = R"|(<?xml version="1.0" ?>
<tt:tt
        ebuttp:sequenceIdentifier="mysocket"
        ebuttp:sequenceNumber="26"
        ttp:timeBase="clock"
      
        ttp:clockMode="local"
      
      
      
      
        xml:lang="en-GB"
        xmlns:ebuttm="urn:ebu:tt:metadata"
        xmlns:ebuttp="urn:ebu:tt:parameters"
        xmlns:ebutts="urn:ebu:tt:style"
        xmlns:tt="http://www.w3.org/ns/ttml"
        xmlns:tts="http://www.w3.org/ns/ttml#styling"
        xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
        xmlns:xml="http://www.w3.org/XML/1998/namespace">
  <tt:head>
    <tt:metadata>
      <ebuttm:documentMetadata/>
    </tt:metadata>
    <tt:styling>
      <tt:style xml:id="styleP" tts:color="rgb(255, 255, 255)"  ebutts:linePadding="0.5c" tts:fontFamily="sansSerif" />
      <tt:style xml:id="styleSpan" tts:backgroundColor="rgb(0, 0, 0)"/>
    </tt:styling>
    <tt:layout>
      <tt:region xml:id="bottomRegion" tts:origin="14.375% 60%" tts:extent="71.25% 24%" tts:displayAlign="after" tts:writingMode="lrtb" tts:overflow="visible" />
    </tt:layout>
  </tt:head>
  <tt:body
  
  
  
  dur="10">
    <tt:div region="bottomRegion">
      <tt:p xml:id="p1" style="styleP"><tt:span style="styleSpan">nils is yo</tt:span></tt:p>
    </tt:div>
  </tt:body>
</tt:tt>
)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 10 * IClock::Rate,
        {{"nils is yo", {12, 13}, {"rgb(255, 255, 255)", "rgb(0, 0, 0)", false, "sansSerif"}}}, 32, 15};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("ttml_decoder: ebu-tt-live (BasicDE) styling") {
  std::string ttml = R"|(<?xml version="1.0" ?>
<?xml version="1.0" encoding="UTF-8"?>
<!-- Profile: EBU-TT-D-Basic-DE -->
<tt:tt xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ebuttm="urn:ebu:tt:metadata" ttp:timeBase="media" xml:lang="de"
    ttp:cellResolution="50 30">
    <tt:head>
        <tt:metadata>
            <ebuttm:documentMetadata>
                <ebuttm:documentEbuttVersion>v1.0</ebuttm:documentEbuttVersion>
            </ebuttm:documentMetadata>
        </tt:metadata>
        <tt:styling>
            <tt:style xml:id="defaultStyle" tts:fontFamily="Verdana, Arial, Tiresias" tts:fontSize="160%"
                tts:lineHeight="125%" />
            <tt:style xml:id="textWhite" tts:color="#ffffff" tts:backgroundColor="#000000c2" />
            <tt:style xml:id="textRed" tts:color="#ff0000" tts:backgroundColor="#000000c2" />
            <tt:style xml:id="textCenter" tts:textAlign="center" />
        </tt:styling>
        <tt:layout>
            <tt:region xml:id="bottom" tts:origin="10% 10%" tts:extent="80% 80%" tts:displayAlign="after" />
        </tt:layout>
    </tt:head>
    <tt:body>
        <tt:div style="defaultStyle">
            <tt:p xml:id="sub1" region="bottom" begin="00:00:00.000" end="00:00:02.120" style="textCenter">
                <tt:span style="textWhite">A white sentence</tt:span>
                <tt:br />
                <tt:span style="textRed">in a two row subtitle</tt:span>
            </tt:p>
        </tt:div>
    </tt:body>
</tt:tt>

)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 30 * IClock::Rate,
        {{"A white sentence", {26, 22}, {"#ffffff", "#000000c2", false, "Verdana, Arial, Tiresias", "160%", "125%"}},
              {"in a two row subtitle", {27, 22},
                    {"#ff0000", "#000000c2", false, "Verdana, Arial, Tiresias", "160%", "125%"}}},
        50, 30};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("[DISABLED] ttml_decoder: ebu-tt-live (BasicDE) letter styling") {
  std::string ttml = R"|(<?xml version="1.0" ?>
<?xml version="1.0" encoding="UTF-8"?>
<!-- Profile: EBU-TT-D-Basic-DE -->
<tt:tt xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ebuttm="urn:ebu:tt:metadata" ttp:timeBase="media" xml:lang="de"
    ttp:cellResolution="50 30">
    <tt:head>
        <tt:metadata>
            <ebuttm:documentMetadata>
                <ebuttm:documentEbuttVersion>v1.0</ebuttm:documentEbuttVersion>
            </ebuttm:documentMetadata>
        </tt:metadata>
        <tt:styling>
            <tt:style xml:id="defaultStyle" tts:fontFamily="Verdana, Arial, Tiresias" tts:fontSize="160%"
                tts:lineHeight="125%" />
            <tt:style xml:id="textWhite" tts:color="#ffffff" tts:backgroundColor="#000000c2" />
            <tt:style xml:id="textRed" tts:color="#ff0000" tts:backgroundColor="#000000c2" />
            <tt:style xml:id="textCenter" tts:textAlign="center" />
        </tt:styling>
        <tt:layout>
            <tt:region xml:id="bottom" tts:origin="10% 10%" tts:extent="80% 80%" tts:displayAlign="after" />
        </tt:layout>
    </tt:head>
    <tt:body>
        <tt:div style="defaultStyle">
            <tt:p xml:id="sub1" region="bottom" begin="00:00:00.000" end="00:00:02.120" style="textCenter">
                <tt:span style="textWhite">A </tt:span>
                <tt:span style="textRed">red</tt:span>
                <tt:span style="textWhite"> Word</tt:span>
                <tt:br />
                <tt:span style="textWhite">in a two row subtitle</tt:span>
            </tt:p>
        </tt:div>
    </tt:body>
</tt:tt>

)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 10 * IClock::Rate, {{"nils is yo", {}, {"#ffffff"}}}};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("ttml_decoder: ebu-tt-live (BasicDE) from WDR styling") {
  std::string ttml = R"|(<?xml version="1.0"?>
<tt:tt xmlns:tt="http://www.w3.org/ns/ttml" xmlns:ttp="http://www.w3.org/ns/ttml#parameter" xmlns:tts="http://www.w3.org/ns/ttml#styling" xmlns:ebuttm="urn:ebu:tt:metadata" xmlns:ebuttp="urn:ebu:tt:parameters" xmlns:ebutts="urn:ebu:tt:style" xml:lang="de" ttp:cellResolution="50 30" ttp:timeBase="clock" ttp:clockMode="local" ebuttp:sequenceIdentifier="TestSequence1" ebuttp:sequenceNumber="1656574125611">
	<tt:head>
		<tt:metadata>
			<ebuttm:documentMetadata>
				<ebuttm:documentEbuttVersion>v1.0</ebuttm:documentEbuttVersion>
			</ebuttm:documentMetadata>
		</tt:metadata>
		<tt:styling>
			<tt:style xml:id="defaultStyle" tts:fontFamily="Verdana,Arial,Tiresias" tts:fontSize="160%" tts:lineHeight="125%"/>
			<tt:style xml:id="textWhite" tts:color="#FFFFFF" tts:backgroundColor="#000000c2"/>
			<tt:style xml:id="textCenter" tts:textAlign="center"/>
		</tt:styling>
		<tt:layout>
			<tt:region xml:id="bottom" tts:origin="10% 10%" tts:extent="80% 80%" tts:displayAlign="after"/>
		</tt:layout>
	</tt:head>
	<tt:body dur="00:00:30.000">
		<tt:div style="defaultStyle">
			<tt:p xml:id="sub1" style="textCenter" region="bottom">
				<tt:span style="textWhite">irgendwie so Ausrufezeichen</tt:span>
				<tt:br/>
				<tt:span style="textWhite">Motorradfahrer dass die haben noch so</tt:span>
			</tt:p>
		</tt:div>
	</tt:body>
</tt:tt>)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 30 * IClock::Rate,
        {{"irgendwie so Ausrufezeichen", {26, 22},
               {"#FFFFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}},
              {"Motorradfahrer dass die haben noch so", {27, 22},
                    {"#FFFFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}}},
        50, 30};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("ttml_decoder: ebu-tt-live (BasicDE) from WDR with \"raisedBottom\" subtitle") {
  std::string ttml = R"|(<?xml version="1.0" encoding="UTF-8"?>
<tt:tt xmlns:tt="http://www.w3.org/ns/ttml"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ebuttm="urn:ebu:tt:metadata"
    xmlns:ebuttp="urn:ebu:tt:parameters"
    xmlns:ebutts="urn:ebu:tt:style" xml:lang="de" ttp:cellResolution="50 30" ttp:timeBase="clock" ttp:clockMode="local" ebuttp:sequenceIdentifier="" ebuttp:sequenceNumber="1.763047308001e+12">
    <tt:head>
        <tt:metadata>
            <ebuttm:documentMetadata>
                <ebuttm:documentEbuttVersion>v1.0</ebuttm:documentEbuttVersion>
            </ebuttm:documentMetadata>
        </tt:metadata>
        <tt:styling>
            <tt:style xml:id="defaultStyle" tts:fontFamily="Verdana,Arial,Tiresias" tts:fontSize="160%" tts:lineHeight="125%"/>
            <tt:style xml:id="textCyan" tts:color="#00FFFF" tts:backgroundColor="#000000c2"/>
            <tt:style xml:id="alignRight" tts:textAlign="right"/>
        </tt:styling>
        <tt:layout>
            <tt:region xml:id="raisedBottom" tts:origin="10% 60%" tts:extent="80% 30%" tts:displayAlign="before"/>
        </tt:layout>
    </tt:head>
    <tt:body dur="00:00:20.000">
        <tt:div style="defaultStyle">
            <tt:p xml:id="sub1" style="alignRight" region="raisedBottom">
                <tt:span style="textCyan">Großeinsätzen, in zwei Fällen schwamm</tt:span>
                <tt:br/>
                <tt:span style="textCyan">Männer in der Fahrrinne. Beide kamen aus</tt:span>
            </tt:p>
        </tt:div>
    </tt:body>
</tt:tt>)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 20 * IClock::Rate,
        {{"Großeinsätzen, in zwei Fällen schwamm", {17, 5},
               {"#00FFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}},
              {"Männer in der Fahrrinne. Beide kamen aus", {18, 5},
                    {"#00FFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}}},
        50, 30};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("ttml_decoder: ebu-tt-live (BasicDE) from WDR with top subtitle") {
  std::string ttml = R"|(<?xml version="1.0" encoding="UTF-8"?>
<tt:tt xmlns:tt="http://www.w3.org/ns/ttml"
    xmlns:ttp="http://www.w3.org/ns/ttml#parameter"
    xmlns:tts="http://www.w3.org/ns/ttml#styling"
    xmlns:ebuttm="urn:ebu:tt:metadata"
    xmlns:ebuttp="urn:ebu:tt:parameters"
    xmlns:ebutts="urn:ebu:tt:style" xml:lang="de" ttp:cellResolution="50 30" ttp:timeBase="clock" ttp:clockMode="local" ebuttp:sequenceIdentifier="" ebuttp:sequenceNumber="1.763047308001e+12">
    <tt:head>
        <tt:metadata>
            <ebuttm:documentMetadata>
                <ebuttm:documentEbuttVersion>v1.0</ebuttm:documentEbuttVersion>
            </ebuttm:documentMetadata>
        </tt:metadata>
        <tt:styling>
            <tt:style xml:id="defaultStyle" tts:fontFamily="Verdana,Arial,Tiresias" tts:fontSize="160%" tts:lineHeight="125%"/>
            <tt:style xml:id="textCyan" tts:color="#00FFFF" tts:backgroundColor="#000000c2"/>
            <tt:style xml:id="alignRight" tts:textAlign="right"/>
        </tt:styling>
        <tt:layout>
            <tt:region xml:id="top" tts:origin="10% 10%" tts:extent="80% 80%" tts:displayAlign="before"/>
        </tt:layout>
    </tt:head>
    <tt:body dur="00:00:20.000">
        <tt:div style="defaultStyle">
            <tt:p xml:id="sub1" style="alignRight" region="top">
                <tt:span style="textCyan">Großeinsätzen, in zwei Fällen schwamm</tt:span>
                <tt:br/>
                <tt:span style="textCyan">Männer in der Fahrrinne. Beide kamen aus</tt:span>
            </tt:p>
        </tt:div>
    </tt:body>
</tt:tt>)|";

  TtmlDecoderConfig cfg;
  auto zc = std::make_shared<ZeroClock>();
  cfg.clock = zc;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  int received = 0;
  Page expected = {0, 20 * IClock::Rate,
        {{"Großeinsätzen, in zwei Fällen schwamm", {2, 5},
               {"#00FFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}},
              {"Männer in der Fahrrinne. Beide kamen aus", {3, 5},
                    {"#00FFFF", "#000000c2", false, "Verdana,Arial,Tiresias", "160%", "125%"}}},
        50, 30};
  ConnectOutput(dec->getOutput(0), [&](Data data) {
    auto &pageReceived = safe_cast<const DataSubtitle>(data)->page;
    ASSERT_EQUALS(expected, pageReceived);
    received++;
  });

  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  dec->getInput(0)->push(pkt);

  ASSERT_EQUALS(1, received);
}

unittest("ttml_decoder: parsing error (empty)") {
  TtmlDecoderConfig cfg;
  auto dec = loadModule("TTMLDecoder", &NullHost, &cfg);
  std::string ttml = R"|(<?xml version="1.0"?>")|";
  auto pkt = std::make_shared<DataRaw>(ttml.size());
  memcpy(pkt->buffer->data().ptr, ttml.data(), ttml.size());
  pkt->set(PresentationTime{1789});
  ASSERT_THROWN(dec->getInput(0)->push(pkt));
}
