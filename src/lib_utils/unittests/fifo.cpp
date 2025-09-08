#include "lib_utils/fifo.hpp"

#include "lib_utils/tools.hpp"
#include "tests/tests.hpp"

using namespace Tests;

unittest("fifo") {
  GenericFifo<char> fp;
  fp.write("Hello", 5);
  fp.write("World", 5);
  ASSERT(fp.bytesToRead() == 10);

  fp.consume(4);
  ASSERT(fp.readPointer()[0] == 'o');
  ASSERT(fp.readPointer()[1] == 'W');
  ASSERT(fp.readPointer()[2] == 'o');
  ASSERT(fp.readPointer()[3] == 'r');

  fp.consume(6);
  ASSERT(fp.bytesToRead() == 0);
}
