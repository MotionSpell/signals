#include "ttml_decoder.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/tools.hpp" // enforce
#include "lib_utils/log_sink.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp" //MetadataRawSubtitle
#include "lib_media/common/subtitle.hpp"
#include "lib_utils/sax_xml_parser.hpp"
#include "lib_utils/xml.hpp"

using namespace Modules;

namespace {
Tag parseXml(span<const char> text) {
	Tag root;
	std::vector<Tag*> tagStack = { &root };

	auto onNodeStart = [&](std::string name, SmallMap<std::string, std::string> &attr) {
		Tag tag{name};

		for (auto &a : attr)
			tag.attr.push_back({a.key, a.value});

		tagStack.back()->add(tag);
		tagStack.push_back(&tagStack.back()->children.back());
	};

	auto onNodeEnd = [&](std::string /*id*/, std::string content) {
		tagStack.back()->content = content;
		tagStack.pop_back();
	};

	saxParse(text, onNodeStart, onNodeEnd);

	if (tagStack.front()->children.size() != 1)
		throw std::runtime_error("XML parsing error");

	return tagStack.front()->children[0];
}

void explore(const Tag& tag, std::function<void(const Tag&)> processTag) {
	processTag(tag);

	for (auto &child: tag.children)
		explore(child, processTag);
}

class TTMLDecoder : public ModuleS {
	public:
		TTMLDecoder(KHost* host, TtmlDecoderConfig* cfg)
			: m_host(host), m_clock(cfg->clock) {
			enforce(cfg->clock != nullptr, "TTMLDecoder: clock can't be NULL");
			output = addOutput();
			output->setMetadata(make_shared<MetadataRawSubtitle>());
		}

		void processOne(Data data) override {
			if(0)
				m_host->log(Warning, std::string((const char*)data->data().ptr, data->data().len).c_str());

			auto document = parseXml({ (const char*)data->data().ptr, data->data().len });
			if(document.name != "tt" && document.name != "tt:tt")
				throw error("Not a TTML document");

			int64_t pageMaxDuration = 30 * IClock::Rate; //default: 30s
			int numCols = 32, numRows = 15; // default in TTML
			SmallMap<std::string, Page::Style> styles;

			explore(document, [&](const Tag& tag) {
				if (tag.name == "tt" || tag.name == "tt:tt")
					for (auto &attr : tag.attr)
						if (attr.name == "cellResolution" || attr.name == "ttp:cellResolution") {
							int ret = sscanf(attr.value.c_str(), "%d %d", &numCols, &numRows);
							if (ret != 2)
								m_host->log(Warning, format("Incorrect parsing of attribute %s=\"%s\" into \"%d %d\" (%d elements parsed)",
								        attr.name, attr.value, numCols, numRows, ret).c_str());
						}

				if (tag.name == "style" || tag.name == "tt:style") {
					std::string id;
					Page::Style style;

					for (auto &attr : tag.attr) {
						if (attr.name == "xml:id")
							id = attr.value;
						else if (attr.name == "tts:fontFamily")
							style.fontFamily = attr.value;
						else if (attr.name == "tts:fontSize")
							style.fontSize = attr.value;
						else if (attr.name == "tts:lineHeight")
							style.lineHeight = attr.value;
						else if (attr.name == "tts:color")
							style.color = attr.value;
						else if (attr.name == "tts:backgroundColor")
							style.bgColor = attr.value;
						else if (attr.name == "ebutts:linePadding")
							m_host->log(Debug, format("Ignored attribute %s", attr.name).c_str());
						else if (attr.name == "tts:textAlign")
							m_host->log(Debug, format("Ignored attribute %s", attr.name).c_str());
						else
							m_host->log(Warning, format("Unknown attribute %s: please report to your vendor", attr.name).c_str());
					}

					styles[id] = style;
				}

				if (tag.name == "body" || tag.name == "tt:body")
					for (auto &attr : tag.attr)
						if (attr.name == "dur") {
							int hour = 0, min = 0, sec = 0, ms = 0;
							auto const parsed = sscanf(attr.value.c_str(), "%02d:%02d:%02d.%03d", &hour, &min, &sec, &ms);

							// peculiar case with UIP when the duration is one number, expected to be seconds
							if (parsed == 1) {
								sec = hour;
								hour = 0;
							}

							pageMaxDuration = timescaleToClock(((hour + min * 60) * 60 + sec) * 1000 + ms, 1000);
						}
			});

			Page page;
			page.hideTimestamp = pageMaxDuration;
			page.numCols = numCols;
			page.numRows = numRows;

			explore(document, [&](const Tag& tag) {
				if (tag.name == "div" || tag.name == "tt:div") {
					Page::Style divStyle;
					// find style
					for (auto &attr : tag.attr)
						if (attr.name == "style")
							divStyle = styles[attr.value];

					for (auto &tagDiv: tag.children) {
						if (tagDiv.name == "p" || tagDiv.name == "tt:p") {
							Page::Style pStyle = divStyle;
							Page::Region pRegion;
							for (auto &attr : tagDiv.attr) {
								if (attr.name == "style")
									pStyle.merge(styles[attr.value]);
								//TODO: handle regions
								//else if (attr.name == "region")
								//	pStyle.color = regions[attr.value];
							}

							for (auto &tagP: tagDiv.children) {
								if (tagP.name == "span" || tagP.name == "tt:span") {
									Page::Style spanStyle = pStyle;
									for (auto &attr : tagP.attr)
										if (attr.name == "style")
											spanStyle.merge(styles[attr.value]);
										else if (attr.name == "tts:color")
											spanStyle.color = attr.value;

									Page::Line line;
									line.region = pRegion;
									line.style = spanStyle;

									// rectify layout to avoid overwrites
									for (auto &line : page.lines)
										line.region.row--;

									line.text = tagP.content;
									page.lines.push_back(line);
								}
							}
						}

					}
				}
			});

			if (page.lines.empty())
				return;

			sendSample(page);
		}

		void sendSample(Page& page) {
			auto const now = fractionToClock(m_clock->now());
			page.showTimestamp = now;
			page.hideTimestamp += now;

			auto out = output->allocData<DataSubtitle>(0);
			out->set(PresentationTime{now});
			out->page = page;

			CueFlags flags {};
			flags.keyframe = true;
			out->set(flags);

			output->post(out);
		}

	private:
		KHost* const m_host;
		std::shared_ptr<IClock> const m_clock;
		OutputDefault* output;
};

IModule* createObject(KHost* host, void* va) {
	auto config = (TtmlDecoderConfig*)va;
	enforce(host, "TTMLDecoder: host can't be NULL");
	enforce(config, "TTMLDecoder: config can't be NULL");
	return createModule<TTMLDecoder>(host, config).release();
}

auto const registered = Factory::registerModule("TTMLDecoder", &createObject);
}
