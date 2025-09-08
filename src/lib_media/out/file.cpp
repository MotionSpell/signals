#include "file.hpp"

#include "lib_utils/format.hpp"
#include "lib_utils/tools.hpp"

namespace Modules { namespace Out {

File::File(KHost *host, std::string const &path)
    : m_host(host) {
  (void)m_host;
  file = fopen(path.c_str(), "wb");
  if(!file)
    throw error(format("Can't open file for writing: %s", path));
}

File::~File() { fclose(file); }

void File::processOne(Data data) { fwrite(data->data().ptr, 1, data->data().len, file); }

}}
