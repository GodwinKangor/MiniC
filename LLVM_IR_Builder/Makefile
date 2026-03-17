LLVM_CONFIG := llvm-config-17
CXX := clang++
YACC := bison
LEX := flex

TARGET := minic_ir.out
LIBOBJ := llvm_builder.o
BUILD_DIR := ../build
LOCAL_BUILD_DIR := build

CXXFLAGS := -std=c++17 -Wall -Wextra -O0 -g $(shell $(LLVM_CONFIG) --cxxflags) -I../ast -I$(BUILD_DIR)
PARSER_CXXFLAGS := $(CXXFLAGS) -Dmain=minic_frontend_parser_main
LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags --libs core)

PARSER_SRC := ../parser.y
SCANNER_SRC := ../scanner.l
PARSER_GEN := $(BUILD_DIR)/y.tab.c
PARSER_HDR := $(BUILD_DIR)/y.tab.h
SCANNER_GEN := $(BUILD_DIR)/lex.yy.c

TESTDIR := builder_tests
TESTCS := $(filter-out $(TESTDIR)/main.c,$(wildcard $(TESTDIR)/*.c))
TESTLL := $(patsubst $(TESTDIR)/%.c,$(LOCAL_BUILD_DIR)/%.ll,$(TESTCS))

.PHONY: all test maketest clean

all: $(TARGET)

$(TARGET): $(LIBOBJ) $(LOCAL_BUILD_DIR)/main.o $(LOCAL_BUILD_DIR)/y.tab.o $(LOCAL_BUILD_DIR)/lex.yy.o $(LOCAL_BUILD_DIR)/semantic.o $(LOCAL_BUILD_DIR)/ast.o
	$(CXX) $^ -o $@ $(LDFLAGS)

$(LIBOBJ): llvm_builder.cpp llvm_builder.h ../ast/ast.h
	$(CXX) $(CXXFLAGS) -c llvm_builder.cpp -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LOCAL_BUILD_DIR):
	mkdir -p $(LOCAL_BUILD_DIR)

$(PARSER_GEN) $(PARSER_HDR): $(PARSER_SRC) | $(BUILD_DIR)
	$(YACC) -d -o $(PARSER_GEN) $(PARSER_SRC)

$(SCANNER_GEN): $(SCANNER_SRC) $(PARSER_HDR) | $(BUILD_DIR)
	$(LEX) -o $@ $(SCANNER_SRC)

$(LOCAL_BUILD_DIR)/y.tab.o: $(PARSER_GEN) ../ast/ast.h | $(LOCAL_BUILD_DIR)
	$(CXX) $(PARSER_CXXFLAGS) -c $< -o $@

$(LOCAL_BUILD_DIR)/lex.yy.o: $(SCANNER_GEN) $(PARSER_HDR) | $(LOCAL_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LOCAL_BUILD_DIR)/semantic.o: ../semantic.c ../ast/ast.h | $(LOCAL_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LOCAL_BUILD_DIR)/ast.o: ../ast/ast.c ../ast/ast.h | $(LOCAL_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(LOCAL_BUILD_DIR)/main.o: main.cpp llvm_builder.h ../ast/ast.h | $(LOCAL_BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

maketest: $(TESTLL)

test: maketest

$(LOCAL_BUILD_DIR)/%.ll: $(TESTDIR)/%.c | $(TARGET) $(LOCAL_BUILD_DIR)
	./$(TARGET) $< $@

clean:
	rm -f $(TARGET) $(LIBOBJ)
	rm -rf $(LOCAL_BUILD_DIR)
	rm -f $(TESTDIR)/*.ll
