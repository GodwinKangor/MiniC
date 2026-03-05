# Author: Godwin Kangor
# Date : 02/13/26
# Makefile

CLANG ?= clang
LLVM_CONFIG ?= llvm-config-17

LLVM_CFLAGS  := $(shell $(LLVM_CONFIG) --cflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBDIR  := $(shell $(LLVM_CONFIG) --libdir)
LLVM_LIBS    := $(shell $(LLVM_CONFIG) --libs core irreader support analysis)

TARGET = ./llvm_parser
SRC = llvm_parser.c

TESTDIR = optimizer_test_results
OUTFILE = test_new.ll
TMP_OUT = .tmp_opt_out.ll

CFOLD_TESTS = cfold_add cfold_mul cfold_sub
CSE_TESTS   = p2_common_subexpr
CPROP_TESTS = p3_const_prop p4_const_prop p5_const_prop

all: $(TARGET)

$(TARGET): $(SRC)
	$(CLANG) -g $(LLVM_CFLAGS) $(SRC) -o $(TARGET) $(LLVM_LDFLAGS) $(LLVM_LIBS) -Wl,-rpath,$(LLVM_LIBDIR)

# Usage: make run FILE=path/to/input.ll MODE=all|cfold|cse|constprop
run: $(TARGET)
	@test -n "$(FILE)" || (echo "Usage: make run FILE=path/to/input.ll MODE=all|cfold|cse|constprop"; exit 1)
	$(TARGET) $(FILE) $(MODE)

# -----------------------------
# Test harness
# -----------------------------

# Runs optimizer on <name>.ll and compares against <name>_opt.ll
# Example: make check NAME=cfold_add
check: $(TARGET)
	@test -n "$(NAME)" || (echo "Usage: make check NAME=<test_base_name>"; exit 1)
	@set -e; \
	MODE=all; \
	case "$(NAME)" in \
		cfold_*) MODE=cfold ;; \
		*common_subexpr*) MODE=cse ;; \
		*const_prop*) MODE=constprop ;; \
		*) MODE=all ;; \
	esac; \
	echo "==> $(TESTDIR)/$(NAME).ll (mode=$$MODE)"; \
		test -f "$(TESTDIR)/$(NAME).ll" || (echo "ERROR: missing input $(TESTDIR)/$(NAME).ll"; exit 1); \
		test -f "$(TESTDIR)/$(NAME)_opt.ll" || (echo "ERROR: missing expected output $(TESTDIR)/$(NAME)_opt.ll"; exit 1); \
	rm -f $(OUTFILE) $(TMP_OUT) .tmp_expected.ll .tmp_got.ll; \
	$(TARGET) "$(TESTDIR)/$(NAME).ll" $$MODE > /dev/null; \
	test -f $(OUTFILE) || (echo "ERROR: expected optimizer to create $(OUTFILE)"; exit 1); \
	mv $(OUTFILE) $(TMP_OUT); \
	sed -e '/^; ModuleID =/d' -e '/^!llvm\.module\.flags/,$$d' "$(TESTDIR)/$(NAME)_opt.ll" > .tmp_expected.ll; \
	sed -e '/^; ModuleID =/d' -e '/^!llvm\.module\.flags/,$$d' $(TMP_OUT) > .tmp_got.ll; \
	if diff -u .tmp_expected.ll .tmp_got.ll > /dev/null; then \
		echo "PASS: $(NAME)"; \
	else \
		echo "FAIL: $(NAME)"; \
		echo "--- diff (expected vs got, normalized) ---"; \
		diff -u .tmp_expected.ll .tmp_got.ll | sed -n '1,200p'; \
		exit 1; \
	fi; \
	rm -f $(TMP_OUT) .tmp_expected.ll .tmp_got.ll

cfold-test:
	@set -e; for t in $(CFOLD_TESTS); do \
		$(MAKE) --no-print-directory check NAME=$$t; \
	done

cse-test:
	@set -e; for t in $(CSE_TESTS); do \
		$(MAKE) --no-print-directory check NAME=$$t; \
	done

constprop-test:
	@set -e; for t in $(CPROP_TESTS); do \
		$(MAKE) --no-print-directory check NAME=$$t; \
	done

# Run all optimizer tests (constant folding + common subexpr + const prop)
alltest: cfold-test cse-test constprop-test
	@echo "All optimizer tests passed."

clean:
	rm -f $(TARGET) $(OUTFILE) $(TMP_OUT) .tmp_expected.ll .tmp_got.ll

.PHONY: all run check cfold-test cse-test constprop-test alltest clean