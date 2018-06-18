#include "lib_utils/tools.hpp"
#include "lib_utils/time.hpp"
#include "../common/metadata.hpp" // MetadataPkt
#include "mpeg_dash_input.hpp"
#include <vector>
#include <map>
#include <cstring> // memcpy

std::string expandVars(std::string input, std::map<std::string,std::string> const& values);
int64_t parseIso8601Period(std::string input);

using namespace std;
using namespace Modules::In;

struct AdaptationSet {
	string media;
	int startNumber=0;
	int duration=0;
	int timescale=1;
	string representationId;
	string codecs;
	string initialization;
	string mimeType;
	string contentType;
};

struct DashMpd {
	bool dynamic = false;
	int64_t availabilityStartTime = 0; // in ms
	int64_t periodDuration = 0; // in seconds
	vector<AdaptationSet> sets;
};

static DashMpd parseMpd(std::string text);

namespace Modules {
namespace In {

struct MPEG_DASH_Input::Stream {
	OutputDefault* out = nullptr;
	AdaptationSet* set = nullptr;
	int currNumber = 0;
	Fraction segmentDuration;
};

static string dirName(string path) {
	auto i = path.rfind('/');
	if(i != path.npos)
		path = path.substr(0, i);
	return path;
}

static shared_ptr<IMetadata> createMetadata(AdaptationSet const& set) {
	if(set.mimeType == "audio/mp4" || set.contentType == "audio") {
		return make_shared<MetadataPktAudio>();
	} else if(set.mimeType == "video/mp4" || set.contentType == "video") {
		return make_shared<MetadataPktVideo>();
	} else {
		Log::msg(Warning, "Ignoring adaptation set with unrecognized mime type: '%s'", set.mimeType);
		return nullptr;
	}
}

MPEG_DASH_Input::MPEG_DASH_Input(std::unique_ptr<IFilePuller> source, std::string const& url) : m_source(move(source)) {
	//GET MPD FROM HTTP
	auto mpdAsText = m_source->get(url);
	if(mpdAsText.empty())
		throw std::runtime_error("can't get mpd");
	m_mpdDirname = dirName(url);

	//PARSE MPD
	mpd = make_unique<DashMpd>();
	*mpd = parseMpd(string(mpdAsText.begin(), mpdAsText.end()));

	//DECLARE OUTPUT PORTS
	for(auto& set : mpd->sets) {
		auto meta = createMetadata(set);
		if(!meta)
			continue;

		auto stream = make_unique<Stream>();
		stream->out = addOutput<OutputDefault>();
		stream->out->setMetadata(meta);
		stream->set = &set;
		stream->segmentDuration = Fraction(set.duration, set.timescale);

		m_streams.push_back(move(stream));
	}

	for(auto& stream : m_streams) {
		stream->currNumber  = stream->set->startNumber;
		if(mpd->dynamic) {
			auto now = (int64_t)getUTC();
			stream->currNumber += int(stream->segmentDuration.inverse() * (now - mpd->availabilityStartTime));
			// HACK: add one segment latency
			stream->currNumber -= 2;
		}
	}
}

MPEG_DASH_Input::~MPEG_DASH_Input() {
}

void MPEG_DASH_Input::process() {
	while(wakeUp()) {
	}
}

bool MPEG_DASH_Input::wakeUp() {
	for(auto& stream : m_streams) {
		auto& set = *stream->set;

		if(mpd->periodDuration) {
			if(stream->segmentDuration * (stream->currNumber - set.startNumber) >= mpd->periodDuration) {
				Log::msg(Info, "End of period");
				return false;
			}
		}

		string url;

		{
			map<string, string> vars;

			vars["RepresentationID"] = set.representationId;

			if(m_initializationChunkSent) {
				vars["Number"] = format("%s", stream->currNumber);
				stream->currNumber++;
				url = m_mpdDirname + "/" + expandVars(set.media, vars);
			} else {
				url = m_mpdDirname + "/" + expandVars(set.initialization, vars);
			}
		}

		Log::msg(Debug, "wget: '%s'", url);

		auto chunk = m_source->get(url);
		if(chunk.empty()) {
			if(mpd->dynamic) {
				stream->currNumber--; // too early, retry
				continue;
			}
			Log::msg(Error, "can't download file: '%s'", url);
			return false;
		}

		auto data = make_shared<DataRaw>(chunk.size());
		memcpy(data->data(), chunk.data(), chunk.size());
		stream->out->emit(data);
	}

	m_initializationChunkSent = true;
	return true;
}

}
}

///////////////////////////////////////////////////////////////////////////////
// nothing above this line should depend upon gpac

void enforce(bool condition, char const* msg) {
	if(condition)
		return;
	throw std::runtime_error(msg);
}

extern "C" {
#include <gpac/xml.h>
}

DashMpd parseMpd(std::string text) {
	GF_Err err;

	struct Context {

		DashMpd* mpd;

		static
		void onNodeStartCallback(void* user, const char* name, const char* namespace_, const GF_XMLAttribute *attributes, u32 nb_attributes) {
			(void)namespace_;
			auto pThis = (Context*)user;

			map<string,string> attr;
			for(int i=0; i < (int)nb_attributes; ++i) {
				auto& attribute = attributes[i];
				attr[attribute.name] = attribute.value;
			}

			pThis->onNodeStart(name, attr);
		}

		void onNodeStart(std::string name, map<string, string>& attr) {
			if(name == "AdaptationSet") {
				AdaptationSet set;
				set.contentType = attr["contentType"];
				mpd->sets.push_back(set);
			} else if(name == "Period") {
				if(!attr["duration"].empty())
					mpd->periodDuration = parseIso8601Period(attr["duration"]);
			} else if(name == "MPD") {
				mpd->dynamic = attr["type"] == "dynamic";
			} else if(name == "SegmentTemplate") {
				auto& set = mpd->sets.back();
				set.initialization = attr["initialization"];
				set.media = attr["media"];
				set.startNumber = atoi(attr["startNumber"].c_str());
				set.duration = atoi(attr["duration"].c_str());
				if(!attr["timescale"].empty())
					set.timescale = atoi(attr["timescale"].c_str());
			} else if(name == "Representation") {
				auto& set = mpd->sets.back();
				set.representationId = attr["id"];
				set.codecs = attr["codecs"];
				set.mimeType = attr["mimeType"];
			}
		}
	};

	DashMpd r {};
	Context ctx { &r };

	auto parser = gf_xml_sax_new(&Context::onNodeStartCallback, nullptr, nullptr, &ctx);
	enforce(parser, "XML parser creation failed");

	err = gf_xml_sax_init(parser, nullptr);
	enforce(!err, "XML parser init failed");

	err = gf_xml_sax_parse(parser, text.c_str());
	enforce(!err, "XML parsing failed");

	gf_xml_sax_del(parser);

	return  r;
}

///////////////////////////////////////////////////////////////////////////////
// move this elsewhere

// usage example:
// map<string, string> vars;
// vars["Name"] = "john";
// assert("hello john" == expandVars("hello $Name$", vars));
string expandVars(string input, map<string,string> const& values) {

	int i=0;
	auto front = [&]() {
		return input[i];
	};
	auto pop = [&]() {
		return input[i++];
	};
	auto empty = [&]() {
		return i >= (int)input.size();
	};

	auto parseVarName = [&]() -> string {
		auto leadingDollar = pop();
		assert(leadingDollar == '$');

		string name;
		while(!empty() && front() != '$') {
			name += pop();
		}

		if(empty())
			throw runtime_error("unexpected end of string found when parsing variable name");

		pop(); // pop terminating '$'
		return name;
	};

	string r;

	while(!empty()) {
		auto const head = front();
		if(head == '$') {
			auto name = parseVarName();
			if(values.find(name) == values.end())
				throw runtime_error("unknown variable name '" + name + "'");

			r += values.at(name);
		} else {
			r += pop();
		}
	}

	return r;
}

