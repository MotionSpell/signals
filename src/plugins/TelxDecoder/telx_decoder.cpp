#include "telx_decoder.hpp"

#include "lib_media/common/attributes.hpp"
#include "lib_media/common/metadata.hpp"
#include "lib_media/common/subtitle.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp" // Debug
#include "lib_utils/tools.hpp" // enforce

#include "telx.hpp"

using namespace Modules;

namespace {

class TeletextDecoder : public ModuleS {
  public:
  TeletextDecoder(KHost *host, TeletextDecoderConfig *cfg)
      : m_host(host) {
    m_telxState.reset(createTeletextParser(host, cfg->pageNum));
    output = addOutput();
    output->setMetadata(make_shared<MetadataRawSubtitle>());
  }

  void processOne(Data data) override {
    // TODO
    // 14. add flush() for ondemand samples
    // 15. UTF8 to TTML formatting? accent

    for(auto &page : m_telxState->parse(data->data(), data->get<PresentationTime>().time)) {
      m_host->log(Debug,
            format("show=%s:hide=%s, clocks:data=%s, content=%s", clockToTimescale(page.showTimestamp, 1000),
                  clockToTimescale(page.hideTimestamp, 1000),
                  clockToTimescale(data->get<PresentationTime>().time, 1000), page.toString())
                  .c_str());

      dispatch(page);
    }
  }

  private:
  KHost *const m_host;
  OutputDefault *output;
  std::unique_ptr<ITeletextParser> m_telxState;

  void dispatch(Page &page) {
    auto out = output->allocData<DataSubtitle>(0);
    out->set(DecodingTime{page.showTimestamp});
    out->set(PresentationTime{page.showTimestamp});
    out->page = page;

    CueFlags flags{};
    flags.keyframe = true;
    out->set(flags);

    output->post(out);
  }
};

IModule *createObject(KHost *host, void *va) {
  auto config = (TeletextDecoderConfig *)va;
  enforce(host, "TeletextDecoder: host can't be NULL");
  enforce(config, "TeletextDecoder: config can't be NULL");
  return createModule<TeletextDecoder>(host, config).release();
}

auto const registered = Factory::registerModule("TeletextDecoder", &createObject);
}
