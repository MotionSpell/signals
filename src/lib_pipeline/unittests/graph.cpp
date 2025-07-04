#include "tests/tests.hpp"
#include "lib_pipeline/pipeline.hpp"
#include "lib_modules/utils/helper.hpp"

using namespace Modules;
using namespace Pipelines;
using namespace std;

namespace {

struct Dummy : public Module {
	Dummy(KHost*) {
		addInput();
		output = addOutput();
	}
	void process() {
		output->post(output->allocData<DataRaw>(1));
	}
	OutputDefault* output;
};

unittest("pipeline graph: empty") {
	Pipeline p;
	string expected =
	    R"(digraph {
	rankdir = "LR";
}
)";
	ASSERT_EQUALS(expected, p.dumpDOT());
}

unittest("pipeline graph: add module") {
	Pipeline p;
	p.addModule<Dummy>();
	string expected =
	    R"(digraph {
	rankdir = "LR";
	"0";
}
)";

	ASSERT_EQUALS(expected, p.dumpDOT());
}

unittest("pipeline graph: add connection") {
	Pipeline p;
	auto A = p.addModule<Dummy>();
	auto B = p.addModule<Dummy>();
	p.connect(A, B);
	string expected =
	    R"(digraph {
	rankdir = "LR";
	"0";
	"1";
	"0" -> "1";
}
)";

	ASSERT_EQUALS(expected, p.dumpDOT());
}

unittest("pipeline graph: disconnect") {
	Pipeline p;
	auto A = p.addModule<Dummy>();
	auto B = p.addModule<Dummy>();
	p.connect(A, B);
	p.disconnect(A, 0, B, 0);
	string expected =
	    R"(digraph {
	rankdir = "LR";
	"0";
	"1";
}
)";

	ASSERT_EQUALS(expected, p.dumpDOT());
}

unittest("pipeline graph: remove module") {
	Pipeline p;
	auto ptr = p.addModule<Dummy>();
	p.removeModule(ptr);
	string expected =
	    R"(digraph {
	rankdir = "LR";
}
)";
	ASSERT_EQUALS(expected, p.dumpDOT());
}

unittest("pipeline graph: remove wrong connection") {
	Pipeline p;
	auto ptr1 = p.addModule<Dummy>();
	auto ptr2 = p.addModule<Dummy>();
	ASSERT_THROWN(p.disconnect(ptr1, 0, ptr2, 0));
}

unittest("pipeline graph: remove module still connected") {
	Pipeline p;
	auto ptr1 = p.addModule<Dummy>();
	auto ptr2 = p.addModule<Dummy>();
	p.connect(ptr1, ptr2);
	ASSERT_THROWN(p.removeModule(ptr1));
	ASSERT_THROWN(p.removeModule(ptr2));
}

}
