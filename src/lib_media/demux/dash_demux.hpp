#pragma once

#include <functional>
#include <string>

struct IAdaptationControl;

struct DashDemuxConfig {
  std::string url;

  // adaptation controller: called back from constructor
  std::function<void(IAdaptationControl *)> adaptationControlCbk = nullptr;
};
