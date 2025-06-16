#include "tests/tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata_file.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/tools.hpp" // safe_cast
#include "plugins/Dasher/mpeg_dash.hpp" // DasherConfig

using namespace Tests;
using namespace Modules;
using namespace std;

extern const char *g_version;

namespace {

std::shared_ptr<DataBase> createPacket(SpanC raw) {
	auto pkt = make_shared<DataRaw>(raw.len);
	memcpy(pkt->buffer->data().ptr, raw.ptr, raw.len);
	return pkt;
}

auto const segmentDuration = IClock::Rate * 3;
auto const segmentDurationInMs = clockToTimescale(segmentDuration, 1000);

std::shared_ptr<DataBase> getTestSegment(std::string lang = "") {
	static const uint8_t markerData[] = { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };

	auto r = createPacket(markerData);

	auto meta = make_shared<MetadataFile>(VIDEO_PKT);
	meta->durationIn180k = segmentDuration;
	meta->lang = lang;
	r->setMetadata(meta);

	return r;
}

struct MyOutput : ModuleS {
	void processOne(Data) override {
		++frameCount;
	}
	int frameCount = 0;
};

}

unittest("dasher: simple live") {
	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.live = true;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	auto recSeg = createModule<MyOutput>();
	ConnectOutputToInput(dasher->getOutput(0), recSeg->getInput(0));

	auto recMpd = createModule<MyOutput>();
	ConnectOutputToInput(dasher->getOutput(1), recMpd->getInput(0));

	dasher->getInput(0)->connect();

	for(int i=0; i < 50; ++i) {
		auto s = getTestSegment();
		dasher->getInput(0)->push(s);
	}

	// We're live: the manifest must have been received before flushing
	ASSERT_EQUALS(50, recMpd->frameCount);

	// All the segments must have been received
	ASSERT_EQUALS(50, recSeg->frameCount);

	dasher->flush();

	// FIXME: the last segment gets uploaded a second time on EOS
	ASSERT_EQUALS(50 + 1, recSeg->frameCount);
}

unittest("dasher: two live streams") {
	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.live = true;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	auto recSeg = createModule<MyOutput>();
	ConnectOutputToInput(dasher->getOutput(0), recSeg->getInput(0));

	auto recMpd = createModule<MyOutput>();
	ConnectOutputToInput(dasher->getOutput(1), recMpd->getInput(0));

	dasher->getInput(0)->connect();
	dasher->getInput(1)->connect();

	for(int i=0; i < 50; ++i) {
		auto s = getTestSegment();
		dasher->getInput(i%2)->push(s);
	}

	// We're live: the manifest must have been received before flushing
	ASSERT_EQUALS(25, recMpd->frameCount);

	// All the segments must have been received
	ASSERT_EQUALS(50, recSeg->frameCount);
}

unittest("dasher: two live streams unevenly interleaved") {
	auto const numStreams = 2;

	auto init = [](std::function<void(std::shared_ptr<IModule>, MyOutput*, MyOutput*)> f) {
		DasherConfig cfg{};
		cfg.segDurationInMs = segmentDurationInMs;
		cfg.live = true;
		cfg.forceRealDurations = true;
		auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

		auto recSeg = createModule<MyOutput>();
		ConnectOutputToInput(dasher->getOutput(0), recSeg->getInput(0));

		auto recMpd = createModule<MyOutput>();
		ConnectOutputToInput(dasher->getOutput(1), recMpd->getInput(0));

		/*simulate a possible behaviour from a muxer*/
		for (int i = 0; i < numStreams; ++i)
			dasher->getInput(i)->connect();

		f(dasher, recMpd.get(), recSeg.get());
	};

	auto getSeg = [&](uint64_t dur, bool EOS) {
		static const uint8_t markerData[] = { 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
		auto r = createPacket(markerData);
		auto meta = make_shared<MetadataFile>(VIDEO_PKT);
		meta->durationIn180k = dur;
		meta->EOS = EOS;
		r->setMetadata(meta);
		return r;
	};

	/*initial offset -> first shorter segment:
	  at the moment the dasher aggregates it with the next one*/
	init([&](std::shared_ptr<IModule> dasher, MyOutput* recMpd, MyOutput* recSeg) {
		for (int i = 0; i < numStreams; ++i)
			dasher->getInput(i)->push(getSeg(segmentDuration / 2, true));
		ASSERT_EQUALS(1, recMpd->frameCount);
		ASSERT_EQUALS(numStreams, recSeg->frameCount);
		recSeg->frameCount = 0;
	});

	/*ideal segment*/
	init([&](std::shared_ptr<IModule> dasher, MyOutput* recMpd, MyOutput* recSeg) {
		for (int i = 0; i < numStreams; ++i)
			dasher->getInput(i)->push(getSeg(segmentDuration, true));
		ASSERT_EQUALS(1, recMpd->frameCount);
		ASSERT_EQUALS(numStreams, recSeg->frameCount);
		recMpd->frameCount = 0;
		recSeg->frameCount = 0;
	});

	/*ideal segment with separate EOS*/
	init([&](std::shared_ptr<IModule> dasher, MyOutput *recMpd, MyOutput *recSeg) {
		auto const numSegs = 2;
		for (int j = 0; j < numSegs; ++j) {
			for (int i = 0; i < numStreams; ++i) {
				dasher->getInput(i)->push(getSeg(segmentDuration, false));
				dasher->getInput(i)->push(getSeg(0, true));
			}
		}
		ASSERT_EQUALS(numSegs, recMpd->frameCount);
		ASSERT_EQUALS(numSegs * numStreams * 2, recSeg->frameCount);
		recMpd->frameCount = 0;
		recSeg->frameCount = 0;
	});
}

unittest("dasher: timeshift buffer") {
	struct Server : ModuleS {
		void processOne(Data data) override {
			auto meta = safe_cast<const MetadataFile>(data->getMetadata().get());
			if(meta->filesize == INT64_MAX) // is it a "DELETE" ?
				deleted ++;
			else
				added ++;
		}
		int added = 0;
		int deleted = 0;
	};

	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.timeShiftBufferDepthInMs = 3 * segmentDurationInMs;
	cfg.live = true;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	auto server = createModule<Server>();
	ConnectOutputToInput(dasher->getOutput(0), server->getInput(0));

	dasher->getInput(0)->connect();

	for(int i=0; i < 40; ++i) {
		auto s = getTestSegment();
		dasher->getInput(0)->push(s);
	}

	// we expect to have 3 live segments ( timeShiftBufferDepthInMs / segDurationInMs )
	ASSERT_EQUALS(40, server->added);
	ASSERT_EQUALS(37, server->deleted);
}

unittest("dasher: timing computation") {
	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.live = true;
	struct MyUtcClock : IUtcClock {
		Fraction getTime() {
			return Fraction(1789, 1);
		}
	};
	MyUtcClock utcClock;
	cfg.utcClock = &utcClock;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	struct OutStub : ModuleS {
		std::string mpd;
		void processOne(Data data) override {
			if(mpd.empty())
				mpd = std::string((char*)data->data().ptr, data->data().len);
		}
	};

	auto mpdAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(dasher->getOutput(1), mpdAnalyzer->getInput(0));

	dasher->getInput(0)->connect();

	auto s = getTestSegment();
	dasher->getInput(0)->push(s);

	dasher->flush();

	auto expectedMpd = format("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	        "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\" id=\"id\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011, http://dashif.org/guidelines/dash264\" availabilityStartTime=\"1970-01-01T00:00:03Z\" publishTime=\"1970-01-01T00:29:49Z\" minBufferTime=\"PT00H00M0.001S\" minimumUpdatePeriod=\"PT00H00M3.000S\">\n"
	        "  <ProgramInformation moreInformationURL=\"http://signals.gpac-licensing.com\">\n"
	        "    <Copyright>Generated by Signals/%s</Copyright>\n"
	        "  </ProgramInformation>\n"
	        "  <Period id=\"1\" start=\"PT00H00M0.000S\">\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\">\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"0\"/>\n"
	        "      <Representation id=\"0\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"v_0_0x0/v_0_0x0-$Number$.m4s\" initialization=\"v_0_0x0/v_0_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "  </Period>\n"
	        "</MPD>\n",
	        g_version, segmentDurationInMs);

	ASSERT_EQUALS(expectedMpd, mpdAnalyzer->mpd);
}

unittest("dasher: multiple languages") {
	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.live = true;
	struct MyUtcClock : IUtcClock {
		Fraction getTime() {
			return Fraction(1789, 1);
		}
	};
	MyUtcClock utcClock;
	cfg.utcClock = &utcClock;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	struct OutStub : ModuleS {
		std::string mpd;
		void processOne(Data data) override {
			if(mpd.empty())
				mpd = std::string((char*)data->data().ptr, data->data().len);
		}
	};

	auto mpdAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(dasher->getOutput(1), mpdAnalyzer->getInput(0));

	dasher->getInput(0)->connect();
	dasher->getInput(1)->connect();

	auto s1 = getTestSegment("eng");
	dasher->getInput(0)->push(s1);

	auto s2 = getTestSegment("fra");
	dasher->getInput(1)->push(s2);

	dasher->flush();

	auto expectedMpd = format("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	        "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\" id=\"id\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011, http://dashif.org/guidelines/dash264\" availabilityStartTime=\"1970-01-01T00:00:03Z\" publishTime=\"1970-01-01T00:29:49Z\" minBufferTime=\"PT00H00M0.001S\" minimumUpdatePeriod=\"PT00H00M3.000S\">\n"
	        "  <ProgramInformation moreInformationURL=\"http://signals.gpac-licensing.com\">\n"
	        "    <Copyright>Generated by Signals/%s</Copyright>\n"
	        "  </ProgramInformation>\n"
	        "  <Period id=\"1\" start=\"PT00H00M0.000S\">\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\" lang=\"eng\">\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"0\"/>\n"
	        "      <Representation id=\"0\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"v_0_0x0/v_0_0x0-$Number$.m4s\" initialization=\"v_0_0x0/v_0_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\" lang=\"fra\">\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"0\"/>\n"
	        "      <Representation id=\"1\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"v_1_0x0/v_1_0x0-$Number$.m4s\" initialization=\"v_1_0x0/v_1_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "  </Period>\n"
	        "</MPD>\n",
	        g_version, segmentDurationInMs, segmentDurationInMs);

	ASSERT_EQUALS(expectedMpd, mpdAnalyzer->mpd);
}

unittest("dasher: multi-period folders") {
	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.live = true;
	cfg.multiPeriodFoldersInMs = cfg.segDurationInMs;
	cfg.baseUrlPrefixes = { "a/", "b/" };

	struct MyUtcClock : IUtcClock {
		Fraction getTime() {
			return Fraction(1789, 1);
		}
	};
	MyUtcClock utcClock;
	cfg.utcClock = &utcClock;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	struct OutStubMpd : ModuleS {
		std::string mpd;
		void processOne(Data data) override {
			//keep the last mpd
			mpd = std::string((char*)data->data().ptr, data->data().len);
		}
	};

	struct OutStubFns : ModuleS {
		std::vector<std::string> filenames;
		void processOne(Data data) override {
			filenames.push_back(safe_cast<const MetadataFile>(data->getMetadata())->filename);
		}
	};

	auto fnsAnalyzer = createModule<OutStubFns>();
	ConnectOutputToInput(dasher->getOutput(0), fnsAnalyzer->getInput(0));

	auto mpdAnalyzer = createModule<OutStubMpd>();
	ConnectOutputToInput(dasher->getOutput(1), mpdAnalyzer->getInput(0));

	dasher->getInput(0)->connect();

	auto s = getTestSegment();
	dasher->getInput(0)->push(s);
	dasher->getInput(0)->push(s);
	dasher->flush();

	auto expectedMpd = format("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	        "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"static\" id=\"id\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011, http://dashif.org/guidelines/dash264\" publishTime=\"1970-01-01T00:29:49Z\" minBufferTime=\"PT00H00M0.001S\" mediaPresentationDuration=\"PT00H00M6.000S\">\n"
	        "  <ProgramInformation moreInformationURL=\"http://signals.gpac-licensing.com\">\n"
	        "    <Copyright>Generated by Signals/%s</Copyright>\n"
	        "  </ProgramInformation>\n"
	        "  <Period id=\"1\" start=\"PT00H00M0.000S\" duration=\"PT00H00M3.000S\">\n"
	        "    <BaseURL serviceLocation=\"a/\"/>\n"
	        "    <BaseURL serviceLocation=\"b/\"/>\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\">\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"0\"/>\n"
	        "      <Representation id=\"0\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"1970_01_01_00_00_00/v_0_0x0/v_0_0x0-$Number$.m4s\" initialization=\"v_0_0x0/v_0_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "  </Period>\n"
	        "  <Period id=\"2\" start=\"PT00H00M3.000S\" duration=\"PT00H00M3.000S\">\n"
	        "    <BaseURL serviceLocation=\"a/\"/>\n"
	        "    <BaseURL serviceLocation=\"b/\"/>\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\">\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"1\"/>\n"
	        "      <Representation id=\"0\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"1970_01_01_00_00_03/v_0_0x0/v_0_0x0-$Number$.m4s\" initialization=\"v_0_0x0/v_0_0x0-init.mp4\" startNumber=\"1\" presentationTimeOffset=\"3000\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "  </Period>\n"
	        "</MPD>\n",
	        g_version, segmentDurationInMs, segmentDurationInMs);

	//FIXME: there are too much output calls with filenames
	std::vector<std::string> expectedFilenames { "1970_01_01_00_00_00/v_0_0x0/v_0_0x0-0.m4s", "1970_01_01_00_00_03/v_0_0x0/v_0_0x0-1.m4s", "1970_01_01_00_00_03/v_0_0x0/v_0_0x0-1.m4s", "1970_01_01_00_00_03/v_0_0x0/v_0_0x0-1.m4s", "1970_01_01_00_00_03/v_0_0x0/v_0_0x0-1.m4s"};

	ASSERT_EQUALS(expectedMpd, mpdAnalyzer->mpd);
	ASSERT_EQUALS(expectedFilenames, fnsAnalyzer->filenames);
}

unittest("dasher: tiles") {
	DasherConfig cfg {};
	cfg.segDurationInMs = segmentDurationInMs;
	cfg.live = true;
	cfg.tileInfo.push_back({ 1, 2, 3, 4, 5, 6, 7 });
	cfg.tileInfo.push_back({ 1, 2, 3, 4, 5, 6, 7 });
	cfg.tileInfo.push_back({ 3, 4, 5, 6, 7, 8, 9 });
	struct MyUtcClock : IUtcClock {
		Fraction getTime() {
			return Fraction(1789, 1);
		}
	};
	MyUtcClock utcClock;
	cfg.utcClock = &utcClock;
	auto dasher = loadModule("MPEG_DASH", &NullHost, &cfg);

	struct OutStub : ModuleS {
		std::string mpd;
		void processOne(Data data) override {
			if(mpd.empty())
				mpd = std::string((char*)data->data().ptr, data->data().len);
		}
	};

	auto mpdAnalyzer = createModule<OutStub>();
	ConnectOutputToInput(dasher->getOutput(1), mpdAnalyzer->getInput(0));

	dasher->getInput(0)->connect();
	dasher->getInput(1)->connect();
	dasher->getInput(2)->connect();

	auto s = getTestSegment();
	dasher->getInput(0)->push(s);
	dasher->getInput(1)->push(s);
	dasher->getInput(2)->push(s);

	dasher->flush();

	auto expectedMpd = format("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
	        "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\" id=\"id\" profiles=\"urn:mpeg:dash:profile:isoff-live:2011, http://dashif.org/guidelines/dash264\" availabilityStartTime=\"1970-01-01T00:00:03Z\" publishTime=\"1970-01-01T00:29:49Z\" minBufferTime=\"PT00H00M0.001S\" minimumUpdatePeriod=\"PT00H00M3.000S\">\n"
	        "  <ProgramInformation moreInformationURL=\"http://signals.gpac-licensing.com\">\n"
	        "    <Copyright>Generated by Signals/%s</Copyright>\n"
	        "  </ProgramInformation>\n"
	        "  <Period id=\"1\" start=\"PT00H00M0.000S\">\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\">\n"
	        "      <SupplementalProperty schemeIdUri=\"urn:mpeg:dash:srd:2014\" value=\"1,2,3,4,5,6,7\"/>\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"0\"/>\n"
	        "      <Representation id=\"0\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"v_0_0x0/v_0_0x0-$Number$.m4s\" initialization=\"v_0_0x0/v_0_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "      <Representation id=\"1\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"v_1_0x0/v_1_0x0-$Number$.m4s\" initialization=\"v_1_0x0/v_1_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "    <AdaptationSet segmentAlignment=\"true\" bitstreamSwitching=\"true\">\n"
	        "      <SupplementalProperty schemeIdUri=\"urn:mpeg:dash:srd:2014\" value=\"3,4,5,6,7,8,9\"/>\n"
	        "      <SegmentTemplate timescale=\"1000\" duration=\"%s\" startNumber=\"0\"/>\n"
	        "      <Representation id=\"2\" bandwidth=\"0\" mimeType=\"\" codecs=\"\" startWithSAP=\"1\">\n"
	        "        <SegmentTemplate media=\"v_2_0x0/v_2_0x0-$Number$.m4s\" initialization=\"v_2_0x0/v_2_0x0-init.mp4\" startNumber=\"0\"/>\n"
	        "      </Representation>\n"
	        "    </AdaptationSet>\n"
	        "  </Period>\n"
	        "</MPD>\n",
	        g_version, segmentDurationInMs, segmentDurationInMs);

	ASSERT_EQUALS(expectedMpd, mpdAnalyzer->mpd);
}
