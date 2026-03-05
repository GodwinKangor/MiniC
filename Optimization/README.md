# Author : Godwin Kangor
# 02/16/26
# MiniC Optimizations (Part 3)

This folder contains an LLVM IR optimizer (`llvm_parser.c`) for MiniC Part 3.
Optimizations implemented:
- **Constant Folding** (CFold)
- **Dead Code Elimination** (DCE)
- **Common Subexpression Elimination** (CSE)
- **Constant Propagation** (ConstProp, global / dataflow)


## What it does
- Runs optimization passes on LLVM `.ll` files


Passes: `cfold`, `dce`, `cse`, `constprop`

## Files
- `llvm_parser.c` — optimizer + passes
- `Makefile` — build + test
- `optimizer_test_results/` — inputs (`*.ll`) + expected (`*_opt.ll`)

## Requirements
On the target machine you need:
- `clang` / `clang++`
- LLVM 17 development libraries (`llvm-17` installed on babylon5.thayer.dartmouth.edu)
- standard build tools (`make`)

## Build
Inside Optimization
```bash
make
```

## Run
```bash
./llvm_parser optimizer_test_results/cfold_add.ll cfold
# output: test_new.ll
```

Modes: `cfold`, `dce`, `cse`, `constprop`, `all`

## Test
```bash
make alltest
```
