#include "lib_utils/tools.hpp"
#include "../common/metadata.hpp" // MetadataPkt
#include "mpeg_dash_input.hpp"
#include <vector>
#include <map>
#include <cstring> // memcpy

std::string expandVars(std::string input, std::map<std::string,std::string> const& values);

using namespace std;
using namespace Modules::In;

struct AdaptationSet {
	string media;
	int startNumber=0;
	string representationId;
	string codecs;
	string initialization;
	string mimeType;
};

struct DashMpd {
	vector<AdaptationSet> sets;
};

static DashMpd parseMpd(std::string text);

namespace Modules {
namespace In {

static string dirName(string path) {
	auto i = path.rfind('/');
	if(i != path.npos)
		path = path.substr(0, i);
	return path;
}

MPEG_DASH_Input::MPEG_DASH_Input(std::unique_ptr<IFilePuller> source, std::string const& url) : m_source(move(source)) {
	//GET MPD FROM HTTP
	auto mpdAsText = m_source->get(url);
	m_mpdDirname = dirName(url);

	//PARSE MPD
	mpd = make_unique<DashMpd>();
	*mpd = parseMpd(string(mpdAsText.begin(), mpdAsText.end()));

	//DECLARE OUTPUT PORTS
	for(auto& set : mpd->sets) {
		shared_ptr<MetadataPkt> meta;
		if(set.mimeType == "audio/mp4") {
			meta = make_shared<MetadataPktAudio>();
		} else if(set.mimeType == "video/mp4") {
			meta = make_shared<MetadataPktVideo>();
		} else {
			Log::msg(Warning, "Ignoring adaptation set with unrecognized mime type: '%s'", set.mimeType);
			continue;
		}

		auto output = addOutput<OutputDefault>();
		output->setMetadata(meta);
	}
}

MPEG_DASH_Input::~MPEG_DASH_Input() {
}

void MPEG_DASH_Input::process() {
	while(wakeUp()) {
	}
}

bool MPEG_DASH_Input::wakeUp() {
	int i=0;
	for(auto& set : mpd->sets) {

		string url;
		map<string, string> vars;

		vars["RepresentationID"] = set.representationId;

		if(m_initializationChunkSent) {
			vars["Number"] = format("%s", set.startNumber);
			set.startNumber++;
			url = m_mpdDirname + "/" + expandVars(set.media, vars);
		} else {
			url = m_mpdDirname + "/" + expandVars(set.initialization, vars);
		}
		Log::msg(Debug, "wget: '%s'", url);

		auto chunk = m_source->get(url);
		if(chunk.empty()) {
			Log::msg(Debug, "end of stream");
			return false;
		}

		auto data = make_shared<DataRaw>(chunk.size());
		memcpy(data->data(), chunk.data(), chunk.size());
		outputs[i]->emit(data);
		++i;
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
				mpd->sets.push_back(set);
			} else if(name == "SegmentTemplate") {
				auto& set = mpd->sets.back();
				set.initialization = attr["initialization"];
				set.media = attr["media"];
				set.startNumber = atoi(attr["startNumber"].c_str());
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

extern "C" {
#include <curl/curl.h>
}

struct HttpSource : IFilePuller {

	std::vector<uint8_t> get(std::string url) override {
		struct HttpContext {
			std::vector<uint8_t> data;

			static size_t callback(void *stream, size_t size, size_t nmemb, void *ptr) {
				auto pThis = (HttpContext*)ptr;
				auto const bytes = size * nmemb;
				pThis->onReceiveBuffer(bytes, (uint8_t*)stream);
				return bytes;
			}

			void onReceiveBuffer(size_t size, uint8_t *stream) {
				for(size_t i=0; i < size; ++i)
					data.push_back(stream[i]);
			}
		};

		auto curl = curl_easy_init();
		if(!curl)
			throw std::runtime_error("can't init curl");

		HttpContext ctx;

		// some servers require a user-agent field
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &HttpContext::callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

		auto res = curl_easy_perform(curl);
		if(res != CURLE_OK)
			throw std::runtime_error(std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res));

		curl_easy_cleanup(curl);
		return ctx.data;
	}
};

std::unique_ptr<IFilePuller> createHttpSource() {
	return make_unique<HttpSource>();
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
