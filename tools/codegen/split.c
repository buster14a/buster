// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The order-preserving partition of one unit's optimized module for parallel native code generation
// (docs/commands/build.md, "Parallel native generation"):
//
//   split INPUT.bc PARTS DIRECTORY [--verify]
//
// writes DIRECTORY/part0.bc to DIRECTORY/partPARTS.bc. Parts 0 to PARTS - 1 hold contiguous ranges of the module's
// function definitions in module order, of about equal instruction counts; part PARTS holds every global variable
// definition and no function. The code generator emits a module's functions in order, each with its constant pools and
// jump tables, and its global variables after them, so the parts' sections, concatenated in index order by
// BusterCodegenMerge.cmake's relocatable link, hold the single module's code and data in the single module's order.
//
// What each part's code generation sees is what the single module's saw:
// - a function another part refers to stays local where it is defined, so that the assembler resolves, and relaxes,
//   the jumps to it there as before (a near tail call's two-byte jmp); a hidden alias, NAME.buster_split, names it for
//   the other parts, which refer to that name. Other local symbols another part refers to become hidden external ones;
// - a code part keeps the initializer of each constant global, available_externally, so that the code generator folds
//   what it reads from one (a string's bytes into immediate stores) as before, and emits none of them;
// - the module's inline assembly (the checker section's placement, main.c) stays in part 0, before every function;
// - a part starts, in each of its text sections, at a function of the greatest alignment it holds there, so that the
//   relocatable link's section alignment adds no padding the single module didn't have;
// - the bitcode keeps the use-list order (split_bitcode.cpp), which instruction selection follows.
// The merge restores local binding to everything the input didn't export. Read-only data keeps its order and each
// item's alignment, but a part's .rodata takes its greatest item's alignment, so the items after a part boundary can
// move by less than that alignment. The build's comparison (build-regression) checks the rest: identical functions at
// identical addresses.
//
// The input is parsed once; the parts are written by one child process each, forked after the plan, in parallel. It is
// build tooling outside the compiler's compile path, using LLVM's C API (and one C++ call), with no vector path.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/Transforms/PassBuilder.h>

#define SPLIT_ALIAS_SUFFIX ".buster_split"
#define SPLIT_MAXIMUM_PARTS 62

// split_bitcode.cpp: writes `module` to `path` with its use-list order.
bool split_write_bitcode(LLVMModuleRef module, const char* path);

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

// Whether the part [begin, end) starts, in each text section, with a function of the greatest alignment it holds there.
static bool split_part_valid(const SplitFunction* functions, uint32_t begin, uint32_t end)
{
    uint32_t first[SPLIT_SECTIONS] = { 0 };
    uint32_t greatest[SPLIT_SECTIONS] = { 0 };
    for (uint32_t i = begin; i < end; i += 1)
    {
        if (functions[i].defined)
        {
            const SplitSection section = functions[i].section;
            first[section] = first[section] == 0 ? functions[i].alignment : first[section];
            greatest[section] = functions[i].alignment > greatest[section] ? functions[i].alignment : greatest[section];
        }
    }
    bool valid = true;
    for (uint32_t section = 0; section < SPLIT_SECTIONS; section += 1)
    {
        valid = valid && first[section] >= greatest[section];
    }
    return valid;
}

// The parts' boundaries: by cost first, contiguous in module order; then each part's start moves forward until the part
// is valid (split_part_valid). A start only moves forward and extends the part before it, which is checked again, so the
// loop ends, every part valid (an empty one is).
static void split_boundaries(SplitPlan* plan)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < plan->count; i += 1)
    {
        total += plan->functions[i].cost;
    }
    plan->starts[0] = 0;
    plan->starts[plan->parts] = plan->count;
    uint64_t running = 0;
    uint32_t next = 1;
    for (uint32_t i = 0; i < plan->count; i += 1)
    {
        while (next < plan->parts && plan->functions[i].defined && running * plan->parts >= (uint64_t)next * total)
        {
            plan->starts[next] = i;
            next += 1;
        }
        running += plan->functions[i].cost;
    }
    for (; next < plan->parts; next += 1)
    {
        plan->starts[next] = plan->count;
    }
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (uint32_t p = 1; p < plan->parts; p += 1)
        {
            while (plan->starts[p] < plan->starts[p + 1] && !split_part_valid(plan->functions, plan->starts[p], plan->starts[p + 1]))
            {
                plan->starts[p] += 1;
                changed = true;
            }
        }
    }
}

// The parts whose emitted code or data refers to `value`, as a mask: an instruction's function's part, and a global
// variable initializer's, the data part's. Constant expressions and aggregates pass references on; an alias or ifunc
// refers to it under a name every part can use.
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
                mask |= LLVMIsDeclaration(user) ? 0 : UINT64_C(1) << plan->parts;
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

static void split_hide(LLVMValueRef value)
{
    LLVMSetLinkage(value, LLVMExternalLinkage);
    LLVMSetVisibility(value, LLVMHiddenVisibility);
}

// The plan's decisions every part shares, made before the parts fork: the boundaries; the appending arrays dropped
// (they only keep definitions from the optimizer, which no longer runs, and ELF code generation ignores
// llvm.compiler.used); names for unnamed local definitions; which functions are aliased; and the other local
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
        if (strcmp(name, "llvm.compiler.used") == 0 || strcmp(name, "llvm.used") == 0)
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

// Part `part` of the plan, from the planned module, written to `path`. Runs in its own child process, which owns its
// copy of the module.
static bool split_part(const SplitPlan* plan, uint32_t part, const char* path)
{
    LLVMModuleRef module = plan->module;
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
        if (part < plan->parts && !LLVMIsDeclaration(global))
        {
            // A code part reads a constant's initializer, available_externally, where the data part defines it.
            if (LLVMIsGlobalConstant(global) && !split_local(global))
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
    // What another part defines is a declaration here. A local one is unreferenced but by dead constants, so it emits
    // nothing; were it referenced, the merge would find its hidden name undefined.
    for (int kind = 0; kind < 2; kind += 1)
    {
        for (LLVMValueRef value = kind == 0 ? LLVMGetFirstFunction(module) : LLVMGetFirstGlobal(module); value != 0;
             value = kind == 0 ? LLVMGetNextFunction(value) : LLVMGetNextGlobal(value))
        {
            const LLVMLinkage linkage = LLVMGetLinkage(value);
            if (LLVMIsDeclaration(value) && linkage != LLVMExternalLinkage && linkage != LLVMExternalWeakLinkage)
            {
                if (split_local(value))
                {
                    split_hide(value);
                }
                else
                {
                    LLVMSetLinkage(value, LLVMExternalLinkage);
                }
            }
        }
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
    else
    {
        written = split_write_bitcode(module, path);
        if (!written)
        {
            fprintf(stderr, "split: can't write %s\n", path);
        }
    }
    return written;
}

// Forks a child per part and waits for them all: whether every part was written.
static bool split_parts(const SplitPlan* plan, const char* directory)
{
    pid_t* children = calloc(plan->parts + 1, sizeof(pid_t));
    bool done = true;
    for (uint32_t part = 0; part <= plan->parts; part += 1)
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
    }
    for (uint32_t part = 0; part <= plan->parts; part += 1)
    {
        int status = 0;
        const bool waited = children[part] > 0 && waitpid(children[part], &status, 0) == children[part];
        done = done && waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    free(children);
    return done;
}

int main(int argc, char** argv)
{
    int status = 2;
    const bool verify = argc == 5 && strcmp(argv[4], "--verify") == 0;
    char* end = 0;
    const unsigned long parts = argc >= 4 ? strtoul(argv[2], &end, 10) : 0;
    if ((argc != 4 && !verify) || end == argv[2] || *end != 0 || parts == 0 || parts > SPLIT_MAXIMUM_PARTS)
    {
        fprintf(stderr, "usage: split INPUT.bc PARTS DIRECTORY [--verify]  (PARTS from 1 to %u)\n", SPLIT_MAXIMUM_PARTS);
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
            SplitPlan plan = { .module = module, .parts = (uint32_t)parts, .verify = verify };
            split_plan(&plan);
            status = split_parts(&plan, argv[3]) ? 0 : 1;
        }
    }
    return status;
}
