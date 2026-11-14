// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The partition of one unit's module before LLVM's optimizer, for the Release and Develop configurations
// (docs/commands/build.md, "Units in parts"; docs/stage1-build-design.md, #2559): each part's job then runs the -O3
// pipeline and the code generator on its part alone, the parts run at once, and a rebuild repeats only the parts whose
// bitcode an edit changed.
//
//   presplit INPUT.bc PARTS DIRECTORY [--import LIMIT] [--anchor M] [--cohere CAP] [--spread S] [--tests-optnone]
//            [--strip-debug] [--keep-statics] [--benchmark-bodies] [--simplified-size] [--verify] [--report]
//
// writes DIRECTORY/part0.bc to DIRECTORY/partPARTS.bc and DIRECTORY/locals.txt. Parts 0 to PARTS - 1 hold function
// definitions; part PARTS holds the global variables more than one part refers to, and no function. INPUT is clang's
// bitcode before any LLVM pass (-Xclang -disable-llvm-passes), with line tables or full debug information, which name
// each function's file. Derived from split.c, which partitions a unit after the optimizer (a sanitizer build's Release),
// and from the record's prototype (buster-performance's stage1-build-design-2040-2026-11-12).
//
// Which part a function goes to (the design's rules 1, 2 and 6):
// - each source file's functions, in their source order (their line, then their name), are cut into chunks: a function
//   whose name's hash is 0 modulo M (--anchor, 24) starts one, and the file's first chunk starts at the file. A chunk
//   goes to one of a run of parts of its file's own, chosen by the chunk's first name: as many parts as the file's size
//   holds of a part's size (the largest power of two at most the unit's size over PARTS). A function's part then
//   depends on its file and the names around it, not on a count of what came before: an edit moves no other function,
//   and an edit that moves a file's lines changes that file's parts alone;
// - a local function that one function alone calls goes to its caller's part, and so on up to a function that isn't
//   one, until that tree holds CAP instructions (--cohere, 150,000): the whole unit's inliner inlines such a function
//   whatever its size, which a part can only do for a body it holds;
// - a function whose inline assembly has text stays in part 0, with the module's own assembly, and no other part reads
//   its body: such text can define a label another function's text names (x86_semantics_test_return).
//
// What a part sees of the others (rules 3 and 4):
// - it reads, available_externally and without their debug information, the bodies of the functions its code calls
//   that another part defines, of at most LIMIT unoptimized instructions (--import, 100) or always_inline, and so on
//   through those bodies, so that its inliner still inlines them; a body that can't inline (optnone) reads none; and
//   every code part reads the bodies the input itself holds available_externally (a C library header's inline ones);
// - a local function another part refers to keeps its local name where it is defined, and a hidden alias,
//   NAME.buster_split, names it for the others, as #2058's split.c did;
// - a local global variable that exactly one code part refers to, and whose initializer names no other global, stays
//   local to that part; any other one a code part refers to becomes a hidden external one in the data part, and the
//   code parts that refer to a strong constant read its initializer available_externally, so that loads from it fold;
// - what __attribute__((used)) kept (llvm.used) is kept by a hidden name instead, which the merge makes local again.
//
// What keeps an unchanged function's part the same bytes (rules 5 and 7): a local variable whose name ends in a number
// clang gave it in the order it met them (.str.N, a string literal; constinit.N; a second static of one name) is named
// by its contents instead; an inline-assembly statement loses its !srcloc, its offset in the whole unit; struct types
// lose their names (struct.anon.N is numbered so too); a body a part only reads comes without its line numbers;
// Develop's debug information is DWARF 4 line tables, which name no file's checksum (Release's -g, DWARF 5, names each
// file's, so that an edit of a file changes every part that mentions it). The same input gives the same parts, whatever
// the CPUs and the order the children run in.
//
// --tests-optnone (the design's step 3, Develop's) marks the functions of the tests' files optnone and noinline, as
// #681's pragma does for the checker's own: a file whose name has _test_, _test., _tests., test_ or fuzz in it.
// --strip-debug writes the parts without any debug information: a Release build without it (BUSTER_DEBUG_INFO=0)
// compiles its units with line tables for the plan's files alone. --verify runs LLVM's verifier on each part; --report
// prints each part's definitions, cost and imports, and the plan's times, to stderr.
//
// Release's options make a part's inliner decide as the whole unit's (#2559's step 5, #2624): --keep-statics gives the
// parts that read a static function that is only ever called a local copy of it, keeps a body in its part when it calls
// a static function called once, and gives a part's static function a phantom caller wherever the whole module calls it
// more (presplit_keep_statics, presplit_phantoms); --benchmark-bodies has a benchmark's file read the bodies it calls,
// whatever their size; --simplified-size measures a body for LIMIT by what SROA leaves of it, with LLVM's vector bonus
// (presplit_scan); and a part that reads a constant table reads the constants it names (presplit_readable_readers).
//
// BusterCodegenMerge.cmake joins the parts' objects and restores local binding (locals.txt). The module is parsed
// once and the plan made on it; each part is then a clone of its own definitions (split_bitcode.cpp's
// split_clone_part), written by a child process, as many at once as the CPUs and the memory allow. It is build tooling
// outside the compiler's compile path, using LLVM's C API (and split_bitcode.cpp's C++ calls), with no vector path.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Error.h>
#include <llvm-c/Transforms/PassBuilder.h>

#define PRESPLIT_ALIAS_SUFFIX ".buster_split"
// A part is a bit of a 64-bit mask, and the data part one more.
#define PRESPLIT_MAXIMUM_PARTS 62
// Each instruction counts one, and each function this much more: a function's own cost to the optimizer.
#define PRESPLIT_FUNCTION_COST 8
#define PRESPLIT_DEFAULT_IMPORT 100
#define PRESPLIT_DEFAULT_ANCHOR 24
#define PRESPLIT_DEFAULT_COHERE 150000
#define PRESPLIT_PATH_BYTES 4096
// An optnone function's weight is its cost over this.
#define PRESPLIT_INERT_SHARE 4
#define PRESPLIT_DEFAULT_SPREAD 1
// --report names every function of at least this cost.
#define PRESPLIT_REPORT_COST 5000

// split_bitcode.cpp: `module`'s clone with the `count` definitions in `keep` and declarations of the rest, the bodies
// it keeps but doesn't own without their debug information, and its struct types without names; and a module's bitcode
// without its use-list order, which the optimizer that follows doesn't need.
LLVMModuleRef split_clone_part(LLVMModuleRef module, LLVMValueRef* keep, uint64_t count, LLVMValueRef* owned, uint64_t owned_count, bool lists);
bool split_write_part(LLVMModuleRef module, const char* path);

typedef struct PresplitFunction PresplitFunction;
struct PresplitFunction
{
    LLVMValueRef function;
    uint64_t cost;
    // Its size to the import limit with --simplified-size: what SROA leaves of its instructions, scaled down by LLVM's
    // inliner's vector bonus (presplit_scan).
    uint64_t import_size;
    // The parts that read the body, available_externally.
    uint64_t imported;
    uint32_t* callees;
    uint32_t callee_count;
    // The part that emits it.
    uint32_t part;
    // Its source file (an index in the plan's files) and the line of its definition.
    uint32_t file;
    uint32_t line;
    // Emitted by some part: a definition that isn't available_externally.
    bool defined;
    bool local;
    // A local definition another part refers to, or one llvm.used kept: an alias names it.
    bool aliased;
    bool importable;
    bool used;
    // Its body inlines nothing (optnone), so a part that reads it needs none of its callees.
    bool inert;
    // No caller inlines it (noinline, optnone or naked).
    bool opaque;
    // Holds inline assembly with text: kept in part 0 and never read by another part (presplit_scan).
    bool pinned;
    // A part that reads it holds a local copy of its own, and its own part no alias (--keep-statics).
    bool copied;
    // A benchmark's function calls it, so the benchmark's part reads its body whatever its size (--benchmark-bodies).
    bool measured;
    // No other part reads its body: it calls a local function only its own part calls and no other part may read.
    bool kept;
    // Its uses in the whole module, which a part's copy or definition of a local function keeps the inliner's view of
    // (presplit_phantoms).
    uint32_t uses;
    // A body the input itself holds available_externally (a C library header's extern inline function: pthread_equal,
    // bsearch, putchar): every code part reads it so, as the whole unit's inliner does, whether its code calls it or the
    // optimizer makes a call (printf("\n") becomes putchar('\n') in run_streaming_benchmark). There are a handful,
    // and a part's optimizer drops those it doesn't call.
    bool external;
};

typedef struct PresplitGlobal PresplitGlobal;
struct PresplitGlobal
{
    LLVMValueRef global;
    // The parts whose code or data refers to it.
    uint64_t readers;
    uint32_t part;
    bool defined;
    bool used;
};

typedef struct PresplitFile PresplitFile;
struct PresplitFile
{
    const char* name;
    uint64_t hash;
    uint64_t cost;
    uint32_t length;
    // The parts its chunks are dealt over.
    uint32_t run;
    bool test;
    // A benchmark's own file (_benchmark in its name): --benchmark-bodies.
    bool benchmark;
};

// Values to their indices, by address.
typedef struct PresplitMap PresplitMap;
struct PresplitMap
{
    LLVMValueRef* keys;
    uint32_t* values;
    uint64_t capacity;
};

typedef struct PresplitPlan PresplitPlan;
struct PresplitPlan
{
    LLVMModuleRef module;
    PresplitFunction* functions;
    PresplitGlobal* globals;
    PresplitFile* files;
    PresplitMap function_indices;
    PresplitMap global_indices;
    uint32_t function_count;
    uint32_t global_count;
    uint32_t file_count;
    uint32_t parts;
    uint64_t anchor;
    uint64_t import_limit;
    // 0 is off.
    uint64_t cohere;
    uint64_t spread;
    unsigned srcloc;
    bool tests_optnone;
    bool strip_debug;
    bool keep_statics;
    bool benchmark_bodies;
    bool simplified_size;
    bool verify;
    bool report;
};

static void* presplit_allocate(uint64_t count, uint64_t size)
{
    void* memory = calloc(count == 0 ? 1 : count, size);
    if (memory == 0)
    {
        fprintf(stderr, "presplit: out of memory\n");
        exit(1);
    }
    return memory;
}

static void* presplit_grow(void* memory, uint64_t count, uint64_t size)
{
    void* grown = realloc(memory, count * size);
    if (grown == 0)
    {
        fprintf(stderr, "presplit: out of memory\n");
        exit(1);
    }
    return grown;
}

static PresplitMap presplit_map_create(uint64_t minimum)
{
    uint64_t capacity = 1;
    while (capacity < minimum * 2)
    {
        capacity <<= 1;
    }
    return (PresplitMap){
        .keys = presplit_allocate(capacity, sizeof(LLVMValueRef)),
        .values = presplit_allocate(capacity, sizeof(uint32_t)),
        .capacity = capacity,
    };
}

static uint64_t presplit_map_slot(const PresplitMap* map, LLVMValueRef key)
{
    uint64_t slot = (((uint64_t)(uintptr_t)key * UINT64_C(0x9e3779b97f4a7c15)) >> 20) & (map->capacity - 1);
    while (map->keys[slot] != 0 && map->keys[slot] != key)
    {
        slot = (slot + 1) & (map->capacity - 1);
    }
    return slot;
}

static void presplit_map_put(PresplitMap* map, LLVMValueRef key, uint32_t value)
{
    const uint64_t slot = presplit_map_slot(map, key);
    map->keys[slot] = key;
    map->values[slot] = value;
}

// The index of `key`, or UINT32_MAX.
static uint32_t presplit_map_get(const PresplitMap* map, LLVMValueRef key)
{
    const uint64_t slot = presplit_map_slot(map, key);
    return map->keys[slot] == key ? map->values[slot] : UINT32_MAX;
}

static unsigned presplit_attribute_kind(const char* name)
{
    return LLVMGetEnumAttributeKindForName(name, strlen(name));
}

static bool presplit_has_attribute(LLVMValueRef function, const char* name)
{
    return LLVMGetEnumAttributeAtIndex(function, LLVMAttributeFunctionIndex, presplit_attribute_kind(name)) != 0;
}

static bool presplit_local(LLVMValueRef value)
{
    const LLVMLinkage linkage = LLVMGetLinkage(value);
    return linkage == LLVMInternalLinkage || linkage == LLVMPrivateLinkage;
}

static uint64_t presplit_hash(const char* bytes, size_t length)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < length; i += 1)
    {
        hash = (hash ^ (uint8_t)bytes[i]) * UINT64_C(0x100000001b3);
    }
    // A final mix: FNV's low bits alone follow the last characters.
    hash ^= hash >> 32;
    hash *= UINT64_C(0xd6e8feb86659fd93);
    hash ^= hash >> 32;
    return hash;
}

// Whether `needle` occurs in the `length` bytes at `text`.
static bool presplit_contains(const char* text, size_t length, const char* needle)
{
    const size_t size = strlen(needle);
    bool found = false;
    for (size_t i = 0; i + size <= length && !found; i += 1)
    {
        found = memcmp(text + i, needle, size) == 0;
    }
    return found;
}

// Whether the file `name` (a path) is one of the tests': its base name has _test_, _test., _tests., test_ or fuzz in
// it, the rule of the record's measurements (#2559) and of the build guide's names for test files.
static bool presplit_test_file(const char* name, size_t length)
{
    size_t start = length;
    while (start > 0 && name[start - 1] != '/')
    {
        start -= 1;
    }
    const char* base = name + start;
    const size_t size = length - start;
    return presplit_contains(base, size, "_test_") || presplit_contains(base, size, "_test.") || presplit_contains(base, size, "_tests.") ||
           presplit_contains(base, size, "test_") || presplit_contains(base, size, "fuzz");
}

static void presplit_push(LLVMValueRef** stack, uint32_t* depth, uint32_t* capacity, LLVMValueRef value)
{
    if (*depth == *capacity)
    {
        *capacity *= 2;
        *stack = presplit_grow(*stack, *capacity, sizeof(LLVMValueRef));
    }
    (*stack)[*depth] = value;
    *depth += 1;
}

// The function's instructions, and its direct callees in the plan, in `entry`. Drops each inline-assembly statement's
// !srcloc, and pins a function whose assembly has text.
static void presplit_scan(PresplitPlan* plan, PresplitFunction* entry)
{
    uint64_t count = 0;
    // The instructions SROA leaves (no alloca, lifetime marker or load or store through an alloca), and how many of
    // those make vectors.
    uint64_t simplified = 0;
    uint64_t vectors = 0;
    uint32_t capacity = 0;
    for (LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(entry->function); block != 0; block = LLVMGetNextBasicBlock(block))
    {
        for (LLVMValueRef instruction = LLVMGetFirstInstruction(block); instruction != 0; instruction = LLVMGetNextInstruction(instruction))
        {
            count += 1;
            LLVMValueRef called = LLVMIsACallInst(instruction) != 0 ? LLVMGetCalledValue(instruction) : 0;
            size_t called_length = 0;
            const char* called_name = called != 0 && LLVMIsAFunction(called) != 0 ? LLVMGetValueName2(called, &called_length) : "";
            const bool scaffolding = LLVMIsAAllocaInst(instruction) != 0 || strncmp(called_name, "llvm.lifetime.", 14) == 0 ||
                                     (LLVMIsALoadInst(instruction) != 0 && LLVMIsAAllocaInst(LLVMGetOperand(instruction, 0)) != 0) ||
                                     (LLVMIsAStoreInst(instruction) != 0 && LLVMIsAAllocaInst(LLVMGetOperand(instruction, 1)) != 0);
            simplified += scaffolding ? 0 : 1;
            vectors += !scaffolding && LLVMGetTypeKind(LLVMTypeOf(instruction)) == LLVMVectorTypeKind ? 1 : 0;
            if (LLVMIsACallInst(instruction) != 0)
            {
                LLVMValueRef callee = LLVMGetCalledValue(instruction);
                // An inline assembly statement's !srcloc is its offset in the whole translation unit, which every
                // earlier edit moves; the parts' jobs can't map it back to a source line anyway.
                if (callee != 0 && LLVMIsAInlineAsm(callee) != 0)
                {
                    LLVMSetMetadata(instruction, plan->srcloc, 0);
                    // Assembly text can define a label and name another function's (x86_semantics_test.c's return
                    // label), which only the assembler resolves, within one object: every function with such text
                    // stays in part 0, and no other part reads its body. The empty barriers ("" with operands) don't.
                    size_t text = 0;
                    LLVMGetInlineAsmAsmString(callee, &text);
                    entry->pinned = entry->pinned || text != 0;
                }
                const uint32_t index = callee != 0 && LLVMIsAFunction(callee) != 0 ? presplit_map_get(&plan->function_indices, callee) : UINT32_MAX;
                const bool repeated = entry->callee_count != 0 && entry->callees[entry->callee_count - 1] == index;
                if (index != UINT32_MAX && !repeated)
                {
                    if (entry->callee_count == capacity)
                    {
                        capacity = capacity == 0 ? 8 : capacity * 2;
                        entry->callees = presplit_grow(entry->callees, capacity, sizeof(uint32_t));
                    }
                    entry->callees[entry->callee_count] = index;
                    entry->callee_count += 1;
                }
            }
        }
    }
    entry->cost = count + PRESPLIT_FUNCTION_COST;
    // LLVM's inliner raises its threshold by 150% for a callee whose instructions are more than half vectors, by 75%
    // for more than a tenth: asm_word_registers_vector, 721 unoptimized instructions and 337 left by SROA, inlined at
    // cost 710 against a threshold of 1574 into the assembler's register cache.
    const uint64_t bonus = vectors * 2 > simplified ? 250 : vectors * 10 > simplified ? 175 : 100;
    entry->import_size = simplified * 100 / bonus;
}

// The parts that read `function`'s body: the one that emits it and those that import it.
static uint64_t presplit_where(const PresplitFunction* function)
{
    return function->defined ? (UINT64_C(1) << function->part) | function->imported : 0;
}

// The parts whose code or data refers to `value`, as a mask: an instruction's function's parts, and a global
// variable initializer's, the data part's (a global whose initializer names another stays in the data part). Constant
// expressions and aggregates pass references on; an alias or ifunc refers to it under a name every part can use.
static uint64_t presplit_references(const PresplitPlan* plan, LLVMValueRef value)
{
    uint64_t mask = 0;
    uint32_t capacity = 64;
    uint32_t depth = 0;
    LLVMValueRef* stack = presplit_allocate(capacity, sizeof(LLVMValueRef));
    presplit_push(&stack, &depth, &capacity, value);
    while (depth != 0)
    {
        depth -= 1;
        LLVMValueRef current = stack[depth];
        for (LLVMUseRef use = LLVMGetFirstUse(current); use != 0; use = LLVMGetNextUse(use))
        {
            LLVMValueRef user = LLVMGetUser(use);
            if (LLVMIsAInstruction(user))
            {
                const uint32_t index = presplit_map_get(&plan->function_indices, LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)));
                mask |= index != UINT32_MAX ? presplit_where(&plan->functions[index]) : 0;
            }
            else if (LLVMIsAGlobalVariable(user))
            {
                mask |= LLVMIsDeclaration(user) ? 0 : UINT64_C(1) << plan->parts;
            }
            else if (LLVMIsAGlobalValue(user))
            {
                mask = ~UINT64_C(0);
            }
            else
            {
                presplit_push(&stack, &depth, &capacity, user);
            }
        }
    }
    free(stack);
    return mask;
}

static void presplit_hide(LLVMValueRef value)
{
    LLVMSetLinkage(value, LLVMExternalLinkage);
    LLVMSetVisibility(value, LLVMHiddenVisibility);
}

// Whether a code part reads `global`'s initializer, available_externally: a strong constant's, one whose initializer
// the single module took as definitive.
static bool presplit_readable(LLVMValueRef global)
{
    return !LLVMIsDeclaration(global) && LLVMIsGlobalConstant(global) && LLVMGetLinkage(global) == LLVMExternalLinkage &&
           !LLVMIsThreadLocal(global) && !LLVMIsExternallyInitialized(global);
}

// Whether the constant `value` names no global value: a leaf's definition can move to the one code part that uses it.
static bool presplit_leaf(LLVMValueRef value)
{
    bool leaf = true;
    uint32_t capacity = 64;
    uint32_t depth = 0;
    LLVMValueRef* stack = presplit_allocate(capacity, sizeof(LLVMValueRef));
    presplit_push(&stack, &depth, &capacity, value);
    while (depth != 0 && leaf)
    {
        depth -= 1;
        LLVMValueRef current = stack[depth];
        if (LLVMIsAGlobalValue(current))
        {
            leaf = false;
        }
        else if (LLVMIsAConstant(current))
        {
            const int operands = LLVMGetNumOperands(current);
            for (int i = 0; i < operands; i += 1)
            {
                presplit_push(&stack, &depth, &capacity, LLVMGetOperand(current, (unsigned)i));
            }
        }
    }
    free(stack);
    return leaf;
}

// A code part that reads a constant's initializer reads the constants it names too, to a fixed point: a table of
// strings (asm_alias's mnemonics, `static const char* const aliases[][2]`) is no use to strlen's folding when its strings
// come as declarations alone, and asm_instruction made 46 calls to strlen where the whole unit's made 16. Their readers
// were the data part's alone, from the table's initializer.
static void presplit_readable_readers(PresplitPlan* plan)
{
    const uint64_t code = (UINT64_C(1) << plan->parts) - 1;
    uint32_t capacity = 64;
    LLVMValueRef* stack = presplit_allocate(capacity, sizeof(LLVMValueRef));
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < plan->global_count; i += 1)
        {
            const PresplitGlobal* entry = &plan->globals[i];
            const uint64_t readers = entry->readers & code;
            uint32_t depth = 0;
            if (entry->defined && readers != 0 && presplit_readable(entry->global))
            {
                presplit_push(&stack, &depth, &capacity, LLVMGetInitializer(entry->global));
            }
            while (depth != 0)
            {
                depth -= 1;
                LLVMValueRef value = stack[depth];
                if (LLVMIsAGlobalVariable(value))
                {
                    const uint32_t index = presplit_map_get(&plan->global_indices, value);
                    if (index != UINT32_MAX && (readers & ~plan->globals[index].readers) != 0)
                    {
                        plan->globals[index].readers |= readers;
                        changed = true;
                    }
                }
                else if (!LLVMIsAGlobalValue(value) && LLVMIsAConstant(value))
                {
                    const int operands = LLVMGetNumOperands(value);
                    for (int o = 0; o < operands; o += 1)
                    {
                        presplit_push(&stack, &depth, &capacity, LLVMGetOperand(value, (unsigned)o));
                    }
                }
            }
        }
    }
    free(stack);
}

// What a code part reads, it may refer to: every local global a readable constant's initializer names is made a hidden
// external one, in the data part, in turn readable where it is a constant, until none is left.
static void presplit_readable_closure(PresplitPlan* plan)
{
    uint32_t capacity = 1024;
    uint32_t depth = 0;
    LLVMValueRef* stack = presplit_allocate(capacity, sizeof(LLVMValueRef));
    for (uint32_t i = 0; i < plan->global_count; i += 1)
    {
        if (presplit_readable(plan->globals[i].global))
        {
            presplit_push(&stack, &depth, &capacity, LLVMGetInitializer(plan->globals[i].global));
        }
    }
    while (depth != 0)
    {
        depth -= 1;
        LLVMValueRef value = stack[depth];
        if (LLVMIsAGlobalVariable(value))
        {
            if (presplit_local(value) && !LLVMIsDeclaration(value))
            {
                presplit_hide(value);
                const uint32_t index = presplit_map_get(&plan->global_indices, value);
                if (index != UINT32_MAX)
                {
                    plan->globals[index].part = plan->parts;
                }
                if (presplit_readable(value))
                {
                    presplit_push(&stack, &depth, &capacity, LLVMGetInitializer(value));
                }
            }
        }
        else if (!LLVMIsAGlobalValue(value) && LLVMIsAConstant(value))
        {
            const int operands = LLVMGetNumOperands(value);
            for (int i = 0; i < operands; i += 1)
            {
                presplit_push(&stack, &depth, &capacity, LLVMGetOperand(value, (unsigned)i));
            }
        }
    }
    free(stack);
}

// Marks what llvm.used or llvm.compiler.used names.
static void presplit_mark_used(PresplitPlan* plan, const char* name)
{
    LLVMValueRef array = LLVMGetNamedGlobal(plan->module, name);
    LLVMValueRef initializer = array != 0 ? LLVMGetInitializer(array) : 0;
    const int operands = initializer != 0 ? LLVMGetNumOperands(initializer) : 0;
    for (int i = 0; i < operands; i += 1)
    {
        LLVMValueRef value = LLVMGetOperand(initializer, (unsigned)i);
        while (LLVMIsAConstantExpr(value))
        {
            value = LLVMGetOperand(value, 0);
        }
        const uint32_t function = presplit_map_get(&plan->function_indices, value);
        const uint32_t global = presplit_map_get(&plan->global_indices, value);
        if (function != UINT32_MAX)
        {
            plan->functions[function].used = true;
        }
        if (global != UINT32_MAX)
        {
            plan->globals[global].used = true;
        }
    }
}

// A hash of the names of the functions whose instructions refer to `value`, whatever their order.
static uint64_t presplit_user_hash(LLVMValueRef value)
{
    uint64_t hash = UINT64_C(0x2545f4914f6cdd1d);
    uint32_t capacity = 64;
    uint32_t depth = 0;
    LLVMValueRef* stack = presplit_allocate(capacity, sizeof(LLVMValueRef));
    presplit_push(&stack, &depth, &capacity, value);
    while (depth != 0)
    {
        depth -= 1;
        LLVMValueRef current = stack[depth];
        for (LLVMUseRef use = LLVMGetFirstUse(current); use != 0; use = LLVMGetNextUse(use))
        {
            LLVMValueRef user = LLVMGetUser(use);
            if (LLVMIsAInstruction(user))
            {
                size_t length = 0;
                const char* name = LLVMGetValueName2(LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)), &length);
                hash ^= presplit_hash(name, length) * UINT64_C(0x9e3779b97f4a7c15);
            }
            else if (!LLVMIsAGlobalValue(user))
            {
                presplit_push(&stack, &depth, &capacity, user);
            }
        }
    }
    free(stack);
    return hash;
}

// Writes `stem`.bHASH, and .cCOPY after it when `copy` isn't 0, into `name`: its length, or 0 when it doesn't fit.
static int presplit_content_name(char* name, size_t size, const char* stem, size_t stem_length, uint64_t hash, unsigned copy)
{
    const int length = (int)(stem_length < 200 ? stem_length : 200);
    int written = 0;
    if (copy == 0)
    {
        written = snprintf(name, size, "%.*s.b%016llx", length, stem, (unsigned long long)hash);
    }
    else
    {
        written = snprintf(name, size, "%.*s.b%016llx.c%u", length, stem, (unsigned long long)hash, copy);
    }
    return written > 0 && (size_t)written < size ? written : 0;
}

static uint64_t presplit_mix(uint64_t hash, uint64_t value)
{
    hash ^= value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    hash *= UINT64_C(0xff51afd7ed558ccd);
    hash ^= hash >> 33;
    return hash;
}

// A hash of `type`'s structure, without its struct types' names, which clang numbers in the order it met them
// (struct.anon.N): an anonymous struct an edit adds would rename the rest.
static uint64_t presplit_type_hash(LLVMTypeRef type)
{
    const LLVMTypeKind kind = LLVMGetTypeKind(type);
    uint64_t hash = presplit_mix(UINT64_C(0x7970657479706574), (uint64_t)kind);
    if (kind == LLVMIntegerTypeKind)
    {
        hash = presplit_mix(hash, LLVMGetIntTypeWidth(type));
    }
    else if (kind == LLVMArrayTypeKind)
    {
        hash = presplit_mix(presplit_mix(hash, LLVMGetArrayLength2(type)), presplit_type_hash(LLVMGetElementType(type)));
    }
    else if (kind == LLVMVectorTypeKind || kind == LLVMScalableVectorTypeKind)
    {
        hash = presplit_mix(presplit_mix(hash, LLVMGetVectorSize(type)), presplit_type_hash(LLVMGetElementType(type)));
    }
    else if (kind == LLVMStructTypeKind)
    {
        const unsigned count = LLVMCountStructElementTypes(type);
        hash = presplit_mix(presplit_mix(hash, (uint64_t)LLVMIsPackedStruct(type)), count);
        for (unsigned i = 0; i < count; i += 1)
        {
            hash = presplit_mix(hash, presplit_type_hash(LLVMStructGetTypeAtIndex(type, i)));
        }
    }
    else if (kind == LLVMPointerTypeKind)
    {
        hash = presplit_mix(hash, LLVMGetPointerAddressSpace(type));
    }
    else if (kind == LLVMFunctionTypeKind)
    {
        const unsigned count = LLVMCountParamTypes(type);
        LLVMTypeRef* parameters = presplit_allocate(count, sizeof(LLVMTypeRef));
        LLVMGetParamTypes(type, parameters);
        hash = presplit_mix(presplit_mix(hash, presplit_type_hash(LLVMGetReturnType(type))), (uint64_t)LLVMIsFunctionVarArg(type));
        for (unsigned i = 0; i < count; i += 1)
        {
            hash = presplit_mix(hash, presplit_type_hash(parameters[i]));
        }
        free(parameters);
    }
    return hash;
}

// A hash of the constant `value`: its type's structure and its contents, the names of the globals it refers to, and no
// struct type's name. A scalar that isn't a plain integer is hashed as LLVM prints it, which names no struct type.
static uint64_t presplit_constant_hash(LLVMValueRef value)
{
    uint64_t hash = presplit_type_hash(LLVMTypeOf(value));
    if (LLVMIsAGlobalValue(value) != 0)
    {
        size_t length = 0;
        const char* name = LLVMGetValueName2(value, &length);
        hash = presplit_mix(presplit_mix(hash, 1), presplit_hash(name, length));
    }
    else if (LLVMIsAConstantInt(value) != 0 && LLVMGetIntTypeWidth(LLVMTypeOf(value)) <= 64)
    {
        hash = presplit_mix(presplit_mix(hash, 2), LLVMConstIntGetZExtValue(value));
    }
    else if (LLVMIsAConstantDataSequential(value) != 0)
    {
        size_t size = 0;
        const char* bytes = LLVMGetRawDataValues(value, &size);
        hash = presplit_mix(presplit_mix(hash, 3), presplit_hash(bytes, size));
    }
    else if (LLVMIsAConstantAggregateZero(value) != 0 || LLVMIsAConstantPointerNull(value) != 0)
    {
        hash = presplit_mix(hash, 4);
    }
    else if (LLVMIsAPoisonValue(value) != 0)
    {
        hash = presplit_mix(hash, 5);
    }
    else if (LLVMIsAUndefValue(value) != 0)
    {
        hash = presplit_mix(hash, 6);
    }
    else if (LLVMIsAConstantStruct(value) != 0 || LLVMIsAConstantArray(value) != 0 || LLVMIsAConstantVector(value) != 0 ||
             LLVMIsAConstantExpr(value) != 0)
    {
        hash = presplit_mix(hash, LLVMIsAConstantExpr(value) != 0 ? 8 + (uint64_t)LLVMGetConstOpcode(value) : 7);
        if (LLVMIsAConstantExpr(value) != 0 && LLVMGetConstOpcode(value) == LLVMGetElementPtr)
        {
            hash = presplit_mix(hash, presplit_type_hash(LLVMGetGEPSourceElementType(value)));
        }
        const int operands = LLVMGetNumOperands(value);
        for (int i = 0; i < operands; i += 1)
        {
            hash = presplit_mix(hash, presplit_constant_hash(LLVMGetOperand(value, (unsigned)i)));
        }
    }
    else
    {
        char* text = LLVMPrintValueToString(value);
        hash = presplit_mix(presplit_mix(hash, 9), presplit_hash(text, strlen(text)));
        LLVMDisposeMessage(text);
    }
    return hash;
}

// Names the local variables whose names end in a number that LLVM gave them in the order clang met them (.str.N, a
// string literal; constinit.N; FUNCTION.NAME.N, a second static of one name), and clang's private ones, by what they
// are instead: the name's stem, .b and a hash of the alignment, the constness and linkage, the type's structure and the
// initializer (presplit_constant_hash, which names no struct type); a second one of the same contents adds the names
// of the functions that use it, and further ones .c1, .c2 and so on in the module's order. The leaves first (the
// strings), so that an initializer that names one hashes its new name. An edit elsewhere then renames none.
static void presplit_content_names(PresplitPlan* plan)
{
    for (int pass = 0; pass < 2; pass += 1)
    {
        for (uint32_t i = 0; i < plan->global_count; i += 1)
        {
            LLVMValueRef global = plan->globals[i].global;
            size_t length = 0;
            const char* name = LLVMGetValueName2(global, &length);
            size_t dot = length;
            while (dot > 0 && name[dot - 1] >= '0' && name[dot - 1] <= '9')
            {
                dot -= 1;
            }
            // Every private variable is clang's own (the first .str has no number yet); a static only when numbered.
            const bool numbered = dot < length && dot > 1 && name[dot - 1] == '.';
            const bool generated = LLVMGetLinkage(global) == LLVMPrivateLinkage || numbered;
            const size_t stem = numbered ? dot - 1 : length;
            if (plan->globals[i].defined && presplit_local(global) && generated && presplit_leaf(LLVMGetInitializer(global)) == (pass == 0))
            {
                const unsigned shape[3] = {
                    LLVMGetAlignment(global),
                    (unsigned)LLVMIsGlobalConstant(global),
                    (unsigned)LLVMGetLinkage(global),
                };
                uint64_t hash = presplit_hash((const char*)shape, sizeof(shape));
                hash = presplit_mix(hash, presplit_type_hash(LLVMGlobalGetValueType(global)));
                hash = presplit_mix(hash, presplit_constant_hash(LLVMGetInitializer(global)));
                char fresh[PRESPLIT_PATH_BYTES];
                int written = presplit_content_name(fresh, sizeof(fresh), name, stem, hash, 0);
                if (written != 0 && LLVMGetNamedGlobal(plan->module, fresh) != 0)
                {
                    // The same contents twice: told apart by the functions that use them, then by their order.
                    hash ^= presplit_user_hash(global);
                    written = presplit_content_name(fresh, sizeof(fresh), name, stem, hash, 0);
                }
                for (unsigned copy = 1; written != 0 && LLVMGetNamedGlobal(plan->module, fresh) != 0 && copy < 100000; copy += 1)
                {
                    written = presplit_content_name(fresh, sizeof(fresh), name, stem, hash, copy);
                }
                if (written != 0 && LLVMGetNamedGlobal(plan->module, fresh) == 0)
                {
                    LLVMSetValueName2(global, fresh, (size_t)written);
                }
            }
        }
    }
}

// The one function whose calls are all that refers to the local function `function`, as an index, or UINT32_MAX: for
// a function several functions call, one whose address something takes, and one nothing refers to.
static uint32_t presplit_single_caller(const PresplitPlan* plan, LLVMValueRef function)
{
    uint32_t caller = UINT32_MAX;
    bool single = true;
    for (LLVMUseRef use = LLVMGetFirstUse(function); use != 0 && single; use = LLVMGetNextUse(use))
    {
        LLVMValueRef user = LLVMGetUser(use);
        single = LLVMIsACallInst(user) != 0 && LLVMGetCalledValue(user) == function;
        if (single)
        {
            const uint32_t index = presplit_map_get(&plan->function_indices, LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)));
            single = index != UINT32_MAX && (caller == UINT32_MAX || caller == index);
            caller = index;
        }
    }
    return single ? caller : UINT32_MAX;
}

// Each defined function's file and line, from its line tables' subprogram, and each file's name; a function without one
// (none of clang's own for C) is the unnamed file's.
static void presplit_files(PresplitPlan* plan)
{
    const uint32_t capacity = 8192;
    plan->files = presplit_allocate(capacity, sizeof(PresplitFile));
    uint32_t* slots = presplit_allocate(capacity * 2, sizeof(uint32_t));
    for (uint32_t i = 0; i < capacity * 2; i += 1)
    {
        slots[i] = UINT32_MAX;
    }
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[i];
        if (entry->defined)
        {
            LLVMMetadataRef subprogram = LLVMGetSubprogram(entry->function);
            LLVMMetadataRef file = subprogram != 0 ? LLVMDIScopeGetFile(subprogram) : 0;
            unsigned length = 0;
            const char* name = file != 0 ? LLVMDIFileGetFilename(file, &length) : "";
            name = name != 0 ? name : "";
            entry->line = subprogram != 0 ? LLVMDISubprogramGetLine(subprogram) : 0;
            const uint64_t hash = presplit_hash(name, length);
            uint32_t slot = (uint32_t)(hash & (capacity * 2 - 1));
            while (slots[slot] != UINT32_MAX &&
                   (plan->files[slots[slot]].length != length || memcmp(plan->files[slots[slot]].name, name, length) != 0))
            {
                slot = (slot + 1) & (capacity * 2 - 1);
            }
            if (slots[slot] == UINT32_MAX)
            {
                if (plan->file_count == capacity)
                {
                    fprintf(stderr, "presplit: more than %u source files\n", capacity);
                    exit(1);
                }
                slots[slot] = plan->file_count;
                plan->files[plan->file_count] = (PresplitFile){
                    .name = name,
                    .hash = hash,
                    .length = length,
                    .test = presplit_test_file(name, length),
                    .benchmark = presplit_contains(name, length, "_benchmark"),
                };
                plan->file_count += 1;
            }
            entry->file = slots[slot];
        }
    }
    free(slots);
}

// --tests-optnone: the tests' files' functions take optnone and noinline, and lose optsize and minsize, which the
// verifier refuses beside optnone (clang gives cold functions optsize), as #681's pragma has them. An always_inline one
// is left as it is: noinline would contradict it, and its callers inline it anyway.
static void presplit_mark_tests(PresplitPlan* plan)
{
    LLVMContextRef context = LLVMGetModuleContext(plan->module);
    const unsigned optnone = presplit_attribute_kind("optnone");
    const unsigned noinline = presplit_attribute_kind("noinline");
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[i];
        if (entry->defined && plan->files[entry->file].test && !presplit_has_attribute(entry->function, "alwaysinline"))
        {
            LLVMRemoveEnumAttributeAtIndex(entry->function, LLVMAttributeFunctionIndex, presplit_attribute_kind("optsize"));
            LLVMRemoveEnumAttributeAtIndex(entry->function, LLVMAttributeFunctionIndex, presplit_attribute_kind("minsize"));
            LLVMRemoveEnumAttributeAtIndex(entry->function, LLVMAttributeFunctionIndex, presplit_attribute_kind("inlinehint"));
            LLVMAddAttributeAtIndex(entry->function, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(context, optnone, 0));
            LLVMAddAttributeAtIndex(entry->function, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(context, noinline, 0));
        }
    }
}

// The order presplit_assign walks the functions in: by file, then by line, then by name, then by module order.
static int presplit_compare_order(const void* left, const void* right, void* context)
{
    const PresplitPlan* plan = context;
    const uint32_t a = *(const uint32_t*)left;
    const uint32_t b = *(const uint32_t*)right;
    const PresplitFunction* first = &plan->functions[a];
    const PresplitFunction* second = &plan->functions[b];
    int order = 0;
    if (first->file != second->file)
    {
        order = first->file < second->file ? -1 : 1;
    }
    else if (first->line != second->line)
    {
        order = first->line < second->line ? -1 : 1;
    }
    else
    {
        size_t first_length = 0;
        size_t second_length = 0;
        const char* first_name = LLVMGetValueName2(first->function, &first_length);
        const char* second_name = LLVMGetValueName2(second->function, &second_length);
        const size_t shorter = first_length < second_length ? first_length : second_length;
        order = memcmp(first_name, second_name, shorter);
        if (order == 0 && first_length != second_length)
        {
            order = first_length < second_length ? -1 : 1;
        }
        if (order == 0)
        {
            order = a < b ? -1 : a > b ? 1 : 0;
        }
    }
    return order;
}

// Rule 1: each file's functions, in source order, cut into chunks at the anchors and dealt over a run of the file's own
// parts by each chunk's first name (the file's first chunk by the file's). A file's run is as many parts as its weight
// holds of a part's size, the largest power of two at most the unit's weight over SPREAD times the parts: an edit then
// changes the run of its own file alone, and only when that file's weight crosses a step, where a size from the unit's
// total would move every file near a rounding boundary whenever any file grew. A function's weight is its cost, a
// quarter of it for an optnone one, whose job generates its code without the optimizer.
static void presplit_assign(PresplitPlan* plan)
{
    uint64_t total = 0;
    uint32_t defined = 0;
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        if (entry->defined)
        {
            const uint64_t weight = entry->inert ? entry->cost / PRESPLIT_INERT_SHARE : entry->cost;
            plan->files[entry->file].cost += weight;
            total += weight;
            defined += 1;
        }
    }
    uint64_t size = 1;
    while (size * 2 <= total / ((uint64_t)plan->parts * plan->spread))
    {
        size *= 2;
    }
    for (uint32_t i = 0; i < plan->file_count; i += 1)
    {
        PresplitFile* file = &plan->files[i];
        const uint64_t run = (file->cost + size / 2) / size;
        file->run = run == 0 ? 1 : run > plan->parts ? plan->parts : (uint32_t)run;
    }
    uint32_t* order = presplit_allocate(defined, sizeof(uint32_t));
    uint32_t count = 0;
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        if (plan->functions[i].defined)
        {
            order[count] = i;
            count += 1;
        }
    }
    qsort_r(order, count, sizeof(uint32_t), presplit_compare_order, plan);
    uint32_t file = UINT32_MAX;
    uint64_t before = 0;
    uint64_t slice = 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[order[i]];
        size_t length = 0;
        const char* name = LLVMGetValueName2(entry->function, &length);
        if (entry->file != file)
        {
            file = entry->file;
            before = 0;
            slice = 0;
        }
        if (presplit_hash(name, length) % plan->anchor == 0)
        {
            slice = before / size;
        }
        const PresplitFile* source = &plan->files[file];
        entry->part = (uint32_t)(((source->hash >> 20) % plan->parts + slice % source->run) % plan->parts);
        before += entry->inert ? entry->cost / PRESPLIT_INERT_SHARE : entry->cost;
    }
    free(order);
}

// Rule 2: a local function that one function alone calls goes to that caller's part, and so on up to a function that
// isn't one (the tree's root), where the caller can inline it: not into an optnone caller, nor a noinline callee. A tree stops taking functions at the cap: the next one keeps its own part and roots its
// own tree, so that no part grows without bound.
static void presplit_cohere(PresplitPlan* plan)
{
    uint32_t* callers = presplit_allocate(plan->function_count, sizeof(uint32_t));
    uint64_t* tree = presplit_allocate(plan->function_count, sizeof(uint64_t));
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        uint32_t caller = entry->defined && entry->local && !entry->used && !entry->opaque ? presplit_single_caller(plan, entry->function) : UINT32_MAX;
        caller = caller != UINT32_MAX && plan->functions[caller].defined && !plan->functions[caller].inert && caller != i ? caller : UINT32_MAX;
        callers[i] = caller;
    }
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        if (plan->functions[i].defined)
        {
            uint32_t root = i;
            for (uint32_t steps = 0; callers[root] != UINT32_MAX && steps < plan->function_count; steps += 1)
            {
                root = callers[root];
            }
            if (root != i && tree[root] + plan->functions[root].cost + plan->functions[i].cost > plan->cohere)
            {
                callers[i] = UINT32_MAX;
                root = i;
            }
            if (root != i)
            {
                tree[root] += plan->functions[i].cost;
                plan->functions[i].part = plan->functions[root].part;
            }
        }
    }
    free(callers);
    free(tree);
}

// Whether every use of the local function `entry` is a call from a defined function: of its own part, with `own`.
static bool presplit_only_called(const PresplitPlan* plan, const PresplitFunction* entry, bool own)
{
    bool inside = LLVMGetFirstUse(entry->function) != 0;
    for (LLVMUseRef use = LLVMGetFirstUse(entry->function); use != 0 && inside; use = LLVMGetNextUse(use))
    {
        LLVMValueRef user = LLVMGetUser(use);
        inside = LLVMIsACallInst(user) != 0 && LLVMGetCalledValue(user) == entry->function;
        if (inside)
        {
            const uint32_t caller = presplit_map_get(&plan->function_indices, LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)));
            inside = caller != UINT32_MAX && plan->functions[caller].defined && (!own || plan->functions[caller].part == entry->part);
        }
    }
    return inside;
}

// --keep-statics (Release's): a body another part reads makes every local function it calls the target of an alias in
// that function's own part, a use the owner's inliner counts, so that it no longer inlines the function as the last
// call to a static one, which the whole unit's does whatever its size (asm_assemble's imported body kept asm_layout,
// asm_parse and asm_emit outlined, and instruction_is_valid's mode_register_is_valid: benchmark-assembler ran 9.6% more
// cycles). So a local function that is only ever called, and that other parts may read, goes to each part that calls
// it or reads a caller's body as a local copy of its own, which that part's inliner treats as the whole unit's does
// (with a phantom caller where the whole module calls it more: presplit_phantoms), and keeps no alias in its own part;
// and no other part reads a body that calls a local function they may not read whose one call is in its part, to a
// fixed point (the body then becomes such a function itself when its callers do). A function the whole module calls
// more than once has no last-call bonus to keep: type_derive's many callers in c_types.c had kept type_pointer, 8
// instructions, from every part but its own. always_inline bodies stay readable.
static void presplit_keep_statics(PresplitPlan* plan)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < plan->function_count; i += 1)
        {
            PresplitFunction* entry = &plan->functions[i];
            bool keeps = false;
            for (uint32_t c = 0; entry->defined && !entry->kept && c < entry->callee_count && !keeps; c += 1)
            {
                const PresplitFunction* callee = &plan->functions[entry->callees[c]];
                keeps = callee->defined && callee->local && !callee->used && !callee->opaque && !callee->importable &&
                        callee->uses == 1 && presplit_only_called(plan, callee, true);
            }
            if (keeps && !presplit_has_attribute(entry->function, "alwaysinline"))
            {
                // A body too large to import stays kept as well, from a benchmark's reading (--benchmark-bodies):
                // foreign_gc_link's, read by its benchmark, outlined its five single-call phases.
                entry->kept = true;
                changed = changed || entry->importable;
                entry->importable = false;
            }
        }
    }
}

// The local functions that go to the parts that read them as copies (--keep-statics, above), once what each part reads
// is known: only ever called, and read by another part.
static void presplit_copies(PresplitPlan* plan)
{
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[i];
        entry->copied = entry->defined && entry->local && !entry->used && !entry->opaque && (entry->importable || entry->measured) &&
                        presplit_only_called(plan, entry, false);
    }
}

// --benchmark-bodies (Release's): a benchmark measures the functions it calls as the whole unit compiles them, inlined
// into its loop where the whole unit's inliner inlines them, so its own file's functions read the bodies of everything
// they call whatever their size (benchmark-environments ran 15% more instructions with c_environment_lookup_batch called,
// benchmark-lowering 3.4% with c_lowering_vector and c_lowering_scalar). A body --keep-statics keeps in its part stays
// there: asm_assemble's, read by the assembler's benchmark, would outline asm_layout and asm_emit again.
static void presplit_measured(PresplitPlan* plan)
{
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        for (uint32_t c = 0; entry->defined && plan->files[entry->file].benchmark && c < entry->callee_count; c += 1)
        {
            PresplitFunction* callee = &plan->functions[entry->callees[c]];
            callee->measured = callee->measured || (callee->defined && !callee->opaque && !callee->pinned && !callee->kept && callee->part != entry->part);
        }
    }
}

// Rule 3: what each part reads, to a fixed point: the importable callees of the bodies it reads that can inline, and a
// benchmark's file's functions every callee it measures (--benchmark-bodies).
static void presplit_imports(PresplitPlan* plan)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t i = 0; i < plan->function_count; i += 1)
        {
            const PresplitFunction* entry = &plan->functions[i];
            const uint64_t where = entry->inert ? 0 : presplit_where(entry);
            for (uint32_t c = 0; c < entry->callee_count && where != 0; c += 1)
            {
                PresplitFunction* callee = &plan->functions[entry->callees[c]];
                if (callee->importable || (callee->measured && plan->files[entry->file].benchmark))
                {
                    const uint64_t added = where & ~(UINT64_C(1) << callee->part) & ~callee->imported;
                    callee->imported |= added;
                    changed = changed || added != 0;
                }
            }
        }
    }
}

// The plan every part shares, made before the parts fork.
static void presplit_plan(PresplitPlan* plan)
{
    for (LLVMValueRef function = LLVMGetFirstFunction(plan->module); function != 0; function = LLVMGetNextFunction(function))
    {
        plan->function_count += 1;
    }
    for (LLVMValueRef global = LLVMGetFirstGlobal(plan->module); global != 0; global = LLVMGetNextGlobal(global))
    {
        plan->global_count += 1;
    }
    plan->functions = presplit_allocate((uint64_t)plan->function_count + 1, sizeof(PresplitFunction));
    plan->globals = presplit_allocate((uint64_t)plan->global_count + 1, sizeof(PresplitGlobal));
    plan->function_indices = presplit_map_create((uint64_t)plan->function_count + 1024);
    plan->global_indices = presplit_map_create((uint64_t)plan->global_count + 1024);
    uint64_t unnamed = 0;
    uint32_t at = 0;
    for (LLVMValueRef function = LLVMGetFirstFunction(plan->module); function != 0; function = LLVMGetNextFunction(function))
    {
        PresplitFunction* entry = &plan->functions[at];
        entry->function = function;
        entry->defined = !LLVMIsDeclaration(function) && LLVMGetLinkage(function) != LLVMAvailableExternallyLinkage;
        entry->local = presplit_local(function);
        entry->part = UINT32_MAX;
        size_t length = 0;
        LLVMGetValueName2(function, &length);
        if (length == 0)
        {
            char name[64];
            snprintf(name, sizeof(name), "__buster_split_function_%llu", (unsigned long long)unnamed);
            unnamed += 1;
            LLVMSetValueName2(function, name, strlen(name));
        }
        presplit_map_put(&plan->function_indices, function, at);
        at += 1;
    }
    // llvm.used and llvm.compiler.used go: what they kept is kept by a hidden name instead.
    at = 0;
    LLVMValueRef global = LLVMGetFirstGlobal(plan->module);
    while (global != 0)
    {
        LLVMValueRef following = LLVMGetNextGlobal(global);
        size_t length = 0;
        const char* name = LLVMGetValueName2(global, &length);
        if (strcmp(name, "llvm.compiler.used") != 0 && strcmp(name, "llvm.used") != 0)
        {
            PresplitGlobal* entry = &plan->globals[at];
            entry->global = global;
            entry->defined = !LLVMIsDeclaration(global);
            entry->part = plan->parts;
            if (length == 0)
            {
                char text[64];
                snprintf(text, sizeof(text), "__buster_split_global_%llu", (unsigned long long)unnamed);
                unnamed += 1;
                LLVMSetValueName2(global, text, strlen(text));
            }
            presplit_map_put(&plan->global_indices, global, at);
            at += 1;
        }
        global = following;
    }
    plan->global_count = at;
    presplit_mark_used(plan, "llvm.used");
    presplit_mark_used(plan, "llvm.compiler.used");
    for (uint32_t kind = 0; kind < 2; kind += 1)
    {
        LLVMValueRef array = LLVMGetNamedGlobal(plan->module, kind == 0 ? "llvm.used" : "llvm.compiler.used");
        if (array != 0)
        {
            LLVMDeleteGlobal(array);
        }
    }

    presplit_content_names(plan);

    // The functions' costs, callees and files, then the tests' marks, which decide what is importable.
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        if (plan->functions[i].defined)
        {
            presplit_scan(plan, &plan->functions[i]);
            for (LLVMUseRef use = LLVMGetFirstUse(plan->functions[i].function); use != 0; use = LLVMGetNextUse(use))
            {
                plan->functions[i].uses += 1;
            }
        }
    }
    presplit_files(plan);
    if (plan->tests_optnone)
    {
        presplit_mark_tests(plan);
    }
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[i];
        if (entry->defined)
        {
            const bool always = presplit_has_attribute(entry->function, "alwaysinline");
            entry->inert = presplit_has_attribute(entry->function, "optnone");
            entry->opaque = presplit_has_attribute(entry->function, "noinline") || entry->inert || presplit_has_attribute(entry->function, "naked");
            const bool small = plan->simplified_size ? entry->import_size <= plan->import_limit : entry->cost <= plan->import_limit + PRESPLIT_FUNCTION_COST;
            entry->importable = !entry->opaque && !entry->pinned && (always || small);
        }
    }

    presplit_assign(plan);
    if (plan->cohere != 0)
    {
        presplit_cohere(plan);
    }
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        if (plan->functions[i].defined && plan->functions[i].pinned)
        {
            plan->functions[i].part = 0;
        }
    }
    if (plan->keep_statics)
    {
        presplit_keep_statics(plan);
    }
    if (plan->benchmark_bodies)
    {
        presplit_measured(plan);
    }
    if (plan->keep_statics)
    {
        presplit_copies(plan);
    }
    presplit_imports(plan);

    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[i];
        if (entry->defined && entry->local)
        {
            const uint64_t references = presplit_references(plan, entry->function);
            // A copy serves only the parts that read the function; another part's reference (an optnone caller's,
            // whose callees no part reads) needs the alias.
            entry->copied = entry->copied && (references & ~presplit_where(entry)) == 0;
            entry->aliased = !entry->copied && (entry->used || (references & ~(UINT64_C(1) << entry->part)) != 0);
        }
    }
    const uint64_t data = UINT64_C(1) << plan->parts;
    for (uint32_t i = 0; i < plan->global_count; i += 1)
    {
        PresplitGlobal* entry = &plan->globals[i];
        entry->readers = entry->defined ? presplit_references(plan, entry->global) : 0;
        if (entry->defined && presplit_local(entry->global))
        {
            const uint64_t mask = entry->readers;
            const uint64_t code = mask & ~data;
            const bool single = mask == code && code != 0 && (code & (code - 1)) == 0;
            if (!entry->used && single && presplit_leaf(LLVMGetInitializer(entry->global)))
            {
                entry->part = (uint32_t)__builtin_ctzll(code);
            }
            else if (entry->used || code != 0)
            {
                presplit_hide(entry->global);
            }
        }
    }
    presplit_readable_closure(plan);
    presplit_readable_readers(plan);
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        PresplitFunction* entry = &plan->functions[i];
        entry->external = !entry->defined && LLVMGetLinkage(entry->function) == LLVMAvailableExternallyLinkage && !LLVMIsDeclaration(entry->function);
    }
}

// --keep-statics: a local function's definition in a part, its own or a copy, has fewer uses there than in the whole
// module when other parts call it too (through their copies). The owner's inliner would then inline it as the last call
// to a static function where the whole unit's sees other calls and doesn't: arena_reserve_small, one call in
// scratch_begin's copy and in arena_reserve's, went into both, and scratch_begin (cost 60 in the whole unit, 1350 so) and
// arena_reserve (5, 970) then stayed outlined in the assembler's layout, emission and benchmark. So each such function
// gets a phantom caller standing for the others: an available_externally function, optnone, that passes its own
// arguments on to it, ignores its result and that llvm.compiler.used keeps; nothing calls it, and the optimizer drops
// its body after the inliner (EliminateAvailableExternallyPass), so the function goes where the whole unit's would.
// Its arguments are unknown to interprocedural constant propagation, as the other parts' callers' are: poison ones let
// a part specialize tier1_select_induction_bounds's copy to its own callers' constants, which folded a call away, gave
// tier1_select_range_phi_endpoint the last-call bonus, and kept the bigger body out of tier1_burs_check_prepare. A
// result the phantom returned would keep dead argument elimination from dropping one no caller reads, as the whole
// unit's does. Returns the phantoms, for llvm.compiler.used.
static uint32_t presplit_phantoms(const PresplitPlan* plan, LLVMModuleRef module, uint32_t part, LLVMValueRef* phantoms)
{
    const uint64_t bit = UINT64_C(1) << part;
    LLVMContextRef context = LLVMGetModuleContext(module);
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);
    const unsigned optnone = presplit_attribute_kind("optnone");
    const unsigned noinline = presplit_attribute_kind("noinline");
    uint32_t count = 0;
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        const bool mine = entry->defined && entry->part == part && !entry->aliased;
        const bool copy = entry->defined && entry->part != part && entry->copied && (entry->imported & bit) != 0;
        size_t length = 0;
        const char* name = (mine || copy) && entry->local ? LLVMGetValueName2(entry->function, &length) : 0;
        LLVMValueRef function = name != 0 ? LLVMGetNamedFunction(module, name) : 0;
        uint32_t uses = 0;
        for (LLVMUseRef use = function != 0 ? LLVMGetFirstUse(function) : 0; use != 0; use = LLVMGetNextUse(use))
        {
            uses += 1;
        }
        if (uses != 0 && uses < entry->uses)
        {
            LLVMTypeRef type = LLVMGlobalGetValueType(function);
            const unsigned parameters = LLVMCountParamTypes(type);
            LLVMTypeRef* types = presplit_allocate((uint64_t)parameters + 1, sizeof(LLVMTypeRef));
            LLVMGetParamTypes(type, types);
            char* phantom_name = presplit_allocate(length + sizeof(".buster_phantom"), 1);
            memcpy(phantom_name, name, length);
            memcpy(phantom_name + length, ".buster_phantom", sizeof(".buster_phantom"));
            LLVMValueRef phantom = LLVMAddFunction(module, phantom_name, LLVMFunctionType(LLVMVoidTypeInContext(context), types, parameters, 0));
            LLVMValueRef* arguments = presplit_allocate((uint64_t)parameters + 1, sizeof(LLVMValueRef));
            for (unsigned p = 0; p < parameters; p += 1)
            {
                arguments[p] = LLVMGetParam(phantom, p);
            }
            LLVMSetLinkage(phantom, LLVMAvailableExternallyLinkage);
            LLVMAddAttributeAtIndex(phantom, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(context, optnone, 0));
            LLVMAddAttributeAtIndex(phantom, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(context, noinline, 0));
            LLVMPositionBuilderAtEnd(builder, LLVMAppendBasicBlockInContext(context, phantom, ""));
            LLVMValueRef call = LLVMBuildCall2(builder, type, function, arguments, parameters, "");
            LLVMSetInstructionCallConv(call, LLVMGetFunctionCallConv(function));
            LLVMAddCallSiteAttribute(call, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(context, noinline, 0));
            LLVMBuildRetVoid(builder);
            phantoms[count] = phantom;
            count += 1;
            free(phantom_name);
            free(arguments);
            free(types);
        }
    }
    LLVMDisposeBuilder(builder);
    return count;
}

// Part `part` of the plan: a clone of the module with this part's definitions alone, written to `path`. Runs in its own
// child process, whose changes to the module no other part sees.
static bool presplit_part(const PresplitPlan* plan, uint32_t part, const char* path)
{
    const bool code = part < plan->parts;
    const uint64_t bit = UINT64_C(1) << part;
    LLVMValueRef* keep = presplit_allocate((uint64_t)plan->function_count + plan->global_count + 1, sizeof(LLVMValueRef));
    uint64_t kept = 0;
    LLVMValueRef* owned = presplit_allocate((uint64_t)plan->function_count + plan->global_count + 1, sizeof(LLVMValueRef));
    uint64_t owned_count = 0;
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        if ((entry->defined && (entry->part == part || (entry->imported & bit) != 0)) || (code && entry->external))
        {
            keep[kept] = entry->function;
            kept += 1;
        }
        if (entry->defined && entry->part == part)
        {
            owned[owned_count] = entry->function;
            owned_count += 1;
        }
    }
    for (uint32_t i = 0; i < plan->global_count; i += 1)
    {
        const PresplitGlobal* entry = &plan->globals[i];
        if (entry->defined && (entry->part == part || (code && (entry->readers & bit) != 0 && presplit_readable(entry->global))))
        {
            keep[kept] = entry->global;
            kept += 1;
        }
        if (entry->defined && entry->part == part)
        {
            owned[owned_count] = entry->global;
            owned_count += 1;
        }
    }
    LLVMModuleRef module = split_clone_part(plan->module, keep, kept, owned, owned_count, !code);
    free(keep);
    free(owned);
    bool consistent = true;
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        if (entry->defined)
        {
            size_t length = 0;
            const char* name = LLVMGetValueName2(entry->function, &length);
            LLVMValueRef function = LLVMGetNamedFunction(module, name);
            const bool mine = entry->part == part;
            const bool imported = !mine && (entry->imported & bit) != 0;
            if (function != 0 && entry->aliased)
            {
                char* alias = presplit_allocate(length + sizeof(PRESPLIT_ALIAS_SUFFIX), 1);
                memcpy(alias, name, length);
                memcpy(alias + length, PRESPLIT_ALIAS_SUFFIX, sizeof(PRESPLIT_ALIAS_SUFFIX));
                if (mine)
                {
                    presplit_hide(LLVMAddAlias2(module, LLVMGlobalGetValueType(function), 0, function, alias));
                }
                else
                {
                    LLVMSetValueName2(function, alias, strlen(alias));
                    presplit_hide(function);
                }
                free(alias);
            }
            // A copied function stays a local definition of this part's own.
            if (function != 0 && imported && !entry->copied)
            {
                LLVMSetLinkage(function, LLVMAvailableExternallyLinkage);
            }
            // A local function no other part refers to is no other part's business.
            if (function != 0 && !mine && entry->local && !entry->aliased && !(imported && entry->copied) && LLVMGetFirstUse(function) != 0)
            {
                fprintf(stderr, "presplit: part %u refers to %s, which stays local to part %u\n", part, name, entry->part);
                consistent = false;
            }
        }
    }
    for (uint32_t i = 0; i < plan->global_count; i += 1)
    {
        const PresplitGlobal* entry = &plan->globals[i];
        if (entry->defined)
        {
            size_t length = 0;
            const char* name = LLVMGetValueName2(entry->global, &length);
            LLVMValueRef global = LLVMGetNamedGlobal(module, name);
            if (global != 0 && entry->part != part)
            {
                if (code && (entry->readers & bit) != 0 && presplit_readable(entry->global))
                {
                    LLVMSetLinkage(global, LLVMAvailableExternallyLinkage);
                }
                else if (presplit_local(entry->global) && LLVMGetFirstUse(global) != 0)
                {
                    fprintf(stderr, "presplit: part %u refers to %s, which stays local to part %u\n", part, name, entry->part);
                    consistent = false;
                }
                // The other parts' globals' debug records go, so that each part's DWARF describes what it emits.
                LLVMGlobalClearMetadata(global);
            }
        }
    }
    if (code && plan->keep_statics)
    {
        LLVMValueRef* phantoms = presplit_allocate((uint64_t)plan->function_count + 1, sizeof(LLVMValueRef));
        const uint32_t count = presplit_phantoms(plan, module, part, phantoms);
        if (count != 0)
        {
            LLVMContextRef context = LLVMGetModuleContext(module);
            LLVMTypeRef pointer = LLVMPointerTypeInContext(context, 0);
            LLVMValueRef array = LLVMAddGlobal(module, LLVMArrayType2(pointer, count), "llvm.compiler.used");
            LLVMSetLinkage(array, LLVMAppendingLinkage);
            LLVMSetSection(array, "llvm.metadata");
            LLVMSetInitializer(array, LLVMConstArray2(pointer, phantoms, count));
        }
        free(phantoms);
    }
    // A global variable declaration nothing here uses goes: the code generator would still name a hidden one.
    LLVMValueRef global = LLVMGetFirstGlobal(module);
    while (global != 0)
    {
        LLVMValueRef following = LLVMGetNextGlobal(global);
        if (LLVMIsDeclaration(global) && LLVMGetFirstUse(global) == 0)
        {
            LLVMDeleteGlobal(global);
        }
        global = following;
    }
    if (part != 0)
    {
        LLVMSetModuleInlineAsm2(module, "", 0);
    }
    if (plan->strip_debug)
    {
        LLVMStripModuleDebugInfo(module);
    }
    LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
    LLVMErrorRef error = LLVMRunPasses(module, "strip-dead-debug-info,strip-dead-prototypes", 0, options);
    LLVMDisposePassBuilderOptions(options);
    bool written = false;
    char* message = 0;
    if (error != 0)
    {
        message = LLVMGetErrorMessage(error);
        fprintf(stderr, "presplit: part %u: %s\n", part, message);
        LLVMDisposeErrorMessage(message);
    }
    else if (!consistent)
    {
        fprintf(stderr, "presplit: part %u is inconsistent\n", part);
    }
    else if (plan->verify && LLVMVerifyModule(module, LLVMPrintMessageAction, &message))
    {
        fprintf(stderr, "presplit: part %u fails verification\n", part);
    }
    else
    {
        written = split_write_part(module, path);
        if (!written)
        {
            fprintf(stderr, "presplit: can't write %s\n", path);
        }
    }
    return written;
}

// Writes DIRECTORY/locals.txt: the names of the module's local definitions and of the aliases the parts give them, one a
// line, which the merge checks no part left undefined. Whether it was written.
static bool presplit_locals(const PresplitPlan* plan, const char* directory)
{
    char path[PRESPLIT_PATH_BYTES];
    const int length = snprintf(path, sizeof(path), "%s/locals.txt", directory);
    FILE* file = length > 0 && (size_t)length < sizeof(path) ? fopen(path, "w") : 0;
    bool written = file != 0;
    for (int kind = 0; written && kind < 2; kind += 1)
    {
        for (LLVMValueRef value = kind == 0 ? LLVMGetFirstFunction(plan->module) : LLVMGetFirstGlobal(plan->module); value != 0;
             value = kind == 0 ? LLVMGetNextFunction(value) : LLVMGetNextGlobal(value))
        {
            const LLVMLinkage linkage = LLVMGetLinkage(value);
            const bool hidden = LLVMGetVisibility(value) == LLVMHiddenVisibility && linkage == LLVMExternalLinkage;
            if (!LLVMIsDeclaration(value) && (presplit_local(value) || hidden))
            {
                size_t size = 0;
                const char* name = LLVMGetValueName2(value, &size);
                fprintf(file, "%.*s\n", (int)size, name);
                if (kind == 0 && presplit_local(value))
                {
                    fprintf(file, "%.*s" PRESPLIT_ALIAS_SUFFIX "\n", (int)size, name);
                }
            }
        }
    }
    written = file != 0 && fclose(file) == 0 && written;
    return written;
}

// How many parts to write at once: at most `wanted`, one per CPU this process may run on, and as many as the available
// memory holds at half this process's resident size each (a child copies the pages it changes, most of its part's and
// little of the rest).
static uint32_t presplit_concurrency(uint32_t wanted)
{
    cpu_set_t set;
    const uint64_t cpus = sched_getaffinity(0, sizeof(set), &set) == 0 ? (uint64_t)CPU_COUNT(&set) : 1;
    uint64_t available = 0;
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo != 0)
    {
        char line[256];
        while (fgets(line, sizeof(line), meminfo) != 0)
        {
            unsigned long long kilobytes = 0;
            if (sscanf(line, "MemAvailable: %llu kB", &kilobytes) == 1)
            {
                available = (uint64_t)kilobytes * 1024;
            }
        }
        fclose(meminfo);
    }
    uint64_t resident = 0;
    FILE* statm = fopen("/proc/self/statm", "r");
    if (statm != 0)
    {
        unsigned long long size = 0;
        unsigned long long pages = 0;
        if (fscanf(statm, "%llu %llu", &size, &pages) == 2)
        {
            resident = (uint64_t)pages * (uint64_t)sysconf(_SC_PAGESIZE);
        }
        fclose(statm);
    }
    const uint64_t by_memory = resident != 0 && available != 0 ? available / (resident / 2 + 1) : wanted;
    uint64_t limit = wanted < cpus ? wanted : cpus;
    limit = by_memory < limit ? by_memory : limit;
    return limit == 0 ? 1 : (uint32_t)limit;
}

// Writes every part, a child process each, as many at once as presplit_concurrency allows, and waits for them all:
// whether every part was written.
static bool presplit_parts(const PresplitPlan* plan, const char* directory)
{
    const uint32_t total = plan->parts + 1;
    const uint32_t limit = presplit_concurrency(total);
    bool done = true;
    uint32_t running = 0;
    uint32_t part = 0;
    while (part < total || running != 0)
    {
        if (part < total && running < limit && done)
        {
            fflush(0);
            const pid_t child = fork();
            if (child == 0)
            {
                char path[PRESPLIT_PATH_BYTES];
                const int length = snprintf(path, sizeof(path), "%s/part%u.bc", directory, part);
                _exit(length > 0 && (size_t)length < sizeof(path) && presplit_part(plan, part, path) ? 0 : 1);
            }
            done = done && child > 0;
            running += child > 0;
            part += 1;
        }
        else if (running != 0)
        {
            int status = 0;
            const pid_t child = waitpid(-1, &status, 0);
            const bool interrupted = child < 0 && errno == EINTR;
            done = done && (interrupted || (child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0));
            running = child > 0 ? running - 1 : interrupted ? running : 0;
        }
        else
        {
            part = total;
        }
    }
    return done;
}

// Prints each part's definitions, estimated cost and imports, and the plan's totals.
static void presplit_report(const PresplitPlan* plan)
{
    uint64_t total = 0;
    uint64_t imported_total = 0;
    uint32_t aliased = 0;
    uint32_t defined_total = 0;
    uint32_t inert = 0;
    for (uint32_t p = 0; p < plan->parts; p += 1)
    {
        uint64_t cost = 0;
        uint64_t imported_cost = 0;
        uint32_t defined = 0;
        uint32_t imported = 0;
        for (uint32_t i = 0; i < plan->function_count; i += 1)
        {
            const PresplitFunction* entry = &plan->functions[i];
            if (entry->defined && entry->part == p)
            {
                cost += entry->cost;
                defined += 1;
                aliased += entry->aliased;
                inert += entry->inert;
            }
            else if (entry->defined && (entry->imported & (UINT64_C(1) << p)) != 0)
            {
                imported_cost += entry->cost;
                imported += 1;
            }
        }
        total += cost;
        imported_total += imported_cost;
        defined_total += defined;
        fprintf(stderr, "presplit: part %u: %u definitions, cost %llu; %u imported, cost %llu\n", p, defined, (unsigned long long)cost, imported,
                (unsigned long long)imported_cost);
        // The part's three largest files.
        uint64_t* by_file = presplit_allocate(plan->file_count, sizeof(uint64_t));
        for (uint32_t i = 0; i < plan->function_count; i += 1)
        {
            const PresplitFunction* entry = &plan->functions[i];
            by_file[entry->file] += entry->defined && entry->part == p ? entry->cost : 0;
        }
        for (uint32_t rank = 0; rank < 3; rank += 1)
        {
            uint32_t best = 0;
            for (uint32_t i = 1; i < plan->file_count; i += 1)
            {
                best = by_file[i] > by_file[best] ? i : best;
            }
            if (by_file[best] != 0)
            {
                fprintf(stderr, "presplit:     %.*s: cost %llu\n", (int)plan->files[best].length, plan->files[best].name, (unsigned long long)by_file[best]);
                by_file[best] = 0;
            }
        }
        free(by_file);
    }
    for (uint32_t i = 0; i < plan->function_count; i += 1)
    {
        const PresplitFunction* entry = &plan->functions[i];
        if (entry->defined && entry->cost >= PRESPLIT_REPORT_COST)
        {
            size_t length = 0;
            const char* name = LLVMGetValueName2(entry->function, &length);
            fprintf(stderr, "presplit: function %.*s: cost %llu, part %u%s\n", (int)length, name, (unsigned long long)entry->cost, entry->part,
                    entry->inert ? ", optnone" : "");
        }
    }
    for (uint32_t i = 0; i < plan->file_count; i += 1)
    {
        const PresplitFile* file = &plan->files[i];
        fprintf(stderr, "presplit: file %.*s: cost %llu, %u parts from part %u%s\n", (int)file->length, file->name, (unsigned long long)file->cost,
                file->run, (uint32_t)((file->hash >> 20) % plan->parts), file->test ? ", a test's" : "");
    }
    uint32_t moved = 0;
    uint32_t hidden = 0;
    for (uint32_t i = 0; i < plan->global_count; i += 1)
    {
        const PresplitGlobal* entry = &plan->globals[i];
        moved += entry->defined && entry->part != plan->parts;
        hidden += entry->defined && LLVMGetVisibility(entry->global) == LLVMHiddenVisibility;
    }
    fprintf(stderr, "presplit: %u definitions in %u files, cost %llu, %u aliased, %u optnone; imports cost %llu (%.1f%%); %u globals, %u local to a code part, %u hidden\n",
            defined_total, plan->file_count, (unsigned long long)total, aliased, inert, (unsigned long long)imported_total,
            total != 0 ? 100.0 * (double)imported_total / (double)total : 0.0, plan->global_count, moved, hidden);
}

static double presplit_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

// Reads the options after the three operands into `plan`: whether they were valid.
static bool presplit_options(PresplitPlan* plan, int argc, char** argv)
{
    bool valid = true;
    for (int i = 4; i < argc && valid; i += 1)
    {
        if (strcmp(argv[i], "--verify") == 0)
        {
            plan->verify = true;
        }
        else if (strcmp(argv[i], "--report") == 0)
        {
            plan->report = true;
        }
        else if (strcmp(argv[i], "--tests-optnone") == 0)
        {
            plan->tests_optnone = true;
        }
        else if (strcmp(argv[i], "--strip-debug") == 0)
        {
            plan->strip_debug = true;
        }
        else if (strcmp(argv[i], "--keep-statics") == 0)
        {
            plan->keep_statics = true;
        }
        else if (strcmp(argv[i], "--benchmark-bodies") == 0)
        {
            plan->benchmark_bodies = true;
        }
        else if (strcmp(argv[i], "--simplified-size") == 0)
        {
            plan->simplified_size = true;
        }
        else if ((strcmp(argv[i], "--anchor") == 0 || strcmp(argv[i], "--import") == 0 || strcmp(argv[i], "--cohere") == 0 || strcmp(argv[i], "--spread") == 0) && i + 1 < argc)
        {
            char* end = 0;
            const unsigned long long value = strtoull(argv[i + 1], &end, 10);
            valid = end != argv[i + 1] && *end == 0 && argv[i + 1][0] != '-';
            if (strcmp(argv[i], "--anchor") == 0)
            {
                plan->anchor = value;
                valid = valid && value != 0;
            }
            else if (strcmp(argv[i], "--cohere") == 0)
            {
                plan->cohere = value;
            }
            else if (strcmp(argv[i], "--spread") == 0)
            {
                plan->spread = value;
                valid = valid && value != 0 && value <= PRESPLIT_MAXIMUM_PARTS;
            }
            else
            {
                plan->import_limit = value;
            }
            i += 1;
        }
        else
        {
            valid = false;
        }
    }
    return valid;
}

int main(int argc, char** argv)
{
    const double started = presplit_seconds();
    int status = 2;
    PresplitPlan plan = {
        .anchor = PRESPLIT_DEFAULT_ANCHOR,
        .import_limit = PRESPLIT_DEFAULT_IMPORT,
        .cohere = PRESPLIT_DEFAULT_COHERE,
        .spread = PRESPLIT_DEFAULT_SPREAD,
    };
    const bool valid = argc >= 4 && presplit_options(&plan, argc, argv);
    char* end = 0;
    const unsigned long parts = argc >= 4 ? strtoul(argv[2], &end, 10) : 0;
    if (!valid || end == argv[2] || *end != 0 || parts == 0 || parts > PRESPLIT_MAXIMUM_PARTS)
    {
        fprintf(stderr, "usage: presplit INPUT.bc PARTS DIRECTORY [--import LIMIT] [--anchor M] [--cohere CAP] [--spread S] [--tests-optnone] [--strip-debug] [--keep-statics] [--benchmark-bodies] [--simplified-size] [--verify] [--report]  (PARTS from 1 to %u)\n",
                PRESPLIT_MAXIMUM_PARTS);
    }
    else
    {
        status = 1;
        LLVMContextRef context = LLVMContextCreate();
        LLVMMemoryBufferRef buffer = 0;
        char* message = 0;
        LLVMModuleRef module = 0;
        if (LLVMCreateMemoryBufferWithContentsOfFile(argv[1], &buffer, &message))
        {
            fprintf(stderr, "presplit: %s: %s\n", argv[1], message);
            LLVMDisposeMessage(message);
        }
        else if (LLVMParseBitcodeInContext2(context, buffer, &module))
        {
            fprintf(stderr, "presplit: can't read %s\n", argv[1]);
        }
        else
        {
            plan.module = module;
            plan.parts = (uint32_t)parts;
            plan.srcloc = LLVMGetMDKindIDInContext(context, "srcloc", 6);
            const double parsed = presplit_seconds();
            presplit_plan(&plan);
            const double planned = presplit_seconds();
            if (plan.report)
            {
                presplit_report(&plan);
            }
            status = presplit_locals(&plan, argv[3]) && presplit_parts(&plan, argv[3]) ? 0 : 1;
            if (plan.report)
            {
                fprintf(stderr, "presplit: reading %.2f s, planning %.2f s, writing the parts %.2f s\n", parsed - started, planned - parsed,
                        presplit_seconds() - planned);
            }
        }
    }
    return status;
}
