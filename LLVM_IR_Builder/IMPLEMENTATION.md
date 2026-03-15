# Implementation Notes: MiniC LLVM IR Builder

This document describes what is currently implemented in `llvm_builder.cpp`, how the lowering works, and known limits.

## Entry points

- `buildLLVMModule(astNode *progNode, const char *moduleName)`
  - Validates `ast_prog`
  - Creates module + builder context
  - Declares runtime functions (`read`, `print`)
  - Emits one user function
  - Returns `LLVMModuleRef`
- `dumpLLVMModule(astNode *progNode, const char *moduleName)`
  - Builds module
  - Calls `LLVMDumpModule`
  - Disposes module

## Core context

`BuilderContext` stores global lowering state:
- LLVM handles: `module`, `builder`
- Current function state: `curFunc`, `entryBB`, `retBB`, `retSlot`
- Common type: `i32Ty`
- Runtime function refs: `readFn`, `printFn`
- Scope stack for symbol resolution (`vector<ScopeTable>`)

`ScopeTable` maps variable names to their alloca pointers.

## Symbol/scoping model

- `pushScope`/`popScope` maintain block scopes.
- `insertSymbol` inserts into current scope.
- `lookupSymbol` searches from innermost scope outward.
- Declarations are allocated in the function entry block (`buildEntryAlloca`) to keep allocas canonical.

## Function lowering

`emitFunction(astNode *funcNode, BuilderContext &ctx)`:
1. Build function type (`i32` return, optional single `i32` param).
2. Create blocks:
   - `entry`
   - `ret`
3. Initialize scope stack and create canonical `retSlot` in `entry`.
4. If parameter exists:
   - allocate local slot
   - store incoming LLVM param value to it
5. Pre-collect all declaration names from the body (`collectDeclNames`) and create entry allocas.
6. Emit body via `emitStmt`.
7. If current block has no terminator, branch to `ret`.
8. In `ret` block, load `retSlot` and emit the single LLVM `ret`.

## Expression lowering

`emitExpr` supports:
- variables (`ast_var`): load from symbol alloca
- constants (`ast_cnst`): `LLVMConstInt`
- binary arithmetic (`ast_bexpr`):
  - `+`, `-`, `*`, `/` -> `add/sub/mul/sdiv`
- relational expressions (`ast_rexpr`):
  - `<`, `>`, `<=`, `>=`, `==`, `!=` -> `LLVMBuildICmp`
- unary minus (`ast_uexpr`): `LLVMBuildNeg`

## Statement lowering

`emitStmt` supports:
- declaration (`ast_decl`)
  - bind name to entry alloca (if not already bound)
- assignment (`ast_asgn`)
  - emit RHS
  - store into LHS variable alloca
- call statement (`ast_call`)
  - supports `read()` and `print(expr)`
- return (`ast_ret`)
  - emit value
  - store to `retSlot`
  - branch to canonical `ret` block
- block (`ast_block`)
  - push scope
  - emit statements in order
  - stop early if block terminator already exists
  - pop scope
- if / if-else (`ast_if`)
  - create `if.then`
  - optional `if.else`
  - `if.end`
  - terminate non-ended arms with branch to merge
- while (`ast_while`)
  - `while.cond`, `while.body`, `while.end`
  - back-edge from non-terminated body to `while.cond`

## Runtime calls

`declareRuntimeFns` adds:
- `read: i32 ()`
- `print: i32 (i32)`

`emitCall` dispatches only these two names and emits `LLVMBuildCall2`.

## Error strategy

`Print_err(...)` prints an error and aborts immediately.  
The builder currently uses fail-fast behavior for malformed or unsupported AST cases.


