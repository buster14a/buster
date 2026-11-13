// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The order-preserving partition of one unit's optimized module for parallel native code generation
// (docs/commands/build.md, "Parallel native generation"):
//
//   split INPUT.bc PARTS DIRECTORY [--verify] [--report]
//
// writes DIRECTORY/part0.bc to DIRECTORY/partPARTS.bc. Parts 0 to PARTS - 1 hold contiguous ranges of the module's
// function definitions in module order, the costliest (by instruction count) as cheap as the alignment rule below
// allows; part PARTS holds every global variable definition and no function. The code generator emits a module's functions in order, each with its constant pools and
// jump tables, and its global variables after them, so the parts' sections, concatenated in index order by
// BusterCodegenMerge.cmake's relocatable link, hold the single module's code and data in the single module's order.
//
// What each part's code generation sees is what the single module's saw:
// - a function another part refers to stays local where it is defined, so that the assembler resolves, and relaxes,
//   the jumps to it there as before (a near tail call's two-byte jmp); a hidden alias, NAME.buster_split, names it for
//   the other parts, which refer to that name. Other local symbols another part refers to become hidden external ones;
// - a code part keeps the initializer of each strong constant global, available_externally, so that the code generator
//   folds what it reads from one (a string's bytes into immediate stores) as before, and emits none of them;
// - the module's inline assembly (the checker section's placement, main.c) stays in part 0, before every function;
// - a part starts, in each of its text sections, at a function of the greatest alignment it holds there, so that the
//   relocatable link's section alignment adds no padding the single module didn't have;
// - the bitcode keeps the use-list order (split_bitcode.cpp), which instruction selection follows;
// - the entries of llvm.used and of the constructors and destructors go to the part that emits what they name, which
//   then emits them as the single module did (a sanitizer's module constructor, in a retained section, and
//   AddressSanitizer's, in a comdat with its .init_array entry); an alias goes with its base object (AddressSanitizer's
//   private aliases of the globals it describes), and no declaration keeps a comdat.
// The merge restores local binding to everything the input didn't export, and the comdat flags llvm-objcopy clears.
// Read-only data keeps its order and each item's alignment, but a part's .rodata takes its greatest item's alignment,
// so the items after a part boundary can move by less than that alignment. The build's comparison (build-regression) checks the rest: identical functions at
// identical addresses.
//
// --verify runs LLVM's verifier on each part; --report prints each part's functions and estimated cost to stderr.
// The input is parsed once; the parts are written by one child process each, forked after the plan, as many at once as
// the CPUs and memory allow (split_concurrency). It is
// build tooling outside the compiler's compile path, using LLVM's C API (and one C++ call), with no vector path.

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
#include <unistd.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Comdat.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/Transforms/PassBuilder.h>

#define SPLIT_ALIAS_SUFFIX ".buster_split"
#define SPLIT_MAXIMUM_PARTS 62

// split_bitcode.cpp: writes `module` to `path` with its use-list order.
bool split_write_bitcode(LLVMModuleRef module, const char* path);
// split_bitcode.cpp: erases `alias` once its dead constant users are gone, and whether it could (nothing else used it).
bool split_erase_alias(LLVMValueRef alias);

typedef enum SplitSection
{
    SPLIT_SECTION_TEXT,
    SPLIT_SECTION_NAMED,
    SPLIT_SECTION_UNLIKELY,
    SPLIT_SECTIONS,
} SplitSection;

typedef struct SplitFunction SplitFunction;
struct SplitFunction
{
    LLVMValueRef function;
    uint64_t cost;
    uint32_t alignment;
    SplitSection section;
    // Emitted by some part: a definition that isn't available_externally.
    bool defined;
    // A local definition another part refers to: an alias names it there.
    bool aliased;
};

// Function definitions to their parts, by address.
typedef struct SplitMap SplitMap;
struct SplitMap
{
    LLVMValueRef* keys;
    uint32_t* values;
    uint64_t capacity;
};

typedef struct SplitPlan SplitPlan;
struct SplitPlan
{
    LLVMModuleRef module;
    SplitFunction* functions;
    uint32_t count;
    uint32_t parts;
    // Part p holds functions [starts[p], starts[p + 1]).
    uint32_t* starts;
    SplitMap owners;
    bool verify;
    bool report;
};

static SplitMap split_map_create(uint64_t minimum)
{
    uint64_t capacity = 1;
    while (capacity < minimum * 2)
    {
        capacity <<= 1;
    }
    return (SplitMap){ .keys = calloc(capacity, sizeof(LLVMValueRef)), .values = calloc(capacity, sizeof(uint32_t)), .capacity = capacity };
}

static uint64_t split_map_slot(const SplitMap* map, LLVMValueRef key)
{
    uint64_t slot = (((uint64_t)(uintptr_t)key * UINT64_C(0x9e3779b97f4a7c15)) >> 20) & (map->capacity - 1);
    while (map->keys[slot] != 0 && map->keys[slot] != key)
    {
        slot = (slot + 1) & (map->capacity - 1);
    }
    return slot;
}

static void split_map_put(SplitMap* map, LLVMValueRef key, uint32_t value)
{
    const uint64_t slot = split_map_slot(map, key);
    map->keys[slot] = key;
    map->values[slot] = value;
}

// The part that emits function `key`, or UINT32_MAX for one no part emits.
static uint32_t split_map_get(const SplitMap* map, LLVMValueRef key)
{
    const uint64_t slot = split_map_slot(map, key);
    return map->keys[slot] == key ? map->values[slot] : UINT32_MAX;
}

static bool split_has_attribute(LLVMValueRef function, const char* name)
{
    const unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    return LLVMGetEnumAttributeAtIndex(function, LLVMAttributeFunctionIndex, kind) != 0;
}

static bool split_local(LLVMValueRef value)
{
    const LLVMLinkage linkage = LLVMGetLinkage(value);
    return linkage == LLVMInternalLinkage || linkage == LLVMPrivateLinkage;
}

// The module's arrays whose entries the code generator emits beside what they name: llvm.used, which gives a definition
// a section the linker keeps (SHF_GNU_RETAIN: the sanitizers' module constructors), and the constructors and
// destructors, which go to .init_array or .fini_array, in the group of the entry's key where it has one
// (AddressSanitizer's). Each part keeps the entries of what it emits (split_part_entries).
static const char* const split_entry_arrays[] = { "llvm.used", "llvm.global_ctors", "llvm.global_dtors" };
#define SPLIT_ENTRY_ARRAYS (sizeof(split_entry_arrays) / sizeof(split_entry_arrays[0]))

static bool split_entry_array(LLVMValueRef global)
{
    size_t length = 0;
    const char* name = LLVMGetValueName2(global, &length);
    bool found = false;
    for (uint32_t i = 0; i < SPLIT_ENTRY_ARRAYS; i += 1)
    {
        found = found || strcmp(name, split_entry_arrays[i]) == 0;
    }
    return found;
}

// A lower bound of the alignment the code generator gives the function's start: the target's preferred 16 bytes but
// under optsize or minsize, and at least the declared alignment. Underestimating only refuses boundaries.
static uint32_t split_function_alignment(LLVMValueRef function)
{
    const uint32_t preferred = split_has_attribute(function, "optsize") || split_has_attribute(function, "minsize") ? 1 : 16;
    const uint32_t declared = LLVMGetAlignment(function);
    return declared > preferred ? declared : preferred;
}

// The section a definition's code goes to: a named one, a cold function's .text.unlikely. (CodeGenPrepare's prefix),
// or .text.
static SplitSection split_function_section(LLVMValueRef function)
{
    const char* section = LLVMGetSection(function);
    SplitSection result = SPLIT_SECTION_TEXT;
    if (section != 0 && section[0] != 0)
    {
        result = SPLIT_SECTION_NAMED;
    }
    else if (split_has_attribute(function, "cold"))
    {
        result = SPLIT_SECTION_UNLIKELY;
    }
    return result;
}

// The estimated cost of generating the function's code: its instructions, a quarter of them under optnone, which the
// fast instruction selector takes.
static uint64_t split_function_cost(LLVMValueRef function)
{
    uint64_t count = 0;
    for (LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(function); block != 0; block = LLVMGetNextBasicBlock(block))
    {
        for (LLVMValueRef instruction = LLVMGetFirstInstruction(block); instruction != 0; instruction = LLVMGetNextInstruction(instruction))
        {
            count += 1;
        }
    }
    if (split_has_attribute(function, "optnone"))
    {
        count = (count + 3) / 4;
    }
    return count + 8;
}

// The fewest parts covering the functions [0, count), each costing at most `limit` and valid: starting, in each text
// section, with a function of the greatest alignment it holds there, so that the relocatable link's section alignment
// adds no padding the single module didn't have. A part [i, j) that is invalid stays invalid as j grows, and one
// function alone is valid. parts_to[j] is the fewest parts covering [0, j), previous[j] where its last part starts.
// Returns parts_to[count], or UINT32_MAX when no cover exists.
static uint32_t split_cover(const SplitFunction* functions, uint32_t count, uint64_t limit, uint32_t* parts_to, uint32_t* previous)
{
    parts_to[0] = 0;
    for (uint32_t j = 1; j <= count; j += 1)
    {
        parts_to[j] = UINT32_MAX;
    }
    for (uint32_t i = 0; i < count; i += 1)
    {
        uint32_t first[SPLIT_SECTIONS] = { 0 };
        uint32_t greatest[SPLIT_SECTIONS] = { 0 };
        uint64_t cost = 0;
        bool valid = parts_to[i] != UINT32_MAX;
        for (uint32_t j = i; j < count && valid; j += 1)
        {
            const SplitFunction* function = &functions[j];
            if (function->defined)
            {
                const SplitSection section = function->section;
                cost += function->cost;
                first[section] = first[section] == 0 ? function->alignment : first[section];
                greatest[section] = function->alignment > greatest[section] ? function->alignment : greatest[section];
                valid = cost <= limit && first[section] >= greatest[section];
            }
            if (valid && parts_to[i] + 1 < parts_to[j + 1])
            {
                parts_to[j + 1] = parts_to[i] + 1;
                previous[j + 1] = i;
            }
        }
    }
    return parts_to[count];
}

// The parts' boundaries: contiguous in module order, every part valid (split_cover), the costliest part as cheap as
// the alignment rule allows, by bisection on its cost. Parts left over are empty, at the end.
static void split_boundaries(SplitPlan* plan)
{
    uint64_t total = 0;
    uint64_t largest = 0;
    for (uint32_t i = 0; i < plan->count; i += 1)
    {
        total += plan->functions[i].cost;
        largest = plan->functions[i].cost > largest ? plan->functions[i].cost : largest;
    }
    uint32_t* parts_to = calloc(plan->count + 1, sizeof(uint32_t));
    uint32_t* previous = calloc(plan->count + 1, sizeof(uint32_t));
    uint64_t low = largest;
    uint64_t high = total > largest ? total : largest;
    while (low < high)
    {
        const uint64_t middle = low + (high - low) / 2;
        if (split_cover(plan->functions, plan->count, middle, parts_to, previous) <= plan->parts)
        {
            high = middle;
        }
        else
        {
            low = middle + 1;
        }
    }
    const uint32_t used = split_cover(plan->functions, plan->count, low, parts_to, previous);
    for (uint32_t p = used; p <= plan->parts; p += 1)
    {
        plan->starts[p] = plan->count;
    }
    uint32_t at = plan->count;
    for (uint32_t p = used; p > 0; p -= 1)
    {
        at = previous[at];
        plan->starts[p - 1] = at;
    }
    free(parts_to);
    free(previous);
}

// The parts whose emitted code or data refers to `value`, as a mask: an instruction's function's part, and a global
// variable initializer's, the data part's, but an entry array's, whose entries go with what they name. Constant
// expressions and aggregates pass references on; an alias or ifunc refers to it under a name every part can use.
static uint64_t split_references(const SplitPlan* plan, LLVMValueRef value)
{
    uint64_t mask = 0;
    uint32_t capacity = 64;
    uint32_t depth = 1;
    LLVMValueRef* stack = malloc(capacity * sizeof(LLVMValueRef));
    stack[0] = value;
    while (depth != 0)
    {
        depth -= 1;
        LLVMValueRef current = stack[depth];
        for (LLVMUseRef use = LLVMGetFirstUse(current); use != 0; use = LLVMGetNextUse(use))
        {
            LLVMValueRef user = LLVMGetUser(use);
            if (LLVMIsAInstruction(user))
            {
                const uint32_t part = split_map_get(&plan->owners, LLVMGetBasicBlockParent(LLVMGetInstructionParent(user)));
                mask |= part != UINT32_MAX ? UINT64_C(1) << part : 0;
            }
            else if (LLVMIsAGlobalVariable(user))
            {
                mask |= LLVMIsDeclaration(user) || split_entry_array(user) ? 0 : UINT64_C(1) << plan->parts;
            }
            else if (LLVMIsAGlobalValue(user))
            {
                mask = ~UINT64_C(0);
            }
            else
            {
                if (depth == capacity)
                {
                    capacity *= 2;
                    stack = realloc(stack, capacity * sizeof(LLVMValueRef));
                }
                stack[depth] = user;
                depth += 1;
            }
        }
    }
    free(stack);
    return mask;
}

// The part that emits `value`'s base object (through constant expressions and aliases): its function's code part, or
// the data part for a global variable or anything no part defines.
static uint32_t split_owner(const SplitPlan* plan, LLVMValueRef value)
{
    LLVMValueRef base = value;
    bool stripping = true;
    for (uint32_t depth = 0; depth < 64 && stripping && base != 0; depth += 1)
    {
        if (LLVMIsAGlobalAlias(base) != 0)
        {
            base = LLVMAliasGetAliasee(base);
        }
        else if (LLVMIsAConstantExpr(base) != 0 && LLVMGetNumOperands(base) != 0)
        {
            base = LLVMGetOperand(base, 0);
        }
        else
        {
            stripping = false;
        }
    }
    uint32_t part = plan->parts;
    if (base != 0 && LLVMIsAFunction(base) != 0)
    {
        const uint32_t owner = split_map_get(&plan->owners, base);
        part = owner == UINT32_MAX ? plan->parts : owner;
    }
    return part;
}

// Whether every entry array's entries can go with what they name: a constructor's or destructor's key (the group its
// entry joins) emitted by the entry's own part, and the entries of each array in their parts' order, so that the merged
// object lists them in the single module's order. Says why when not.
static bool split_entries_kept(const SplitPlan* plan)
{
    bool kept = true;
    for (uint32_t n = 1; n < SPLIT_ENTRY_ARRAYS; n += 1)
    {
        LLVMValueRef array = LLVMGetNamedGlobal(plan->module, split_entry_arrays[n]);
        if (array != 0 && !LLVMIsDeclaration(array))
        {
            LLVMValueRef initializer = LLVMGetInitializer(array);
            const uint64_t count = LLVMGetArrayLength2(LLVMGlobalGetValueType(array));
            uint32_t previous = 0;
            for (uint64_t i = 0; i < count; i += 1)
            {
                LLVMValueRef entry = LLVMGetAggregateElement(initializer, (unsigned)i);
                LLVMValueRef function = entry != 0 ? LLVMGetAggregateElement(entry, 1) : 0;
                LLVMValueRef key = entry != 0 ? LLVMGetAggregateElement(entry, 2) : 0;
                const uint32_t owner = function != 0 ? split_owner(plan, function) : plan->parts;
                if (key != 0 && !LLVMIsNull(key) && split_owner(plan, key) != owner)
                {
                    fprintf(stderr, "split: %s's entry %llu has its key in another part\n", split_entry_arrays[n], (unsigned long long)i);
                    kept = false;
                }
                if (owner < previous)
                {
                    fprintf(stderr, "split: %s's entry %llu comes before an earlier entry's part\n", split_entry_arrays[n], (unsigned long long)i);
                    kept = false;
                }
                previous = owner;
            }
        }
    }
    return kept;
}

static void split_hide(LLVMValueRef value)
{
    LLVMSetLinkage(value, LLVMExternalLinkage);
    LLVMSetVisibility(value, LLVMHiddenVisibility);
}

// Whether a code part reads `global`'s initializer, available_externally (split_part): a strong constant's, one whose
// initializer the single module took as definitive.
static bool split_readable(LLVMValueRef global)
{
    return !LLVMIsDeclaration(global) && LLVMIsGlobalConstant(global) && LLVMGetLinkage(global) == LLVMExternalLinkage &&
           !LLVMIsThreadLocal(global) && !LLVMIsExternallyInitialized(global);
}

// What a code part reads, it may refer to: the code generator can fold a pointer out of a readable constant (a table's
// string), so every local global such an initializer names is made a hidden external one, in turn readable where it is
// a constant, until none is left. A local function it names already has its alias: the data part refers to it.
static void split_readable_closure(LLVMModuleRef module)
{
    uint32_t capacity = 1024;
    uint32_t depth = 0;
    LLVMValueRef* stack = malloc(capacity * sizeof(LLVMValueRef));
    for (LLVMValueRef global = LLVMGetFirstGlobal(module); global != 0; global = LLVMGetNextGlobal(global))
    {
        if (split_readable(global))
        {
            if (depth == capacity)
            {
                capacity *= 2;
                stack = realloc(stack, capacity * sizeof(LLVMValueRef));
            }
            stack[depth] = LLVMGetInitializer(global);
            depth += 1;
        }
    }
    while (depth != 0)
    {
        depth -= 1;
        LLVMValueRef value = stack[depth];
        if (LLVMIsAGlobalVariable(value))
        {
            if (split_local(value) && !LLVMIsDeclaration(value))
            {
                split_hide(value);
                if (split_readable(value))
                {
                    if (depth == capacity)
                    {
                        capacity *= 2;
                        stack = realloc(stack, capacity * sizeof(LLVMValueRef));
                    }
                    stack[depth] = LLVMGetInitializer(value);
                    depth += 1;
                }
            }
        }
        else if (!LLVMIsAGlobalValue(value) && LLVMIsAConstant(value))
        {
            const int operands = LLVMGetNumOperands(value);
            for (int i = 0; i < operands; i += 1)
            {
                if (depth == capacity)
                {
                    capacity *= 2;
                    stack = realloc(stack, capacity * sizeof(LLVMValueRef));
                }
                stack[depth] = LLVMGetOperand(value, (unsigned)i);
                depth += 1;
            }
        }
    }
    free(stack);
}

// The plan's decisions every part shares, made before the parts fork: the boundaries; llvm.compiler.used dropped (it
// only keeps definitions from the optimizer, which no longer runs, and ELF code generation ignores it; the entry arrays
// stay, for split_part_entries); names for unnamed local definitions; which functions are aliased; and the other local
// definitions another part refers to made hidden external ones.
static void split_plan(SplitPlan* plan)
{
    plan->count = 0;
    for (LLVMValueRef function = LLVMGetFirstFunction(plan->module); function != 0; function = LLVMGetNextFunction(function))
    {
        plan->count += 1;
    }
    plan->functions = calloc(plan->count + 1, sizeof(SplitFunction));
    uint32_t at = 0;
    for (LLVMValueRef function = LLVMGetFirstFunction(plan->module); function != 0; function = LLVMGetNextFunction(function))
    {
        SplitFunction* entry = &plan->functions[at];
        entry->function = function;
        entry->defined = !LLVMIsDeclaration(function) && LLVMGetLinkage(function) != LLVMAvailableExternallyLinkage;
        if (entry->defined)
        {
            entry->cost = split_function_cost(function);
            entry->alignment = split_function_alignment(function);
            entry->section = split_function_section(function);
        }
        at += 1;
    }
    plan->starts = calloc(plan->parts + 1, sizeof(uint32_t));
    split_boundaries(plan);

    LLVMValueRef global = LLVMGetFirstGlobal(plan->module);
    while (global != 0)
    {
        LLVMValueRef following = LLVMGetNextGlobal(global);
        size_t length = 0;
        const char* name = LLVMGetValueName2(global, &length);
        if (strcmp(name, "llvm.compiler.used") == 0)
        {
            LLVMDeleteGlobal(global);
        }
        global = following;
    }

    plan->owners = split_map_create((uint64_t)plan->count + 1024);
    for (uint32_t p = 0; p < plan->parts; p += 1)
    {
        for (uint32_t i = plan->starts[p]; i < plan->starts[p + 1]; i += 1)
        {
            if (plan->functions[i].defined)
            {
                split_map_put(&plan->owners, plan->functions[i].function, p);
            }
        }
    }
    uint64_t unnamed = 0;
    for (uint32_t i = 0; i < plan->count; i += 1)
    {
        SplitFunction* entry = &plan->functions[i];
        size_t length = 0;
        LLVMGetValueName2(entry->function, &length);
        if (length == 0)
        {
            char name[64];
            snprintf(name, sizeof(name), "__buster_split_function_%llu", (unsigned long long)unnamed);
            unnamed += 1;
            LLVMSetValueName2(entry->function, name, strlen(name));
        }
        if (entry->defined && split_local(entry->function))
        {
            const uint32_t owner = split_map_get(&plan->owners, entry->function);
            entry->aliased = (split_references(plan, entry->function) & ~(UINT64_C(1) << owner)) != 0;
        }
    }
    for (global = LLVMGetFirstGlobal(plan->module); global != 0; global = LLVMGetNextGlobal(global))
    {
        size_t length = 0;
        LLVMGetValueName2(global, &length);
        if (length == 0)
        {
            char name[64];
            snprintf(name, sizeof(name), "__buster_split_global_%llu", (unsigned long long)unnamed);
            unnamed += 1;
            LLVMSetValueName2(global, name, strlen(name));
        }
        if (!LLVMIsDeclaration(global) && split_local(global) && (split_references(plan, global) & ~(UINT64_C(1) << plan->parts)) != 0)
        {
            split_hide(global);
        }
    }
    split_readable_closure(plan->module);
}

// Deletes a function's body, leaving a declaration.
static void split_delete_body(LLVMValueRef function)
{
    for (LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(function); block != 0; block = LLVMGetNextBasicBlock(block))
    {
        for (LLVMValueRef instruction = LLVMGetFirstInstruction(block); instruction != 0; instruction = LLVMGetNextInstruction(instruction))
        {
            LLVMTypeRef type = LLVMTypeOf(instruction);
            if (LLVMGetTypeKind(type) != LLVMVoidTypeKind)
            {
                LLVMReplaceAllUsesWith(instruction, LLVMGetPoison(type));
            }
        }
    }
    for (LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(function); block != 0; block = LLVMGetNextBasicBlock(block))
    {
        LLVMValueRef instruction = LLVMGetFirstInstruction(block);
        while (instruction != 0)
        {
            LLVMValueRef next = LLVMGetNextInstruction(instruction);
            LLVMInstructionEraseFromParent(instruction);
            instruction = next;
        }
    }
    LLVMBasicBlockRef block = LLVMGetFirstBasicBlock(function);
    while (block != 0)
    {
        LLVMBasicBlockRef next = LLVMGetNextBasicBlock(block);
        LLVMDeleteBasicBlock(block);
        block = next;
    }
    LLVMGlobalClearMetadata(function);
    if (LLVMHasPersonalityFn(function))
    {
        LLVMSetPersonalityFn(function, 0);
    }
}

// Part `part`'s entry arrays (split_entry_array): the entries of what it emits, in their order, an array the same as
// the module's where it keeps them all and none where it keeps none (split_entries_kept checked that the rest can go).
static void split_part_entries(const SplitPlan* plan, uint32_t part)
{
    LLVMModuleRef module = plan->module;
    for (uint32_t n = 0; n < SPLIT_ENTRY_ARRAYS; n += 1)
    {
        LLVMValueRef array = LLVMGetNamedGlobal(module, split_entry_arrays[n]);
        if (array != 0 && !LLVMIsDeclaration(array))
        {
            LLVMValueRef initializer = LLVMGetInitializer(array);
            LLVMTypeRef type = LLVMGlobalGetValueType(array);
            LLVMTypeRef element = LLVMGetElementType(type);
            const uint64_t count = LLVMGetArrayLength2(type);
            LLVMValueRef* entries = calloc(count + 1, sizeof(LLVMValueRef));
            uint64_t kept = 0;
            for (uint64_t i = 0; i < count; i += 1)
            {
                LLVMValueRef entry = LLVMGetAggregateElement(initializer, (unsigned)i);
                LLVMValueRef subject = n == 0 ? entry : LLVMGetAggregateElement(entry, 1);
                if (split_owner(plan, subject) == part)
                {
                    entries[kept] = entry;
                    kept += 1;
                }
            }
            if (kept != count)
            {
                LLVMValueRef replacement = 0;
                if (kept != 0)
                {
                    replacement = LLVMAddGlobal(module, LLVMArrayType2(element, kept), "");
                    LLVMSetLinkage(replacement, LLVMAppendingLinkage);
                    const char* section = LLVMGetSection(array);
                    if (section != 0 && section[0] != 0)
                    {
                        LLVMSetSection(replacement, section);
                    }
                    LLVMSetInitializer(replacement, LLVMConstArray2(element, entries, kept));
                }
                LLVMDeleteGlobal(array);
                if (replacement != 0)
                {
                    LLVMSetValueName2(replacement, split_entry_arrays[n], strlen(split_entry_arrays[n]));
                }
            }
            free(entries);
        }
    }
}

// Part `part` of the plan, from the planned module, written to `path`. Runs in its own child process, which owns its
// copy of the module.
static bool split_part(const SplitPlan* plan, uint32_t part, const char* path)
{
    LLVMModuleRef module = plan->module;
    split_part_entries(plan, part);
    for (uint32_t i = 0; i < plan->count; i += 1)
    {
        const SplitFunction* entry = &plan->functions[i];
        const bool owned = part < plan->parts && i >= plan->starts[part] && i < plan->starts[part + 1];
        if (entry->aliased)
        {
            size_t length = 0;
            const char* name = LLVMGetValueName2(entry->function, &length);
            char* alias = malloc(length + sizeof(SPLIT_ALIAS_SUFFIX));
            memcpy(alias, name, length);
            memcpy(alias + length, SPLIT_ALIAS_SUFFIX, sizeof(SPLIT_ALIAS_SUFFIX));
            if (owned)
            {
                split_hide(LLVMAddAlias2(module, LLVMGlobalGetValueType(entry->function), 0, entry->function, alias));
            }
            else
            {
                LLVMSetValueName2(entry->function, alias, strlen(alias));
                split_hide(entry->function);
            }
            free(alias);
        }
        if (!owned && !LLVMIsDeclaration(entry->function))
        {
            // Another part's, or available_externally, which no part emits.
            split_delete_body(entry->function);
        }
    }
    for (LLVMValueRef global = LLVMGetFirstGlobal(module); global != 0; global = LLVMGetNextGlobal(global))
    {
        if (part < plan->parts && !LLVMIsDeclaration(global) && !split_entry_array(global))
        {
            // A code part reads a constant's initializer, available_externally, where the data part defines it: one
            // whose initializer the single module took as definitive, a strong definition's, not a weak one's.
            if (split_readable(global))
            {
                LLVMSetLinkage(global, LLVMAvailableExternallyLinkage);
            }
            else
            {
                LLVMSetInitializer(global, 0);
            }
            LLVMGlobalClearMetadata(global);
        }
    }
    // An alias goes with its base object: AddressSanitizer's private aliases of the globals it describes, which its
    // descriptors in the data part name. A part that doesn't emit the object drops the alias, which would be an alias
    // of a declaration; one that still refers to it there can't be split.
    bool aliases = true;
    LLVMValueRef alias = LLVMGetFirstGlobalAlias(module);
    while (alias != 0)
    {
        LLVMValueRef following = LLVMGetNextGlobalAlias(alias);
        if (split_owner(plan, alias) != part && !split_erase_alias(alias))
        {
            size_t length = 0;
            const char* name = LLVMGetValueName2(alias, &length);
            fprintf(stderr, "split: part %u refers to the alias %.*s, which goes with another part's definition\n", part, (int)length, name);
            aliases = false;
        }
        alias = following;
    }
    // What another part defines is a declaration here. A local one nothing live refers to (only dead constants left by
    // the deleted bodies, and metadata such as !inline_history) is an ordinary external declaration, which the code
    // generator doesn't name; as a hidden one, it would name it, undefined, which GNU ld refuses. One something does
    // refer to is hidden, as the defining part's name is; the merge's check of the local names finds either undefined.
    for (int kind = 0; kind < 2; kind += 1)
    {
        LLVMValueRef value = kind == 0 ? LLVMGetFirstFunction(module) : LLVMGetFirstGlobal(module);
        while (value != 0)
        {
            LLVMValueRef following = kind == 0 ? LLVMGetNextFunction(value) : LLVMGetNextGlobal(value);
            const LLVMLinkage linkage = LLVMGetLinkage(value);
            if (LLVMIsDeclaration(value) && linkage != LLVMExternalLinkage && linkage != LLVMExternalWeakLinkage)
            {
                if (split_local(value) && split_references(plan, value) == 0)
                {
                    LLVMSetLinkage(value, LLVMExternalLinkage);
                    LLVMSetVisibility(value, LLVMDefaultVisibility);
                }
                else if (split_local(value))
                {
                    split_hide(value);
                }
                else
                {
                    LLVMSetLinkage(value, LLVMExternalLinkage);
                }
            }
            // No declaration is in a comdat (AddressSanitizer puts each global it describes in one): the part that
            // defines it keeps the comdat, and the code generator emits nothing for a code part's readable copy.
            if (LLVMIsDeclaration(value) || LLVMGetLinkage(value) == LLVMAvailableExternallyLinkage)
            {
                LLVMSetComdat(value, 0);
            }
            value = following;
        }
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
    // The other parts' globals' debug records go, so that each part's DWARF describes what it emits.
    LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
    LLVMErrorRef error = LLVMRunPasses(module, "strip-dead-debug-info,strip-dead-prototypes", 0, options);
    LLVMDisposePassBuilderOptions(options);
    bool written = false;
    char* message = 0;
    if (error != 0)
    {
        message = LLVMGetErrorMessage(error);
        fprintf(stderr, "split: part %u: %s\n", part, message);
        LLVMDisposeErrorMessage(message);
    }
    else if (plan->verify && LLVMVerifyModule(module, LLVMPrintMessageAction, &message))
    {
        fprintf(stderr, "split: part %u fails verification\n", part);
    }
    else if (aliases)
    {
        written = split_write_bitcode(module, path);
        if (!written)
        {
            fprintf(stderr, "split: can't write %s\n", path);
        }
    }
    return written;
}

// How many parts to write at once: no more than the CPUs this process may run on (a build's grant, through its
// affinity), nor than the available memory holds copies of the module, since a child writing a part copies most of its
// pages as it deletes the other parts' bodies (14 children of the checker's 1 GB module came to about 14.8 GB).
static uint32_t split_concurrency(uint32_t wanted)
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
    const uint64_t by_memory = resident != 0 && available != 0 ? available / (resident + resident / 4) : wanted;
    uint64_t limit = wanted < cpus ? wanted : cpus;
    limit = by_memory < limit ? by_memory : limit;
    return limit == 0 ? 1 : (uint32_t)limit;
}

// Writes DIRECTORY/locals.txt: the names of the module's local definitions and of the aliases the parts give them, one a
// line, which the merge checks no part left undefined. Whether it was written.
static bool split_locals(const SplitPlan* plan, const char* directory)
{
    char path[4096];
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
            if (!LLVMIsDeclaration(value) && (split_local(value) || hidden))
            {
                size_t size = 0;
                const char* name = LLVMGetValueName2(value, &size);
                fprintf(file, "%.*s\n", (int)size, name);
                if (kind == 0 && split_local(value))
                {
                    fprintf(file, "%.*s" SPLIT_ALIAS_SUFFIX "\n", (int)size, name);
                }
            }
        }
    }
    written = file != 0 && fclose(file) == 0 && written;
    return written;
}

// Writes every part, a child process each, at most split_concurrency at once, and waits for them all: whether every
// part was written.
static bool split_parts(const SplitPlan* plan, const char* directory)
{
    const uint32_t total = plan->parts + 1;
    const uint32_t limit = split_concurrency(total);
    pid_t* children = calloc(total, sizeof(pid_t));
    bool done = true;
    uint32_t running = 0;
    for (uint32_t part = 0; part < total || running != 0;)
    {
        if (part < total && running < limit && done)
        {
            fflush(0);
            children[part] = fork();
            if (children[part] == 0)
            {
                char path[4096];
                const int length = snprintf(path, sizeof(path), "%s/part%u.bc", directory, part);
                _exit(length > 0 && (size_t)length < sizeof(path) && split_part(plan, part, path) ? 0 : 1);
            }
            done = done && children[part] > 0;
            running += children[part] > 0;
            part += 1;
        }
        else if (running != 0)
        {
            int status = 0;
            const pid_t child = waitpid(-1, &status, 0);
            const bool interrupted = child < 0 && errno == EINTR;
            done = done && (interrupted || (child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0));
            // No child left to wait for (ECHILD) ends the wait too.
            running = child > 0 ? running - 1 : interrupted ? running : 0;
        }
        else
        {
            // A failed fork or child stops the remaining parts.
            part = total;
        }
    }
    free(children);
    return done;
}

// Prints each part's function range, definitions and estimated cost.
static void split_report(const SplitPlan* plan)
{
    for (uint32_t p = 0; p < plan->parts; p += 1)
    {
        uint64_t cost = 0;
        uint32_t defined = 0;
        for (uint32_t i = plan->starts[p]; i < plan->starts[p + 1]; i += 1)
        {
            cost += plan->functions[i].cost;
            defined += plan->functions[i].defined;
        }
        fprintf(stderr, "split: part %u: functions [%u, %u), %u definitions, cost %llu\n", p, plan->starts[p], plan->starts[p + 1], defined,
                (unsigned long long)cost);
    }
}

int main(int argc, char** argv)
{
    int status = 2;
    bool verify = false;
    bool report = false;
    bool flags = argc >= 4;
    for (int i = 4; i < argc; i += 1)
    {
        verify = verify || strcmp(argv[i], "--verify") == 0;
        report = report || strcmp(argv[i], "--report") == 0;
        flags = flags && (strcmp(argv[i], "--verify") == 0 || strcmp(argv[i], "--report") == 0);
    }
    char* end = 0;
    const unsigned long parts = argc >= 4 ? strtoul(argv[2], &end, 10) : 0;
    if (!flags || end == argv[2] || *end != 0 || parts == 0 || parts > SPLIT_MAXIMUM_PARTS)
    {
        fprintf(stderr, "usage: split INPUT.bc PARTS DIRECTORY [--verify] [--report]  (PARTS from 1 to %u)\n", SPLIT_MAXIMUM_PARTS);
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
            fprintf(stderr, "split: %s: %s\n", argv[1], message);
            LLVMDisposeMessage(message);
        }
        else if (LLVMParseBitcodeInContext2(context, buffer, &module))
        {
            fprintf(stderr, "split: can't read %s\n", argv[1]);
        }
        else
        {
            SplitPlan plan = { .module = module, .parts = (uint32_t)parts, .verify = verify, .report = report };
            split_plan(&plan);
            if (plan.report)
            {
                split_report(&plan);
            }
            status = split_entries_kept(&plan) && split_locals(&plan, argv[3]) && split_parts(&plan, argv[3]) ? 0 : 1;
        }
    }
    return status;
}
