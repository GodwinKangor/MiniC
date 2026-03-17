# MiniC Compiler Lab

This directory now serves as the top-level workspace for the MiniC pipeline:

- frontend parsing and semantic analysis at the repository root
- LLVM IR generation in `LLVM_IR_Builder/`
- LLVM IR optimization in `Optimization/`
- x86 assembly generation in `Assembly_Code_Gen/`

The root `Makefile` ties those stages together so `make`, `make test`, and `make test-pipeline` can be run from one place.

## Directory Layout

- `scanner.l`, `parser.y`, `semantic.c`:
  frontend lexer, parser, and semantic checks
- `ast/`:
  shared AST implementation used by the frontend
- `parser_tests/`:
  parser-only MiniC inputs
- `semantic_analysis_tests/`:
  semantic success/failure cases
- `LLVM_IR_Builder/`:
  Part 2 IR builder sources, tests, and local build outputs
- `Optimization/`:
  optimization pass implementation and expected `.ll` results
- `Assembly_Code_Gen/`:
  assembly generator sources, tests, and local build outputs
- `build/`:
  generated root-level parser files and pipeline artifacts
- `Frontend/`:
  older frontend-only copy kept as local reference material
- `MiniC_tests/`:
  extra sample C programs used across stages

## Build Outputs

Generated files are kept out of the source layout as much as possible:

- root frontend artifacts go in `build/` and the `minic_frontend` executable
- IR builder artifacts stay under `LLVM_IR_Builder/build/`
- optimizer scratch output stays inside `Optimization/`
- assembly outputs stay under `Assembly_Code_Gen/build/`

Use `make clean` at the repository root to remove the standard generated artifacts.

## Common Commands

Build the full pipeline:

```bash
make
```

Run all stage tests:

```bash
make test
```

Run only the frontend parser tests:

```bash
make test-frontend
```

Run semantic analysis tests:

```bash
make semtest
```

Run the end-to-end IR -> optimized IR -> assembly pipeline:

```bash
make test-pipeline
```

## Notes

- The frontend executable produced by the root `Makefile` is `minic_frontend`.
- A single Bison shift/reduce conflict is expected because of the classic dangling-`else` ambiguity.
- `parser_tests/main.c` is intentionally not treated as a MiniC parser test input.
