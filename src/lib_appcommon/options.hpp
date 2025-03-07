#pragma once

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <stdexcept>

typedef std::queue<std::string> ArgQueue;

static inline std::string safePop(ArgQueue& args) {
	if(args.empty())
		throw std::runtime_error("unexpected end of command line");
	auto val = args.front();
	args.pop();
	return val;
}

void parseValue(double& var, ArgQueue& args);
void parseValue(int& var, ArgQueue& args);

static inline void parseValue(bool& var, ArgQueue&) {
	var = true;
}

static inline void parseValue(std::string& var, ArgQueue& args) {
	var = safePop(args);
}

template<typename Element>
static inline void parseValue(std::vector<Element>& var, ArgQueue& args) {
	Element e {};
	parseValue(e, args);
	var.push_back(e);
}

struct CmdLineOptions {
		void addFlag(std::string shortName, std::string longName, bool* pVar, std::string desc="") {
			add(shortName, longName, pVar, desc);
		}

		template<typename T>
		void add(std::string shortName, std::string longName, T* pVar, std::string desc="") {
			auto opt = std::make_unique<TypedOption<T>>();
			opt->pVar = pVar;
			opt->shortName = "-" + shortName;
			opt->longName = "--" + longName;
			opt->desc = desc;
			m_Options.push_back(std::move(opt));
		}

		std::vector<std::string> parse(int argc, const char* argv[]);
		void printHelp();

	private:
		struct AbstractOption {
			virtual ~AbstractOption() = default;
			std::string shortName, longName;
			std::string desc;
			virtual void parse(ArgQueue& args) = 0;
		};

		std::vector<std::unique_ptr<AbstractOption>> m_Options;

		template<typename T>
		struct TypedOption : AbstractOption {
			T* pVar;
			void parse(ArgQueue& args) {
				parseValue(*pVar, args);
			}
		};
};

