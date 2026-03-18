# MiniC Assembly Code Generator

This backend lowers the shared MiniC AST to 32-bit x86 AT&T assembly.

## Directory role

`Assembly_Code_Gen/` holds the backend-specific sources, notes, and tests. The actual compiler build is owned by the top-level [`MiniC/Makefile`](../Makefile), so frontend and backend stages reuse the same parser, scanner, AST, and semantic-analysis sources.
The generated Flex/Bison files live in the shared [`MiniC/build`](../build) directory rather than inside `Assembly_Code_Gen/`.

## Build

From `MiniC/`:

```bash
make codegen
```

Or from this directory:

```bash
make
```

Both commands produce `minic_codegen.out` in this directory when you build from `Assembly_Code_Gen/`, or `minic_codegen.out` in the `MiniC/` root when you build through the top-level Makefile.

## Run

From `Assembly_Code_Gen/`:

```bash
./minic_codegen.out assembly_gen_tests/fact.c assembly_gen_tests/fact.s
```

From `MiniC/`:

```bash
./minic_codegen.out Assembly_Code_Gen/assembly_gen_tests/fact.c Assembly_Code_Gen/assembly_gen_tests/fact.s
```

Then link with the provided runtime:

```bash
clang -m32 assembly_gen_tests/fact.s assembly_gen_tests/main.c
```

## Tests

From `MiniC/`:

```bash
make codegen-test
```

## Current scope

- one user-defined function
- optional single integer parameter
- local declarations, including `int x = expr;`
- assignment, `print`, `read`, `return`
- `if`, `if/else`, `while`
- integer arithmetic `+ - * /`
- relational conditions `< > <= >= == !=`

The backend currently uses stack slots for locals and simple register usage during expression evaluation.
