%{
/* Author : Godwin Kangpr
 * Date : 01/28/26
 * scanner.l 
 */
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>

  #include "y.tab.h"
%}



%%

"extern"        { return EXTERN; }
"void"          { return VOID; }
"int"           { return INT; }
"if"            { return IF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"return"        { return RETURN; }
"print"         { return PRINT; }
"read"          { return READ; }

">="            { return GE; }
"<="            { return LE; }
"=="            { return EQ; }
"!="            { return NE; }

[0-9]+                  { yylval.ival = atoi(yytext); return NUM; }
[A-Za-z_][A-Za-z0-9_]*   { yylval.sval = strdup(yytext); return ID; }

[ \t\r\n]+      { /* skip whitespace */ }

[+\-*/=;()\{\}<>] { return yytext[0]; }

.                { return yytext[0]; }

%%
int yywrap(void) { return 1; }