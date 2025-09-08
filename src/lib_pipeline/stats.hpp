#pragma once

#include <cstdint>

namespace Pipelines {

struct StatsEntry {
  char name[252]{};
  int32_t value = 0;
};

static_assert(sizeof(StatsEntry) == 256, "StatsEntry size must be 256");

struct IStatsRegistry {
  virtual ~IStatsRegistry() {}
  virtual StatsEntry *getNewEntry(const char *name) = 0; /*owned by the StatsRegistry object*/
};

}
