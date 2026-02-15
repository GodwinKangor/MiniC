# Author : Godwin Kangor
# 01/26/26 
# MiniC Frontend (Part 1 + Part 2)

This repository contains a **MiniC frontend** for Part 1 and Part 2 of the compiler lab.
It includes:
- a **lexer** written in Flex (`scanner.l`)
- a **parser** written in Bison (`parser.y`)
- AST construction using the provided **AST library** (`ast/ast.c`, `ast/ast.h`)
- semantic analysis (scope + symbol checks) in `semantic.c`

---

## Directory/Lab structure

- `scanner.l` — Flex lexer
- `parser.y` — Bison grammar + AST construction actions
- `semantic.c` — semantic analysis (decl-before-use, no duplicate decl in same scope)
- `ast/` — provided AST implementation
  - `ast.c`, `ast.h`
- `parser_tests/` — test inputs
  - `p1.c`–`p5.c` should parse successfully
  - `p_bad.c` should fail (intentionally invalid)
- `Makefile` — build + test automation

Generated files (created by the build):
- `y.tab.c`, `y.tab.h` — output of Bison
- `lex.yy.c` — output of Flex
- `minic_frontend.out` — compiled frontend executable

---

## Requirements

On the target machine ,you need:
- `flex`
- `bison`
- `g++` (C++17)
- standard build tools (`make`)

> Note: We compile with **g++** because the parser and `%union` use C++ types (e.g., `std::vector`).

---

## Build

From the repository directory:

```bash
make
```

This runs:
- `bison -d -o y.tab.c parser.y`
- `flex -o lex.yy.c scanner.l`
- `g++ -std=c++17 ... -o minic_frontend.out`

Clean build artifacts:

```bash
make clean
```

---

## Run

Parse a MiniC file:

```bash
./minic_frontend.out parser_tests/p1.c
```

- Exit code `0` → parse success
- Non-zero → parse failure (see error message)

### Optional: print the AST
In `parser.y`, inside `main`, there is an optional AST print call (commented out):

```c
/* if (rc == 0 && root) printNode(root, 0); */
```

Uncomment it if you want the AST printed after a successful parse.

### Semantic analysis
Semantic analysis runs automatically after parsing succeeds. A semantic error causes a non-zero exit code. The semantic checks include:
- variable declared before use
- no duplicate declaration in the same scope

---

## Test

Run the provided parser tests:

```bash
make test
```

Expected behavior:
- `p1.c`–`p5.c` parse successfully
- `p_bad.c` fails (and the test target treats that failure as success)

> Note: `parser_tests/main.c` is **not** a MiniC test input (it begins with `#include`), so it should not be parsed by the MiniC frontend.

### Semantic analysis tests
Run the semantic analysis tests:

```bash
make semtest
```

Expected behavior: all `*_good.c` pass, all `*_bad.c` fail

Semantic tests live in `semantic_analysis_tests/`.

---

## Helpful debugging commands

Generate Bison’s state report (`parser.output`) to inspect conflicts:

```bash
bison -v -d -o y.tab.c parser.y
```

Rebuild from scratch:

```bash
make clean && make
```

---

## Notes

- A single shift/reduce conflict warning from Bison is expected due to the classic **dangling else** ambiguity.
On babylon5, a successful build looks like:

```text
f006vzt@babylon5:~/compiler/MiniC$ make
bison -d -o y.tab.c parser.y
parser.y: warning: 1 shift/reduce conflict [-Wconflicts-sr]
parser.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples
flex -o lex.yy.c scanner.l
g++ -std=c++17 -Wall -Wextra -O0 -g -Iast y.tab.c lex.yy.c ast/ast.c -lfl -o minic_frontend.out
lex.yy.c:1194:17: warning: 'void yyunput(int, char*)' defined but not used [-Wunused-function]
  1194 |     static void yyunput (int c, char * yy_bp )
      |                 ^~~~~~~
```
- Part 1 enforces some constraints in the grammar (e.g., **declarations must appear at the start of a block**).
- Part 2 adds semantic analysis (symbol table + scope stack).
- Run `make semtest` to validate the semantic rules using `semantic_analysis_tests/`.
