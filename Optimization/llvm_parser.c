// Author: Godwin Kangor
// Date : 02/12/26
// llvm_parser.c

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <llvm-c/Core.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Types.h>

#define prt(x) if(x) { printf("%s\n", x); }

static int g_changed = 0; // set to 1 by any pass that modifies the IR
static const char* g_mode = NULL; // NULL or "all" => run all; "cfold" => fold+DCE only; "cse" => common-subexpr+fold+DCE; "constprop" => const-prop+fold+DCE

// -----------------------------
// Helper data-structures for constant propagation (reaching stores)
// -----------------------------

typedef struct {
	LLVMValueRef* data;
	size_t n;
	size_t cap;
} ValSet;

static void set_init(ValSet* s) { s->data = NULL; s->n = 0; s->cap = 0; }
static void set_free(ValSet* s) { free(s->data); s->data = NULL; s->n = 0; s->cap = 0; }

static int set_contains(const ValSet* s, LLVMValueRef v) {
	for (size_t i = 0; i < s->n; i++) if (s->data[i] == v) return 1;
	return 0;
}

static void set_reserve(ValSet* s, size_t need) {
	if (s->cap >= need) return;
	size_t newcap = s->cap ? s->cap * 2 : 8;
	while (newcap < need) newcap *= 2;
	s->data = (LLVMValueRef*)realloc(s->data, newcap * sizeof(LLVMValueRef));
	s->cap = newcap;
}

static void set_add(ValSet* s, LLVMValueRef v) {
	if (set_contains(s, v)) return;
	set_reserve(s, s->n + 1);
	s->data[s->n++] = v;
}

static void set_remove_idx(ValSet* s, size_t idx) {
	if (idx >= s->n) return;
	s->data[idx] = s->data[s->n - 1];
	s->n--;
}

static void set_copy(ValSet* dst, const ValSet* src) {
	set_free(dst);
	set_init(dst);
	set_reserve(dst, src->n);
	for (size_t i = 0; i < src->n; i++) dst->data[dst->n++] = src->data[i];
}

static void set_union_into(ValSet* dst, const ValSet* src) {
	for (size_t i = 0; i < src->n; i++) set_add(dst, src->data[i]);
}

static int set_equal(const ValSet* a, const ValSet* b) {
	if (a->n != b->n) return 0;
	for (size_t i = 0; i < a->n; i++) if (!set_contains(b, a->data[i])) return 0;
	return 1;
}

static LLVMValueRef store_ptr(LLVMValueRef storeInst) { return LLVMGetOperand(storeInst, 1); }
static LLVMValueRef store_val(LLVMValueRef storeInst) { return LLVMGetOperand(storeInst, 0); }
static LLVMValueRef load_ptr(LLVMValueRef loadInst) { return LLVMGetOperand(loadInst, 0); }

static void set_remove_stores_to_ptr(ValSet* s, LLVMValueRef ptr) {
	for (size_t i = 0; i < s->n; ) {
		LLVMValueRef st = s->data[i];
		if (LLVMGetInstructionOpcode(st) == LLVMStore && store_ptr(st) == ptr) {
			set_remove_idx(s, i);
		} else {
			i++;
		}
	}
}

typedef struct {
	int* data;
	size_t n;
	size_t cap;
} IntVec;

static void ivec_init(IntVec* v) { v->data = NULL; v->n = 0; v->cap = 0; }
static void ivec_free(IntVec* v) { free(v->data); v->data = NULL; v->n = 0; v->cap = 0; }
static void ivec_push(IntVec* v, int x) {
	if (v->n + 1 > v->cap) {
		size_t nc = v->cap ? v->cap * 2 : 8;
		v->data = (int*)realloc(v->data, nc * sizeof(int));
		v->cap = nc;
	}
	v->data[v->n++] = x;
}

void constant_propagation(LLVMValueRef function);
void Common_subexpression_elimination(LLVMBasicBlockRef bb);
void Constfolding(LLVMBasicBlockRef bb);
void DeadCodeElimanation(LLVMBasicBlockRef bb);

/* This function reads the given llvm file and loads the LLVM IR into
	 data-structures that we can works on for optimization phase.
*/

LLVMModuleRef createLLVMModel(char * filename){
	char *err = 0;

	LLVMMemoryBufferRef ll_f = 0;
	LLVMModuleRef m = 0;

	LLVMCreateMemoryBufferWithContentsOfFile(filename, &ll_f, &err);

	if (err != NULL) {
		prt(err);
		return NULL;
	}

	LLVMParseIRInContext(LLVMGetGlobalContext(), ll_f, &m, &err);

	if (err != NULL) {
		prt(err);
	}

	return m;
}

void walkBBInstructions(LLVMBasicBlockRef bb){
	for (LLVMValueRef instruction = LLVMGetFirstInstruction(bb); instruction;
				instruction = LLVMGetNextInstruction(instruction)) {

		// LLVMGetInstructionOpcode gives you LLVMOpcode that is a enum
		LLVMOpcode op = LLVMGetInstructionOpcode(instruction);

		if (LLVMIsABranchInst(instruction)){
			printf("Inst: \n");
			LLVMDumpValue(instruction);
			printf("     %d\n", LLVMGetNumOperands(instruction));

			//  print if this is a conditional branch.
			if (LLVMIsConditional(instruction)){
				printf("\n>>> Conditional branch inst found:\n");
				LLVMDumpValue(instruction);
				printf("\n");

				LLVMValueRef b1 = LLVMGetOperand(instruction, 1);
				LLVMValueRef b2 = LLVMGetOperand(instruction, 2);
				printf("\n");
				LLVMDumpValue(b1);
				printf("\n");
				LLVMDumpValue(b2);
				printf("\n");
			}
		}

		if (op == LLVMAdd) {
			LLVMDumpValue(instruction);
			printf("\n");
			//Check if one of the operands in a constant
			if (LLVMIsConstant(LLVMGetOperand(instruction, 0)) ||
					LLVMIsConstant(LLVMGetOperand(instruction, 1))) {
				LLVMUseRef y = LLVMGetFirstUse(instruction);
				if (y == NULL) printf("No uses\n");
				else printf("\n Exploring users of add with constant operand:\n");
				for (LLVMUseRef u = LLVMGetFirstUse(instruction);
					u;
					u = LLVMGetNextUse(u)){
					LLVMValueRef x = LLVMGetUser(u);
					LLVMDumpValue(x);
					printf("\n");

				}
			}
		}
	}
}

// find and print successor basicblocks
// locate the terminator instruction & LLVMGetNumSuccessors/LLVMGetSuccessor.
void printSuccessors(LLVMBasicBlockRef bb){
	LLVMValueRef term = LLVMGetLastInstruction(bb);
	if (term == NULL) return;

	if (!LLVMIsATerminatorInst(term)) {
		for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst; inst = LLVMGetNextInstruction(inst)) {
			if (LLVMIsATerminatorInst(inst)) { term = inst; break; }
		}
	}

	if (term == NULL || !LLVMIsATerminatorInst(term)) return;

	unsigned n = LLVMGetNumSuccessors(term);
	printf("Successors (%u):\n", n);
	for (unsigned i = 0; i < n; ++i) {
		LLVMBasicBlockRef succ = LLVMGetSuccessor(term, i);
		LLVMDumpValue(LLVMBasicBlockAsValue(succ));
		printf("\n");
	}
}

void walkBasicblocks(LLVMValueRef function){
	for (LLVMBasicBlockRef basicBlock = LLVMGetFirstBasicBlock(function);
			basicBlock;
			basicBlock = LLVMGetNextBasicBlock(basicBlock)) {

		printf("In basic block\n");
		// --->Print the current basic block and its successors.
		LLVMDumpValue(LLVMBasicBlockAsValue(basicBlock));
		printf("\n");
		printSuccessors(basicBlock);
		printf("\n");
		walkBBInstructions(basicBlock);

	}
}

void walkFunctions(LLVMModuleRef module){
	for (LLVMValueRef function =  LLVMGetFirstFunction(module);
			function;
			function = LLVMGetNextFunction(function)) {

		const char* funcName = LLVMGetValueName(function);

		printf("Function Name: %s\n", funcName);

		// FIXPOINT: repeat (const-prop -> const-fold -> DCE) until no changes
		do {
			g_changed = 0;

			int do_constprop = (g_mode == NULL) || (strcmp(g_mode, "all") == 0) || (strcmp(g_mode, "constprop") == 0);
			if (do_constprop) {
				constant_propagation(function);
			}

			int do_cse = (g_mode == NULL) || (strcmp(g_mode, "all") == 0) || (strcmp(g_mode, "cse") == 0);

			// Local passes (bb-level)
			for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
					bb;
					bb = LLVMGetNextBasicBlock(bb)) {
				if (do_cse) Common_subexpression_elimination(bb);
				Constfolding(bb);
				DeadCodeElimanation(bb);
			}

		} while (g_changed);

		// Optional: keep your debug walk if you still want to print CFG/insts
		// walkBasicblocks(function);
	}
}


void walkGlobalValues(LLVMModuleRef module){
	for (LLVMValueRef gVal =  LLVMGetFirstGlobal(module);
					gVal;
					gVal = LLVMGetNextGlobal(gVal)) {

		const char* gName = LLVMGetValueName(gVal);
		printf("Global variable name: %s\n", gName);
	}
}

int main(int argc, char** argv)
{
	LLVMModuleRef m;

	if (argc == 2 || argc == 3){
		m = createLLVMModel(argv[1]);
		if (argc == 3) g_mode = argv[2];
	} else {
		fprintf(stderr, "Usage: %s <input.ll> [all|cfold|cse|constprop]\n", argv[0]);
		return 1;
	}

	if (m != NULL){
		//LLVMDumpModule(m);
		walkGlobalValues(m);
		walkFunctions(m);
		LLVMPrintModuleToFile (m, "test_new.ll", NULL);
	}
	else {
		fprintf(stderr, "m is NULL\n");
	}

	return 0;
}

void constant_propagation(LLVMValueRef function) {
	// Skip declarations
	if (LLVMCountBasicBlocks(function) == 0) return;

	// Collect basic blocks into an array for indexing
	int nbb = 0;
	for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function); bb; bb = LLVMGetNextBasicBlock(bb)) nbb++;
	LLVMBasicBlockRef* blocks = (LLVMBasicBlockRef*)malloc(sizeof(LLVMBasicBlockRef) * (size_t)nbb);
	int bi = 0;
	for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function); bb; bb = LLVMGetNextBasicBlock(bb)) blocks[bi++] = bb;

	// Collect all store instructions in the function
	ValSet allStores; set_init(&allStores);
	for (int i = 0; i < nbb; i++) {
		for (LLVMValueRef inst = LLVMGetFirstInstruction(blocks[i]); inst; inst = LLVMGetNextInstruction(inst)) {
			if (LLVMGetInstructionOpcode(inst) == LLVMStore) set_add(&allStores, inst);
		}
	}

	// GEN/KILL/IN/OUT per block
	ValSet* GEN = (ValSet*)malloc(sizeof(ValSet) * (size_t)nbb);
	ValSet* KILL = (ValSet*)malloc(sizeof(ValSet) * (size_t)nbb);
	ValSet* IN = (ValSet*)malloc(sizeof(ValSet) * (size_t)nbb);
	ValSet* OUT = (ValSet*)malloc(sizeof(ValSet) * (size_t)nbb);
	for (int i = 0; i < nbb; i++) { set_init(&GEN[i]); set_init(&KILL[i]); set_init(&IN[i]); set_init(&OUT[i]); }

	// preds list
	IntVec* preds = (IntVec*)malloc(sizeof(IntVec) * (size_t)nbb);
	for (int i = 0; i < nbb; i++) ivec_init(&preds[i]);

	// Build predecessor lists by scanning terminators and successors
	for (int i = 0; i < nbb; i++) {
		LLVMValueRef term = LLVMGetLastInstruction(blocks[i]);
		if (!term || !LLVMIsATerminatorInst(term)) {
			for (LLVMValueRef inst = LLVMGetFirstInstruction(blocks[i]); inst; inst = LLVMGetNextInstruction(inst)) {
				if (LLVMIsATerminatorInst(inst)) { term = inst; break; }
			}
		}
		if (!term || !LLVMIsATerminatorInst(term)) continue;
		unsigned ns = LLVMGetNumSuccessors(term);
		for (unsigned s = 0; s < ns; s++) {
			LLVMBasicBlockRef succ = LLVMGetSuccessor(term, s);
			int j = -1;
			for (int t = 0; t < nbb; t++) { if (blocks[t] == succ) { j = t; break; } }
			if (j >= 0) ivec_push(&preds[j], i);
		}
	}

	// Compute GEN for each block: last store per ptr in the block
	for (int i = 0; i < nbb; i++) {
		for (LLVMValueRef inst = LLVMGetFirstInstruction(blocks[i]); inst; inst = LLVMGetNextInstruction(inst)) {
			if (LLVMGetInstructionOpcode(inst) == LLVMStore) {
				LLVMValueRef p = store_ptr(inst);
				set_remove_stores_to_ptr(&GEN[i], p);
				set_add(&GEN[i], inst);
			}
		}
	}

	// Compute KILL for each block
	for (int i = 0; i < nbb; i++) {
		for (size_t g = 0; g < GEN[i].n; g++) {
			LLVMValueRef st_in_block = GEN[i].data[g];
			LLVMValueRef p = store_ptr(st_in_block);
			for (size_t a = 0; a < allStores.n; a++) {
				LLVMValueRef st = allStores.data[a];
				if (st != st_in_block && store_ptr(st) == p) set_add(&KILL[i], st);
			}
		}
	}

	// Initialize OUT = GEN (IN empty)
	for (int i = 0; i < nbb; i++) set_copy(&OUT[i], &GEN[i]);

	// Iterate to fixpoint
	int changed = 1;
	while (changed) {
		changed = 0;
		for (int i = 0; i < nbb; i++) {
			ValSet newIN; set_init(&newIN);
			for (size_t pi = 0; pi < preds[i].n; pi++) {
				int pidx = preds[i].data[pi];
				set_union_into(&newIN, &OUT[pidx]);
			}

			// newOUT = GEN ∪ (newIN - KILL)
			ValSet newOUT; set_init(&newOUT);
			set_union_into(&newOUT, &GEN[i]);
			for (size_t si = 0; si < newIN.n; si++) {
				LLVMValueRef st = newIN.data[si];
				if (!set_contains(&KILL[i], st)) set_add(&newOUT, st);
			}

			if (!set_equal(&IN[i], &newIN) || !set_equal(&OUT[i], &newOUT)) {
				set_copy(&IN[i], &newIN);
				set_copy(&OUT[i], &newOUT);
				changed = 1;
			}

			set_free(&newIN);
			set_free(&newOUT);
		}
	}

	// Apply propagation: walk each block with R starting at IN
	for (int i = 0; i < nbb; i++) {
		ValSet R; set_init(&R);
		set_copy(&R, &IN[i]);

		// collect loads to delete after scan
		ValSet loadsToDelete; set_init(&loadsToDelete);

		for (LLVMValueRef inst = LLVMGetFirstInstruction(blocks[i]); inst; inst = LLVMGetNextInstruction(inst)) {
			LLVMOpcode op = LLVMGetInstructionOpcode(inst);

			if (op == LLVMStore) {
				LLVMValueRef p = store_ptr(inst);
				set_remove_stores_to_ptr(&R, p);
				set_add(&R, inst);
				continue;
			}

			if (op == LLVMLoad) {
				LLVMValueRef p = load_ptr(inst);

				// Find reaching stores to p
				long long firstConst = 0;
				int haveAny = 0;
				int firstSeen = 0;
				int ok = 1;
				LLVMValueRef constValRef = NULL;

				for (size_t ri = 0; ri < R.n; ri++) {
					LLVMValueRef st = R.data[ri];
					if (LLVMGetInstructionOpcode(st) != LLVMStore) continue;
					if (store_ptr(st) != p) continue;

					haveAny = 1;
					LLVMValueRef v = store_val(st);
					if (!LLVMIsAConstantInt(v)) { ok = 0; break; }

					long long c = LLVMConstIntGetSExtValue(v);
					if (!firstSeen) {
						firstConst = c;
						constValRef = v;
						firstSeen = 1;
					} else if (c != firstConst) {
						ok = 0;
						break;
					}
				}

				if (haveAny && ok && constValRef != NULL) {
					// Replace load result with constant and delete load later
					LLVMReplaceAllUsesWith(inst, constValRef);
					g_changed = 1;
					set_add(&loadsToDelete, inst);
				}
			}
		}

		// Delete loads safely
		for (LLVMValueRef inst = LLVMGetFirstInstruction(blocks[i]); inst; ) {
			LLVMValueRef next = LLVMGetNextInstruction(inst);
			if (set_contains(&loadsToDelete, inst)) {
				LLVMInstructionEraseFromParent(inst);
			}
			inst = next;
		}

		set_free(&loadsToDelete);
		set_free(&R);
	}

	// Cleanup
	for (int i = 0; i < nbb; i++) {
		set_free(&GEN[i]); set_free(&KILL[i]); set_free(&IN[i]); set_free(&OUT[i]);
		ivec_free(&preds[i]);
	}
	free(GEN); free(KILL); free(IN); free(OUT);
	free(preds);
	set_free(&allStores);
	free(blocks);
}

// ------------------------------------------------------------
// Common subexpression elimination (CSE)
// - For non-load instructions (SSA, no pointers in miniC):
//   if same opcode + same operands appeared earlier in the same BB,
//   replace all uses of the later inst (B) with the earlier inst (A).
// - For load instructions: only safe if there is NO intervening store
//   to the same pointer between A and B.
// Note: We do NOT delete B here; DCE will clean it up.
// ------------------------------------------------------------
void Common_subexpression_elimination(LLVMBasicBlockRef bb) {
	for (LLVMValueRef B = LLVMGetFirstInstruction(bb); B; B = LLVMGetNextInstruction(B)) {
		LLVMOpcode opB = LLVMGetInstructionOpcode(B);

		// Only handle a small set required by the lab/tests.
		// (Ignore stores/calls/branches/alloca/ret etc.)
		if (opB != LLVMLoad && opB != LLVMAdd && opB != LLVMSub && opB != LLVMMul && opB != LLVMICmp) {
			continue;
		}

		// --------- Case 1: LOAD (needs safety check) ---------
		if (opB == LLVMLoad) {
			LLVMValueRef ptrB = load_ptr(B);

			// Walk backwards. Stop if a store to ptrB is seen.
			for (LLVMValueRef A = LLVMGetPreviousInstruction(B); A; A = LLVMGetPreviousInstruction(A)) {
				LLVMOpcode opA = LLVMGetInstructionOpcode(A);

				if (opA == LLVMStore && store_ptr(A) == ptrB) {
					// Memory changed; cannot reuse an earlier load.
					break;
				}

				if (opA == LLVMLoad && load_ptr(A) == ptrB) {
					// Safe: no intervening store to same address.
					LLVMReplaceAllUsesWith(B, A);
					g_changed = 1;
					break;
				}
			}

			continue;
		}

		// --------- Case 2: non-load (SSA => safe) ---------
		LLVMValueRef b0 = LLVMGetOperand(B, 0);
		LLVMValueRef b1 = LLVMGetOperand(B, 1);

		// Canonicalize commutative ops so (x,y) matches (y,x)
		if (opB == LLVMAdd || opB == LLVMMul) {
			if ((uintptr_t)b0 > (uintptr_t)b1) {
				LLVMValueRef tmp = b0; b0 = b1; b1 = tmp;
			}
		}

		unsigned predB = 0;
		if (opB == LLVMICmp) predB = (unsigned)LLVMGetICmpPredicate(B);

		for (LLVMValueRef A = LLVMGetPreviousInstruction(B); A; A = LLVMGetPreviousInstruction(A)) {
			LLVMOpcode opA = LLVMGetInstructionOpcode(A);
			if (opA != opB) continue;

			// For icmp, predicate must match too
			if (opB == LLVMICmp) {
				unsigned predA = (unsigned)LLVMGetICmpPredicate(A);
				if (predA != predB) continue;
			}

			LLVMValueRef a0 = LLVMGetOperand(A, 0);
			LLVMValueRef a1 = LLVMGetOperand(A, 1);

			if (opA == LLVMAdd || opA == LLVMMul) {
				if ((uintptr_t)a0 > (uintptr_t)a1) {
					LLVMValueRef tmp = a0; a0 = a1; a1 = tmp;
				}
			}

			if (a0 == b0 && a1 == b1) {
				LLVMReplaceAllUsesWith(B, A);
				g_changed = 1;
				break;
			}
		}
	}
}

void DeadCodeElimanation(LLVMBasicBlockRef bb){
	for (LLVMValueRef instruction = LLVMGetFirstInstruction(bb); instruction; ) {
		LLVMValueRef next = LLVMGetNextInstruction(instruction);
		LLVMOpcode op = LLVMGetInstructionOpcode(instruction);

		// Keep side-effecting/control-flow instructions
		if (op == LLVMStore || op == LLVMCall || op == LLVMRet || op == LLVMAlloca || op == LLVMBr) {
			instruction = next;
			continue;
		}

		// If no uses, delete it
		if (LLVMGetFirstUse(instruction) == NULL) {
			LLVMInstructionEraseFromParent(instruction);
			g_changed = 1;
		}

		instruction = next;
	}
}

void Constfolding(LLVMBasicBlockRef bb){
	for (LLVMValueRef instruction = LLVMGetFirstInstruction(bb); instruction;
			instruction = LLVMGetNextInstruction(instruction)) {

		LLVMOpcode op = LLVMGetInstructionOpcode(instruction);
		if (!(op == LLVMAdd || op == LLVMSub || op == LLVMMul)) continue;

		LLVMValueRef a = LLVMGetOperand(instruction, 0);
		LLVMValueRef b = LLVMGetOperand(instruction, 1);
		if (!(LLVMIsConstant(a) && LLVMIsConstant(b))) continue;

		LLVMValueRef new_inst = NULL;
		if (op == LLVMAdd) new_inst = LLVMConstAdd(a, b);
		else if (op == LLVMSub) new_inst = LLVMConstSub(a, b);
		else if (op == LLVMMul) new_inst = LLVMConstMul(a, b);

		if (new_inst != NULL) {
			LLVMReplaceAllUsesWith(instruction, new_inst);
			g_changed = 1;
		}
	}
}
