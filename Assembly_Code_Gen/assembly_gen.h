#ifndef MINIC_ASSEMBLY_GEN_H
#define MINIC_ASSEMBLY_GEN_H

#include <llvm-c/Types.h>

int emitAssemblyFromModule(LLVMModuleRef module, const char *outputPath);
int emitAssemblyFromIRFile(const char *inputPath, const char *outputPath);

#endif
