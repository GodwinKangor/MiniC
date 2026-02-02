#Author : Godwin Kangor
#Date : 01/28/26
#Makefile


filename = minic_frontend

YACC = bison
LEX  = flex
CXX  = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -g -Iast


$(filename).out: parser.y scanner.l ast/ast.c ast/ast.h
	$(YACC) -d -o y.tab.c parser.y
	$(LEX) -o lex.yy.c scanner.l
	$(CXX) $(CXXFLAGS) y.tab.c lex.yy.c ast/ast.c -lfl -o $(filename).out


test: $(filename).out
	@set -e; \
	for f in parser_tests/p1.c parser_tests/p2.c parser_tests/p3.c parser_tests/p4.c parser_tests/p5.c; do \
		echo "==> $$f"; \
		./$(filename).out $$f > /dev/null; \
	done; \
	echo "==> parser_tests/p_bad.c (should fail)"; \
	if ./$(filename).out parser_tests/p_bad.c > /dev/null; then \
		echo "ERROR: p_bad.c unexpectedly parsed successfully"; exit 1; \
	else \
		echo "OK: p_bad.c failed as expected"; \
	fi

clean:
	rm -f $(filename).out lex.yy.c y.tab.c y.tab.h