// Author : Godwin Kangor
// semantic.c

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ast/ast.h"

static int sem_error = 0;

//stack of scopes--> each scope is a list of declared names 
static std::vector<std::vector<char*>> scopes;

static void push_scope() {
  scopes.push_back(std::vector<char*>());
}

static void pop_scope() {
  for (char* s : scopes.back()) free(s);
  scopes.pop_back();
}

static int scope_has(const std::vector<char*>& scope, const char* name) {
  for (char* s : scope) {
    if (strcmp(s, name) == 0) return 1;
  }
  return 0;
}

static int any_scope_has(const char* name) {
  for (int i = (int)scopes.size() - 1; i >= 0; --i) {
    if (scope_has(scopes[i], name)) return 1;
  }
  return 0;
}

static void report_dup(const char* name) {
  fprintf(stderr, "semantic error: duplicate declaration of '%s'\n", name);
  sem_error = 1;
}

static void report_undef(const char* name) {
  fprintf(stderr, "semantic error: '%s' used before declaration\n", name);
  sem_error = 1;
}

static void declare_name(const char* name) {
  if (scopes.empty()) push_scope();
  if (scope_has(scopes.back(), name)) {
    report_dup(name);
    return;
  }
  scopes.back().push_back(strdup(name));
}

static void use_name(const char* name) {
  if (!any_scope_has(name)) {
    report_undef(name);
  }
}

//forward decl /
static void visit(astNode* n, int is_func_body_block);

static void visit_expr(astNode* n) {
  if (!n) return;

  switch (n->type) {
    case ast_var:
      use_name(n->var.name);
      break;

    case ast_cnst:
      break;

    case ast_bexpr:
      visit_expr(n->bexpr.lhs);
      visit_expr(n->bexpr.rhs);
      break;

    case ast_uexpr:
      visit_expr(n->uexpr.expr);
      break;

    case ast_rexpr:
      visit_expr(n->rexpr.lhs);
      visit_expr(n->rexpr.rhs);
      break;

    case ast_stmt:
      visit(n, 0);
      break;

    default:
      break;
  }
}

static void visit_stmt(astNode* n, int is_func_body_block) {
  if (!n || n->type != ast_stmt) return;

  switch (n->stmt.type) {
    case ast_decl:
      declare_name(n->stmt.decl.name);
      break;

    case ast_asgn:
      // LHS must already be declared
      if (n->stmt.asgn.lhs && n->stmt.asgn.lhs->type == ast_var) {
        use_name(n->stmt.asgn.lhs->var.name);
      }
      visit_expr(n->stmt.asgn.rhs);
      break;

    case ast_call:
      // print(expr) uses expr; read() has param == NULL
      if (n->stmt.call.param) visit_expr(n->stmt.call.param);
      break;

    case ast_ret:
      if (n->stmt.ret.expr) visit_expr(n->stmt.ret.expr);
      break;

    case ast_while:
      if (n->stmt.whilen.cond) visit_expr(n->stmt.whilen.cond);
      if (n->stmt.whilen.body) visit(n->stmt.whilen.body, 0);
      break;

    case ast_if:
      if (n->stmt.ifn.cond) visit_expr(n->stmt.ifn.cond);
      if (n->stmt.ifn.if_body) visit(n->stmt.ifn.if_body, 0);
      if (n->stmt.ifn.else_body) visit(n->stmt.ifn.else_body, 0);
      break;

    case ast_block: {
      // this makes: int func(int i){ int i; }  -> duplicate decl error .
      int push = !is_func_body_block;
      if (push) push_scope();

      if (n->stmt.block.stmt_list) {
        for (astNode* s : *(n->stmt.block.stmt_list)) {
          visit(s, 0);
        }
      }

      if (push) pop_scope();
      break;
    }
  }
}

static void visit(astNode* n, int is_func_body_block) {
  if (!n) return;

  switch (n->type) {
    case ast_prog:
      visit(n->prog.ext1, 0);
      visit(n->prog.ext2, 0);
      visit(n->prog.func, 0);
      break;

    case ast_extern:
      break;

    case ast_func:
      // Function introduces a scope for parameters + locals
      push_scope();

      // declare parameter in this scope (if present)
      if (n->func.param && n->func.param->type == ast_var) {
        declare_name(n->func.param->var.name);
      }

      // visit body without creating a NEW scope for the top-level body block
      visit(n->func.body, 1);

      pop_scope();
      break;

    case ast_stmt:
      visit_stmt(n, is_func_body_block);
      break;

    default:
      visit_expr(n);
      break;
  }
}

/* entry point used by parser/main */
int semantic_check(astNode* root) {
  sem_error = 0;

  // reset scopes
  while (!scopes.empty()) pop_scope();
  push_scope();

  visit(root, 0);

  pop_scope();
  return sem_error ? 1 : 0;
}