#include "print.hpp"

namespace Modules { namespace Out {

void Print::processOne(Data data) { os << "Print: Received data of size: " << data->data().len << std::endl; }

Print::Print(KHost *host, std::ostream &os)
    : m_host(host)
    , os(os) {
  (void)m_host;
}

}}
