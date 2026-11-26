// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The assembly-output test's independent DWARF call-frame interpreter. GNU as chooses its own CIE sharing, padding
// and instruction spellings. Compare the rule in force at every PC interval, and the FDE's relocated range, before
// letting the ordinary object comparator omit those encoded bytes. An unsupported CFI program fails closed.

#define BUSTER_ASSEMBLY_CFI_REGISTERS 128
#define BUSTER_ASSEMBLY_CFI_STATES 16

typedef struct AssemblyCfiReader AssemblyCfiReader;
struct AssemblyCfiReader
{
    const uint8_t* bytes;
    uint64_t at;
    uint64_t end;
    bool valid;
};

static uint64_t assembly_cfi_integer(AssemblyCfiReader* reader, uint32_t width)
{
    uint64_t value = 0;
    reader->valid = reader->valid && width <= 8 && reader->at <= reader->end && width <= reader->end - reader->at;
    if (reader->valid)
    {
        for (uint32_t i = 0; i < width; i += 1)
        {
            value |= (uint64_t)reader->bytes[reader->at++] << (i * 8);
        }
    }
    return value;
}

static uint64_t assembly_cfi_leb(AssemblyCfiReader* reader, bool sign)
{
    uint64_t value = 0;
    uint32_t shift = 0;
    uint64_t byte = 0;
    do
    {
        byte = assembly_cfi_integer(reader, 1);
        reader->valid = reader->valid && shift < 64 && (shift != 63 || (sign ? (byte & 0x7f) == 0 || (byte & 0x7f) == 0x7f : (byte & 0x7f) <= 1));
        if (reader->valid)
        {
            value |= (byte & 0x7f) << shift;
            shift += 7;
        }
    } while (reader->valid && (byte & 0x80) != 0);
    if (reader->valid && sign && shift < 64 && (byte & 0x40) != 0)
    {
        value |= UINT64_MAX << shift;
    }
    return value;
}

// Rule kinds: undefined, same value, CFA offset, another register, CFA value offset.
typedef struct AssemblyCfiRow AssemblyCfiRow;
struct AssemblyCfiRow
{
    int64_t values[BUSTER_ASSEMBLY_CFI_REGISTERS];
    uint8_t kinds[BUSTER_ASSEMBLY_CFI_REGISTERS];
    int64_t cfa_offset;
    uint32_t cfa_register;
};

typedef struct AssemblyCfiCie AssemblyCfiCie;
struct AssemblyCfiCie
{
    AssemblyCfiRow initial;
    int64_t data_alignment;
    uint64_t code_alignment;
    uint32_t return_register;
};

typedef struct AssemblyCfiProgram AssemblyCfiProgram;
struct AssemblyCfiProgram
{
    AssemblyCfiReader reader;
    const AssemblyCfiCie* cie;
    AssemblyCfiRow row;
    AssemblyCfiRow saved[BUSTER_ASSEMBLY_CFI_STATES];
    uint32_t depth;
    uint64_t pc;
    uint64_t next;
    uint64_t size;
};

static int64_t assembly_cfi_scaled(AssemblyCfiProgram* program, uint64_t operand, bool sign)
{
    const __int128 value = sign ? (__int128)(int64_t)operand : (__int128)operand;
    const __int128 scaled = value * program->cie->data_alignment;
    program->reader.valid = program->reader.valid && scaled >= INT64_MIN && scaled <= INT64_MAX;
    return (int64_t)scaled;
}

// Consume all changes at the current PC, stopping at the next advance. Different legal encodings, repeated rules,
// zero advances and padding do not create artificial differences in the decoded states.
static bool assembly_cfi_next(AssemblyCfiProgram* program)
{
    AssemblyCfiReader* reader = &program->reader;
    bool advanced = false;
    program->next = program->size;
    while (reader->valid && !advanced && reader->at < reader->end)
    {
        const uint32_t opcode = (uint32_t)assembly_cfi_integer(reader, 1);
        uint64_t reg = 0;
        int64_t value = 0;
        uint8_t kind = 0;
        bool rule = false;
        if ((opcode & 0xc0) == 0x40 || opcode == 2 || opcode == 3 || opcode == 4)
        {
            const uint64_t delta = (opcode & 0xc0) == 0x40 ? opcode & 63 : assembly_cfi_integer(reader, 1u << (opcode - 2));
            reader->valid = reader->valid && program->pc <= program->size && delta <= (program->size - program->pc) / program->cie->code_alignment;
            if (reader->valid && delta != 0)
            {
                program->next = program->pc + delta * program->cie->code_alignment;
                advanced = true;
            }
        }
        else if ((opcode & 0xc0) == 0x80 || opcode == 5 || opcode == 0x11 || opcode == 0x14 || opcode == 0x15)
        {
            reg = (opcode & 0xc0) == 0x80 ? opcode & 63 : assembly_cfi_leb(reader, false);
            const bool sign = opcode == 0x11 || opcode == 0x15;
            const uint64_t operand = assembly_cfi_leb(reader, sign);
            value = assembly_cfi_scaled(program, operand, sign);
            kind = opcode == 0x14 || opcode == 0x15 ? 4 : 2;
            rule = true;
        }
        else if ((opcode & 0xc0) == 0xc0 || opcode == 6)
        {
            reg = (opcode & 0xc0) == 0xc0 ? opcode & 63 : assembly_cfi_leb(reader, false);
            reader->valid = reader->valid && reg < BUSTER_ASSEMBLY_CFI_REGISTERS;
            if (reader->valid)
            {
                kind = program->cie->initial.kinds[reg];
                value = program->cie->initial.values[reg];
                rule = true;
            }
        }
        else if (opcode == 7 || opcode == 8 || opcode == 9)
        {
            reg = assembly_cfi_leb(reader, false);
            value = opcode == 9 ? (int64_t)assembly_cfi_leb(reader, false) : 0;
            kind = opcode == 7 ? 0 : opcode == 8 ? 1 : 3;
            reader->valid = reader->valid && (opcode != 9 || (value >= 0 && value < BUSTER_ASSEMBLY_CFI_REGISTERS));
            rule = true;
        }
        else if (opcode == 0x0a)
        {
            reader->valid = reader->valid && program->depth < BUSTER_ASSEMBLY_CFI_STATES;
            if (reader->valid)
            {
                program->saved[program->depth++] = program->row;
            }
        }
        else if (opcode == 0x0b)
        {
            reader->valid = reader->valid && program->depth != 0;
            if (reader->valid)
            {
                program->row = program->saved[--program->depth];
            }
        }
        else if (opcode == 0x0c || opcode == 0x0d || opcode == 0x0e || opcode == 0x12 || opcode == 0x13)
        {
            if (opcode == 0x0c || opcode == 0x0d || opcode == 0x12)
            {
                reg = assembly_cfi_leb(reader, false);
                reader->valid = reader->valid && reg < BUSTER_ASSEMBLY_CFI_REGISTERS;
                program->row.cfa_register = (uint32_t)reg;
            }
            if (opcode != 0x0d)
            {
                const bool sign = opcode == 0x12 || opcode == 0x13;
                const uint64_t operand = assembly_cfi_leb(reader, sign);
                value = sign ? assembly_cfi_scaled(program, operand, true) : (int64_t)operand;
                reader->valid = reader->valid && (sign || operand <= INT64_MAX);
                program->row.cfa_offset = value;
            }
        }
        else
        {
            reader->valid = reader->valid && opcode == 0;
        }
        if (rule)
        {
            reader->valid = reader->valid && reg < BUSTER_ASSEMBLY_CFI_REGISTERS;
            if (reader->valid)
            {
                program->row.kinds[reg] = kind;
                program->row.values[reg] = value;
            }
        }
    }
    return reader->valid;
}

static bool assembly_cfi_cie(const uint8_t* bytes, uint64_t size, uint64_t offset, AssemblyCfiCie* cie)
{
    AssemblyCfiReader reader = {
        .bytes = bytes,
        .at = offset,
        .end = size,
        .valid = true,
    };
    const uint64_t length = assembly_cfi_integer(&reader, 4);
    reader.valid = reader.valid && length >= 13 && length <= size - reader.at;
    reader.end = reader.valid ? reader.at + length : reader.at;
    const uint64_t id = assembly_cfi_integer(&reader, 4);
    const uint64_t version = assembly_cfi_integer(&reader, 1);
    const uint64_t augmentation = assembly_cfi_integer(&reader, 3);
    *cie = (AssemblyCfiCie){ 0 };
    cie->code_alignment = assembly_cfi_leb(&reader, false);
    cie->data_alignment = (int64_t)assembly_cfi_leb(&reader, true);
    const uint64_t return_register = version == 1 ? assembly_cfi_integer(&reader, 1) : assembly_cfi_leb(&reader, false);
    cie->return_register = (uint32_t)return_register;
    const uint64_t augmentation_length = assembly_cfi_leb(&reader, false);
    const uint64_t encoding = assembly_cfi_integer(&reader, 1);
    reader.valid = reader.valid && id == 0 && (version == 1 || version == 3) && augmentation == ('z' | ('R' << 8)) &&
                   cie->code_alignment != 0 && return_register < BUSTER_ASSEMBLY_CFI_REGISTERS && augmentation_length == 1 && encoding == 0x1b;
    AssemblyCfiProgram program = {
        .reader = reader,
        .cie = cie,
    };
    const bool valid = reader.valid && assembly_cfi_next(&program) && program.reader.at == program.reader.end && program.depth == 0;
    if (valid)
    {
        cie->initial = program.row;
    }
    return valid;
}

typedef struct AssemblyCfiFrame AssemblyCfiFrame;
struct AssemblyCfiFrame
{
    ObjectTestTarget target;
    const uint8_t* bytes;
    uint64_t section_size;
    uint64_t cie;
    uint64_t program;
    uint64_t end;
    uint64_t size;
};

static bool assembly_cfi_frames(const ObjectView* view, uint64_t index, Arena* arena, AssemblyCfiFrame** output, uint64_t* count)
{
    const Elf64_Shdr* section = &view->sections[index];
    const uint8_t* bytes = view->file + section->sh_offset;
    const uint64_t maximum = section->sh_size / 17;
    AssemblyCfiFrame* frames = BUSTER_ARENA_PUSH_ARRAY(arena, AssemblyCfiFrame, maximum);
    uint64_t rela_index = 0;
    for (uint64_t i = 1; i < view->section_count; i += 1)
    {
        rela_index = view->sections[i].sh_type == SHT_RELA && view->sections[i].sh_info == index ? i : rela_index;
    }
    const Elf64_Shdr* relocation_section = rela_index != 0 ? &view->sections[rela_index] : NULL;
    const Elf64_Rela* relocations = relocation_section != NULL ? (const Elf64_Rela*)(view->file + relocation_section->sh_offset) : NULL;
    const uint64_t relocation_count = relocation_section != NULL ? relocation_section->sh_size / sizeof(*relocations) : 0;
    AssemblyCfiReader reader = {
        .bytes = bytes,
        .end = section->sh_size,
        .valid = maximum == 0 || frames != NULL,
    };
    uint64_t used = 0;
    uint64_t found = 0;
    while (reader.valid && reader.at < reader.end)
    {
        const uint64_t start = reader.at;
        const uint64_t length = assembly_cfi_integer(&reader, 4);
        reader.valid = reader.valid && length != UINT32_MAX && (length == 0 || length >= 4) && length <= reader.end - reader.at;
        const uint64_t end = reader.at + length;
        if (reader.valid && length == 0)
        {
            for (; reader.valid && reader.at < reader.end; reader.at += 1)
            {
                reader.valid = bytes[reader.at] == 0;
            }
        }
        else if (reader.valid)
        {
            const uint64_t id = assembly_cfi_integer(&reader, 4);
            if (id != 0)
            {
                const uint64_t initial = reader.at;
                const uint64_t location = assembly_cfi_integer(&reader, 4);
                const uint64_t range = assembly_cfi_integer(&reader, 4);
                const uint64_t augmentation = assembly_cfi_leb(&reader, false);
                reader.valid = reader.valid && length >= 13 && id <= start + 4 && reader.at <= end && augmentation == 0 && location == 0 && found < maximum;
                const Elf64_Rela* relocation = used < relocation_count ? &relocations[used] : NULL;
                reader.valid = reader.valid && relocation != NULL && relocation->r_offset == initial && ELF64_R_TYPE(relocation->r_info) == R_X86_64_PC32;
                if (reader.valid)
                {
                    frames[found++] = (AssemblyCfiFrame){
                        .target = object_test_target(view, relocation),
                        .bytes = bytes,
                        .section_size = section->sh_size,
                        .cie = start + 4 - id,
                        .program = reader.at,
                        .end = end,
                        .size = range,
                    };
                    used += 1;
                }
            }
            reader.at = end;
        }
    }
    *output = frames;
    *count = found;
    return reader.valid && used == relocation_count;
}

static int assembly_cfi_frame_order(const void* left, const void* right)
{
    const AssemblyCfiFrame* a = left;
    const AssemblyCfiFrame* b = right;
    const ObjectTestTarget x = a->target;
    const ObjectTestTarget y = b->target;
    int order = strcmp(x.section != NULL ? x.section : "", y.section != NULL ? y.section : "");
    order = order == 0 ? strcmp(x.group != NULL ? x.group : "", y.group != NULL ? y.group : "") : order;
    order = order == 0 && x.section == NULL ? strcmp(x.name != NULL ? x.name : "", y.name != NULL ? y.name : "") : order;
    const uint64_t first = x.value + (uint64_t)x.addend;
    const uint64_t second = y.value + (uint64_t)y.addend;
    order = order == 0 ? (first > second) - (first < second) : order;
    order = order == 0 ? (a->size > b->size) - (a->size < b->size) : order;
    return order;
}

static bool assembly_cfi_same_frame(const AssemblyCfiFrame* a, const AssemblyCfiFrame* b)
{
    AssemblyCfiCie first = { 0 };
    AssemblyCfiCie second = { 0 };
    bool valid = a->size == b->size && object_test_same_target(R_X86_64_PC32, a->target, b->target) &&
                 assembly_cfi_cie(a->bytes, a->section_size, a->cie, &first) && assembly_cfi_cie(b->bytes, b->section_size, b->cie, &second);
    AssemblyCfiProgram x = {
        .reader = { .bytes = a->bytes, .at = a->program, .end = a->end, .valid = valid },
        .cie = &first,
        .size = a->size,
    };
    AssemblyCfiProgram y = {
        .reader = { .bytes = b->bytes, .at = b->program, .end = b->end, .valid = valid },
        .cie = &second,
        .size = b->size,
    };
    if (valid)
    {
        x.row = first.initial;
        y.row = second.initial;
        valid = first.return_register == second.return_register && assembly_cfi_next(&x) && assembly_cfi_next(&y);
    }
    uint64_t pc = 0;
    while (valid)
    {
        valid = x.row.cfa_register == y.row.cfa_register && x.row.cfa_offset == y.row.cfa_offset &&
                memcmp(x.row.values, y.row.values, sizeof(x.row.values)) == 0 && memcmp(x.row.kinds, y.row.kinds, sizeof(x.row.kinds)) == 0;
        const uint64_t next = x.next < y.next ? x.next : y.next;
        if (!valid || next == a->size)
        {
            break;
        }
        valid = next > pc;
        pc = next;
        if (valid && x.next == pc)
        {
            x.pc = pc;
            valid = assembly_cfi_next(&x);
        }
        if (valid && y.next == pc)
        {
            y.pc = pc;
            valid = assembly_cfi_next(&y);
        }
    }
    if (valid && x.reader.at != x.reader.end)
    {
        x.pc = x.size;
        valid = assembly_cfi_next(&x) && x.reader.at == x.reader.end;
    }
    if (valid && y.reader.at != y.reader.end)
    {
        y.pc = y.size;
        valid = assembly_cfi_next(&y) && y.reader.at == y.reader.end;
    }
    return valid;
}

// Independent encodings of the same six-byte function: one CIE owns the callee-saved baseline and restores RBP;
// the other puts those initial rules in the FDE and spells its CFA transitions and restores differently.
static void assembly_test_cfi_rules(AssemblyTest* test)
{
    static const uint8_t first_cie[] = {
        36, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1,
        0x1b, 0x0c, 7, 8, 0x90, 1, 0x08, 3, 0x08, 6, 0x08, 12, 0x08, 13, 0x08, 14,
        0x08, 15, 0, 0, 0, 0, 0, 0,
    };
    static const uint8_t second_cie[] = {
        20, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1,
        0x1b, 0x0c, 7, 8, 0x90, 1, 0, 0,
    };
    static const uint8_t first_program[] = {
        0x41, 0x0e, 16, 0x86, 2, 0x43, 0x0d, 6, 0x41, 0x0c, 7, 8, 0xc6,
    };
    static const uint8_t second_program[] = {
        0x08, 3, 0x08, 6, 0x08, 12, 0x08, 13, 0x08, 14, 0x08, 15,
        0x02, 1, 0x0c, 7, 16, 0x05, 6, 2, 0x03, 3, 0, 0x0c, 6, 16,
        0x02, 1, 0x0c, 7, 8, 0x08, 6,
    };
    uint8_t first[sizeof(first_cie) + sizeof(first_program)];
    uint8_t second[sizeof(second_cie) + sizeof(second_program)];
    memcpy(first, first_cie, sizeof(first_cie));
    memcpy(first + sizeof(first_cie), first_program, sizeof(first_program));
    memcpy(second, second_cie, sizeof(second_cie));
    memcpy(second + sizeof(second_cie), second_program, sizeof(second_program));
    const AssemblyCfiFrame a = {
        .target = { .section = ".text", .value = 7 },
        .bytes = first,
        .section_size = sizeof(first),
        .program = sizeof(first_cie),
        .end = sizeof(first),
        .size = 6,
    };
    const AssemblyCfiFrame b = {
        .target = { .section = ".text", .value = 7 },
        .bytes = second,
        .section_size = sizeof(second),
        .program = sizeof(second_cie),
        .end = sizeof(second),
        .size = 6,
    };
    assembly_test_check(test, assembly_cfi_same_frame(&a, &b), "CFI comparison accepts equivalent encodings and CIE baselines", NULL);
    static const uint32_t mutations[] = { 14, 21, sizeof(second_cie) + 13, sizeof(second_cie) + 24, sizeof(second) - 1 };
    for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(mutations); m += 1)
    {
        const uint32_t at = mutations[m];
        second[at] ^= 1;
        assembly_test_check(test, !assembly_cfi_same_frame(&a, &b), "CFI comparison rejects changed return, CFA, saved-register and boundary rules", NULL);
        second[at] ^= 1;
    }
    AssemblyCfiFrame changed = b;
    changed.size += 1;
    assembly_test_check(test, !assembly_cfi_same_frame(&a, &changed), "CFI comparison rejects a changed range", NULL);
    changed = b;
    changed.target.value += 1;
    assembly_test_check(test, !assembly_cfi_same_frame(&a, &changed), "CFI comparison rejects a changed target", NULL);
    second[sizeof(second_cie)] = 0x0f;
    assembly_test_check(test, !assembly_cfi_same_frame(&a, &b), "CFI comparison refuses an unsupported expression", NULL);
}

static void assembly_test_cfi_boundaries(AssemblyTest* test)
{
    const uint64_t mark = test->arena.length;
    const uint64_t output_mark = test->output.length;
    const uint64_t scratch_mark = test->scratch.length;
    AssemblyFixture fixture;
    assembly_fixture_begin(&fixture, &test->arena);
    const AtomId atom = assembly_fixture_function(test, &fixture, "framed", BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_FUNCTION, 0,
                                                 "push rbp\nmov rbp,rsp\npop rbp\nret\n");
    EmissionFrameRow rows[] = {
        { .offset = 1, .kind = EMISSION_FRAME_CFA_OFFSET, .value = 16 },
        { .offset = 1, .kind = EMISSION_FRAME_OFFSET, .reg = 6, .value = -16 },
        { .offset = 4, .kind = EMISSION_FRAME_CFA, .reg = 6, .value = 16 },
        { .offset = 5, .kind = EMISSION_FRAME_CFA, .reg = 7, .value = 8 },
        { .offset = 5, .kind = EMISSION_FRAME_RESTORE, .reg = 6 },
    };
    AssemblyView view = assembly_fixture_view(&fixture);
    AssemblyCode* code = &fixture.code[atom];
    code->frame_enabled = true;
    code->frame_size = 6;
    code->frame_rows = rows;
    code->frame_count = BUSTER_ARRAY_LENGTH(rows);
    const AssemblyResult printed = fixture.valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    assembly_test_check(test, printed.error == ASSEMBLY_OK, "CFI rows print at their instruction boundaries", printed.reason);
    if (printed.error == ASSEMBLY_OK)
    {
        const AsmResult assembled = asm_assemble((String8){ .pointer = (char*)printed.bytes, .length = printed.length }, test->index,
                                                (AsmOptions){ .integrated = true }, &test->arena);
        assembly_test_check(test, assembled.error == NULL, "the project assembler reads the generated CFI directives", assembled.error);
    }
    arena_pop_to(&test->output, output_mark);
    rows[0].value = INT64_C(1) << 32;
    rows[1].value = -(INT64_C(1) << 33);
    const AssemblyResult wide = fixture.valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    const AsmResult wide_assembled = wide.error == ASSEMBLY_OK ?
                                    asm_assemble((String8){ .pointer = (char*)wide.bytes, .length = wide.length }, test->index,
                                                 (AsmOptions){ .integrated = true }, &test->arena) : (AsmResult){ .error = "large CFI row did not print" };
    assembly_test_check(test, wide.error == ASSEMBLY_OK && wide_assembled.error == NULL, "wide CFI offsets retain their full values with escape operands", wide_assembled.error);
    rows[0].value = 16;
    rows[1].value = -16;
    arena_pop_to(&test->output, output_mark);
    for (uint32_t mutation = 0; fixture.valid && mutation < 4; mutation += 1)
    {
        code->frame_offset = mutation == 1 ? 2 : 0;
        code->frame_size = mutation == 1 ? 4 : mutation == 2 ? 2 : 6;
        code->frame_count = mutation == 1 || mutation == 2 ? 0 : BUSTER_ARRAY_LENGTH(rows);
        code->frame_rows = mutation == 3 ? NULL : rows;
        rows[0].offset = rows[1].offset = mutation == 0 ? 2 : 1;
        const AssemblyResult refused = assembly_print(&view, &test->output, &test->scratch);
        assembly_test_check(test, refused.error == ASSEMBLY_INVALID && test->output.length == output_mark && test->scratch.length == scratch_mark,
                            "invalid frame rows and boundaries fail transactionally", refused.reason);
    }
    code->frame_offset = 0;
    code->frame_size = 6;
    code->frame_rows = rows;
    code->frame_count = BUSTER_ARRAY_LENGTH(rows);
    rows[0].offset = rows[1].offset = 1;
    static const uint8_t table[] = {
        36, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1,
        0x1b, 0x0c, 7, 8, 0x90, 1, 0x08, 3, 0x08, 6, 0x08, 12, 0x08, 13, 0x08, 14,
        0x08, 15, 0, 0, 0, 0, 0, 0,
        28, 0, 0, 0, 44, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0,
        0x41, 0x0e, 16, 0x86, 2, 0x43, 0x0d, 6, 0x41, 0x0c, 7, 8, 0xc6, 0, 0,
    };
    const AtomId unwind = assembly_fixture_data(test, &fixture, BUSTER_LINK_CLASS_EH_FRAME, 3, table, sizeof(table));
    assembly_fixture_fixup(&fixture, unwind, 48, BUSTER_LINK_FIXUP_PC32, "framed", 0);
    view.cfi_atom = unwind;
    const AssemblyResult replaced = fixture.valid ? assembly_print(&view, &test->output, &test->scratch) : (AssemblyResult){ .error = ASSEMBLY_INVALID };
    assembly_test_check(test, replaced.error == ASSEMBLY_OK, "a generated unwind table is covered by its published frame", replaced.reason);
    arena_pop_to(&test->output, output_mark);
    for (uint32_t mutation = 0; fixture.valid && mutation < 5; mutation += 1)
    {
        view.cfi_atom = mutation == 0 ? atom : mutation == 1 ? fixture.link.atoms.count : unwind;
        fixture.link.atoms.unit[unwind] = mutation == 2 ? 1 : 0;
        code->frame_enabled = mutation != 3;
        code->frame_size = mutation == 4 ? 5 : 6;
        code->frame_count = mutation == 4 ? 0 : BUSTER_ARRAY_LENGTH(rows);
        const AssemblyResult refused = assembly_print(&view, &test->output, &test->scratch);
        assembly_test_check(test, refused.error == ASSEMBLY_INVALID && test->output.length == output_mark && test->scratch.length == scratch_mark,
                            "an uncovered or invalid replacement unwind table fails transactionally", refused.reason);
    }
    assembly_fixture_end(&fixture);
    arena_pop_to(&test->arena, mark);
}

// Successful semantic comparison replaces the sections' view types only, so the existing generic comparator still
// checks every ordinary code/data byte, symbol and relocation. These views are private scratch copies in the caller.
static bool assembly_cfi_normalize(ObjectView* ours, ObjectView* theirs, Arena* arena, char* error, uint64_t error_size)
{
    const uint64_t a = object_test_section_named(ours, ".eh_frame", false);
    const uint64_t b = object_test_section_named(theirs, ".eh_frame", false);
    bool valid = (a == 0) == (b == 0);
    const Elf64_Shdr* first = a != 0 ? &ours->sections[a] : NULL;
    const Elf64_Shdr* second = b != 0 ? &theirs->sections[b] : NULL;
    const bool different = valid && first != NULL && (first->sh_size != second->sh_size || memcmp(ours->file + first->sh_offset, theirs->file + second->sh_offset, first->sh_size) != 0);
    if (different)
    {
        AssemblyCfiFrame* x = NULL;
        AssemblyCfiFrame* y = NULL;
        uint64_t x_count = 0;
        uint64_t y_count = 0;
        valid = object_test_same_section_identity(ours, a, theirs, b) && first->sh_flags == second->sh_flags &&
                first->sh_addralign == second->sh_addralign && first->sh_entsize == second->sh_entsize &&
                assembly_cfi_frames(ours, a, arena, &x, &x_count) && assembly_cfi_frames(theirs, b, arena, &y, &y_count) && x_count == y_count;
        if (valid)
        {
            qsort(x, x_count, sizeof(*x), assembly_cfi_frame_order);
            qsort(y, y_count, sizeof(*y), assembly_cfi_frame_order);
            for (uint64_t i = 0; valid && i < x_count; i += 1)
            {
                valid = assembly_cfi_same_frame(&x[i], &y[i]);
            }
        }
        ObjectView* views[2] = { ours, theirs };
        const uint64_t indices[2] = { a, b };
        for (uint32_t v = 0; valid && v < 2; v += 1)
        {
            ObjectView* view = views[v];
            Elf64_Shdr* sections = (Elf64_Shdr*)view->sections;
            for (uint64_t s = 1; s < view->section_count; s += 1)
            {
                if (s == indices[v] || (sections[s].sh_type == SHT_RELA && sections[s].sh_info == indices[v]))
                {
                    sections[s].sh_type = SHT_STRTAB;
                }
            }
        }
    }
    if (!valid)
    {
        snprintf(error, error_size, ".eh_frame ranges or decoded unwind rules differ (or the program is unsupported)");
    }
    return valid;
}
