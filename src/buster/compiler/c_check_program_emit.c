// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real consumer of CheckProgram: lower, validate, compile, size branches, encode, and publish code atoms and
// their symbolic fixups in the Link shared with static data. Symbol IDs are assigned once by check_program.
// data_capacity reserves the data consumer's atoms/fixups; that consumer defines its already-created symbols.
// The caller performs final link_check after both publications. Output tables/names and byte arenas are distinct
// from scratch; all three roll back on failure. Backend template initialization precedes concurrent workers.

typedef struct CheckProgramCode CheckProgramCode;
struct CheckProgramCode
{
    Link link;
    CheckProgramLowered lowered;
    AtomId* function_atoms;
    CheckProgramStatus status;
    Tier0Error backend_error;
    uint32_t token;
    uint32_t backend_node;
    uint64_t records;
    uint64_t deferred;
    uint64_t code_bytes;
};

static bool check_program_emit(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                               const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch)
{
    *result = (CheckProgramCode){ .token = BUSTER_CHECK_NONE };
    const uint64_t mark = output->length;
    const uint64_t byte_mark = bytes->length;
    const uint64_t temporary = scratch->length;
    bool valid = output != bytes && output != scratch && bytes != scratch && program->status == CHECK_PROGRAM_OK;
    result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && !tier0_initialized)
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    if (valid)
    {
        valid = check_program_lower(&result->lowered, program, scalar, output, scratch);
        if (!valid)
        {
            result->status = result->lowered.status;
            result->token = result->lowered.token;
            result->backend_node = result->lowered.backend_node;
        }
    }
    const uint32_t count = program->body_count;
    Tier0Code* codes = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, Tier0Code, count) : 0;
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    result->function_atoms = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, count) : 0;
    valid = valid && alignments != 0 && (count == 0 || (codes != 0 && result->function_atoms != 0));
    if (valid)
    {
        memset(alignments, 0, (uint64_t)program->symbol_count + 1);
        for (uint32_t token = 0; token < program->input->token_count; token += 1)
        {
            const CheckInputAttributes* attributes = program->input->attributes[token];
            const uint32_t object = program->input->objects[token];
            if (program->input->declarations[token] == token && attributes != 0 && object != 0 && object < program->object_count)
            {
                const SymbolId symbol = program->object_symbols[object];
                alignments[symbol] = attributes->alignment_log2 > alignments[symbol] ? attributes->alignment_log2 : alignments[symbol];
            }
        }
    }
    uint64_t references = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const Tier0Function* function = &result->lowered.functions[b];
        codes[b] = scalar ? tier0_compile_scalar(function, scratch) : tier0_compile(function, scratch);
        valid = codes[b].error == TIER0_OK;
        if (valid)
        {
            references += codes[b].reference_count;
            result->records += codes[b].count;
            result->deferred += codes[b].deferred;
        }
        else
        {
            result->status = codes[b].error == TIER0_ERROR_MEMORY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
            result->backend_error = codes[b].error;
            result->backend_node = codes[b].node;
            result->token = program->bodies[b].declaration;
        }
    }
    const uint64_t atom_count = (uint64_t)data_capacity.atoms + count;
    const uint64_t fixup_count = (uint64_t)data_capacity.fixups + references;
    const uint64_t wide_count = (uint64_t)data_capacity.wide_addends + references;
    valid = valid && atom_count < UINT32_MAX && fixup_count < UINT32_MAX && wide_count < UINT32_MAX;
    const LinkCapacity capacity = {
        .names = program->symbol_count,
        .atoms = valid ? (uint32_t)atom_count : 0,
        .symbols = program->symbol_count,
        .fixups = valid ? (uint32_t)fixup_count : 0,
        .wide_addends = valid ? (uint32_t)wide_count : 0,
        .priorities = data_capacity.priorities,
    };
    valid = valid && link_initialize(&result->link, output, output, capacity);
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        NameId name = 0;
        uint8_t flags = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            if (program->input->compound_objects[declaration])
            {
                char spelling[48];
                const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_compound_%u", object);
                name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
            }
            else
            {
                name = link_add_name(&result->link, intern_bytes(identifiers, identifier), intern_length(identifiers, identifier));
            }
            const uint8_t kind = program->input->declaration_kinds[declaration] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION :
                                 program->input->thread_local[declaration] ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            flags = kind | (program->input->linkage[declaration] == 2 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL);
        }
        else if (object != 0 && object - 1 < parser->tokens.count && parser->tokens.kinds[object - 1] == PARSE_KIND_STRING)
        {
            char spelling[48];
            const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_string_%u", object);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        }
        else
        {
            result->status = CHECK_PROGRAM_INVALID;
            result->token = object != 0 ? object - 1 : 0;
        }
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == s + 1;
    }
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const Tier0Code* code = &codes[b];
        const uint64_t work_mark = scratch->length;
        uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
        uint32_t* fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
        const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
        const uint64_t before = bytes->length;
        uint8_t* encoded = arena_push(bytes, byte_capacity, 16);
        valid = offsets != 0 && fixup_ends != 0 && encoded != 0;
        if (valid)
        {
            const BranchSizingCheck shape = branch_sizing_validate(code->records, code->count, code->branches, code->targets, code->branch_count);
            const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
            const int64_t length = sizing.error == BRANCH_SIZING_OK ? x86_encode(code->records, code->count, encoded, byte_capacity, 0, fixup_ends) : -1;
            valid = length >= 0;
            if (valid)
            {
                const uint64_t end = (uint64_t)(encoded - bytes->base) + (uint64_t)length;
                arena_pop_to(bytes, end > before ? end : before);
                const SymbolId symbol = program->object_symbols[program->bodies[b].object];
                const uint8_t alignment = alignments[symbol] > 5 ? alignments[symbol] - 1 : 4;
                const AtomId atom = link_add_atom(&result->link, BUSTER_LINK_CLASS_TEXT, alignment, (uint64_t)length, encoded, 0);
                valid = atom != 0 && symbol != 0 && link_define_symbol(&result->link, symbol, atom, 0, (uint64_t)length) &&
                        link_add_code_fixups(&result->link, atom, code->records, code->count, fixup_ends, code->reference_count, code->references, code->reference_count);
                if (valid)
                {
                    result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
                }
                result->function_atoms[b] = atom;
                result->code_bytes += (uint64_t)length;
            }
            else
            {
                result->status = CHECK_PROGRAM_BACKEND;
                result->token = program->bodies[b].declaration;
            }
        }
        arena_pop_to(scratch, work_mark);
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : result->status;
        const uint32_t token = result->token;
        const Tier0Error backend = result->backend_error;
        const uint32_t backend_node = result->backend_node;
        arena_pop_to(output, mark);
        arena_pop_to(bytes, byte_mark);
        *result = (CheckProgramCode){ .status = status, .token = token, .backend_error = backend, .backend_node = backend_node };
    }
    arena_pop_to(scratch, temporary);
    return valid;
}
