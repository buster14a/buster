// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// GNU-style CFI adapter. Parsing retains fragment identities, never provisional PCs. After relaxation, the writer
// encodes bounded DWARF events, interns identical CIE bytes and publishes one .eh_frame atom plus PC32 fixups.
// This producer is independent of the code generator's fixed-frame unwind templates. No external code is copied.

#define BUSTER_ASM_CFI_ESCAPE UINT32_MAX

static uint32_t asm_cfi_register(AsmParser* parser, uint32_t end)
{
    const uint32_t location = parser->tokens[parser->at].start;
    const bool percent = asm_take(parser, '%');
    const String8 name = asm_token_text(parser, parser->at);
    X86Operand operand = {0};
    uint32_t result = 0;
    if (asm_is(parser, parser->at, "rip"))
    {
        result = 16;
        parser->at += 1;
    }
    else if (asm_register(name, &operand))
    {
        static const uint8_t gprs[] = {0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15};
        if (operand.class <= BUSTER_X86_CLASS_GPR64 && operand.reg < 16)
        {
            result = gprs[operand.reg];
        }
        else if (operand.class == BUSTER_X86_CLASS_XMM && operand.reg < 16)
        {
            result = 17 + operand.reg;
        }
        else
        {
            asm_fail(parser, location, "unsupported named CFI register");
        }
        parser->at += 1;
    }
    else
    {
        const __int128 value = asm_absolute(parser, end);
        if (percent || value < 0 || value > UINT16_MAX)
        {
            asm_fail(parser, location, "CFI register must fit 16 bits");
        }
        result = (uint32_t)value;
    }
    return result;
}

static void asm_cfi_directive(AsmParser* parser, uint32_t token, uint32_t end)
{
    const uint32_t location = parser->tokens[token].start;
    if (parser->cfi == 0)
    {
        parser->cfi = BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmCfiEvent, parser->instruction_capacity);
        parser->procedures = BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmCfiProcedure, parser->instruction_capacity);
        parser->cfi_stack = BUSTER_ARENA_PUSH_ARRAY(parser->arena, AsmCfiState, parser->instruction_capacity);
        if (parser->cfi == 0 || parser->procedures == 0 || parser->cfi_stack == 0)
        {
            asm_fail(parser, location, "arena exhausted allocating CFI events");
        }
    }
    if (parser->result.error == 0 && asm_is(parser, token, ".cfi_sections"))
    {
        if (parser->procedure_count != 0 || !asm_is(parser, parser->at++, ".eh_frame"))
        {
            asm_fail(parser, location, "CFI output supports .eh_frame selected before the first procedure");
        }
    }
    else if (parser->result.error == 0 && asm_is(parser, token, ".cfi_startproc"))
    {
        if (parser->cfi_active || parser->section == 0 || (parser->sections[parser->section].object.flags & SHF_EXECINSTR) == 0 || parser->procedure_count >= parser->instruction_capacity)
        {
            asm_fail(parser, location, "CFI procedure must start once in a code section");
        }
        else
        {
            const bool simple = parser->at < end && asm_is(parser, parser->at, "simple");
            parser->at += simple;
            parser->procedures[parser->procedure_count++] = (AsmCfiProcedure){
                .symbol = asm_dot(parser, location),
                .first = parser->cfi_count,
                .return_column = 16,
                .simple = simple,
            };
            parser->cfi_active = true;
            parser->cfi_depth = 0;
            parser->cfa = (AsmCfiState){.offset = simple ? 0 : 8, .reg = 7};
        }
    }
    else if (parser->result.error == 0 && !parser->cfi_active)
    {
        asm_fail(parser, location, "CFI directive outside a procedure");
    }
    else if (parser->result.error == 0)
    {
        AsmCfiProcedure* procedure = &parser->procedures[parser->procedure_count - 1];
        const uint32_t start = parser->symbols[procedure->symbol].fragment;
        if (parser->fragments[start].section != parser->section)
        {
            asm_fail(parser, location, "CFI procedure crosses sections");
        }
        if (asm_is(parser, token, ".cfi_endproc"))
        {
            procedure->end = asm_fragment(parser, ASM_LABEL, location);
            procedure->count = parser->cfi_count - procedure->first;
            parser->cfi_active = false;
        }
        else if (asm_is(parser, token, ".cfi_signal_frame"))
        {
            procedure->signal = true;
        }
        else if (asm_is(parser, token, ".cfi_return_column"))
        {
            procedure->return_column = asm_cfi_register(parser, end);
            if (procedure->return_column > UINT8_MAX)
            {
                asm_fail(parser, location, "CFI return column exceeds the supported CIE version");
            }
        }
        else if (parser->cfi_count >= parser->instruction_capacity)
        {
            asm_fail(parser, location, "CFI event capacity exhausted");
        }
        else
        {
            AsmCfiEvent event = {.fragment = asm_fragment(parser, ASM_LABEL, location)};
            if (asm_is(parser, token, ".cfi_def_cfa") || asm_is(parser, token, ".cfi_offset") || asm_is(parser, token, ".cfi_rel_offset") || asm_is(parser, token, ".cfi_val_offset"))
            {
                event.first = asm_cfi_register(parser, end);
                asm_expect(parser, ',', "expected comma before CFI offset");
                const __int128 offset = asm_absolute(parser, end);
                if (offset < INT32_MIN || offset > INT32_MAX)
                {
                    asm_fail(parser, location, "CFI offset must fit signed 32 bits");
                }
                event.second = offset;
                event.opcode = asm_is(parser, token, ".cfi_def_cfa") ? 0x0c : asm_is(parser, token, ".cfi_val_offset") ? 0x14 : 0x80;
                if (asm_is(parser, token, ".cfi_rel_offset"))
                {
                    event.second -= parser->cfa.offset;
                }
                if (event.opcode != 0x0c && event.second % 8 != 0)
                {
                    asm_fail(parser, location, "CFI saved offset must be a multiple of eight");
                }
                if (event.opcode == 0x0c)
                {
                    if (event.second < 0 && event.second % 8 != 0)
                    {
                        asm_fail(parser, location, "negative CFI CFA offset must be a multiple of eight");
                    }
                    parser->cfa = (AsmCfiState){.offset = event.second, .reg = event.first};
                }
            }
            else if (asm_is(parser, token, ".cfi_def_cfa_offset") || asm_is(parser, token, ".cfi_adjust_cfa_offset"))
            {
                const __int128 value = asm_absolute(parser, end) + (asm_is(parser, token, ".cfi_adjust_cfa_offset") ? parser->cfa.offset : 0);
                if (value < INT32_MIN || value > INT32_MAX)
                {
                    asm_fail(parser, location, "CFI CFA offset must fit signed 32 bits");
                }
                event.opcode = 0x0e;
                event.first = value;
                if (value < 0 && value % 8 != 0)
                {
                    asm_fail(parser, location, "negative CFI CFA offset must be a multiple of eight");
                }
                parser->cfa.offset = value;
            }
            else if (asm_is(parser, token, ".cfi_def_cfa_register") || asm_is(parser, token, ".cfi_restore") || asm_is(parser, token, ".cfi_undefined") || asm_is(parser, token, ".cfi_same_value") || asm_is(parser, token, ".cfi_register"))
            {
                event.first = asm_cfi_register(parser, end);
                event.opcode = asm_is(parser, token, ".cfi_def_cfa_register") ? 0x0d : asm_is(parser, token, ".cfi_restore") ? 0xc0 : asm_is(parser, token, ".cfi_undefined") ? 7 : asm_is(parser, token, ".cfi_same_value") ? 8 : 9;
                if (event.opcode == 9)
                {
                    asm_expect(parser, ',', "expected second CFI register");
                    event.second = asm_cfi_register(parser, end);
                }
                else if (event.opcode == 0x0d)
                {
                    parser->cfa.reg = event.first;
                }
            }
            else if (asm_is(parser, token, ".cfi_remember_state") || asm_is(parser, token, ".cfi_restore_state"))
            {
                const bool remember = asm_is(parser, token, ".cfi_remember_state");
                event.opcode = remember ? 0x0a : 0x0b;
                if (remember && parser->cfi_depth < parser->instruction_capacity)
                {
                    parser->cfi_stack[parser->cfi_depth++] = parser->cfa;
                }
                else if (!remember && parser->cfi_depth != 0)
                {
                    parser->cfa = parser->cfi_stack[--parser->cfi_depth];
                }
                else
                {
                    asm_fail(parser, location, "CFI state stack is empty or exhausted");
                }
            }
            else if (asm_is(parser, token, ".cfi_escape"))
            {
                event.opcode = BUSTER_ASM_CFI_ESCAPE;
                event.bytes = arena_push(parser->arena, end - parser->at + 1, 1);
                if (event.bytes == 0)
                {
                    asm_fail(parser, location, "arena exhausted storing CFI escape bytes");
                }
                do
                {
                    const __int128 byte = asm_absolute(parser, end);
                    if (byte < 0 || byte > UINT8_MAX)
                    {
                        asm_fail(parser, location, "CFI escape value must fit a byte");
                    }
                    if (parser->result.error == 0)
                    {
                        event.bytes[event.size++] = byte;
                    }
                } while (parser->result.error == 0 && asm_take(parser, ','));
            }
            else
            {
                asm_fail(parser, location, "unsupported CFI directive");
            }
            parser->cfi[parser->cfi_count++] = event;
        }
    }
}

typedef struct AsmCfiWriter AsmCfiWriter;
struct AsmCfiWriter
{
    uint8_t* bytes;
    uint32_t size;
    uint32_t capacity;
    bool failed;
};

static void asm_cfi_integer(AsmCfiWriter* writer, uint32_t value, uint32_t count)
{
    if (count > writer->capacity - writer->size)
    {
        writer->failed = true;
    }
    else
    {
        memcpy(writer->bytes + writer->size, &value, count);
        writer->size += count;
    }
}

static void asm_cfi_leb(AsmCfiWriter* writer, int64_t value, bool sign)
{
    bool more;
    do
    {
        const uint8_t byte = value & 0x7f;
        value >>= 7;
        more = sign ? !((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) : value != 0;
        asm_cfi_integer(writer, byte | (more ? 0x80 : 0), 1);
    } while (more && !writer->failed);
}

static void asm_cfi_event(AsmCfiWriter* writer, const AsmCfiEvent* event)
{
    uint32_t opcode = event->opcode;
    if (opcode == BUSTER_ASM_CFI_ESCAPE)
    {
        for (uint32_t i = 0; i < event->size; i += 1)
        {
            asm_cfi_integer(writer, event->bytes[i], 1);
        }
    }
    else if (opcode == 0x80 || opcode == 0x14)
    {
        const int64_t factored = event->second / -8;
        const bool compact = opcode == 0x80 && event->first < 64 && factored >= 0;
        opcode = compact ? 0x80 | (uint32_t)event->first : opcode == 0x80 ? (factored < 0 ? 0x11 : 5) : factored < 0 ? 0x15 : 0x14;
        asm_cfi_integer(writer, opcode, 1);
        if (!compact)
        {
            asm_cfi_leb(writer, event->first, false);
        }
        asm_cfi_leb(writer, factored, factored < 0);
    }
    else if (opcode == 0xc0)
    {
        asm_cfi_integer(writer, event->first < 64 ? 0xc0 | event->first : 6, 1);
        if (event->first >= 64)
        {
            asm_cfi_leb(writer, event->first, false);
        }
    }
    else if (opcode == 0x0c || opcode == 0x0e)
    {
        const int64_t offset = opcode == 0x0c ? event->second : event->first;
        asm_cfi_integer(writer, offset < 0 ? (opcode == 0x0c ? 0x12 : 0x13) : opcode, 1);
        if (opcode == 0x0c)
        {
            asm_cfi_leb(writer, event->first, false);
        }
        asm_cfi_leb(writer, offset < 0 ? offset / -8 : offset, offset < 0);
    }
    else
    {
        asm_cfi_integer(writer, opcode, 1);
        if (opcode != 0x0a && opcode != 0x0b)
        {
            asm_cfi_leb(writer, event->first, false);
        }
        if (opcode == 9)
        {
            asm_cfi_leb(writer, event->second, false);
        }
    }
}

static void asm_cfi_pad(AsmCfiWriter* writer, uint32_t alignment)
{
    while (!writer->failed && writer->size % alignment != 0)
    {
        asm_cfi_integer(writer, 0, 1);
    }
}

static void asm_cfi_length(AsmCfiWriter* writer, uint32_t begin)
{
    if (!writer->failed)
    {
        const uint32_t length = writer->size - begin - 4;
        memcpy(writer->bytes + begin, &length, 4);
    }
}

static void asm_cfi_emit(AsmParser* parser)
{
    if (parser->procedure_count != 0)
    {
        uint64_t escape_bytes = 0;
        uint32_t initial_max = 0;
        for (uint32_t i = 0; i < parser->cfi_count; i += 1)
        {
            escape_bytes += parser->cfi[i].size;
        }
        for (uint32_t i = 0; i < parser->procedure_count; i += 1)
        {
            AsmCfiProcedure* procedure = &parser->procedures[i];
            const AsmFragment* start = &parser->fragments[parser->symbols[procedure->symbol].fragment];
            uint32_t count = 0;
            // Escapes and saved-state programs stay in the FDE even at PC zero.
            while (count < procedure->count && parser->cfi[procedure->first + count].opcode != BUSTER_ASM_CFI_ESCAPE &&
                   parser->cfi[procedure->first + count].opcode != 0x0a &&
                   parser->fragments[parser->cfi[procedure->first + count].fragment].offset == start->offset)
            {
                count += 1;
            }
            procedure->initial_count = count;
            initial_max = count > initial_max ? count : initial_max;
        }
        // Headers/alignment and bounded encodings per event, plus only the actual opaque escape payload. CIEs
        // never embed escapes, so scratch covers the largest understood initial program rather than all source.
        const uint64_t bound = (uint64_t)parser->cfi_count * 64 + (uint64_t)parser->procedure_count * 128 + escape_bytes;
        const uint64_t candidate_bound = (uint64_t)initial_max * 64 + 64;
        uint8_t* bytes = bound <= BUSTER_ASM_MAX_SECTION ? arena_push(parser->arena, bound, 8) : 0;
        Scratch scratch = scratch_begin(parser->arena);
        uint8_t* candidate_bytes = candidate_bound <= BUSTER_ASM_MAX_SECTION ? arena_push(scratch.arena, candidate_bound, 8) : 0;
        uint32_t slots = 16;
        while (slots < parser->procedure_count * 2)
        {
            slots *= 2;
        }
        uint32_t* cies = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, slots);
        AsmCfiWriter writer = {.bytes = bytes, .capacity = bound <= BUSTER_ASM_MAX_SECTION ? bound : 0};
        if (bytes == 0 || candidate_bytes == 0 || cies == 0 || parser->sections[BUSTER_LINK_CLASS_EH_FRAME].declared)
        {
            asm_fail(parser, 0, "CFI output capacity exhausted or explicit .eh_frame conflicts with CFI");
        }
        if (cies != 0)
        {
            memset(cies, 0, slots * sizeof(uint32_t));
        }
        for (uint32_t i = 0; parser->result.error == 0 && i < parser->procedure_count; i += 1)
        {
            AsmCfiProcedure* procedure = &parser->procedures[i];
            const AsmFragment* start = &parser->fragments[parser->symbols[procedure->symbol].fragment];
            const AsmFragment* finish = &parser->fragments[procedure->end];
            AsmCfiWriter candidate = {.bytes = candidate_bytes, .capacity = candidate_bound <= BUSTER_ASM_MAX_SECTION ? candidate_bound : 0};
            asm_cfi_integer(&candidate, 0, 4);
            asm_cfi_integer(&candidate, 0, 4);
            asm_cfi_integer(&candidate, 1, 1);
            asm_cfi_integer(&candidate, 'z', 1);
            asm_cfi_integer(&candidate, 'R', 1);
            if (procedure->signal)
            {
                asm_cfi_integer(&candidate, 'S', 1);
            }
            asm_cfi_integer(&candidate, 0, 1);
            asm_cfi_integer(&candidate, 1, 1); // code alignment
            asm_cfi_integer(&candidate, 0x78, 1); // SLEB128(-8), data alignment
            asm_cfi_integer(&candidate, procedure->return_column, 1);
            asm_cfi_integer(&candidate, 1, 1); // augmentation length
            asm_cfi_integer(&candidate, 0x1b, 1); // pcrel sdata4
            if (!procedure->simple)
            {
                static const uint8_t initial[] = {0x0c, 7, 8, 0x90, 1};
                for (uint32_t j = 0; j < sizeof(initial); j += 1)
                {
                    asm_cfi_integer(&candidate, initial[j], 1);
                }
            }
            const uint32_t first = procedure->initial_count;
            for (uint32_t initial = 0; initial < first; initial += 1)
            {
                asm_cfi_event(&candidate, &parser->cfi[procedure->first + initial]);
            }
            asm_cfi_pad(&candidate, 4);
            asm_cfi_length(&candidate, 0);
            uint32_t cie = UINT32_MAX;
            uint32_t slot = candidate.failed ? 0 : link_hash(candidate.bytes, candidate.size) & (slots - 1);
            while (!candidate.failed && cies[slot] != 0)
            {
                const uint32_t offset = cies[slot] - 1;
                uint32_t length;
                memcpy(&length, bytes + offset, 4);
                if (length + 4 == candidate.size && memcmp(bytes + offset, candidate.bytes, candidate.size) == 0)
                {
                    cie = offset;
                    break;
                }
                slot = (slot + 1) & (slots - 1);
            }
            if (cie == UINT32_MAX && !candidate.failed && candidate.size <= writer.capacity - writer.size)
            {
                cie = writer.size;
                cies[slot] = cie + 1;
                memcpy(bytes + writer.size, candidate.bytes, candidate.size);
                writer.size += candidate.size;
            }
            else if (cie == UINT32_MAX)
            {
                writer.failed = true;
            }
            const uint32_t fde = writer.size;
            asm_cfi_integer(&writer, 0, 4);
            asm_cfi_integer(&writer, fde + 4 - cie, 4);
            procedure->relocation = writer.size;
            asm_cfi_integer(&writer, 0, 4);
            asm_cfi_integer(&writer, finish->offset - start->offset, 4);
            asm_cfi_integer(&writer, 0, 1); // FDE augmentation length
            uint32_t previous = start->offset;
            for (uint32_t j = first; j < procedure->count; j += 1)
            {
                const AsmCfiEvent* event = &parser->cfi[procedure->first + j];
                const uint32_t offset = parser->fragments[event->fragment].offset;
                const uint32_t delta = offset - previous;
                if (offset < previous || offset > finish->offset)
                {
                    writer.failed = true;
                }
                else if (delta != 0)
                {
                    const uint32_t count = delta <= UINT8_MAX ? 1 : delta <= UINT16_MAX ? 2 : 4;
                    asm_cfi_integer(&writer, delta < 64 ? 0x40 | delta : count == 1 ? 2 : count == 2 ? 3 : 4, 1);
                    if (delta >= 64)
                    {
                        asm_cfi_integer(&writer, delta, count);
                    }
                }
                asm_cfi_event(&writer, event);
                previous = offset;
            }
            asm_cfi_pad(&writer, i + 1 == parser->procedure_count ? 8 : 4);
            asm_cfi_length(&writer, fde);
            if (writer.failed || candidate.failed)
            {
                asm_fail(parser, start->location, "CFI emission exceeds its bounded output or PC range");
            }
        }
        if (parser->result.error == 0)
        {
            const uint32_t previous = parser->section;
            parser->section = BUSTER_LINK_CLASS_EH_FRAME;
            const uint32_t id = asm_fragment(parser, ASM_BYTES, 0);
            AsmFragment* fragment = &parser->fragments[id];
            fragment->bytes = bytes;
            fragment->size = writer.size;
            parser->sections[parser->section].size = writer.size;
            parser->sections[parser->section].alignment = 3;
            asm_partition_append(parser, id);
            parser->section = previous;
        }
        scratch_end(scratch);
    }
}
