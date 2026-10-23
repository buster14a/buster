// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #148's tiny baseline-x86 terminal builtin helpers. Include after c_builtin_catalog.c and tier0.c. The existing
// generated-ISA table explicitly permits UD2 at x86-64 baseline, and its semantics classify the form as a faulting
// trap. Both __builtin_trap and __builtin_unreachable use that terminal realization: reaching unreachable already
// has undefined behavior, so trapping there is a conservative choice. The helper never returns and emits no RET.
//
// The source producer must retain terminal control flow (no successor) and the call effect. It may bind these records
// to one local per-unit TEXT atom and use ordinary CALL/fixups. A void-result call alone does not represent that
// terminal property, and this API neither adds a typed opcode nor makes the in-place misc lowerer accept one.
//
// Successful code owns one arena-allocated X86Instruction, with zero parameters, a VOID result and no branches or
// symbolic references. No padding is readable beyond that record. Invalid IDs and allocation failure leave the
// arena's length and bytes unchanged. The caller owns arena lifetime; returned records remain valid until reset.
// Constructing one fixed record has no data-parallel work or runtime benchmark; no throughput claim is made.
#ifndef BUSTER_C_BUILTIN_TERMINAL_HELPERS_INCLUDED
#define BUSTER_C_BUILTIN_TERMINAL_HELPERS_INCLUDED

typedef struct CBuiltinTerminalHelper CBuiltinTerminalHelper;
struct CBuiltinTerminalHelper
{
    Tier0Code code;
    uint32_t parameter_count;
    uint32_t result_class;
    bool terminates;
};

static CBuiltinTerminalHelper cbuiltin_terminal_helper(CBuiltinId id, Arena* output)
{
    CBuiltinTerminalHelper result = { 0 };
    if (id != CBUILTIN_TRAP && id != CBUILTIN_UNREACHABLE)
    {
        result.code.error = TIER0_ERROR_UNSUPPORTED;
    }
    else if (output == 0 || output->length > output->capacity || (output->base == 0 && output->capacity != 0))
    {
        result.code.error = TIER0_ERROR_INVALID;
    }
    else
    {
        X86Instruction* record = BUSTER_ARENA_PUSH_ARRAY(output, X86Instruction, 1);
        if (record == 0)
        {
            result.code.error = TIER0_ERROR_MEMORY;
        }
        else
        {
            *record = (X86Instruction){ .form = X86_FORM_UD2 };
            result.code.records = record;
            result.code.count = 1;
            result.result_class = BUSTER_TYPED_CLASS_VOID;
            result.terminates = true;
        }
    }
    return result;
}

#endif
