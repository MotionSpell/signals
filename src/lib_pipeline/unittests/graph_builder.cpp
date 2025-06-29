#include "tests/tests.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "lib_pipeline/graph_builder.hpp"
#include "lib_modules/utils/helper.hpp"
#include "lib_modules/utils/factory.hpp"
#include <cstdlib>

using namespace std;
using namespace Pipelines;

namespace Modules {
struct Dummy : public Module {
	Dummy(KHost*) {
		addInput();
		addOutput();
	}
	Dummy(KHost*, bool oneMoreOutput) {
		addInput();
		addOutput();
		if (oneMoreOutput)
			addOutput();
	}
	void process() override {
	}

	static IModule* create(KHost* host, void* va) {
		auto type = (StreamType)(uintptr_t)va;
		if (type)
			return createModule<Dummy>(host, type).release();
		else
			return createModule<Dummy>(host).release();
	}
};

auto const registered = Factory::registerModule("Dummy", &Dummy::create);
}

unittest("graph builder: dummy") {
	string json =
	    R"|({
    "version" : 1,
    "modules" : {
        "dummy0" : {
            "type" : "Dummy",
            "config" : {
                "oneMoreOutput": "1"
            }
        },
        "dummy1" : {
            "type" : "Dummy"
        }
    },
    "connections" : [
        {
            "source": {"dummy0": 1},
            "destinations": [ {"dummy1": 0} ]
        }
    ]
}
)|";

	string expected = R"|(digraph {
	rankdir = "LR";
	"Dummy (#0)";
	"Dummy (#1)";
	"Dummy (#0)" -> "Dummy (#1)";
}
)|";

	auto p = createPipelineFromJSON(json, [](const string type, const SmallMap<std::string, json::Value> &params) {
		ASSERT_EQUALS("Dummy", type);
		bool param = false;
		if (params.find("oneMoreOutput") != params.end())
			param = params["oneMoreOutput"].intValue;
		auto deleter = [](ConfigType *p) {delete (bool*)p;};
		return shared_ptr<ConfigType>((ConfigType*)new bool(param), deleter);
	});
	ASSERT_EQUALS(expected, p->dumpDOT());
}

unittest("graph builder: wrong connections") {
	string json =
	    R"|({
    "version" : 1,
    "modules" : {
        "dummy0" : {
            "type" : "Dummy"
        },
        "dummy1" : {
            "type" : "Dummy"
        }
    },
    "connections" : [
        {
            "source": {"dummy0": 1},
            "destinations" : [ {"dummy1": 1} ]
        }
    ]
}
)|";

	string expected = R"|(digraph {
	rankdir = "LR";
	"Dummy (#0)";
	"Dummy (#1)";
	"Dummy (#0)" -> "Dummy (#1)";
}
)|";

	ASSERT_THROWN(createPipelineFromJSON(json, [](const string, const SmallMap<std::string, json::Value>&) { return nullptr; }));
}

unittest("graph builder: one output to multiple inputs") {
	string json =
	    R"|({
    "version" : 1,
    "modules" : {
        "dummy0" : {
            "type" : "Dummy"
        },
        "dummy1" : {
            "type" : "Dummy"
        },
        "dummy2" : {
            "type" : "Dummy"
        }
    },
    "connections" : [
        {
            "source": {"dummy0": 0},
            "destinations": [
                {"dummy1": 0},
                {"dummy2": 0}
            ]
        }
    ]
}
)|";

	string expected = R"|(digraph {
	rankdir = "LR";
	"Dummy (#0)";
	"Dummy (#1)";
	"Dummy (#2)";
	"Dummy (#0)" -> "Dummy (#1)";
	"Dummy (#0)" -> "Dummy (#2)";
}
)|";

	auto p = createPipelineFromJSON(json, [](const string, const SmallMap<std::string, json::Value>&) {
		auto deleter = [](ConfigType *p) {delete (bool*)p;};
		return shared_ptr<ConfigType>((ConfigType*)new bool(false), deleter);
	});
	ASSERT_EQUALS(expected, p->dumpDOT());
}

unittest("graph builder: using non-existing names for connection") {
	string json =
	    R"|({
    "version" : 1,
    "connections" : [
        {
            "source": {"wrong_name0": 0},
            "destinations": [ {"wrong_name1": 0} ],
        }
    ]
}
)|";

	ASSERT_THROWN(createPipelineFromJSON(json, [](const string, const SmallMap<std::string, json::Value>&) { return nullptr; }));
}
