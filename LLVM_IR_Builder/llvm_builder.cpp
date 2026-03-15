#include <llvm-c/Core.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <unordered_map>
#include <vector>
#include <string>
#include "ast.h"
// #include <llvm-c/Core.h>

// #include <inttypes.h>
// #include <stdio.h>
// #include <stdlib.h>

using std::string;
using std::unordered_map;
using std::vector;

/*
 * LLVM IR Builder skeleton 
 *
 * Main ideas from Pseudocode.md:
 * - emitExpr / emitStmt recursive traversal
 * - one canonical return block (retBB) + return slot (retSlot)
 * - control-flow blocks for if/while only
 * - symbol table stack for block scopes
 */

struct ScopeTable {
  unordered_map<string, LLVMValueRef> table;  // varName -> allocaPtr
};

struct BuilderContext {
  LLVMModuleRef module = NULL;
  LLVMBuilderRef builder = NULL;

  LLVMValueRef curFunc = NULL;
  LLVMBasicBlockRef entryBB = NULL;
  LLVMBasicBlockRef retBB = NULL;
  LLVMValueRef retSlot = NULL;

  LLVMTypeRef i32Ty = NULL;
  LLVMValueRef readFn = NULL;
  LLVMValueRef printFn = NULL;

  vector<ScopeTable> scopeStack;
};

static void Print_err(const char *msg) {
  fprintf(stderr, "llvm_builder error: %s\n", msg);
  abort();
}

static bool hasTerminator(LLVMBasicBlockRef bb) {
  return LLVMGetBasicBlockTerminator(bb) != NULL;
}

/* Pseudocode.md (Shadowing):
 * - Push a new scope table when entering a block.
 * - Pop the scope table when leaving the block.
 */
static void pushScope(BuilderContext &ctx) {
  ctx.scopeStack.push_back(ScopeTable{});
}

static void popScope(BuilderContext &ctx) {
  if (ctx.scopeStack.empty()) Print_err("scope stack underflow");
  ctx.scopeStack.pop_back();
}

static void insertSymbol(BuilderContext &ctx, const char *name, LLVMValueRef allocaPtr) {
  if (ctx.scopeStack.empty()) pushScope(ctx);
  ctx.scopeStack.back().table[string(name)] = allocaPtr;
}

/* Pseudocode.md (#Symbol Table):
 * Lookup(name):
 * - Search from the top (current scope) downward until found.
 * - Return the allocaPtr.
 */
static LLVMValueRef lookupSymbol(BuilderContext &ctx, const char *name) {
  for (auto it = ctx.scopeStack.rbegin(); it != ctx.scopeStack.rend(); ++it) {
    auto found = it->table.find(name);
    if (found != it->table.end()) return found->second;
  }
  return NULL;
}

static LLVMValueRef buildEntryAlloca(BuilderContext &ctx, const char *name) {
  if (!ctx.entryBB) Print_err("entryBB not initialized");

  LLVMBuilderRef tmpBuilder = LLVMCreateBuilder();
  LLVMPositionBuilderAtEnd(tmpBuilder, ctx.entryBB);
  LLVMValueRef allocaPtr = LLVMBuildAlloca(tmpBuilder, ctx.i32Ty, name);
  LLVMDisposeBuilder(tmpBuilder);
  return allocaPtr;
}

/* Pseudocode.md (Before generating statements for the function):
 * - Walk the function body and collect all declaration names (including nested blocks).
 * - later, emit allocas in entryBB for each collected name.
 */
static void collectDeclNames(astNode *node, vector<string> &declNames) {
  if (!node) return;

  if (node->type != ast_stmt) return;

  switch (node->stmt.type) {
    case ast_decl:
      declNames.push_back(node->stmt.decl.name);
      return;

    case ast_block:
      if (node->stmt.block.stmt_list) {
        for (astNode *stmtNode : *node->stmt.block.stmt_list) {
          collectDeclNames(stmtNode, declNames);
        }
      }
      return;

    case ast_if:
      collectDeclNames(node->stmt.ifn.if_body, declNames);
      collectDeclNames(node->stmt.ifn.else_body, declNames);
      return;

    case ast_while:
      collectDeclNames(node->stmt.whilen.body, declNames);
      return;

    default:
      return;
  }
}

static LLVMIntPredicate getPredicate(rop_type op) {
  switch (op) {
    case lt:  return LLVMIntSLT;
    case gt:  return LLVMIntSGT;
    case le:  return LLVMIntSLE;
    case ge:  return LLVMIntSGE;
    case eq:  return LLVMIntEQ;
    case neq: return LLVMIntNE;
  }
  Print_err("unknown relational operator");
}

static LLVMValueRef emitExpr(astNode *node, BuilderContext &ctx);
static void emitStmt(astNode *stmtNode, BuilderContext &ctx);

/* Pseudocode.md (function call):
 * - Generate LLVMValueRef for each argument (left to right).
 * - Generate LLVMBuildCall2(...).
 * - Return call value (or ignore if it is a statement call).
 */
static LLVMValueRef emitCall(astNode *stmtNode, BuilderContext &ctx, bool isStmtCall) {
  assert(stmtNode && stmtNode->type == ast_stmt && stmtNode->stmt.type == ast_call);

  const astCall &callNode = stmtNode->stmt.call;
  LLVMValueRef callee = NULL;
  LLVMTypeRef fnTy = NULL;
  LLVMValueRef args[1];
  unsigned numArgs = 0;

  if (string(callNode.name) == "read") {
    callee = ctx.readFn;
    fnTy = LLVMGetElementType(LLVMTypeOf(callee));
  } else if (string(callNode.name) == "print") {
    callee = ctx.printFn;
    fnTy = LLVMGetElementType(LLVMTypeOf(callee));
    if (callNode.param) {
      args[numArgs++] = emitExpr(callNode.param, ctx);
    }
  } else {
    Print_err("unsupported call target");
  }

  LLVMValueRef callV = LLVMBuildCall2(
      ctx.builder, fnTy, callee, numArgs ? args : NULL, numArgs, "");

  if (isStmtCall) return NULL;
  return callV;
}

static LLVMValueRef emitExpr(astNode *node, BuilderContext &ctx) {
  /* Pseudocode.md (emitExpr template):
   * ast_var   -> lookup ptr, load
   * ast_cnst  -> LLVMConstInt
   * ast_bexpr -> emit lhs/rhs, then add/sub/mul/sdiv
   * ast_rexpr -> emit lhs/rhs, choose predicate, LLVMBuildICmp
   */
  if (!node) Print_err("emitExpr got NULL node");

  switch (node->type) {
    case ast_var: {
      LLVMValueRef allocaPtr = lookupSymbol(ctx, node->var.name);
      if (!allocaPtr) Print_err("variable not found in symbol table");
      return LLVMBuildLoad2(ctx.builder, ctx.i32Ty, allocaPtr, node->var.name);
    }

    case ast_cnst:
      return LLVMConstInt(ctx.i32Ty, (unsigned long long)node->cnst.value, 1);

    case ast_bexpr: {
      LLVMValueRef lhsVal = emitExpr(node->bexpr.lhs, ctx);
      LLVMValueRef rhsVal = emitExpr(node->bexpr.rhs, ctx);

      switch (node->bexpr.op) {
        case add:    return LLVMBuildAdd(ctx.builder, lhsVal, rhsVal, "");
        case sub:    return LLVMBuildSub(ctx.builder, lhsVal, rhsVal, "");
        case mul:    return LLVMBuildMul(ctx.builder, lhsVal, rhsVal, "");
        case divide: return LLVMBuildSDiv(ctx.builder, lhsVal, rhsVal, "");
        default:     Print_err("unsupported binary operator");
      }
    }

    case ast_rexpr: {
      LLVMValueRef lhsVal = emitExpr(node->rexpr.lhs, ctx);
      LLVMValueRef rhsVal = emitExpr(node->rexpr.rhs, ctx);
      LLVMIntPredicate pred = getPredicate(node->rexpr.op);
      return LLVMBuildICmp(ctx.builder, pred, lhsVal, rhsVal, "");
    }

    case ast_uexpr: {
      LLVMValueRef exprVal = emitExpr(node->uexpr.expr, ctx);
      if (node->uexpr.op != uminus) Print_err("unsupported unary operator");
      return LLVMBuildNeg(ctx.builder, exprVal, "");
    }

    default:
      Print_err("unsupported AST node in emitExpr");
	  return NULL;
  }
}

static void emitBlock(astNode *stmtNode, BuilderContext &ctx) {
  /* Pseudocode.md (Shadowing):
   * - Enter block => push scope
   * - Generate statements
   * - Exit block => pop scope
   */
  assert(stmtNode && stmtNode->type == ast_stmt && stmtNode->stmt.type == ast_block);

  pushScope(ctx);

  if (stmtNode->stmt.block.stmt_list) {
    for (astNode *childStmt : *stmtNode->stmt.block.stmt_list) {
      emitStmt(childStmt, ctx);
      if (hasTerminator(LLVMGetInsertBlock(ctx.builder))) break;
    }
  }

  popScope(ctx);
}

static void emitStmt(astNode *stmtNode, BuilderContext &ctx) {
  /* Pseudocode.md (statement lowering):
   * - decl   : alloca + insert symbol table (entryBB allocation strategy)
   * - asgn   : emitExpr(rhs), lookup lhs ptr, store
   * - return : emitExpr, store into retSlot, br retBB
   * - if     : thenBB / elseBB / mergeBB pattern
   * - while  : condBB / bodyBB / endBB pattern
   */
  if (!stmtNode || stmtNode->type != ast_stmt) Print_err("emitStmt expected ast_stmt");

  switch (stmtNode->stmt.type) {
    case ast_decl: {
      if (!lookupSymbol(ctx, stmtNode->stmt.decl.name)) {
        LLVMValueRef allocaPtr = buildEntryAlloca(ctx, stmtNode->stmt.decl.name);
        insertSymbol(ctx, stmtNode->stmt.decl.name, allocaPtr);
      }
      return;
    }

    case ast_asgn: {
      astNode *lhsNode = stmtNode->stmt.asgn.lhs;
      if (!lhsNode || lhsNode->type != ast_var) Print_err("assignment LHS must be ast_var");

      LLVMValueRef rhsVal = emitExpr(stmtNode->stmt.asgn.rhs, ctx);
      LLVMValueRef lhsPtr = lookupSymbol(ctx, lhsNode->var.name);
      if (!lhsPtr) Print_err("assignment target not found");

      LLVMBuildStore(ctx.builder, rhsVal, lhsPtr);
      return;
    }

    case ast_call:
      (void)emitCall(stmtNode, ctx, true);
      return;

    case ast_ret: {
      LLVMValueRef retVal = emitExpr(stmtNode->stmt.ret.expr, ctx);
      LLVMBuildStore(ctx.builder, retVal, ctx.retSlot);
      LLVMBuildBr(ctx.builder, ctx.retBB);
      return;
    }

    case ast_block:
      emitBlock(stmtNode, ctx);
      return;

    case ast_if: {
      LLVMValueRef condV = emitExpr(stmtNode->stmt.ifn.cond, ctx);

      LLVMBasicBlockRef thenBB = LLVMAppendBasicBlock(ctx.curFunc, "if.then");
      LLVMBasicBlockRef elseBB = NULL;
      LLVMBasicBlockRef mergeBB = LLVMAppendBasicBlock(ctx.curFunc, "if.end");

      if (stmtNode->stmt.ifn.else_body) {
        elseBB = LLVMAppendBasicBlock(ctx.curFunc, "if.else");
      }

      LLVMBuildCondBr(ctx.builder, condV, thenBB, elseBB ? elseBB : mergeBB);

      LLVMPositionBuilderAtEnd(ctx.builder, thenBB);
      emitStmt(stmtNode->stmt.ifn.if_body, ctx);
      if (!hasTerminator(LLVMGetInsertBlock(ctx.builder))) {
        LLVMBuildBr(ctx.builder, mergeBB);
      }

      if (elseBB) {
        LLVMPositionBuilderAtEnd(ctx.builder, elseBB);
        emitStmt(stmtNode->stmt.ifn.else_body, ctx);
        if (!hasTerminator(LLVMGetInsertBlock(ctx.builder))) {
          LLVMBuildBr(ctx.builder, mergeBB);
        }
      }

      LLVMPositionBuilderAtEnd(ctx.builder, mergeBB);
      return;
    }

    case ast_while: {
      LLVMBasicBlockRef condBB = LLVMAppendBasicBlock(ctx.curFunc, "while.cond");
      LLVMBasicBlockRef bodyBB = LLVMAppendBasicBlock(ctx.curFunc, "while.body");
      LLVMBasicBlockRef endBB  = LLVMAppendBasicBlock(ctx.curFunc, "while.end");

      LLVMBuildBr(ctx.builder, condBB);

      LLVMPositionBuilderAtEnd(ctx.builder, condBB);
      LLVMValueRef condV = emitExpr(stmtNode->stmt.whilen.cond, ctx);
      LLVMBuildCondBr(ctx.builder, condV, bodyBB, endBB);

      LLVMPositionBuilderAtEnd(ctx.builder, bodyBB);
      emitStmt(stmtNode->stmt.whilen.body, ctx);
      if (!hasTerminator(LLVMGetInsertBlock(ctx.builder))) {
        LLVMBuildBr(ctx.builder, condBB);
      }

      LLVMPositionBuilderAtEnd(ctx.builder, endBB);
      return;
    }
  }

  Print_err("unknown statement type");
}

static void declareRuntimeFns(BuilderContext &ctx) {

  LLVMTypeRef readTy = LLVMFunctionType(ctx.i32Ty, NULL, 0, 0);
  ctx.readFn = LLVMAddFunction(ctx.module, "read", readTy);

  LLVMTypeRef printParamTys[] = {ctx.i32Ty};
  LLVMTypeRef printTy = LLVMFunctionType(ctx.i32Ty, printParamTys, 1, 0);
  ctx.printFn = LLVMAddFunction(ctx.module, "print", printTy);
}

static void emitFunction(astNode *funcNode, BuilderContext &ctx) {
  /* Pseudocode.md :
   * 1. Create function type / add function
   * 2. Create entry and ret basic blocks
   * 3. Position builder at entry
   * 4. Emit allocas in entry (params, locals, retSlot)
   * 5. Store incoming params to allocas
   * 6. Build body CFG
   * 7. In retBB: load retSlot and emit single ret
   */
  if (!funcNode || funcNode->type != ast_func) Print_err("emitFunction expected ast_func");


  LLVMTypeRef paramTys[1];
  unsigned numParams = 0;
  if (funcNode->func.param) {
    paramTys[numParams++] = ctx.i32Ty;
  }

  LLVMTypeRef fnTy = LLVMFunctionType(ctx.i32Ty, paramTys, numParams, 0);
  ctx.curFunc = LLVMAddFunction(ctx.module, funcNode->func.name, fnTy);

  ctx.entryBB = LLVMAppendBasicBlock(ctx.curFunc, "entry");
  ctx.retBB = LLVMAppendBasicBlock(ctx.curFunc, "ret");
  LLVMPositionBuilderAtEnd(ctx.builder, ctx.entryBB);

  ctx.scopeStack.clear();
  pushScope(ctx);

  /* canonical return slot */
  ctx.retSlot = buildEntryAlloca(ctx, "retSlot");

  /* parameter local copy */
  if (funcNode->func.param) {
    const char *paramName = funcNode->func.param->var.name;
    LLVMValueRef paramPtr = buildEntryAlloca(ctx, paramName);
    insertSymbol(ctx, paramName, paramPtr);
    LLVMBuildStore(ctx.builder, LLVMGetParam(ctx.curFunc, 0), paramPtr);
  }


  vector<string> declNames;
  collectDeclNames(funcNode->func.body, declNames);
  for (const string &name : declNames) {
    if (lookupSymbol(ctx, name.c_str())) continue;
    LLVMValueRef allocaPtr = buildEntryAlloca(ctx, name.c_str());
    insertSymbol(ctx, name.c_str(), allocaPtr);
  }

  emitStmt(funcNode->func.body, ctx);

  if (!hasTerminator(LLVMGetInsertBlock(ctx.builder))) {
    LLVMBuildBr(ctx.builder, ctx.retBB);
  }

  LLVMPositionBuilderAtEnd(ctx.builder, ctx.retBB);
  LLVMValueRef retVal = LLVMBuildLoad2(ctx.builder, ctx.i32Ty, ctx.retSlot, "retVal");
  LLVMBuildRet(ctx.builder, retVal);

  popScope(ctx);
}

LLVMModuleRef buildLLVMModule(astNode *progNode, const char *moduleName) {
  /* Pseudocode.md (IR Builder Overview):
   * Create module, declare externs, then emit the user function by AST traversal.
   */
  if (!progNode || progNode->type != ast_prog) Print_err("buildLLVMModule expected ast_prog");

  BuilderContext ctx;
  ctx.i32Ty = LLVMInt32Type();
  ctx.module = LLVMModuleCreateWithName(moduleName ? moduleName : "minic");
  ctx.builder = LLVMCreateBuilder();

  declareRuntimeFns(ctx);


  emitFunction(progNode->prog.func, ctx);

  LLVMDisposeBuilder(ctx.builder);
  return ctx.module;
}

void dumpLLVMModule(astNode *progNode, const char *moduleName) {
  LLVMModuleRef module = buildLLVMModule(progNode, moduleName);
  LLVMDumpModule(module);
  LLVMDisposeModule(module);
}
