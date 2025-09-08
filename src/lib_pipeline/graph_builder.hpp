#pragma once

#include "lib_utils/json.hpp"

#include "pipeline.hpp"

namespace Pipelines {
/*
User-defined function to provide the configuration of the modules.

The Signals framework doesn't provide any serialization mechanism for the
configuration of modules. Therefore the user must supply a function which
converts a string from the input JSON configuration to the actual type.

ConfigType is there to erase the variety of configuration types.
*/
typedef int *ConfigType;
typedef std::shared_ptr<ConfigType> ParseModuleConfig(std::string const &moduleType,
      SmallMap<std::string /*name*/, json::Value /*value*/> const &moduleConfig);

std::unique_ptr<Pipeline> createPipelineFromJSON(const std::string &json,
      std::function<ParseModuleConfig> parseModuleConfig);
}
