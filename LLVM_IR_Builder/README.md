# LLVM IR Builder (MiniC)

This module lowers the MiniC AST (`ast.h`) into LLVM IR using the LLVM C API.

It currently provides:
- `buildLLVMModule(astNode *progNode, const char *moduleName)`
- `dumpLLVMModule(astNode *progNode, const char *moduleName)`

Implementation lives in `llvm_builder.cpp`.

## Prerequisites

- `clang++`
- `clang`
- `llvm-config-17`
- LLVM C headers at `/usr/include/llvm-c-17/` (as referenced by the `Makefile`)

## Build

From `MiniC/LLVM_IR_Builder`:

```bash
make all
```

This compiles:
- `llvm_builder.cpp -> llvm_builder.o`

## Generate reference LLVM IR for C tests

```bash
make test
```

or:

```bash
make maketest
```

This emits `builder_tests/*.ll` by running `clang -S -emit-llvm` on each `builder_tests/*.c`.

## Clean

```bash
make clean
```

Removes:
- `*.o`
- `builder_tests/*.ll`

## How to run this module

There is no standalone executable in this folder. The builder is meant to be called from your parser/semantic pipeline after an AST is constructed.

Typical integration flow:
1. Parse source into `ast_prog`.
2. Call `buildLLVMModule(...)` (or `dumpLLVMModule(...)` for debug output).
3. Optionally write/verify the produced module in your driver.

## Notes

- The builder expects a single user function (`ast_prog -> func`).
- Runtime externs are declared internally:
  - `int read()`
  - `int print(int)`
- Control-flow lowering supports `if`, `if/else`, and `while`.
- Returns use one canonical `ret` block and a `retSlot` alloca.
- Current implementation supports at most one function parameter and does not yet include a rename/resolve prepass for duplicate declaration names in nested scopes.
