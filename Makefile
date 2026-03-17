.PHONY: all frontend test test-all test-frontend semtest ir opt codegen test-ir test-opt test-codegen test-codegen-all test-pipeline clean clean-frontend

CXX := g++
YACC := bison
LEX := flex

FRONTEND_TARGET := minic_frontend
BUILD_DIR := build

CXXFLAGS := -std=c++17 -Wall -Wextra -O0 -g -Iast
LDFLAGS := -lfl

PARSER_SRC := parser.y
SCANNER_SRC := scanner.l
PARSER_GEN := $(BUILD_DIR)/y.tab.c
PARSER_HDR := $(BUILD_DIR)/y.tab.h
SCANNER_GEN := $(BUILD_DIR)/lex.yy.c

FRONTEND_SRCS := $(PARSER_GEN) $(SCANNER_GEN) semantic.c ast/ast.c

PARSER_TESTS := $(sort $(filter-out parser_tests/main.c parser_tests/p_bad.c,$(wildcard parser_tests/*.c)))
SEM_GOOD_TESTS := $(sort $(wildcard semantic_analysis_tests/*_good.c))
SEM_BAD_TESTS := $(sort $(wildcard semantic_analysis_tests/*_bad.c))
IR_BUILDER_SOURCES := $(sort $(filter-out LLVM_IR_Builder/builder_tests/main.c,$(wildcard LLVM_IR_Builder/builder_tests/*.c)))
ASSGEN_PIPELINE_SOURCES := Assembly_Code_Gen/assembly_gen_tests/fib.c Assembly_Code_Gen/assembly_gen_tests/rem_2.c Assembly_Code_Gen/assembly_gen_tests/square.c Assembly_Code_Gen/assembly_gen_tests/sum_n.c
PIPELINE_SOURCES := $(sort $(PARSER_TESTS) $(SEM_GOOD_TESTS) $(IR_BUILDER_SOURCES) $(ASSGEN_PIPELINE_SOURCES))
PIPELINE_BUILD_DIR := $(BUILD_DIR)/pipeline

all: frontend ir opt codegen

frontend: $(FRONTEND_TARGET)

$(FRONTEND_TARGET): $(FRONTEND_SRCS)
	$(CXX) $(CXXFLAGS) $(FRONTEND_SRCS) $(LDFLAGS) -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PARSER_GEN) $(PARSER_HDR): $(PARSER_SRC) | $(BUILD_DIR)
	$(YACC) -d -o $(PARSER_GEN) $(PARSER_SRC)

$(SCANNER_GEN): $(SCANNER_SRC) $(PARSER_HDR) | $(BUILD_DIR)
	$(LEX) -o $@ $(SCANNER_SRC)

test: test-all

test-all: test-frontend semtest test-ir test-opt test-codegen test-pipeline

test-pipeline: test-codegen
	@mkdir -p $(PIPELINE_BUILD_DIR)
	@set -e; \
	failed=""; \
	for src in $(PIPELINE_SOURCES); do \
		stem=$$(printf '%s' "$$src" | sed 's#/#_#g; s#\.c$$##'); \
		raw_ll="$(PIPELINE_BUILD_DIR)/$$stem.ll"; \
		opt_ll="$(PIPELINE_BUILD_DIR)/$$stem.opt.ll"; \
		asm_out="$(PIPELINE_BUILD_DIR)/$$stem.s"; \
		echo "==> $$src"; \
		if ./LLVM_IR_Builder/minic_ir.out "$$src" "$$raw_ll" && \
		   (cd Optimization && ./llvm_parser "../$$raw_ll" all > /dev/null) && \
		   cp Optimization/test_new.ll "$$opt_ll" && \
		   ./Assembly_Code_Gen/minic_codegen.out "$$opt_ll" "$$asm_out"; then \
			echo "PASS: $$asm_out"; \
		else \
			echo "FAIL: $$src"; \
			rm -f "$$raw_ll" "$$opt_ll" "$$asm_out"; \
			failed="$$failed $$src"; \
		fi; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "Pipeline failed for:$$failed"; \
		exit 1; \
	fi; \
	echo "Pipeline outputs written to $(PIPELINE_BUILD_DIR)/"

test-frontend: frontend
	@set -e; \
	for test in $(PARSER_TESTS); do \
		echo "==> $$test"; \
		./$(FRONTEND_TARGET) "$$test"; \
	done; \
	echo "==> parser_tests/p_bad.c"; \
	if ./$(FRONTEND_TARGET) parser_tests/p_bad.c; then \
		echo "FAIL: parser_tests/p_bad.c was expected to fail"; \
		exit 1; \
	fi

semtest: frontend
	@set -e; \
	for test in $(SEM_GOOD_TESTS); do \
		echo "==> $$test"; \
		./$(FRONTEND_TARGET) "$$test"; \
	done; \
	for test in $(SEM_BAD_TESTS); do \
		echo "==> $$test"; \
		if ./$(FRONTEND_TARGET) "$$test"; then \
			echo "FAIL: $$test was expected to fail semantic analysis"; \
			exit 1; \
		fi; \
	done

ir:
	$(MAKE) -C LLVM_IR_Builder

opt: ir
	$(MAKE) -C Optimization

codegen: opt
	$(MAKE) -C Assembly_Code_Gen

test-ir:
	$(MAKE) -C LLVM_IR_Builder test

test-opt: opt
	$(MAKE) -C Optimization alltest

test-codegen: codegen
	$(MAKE) -C Assembly_Code_Gen test

test-codegen-all: codegen
	$(MAKE) -C Assembly_Code_Gen test

clean-frontend:
	rm -f $(FRONTEND_TARGET) minic_frontend.out minic_codegen.out parser.output
	rm -rf $(BUILD_DIR)

clean:
	$(MAKE) clean-frontend
	$(MAKE) -C LLVM_IR_Builder clean
	$(MAKE) -C Optimization clean
	$(MAKE) -C Assembly_Code_Gen clean
