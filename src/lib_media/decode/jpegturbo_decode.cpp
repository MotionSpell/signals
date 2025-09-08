#include "../common/metadata.hpp"
#include "../common/picture.hpp"
#include "lib_media/common/attributes.hpp"
#include "lib_modules/utils/factory.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_utils/log_sink.hpp"
#include "lib_utils/tools.hpp"

extern "C" {
#include <turbojpeg.h>
}

using namespace Modules;

namespace {

class JPEGTurboDecode : public ModuleS {
  public:
  JPEGTurboDecode(KHost *host);
  ~JPEGTurboDecode();
  void processOne(Data data) override;

  private:
  KHost *const m_host;
  OutputDefault *output;
  void ensureMetadata(int width, int height, int pixelFmt);
  tjhandle jtHandle;
};

JPEGTurboDecode::JPEGTurboDecode(KHost *host_)
    : m_host(host_)
    , jtHandle(tjInitDecompress()) {
  input->setMetadata(make_shared<MetadataPkt>(VIDEO_PKT));

  output = addOutput();
  output->setMetadata(make_shared<MetadataRawVideo>());
}

JPEGTurboDecode::~JPEGTurboDecode() { tjDestroy(jtHandle); }

void JPEGTurboDecode::ensureMetadata(int /*width*/, int /*height*/, int /*pixelFmt*/) {
  if(!output->getMetadata()) {
    auto p = safe_cast<const MetadataRawVideo>(output->getMetadata());
    // TODO: add resolution and pixel format to MetadataRawVideo
    // ctx->width = width;
    // ctx->height = height;
    // ctx->pix_fmt = getAVPF(pixelFmt);
    // output->setMetadata(new MetadataRawVideo(ctx));
  }
}

void JPEGTurboDecode::processOne(Data data) {
  const int pixelFmt = TJPF_RGB;
  int w = 0, h = 0, jpegSubsamp = 0;
  auto buf = (const unsigned char *)data->data().ptr;
  auto size = (unsigned long)data->data().len;
  if(tjDecompressHeader2(jtHandle, (unsigned char *)buf, size, &w, &h, &jpegSubsamp) < 0) {
    m_host->log(Warning, "error encountered while decompressing header.");
    return;
  }
  auto out = output->allocData<DataPicture>(Resolution(w, h), PixelFormat::RGB24);
  if(tjDecompress2(jtHandle, buf, size, out->buffer->data().ptr, w, 0 /*pitch*/, h, pixelFmt, TJFLAG_FASTDCT) < 0) {
    m_host->log(Warning, "error encountered while decompressing frame.");
    return;
  }
  ensureMetadata(w, h, pixelFmt);
  out->set(data->get<PresentationTime>());
  output->post(out);
}

IModule *createObject(KHost *host, void *va) {
  (void)va;
  enforce(host, "JPEGTurboDecode: host can't be NULL");
  return createModule<JPEGTurboDecode>(host).release();
}

auto const registered = Factory::registerModule("JPEGTurboDecode", &createObject);
}
