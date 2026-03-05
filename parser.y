%{
/* Author : Godwin Kangpr
 * Date : 01/28/26
 *parser.y — miniC parser (Bison)
 * This Part 1 parser builds an AST using ast/ast.h + ast/ast.c.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ast.h"

/* Flex interface */
extern int yylex();
extern int yyparse();
extern FILE *yyin;
extern int yylineno;   /* requires %option yylineno in scanner.l */
extern char *yytext;   /* current token text from the lexer */
extern int semantic_check(astNode* root);

void yyerror(const char *);

/* Root AST node produced by parsing */
astNode *root = nullptr;
%}

%code requires {
  #include <vector>
  #include "ast.h"
}

/*
 * --->NOTE:
 *   scanner.l includes y.tab.h, which contains the %union definition.
 *   Because our %union uses C++ types (std::vector<...>) and AST types,
 *   we must include <vector> and "ast.h" in %code requires so BOTH the
 *   parser (y.tab.c) and lexer (lex.yy.c) see those definitions.
 */

%union {
  int ival;
  char *sval;
  astNode *node;
  std::vector<astNode*> *vec;
  rop_type rop;
}

%start program

/* keywords */
%token EXTERN VOID INT
%token IF ELSE WHILE RETURN
%token PRINT READ

/* identifiers / constants */
%token <sval> ID
%token <ival> NUM

/* 2-char relational ops */
%token LE GE EQ NE

/* nonterminal types */
%type <node> program extern_decl func_def param_opt block stmt decl_stmt asgn_stmt while_stmt if_stmt return_stmt print_stmt expr aexpr cond factor
%type <vec>  decl_list stmt_list
%type <rop>  relop

/* precedence */
%left '+' '-'
%left '*' '/'
%right UMINUS
%nonassoc IFX
%nonassoc ELSE

%%

/* ===== program structure ===== */
program
  : extern_decl extern_decl func_def
    {
      root = createProg($1, $2, $3);
      $$ = root;
    }
  ;

/* ===== extern prototypes ===== */
extern_decl
  : EXTERN VOID PRINT '(' INT ')' ';'
    { $$ = createExtern("print"); }
  | EXTERN INT READ '(' ')' ';'
    { $$ = createExtern("read"); }
  ;

/* ===== function definition ===== */
func_def
  : INT ID '(' param_opt ')' block
    {
      $$ = createFunc($2, $4, $6);
      free($2);
    }
  ;

param_opt
  : /* empty */      { $$ = NULL; }
  | INT ID
    {
      $$ = createVar($2);
      free($2);
    }
  ;

/* ===== blocks: decls first, then stmts ===== */
block
  : '{' decl_list stmt_list '}'
    {
      std::vector<astNode*> *all = new std::vector<astNode*>();

      if ($2) { all->insert(all->end(), $2->begin(), $2->end()); delete $2; }
      if ($3) { all->insert(all->end(), $3->begin(), $3->end()); delete $3; }

      $$ = createBlock(all);
    }
  ;

decl_list
  : /* empty */          { $$ = new std::vector<astNode*>(); }
  | decl_list decl_stmt  { $1->push_back($2); $$ = $1; }
  ;

decl_stmt
  : INT ID ';'
    {
      $$ = createDecl($2);
      free($2);
    }
  ;

/* ===== statements ===== */
stmt_list
  : /* empty */      { $$ = new std::vector<astNode*>(); }
  | stmt_list stmt   { $1->push_back($2); $$ = $1; }
  ;

stmt
  : asgn_stmt ';'     { $$ = $1; }
  | print_stmt ';'    { $$ = $1; }
  | return_stmt ';'   { $$ = $1; }
  | while_stmt        { $$ = $1; }
  | if_stmt           { $$ = $1; }
  | block             { $$ = $1; }
  ;

asgn_stmt
  : ID '=' expr
    {
      astNode *lhs = createVar($1);
      free($1);
      $$ = createAsgn(lhs, $3);
    }
  ;

print_stmt
  : PRINT '(' expr ')'
    { $$ = createCall("print", $3); }
  ;

return_stmt
  : RETURN '(' expr ')'
    { $$ = createRet($3); }
  | RETURN expr
    { $$ = createRet($2); }
  ;
  
while_stmt
  : WHILE '(' cond ')' stmt
    { $$ = createWhile($3, $5); }
  ;

if_stmt
  : IF '(' cond ')' stmt %prec IFX
    { $$ = createIf($3, $5, NULL); }
  | IF '(' cond ')' stmt ELSE stmt
    { $$ = createIf($3, $5, $7); }
  ;

/* ===== conditions (relational only in Part 1) ===== */
cond
  : aexpr relop aexpr
    { $$ = createRExpr($1, $3, $2); }
  ;

relop
  : '<' { $$ = lt; }
  | '>' { $$ = gt; }
  | LE  { $$ = le; }
  | GE  { $$ = ge; }
  | EQ  { $$ = eq; }
  | NE  { $$ = neq; }
  ;

/* ===== expressions (precedence via left recursion) ===== */
expr
  : aexpr { $$ = $1; }
  ;

aexpr
  : aexpr '+' aexpr          { $$ = createBExpr($1, $3, add); }
  | aexpr '-' aexpr          { $$ = createBExpr($1, $3, sub); }
  | aexpr '*' aexpr          { $$ = createBExpr($1, $3, mul); }
  | aexpr '/' aexpr          { $$ = createBExpr($1, $3, divide); }
  | '-' aexpr %prec UMINUS   { $$ = createUExpr($2, uminus); }
  | factor                   { $$ = $1; }
  ;

/* ===== factors ===== */
factor
  : NUM              { $$ = createCnst($1); }
  | ID               { $$ = createVar($1); free($1); }
  | READ '(' ')'     { $$ = createCall("read", NULL); }
  | '(' aexpr ')'    { $$ = $2; }
  ;

%%

/*
 * yyerror: called by Bison on syntax errors.
 * prints line number + the current lexer token to make debugging easier.
 */
void yyerror(const char *s) {
  fprintf(stderr,
          "parse error at line %d near '%s': %s\n",
          yylineno,
          (yytext ? yytext : ""),
          s);
}

/*
 * main:
 *   - If a filename is given, parse that file.
 *   - else, parse stdin.
 */
int main(int argc, char **argv) {
  if (argc == 2) {
    yyin = fopen(argv[1], "r");
    if (!yyin) { perror("fopen"); return 1; }
  }
  int rc = yyparse();
  if (rc == 0 && root) {
  if (semantic_check(root) != 0) return 1;
  }

  return rc;

  // optional AST print:
  // if (rc == 0 && root) printNode(root, 0);

  if (argc == 2 && yyin) fclose(yyin);
  return rc;
}