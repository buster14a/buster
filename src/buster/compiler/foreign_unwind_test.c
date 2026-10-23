// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original record-level fixtures for foreign_unwind.c. The expected retained records are specified independently
// of the pruner, including CIE personality and FDE LSDA relocations which must not decide an FDE's ownership.
// Both paths must preserve the inputs, produce the expected compact bytes and every offset mapping, and leave
// output and the caller's arena unchanged on refusal. Include after foreign_test.c's tally helpers.

#define BUSTER_FOREIGN_UNWIND_TEST_BYTES 4096
#define BUSTER_FOREIGN_UNWIND_TEST_RECORDS 128
#define BUSTER_FOREIGN_UNWIND_TEST_RELOCATIONS 256

typedef struct ForeignUnwindTestRecord ForeignUnwindTestRecord;
struct ForeignUnwindTestRecord
{
    uint32_t offset;
    uint32_t length;
    uint32_t kind;
    uint32_t cie;
    uint32_t section;
    AtomId atom;
    bool kept;
};

typedef struct ForeignUnwindTestInput ForeignUnwindTestInput;
struct ForeignUnwindTestInput
{
    uint8_t bytes[BUSTER_FOREIGN_UNWIND_TEST_BYTES];
    Elf64_Rela relocations[BUSTER_FOREIGN_UNWIND_TEST_RELOCATIONS];
    ForeignUnwindTestRecord records[BUSTER_FOREIGN_UNWIND_TEST_RECORDS];
    Elf64_Sym symbols[8];
    uint32_t extended[8];
    AtomId sections[8];
    uint32_t length;
    uint32_t relocation_count;
    uint32_t record_count;
};

static void foreign_unwind_test_begin(ForeignUnwindTestInput* fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->sections[1] = 101;
    fixture->sections[3] = 103;
    fixture->sections[4] = 104;
    for (uint32_t i = 1; i < BUSTER_ARRAY_LENGTH(fixture->symbols); i += 1)
    {
        fixture->symbols[i].st_info = ELF64_ST_INFO(i == 2 ? STB_WEAK : STB_LOCAL, i <= 2 ? STT_FUNC : STT_OBJECT);
        fixture->symbols[i].st_shndx = (uint16_t)i;
    }
    // The discarded weak function still belongs to section 2 even when the adapter later redirects its name.
    fixture->symbols[2].st_value = 9;
    fixture->symbols[3].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
    fixture->symbols[5].st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION);
    fixture->symbols[5].st_shndx = 2;
    fixture->symbols[6].st_info = ELF64_ST_INFO(STB_LOCAL, STT_FUNC);
    fixture->symbols[6].st_shndx = SHN_XINDEX;
    fixture->extended[6] = 1;
    fixture->symbols[7].st_info = ELF64_ST_INFO(STB_LOCAL, STT_FUNC);
    fixture->symbols[7].st_shndx = SHN_XINDEX;
    fixture->extended[7] = 2;
}

static void foreign_unwind_test_relocate(ForeignUnwindTestInput* fixture, uint32_t offset, uint32_t symbol, uint32_t type, int64_t addend)
{
    fixture->relocations[fixture->relocation_count] = (Elf64_Rela){
        .r_offset = offset,
        .r_info = ELF64_R_INFO(symbol, type),
        .r_addend = addend,
    };
    fixture->relocation_count += 1;
}

static uint32_t foreign_unwind_test_cie(ForeignUnwindTestInput* fixture, bool personality, bool kept)
{
    // zR: code alignment 1, data alignment -8, return register 16, pc-relative sdata4 initial locations.
    static const uint8_t plain[] = {
        20, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1,
        0x1b, 0x0c, 7, 8, 0x90, 1, 0, 0,
    };
    // zPLR: indirect pc-relative personality, pc-relative LSDA, and the same initial-location encoding.
    static const uint8_t augmented[] = {
        28, 0, 0, 0, 0, 0, 0, 0, 1, 'z', 'P', 'L', 'R', 0, 1, 0x78,
        16, 7, 0x9b, 0, 0, 0, 0, 0x1b, 0x1b, 0x0c, 7, 8, 0x90, 1, 0, 0,
    };
    const uint32_t record = fixture->record_count;
    const uint32_t offset = fixture->length;
    const uint32_t length = personality ? sizeof(augmented) : sizeof(plain);
    memcpy(fixture->bytes + offset, personality ? augmented : plain, length);
    fixture->records[record] = (ForeignUnwindTestRecord){
        .offset = offset,
        .length = length,
        .kind = FOREIGN_UNWIND_CIE,
        .cie = UINT32_MAX,
        .kept = kept,
    };
    if (personality)
    {
        foreign_unwind_test_relocate(fixture, offset + 19, 3, R_X86_64_PC32, -7);
    }
    fixture->length += length;
    fixture->record_count += 1;
    return record;
}

static uint32_t foreign_unwind_test_fde(ForeignUnwindTestInput* fixture, uint32_t cie, uint32_t symbol, bool lsda, bool kept, uint32_t section, AtomId atom)
{
    const uint32_t record = fixture->record_count;
    const uint32_t offset = fixture->length;
    const uint32_t length = lsda ? 28 : 24;
    const uint32_t stored = length - 4;
    const uint32_t pointer = offset + 4 - fixture->records[cie].offset;
    const uint32_t range = 19;
    memcpy(fixture->bytes + offset, &stored, sizeof(stored));
    memcpy(fixture->bytes + offset + 4, &pointer, sizeof(pointer));
    memcpy(fixture->bytes + offset + 12, &range, sizeof(range));
    fixture->bytes[offset + 16] = lsda ? 4 : 0;
    const uint32_t instructions = offset + (lsda ? 21 : 17);
    fixture->bytes[instructions] = 0x41;
    fixture->bytes[instructions + 1] = 0x0e;
    fixture->bytes[instructions + 2] = 0x10;
    foreign_unwind_test_relocate(fixture, offset + 8, symbol, R_X86_64_PC32, 3);
    if (lsda)
    {
        foreign_unwind_test_relocate(fixture, offset + 17, 4, R_X86_64_PC32, 11);
    }
    fixture->records[record] = (ForeignUnwindTestRecord){
        .offset = offset,
        .length = length,
        .kind = FOREIGN_UNWIND_FDE,
        .cie = cie,
        .section = section,
        .atom = atom,
        .kept = kept,
    };
    fixture->length += length;
    fixture->record_count += 1;
    return record;
}

static ForeignUnwindInput foreign_unwind_test_input(const ForeignUnwindTestInput* fixture, ImportSymbols* symbols)
{
    *symbols = (ImportSymbols){
        .section = 7,
        .entries = {
            .pointer = (char*)fixture->symbols,
            .length = sizeof(fixture->symbols),
        },
        .count = BUSTER_ARRAY_LENGTH(fixture->symbols),
        .extended = {
            .pointer = (char*)fixture->extended,
            .length = sizeof(fixture->extended),
        },
    };
    const ForeignUnwindInput input = {
        .bytes = {
            .pointer = (char*)fixture->bytes,
            .length = fixture->length,
        },
        .relocations = fixture->relocations,
        .relocation_count = fixture->relocation_count,
        .symbols = symbols,
        .sections = fixture->sections,
        .section_count = BUSTER_ARRAY_LENGTH(fixture->sections),
    };
    return input;
}

static void foreign_unwind_test_terminator(ForeignUnwindTestInput* fixture, bool kept)
{
    fixture->records[fixture->record_count] = (ForeignUnwindTestRecord){
        .offset = fixture->length,
        .length = 4,
        .kind = FOREIGN_UNWIND_TERMINATOR,
        .cie = UINT32_MAX,
        .kept = kept,
    };
    fixture->length += 4;
    fixture->record_count += 1;
}

static bool foreign_unwind_test_same_relocation(const Elf64_Rela* left, const Elf64_Rela* right)
{
    return left->r_offset == right->r_offset && left->r_info == right->r_info && left->r_addend == right->r_addend;
}

static void foreign_unwind_test_kept(ForeignTestTally* tally, const ForeignUnwindTestInput* fixture, Arena* arena, const char* detail)
{
    uint8_t original[sizeof(*fixture)];
    memcpy(original, fixture, sizeof(original));
    uint8_t expected[BUSTER_FOREIGN_UNWIND_TEST_BYTES];
    uint64_t positions[BUSTER_FOREIGN_UNWIND_TEST_RECORDS];
    Elf64_Rela relocations[BUSTER_FOREIGN_UNWIND_TEST_RELOCATIONS];
    uint32_t length = 0;
    uint32_t count = 0;
    uint32_t fdes = 0;
    for (uint32_t r = 0; r < fixture->record_count; r += 1)
    {
        const ForeignUnwindTestRecord* record = &fixture->records[r];
        positions[r] = record->kept ? length : UINT64_MAX;
        if (record->kept)
        {
            memcpy(expected + length, fixture->bytes + record->offset, record->length);
            if (record->kind == FOREIGN_UNWIND_FDE)
            {
                const uint32_t pointer = (uint32_t)(length + 4 - positions[record->cie]);
                memcpy(expected + length + 4, &pointer, sizeof(pointer));
                fdes += 1;
            }
            length += record->length;
        }
    }
    for (uint32_t i = 0; i < fixture->relocation_count; i += 1)
    {
        const Elf64_Rela* relocation = &fixture->relocations[i];
        for (uint32_t r = 0; r < fixture->record_count; r += 1)
        {
            const ForeignUnwindTestRecord* record = &fixture->records[r];
            if (record->kept && relocation->r_offset >= record->offset && relocation->r_offset < record->offset + record->length)
            {
                relocations[count] = *relocation;
                relocations[count].r_offset = positions[r] + relocation->r_offset - record->offset;
                count += 1;
            }
        }
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const uint64_t saved = arena->length;
        ImportSymbols symbols;
        const ForeignUnwindInput input = foreign_unwind_test_input(fixture, &symbols);
        ForeignUnwind output = { 0 };
        const ForeignUnwindResult result = foreign_unwind_prune(&input, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, arena, &output);
        foreign_test_expect(tally, result.error == FOREIGN_UNWIND_ERROR_NONE, "prune accepted records", detail);
        foreign_test_expect(tally, memcmp(fixture, original, sizeof(original)) == 0, "pruning preserves all input storage", detail);
        if (result.error == FOREIGN_UNWIND_ERROR_NONE)
        {
            const bool bytes = output.bytes.length == length && (length == 0 || (output.bytes.pointer != 0 && output.bytes.pointer != input.bytes.pointer && memcmp(output.bytes.pointer, expected, length) == 0));
            foreign_test_expect(tally, bytes, "retained bytes and CIE displacements match the fixture", detail);
            foreign_test_expect(tally, output.old_length == input.bytes.length && output.fde_count == fdes && output.record_count == fixture->record_count,
                                "old extent, records and live FDE count", detail);
            bool records = output.record_count == fixture->record_count && (output.record_count == 0 || output.records != 0);
            for (uint32_t r = 0; records && r < fixture->record_count; r += 1)
            {
                const ForeignUnwindTestRecord* wanted = &fixture->records[r];
                const ForeignUnwindRecord* actual = &output.records[r];
                const uint64_t cie = wanted->cie != UINT32_MAX ? fixture->records[wanted->cie].offset : UINT64_MAX;
                records = actual->old_offset == wanted->offset && actual->new_offset == positions[r] && actual->length == wanted->length &&
                          actual->kind == wanted->kind && actual->cie_offset == cie &&
                          (wanted->kind != FOREIGN_UNWIND_FDE || (actual->section == wanted->section && actual->atom == wanted->atom));
            }
            foreign_test_expect(tally, records, "record map retains original ownership", detail);
            bool mapped = true;
            for (uint32_t r = 0; r < fixture->record_count; r += 1)
            {
                const ForeignUnwindTestRecord* record = &fixture->records[r];
                for (uint32_t byte = 0; byte < record->length; byte += 1)
                {
                    uint64_t translated = UINT64_MAX;
                    const bool found = foreign_unwind_offset(&output, record->offset + byte, &translated);
                    mapped = mapped && found == record->kept && (!found || translated == positions[r] + byte);
                }
            }
            uint64_t end = UINT64_MAX;
            uint64_t outside = 0;
            mapped = mapped && foreign_unwind_offset(&output, input.bytes.length, &end) && end == length &&
                     !foreign_unwind_offset(&output, input.bytes.length + 1, &outside) && !foreign_unwind_offset(&output, UINT64_MAX, &outside);
            foreign_test_expect(tally, mapped, "every retained byte and section end translate; removed bytes do not", detail);
            bool relocated = output.relocation_count == count && (count == 0 || output.relocations != 0);
            for (uint32_t i = 0; relocated && i < count; i += 1)
            {
                uint32_t matches = 0;
                for (uint32_t j = 0; j < output.relocation_count; j += 1)
                {
                    matches += foreign_unwind_test_same_relocation(&relocations[i], &output.relocations[j]) ? 1 : 0;
                }
                relocated = matches == 1;
            }
            foreign_test_expect(tally, relocated, "all live relocations move once with unchanged symbols and addends", detail);
        }
        arena_pop_to(arena, saved);
    }
}

static void foreign_unwind_test_successes(ForeignTestTally* tally, Arena* arena)
{
    ForeignUnwindTestInput fixture;
    foreign_unwind_test_begin(&fixture);
    uint32_t cie = foreign_unwind_test_cie(&fixture, false, true);
    foreign_unwind_test_fde(&fixture, cie, 1, false, true, 1, 101);
    foreign_unwind_test_fde(&fixture, cie, 1, false, true, 1, 101);
    foreign_unwind_test_kept(tally, &fixture, arena, "all-live zR");

    foreign_unwind_test_begin(&fixture);
    cie = foreign_unwind_test_cie(&fixture, true, true);
    foreign_unwind_test_fde(&fixture, cie, 2, true, false, 2, 0);
    foreign_unwind_test_fde(&fixture, cie, 1, true, true, 1, 101);
    foreign_unwind_test_fde(&fixture, cie, 5, true, false, 2, 0);
    foreign_unwind_test_fde(&fixture, cie, 1, true, true, 1, 101);
    foreign_unwind_test_kept(tally, &fixture, arena, "shared personality; first and middle FDE removed");
    // CIE personality and FDE LSDA targets do not determine function ownership, even when their input section maps
    // to no atom. The adapter may resolve a global target elsewhere or diagnose a dangling local after pruning.
    fixture.sections[3] = 0;
    fixture.sections[4] = 0;
    fixture.symbols[4].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
    foreign_unwind_test_kept(tally, &fixture, arena, "personality and LSDA are not pc_begin");
    for (uint32_t i = 0; i < fixture.relocation_count / 2; i += 1)
    {
        const uint32_t other = fixture.relocation_count - 1 - i;
        const Elf64_Rela temporary = fixture.relocations[i];
        fixture.relocations[i] = fixture.relocations[other];
        fixture.relocations[other] = temporary;
    }
    foreign_unwind_test_kept(tally, &fixture, arena, "unsorted personality, initial-location and LSDA relocations");

    foreign_unwind_test_begin(&fixture);
    cie = foreign_unwind_test_cie(&fixture, true, false);
    foreign_unwind_test_fde(&fixture, cie, 2, true, false, 2, 0);
    cie = foreign_unwind_test_cie(&fixture, false, true);
    foreign_unwind_test_fde(&fixture, cie, 6, false, true, 1, 101);
    foreign_unwind_test_fde(&fixture, cie, 7, false, false, 2, 0);
    foreign_unwind_test_kept(tally, &fixture, arena, "unused personality CIE disappears; extended section indices");

    foreign_unwind_test_begin(&fixture);
    cie = foreign_unwind_test_cie(&fixture, true, false);
    foreign_unwind_test_fde(&fixture, cie, 2, true, false, 2, 0);
    foreign_unwind_test_kept(tally, &fixture, arena, "all-dead records produce an empty fragment");
    foreign_unwind_test_terminator(&fixture, false);
    foreign_unwind_test_kept(tally, &fixture, arena, "all-dead records do not leave an early terminator");

    foreign_unwind_test_begin(&fixture);
    foreign_unwind_test_terminator(&fixture, true);
    foreign_unwind_test_kept(tally, &fixture, arena, "standalone original terminator survives");

    foreign_unwind_test_begin(&fixture);
    cie = foreign_unwind_test_cie(&fixture, false, true);
    foreign_unwind_test_fde(&fixture, cie, 2, false, false, 2, 0);
    foreign_unwind_test_fde(&fixture, cie, 1, false, true, 1, 101);
    foreign_unwind_test_terminator(&fixture, true);
    foreign_unwind_test_kept(tally, &fixture, arena, "original final terminator follows surviving records");

    foreign_unwind_test_begin(&fixture);
    foreign_unwind_test_kept(tally, &fixture, arena, "empty input fragment");

    foreign_unwind_test_begin(&fixture);
    cie = foreign_unwind_test_cie(&fixture, false, true);
    const uint32_t orphan = foreign_unwind_test_cie(&fixture, true, false);
    foreign_unwind_test_fde(&fixture, orphan, 2, true, false, 2, 0);
    foreign_unwind_test_fde(&fixture, cie, 1, false, true, 1, 101);
    foreign_unwind_test_kept(tally, &fixture, arena, "retained FDE points past a removed CIE to an earlier CIE");

    static const uint32_t counts[] = { 1, 7, 8, 9, 16, 17, 65 };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(counts); c += 1)
    {
        foreign_unwind_test_begin(&fixture);
        cie = foreign_unwind_test_cie(&fixture, false, true);
        for (uint32_t fde = 0; fde < counts[c]; fde += 1)
        {
            const bool kept = fde % 3 != 1;
            foreign_unwind_test_fde(&fixture, cie, kept ? 1 : 2, false, kept, kept ? 1 : 2, kept ? 101 : 0);
        }
        char detail[64];
        snprintf(detail, sizeof(detail), "%u FDEs across batch boundaries", counts[c]);
        foreign_unwind_test_kept(tally, &fixture, arena, detail);
    }
}

static void foreign_unwind_test_refused(ForeignTestTally* tally, const ForeignUnwindTestInput* fixture, const ForeignUnwindInput* input, Arena* arena, const char* detail)
{
    uint8_t original[sizeof(*fixture)];
    memcpy(original, fixture, sizeof(original));
    ForeignUnwindResult exact = { 0 };
    for (uint32_t path = 0; path < 2; path += 1)
    {
        const uint64_t saved = arena->length;
        ForeignUnwind output;
        uint8_t original_output[sizeof(output)];
        memset(&output, 0xa5, sizeof(output));
        memcpy(original_output, &output, sizeof(output));
        const ForeignUnwindResult result = foreign_unwind_prune(input, path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, arena, &output);
        foreign_test_expect(tally, result.error != FOREIGN_UNWIND_ERROR_NONE, "refuse malformed or unsupported unwind input", detail);
        if (path == 0)
        {
            exact = result;
        }
        else
        {
            foreign_test_expect(tally, result.error == exact.error && result.offset == exact.offset && result.relocation == exact.relocation,
                                "exact and vector refusal diagnostics agree", detail);
        }
        foreign_test_expect(tally, arena->length == saved && memcmp(&output, original_output, sizeof(output)) == 0,
                            "refusal rolls arena back and does not publish output", detail);
        foreign_test_expect(tally, memcmp(fixture, original, sizeof(original)) == 0, "refusal preserves input storage", detail);
        arena_pop_to(arena, saved);
    }
}

static void foreign_unwind_test_failures(ForeignTestTally* tally, Arena* arena)
{
    ForeignUnwindTestInput valid;
    foreign_unwind_test_begin(&valid);
    const uint32_t cie = foreign_unwind_test_cie(&valid, false, true);
    const uint32_t fde = foreign_unwind_test_fde(&valid, cie, 1, false, true, 1, 101);
    static const char* const cases[] = {
        "truncated length field",
        "record extends beyond section",
        "DWARF64 record marker",
        "FDE points inside CIE",
        "missing pc_begin relocation",
        "duplicate pc_begin relocation",
        "wrong pc_begin relocation kind",
        "out-of-range symbol index",
        "out-of-range original section index",
        "missing extended section indices",
        "relocation overlaps record boundary",
        "relocation overwrites record length",
        "unsupported initial-location encoding",
        "CIE augmentation extends beyond record",
        "nonzero bytes after terminator",
    };
    for (uint32_t change = 0; change < BUSTER_ARRAY_LENGTH(cases); change += 1)
    {
        ForeignUnwindTestInput fixture = valid;
        ImportSymbols symbols;
        ForeignUnwindInput input = foreign_unwind_test_input(&fixture, &symbols);
        const uint32_t initial = fixture.records[fde].offset;
        if (change == 0)
        {
            input.bytes.length = 3;
        }
        else if (change == 1 || change == 2)
        {
            const uint32_t length = change == 1 ? fixture.length : UINT32_MAX;
            memcpy(fixture.bytes, &length, sizeof(length));
        }
        else if (change == 3)
        {
            const uint32_t pointer = initial + 3;
            memcpy(fixture.bytes + initial + 4, &pointer, sizeof(pointer));
        }
        else if (change == 4)
        {
            input.relocation_count = 0;
        }
        else if (change == 5)
        {
            fixture.relocations[1] = fixture.relocations[0];
            input.relocation_count = 2;
        }
        else if (change == 6 || change == 7)
        {
            fixture.relocations[0].r_info = ELF64_R_INFO(change == 7 ? BUSTER_ARRAY_LENGTH(fixture.symbols) : 1, change == 6 ? R_X86_64_64 : R_X86_64_PC32);
        }
        else if (change == 8)
        {
            fixture.symbols[1].st_shndx = BUSTER_ARRAY_LENGTH(fixture.sections);
        }
        else if (change == 9)
        {
            fixture.symbols[1].st_shndx = SHN_XINDEX;
            symbols.extended = (String8){ 0 };
        }
        else if (change == 10 || change == 11)
        {
            fixture.relocations[1] = (Elf64_Rela){
                .r_offset = change == 10 ? fixture.records[0].length - 2 : 0,
                .r_info = ELF64_R_INFO(3, R_X86_64_PC32),
            };
            input.relocation_count = 2;
        }
        else if (change == 12)
        {
            fixture.bytes[16] = 0;
        }
        else if (change == 13)
        {
            fixture.bytes[15] = 0x7f;
        }
        else
        {
            const uint32_t zero = 0;
            memcpy(fixture.bytes + initial, &zero, sizeof(zero));
        }
        foreign_unwind_test_refused(tally, &fixture, &input, arena, cases[change]);
    }
    ImportSymbols symbols;
    ForeignUnwindInput input = foreign_unwind_test_input(&valid, &symbols);
    input.bytes.pointer = 0;
    foreign_unwind_test_refused(tally, &valid, &input, arena, "null bytes with nonzero extent");
    input = foreign_unwind_test_input(&valid, &symbols);
    input.relocations = 0;
    foreign_unwind_test_refused(tally, &valid, &input, arena, "null relocation table with nonzero count");
    for (uint32_t change = 0; change < 4; change += 1)
    {
        ForeignUnwindTestInput fixture;
        foreign_unwind_test_begin(&fixture);
        const uint32_t augmented = foreign_unwind_test_cie(&fixture, true, true);
        foreign_unwind_test_fde(&fixture, augmented, 1, true, true, 1, 101);
        input = foreign_unwind_test_input(&fixture, &symbols);
        if (change < 2)
        {
            const uint32_t relocation = change == 0 ? 0 : 2;
            const uint32_t target = change == 0 ? 3 : 4;
            fixture.relocations[relocation].r_info = ELF64_R_INFO(target, R_X86_64_32);
        }
        else if (change == 2)
        {
            fixture.sections[4] = 0;
        }
        else
        {
            fixture.sections[3] = 0;
            fixture.symbols[3].st_info = ELF64_ST_INFO(STB_LOCAL, STT_OBJECT);
        }
        static const char* const auxiliary_cases[] = {
            "absolute relocation cannot encode a pc-relative personality",
            "absolute relocation cannot encode a pc-relative LSDA",
            "live FDE's local LSDA points to a dropped section",
            "live CIE's local personality points to a dropped section",
        };
        foreign_unwind_test_refused(tally, &fixture, &input, arena, auxiliary_cases[change]);
    }
    input = foreign_unwind_test_input(&valid, &symbols);
    Arena exhausted = arena_reserve(4096);
    const bool filled = exhausted.base != 0 && arena_push(&exhausted, exhausted.capacity, 1) != 0;
    foreign_test_expect(tally, filled, "reserve exhausted unwind arena", 0);
    if (filled)
    {
        foreign_unwind_test_refused(tally, &valid, &input, &exhausted, "exhausted output arena");
    }
    arena_release(&exhausted);
}

static bool foreign_unwind_test(void)
{
    ForeignTestTally tally = {
        .check = "foreign unwind pruning",
    };
    Arena arena = arena_reserve(1ull << 20);
    foreign_test_expect(&tally, arena.base != 0, "reserve unwind test arena", 0);
    if (arena.base != 0)
    {
        foreign_unwind_test_successes(&tally, &arena);
        foreign_unwind_test_failures(&tally, &arena);
    }
    arena_release(&arena);
    return foreign_test_report(&tally);
}
