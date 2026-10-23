// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original bounded CIE-pool fixtures. Include after foreign_unwind_test.c and the dedup adapter. These tests build
// current Link metadata directly; the separate adapter tests exercise actual COMDAT/GC producers and C++ unwinding.

typedef struct ForeignUnwindDedupTest ForeignUnwindDedupTest;
struct ForeignUnwindDedupTest
{
    Link link;
    ForeignUnwindTestInput raw[3];
    ForeignUnwind frames[3];
    ForeignUnwindRecord records[3][8];
    const ForeignUnwind* by_atom[16];
    uint8_t pins[16];
    uint8_t sections[32];
    AtomId code[3];
    AtomId eh[3];
    SymbolId functions[3];
    SymbolId section_symbols[3];
    uint8_t code_bytes[3][16];
};

static bool foreign_unwind_dedup_test_build(ForeignUnwindDedupTest* test, Arena* arena, uint32_t variant)
{
    memset(test, 0, sizeof(*test));
    const LinkCapacity capacity = {
        .names = 4,
        .atoms = 8,
        .symbols = 16,
        .fixups = 32,
        .wide_addends = 8,
    };
    bool valid = link_initialize(&test->link, arena, arena, capacity);
    const bool personality = variant >= 8 && variant <= 12;
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        test->code_bytes[i][0] = 0xc3;
        test->code[i] = link_add_atom(&test->link, BUSTER_LINK_CLASS_TEXT, 0, sizeof(test->code_bytes[i]), test->code_bytes[i], i + 1);
        ForeignUnwindTestInput* raw = &test->raw[i];
        foreign_unwind_test_begin(raw);
        uint32_t cie = foreign_unwind_test_cie(raw, personality, true);
        foreign_unwind_test_fde(raw, cie, 1, personality, true, 1, test->code[i]);
        if (variant == 6 && i == 1)
        {
            cie = foreign_unwind_test_cie(raw, personality, true);
            foreign_unwind_test_fde(raw, cie, 1, personality, true, 1, test->code[i]);
        }
        if (variant == 4 && i == 1)
        {
            raw->records[raw->record_count] = (ForeignUnwindTestRecord){
                .offset = raw->length,
                .length = 4,
                .kind = FOREIGN_UNWIND_TERMINATOR,
                .kept = true,
            };
            raw->record_count += 1;
            raw->length += 4;
        }
        if ((variant == 1 && i == 1) || (variant == 2 && i == 2))
        {
            // A valid different return-address register, without changing the record size.
            raw->bytes[14] = 15;
        }
        const uint8_t alignment = variant == 14 ? 2 : variant == 15 ? (uint8_t)(i + 2) : 3;
        test->eh[i] = link_add_atom(&test->link, BUSTER_LINK_CLASS_EH_FRAME, alignment, raw->length, raw->bytes, i + 1);
        ForeignUnwind* frame = &test->frames[i];
        *frame = (ForeignUnwind){
            .bytes = {
                .pointer = (char*)raw->bytes,
                .length = raw->length,
            },
            .records = test->records[i],
            .record_count = raw->record_count,
            .old_length = raw->length,
        };
        for (uint32_t r = 0; r < raw->record_count; r += 1)
        {
            const ForeignUnwindTestRecord* record = &raw->records[r];
            frame->records[r] = (ForeignUnwindRecord){
                .old_offset = record->offset,
                .new_offset = record->offset,
                .length = record->length,
                .cie_offset = record->kind == FOREIGN_UNWIND_FDE ? raw->records[record->cie].offset : UINT64_MAX,
                .kind = record->kind,
                .section = record->section,
                .atom = record->atom,
            };
        }
        test->by_atom[test->eh[i]] = frame;
        valid = test->code[i] != 0 && test->eh[i] != 0;
    }
    const SymbolId personality_a = valid ? link_add_symbol(&test->link, 0, 0, 17, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_ABSOLUTE) : 0;
    const SymbolId personality_b = valid ? link_add_symbol(&test->link, 0, 0, 17, 0, BUSTER_LINK_SYMBOL_GLOBAL, BUSTER_LINK_SOURCE_ABSOLUTE) : 0;
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        test->functions[i] = link_add_symbol(&test->link, 0, test->code[i], 0, 1, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
        test->section_symbols[i] = link_add_symbol(&test->link, 0, test->eh[i], 0, 0, BUSTER_LINK_SYMBOL_LOCAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT);
        test->sections[test->section_symbols[i]] = 1;
        valid = test->functions[i] != 0 && test->section_symbols[i] != 0;
    }
    if (valid && (variant == 3 || variant == 5 || variant == 7))
    {
        if (variant == 3)
        {
            test->pins[test->eh[1]] = 1;
        }
        else
        {
            const uint64_t value = variant == 5 ? 0 : 8;
            const uint64_t size = variant == 7 ? 24 : 0;
            valid = link_add_symbol(&test->link, 0, test->eh[1], value, size, BUSTER_LINK_SYMBOL_LOCAL, BUSTER_LINK_SOURCE_FOREIGN_OBJECT) != 0;
        }
    }
    for (uint32_t i = 0; valid && i < 3; i += 1)
    {
        if (variant == 13 && i == 1)
        {
            valid = link_add_fixup(&test->link, test->code[i], 0, BUSTER_LINK_FIXUP_ABS64, test->section_symbols[i], 0) != 0;
        }
        const ForeignUnwindTestInput* raw = &test->raw[i];
        for (uint32_t r = 0; valid && r < raw->relocation_count; r += 1)
        {
            const Elf64_Rela relocation = raw->relocations[r];
            const bool auxiliary = ELF64_R_SYM(relocation.r_info) == 3;
            const SymbolId target = ELF64_R_SYM(relocation.r_info) == 1 ? test->functions[i] :
                                    auxiliary && variant == 9 && i == 1 ? personality_b : personality_a;
            const int64_t addend = auxiliary && variant >= 10 && variant <= 12 ? INT64_C(0x100000003) + (variant == 11 && i == 1 ? 1 : 0) : relocation.r_addend;
            const uint8_t kind = auxiliary && variant == 12 && i == 1 ? BUSTER_LINK_FIXUP_ABS32S : BUSTER_LINK_FIXUP_PC32;
            valid = link_add_fixup(&test->link, test->eh[i], (uint32_t)relocation.r_offset, kind, target, addend) != 0;
        }
    }
    return valid && personality_a != 0 && personality_b != 0 && link_check(&test->link) == LINK_ERROR_NONE;
}

static bool foreign_unwind_dedup_test_same(const ForeignUnwindDedup* a, const ForeignUnwindDedup* b)
{
    bool same = a->atom_count == b->atom_count && a->fixup_count == b->fixup_count && a->removed_cies == b->removed_cies &&
                a->removed_atoms == b->removed_atoms && a->removed_bytes == b->removed_bytes && memcmp(&a->capacity, &b->capacity, sizeof(a->capacity)) == 0 &&
                memcmp(a->atoms, b->atoms, (uint64_t)a->atom_count * sizeof(AtomId)) == 0 && memcmp(a->fixups, b->fixups, (uint64_t)a->fixup_count * sizeof(FixupId)) == 0;
    for (AtomId atom = 1; same && atom < a->atom_count; atom += 1)
    {
        const ForeignUnwindDedupAtom* left = &a->maps[atom];
        const ForeignUnwindDedupAtom* right = &b->maps[atom];
        same = left->record_count == right->record_count && left->owner == right->owner && left->next == right->next && left->length == right->length &&
               left->endpoint == right->endpoint && left->alignment_log2 == right->alignment_log2 && left->eligible == right->eligible && left->pooled == right->pooled;
        for (uint32_t r = 0; same && r < left->record_count; r += 1)
        {
            const ForeignUnwindRecord* x = &left->records[r];
            const ForeignUnwindRecord* y = &right->records[r];
            same = x->old_offset == y->old_offset && x->new_offset == y->new_offset && x->length == y->length && x->cie_offset == y->cie_offset &&
                   x->kind == y->kind && x->section == y->section && x->atom == y->atom && left->kept[r] == right->kept[r];
        }
    }
    return same;
}

static void foreign_unwind_dedup_test_cases(ForeignTestTally* tally, Arena* arena)
{
    for (uint32_t variant = 0; variant < 16; variant += 1)
    {
        const uint64_t saved = arena->length;
        ForeignUnwindDedupTest* test = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindDedupTest, 1);
        ForeignUnwindDedupTest* snapshot = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindDedupTest, 1);
        const bool ready = test != 0 && snapshot != 0 && foreign_unwind_dedup_test_build(test, arena, variant);
        if (ready)
        {
            *snapshot = *test;
        }
        char detail[64];
        snprintf(detail, sizeof(detail), "variant %u", variant);
        foreign_test_expect(tally, ready, "construct independent current-link fixture", detail);
        ForeignUnwindDedup plans[2] = { 0 };
        for (uint32_t path = 0; ready && path < 2; path += 1)
        {
            const ForeignUnwindResult result = foreign_unwind_dedup_plan(&test->link, test->by_atom, test->pins, test->sections,
                                                                        path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, arena, &plans[path]);
            foreign_test_expect(tally, result.error == FOREIGN_UNWIND_ERROR_NONE, "plan exact/vector pool or pinned fallback", detail);
            if (result.error == FOREIGN_UNWIND_ERROR_NONE)
            {
                const uint32_t expected = variant == 0 || variant == 8 || variant == 10 || variant == 14 || variant == 15 ? 2 : variant == 6 ? 3 : variant == 2 ? 1 : 0;
                foreign_test_expect(tally, plans[path].removed_cies == expected, "only consecutive compatible unpinned CIEs alias", detail);
                for (uint32_t i = 0; i < 3; i += 1)
                {
                    const ForeignUnwindDedupAtom* map = &plans[path].maps[test->eh[i]];
                    uint64_t endpoint = UINT64_MAX;
                    foreign_test_expect(tally, foreign_unwind_dedup_offset(map, test->frames[i].old_length, test->frames[i].old_length, &endpoint) &&
                                              endpoint == map->endpoint, "explicit endpoint belongs to its input section", detail);
                    for (uint32_t r = 0; r < map->record_count; r += 1)
                    {
                        uint64_t mapped = UINT64_MAX;
                        const ForeignUnwindRecord* record = &map->records[r];
                        foreign_test_expect(tally, foreign_unwind_dedup_offset(map, test->frames[i].old_length, record->old_offset, &mapped) &&
                                                  mapped == record->new_offset, "aliased CIE and retained FDE both have byte maps", detail);
                    }
                }
                ForeignUnwindDedupLink copied = { .plan = plans[path] };
                const ForeignLink input = { .link = test->link };
                const ForeignLinkResult built = foreign_unwind_dedup_link_copy(&copied, &input, arena, arena);
                foreign_test_expect(tally, built.error == FOREIGN_LINK_ERROR_NONE, "fresh pooled Link passes source-only local checks", detail);
                if (built.error == FOREIGN_LINK_ERROR_NONE)
                {
                    if (expected == 0)
                    {
                        foreign_test_expect(tally, link_test_same(&test->link, &copied.converted.link),
                                            "pinned or incompatible optimization retains the same layout", detail);
                    }
                    uint64_t fdes = 0;
                    for (AtomId atom = 1; atom < copied.converted.link.atoms.count; atom += 1)
                    {
                        if (copied.converted.link.atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
                        {
                            fdes += executable_eh_frame_walk(&copied.converted.link, atom, 0, 0, 0);
                        }
                    }
                    foreign_test_expect(tally, fdes == (variant == 6 ? 4 : 3), "writer finds every FDE after backlink rewrite", detail);
                    foreign_test_expect(tally, copied.converted.link.symbols.count == test->link.symbols.count,
                                        "all SymbolIds survive terminal pooling", detail);
                    if (variant == 0 || variant == 6 || variant == 14 || variant == 15)
                    {
                        const AtomId pool = plans[path].atoms[test->eh[0]];
                        const uint8_t alignment = variant == 15 ? 4 : variant == 14 ? 2 : 3;
                        foreign_test_expect(tally, copied.converted.link.atoms.alignment_log2[pool] == alignment,
                                            "pool preserves maximum 4/8/16-byte section alignment", detail);
                        foreign_test_expect(tally, executable_eh_frame_fdes(&copied.converted.link, pool) == fdes,
                                            "single-CIE plain pool remains eligible for writer SIMD", detail);
                        const AtomId code = plans[path].atoms[test->code[0]];
                        foreign_test_expect(tally, link_check_target(&copied.converted.link, pool, BUSTER_LINK_FIXUP_PC32, test->functions[2]) &&
                                                  !link_check_target(&copied.converted.link, code, BUSTER_LINK_FIXUP_ABS64, test->section_symbols[2]),
                                            "unit-zero EH permission is source-only", detail);
                    }
                }
            }
        }
        if (ready && plans[0].maps != 0 && plans[1].maps != 0)
        {
            foreign_test_expect(tally, foreign_unwind_dedup_test_same(&plans[0], &plans[1]), "exact/vector maps and retained fixups agree", detail);
        }
        if (ready)
        {
            foreign_test_expect(tally, memcmp(test, snapshot, sizeof(*test)) == 0, "planner and fresh copy preserve original bytes and metadata", detail);
        }
        arena_pop_to(arena, saved);
    }
}

static void foreign_unwind_dedup_test_failures(ForeignTestTally* tally, Arena* arena)
{
    const uint64_t saved = arena->length;
    ForeignUnwindDedupTest* test = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindDedupTest, 1);
    const bool ready = test != 0 && foreign_unwind_dedup_test_build(test, arena, 0);
    foreign_test_expect(tally, ready, "construct rollback input", 0);
    Arena output = arena_reserve(1ull << 20);
    for (uint32_t path = 0; ready && output.base != 0 && path < 2; path += 1)
    {
        for (uint32_t failure = 0; failure < 3; failure += 1)
        {
            const uint64_t capacity = output.capacity;
            if (failure == 0)
            {
                output.capacity = 64;
            }
            else if (failure == 1)
            {
                test->records[1][1].cie_offset = 4;
            }
            else
            {
                test->records[1][0].atom = test->link.atoms.count;
            }
            ForeignUnwindDedup result;
            memset(&result, 0xa5, sizeof(result));
            const ForeignUnwindDedup untouched = result;
            const uint64_t length = output.length;
            const ForeignUnwindResult planned = foreign_unwind_dedup_plan(&test->link, test->by_atom, test->pins, test->sections,
                                                                         path == 0 ? FOREIGN_PATH_EXACT : FOREIGN_PATH_VECTOR, &output, &result);
            foreign_test_expect(tally, planned.error != FOREIGN_UNWIND_ERROR_NONE && output.length == length && memcmp(&result, &untouched, sizeof(result)) == 0,
                                "allocation/malformed map failure leaves arena and result unchanged", 0);
            output.capacity = capacity;
            test->records[1][1].cie_offset = 0;
            test->records[1][0].atom = 0;
        }
    }
    arena_release(&output);
    arena_pop_to(arena, saved);
}

static void foreign_unwind_dedup_test_guards(ForeignTestTally* tally)
{
    const uint64_t page = 4096;
    uint8_t* memory = mmap(0, page * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool mapped = memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0 && mprotect(memory + 3 * page, page, PROT_NONE) == 0;
    foreign_test_expect(tally, mapped, "reserve guarded CIE identity tails", 0);
    for (uint32_t length = 0; mapped && length <= 129; length += 1)
    {
        uint8_t* a = memory + page - length;
        uint8_t* b = memory + 3 * page - length;
        for (uint32_t i = 0; i < length; i += 1)
        {
            a[i] = (uint8_t)(i * 13 + length);
            b[i] = a[i];
        }
        const bool exact = foreign_unwind_dedup_bytes(a, b, length, FOREIGN_PATH_EXACT);
        const bool vector = foreign_unwind_dedup_bytes(a, b, length, FOREIGN_PATH_VECTOR);
        foreign_test_expect(tally, exact && vector, "exact/vector identity stops at guarded tail", 0);
        if (length != 0)
        {
            b[length - 1] ^= 1;
            foreign_test_expect(tally, !foreign_unwind_dedup_bytes(a, b, length, FOREIGN_PATH_EXACT) &&
                                      !foreign_unwind_dedup_bytes(a, b, length, FOREIGN_PATH_VECTOR), "last byte participates in identity", 0);
        }
    }
    if (memory != MAP_FAILED)
    {
        munmap(memory, page * 4);
    }
}

static __attribute__((unused)) bool foreign_unwind_dedup_test(void)
{
    ForeignTestTally tally = {
        .check = "foreign terminal CIE pooling",
    };
    Arena arena = arena_reserve(1ull << 24);
    foreign_test_expect(&tally, arena.base != 0, "reserve pool fixture arena", 0);
    if (arena.base != 0)
    {
        foreign_unwind_dedup_test_cases(&tally, &arena);
        foreign_unwind_dedup_test_failures(&tally, &arena);
        foreign_unwind_dedup_test_guards(&tally);
    }
    arena_release(&arena);
    return foreign_test_report(&tally);
}
