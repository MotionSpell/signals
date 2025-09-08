#pragma once

#include "lib_utils/clock.hpp"

namespace Modules { namespace Render {

static uint64_t const PREROLL_DELAY_IN_MS = 500ULL;
static uint64_t const PREROLL_DELAY = timescaleToClock(PREROLL_DELAY_IN_MS, 1000);

}}
