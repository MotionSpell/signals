#include "lib_utils/sax_xml_parser.hpp"

#include "tests/tests.hpp"

static const char xmlTestData[] = R"(
<?xml version="1.0" encoding="utf-8"?>
<!-- This is a comment -->
<MPD availabilityStartTime="1970-01-01T00:00:00Z" id="Config part of url maybe?" maxSegmentDuration="PT2S" minBufferTime="PT2S" minimumUpdatePeriod="P100Y" profiles="urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple" publishTime="2019-01-15T15:09:07Z" timeShiftBufferDepth="PT5M" type="dynamic" ns1:schemaLocation="urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd" xmlns="urn:mpeg:dash:schema:mpd:2011" xmlns:ns1="http://www.w3.org/2001/XMLSchema-instance">
   <ProgramInformation>
      <Title>Media Presentation Description from DASH-IF live simulator</Title>
   </ProgramInformation>
   <BaseURL>http://livesim.dashif.org/livesim/testpic_2s/</BaseURL>
<Period id="p0" start="PT0S">
      <AdaptationSet contentType="audio" lang="eng" mimeType="audio/mp4" segmentAlignment="true" startWithSAP="1">
         <Role schemeIdUri="urn:mpeg:dash:role:2011" value="main" />
         <SegmentTemplate duration="2" initialization="$RepresentationID$/init.mp4" media="$RepresentationID$/$Number$.m4s" startNumber="0" />
         <Representation audioSamplingRate="48000" bandwidth="48000" codecs="mp4a.40.2" id="A48">
            <AudioChannelConfiguration schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011" value="2" />
         </Representation>
      </AdaptationSet>
      <AdaptationSet contentType="video" maxFrameRate="60/2" maxHeight="360" maxWidth="640" mimeType="video/mp4" minHeight="360" minWidth="640" par="16:9" segmentAlignment="true" startWithSAP="1">
         <Role schemeIdUri="urn:mpeg:dash:role:2011" value="main" />
         <SegmentTemplate duration="2" initialization="$RepresentationID$/init.mp4" media="$RepresentationID$/$Number$.m4s" startNumber="0" />
         <Representation bandwidth="300000" codecs="avc1.64001e" frameRate="60/2" height="360" id="V300" sar="1:1" width="640" />
      </AdaptationSet>
   </Period>
</MPD>
)";

unittest("SAX XML parser: normal") {
  std::vector<std::string> tagsS, tagsE;
  auto onNodeStart = [&](std::string name, SmallMap<std::string, std::string> &attributes) {
    (void)attributes;
    tagsS.push_back(name);
  };
  auto onNodeEnd = [&](std::string name, std::string /*content*/) { tagsE.push_back(name); };

  auto expectedS = std::vector<std::string>({
        "MPD",
        "ProgramInformation",
        "Title",
        "BaseURL",
        "Period",
        "AdaptationSet",
        "Role",
        "SegmentTemplate",
        "Representation",
        "AudioChannelConfiguration",
        "AdaptationSet",
        "Role",
        "SegmentTemplate",
        "Representation",
  });

  auto expectedE = std::vector<std::string>({
        "Title",
        "ProgramInformation",
        "BaseURL",
        "",
        "",
        "",
        "Representation",
        "AdaptationSet",
        "",
        "",
        "",
        "AdaptationSet",
        "Period",
        "MPD",
  });

  saxParse(xmlTestData, onNodeStart, onNodeEnd);

  ASSERT_EQUALS(expectedS, tagsS);
  ASSERT_EQUALS(expectedE, tagsE);
}

static const char xmlTestDataContent[] = R"(
<?xml version="1.0" encoding="utf-8"?>
 <A>a</A>
 <B>
  <C>c</C>
  <!-- This is a comment -->

  <D>d</D>

 </B>
)";

unittest("SAX XML parser: get element contents") {
  std::vector<std::string> contents;
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string content) { contents.push_back(content); };

  saxParse(xmlTestDataContent, onNodeStart, onNodeEnd);
  ASSERT_EQUALS(std::vector<std::string>({"a", "c", "d", ""}), contents);
}

unittest("SAX XML parser: invalid cases ") {
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string) {};

  const char invalidXmlTestData[] = R"(
<?xml version="1.0" encoding="utf-8"?>
<Hello>
  <World #>
  </World>
</Hello>
)";

  ASSERT_THROWN(saxParse(invalidXmlTestData, onNodeStart, onNodeEnd));
}

unittest("SAX XML parser: invalid (premature end of file)") {
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string) {};

  char invalidXmlTestData1[] = R"(<Hello)";
  ASSERT_THROWN(saxParse(invalidXmlTestData1, onNodeStart, onNodeEnd));

  char invalidXmlTestData2[] = R"(<Hello/)";
  // ASSERT_THROWN  //FIXME: should raise an exception
  saxParse(invalidXmlTestData2, onNodeStart, onNodeEnd);

  char invalidXmlTestData3[] = R"(<Hello/>)"; // corner case of the parser
  saxParse(invalidXmlTestData3, onNodeStart, onNodeEnd);
}

unittest("SAX XML parser: invalid </br> in middle of text") {
  std::vector<std::string> contents;
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string content) { contents.push_back(content); };
  char invalidXmlTestData[] = R"(<T>tt<br/>uu</T>)"; // corner case of the parser
  saxParse(invalidXmlTestData, onNodeStart, onNodeEnd);
  ASSERT_EQUALS(std::vector<std::string>({"tt<br/>uu"}), contents);
}

unittest("SAX XML parser: escape") {
  std::vector<std::string> contents;
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string content) { contents.push_back(content); };
  char invalidXmlTestData[] = R"(<T>&amp;</T>)";
  saxParse(invalidXmlTestData, onNodeStart, onNodeEnd);
  ASSERT_EQUALS(std::vector<std::string>({"&"}), contents);
}

unittest("SAX XML parser: escape truncated") {
  std::vector<std::string> contents;
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string content) { contents.push_back(content); };
  char invalidXmlTestData[] = R"(<T>&amp</T>)"; // corner case of the parser
  saxParse(invalidXmlTestData, onNodeStart, onNodeEnd);
  ASSERT_EQUALS(std::vector<std::string>({"&amp"}), contents);
}

unittest("SAX XML parser: escape truncated") {
  std::vector<std::string> contents;
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string content) { contents.push_back(content); };
  char invalidXmlTestData[] = R"(<T>&&amp&amp;</T>)"; // corner case of the parser
  saxParse(invalidXmlTestData, onNodeStart, onNodeEnd);
  ASSERT_EQUALS(std::vector<std::string>({"&&amp&"}), contents);
}

unittest("SAX XML parser: escape coverage") {
  std::vector<std::string> contents;
  auto onNodeStart = [&](std::string, SmallMap<std::string, std::string> &) {};
  auto onNodeEnd = [&](std::string, std::string content) { contents.push_back(content); };
  char invalidXmlTestData[] = R"(<T>&amp;&lt;&gt;&apos;&quot;</T>)"; // corner case of the parser
  saxParse(invalidXmlTestData, onNodeStart, onNodeEnd);
  ASSERT_EQUALS(std::vector<std::string>({"&<>'\""}), contents);
}
