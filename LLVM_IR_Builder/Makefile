LLVMCODE = llvm_builder
LLVM_CONFIG = llvm-config-17
LLVM_C_INC = /usr/include/llvm-c-17/
TESTDIR = builder_tests
TESTCS = $(wildcard $(TESTDIR)/*.c)
TESTLL = $(TESTCS:.c=.ll)

.PHONY: all test maketest clean

all: $(LLVMCODE).o

$(LLVMCODE).o: $(LLVMCODE).cpp ast.h
	clang++ -g `$(LLVM_CONFIG) --cxxflags` -I $(LLVM_C_INC) -c $(LLVMCODE).cpp -o $(LLVMCODE).o

maketest: $(TESTLL)

test: maketest

$(TESTDIR)/%.ll: $(TESTDIR)/%.c
	clang -S -emit-llvm $< -o $@

clean:
	rm -f *.o
	rm -f $(TESTDIR)/*.ll
