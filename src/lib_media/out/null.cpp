#include "null.hpp"

namespace Modules { namespace Out {

Null::Null(KHost *host)
    : m_host(host) {
  (void)m_host;
}

void Null::processOne(Data) {}

}}
