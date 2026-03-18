#include "assembly_gen.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/IRReader.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kSpilled = -1;
constexpr int kNumRegs = 3;
const char *kRegNames[kNumRegs] = {"%ebx", "%ecx", "%edx"};
const char *kByteRegNames[kNumRegs] = {"%bl", "%cl", "%dl"};

struct LiveRange {
  int start = -1;
  int end = -1;
  int useCount = 0;
};

struct BBAnalysis {
  std::unordered_map<LLVMValueRef, int> instIndex;
  std::unordered_map<LLVMValueRef, LiveRange> liveRange;
  std::unordered_map<LLVMValueRef, int> regMap;
};

struct FunctionContext {
  FILE *out = nullptr;
  std::unordered_map<LLVMBasicBlockRef, std::string> bbLabels;
  std::unordered_map<LLVMValueRef, int> offsetMap;
  int localMem = 4;
};

[[noreturn]] void codegenError(const char *msg) {
  std::fprintf(stderr, "assembly codegen error: %s\n", msg);
  std::exit(1);
}

void emitLine(FILE *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(out, fmt, ap);
  va_end(ap);
  std::fputc('\n', out);
}

bool isConstInt(LLVMValueRef value) {
  return value && LLVMIsAConstantInt(value);
}

bool isArgument(LLVMValueRef value) {
  return value && LLVMIsAArgument(value);
}

bool isInstruction(LLVMValueRef value) {
  return value && LLVMIsAInstruction(value);
}

bool isTrackableValue(LLVMValueRef value) {
  if (!isInstruction(value) || LLVMIsAAllocaInst(value)) {
    return false;
  }
  return LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMVoidTypeKind;
}

bool isNonVoidCall(LLVMValueRef inst) {
  return LLVMGetInstructionOpcode(inst) == LLVMCall &&
         LLVMGetTypeKind(LLVMTypeOf(inst)) != LLVMVoidTypeKind;
}

std::string valueName(LLVMValueRef value) {
  size_t len = 0;
  const char *name = LLVMGetValueName2(value, &len);
  if (!name || len == 0) {
    return "";
  }
  return std::string(name, len);
}

const char *regName(int reg) {
  if (reg < 0 || reg >= kNumRegs) {
    codegenError("invalid register index");
  }
  return kRegNames[reg];
}

const char *byteRegName(int reg) {
  if (reg < 0 || reg >= kNumRegs) {
    codegenError("invalid byte-register index");
  }
  return kByteRegNames[reg];
}

std::string conditionSuffix(LLVMIntPredicate pred) {
  switch (pred) {
    case LLVMIntSLT: return "l";
    case LLVMIntSGT: return "g";
    case LLVMIntSLE: return "le";
    case LLVMIntSGE: return "ge";
    case LLVMIntEQ: return "e";
    case LLVMIntNE: return "ne";
    default: codegenError("unsupported integer predicate");
  }
}

std::string blockLabelName(LLVMBasicBlockRef bb, int index) {
  const char *name = LLVMGetBasicBlockName(bb);
  if (name && *name) {
    return ".L" + std::string(name);
  }
  return ".Lbb_" + std::to_string(index);
}

void createBBLabels(LLVMValueRef func, FunctionContext &ctx) {
  int index = 0;
  for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func); bb;
       bb = LLVMGetNextBasicBlock(bb), ++index) {
    ctx.bbLabels[bb] = blockLabelName(bb, index);
  }
}

void printDirectives(LLVMValueRef func, FILE *out) {
  const std::string name = valueName(func);
  emitLine(out, "    .text");
  emitLine(out, "    .globl %s", name.c_str());
  emitLine(out, "    .type %s, @function", name.c_str());
  emitLine(out, "%s:", name.c_str());
}

void printFunctionEnd(FILE *out) {
  emitLine(out, "    leave");
  emitLine(out, "    ret");
}

int getOffset(const FunctionContext &ctx, LLVMValueRef value) {
  auto it = ctx.offsetMap.find(value);
  if (it == ctx.offsetMap.end()) {
    codegenError("missing stack offset");
  }
  return it->second;
}

void getOffsetMap(LLVMValueRef func, FunctionContext &ctx) {
  ctx.offsetMap.clear();
  ctx.localMem = 4;

  if (LLVMCountParams(func) > 0) {
    ctx.offsetMap[LLVMGetParam(func, 0)] = 8;
  }

  for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func); bb;
       bb = LLVMGetNextBasicBlock(bb)) {
    for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
         inst = LLVMGetNextInstruction(inst)) {
      const LLVMOpcode opcode = LLVMGetInstructionOpcode(inst);
      if (opcode == LLVMAlloca) {
        ctx.localMem += 4;
        ctx.offsetMap[inst] = -ctx.localMem;
        continue;
      }

      if (opcode == LLVMStore) {
        LLVMValueRef src = LLVMGetOperand(inst, 0);
        LLVMValueRef dst = LLVMGetOperand(inst, 1);
        if (isArgument(src)) {
          ctx.offsetMap[dst] = getOffset(ctx, src);
        } else if (!isConstInt(src)) {
          ctx.offsetMap[src] = getOffset(ctx, dst);
        }
        continue;
      }

      if (opcode == LLVMLoad) {
        LLVMValueRef ptr = LLVMGetOperand(inst, 0);
        ctx.offsetMap[inst] = getOffset(ctx, ptr);
      }
    }
  }
}

void computeLiveness(LLVMBasicBlockRef bb, BBAnalysis &analysis) {
  analysis.instIndex.clear();
  analysis.liveRange.clear();
  analysis.regMap.clear();

  int index = 0;
  for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
       inst = LLVMGetNextInstruction(inst)) {
    if (LLVMGetInstructionOpcode(inst) == LLVMAlloca) {
      continue;
    }
    analysis.instIndex[inst] = index;
    if (isTrackableValue(inst)) {
      analysis.liveRange[inst] = LiveRange{index, index, 0};
    }
    ++index;
  }

  for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
       inst = LLVMGetNextInstruction(inst)) {
    auto idxIt = analysis.instIndex.find(inst);
    if (idxIt == analysis.instIndex.end()) {
      continue;
    }
    const int instIdx = idxIt->second;
    const int numOperands = LLVMGetNumOperands(inst);
    for (int i = 0; i < numOperands; ++i) {
      LLVMValueRef operand = LLVMGetOperand(inst, i);
      if (!isTrackableValue(operand)) {
        continue;
      }
      if (LLVMGetInstructionParent(operand) != bb) {
        continue;
      }
      auto rangeIt = analysis.liveRange.find(operand);
      if (rangeIt == analysis.liveRange.end()) {
        continue;
      }
      rangeIt->second.end = std::max(rangeIt->second.end, instIdx);
      rangeIt->second.useCount += 1;
    }
  }
}

bool isRegisterReusedOp(LLVMOpcode opcode) {
  return opcode == LLVMAdd || opcode == LLVMSub || opcode == LLVMMul;
}

void freeEndedOperands(const BBAnalysis &analysis, LLVMValueRef inst,
                       std::vector<int> &available, int skipReg) {
  const int idx = analysis.instIndex.at(inst);
  const int numOperands = LLVMGetNumOperands(inst);
  for (int i = 0; i < numOperands; ++i) {
    LLVMValueRef operand = LLVMGetOperand(inst, i);
    auto regIt = analysis.regMap.find(operand);
    auto rangeIt = analysis.liveRange.find(operand);
    if (regIt == analysis.regMap.end() || rangeIt == analysis.liveRange.end()) {
      continue;
    }
    const int reg = regIt->second;
    if (reg == kSpilled || reg == skipReg) {
      continue;
    }
    if (rangeIt->second.end == idx && !available[reg]) {
      available[reg] = 1;
    }
  }
}

LLVMValueRef findSpillCandidate(LLVMValueRef inst, const BBAnalysis &analysis) {
  const LiveRange &current = analysis.liveRange.at(inst);
  LLVMValueRef best = nullptr;
  for (const auto &entry : analysis.liveRange) {
    LLVMValueRef candidate = entry.first;
    const LiveRange &range = entry.second;
    auto regIt = analysis.regMap.find(candidate);
    if (regIt == analysis.regMap.end() || regIt->second == kSpilled) {
      continue;
    }
    const bool overlaps = !(range.end < current.start || current.end < range.start);
    if (!overlaps) {
      continue;
    }
    if (!best) {
      best = candidate;
      continue;
    }
    const LiveRange &bestRange = analysis.liveRange.at(best);
    if (range.end > bestRange.end ||
        (range.end == bestRange.end && range.useCount < bestRange.useCount)) {
      best = candidate;
    }
  }
  return best;
}

void assignRegistersForBB(LLVMBasicBlockRef bb, BBAnalysis &analysis) {
  computeLiveness(bb, analysis);
  std::vector<int> available(kNumRegs, 1);

  for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
       inst = LLVMGetNextInstruction(inst)) {
    auto idxIt = analysis.instIndex.find(inst);
    if (idxIt == analysis.instIndex.end()) {
      continue;
    }

    const LLVMOpcode opcode = LLVMGetInstructionOpcode(inst);
    if (!isTrackableValue(inst)) {
      freeEndedOperands(analysis, inst, available, kSpilled);
      continue;
    }

    int assignedReg = kSpilled;
    if (isRegisterReusedOp(opcode)) {
      LLVMValueRef lhs = LLVMGetOperand(inst, 0);
      auto lhsRegIt = analysis.regMap.find(lhs);
      auto lhsRangeIt = analysis.liveRange.find(lhs);
      if (lhsRegIt != analysis.regMap.end() && lhsRangeIt != analysis.liveRange.end() &&
          lhsRegIt->second != kSpilled &&
          lhsRangeIt->second.end == idxIt->second) {
        assignedReg = lhsRegIt->second;
      }
    }

    if (assignedReg == kSpilled) {
      for (int reg = 0; reg < kNumRegs; ++reg) {
        if (available[reg]) {
          assignedReg = reg;
          available[reg] = 0;
          break;
        }
      }
    }

    if (assignedReg == kSpilled) {
      LLVMValueRef spill = findSpillCandidate(inst, analysis);
      if (spill) {
        const LiveRange &instRange = analysis.liveRange.at(inst);
        const LiveRange &spillRange = analysis.liveRange.at(spill);
        if (spillRange.end > instRange.end) {
          assignedReg = analysis.regMap.at(spill);
          analysis.regMap[spill] = kSpilled;
        }
      }
    }

    analysis.regMap[inst] = assignedReg;
    freeEndedOperands(analysis, inst, available, assignedReg);

    if (assignedReg != kSpilled && analysis.liveRange.at(inst).end == idxIt->second) {
      available[assignedReg] = 1;
    }
  }
}

int getAssignedReg(LLVMValueRef value, const BBAnalysis &analysis) {
  auto it = analysis.regMap.find(value);
  if (it == analysis.regMap.end()) {
    return kSpilled;
  }
  return it->second;
}

void emitMoveOperandToReg(FILE *out, LLVMValueRef operand, int reg,
                          const FunctionContext &ctx,
                          const BBAnalysis &analysis) {
  if (isConstInt(operand)) {
    emitLine(out, "    movl $%lld, %s",
             LLVMConstIntGetSExtValue(operand), regName(reg));
    return;
  }
  if (isArgument(operand)) {
    emitLine(out, "    movl %d(%%ebp), %s", getOffset(ctx, operand), regName(reg));
    return;
  }
  if (!isInstruction(operand)) {
    codegenError("unsupported operand kind");
  }

  const int operandReg = getAssignedReg(operand, analysis);
  if (operandReg != kSpilled) {
    if (operandReg != reg) {
      emitLine(out, "    movl %s, %s", regName(operandReg), regName(reg));
    }
    return;
  }

  emitLine(out, "    movl %d(%%ebp), %s", getOffset(ctx, operand), regName(reg));
}

void emitStoreToMemory(FILE *out, LLVMValueRef value, int offset,
                       const FunctionContext &ctx, const BBAnalysis &analysis) {
  if (isConstInt(value)) {
    emitLine(out, "    movl $%lld, %d(%%ebp)",
             LLVMConstIntGetSExtValue(value), offset);
    return;
  }
  if (isArgument(value)) {
    return;
  }
  if (!isInstruction(value)) {
    codegenError("unsupported store source");
  }

  const int assignedReg = getAssignedReg(value, analysis);
  if (assignedReg != kSpilled) {
    emitLine(out, "    movl %s, %d(%%ebp)", regName(assignedReg), offset);
    return;
  }

  emitLine(out, "    movl %d(%%ebp), %%eax", getOffset(ctx, value));
  emitLine(out, "    movl %%eax, %d(%%ebp)", offset);
}

bool branchUsesCompare(LLVMValueRef cond) {
  return isInstruction(cond) && LLVMGetInstructionOpcode(cond) == LLVMICmp;
}

void emitLoadInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                  const BBAnalysis &analysis) {
  const int assignedReg = getAssignedReg(inst, analysis);
  if (assignedReg == kSpilled) {
    return;
  }
  const int ptrOffset = getOffset(ctx, LLVMGetOperand(inst, 0));
  emitLine(out, "    movl %d(%%ebp), %s", ptrOffset, regName(assignedReg));
}

void emitStoreInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                   const BBAnalysis &analysis) {
  LLVMValueRef src = LLVMGetOperand(inst, 0);
  LLVMValueRef dst = LLVMGetOperand(inst, 1);
  if (isArgument(src)) {
    return;
  }
  emitStoreToMemory(out, src, getOffset(ctx, dst), ctx, analysis);
}

void emitCallInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                  const BBAnalysis &analysis) {
  emitLine(out, "    pushl %%ecx");
  emitLine(out, "    pushl %%edx");

  const unsigned numArgs = LLVMGetNumArgOperands(inst);
  if (numArgs == 1) {
    LLVMValueRef arg = LLVMGetOperand(inst, 0);
    if (isConstInt(arg)) {
      emitLine(out, "    pushl $%lld", LLVMConstIntGetSExtValue(arg));
    } else if (isArgument(arg)) {
      emitLine(out, "    pushl %d(%%ebp)", getOffset(ctx, arg));
    } else {
      const int argReg = getAssignedReg(arg, analysis);
      if (argReg != kSpilled) {
        emitLine(out, "    pushl %s", regName(argReg));
      } else {
        emitLine(out, "    pushl %d(%%ebp)", getOffset(ctx, arg));
      }
    }
  }

  LLVMValueRef callee = LLVMGetCalledValue(inst);
  std::string calleeName = valueName(callee);
  emitLine(out, "    call %s", calleeName.c_str());

  if (numArgs == 1) {
    emitLine(out, "    addl $4, %%esp");
  }
  emitLine(out, "    popl %%edx");
  emitLine(out, "    popl %%ecx");

  if (!isNonVoidCall(inst)) {
    return;
  }

  const int assignedReg = getAssignedReg(inst, analysis);
  if (assignedReg != kSpilled) {
    emitLine(out, "    movl %%eax, %s", regName(assignedReg));
  } else {
    emitLine(out, "    movl %%eax, %d(%%ebp)", getOffset(ctx, inst));
  }
}

void emitArithmeticInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                        const BBAnalysis &analysis) {
  const int assignedReg = getAssignedReg(inst, analysis);
  const bool spilled = assignedReg == kSpilled;
  const int targetReg = spilled ? 0 : assignedReg;

  LLVMValueRef lhs = LLVMGetOperand(inst, 0);
  LLVMValueRef rhs = LLVMGetOperand(inst, 1);
  emitMoveOperandToReg(out, lhs, targetReg, ctx, analysis);

  const char *op = nullptr;
  switch (LLVMGetInstructionOpcode(inst)) {
    case LLVMAdd: op = "addl"; break;
    case LLVMSub: op = "subl"; break;
    case LLVMMul: op = "imull"; break;
    default: codegenError("unsupported arithmetic opcode");
  }

  if (isConstInt(rhs)) {
    emitLine(out, "    %s $%lld, %s", op, LLVMConstIntGetSExtValue(rhs),
             regName(targetReg));
  } else if (isArgument(rhs)) {
    emitLine(out, "    %s %d(%%ebp), %s", op, getOffset(ctx, rhs),
             regName(targetReg));
  } else {
    const int rhsReg = getAssignedReg(rhs, analysis);
    if (rhsReg != kSpilled) {
      emitLine(out, "    %s %s, %s", op, regName(rhsReg), regName(targetReg));
    } else {
      emitLine(out, "    %s %d(%%ebp), %s", op, getOffset(ctx, rhs),
               regName(targetReg));
    }
  }

  if (spilled) {
    emitLine(out, "    movl %%eax, %d(%%ebp)", getOffset(ctx, inst));
  }
}

void emitCmpInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                 const BBAnalysis &analysis) {
  const int assignedReg = getAssignedReg(inst, analysis);
  const bool spilled = assignedReg == kSpilled;
  const int targetReg = spilled ? 0 : assignedReg;

  LLVMValueRef lhs = LLVMGetOperand(inst, 0);
  LLVMValueRef rhs = LLVMGetOperand(inst, 1);
  emitMoveOperandToReg(out, lhs, targetReg, ctx, analysis);

  if (isConstInt(rhs)) {
    emitLine(out, "    cmpl $%lld, %s", LLVMConstIntGetSExtValue(rhs),
             regName(targetReg));
  } else if (isArgument(rhs)) {
    emitLine(out, "    cmpl %d(%%ebp), %s", getOffset(ctx, rhs),
             regName(targetReg));
  } else {
    const int rhsReg = getAssignedReg(rhs, analysis);
    if (rhsReg != kSpilled) {
      emitLine(out, "    cmpl %s, %s", regName(rhsReg), regName(targetReg));
    } else {
      emitLine(out, "    cmpl %d(%%ebp), %s", getOffset(ctx, rhs),
               regName(targetReg));
    }
  }

  emitLine(out, "    movl $0, %s", regName(targetReg));
  emitLine(out, "    set%s %s",
           conditionSuffix(LLVMGetICmpPredicate(inst)).c_str(),
           byteRegName(targetReg));

  if (spilled) {
    emitLine(out, "    movl %%eax, %d(%%ebp)", getOffset(ctx, inst));
  }
}

void emitReturnInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                    const BBAnalysis &analysis) {
  if (LLVMGetNumOperands(inst) == 0) {
    emitLine(out, "    movl $0, %%eax");
  } else {
    LLVMValueRef retVal = LLVMGetOperand(inst, 0);
    if (isConstInt(retVal)) {
      emitLine(out, "    movl $%lld, %%eax", LLVMConstIntGetSExtValue(retVal));
    } else if (isArgument(retVal)) {
      emitLine(out, "    movl %d(%%ebp), %%eax", getOffset(ctx, retVal));
    } else {
      const int retReg = getAssignedReg(retVal, analysis);
      if (retReg != kSpilled) {
        emitLine(out, "    movl %s, %%eax", regName(retReg));
      } else {
        emitLine(out, "    movl %d(%%ebp), %%eax", getOffset(ctx, retVal));
      }
    }
  }
  emitLine(out, "    popl %%ebx");
  printFunctionEnd(out);
}

void emitBranchInst(FILE *out, LLVMValueRef inst, const FunctionContext &ctx) {
  if (!LLVMIsConditional(inst)) {
    emitLine(out, "    jmp %s",
             ctx.bbLabels.at(LLVMGetSuccessor(inst, 0)).c_str());
    return;
  }

  LLVMValueRef cond = LLVMGetCondition(inst);
  if (!branchUsesCompare(cond)) {
    codegenError("conditional branch must use icmp result");
  }

  const std::string trueLabel = ctx.bbLabels.at(LLVMGetSuccessor(inst, 0));
  const std::string falseLabel = ctx.bbLabels.at(LLVMGetSuccessor(inst, 1));
  emitLine(out, "    j%s %s",
           conditionSuffix(LLVMGetICmpPredicate(cond)).c_str(),
           trueLabel.c_str());
  emitLine(out, "    jmp %s", falseLabel.c_str());
}

void emitInstruction(FILE *out, LLVMValueRef inst, const FunctionContext &ctx,
                     const BBAnalysis &analysis) {
  switch (LLVMGetInstructionOpcode(inst)) {
    case LLVMAlloca:
      return;
    case LLVMLoad:
      emitLoadInst(out, inst, ctx, analysis);
      return;
    case LLVMStore:
      emitStoreInst(out, inst, ctx, analysis);
      return;
    case LLVMCall:
      emitCallInst(out, inst, ctx, analysis);
      return;
    case LLVMBr:
      emitBranchInst(out, inst, ctx);
      return;
    case LLVMRet:
      emitReturnInst(out, inst, ctx, analysis);
      return;
    case LLVMAdd:
    case LLVMSub:
    case LLVMMul:
      emitArithmeticInst(out, inst, ctx, analysis);
      return;
    case LLVMICmp:
      emitCmpInst(out, inst, ctx, analysis);
      return;
    default:
      codegenError("unsupported LLVM instruction in assembly generator");
  }
}

int emitAssemblyForFunction(LLVMValueRef func, FILE *out) {
  FunctionContext ctx;
  ctx.out = out;

  createBBLabels(func, ctx);
  getOffsetMap(func, ctx);
  printDirectives(func, out);
  emitLine(out, "    pushl %%ebp");
  emitLine(out, "    movl %%esp, %%ebp");
  emitLine(out, "    subl $%d, %%esp", ctx.localMem);
  emitLine(out, "    pushl %%ebx");

  for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func); bb;
       bb = LLVMGetNextBasicBlock(bb)) {
    emitLine(out, "%s:", ctx.bbLabels.at(bb).c_str());
    BBAnalysis analysis;
    assignRegistersForBB(bb, analysis);
    for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
         inst = LLVMGetNextInstruction(inst)) {
      emitInstruction(out, inst, ctx, analysis);
    }
  }

  emitLine(out, "    .size %s, .-%s", valueName(func).c_str(),
           valueName(func).c_str());
  return 0;
}

}  // namespace

int emitAssemblyFromModule(LLVMModuleRef module, const char *outputPath) {
  FILE *out = std::fopen(outputPath, "w");
  if (!out) {
    std::perror("fopen");
    return 1;
  }

  LLVMValueRef func = nullptr;
  for (LLVMValueRef candidate = LLVMGetFirstFunction(module); candidate;
       candidate = LLVMGetNextFunction(candidate)) {
    if (!LLVMIsDeclaration(candidate)) {
      func = candidate;
      break;
    }
  }

  if (!func) {
    std::fclose(out);
    std::fprintf(stderr, "assembly codegen error: module has no defined function\n");
    return 1;
  }

  int rc = emitAssemblyForFunction(func, out);
  std::fclose(out);
  return rc;
}

int emitAssemblyFromIRFile(const char *inputPath, const char *outputPath) {
  LLVMContextRef context = LLVMContextCreate();
  LLVMMemoryBufferRef buffer = nullptr;
  LLVMModuleRef module = nullptr;
  char *message = nullptr;

  if (LLVMCreateMemoryBufferWithContentsOfFile(inputPath, &buffer, &message) != 0) {
    std::fprintf(stderr, "%s\n", message ? message : "failed to read IR file");
    LLVMDisposeMessage(message);
    LLVMContextDispose(context);
    return 1;
  }

  if (LLVMParseIRInContext(context, buffer, &module, &message) != 0) {
    std::fprintf(stderr, "%s\n", message ? message : "failed to parse IR");
    LLVMDisposeMessage(message);
    LLVMDisposeMemoryBuffer(buffer);
    LLVMContextDispose(context);
    return 1;
  }

  if (LLVMVerifyModule(module, LLVMReturnStatusAction, &message) != 0) {
    std::fprintf(stderr, "%s\n", message ? message : "module verification failed");
    LLVMDisposeMessage(message);
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    return 1;
  }

  int rc = emitAssemblyFromModule(module, outputPath);
  LLVMDisposeModule(module);
  LLVMContextDispose(context);
  return rc;
}
