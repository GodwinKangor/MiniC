# MiniC assembly code generator Makefile

CXX := clang++
LLVM_CONFIG := llvm-config-17

TARGET := minic_codegen.out
BUILD_DIR := build
TEST_BUILD_DIR := $(BUILD_DIR)/tests

CXXFLAGS := -std=c++17 -Wall -Wextra -O0 -g $(shell $(LLVM_CONFIG) --cxxflags)
LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags --libs core irreader support analysis)

CPP_SRCS := assembly_gen.cpp main.cpp
OBJS := \
	$(BUILD_DIR)/assembly_gen.o \
	$(BUILD_DIR)/main.o

IR_TEST_DIR := ../Optimization/optimizer_test_results
IR_TESTS := \
	p2_common_subexpr_opt \
	p3_const_prop_opt \
	p4_const_prop_opt \
	p5_const_prop_opt

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/assembly_gen.o: assembly_gen.cpp assembly_gen.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: main.cpp assembly_gen.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --> make run FILE=../Optimization/optimizer_test_results/p3_const_prop_opt.ll OUT=build/tests/p3_const_prop_opt.s
run: $(TARGET)
	@test -n "$(FILE)" || (echo "Usage: make run FILE=path/to/input.ll OUT=path/to/output.s"; exit 1)
	@test -n "$(OUT)" || (echo "Usage: make run FILE=path/to/input.ll OUT=path/to/output.s"; exit 1)
	./$(TARGET) $(FILE) $(OUT)

test: $(TARGET) | $(TEST_BUILD_DIR)
	@set -e; \
	for sample in $(IR_TESTS); do \
		echo "==> $$sample.ll"; \
		./$(TARGET) $(IR_TEST_DIR)/$$sample.ll $(TEST_BUILD_DIR)/$$sample.s; \
	done
	@echo "Assembly files written to $(TEST_BUILD_DIR)/"

$(TEST_BUILD_DIR): | $(BUILD_DIR)
	mkdir -p $(TEST_BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
