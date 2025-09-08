#include "recorder.hpp"

#include "../common/attributes.hpp"
#include "lib_utils/format.hpp"
#include "lib_utils/log_sink.hpp" // Warning
#include "lib_utils/tools.hpp"

namespace Modules { namespace Utils {

Recorder::Recorder(KHost * /*host*/) {}

void Recorder::flush() { record.clear(); }

void Recorder::processOne(Data data) { record.push(data); }

Data Recorder::pop() { return record.pop(); }

bool Recorder::tryPop(Data &data) { return record.tryPop(data); }

}}
