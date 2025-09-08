// holds the chain: [dash downloader] => ( [mp4demuxer] )*
#include "lib_media/demux/dash_demux.hpp"

#include "lib_media/in/mpeg_dash_input.hpp"
#include "lib_media/out/null.hpp"
#include "lib_modules/core/connection.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/loader.hpp"
#include "lib_utils/tools.hpp" // enforce

std::unique_ptr<Modules::In::IFilePuller> createHttpSource();

using namespace Modules;
using namespace In;

namespace {

class DashDemuxer : public Module {
  public:
  DashDemuxer(KHost *host, DashDemuxConfig *cfg)
      : m_host(host) {
    m_downloader = createModule<MPEG_DASH_Input>(m_host, &filePullerFactory, cfg->url);

    for(int i = 0; i < m_downloader->getNumOutputs(); ++i)
      addStream(m_downloader->getOutput(i));

    if(cfg->adaptationControlCbk)
      cfg->adaptationControlCbk(m_downloader.get());
  }

  void process() override { m_downloader->process(); }

  private:
  struct FilePullerFactory : IFilePullerFactory {
    std::unique_ptr<IFilePuller> create() override { return createHttpSource(); }
  };

  KHost *const m_host;
  FilePullerFactory filePullerFactory;

  void addStream(IOutput *downloader) {
    // add MP4 box-splitter
    std::shared_ptr<IModule> splitter = loadModule("Fmp4Splitter", m_host, nullptr);
    modules.push_back(splitter);
    ConnectOutputToInput(downloader, splitter->getInput(0));

    // create our own output
    auto output = addOutput();
    output->setMetadata(downloader->getMetadata());

    auto deliver = [output](Data data) { output->post(data); };

    ConnectOutput(splitter->getOutput(0), deliver);
  }

  std::vector<std::shared_ptr<IModule>> modules;
  std::unique_ptr<MPEG_DASH_Input> m_downloader;
};

IModule *createObject(KHost *host, void *va) {
  auto config = (DashDemuxConfig *)va;
  enforce(host, "DashDemuxer: host can't be NULL");
  enforce(config, "DashDemuxer: config can't be NULL");
  return createModule<DashDemuxer>(host, config).release();
}

auto const registered = Factory::registerModule("DashDemuxer", &createObject);
}
