#pragma once

#include "lib_utils/format.hpp"
#include <lib_utils/log.hpp>

#include <stdexcept>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/frame.h>
}

namespace ffpp {

class Frame {
  public:
  Frame() {
    avFrame = av_frame_alloc();
    if(!avFrame)
      throw std::runtime_error("Frame allocation failed");
  }
  ~Frame() { av_frame_free(&avFrame); }
  AVFrame *get() { return avFrame; }

  private:
  AVFrame *avFrame;
};

class Dict {
  public:
  Dict(const std::string &moduleName, const std::string &options)
      : avDict(nullptr)
      , options(options)
      , moduleName(moduleName) {
    if(av_dict_parse_string(&avDict, options.c_str(), " ", " ", 0) != 0) {
      g_Log->log(Warning, format("[%s] could not parse option list \"%s\".", moduleName, options).c_str());
    }

    AVDictionaryEntry *avde = nullptr;
    while((avde = av_dict_get(avDict, "", avde, AV_DICT_IGNORE_SUFFIX))) {
      if(avde->key[0] == '-') {
        for(size_t i = 1; i <= strlen(avde->key); ++i) {
          avde->key[i - 1] = avde->key[i];
        }
      }
      g_Log->log(
            Debug, format("[%s] detected option \"%s\", value \"%s\".", moduleName, avde->key, avde->value).c_str());
    }

    if(av_dict_copy(&avDictOri, avDict, 0) != 0)
      throw(format("[%s] could not copy options for \"%s\".", moduleName, options));
  }

  ~Dict() {
    ensureAllOptionsConsumed();
    av_dict_free(&avDict);
    av_dict_free(&avDictOri);
  }

  AVDictionaryEntry *get(std::string const name) const { return av_dict_get(avDict, name.c_str(), nullptr, 0); }

  AVDictionary **operator&() { return &avDict; }

  void ensureAllOptionsConsumed() const {
    AVDictionaryEntry *avde = nullptr;
    while((avde = av_dict_get(avDictOri, "", avde, AV_DICT_IGNORE_SUFFIX))) {
      if(get(avde->key)) {
        g_Log->log(Warning, format("codec option \"%s\", value \"%s\" was ignored.", avde->key, avde->value).c_str());
      }
    }
  }

  private:
  AVDictionary *avDict = nullptr, *avDictOri = nullptr;
  std::string options, moduleName;
};

}
