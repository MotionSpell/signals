#include <cassert>
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp" // enforce
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/attributes.hpp"
#include <algorithm> // std::find

using namespace Modules;

namespace {

struct rawAac2Adts : ModuleS {
		rawAac2Adts() {
			output = addOutput();
		}
		void processOne(Data in) override {
			if (isDeclaration(in))
				return;

			auto out = output->allocData<DataRaw>(7 + in->data().len);

			out->copyAttributes(*in);

			// write ADTS header
			auto bytes = out->buffer->data().ptr;
			*bytes++ = 0xff;
			*bytes++ = 0xf1;
			*bytes++ = 0x50;
			*bytes++ = 0x40;
			*bytes++ = 0x2c;
			*bytes++ = 0x1f;
			*bytes++ = 0xfc;

			memcpy(out->buffer->data().ptr + 7, in->data().ptr, in->data().len);

			out->setMetadata(in->getMetadata());
			output->post(out);
		}

	private:
		OutputDefault* output;
};

template<size_t N>
constexpr uint32_t FOURCC(const char (&a)[N]) {
	static_assert(N == 5, "FOURCC must be composed of 4 characters");
	return (a[0]<<24) | (a[1]<<16) | (a[2]<<8) | a[3];
}

int64_t readBytes(SpanC& s, int bytes) {
	int64_t r = 0;
	for(int i=0; i < bytes; ++i) {
		r <<= 8;
		r |= s[0];
		s += 1;
	}

	return r;
}

struct TopLevelBoxSeparator {
		std::function<void(SpanC)> m_onBox;

		void process(Data data) {
			for(auto byte : data->data())
				pushByte(byte);
		}

	private:
		void pushByte(uint8_t byte) {
			currData.push_back(byte);

			if(insideHeader) {
				if(headerBytes < 4) {
					boxBytes <<= 8;
					boxBytes |= byte;
				} else {
					boxFourcc <<= 8;
					boxFourcc |= byte;
				}
				// reading header
				headerBytes++;
				assert(headerBytes <= 8);
				if(headerBytes == 8) {
					if(boxBytes > 8) {
						boxBytes -= 8;
						insideHeader = false;
					} else {
						boxBytes = 0;
					}

					headerBytes = 0;
				}
			} else {
				assert(boxBytes > 0);
				boxBytes--;

				// is the current box complete?
				if(boxBytes == 0) {

					{
						// flush current box
						m_onBox({currData.data(), currData.size()});
						currData.clear();
					}

					// go back to 'header' state
					boxBytes = 0;
					insideHeader = true;
				}
			}
		}

		std::vector<uint8_t> currData; // box buffer.
		uint32_t boxFourcc = 0;
		int insideHeader = true;
		int headerBytes = 0;
		int64_t boxBytes = 0;
};

struct BoxBrowser {
	SpanC data; // read pointer. Points to the 32-bit size of the current box
	uint32_t fourcc() const {
		return (data[4]<<24) | (data[5]<<16) | (data[6]<<8) | (data[7]<<0);
	}
	size_t size() const {
		return (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | (data[3]<<0);
	}
	SpanC contents() const {
		return { data.ptr + 8, size() - 8 };
	}
	BoxBrowser firstChild() const {
		auto subData = this->data;
		subData += 8;
		return { subData };
	}
	BoxBrowser nextSibling() const {
		auto subData = this->data;
		subData += size();
		return {subData};
	}
	bool hasSibling() const {
		return data.len > size();
	}
	BoxBrowser child(uint32_t fourcc) {
		auto r = firstChild();
		while(r.fourcc() != fourcc) {
			if(!r.hasSibling())
				throw std::runtime_error("invalid mp4: box not found");
			r = r.nextSibling();
		}
		return r;
	}
};

// reconstruct framing of top-level MP4 boxes
struct Fmp4Splitter : ModuleS {
	Fmp4Splitter(KHost* host)
		: m_host(host) {
		output = addOutput();
		m_separator.m_onBox = std::bind(&Fmp4Splitter::processTopLevelBox, this, std::placeholders::_1);
	}

	void processOne(Data data) override {
		m_separator.process(data);
	}

	void processTopLevelBox(SpanC data) {
		auto parser = BoxBrowser { data };
		switch(parser.fourcc()) {
		case FOURCC("moov"):
			processMoov(parser);
			break;
		case FOURCC("moof"):
			processMoof(parser);
			break;
		case FOURCC("mdat"):
			m_dataOffset -= 8;
			enforce(m_dataOffset >= 0, "Data referred to by data-offset must be inside an 'mdat' box");
			auto contents = parser.contents();
			contents += m_dataOffset;

			for(auto sample : m_samples) {
				enforce((int)contents.len >= sample.size, "Each sample must fit into the 'mdat' box");

				auto out = output->allocData<DataRaw>(sample.size);
				memcpy(out->buffer->data().ptr, contents.ptr, sample.size);
				out->set(PresentationTime { timescaleToClock(m_decodeTime + sample.cts, m_timescale) });

				CueFlags flags {};
				flags.keyframe = true;
				out->set(flags);

				out->setMetadata(createMetadata());

				ensureConverter();

				if (converter)
					converter->getInput(0)->push(out);
				else
					output->post(out);

				m_decodeTime += sample.duration;
				contents += sample.size;
			}

			m_samples.clear();
			break;
		}

		// update 'dataOffset' so it will be relative to the next box
		m_dataOffset -= data.len;
	}

	void ensureConverter() {
		if (converter)
			return;

		switch (m_codecFourcc) {
		case FOURCC("mp4a"): {
			// add ADTS header
			converter = createModule<rawAac2Adts>();
			auto deliver = [this](Data data) {
				output->post(data);
			};
			ConnectOutput(converter->getOutput(0), deliver);
			break;
		}
		case FOURCC("avc1"): {
			// convert to Annex B
			converter = loadModule("AVCC2AnnexBConverter", m_host, nullptr);

			auto deliver = [this](Data data) {
				output->post(data);
			};
			ConnectOutput(converter->getOutput(0), deliver);
			break;
		}
		default: {
		}
		}
	}

	std::shared_ptr<MetadataPkt> createMetadata() {
		std::shared_ptr<MetadataPkt> meta;

		switch(m_codecFourcc) {
		case FOURCC("mp4a"): {
			meta = make_shared<MetadataPkt>(AUDIO_PKT);
			meta->codec = "aac_raw";
			break;
		}
		case FOURCC("avc1"): {
			meta = make_shared<MetadataPkt>(VIDEO_PKT);
			meta->codec = "h264_avcc";
			break;
		}
		default: {
			char msg[256];
			snprintf(msg, sizeof msg, "Unknown SampleEntry: %c%c%c%c",
			    (m_codecFourcc>>24)&0xff,
			    (m_codecFourcc>>16)&0xff,
			    (m_codecFourcc>>8)&0xff,
			    (m_codecFourcc>>0)&0xff);
			if (std::find(unknown4CCs.begin(), unknown4CCs.end(), m_codecFourcc) == unknown4CCs.end()) {
				m_host->log(Warning, msg);
				unknown4CCs.push_back(m_codecFourcc);
			}
			meta = make_shared<MetadataPkt>(VIDEO_PKT);
			snprintf(msg, sizeof msg, "%c%c%c%c",
			    (m_codecFourcc>>24)&0xff,
			    (m_codecFourcc>>16)&0xff,
			    (m_codecFourcc>>8)&0xff,
			    (m_codecFourcc>>0)&0xff);
			meta->codec = msg;
		}
		}

		meta->codecSpecificInfo = m_codecSpecificInfo;
		return meta;
	}

	void processMoov(BoxBrowser moov) {
		auto trak = moov.child(FOURCC("trak"));
		auto mdia = trak.child(FOURCC("mdia"));
		auto minf = mdia.child(FOURCC("minf"));
		auto stbl = minf.child(FOURCC("stbl"));
		auto stsd = stbl.child(FOURCC("stsd"));
		processStsd(stsd);

		auto mdhd = mdia.child(FOURCC("mdhd"));
		processMdhd(mdhd);
	}

	void processMdhd(BoxBrowser box) {
		auto s = box.contents();
		auto version = readBytes(s, 1);
		/* auto flags = */ readBytes(s, 3);

		/* auto creation_time = */ readBytes(s, version == 1 ? 8 : 4);
		/* auto modification_time = */ readBytes(s, version == 1 ? 8 : 4);
		m_timescale = readBytes(s, 4);
		/* auto duration = */ readBytes(s, version == 1 ? 8 : 4);
	}

	void processStsd(BoxBrowser box) {
		auto s = box.contents();
		/* auto version = */ readBytes(s, 1);
		/* auto flags = */ readBytes(s, 3);
		auto entryCount = readBytes(s, 4);
		enforce(entryCount == 1, "stsd.entry-count must be 1");

		// SampleEntry
		auto parser = BoxBrowser { s };
		m_codecFourcc = parser.fourcc();
		switch(parser.fourcc()) {
		case FOURCC("avc1"):
			processAvc1(parser);
			break;
		case FOURCC("mp4a"):
			processMp4a(parser);
			break;
		}
	}

	void processAvc1(BoxBrowser box) {
		auto s = box.contents();

		s += 8 + 16; // skip SampleEntry
		s += 4; // skip width & height
		s += 14 + 32 + 4;

		{
			auto parser = BoxBrowser { s };
			enforce(parser.fourcc() == FOURCC("avcC"), "avc1 must contain an avcC box");

			auto s = parser.contents();
			m_codecSpecificInfo.assign(s.ptr, s.ptr + s.len);
		}
	}

	void processMp4a(BoxBrowser box) {
		auto s = box.contents();

		s += 8; // skip SampleEntry
		s += 20; // skip AudioSampleEntry

		{
			auto parser = BoxBrowser { s };
			enforce(parser.fourcc() == FOURCC("esds"), "mp4a must contain an esds box");

			processEsds(parser);
		}
	}

	void processEsds(BoxBrowser box) {
		auto s = box.contents();

		s += 4; // version & flags

		while(s.len)
			processDescriptor(s);
	}

	void processDescriptor(SpanC& s) {
		auto parseVlc = [](SpanC& s) {
			int val = 0;
			int n = 4;
			while (n--) {
				int c = s[0];
				s += 1;
				val <<= 7;
				val = c & 0x7f;
				if (!(c & 0x80))
					break;
			}
			return val;
		};

		auto const tag = readBytes(s, 1);
		auto const len = parseVlc(s);

		auto sub = SpanC{s.ptr, (size_t)len};
		s += len;

		switch(tag) {
		case 0x03:
			processEsDescriptor(sub);
			break;
		case 0x04:
			processDecoderConfigDescriptor(sub);
			break;
		case 0x05:
			processDecoderSpecificInfoDescriptor(sub);
			break;
		default:
			fprintf(stderr, "unknown descriptor tag: %d\n", (int)tag);
			break;
		}
	}

	// ISO 14496-1 7.2.6.5.1
	void processEsDescriptor(SpanC& s) {
		s += 2; // ES_ID

		auto stream_dependence_flag = s[0] & 0b10000000;
		auto URL_flag = s[0] & 0b01000000;
		auto OCR_stream_flag = s[0] & 0b00100000;
		s += 1;

		if(stream_dependence_flag)
			s += 2; // dependsOn_ES_ID

		if(URL_flag)
			assert(0 && "URL_flag=1 is not implemented");

		if(OCR_stream_flag)
			s += 2; // OCR_ES_Id

		processDescriptor(s);
	}

	// ISO 14496-1 7.2.6.6.1
	void processDecoderConfigDescriptor(SpanC& s) {
		s += 13;
		processDescriptor(s);
	}

	void processDecoderSpecificInfoDescriptor(SpanC& s) {
		m_codecSpecificInfo.assign(s.ptr, s.ptr + s.len);
	}

	void processMoof(BoxBrowser box) {
		auto traf = box.child(FOURCC("traf"));
		processTfdt(traf.child(FOURCC("tfdt")));
		processTfhd(traf.child(FOURCC("tfhd")));
		processTrun(traf.child(FOURCC("trun")));
	}

	void processTfdt(BoxBrowser box) {
		auto s = box.contents();
		auto version = readBytes(s, 1);
		/* auto flags = */ readBytes(s, 3);
		m_decodeTime = readBytes(s, version == 1 ? 8 : 4); // baseMediaDecodeTime
	}

	// 8.8.7.1
	void processTfhd(BoxBrowser box) {
		auto s = box.contents();
		/* auto version = */ readBytes(s, 1);
		auto flags = readBytes(s, 3);

		/* auto track-id = */ readBytes(s, 4);

		enforce(flags & 0x20000, "default-base-is-moof must be set");
		enforce(!(flags & 0x00001), "base-data-offset must not be set");

		if(flags & 0x000002)
			readBytes(s, 4); // sample-description-index

		if(flags & 0x000008)
			m_defaultSampleDuration = readBytes(s, 4);

		if(flags & 0x000010)
			m_defaultSampleSize = readBytes(s, 4);

		if(flags & 0x000020)
			readBytes(s, 4); // default-sample-flags
	}

	void processTrun(BoxBrowser box) {
		auto s = box.contents();
		auto version = readBytes(s, 1);
		auto flags = readBytes(s, 3);
		enforce(version == 0, "trun version must be 0");
		enforce(flags & 0x1, "data-offset-present must be set");

		auto sampleCount = readBytes(s, 4);

		m_dataOffset = readBytes(s, 4);

		if(flags & 0x4)
			readBytes(s, 4); // first_sample_flags

		for(int i=0; i < sampleCount; ++i) {
			Sample sample {};
			sample.duration = flags & 0x100 ? readBytes(s, 4) : m_defaultSampleDuration;
			sample.size = flags & 0x200 ? readBytes(s, 4) : m_defaultSampleSize;

			if(flags & 0x400)
				readBytes(s, 4); // sample_flags

			if(flags & 0x800)
				sample.cts = readBytes(s, 4); // sample_composition_time_offset

			m_samples.push_back(sample);
		}
	}

	struct Sample {
		int size, duration, cts;
	};

	KHost* const m_host;
	OutputDefault* output;
	TopLevelBoxSeparator m_separator;

	std::vector<uint8_t> m_codecSpecificInfo;
	uint32_t m_codecFourcc = 0;
	std::vector<uint32_t> unknown4CCs;

	int m_dataOffset = 0;
	int m_defaultSampleSize = 0;
	int m_defaultSampleDuration = 3600;
	std::vector<Sample> m_samples;
	int64_t m_timescale = 1;
	int64_t m_decodeTime = 0;

	std::shared_ptr<IModule> converter;
};

IModule* createObject(KHost* host, void*) {
	enforce(host, "Fmp4Splitter: host can't be NULL");
	return new ModuleDefault<Fmp4Splitter>(256, host);
}

auto const registered = Factory::registerModule("Fmp4Splitter", &createObject);
}

