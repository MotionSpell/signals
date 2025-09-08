#include "video_generator.hpp"

#include "../common/attributes.hpp"
#include "../common/metadata.hpp"

#include <cassert>
#include <cstring> // memset
#include <map>

namespace {

span<const char *> getFont();

void renderChar(uint8_t *dst, size_t stride, int asciiValue) {
  auto font = getFont();

  assert(asciiValue >= 0 && asciiValue <= (int)font.len);

  auto src = font[asciiValue];

  for(int y = 0; y < 5; ++y) {
    for(int x = 0; x < 5; ++x) {
      if(src[x] == 'X')
        dst[x] = 0xFF;
    }
    src += 5;
    dst += stride;
  }
}

void renderString(uint8_t *dst, size_t stride, const char *buffer) {
  auto const hstep = 6;
  int i = 0;
  while(buffer[i]) {
    renderChar(dst, stride, buffer[i]);
    dst += hstep;
    ++i;
  }
}

void renderNumber(uint8_t *dst, size_t stride, int value) {
  char buffer[256];
  sprintf(buffer, "%03d", value);
  renderString(dst, stride, buffer);
}

void drawBox(uint8_t *dst, int stride, int x, int y, int cx, int cy) {
  dst += y * stride + x;
  while(cy-- >= 0) {
    memset(dst, 0xF0, cx);
    dst += stride;
  }
}

struct UnifiedResourceLocator {
  std::string protocol;
  std::map<std::string, std::string> bindings;
};

// parse a url of the form 'proto://name1=value1&name2=value2'
UnifiedResourceLocator parseUrl(const char *url) {
  std::map<std::string, std::string> bindings;

  auto s = url;

  auto parseWord = [&]() -> std::string {
    std::string w;
    while(*s && *s != '&' && *s != '=' && *s != ':')
      w += *s++;
    return w;
  };

  auto expect = [&](char c) {
    if(*s != c)
      throw std::runtime_error("VideoGenerator: invalid url '" + std::string(url) + "'");
    ++s;
  };

  UnifiedResourceLocator r;

  r.protocol = parseWord();

  expect(':');
  expect('/');
  expect('/');

  bool first = true;
  while(*s) {
    if(!first)
      expect('&');
    first = false;

    auto name = parseWord();
    expect('=');
    auto value = parseWord();

    r.bindings[name] = value;
  }

  return r;
}

Modules::In::VideoGenerator::Config parseConfig(const char *url) {
  auto values = parseUrl(url);

  if(values.protocol != "videogen")
    throw std::runtime_error("VideoGenerator: invalid protocol '" + values.protocol + "'");

  Modules::In::VideoGenerator::Config config{};

  auto getValue = [&](const char *name, int defaultValue) {
    auto i_value = values.bindings.find(name);
    if(i_value == values.bindings.end())
      return defaultValue;
    return atoi(i_value->second.c_str());
  };

  config.maxFrames = getValue("framecount", 0);
  config.frameRate = getValue("framerate", 25);

  return config;
}
}

namespace Modules { namespace In {

VideoGenerator::VideoGenerator(KHost *host, const char *url)
    : m_host(host)
    , config(parseConfig(url)) {
  output = addOutput();
  output->setMetadata(std::make_shared<MetadataRawVideo>());
  m_host->activate(true);
}

VideoGenerator::VideoGenerator(KHost *host, int maxFrames, int frameRate)
    : m_host(host)
    , config{maxFrames, frameRate} {
  output = addOutput();
  output->setMetadata(std::make_shared<MetadataRawVideo>());
  m_host->activate(true);
}

void VideoGenerator::process() {

  if(config.maxFrames && m_numFrames >= (uint64_t)config.maxFrames) {
    m_host->activate(false);
    return;
  }

  auto const dim = Resolution(320, 180);
  auto pic = output->allocData<DataPicture>(dim, PixelFormat::I420);

  // generate video
  auto const p = pic->buffer->data().ptr;
  auto const flash = (m_numFrames % config.frameRate) == 0;
  auto const val = flash ? 0xCC : 0x80;
  memset(p, val, pic->getSize());

  // add white noise (make the content harder to encode, to mimic real content)
  for(int i = 0; i < (int)pic->getSize(); ++i)
    p[i] += rand() % 10 - 5;

  {
    auto period = 80;
    auto phase = int(m_numFrames % period);
    auto pos = 10 + (phase < period / 2 ? phase : (period - phase)) * 2;
    drawBox(p, dim.width, 20, pos, 40, 40);
  }

  {
    auto period = 80;
    auto phase = int((m_numFrames + period / 3) % period);
    auto pos = 10 + (phase < period / 2 ? phase : (period - phase)) * 3;
    drawBox(p, dim.width, 80, pos, 40, 40);
  }

  if(dim.width > 32 && dim.height > 32) {

    auto dst = pic->getPlane(0);
    auto stride = pic->getStride(0);
    auto const margin = 3;
    dst += margin;
    dst += stride * margin;

    renderNumber(dst, stride, m_numFrames);
  }

  if(dim.width > 64 && dim.height > 32) {

    auto dst = pic->getPlane(0);
    auto stride = pic->getStride(0);
    auto const h_margin = (dim.width - 6 * 7) / 2;
    auto const v_margin = dim.height / 2;
    dst += h_margin;
    dst += stride * v_margin;

    renderString(dst, stride, "SIGNALS");
  }

  auto const framePeriodIn180k = IClock::Rate / config.frameRate;
  assert(IClock::Rate % config.frameRate == 0);
  pic->set(PresentationTime{(int64_t)m_numFrames * framePeriodIn180k});

  output->post(pic);

  ++m_numFrames;
}

}}

namespace {
span<const char *> getFont() {
  static const char *font[] = {
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        // space
        ".........................",

        // bang
        "..X.."
        "..X.."
        "..X.."
        "....."
        "..X..",

        // double-quote
        ".X.X."
        ".X.X."
        "....."
        "....."
        ".....",

        // hash
        ".X.X."
        "XXXXX"
        ".X.X."
        "XXXXX"
        ".X.X.",

        // dollar
        ".XXX."
        "X.X.."
        ".XXX."
        "..X.X"
        ".XXX.",

        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        "....X"
        "...X."
        "..X.."
        ".X..."
        "X....",

        ".XXX."
        "X...X"
        "X...X"
        "X...X"
        ".XXX.",

        "....X"
        "...XX"
        "..X.X"
        "....X"
        "....X",

        ".XXX."
        "X...X"
        "..XX."
        ".X..."
        "XXXXX",

        ".XXX."
        "X...X"
        "..XX."
        "X...X"
        ".XXX.",

        "X...X"
        "X...X"
        ".XXXX"
        "....X"
        "....X",

        "XXXXX"
        "X...."
        "XXXX."
        "....X"
        "XXXX.",

        ".XXX."
        "X...."
        "XXXX."
        "X...X"
        ".XXX.",

        "XXXXX"
        "....X"
        "....X"
        "....X"
        "....X",

        ".XXX."
        "X...X"
        ".XXX."
        "X...X"
        ".XXX.",

        ".XXX."
        "X...X"
        ".XXXX"
        "....X"
        ".XXX.",

        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        // @
        ".XXX."
        "X.X.X"
        "X.XXX"
        "X...."
        ".XXX.",

        ".XXX."
        "X...X"
        "XXXXX"
        "X...X"
        "X...X",

        "XXXX."
        "X...X"
        "XXXX."
        "X...X"
        "XXXXX",

        ".XXXX"
        "X...."
        "X...."
        "X...."
        ".XXXX",

        "XXXX."
        "X...X"
        "X...X"
        "X...X"
        "XXXX.",

        "XXXXX"
        "X...."
        "XXX.."
        "X...."
        "XXXXX",

        "XXXXX"
        "X...."
        "XXX.."
        "X...."
        "X....",

        ".XXXX"
        "X...."
        "X..XX"
        "X...X"
        ".XXXX",

        "X...X"
        "X...X"
        "XXXXX"
        "X...X"
        "X...X",

        "XXXXX"
        "..X.."
        "..X.."
        "..X.."
        "XXXXX",

        "XXXXX"
        "...X."
        "...X."
        "...X."
        "XXX..",

        "X..X."
        "X.X.."
        "XX..."
        "X.X.."
        "X..X.",

        "X...."
        "X...."
        "X...."
        "X...."
        "XXXXX",

        "X...X"
        "XX.XX"
        "X.X.X"
        "X...X"
        "X...X",

        "X...X"
        "XX..X"
        "X.X.X"
        "X..XX"
        "X...X",

        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXXXXXXXXXX",

        "XXXXX"
        "X...."
        ".XXX."
        "....X"
        "XXXXX",

  };

  return font;
}
}
