#include "options.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

std::vector<std::string> CmdLineOptions::parse(int argc, const char *argv[]) {

  std::vector<std::string> remaining;

  ArgQueue args;
  for(int i = 1; i < argc; ++i) // skip argv[0]
    args.push(argv[i]);

  while(!args.empty()) {
    auto word = args.front();
    args.pop();

    if(word.substr(0, 1) != "-") {
      remaining.push_back(word);
      continue;
    }

    AbstractOption *opt = nullptr;

    for(auto &o : m_Options) {
      if(word == o->shortName || word == o->longName) {
        opt = o.get();
        break;
      }
    }

    if(!opt)
      throw std::runtime_error("unknown option: \"" + word + "\"");

    opt->parse(args);
  }

  return remaining;
}

void CmdLineOptions::printHelp() {
  for(auto &o : m_Options) {
    auto s = o->shortName + ", " + o->longName;
    while(s.size() < 40)
      s += " ";
    std::cout << "    " << s << o->desc << std::endl;
  }
}

void parseValue(double &var, ArgQueue &args) {
  std::stringstream ss(safePop(args));
  ss >> var;
}

void parseValue(int &var, ArgQueue &args) {
  std::stringstream ss(safePop(args));
  ss >> var;
}
