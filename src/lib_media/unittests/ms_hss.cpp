#include "lib_media/common/metadata_file.hpp"
#include "lib_modules/modules.hpp"
#include "lib_modules/utils/loader.hpp"
#include "tests/tests.hpp"

#include <cstring> // memcpy

using namespace Modules;

namespace {

std::shared_ptr<DataBase> createPacket(span<uint8_t> contents) {
  auto meta = std::make_shared<MetadataFile>(VIDEO_PKT);
  meta->filename = "filename";
  meta->mimeType = "mimetype";
  meta->codecName = "codecName";
  meta->lang = "lang";
  meta->durationIn180k = 100;
  meta->filesize = 0;
  meta->latencyIn180k = 0;
  meta->startsWithRAP = false;
  meta->EOS = false;

  auto r = std::make_shared<DataRaw>(contents.len);
  r->setMetadata(meta);
  memcpy(r->buffer->data().ptr, contents.ptr, contents.len);
  return r;
}

}

secondclasstest("MS_HSS: simple") {
  std::string url = "http://127.0.0.1:9000";
  auto mod = loadModule("MS_HSS", &NullHost, &url);
  uint8_t data[] = {
        // 'ftyp' box
        0x00,
        0x00,
        0x00,
        0x0A,
        'f',
        't',
        'y',
        'p',
        0x77,
        0x77,

        // 'evil' box
        0x00,
        0x00,
        0x00,
        0x0C,
        'e',
        'v',
        'i',
        'l',
        0x66,
        0x66,
        0x66,
        0x66,

        // 'free' box
        0x00,
        0x00,
        0x00,
        0x0A,
        'f',
        'r',
        'e',
        'e',
        0x55,
        0x55,

        // 'moov' box
        0x00,
        0x00,
        0x00,
        0x10,
        'm',
        'o',
        'o',
        'v',
        0x11,
        0x22,
        0x22,
        0x22,
        0x22,
        0x22,
        0x22,
        0x33,

        // actual data
        0xAA,
        0xAA,
        0xAA,
        0xAA,
        0xFF,
  };
  mod->getInput(0)->push(createPacket(data));
  mod->getInput(0)->push(createPacket(data));
  mod->getInput(0)->push(createPacket(data));
  // mod->flush();
}
