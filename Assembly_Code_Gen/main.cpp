#include <cstdio>

#include "assembly_gen.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <input.ll> <output.s>\n", argv[0]);
    return 1;
  }

  return emitAssemblyFromIRFile(argv[1], argv[2]);
}
