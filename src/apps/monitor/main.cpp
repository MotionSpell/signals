#include "lib_pipeline/stats.hpp"
#include "lib_utils/os.hpp"

int main(int argc, char *argv[]) {
  if(argc != 2) {
    fprintf(stderr, "Usage: %s <pid>\n", argv[0]);
    return 1;
  }

  auto const size = sizeof(Pipelines::StatsEntry) * 256;
  auto mem = createSharedMemory(size, argv[1]);

  auto entry = (Pipelines::StatsEntry *)mem->data();
  while(entry->name[0]) {
    printf("%s: %d\n", entry->name, entry->value);
    entry++;
  }

  if(entry == mem->data())
    printf("No entries\n");

  mem.release(); // HACK: don't delete the file
  return 0;
}
