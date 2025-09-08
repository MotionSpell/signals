#pragma once

namespace Modules {
struct HardwareContextCuda {
  HardwareContextCuda();
  ~HardwareContextCuda();
  void *device = nullptr; // AVBufferRef
};
}
