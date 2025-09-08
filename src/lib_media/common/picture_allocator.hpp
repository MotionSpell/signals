#pragma once

#include "picture.hpp"

namespace Modules {
struct PictureAllocator {
  struct PictureContext {
    std::shared_ptr<DataPicture> pic;
  };
  virtual PictureContext *getPicture(Resolution res, Resolution resInternal, PixelFormat format) = 0;
};
}
