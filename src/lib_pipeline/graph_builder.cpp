#include "graph_builder.hpp"
#include "lib_utils/json.hpp"
#include "lib_utils/log.hpp" // g_Log
#include "lib_utils/tools.hpp" // uptr
#include <iostream>
#include <stdexcept>

namespace {
auto const JSON_VERSION = 1;
}

namespace Pipelines {

static void setLogConsole(SmallMap<std::string, json::Value> const& params) {
	bool color = false;

	for (auto const &prop : params) {
		if (prop.key == "color") {
			color = prop.value.boolValue;
		} else {
			auto const err = std::string("\"console log config\" unknown member: ") + prop.key;
			throw std::runtime_error(err.c_str());
		}
	}

	setGlobalLogConsole(color);
}

static void setLogSyslog(SmallMap<std::string, json::Value> const& params) {
	const char* ident = "";
	std::string channel_name;

	for (auto const &prop : params) {
		if (prop.key == "ident") {
			ident = prop.value.stringValue.c_str();
		} else if (prop.key == "channel_name") {
			channel_name = prop.value.stringValue;
		} else {
			auto const err = std::string("\"syslog log config\" unknown member: ") + prop.key;
			throw std::runtime_error(err.c_str());
		}
	}

	setGlobalLogSyslog(ident, channel_name.c_str());
}

static void setLogCSV(SmallMap<std::string, json::Value> const& params) {
	const char* path = "";

	for (auto const &prop : params) {
		if (prop.key == "path") {
			path = prop.value.stringValue.c_str();
		} else {
			auto const err = std::string("\"CSV log config\" unknown member: ") + prop.key;
			throw std::runtime_error(err.c_str());
		}
	}

	setGlobalLogCSV(path);
}

static void setLogConfig(const std::string &logType, Level logLevel, SmallMap<std::string, json::Value> const &params) {
	if (logType == "console")
		setLogConsole(params);
	else if (logType == "syslog")
		setLogSyslog(params);
	else if (logType == "csv")
		setLogCSV(params);

	setGlobalLogLevel(logLevel);
}

std::unique_ptr<Pipeline> createPipelineFromJSON(const std::string &jsonText, std::function<ParseModuleConfig> parseModuleConfig) {
	auto json = json::parse(jsonText);

	if (!json.has("version"))
		return nullptr;

	auto const &version = json.objectValue["version"].intValue;
	if (version != JSON_VERSION) {
		auto const err = std::string("Config version is") + std::to_string(version) + ", expected " + std::to_string(JSON_VERSION);
		throw std::runtime_error(err.c_str());
	}

	if (json.has("log")) {
		std::string logType = "console";
		Level logLevel = Info;
		SmallMap<std::string, json::Value> logConfig;

		auto &log = json["log"];
		for (auto const &prop : log.objectValue) {
			if (prop.key == "type") {
				logType = prop.value.stringValue;
			} else if (prop.key == "level") {
				logLevel = parseLogLevel(prop.value.stringValue.c_str());
			} else if (prop.key == "config") {
				logConfig = prop.value.objectValue;
			} else {
				auto const err = std::string("\"log\" unknown member: ") + prop.key;
				throw std::runtime_error(err.c_str());
			}
		}

		setLogConfig(logType, logLevel, logConfig);
	}

	auto pipeline = uptr(new Pipeline);

	SmallMap<std::string /*caption*/, IFilter*> modulesDesc;

	if (!json.has("modules"))
		return nullptr;

	{
		auto &modules = json["modules"];
		for (auto const &module : modules.objectValue) {
			std::string moduleType, moduleCaption = module.key;
			SmallMap<std::string, json::Value> moduleConfig;

			for (auto const &prop : module.value.objectValue) {
				if (prop.key == "type") {
					moduleType = prop.value.stringValue;
				} else if (prop.key == "config") {
					moduleConfig = prop.value.objectValue;
				} else {
					auto const err = std::string("\"modules\" unknown member: ") + prop.key;
					throw std::runtime_error(err.c_str());
				}
			}

			auto va = parseModuleConfig(moduleType, moduleConfig);
			modulesDesc[moduleCaption] = pipeline->add(moduleType.c_str(), va.get());
		}
	}

	if (json.has("connections")) {
		auto &connections = json["connections"];
		int connCount = 0;
		for (auto const &conn : connections.arrayValue) {
			OutputPin src(nullptr);
			InputPin dst(nullptr);
			// Check source and destinations existences
			if (!conn.has("source")) {
				auto const err = std::string("Connection " + std::to_string(connCount) + ": missing \"source\"");
				throw std::runtime_error(err.c_str());
			}
			if (!conn.has("destinations")) {
				auto const err = std::string("Connection " + std::to_string(connCount) + ": missing \"destinations\"");
				throw std::runtime_error(err.c_str());
			}
			// Source
			auto &srcConn = conn["source"];
			for (auto const &prop : srcConn.objectValue) {
				src.mod = modulesDesc[prop.key];
				if(!src.mod) {
					auto const err = std::string("Connection " + std::to_string(connCount) + ": source module \"" + prop.key + "\" unknown");
					throw std::runtime_error(err.c_str());
				}
				src.index = prop.value.intValue;
			}
			if(!src.mod) {
				auto const err = std::string("Connection " + std::to_string(connCount) + ": empty source");
				throw std::runtime_error(err.c_str());
			}
			// Destinations
			auto &dstConns = conn["destinations"];
			for (auto const &dstConn : dstConns.arrayValue) {
				for (auto const &prop : dstConn.objectValue) {
					dst.mod = modulesDesc[prop.key];
					if (!dst.mod) {
						auto const err = std::string("Connection " + std::to_string(connCount) + ": destination module \"" + prop.key + "\" unknown");
						throw std::runtime_error(err.c_str());
					}
					dst.index = prop.value.intValue;
					pipeline->connect(src, dst);
				}
			}

			connCount++;
		}
	}

	return pipeline;
}

}
