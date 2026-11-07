// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original #1474 regressions. Data tests retain independent symbol identities while checking shared storage, ELF
// symbol values and relocation addends. Guarded byte tails and a simple pairwise oracle check both hashing/sorting
// paths; the oracle is deliberately quadratic only in these small test fixtures.

static void check_data_test_string_storage(CheckTest* test, TypeTable* types)
{
    enum { COUNT = 14, ATOMS = 11, BYTES = 41 };
    static const uint8_t text[] = { 'a', 0, 'b', 0 };
    static const uint8_t copy[] = { 'a', 0, 'b', 0 };
    static const uint8_t prefix[] = { 'a', 0 };
    static const uint8_t different[] = { 'a', 0, 'c', 0 };
    static const uint8_t wide[] = { 0x34, 0x12, 0, 0 };
    static const uint8_t empty[] = { 0 };
    const TypeId chars = type_array(types, BUSTER_TYPE_CHAR, sizeof(text));
    const TypeId readonly = type_qualify(types, chars, BUSTER_TYPE_CONST);
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_CHAR);
    CheckInitPart copied = {
        .kind = CHECK_INIT_BYTES,
        .type = chars,
        .bytes = text,
        .byte_count = sizeof(text),
    };
    const CheckInitializer named = {
        .type = readonly,
        .parts = &copied,
        .count = 1,
    };
    CheckInitPart address = {
        .kind = CHECK_INIT_VALUE,
        .type = pointer,
        .value = {
            .type = pointer,
            .category = CHECK_RVALUE,
            .flags = BUSTER_CHECK_STATIC_OBJECT,
            .constant_kind = CHECK_CONSTANT_ADDRESS,
            .object = 2,
            .offset = 2,
        },
    };
    const CheckInitializer pointed = {
        .type = pointer,
        .parts = &address,
        .count = 1,
    };
    CheckDataTestLink fixture;
    bool ready = check_data_test_open(&fixture, COUNT + 2);
    SymbolId map[COUNT + 1] = { 0 };
    CheckDataObject objects[COUNT] = { 0 };
    char names[COUNT][32];
    for (uint32_t i = 0; ready && i < COUNT; i += 1)
    {
        snprintf(names[i], sizeof(names[i]), "pooled_%u", i);
        map[i + 1] = check_data_test_symbol(&fixture.link, names[i], BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL);
        ready = map[i + 1] != 0;
        objects[i] = (CheckDataObject){
            .type = chars,
            .object = i + 1,
            .initializer = BUSTER_CHECK_NONE,
            .literal_bytes = text,
            .literal_size = sizeof(text),
            .string_pool = true,
            .mergeable_string = true,
        };
    }
    check_test_expect(test, ready, "literal pooling fixture");
    objects[1].literal_bytes = copy;
    objects[2].type = type_array(types, BUSTER_TYPE_CHAR, sizeof(prefix));
    objects[2].literal_bytes = prefix;
    objects[2].literal_size = sizeof(prefix);
    objects[3].literal_bytes = different;
    objects[4].type = type_array(types, BUSTER_TYPE_UCHAR, sizeof(text));
    objects[5].alignment_log2 = 4;
    objects[6].type = objects[2].type;
    objects[6].literal_bytes = prefix;
    objects[6].literal_size = sizeof(prefix);
    objects[6].mergeable_string = false;
    for (uint32_t i = 7; i <= 13; i += 6)
    {
        objects[i].type = readonly;
        objects[i].initializer_plan = &named;
        objects[i].literal_bytes = NULL;
        objects[i].literal_size = 0;
        objects[i].string_pool = false;
        objects[i].mergeable_string = false;
    }
    for (uint32_t i = 8; i <= 9; i += 1)
    {
        objects[i].type = type_array(types, BUSTER_TYPE_USHORT, 2);
        objects[i].literal_bytes = wide;
    }
    for (uint32_t i = 10; i <= 11; i += 1)
    {
        objects[i].type = type_array(types, BUSTER_TYPE_CHAR, 1);
        objects[i].literal_bytes = empty;
        objects[i].literal_size = sizeof(empty);
    }
    objects[12].type = pointer;
    objects[12].initializer_plan = &pointed;
    objects[12].literal_bytes = NULL;
    objects[12].literal_size = 0;
    objects[12].string_pool = false;
    objects[12].mergeable_string = false;
    const uint32_t atom_capacity = fixture.link.atoms.capacity;
    const uint64_t byte_capacity = fixture.bytes.capacity;
    const uint64_t scratch_capacity = fixture.scratch.capacity;
    // Exact capacities count canonical payloads, not literal occurrences. Failed publication leaves all symbols undefined.
    for (uint32_t failure = 0; ready && failure < 3; failure += 1)
    {
        fixture.link.atoms.capacity = fixture.link.atoms.count + ATOMS - (failure == 0);
        fixture.bytes.capacity = BYTES - (failure == 1);
        fixture.scratch.capacity = failure == 2 ? 1 : scratch_capacity;
        CheckDataResult result;
        const bool emitted = check_data_emit(types, NULL, objects, COUNT, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                             &fixture.bytes, &fixture.scratch, &result);
        bool unchanged = !emitted && result.status == CHECK_DATA_RESOURCE && fixture.link.atoms.count == 1 && fixture.link.fixups.count == 1 &&
                         fixture.bytes.length == 0 && fixture.scratch.length == 0;
        for (uint32_t i = 1; unchanged && i <= COUNT; i += 1)
        {
            unchanged = fixture.link.symbols.atom[map[i]] == 0 && fixture.link.symbols.source[map[i]] == BUSTER_LINK_SOURCE_NONE;
        }
        check_test_expect(test, unchanged && (failure == 2 || (result.atom_count == ATOMS && result.byte_count == BYTES && result.fixup_count == 1)),
                          "pooled data capacity failures preserve the unpublished transaction and report unique demand");
    }
    fixture.link.atoms.capacity = fixture.link.atoms.count + ATOMS;
    fixture.bytes.capacity = BYTES;
    fixture.scratch.capacity = scratch_capacity;
    CheckDataResult result;
    const bool emitted = ready && check_data_emit(types, NULL, objects, COUNT, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                 &fixture.bytes, &fixture.scratch, &result);
    check_test_expect(test, emitted && result.atom_count == ATOMS && result.byte_count == BYTES && fixture.bytes.length == BYTES && fixture.scratch.length == 0,
                      "identical literals publish one atom and payload at exact unique capacities");
    static const uint32_t canonical[COUNT] = { 0, 0, 2, 3, 4, 5, 6, 7, 8, 8, 10, 10, 12, 13 };
    static const uint8_t classes[COUNT] = {
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_STRINGS,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_RODATA,
        BUSTER_LINK_CLASS_STRINGS,
        BUSTER_LINK_CLASS_STRINGS,
        BUSTER_LINK_CLASS_DATA,
        BUSTER_LINK_CLASS_RODATA,
    };
    for (uint32_t i = 0; emitted && i < COUNT; i += 1)
    {
        const SymbolId symbol = map[i + 1];
        const AtomId atom = fixture.link.symbols.atom[symbol];
        bool matches = atom != 0 && fixture.link.symbols.value[symbol] == 0 && fixture.link.symbols.size[symbol] == type_size(types, objects[i].type) &&
                       fixture.link.atoms.class[atom] == classes[i];
        for (uint32_t j = 0; matches && j < COUNT; j += 1)
        {
            matches = (atom == fixture.link.symbols.atom[map[j + 1]]) == (canonical[i] == canonical[j]);
        }
        if (matches && objects[i].string_pool)
        {
            matches = fixture.link.atoms.size[atom] == objects[i].literal_size &&
                      memcmp(fixture.link.atoms.bytes[atom], objects[i].literal_bytes, objects[i].literal_size) == 0;
        }
        check_test_expect(test, matches, "pooling keeps full NUL-containing bytes, type, alignment and identity-bearing arrays distinct");
    }
    if (emitted)
    {
        const AtomId pointer_atom = fixture.link.symbols.atom[map[13]];
        const FixupId fixup = fixture.link.atoms.fixup_begin[pointer_atom];
        check_test_expect(test, fixup != 0 && fixture.link.fixups.target[fixup] == map[2] && link_fixup_addend(&fixture.link, fixup) == 2 &&
                               fixture.link.atoms.alignment_log2[fixture.link.symbols.atom[map[6]]] == 4 && link_check(&fixture.link) == LINK_ERROR_NONE,
                          "relocations keep the duplicate's symbol and byte addend while that symbol addresses the shared atom");
    }
    Arena output = arena_reserve(1u << 20);
    ObjectUnits units;
    ObjectSymbols symbols;
    Object object = { 0 };
    ObjectView view;
    char error[256] = "";
    bool built = emitted && object_prepare(&units, &fixture.link, 1, &output) && object_map_symbols(&symbols, &fixture.link, &output);
    if (built)
    {
        object = object_build(&fixture.link, &units, &symbols, 0, (ObjectOptions){ 0 }, BUSTER_S8("string-pool.c"), &output);
        built = object.error == OBJECT_ERROR_NONE && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
    }
    check_test_expect(test, built, "pooled literal ELF publication");
    if (built)
    {
        const uint64_t first = check_data_test_find_symbol(&view, names[0]);
        const uint64_t second = check_data_test_find_symbol(&view, names[1]);
        const uint64_t pointer_symbol = check_data_test_find_symbol(&view, names[12]);
        const uint64_t pointer_section = pointer_symbol != 0 ? object_view_symbol_section(&view, pointer_symbol) : 0;
        uint32_t references = 0;
        for (uint64_t i = 1; first != 0 && second != 0 && pointer_symbol != 0 && i < view.section_count; i += 1)
        {
            const Elf64_Shdr* section = view.sections + i;
            if (section->sh_type == SHT_RELA && section->sh_info == pointer_section)
            {
                const Elf64_Rela* entries = (const Elf64_Rela*)(view.file + section->sh_offset);
                for (uint64_t j = 0; j < section->sh_size / sizeof(*entries); j += 1)
                {
                    const uint64_t target = ELF64_R_SYM(entries[j].r_info);
                    references += entries[j].r_offset == view.symbols[pointer_symbol].st_value && ELF64_R_TYPE(entries[j].r_info) == R_X86_64_64 &&
                                  target < view.symbol_count && object_view_symbol_section(&view, target) == object_view_symbol_section(&view, second) &&
                                  view.symbols[target].st_value + entries[j].r_addend == view.symbols[second].st_value + 2;
                }
            }
        }
        check_test_expect(test, first != 0 && second != 0 && first != second && view.symbols[first].st_value == view.symbols[second].st_value &&
                               object_view_symbol_section(&view, first) == object_view_symbol_section(&view, second) &&
                               view.symbols[first].st_size == sizeof(text) && view.symbols[second].st_size == sizeof(text) && references == 1 &&
                               (view.sections[object_view_symbol_section(&view, first)].sh_flags & (SHF_MERGE | SHF_STRINGS)) == 0,
                          "ELF retains both full literal symbols at the shared address and resolves the pointer into unsplit bytes");
    }
    arena_release(&output);
    fixture.link.atoms.capacity = atom_capacity;
    fixture.bytes.capacity = byte_capacity;
    fixture.scratch.capacity = scratch_capacity;
    check_data_test_close(&fixture);
}

static void check_data_test_string_boundaries(CheckTest* test, TypeTable* types)
{
    enum { COUNT = 129 };
    static const uint32_t sizes[] = { 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 257 };
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[BUSTER_ARRAY_LENGTH(sizes)];
    const uint8_t* payloads[BUSTER_ARRAY_LENGTH(sizes)];
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        mappings[i] = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        const bool mapped = mappings[i] != MAP_FAILED && mprotect(mappings[i] + page, page, PROT_NONE) == 0;
        ready &= mapped;
        payloads[i] = mapped ? mappings[i] + page - sizes[i] : NULL;
        if (mapped)
        {
            uint8_t* bytes = mappings[i] + page - sizes[i];
            for (uint32_t j = 0; j < sizes[i]; j += 1)
            {
                bytes[j] = j + 1 == sizes[i] || j == sizes[i] / 2 ? 0 : (uint8_t)(i + j + 1);
            }
        }
    }
    CheckDataTestLink fixture;
    ready = check_data_test_open(&fixture, COUNT + 1) && ready;
    SymbolId map[COUNT + 1] = { 0 };
    CheckDataObject objects[COUNT] = { 0 };
    for (uint32_t i = 0; ready && i < COUNT; i += 1)
    {
        const uint32_t variant = (i * 7) % BUSTER_ARRAY_LENGTH(sizes);
        char name[32];
        snprintf(name, sizeof(name), "guarded_literal_%u", i);
        map[i + 1] = check_data_test_symbol(&fixture.link, name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL);
        ready = map[i + 1] != 0;
        objects[i] = (CheckDataObject){
            .type = type_array(types, (i / 36) % 2 == 0 ? BUSTER_TYPE_CHAR : BUSTER_TYPE_UCHAR, sizes[variant]),
            .object = i + 1,
            .initializer = BUSTER_CHECK_NONE,
            .literal_bytes = payloads[variant],
            .literal_size = sizes[variant],
            .string_pool = true,
            .mergeable_string = i % 11 != 0,
            .alignment_log2 = (uint8_t)((i / 18) % 2),
        };
    }
    check_test_expect(test, ready, "literal pooling guarded input tails");
    for (uint32_t count = 0; ready && count <= COUNT; count += 1)
    {
        for (uint32_t path = 0; path < 2; path += 1)
        {
            uint32_t* canonical = NULL;
            bool matches = check_data_string_pool(types, objects, count, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, path == 0, &fixture.scratch, &canonical);
            for (uint32_t i = 0; matches && i < count; i += 1)
            {
                uint32_t first = i;
                for (uint32_t j = 0; objects[i].mergeable_string && first == i && j < i; j += 1)
                {
                    if (objects[j].mergeable_string && objects[j].type == objects[i].type && objects[j].alignment_log2 == objects[i].alignment_log2 &&
                        objects[j].literal_size == objects[i].literal_size && memcmp(objects[j].literal_bytes, objects[i].literal_bytes, objects[i].literal_size) == 0)
                    {
                        first = j;
                    }
                }
                matches = (canonical != NULL ? canonical[i] : i) == first;
            }
            check_test_expect(test, matches, "scalar/vector pooling matches the independent first-equal oracle at every batch tail, length and alignment");
            arena_pop_to(&fixture.scratch, 0);
        }
    }
    if (ready)
    {
        static const uint8_t collision_bytes[][4] = {
            { 'z', 0, 'a', 0 },
            { 'a', 0, 'b', 0 },
            { 'a', 0, 'a', 0 },
            { 'z', 0, 'a', 0 },
            { 'a', 0, 'b', 0 },
            { 'a', 0, 'z', 0 },
            { 'a', 0, 'a', 0 },
        };
        CheckDataObject collision_objects[BUSTER_ARRAY_LENGTH(collision_bytes)] = { 0 };
        uint64_t keys[BUSTER_ARRAY_LENGTH(collision_bytes)];
        uint64_t temporary[BUSTER_ARRAY_LENGTH(collision_bytes)];
        static const uint32_t ordered[] = { 2, 6, 1, 4, 5, 0, 3 };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(collision_bytes); i += 1)
        {
            collision_objects[i].type = objects[0].type;
            collision_objects[i].literal_bytes = collision_bytes[i];
            collision_objects[i].literal_size = sizeof(collision_bytes[i]);
            keys[i] = UINT64_C(0x1234567800000000) | (BUSTER_ARRAY_LENGTH(collision_bytes) - i - 1);
        }
        check_data_string_collision_sort(collision_objects, keys, temporary, BUSTER_ARRAY_LENGTH(collision_bytes));
        bool ordered_keys = true;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(collision_bytes); i += 1)
        {
            ordered_keys &= keys[i] == (UINT64_C(0x1234567800000000) | ordered[i]);
        }
        check_test_expect(test, ordered_keys, "equal-hash byte collisions sort by full embedded-NUL content and earliest record");
        uint32_t* canonical = NULL;
        const uint64_t capacity = fixture.scratch.capacity;
        fixture.scratch.capacity = COUNT * sizeof(uint32_t);
        const bool pooled = check_data_string_pool(types, objects, COUNT, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, false, &fixture.scratch, &canonical);
        check_test_expect(test, !pooled && canonical == NULL && fixture.scratch.length == 0, "literal pooling rolls back a partial scratch allocation");
        fixture.scratch.capacity = capacity;
    }
    check_data_test_close(&fixture);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(sizes); i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], page * 2);
        }
    }
}

static void check_data_test_string_invalid(CheckTest* test, TypeTable* types)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    CheckDataTestLink fixture;
    const bool ready = check_data_test_open(&fixture, 4) && guarded;
    check_test_expect(test, ready, "invalid literal metadata guard");
    if (ready)
    {
        const TypeId chars = type_array(types, BUSTER_TYPE_CHAR, 2);
        SymbolId map[4] = { 0 };
        CheckDataObject objects[3];
        static const uint8_t text[] = { 'x', 0 };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "invalid_literal_%u", i);
            map[i + 1] = check_data_test_symbol(&fixture.link, name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL);
            objects[i] = (CheckDataObject){
                .type = chars,
                .object = i + 1,
                .initializer = BUSTER_CHECK_NONE,
                .literal_bytes = text,
                .literal_size = sizeof(text),
                .string_pool = true,
                .mergeable_string = true,
            };
        }
        const CheckDataObject valid = objects[1];
        for (uint32_t failure = 0; failure < 6; failure += 1)
        {
            objects[1] = valid;
            // No byte at this pointer is readable. Invalid metadata must be refused before hashing or copying.
            objects[1].literal_bytes = mapping + page;
            objects[1].type = failure == 0 ? 0 : failure == 1 ? BUSTER_TYPE_INT : chars;
            objects[1].literal_size = failure == 2 ? 3 : failure == 1 ? 4 : sizeof(text);
            objects[1].alignment_log2 = failure == 3 ? BUSTER_LINK_MAX_ALIGNMENT_LOG2 + 1 : 0;
            objects[1].object = failure == 4 ? 0 : valid.object;
            fixture.link.symbols.flags[map[2]] = BUSTER_LINK_SYMBOL_LOCAL | (failure == 5 ? BUSTER_LINK_SYMBOL_FUNCTION : BUSTER_LINK_SYMBOL_OBJECT);
            const CheckDataStatus expected = failure == 3 ? CHECK_DATA_UNSUPPORTED : failure >= 4 ? CHECK_DATA_SYMBOL : CHECK_DATA_INVALID;
            CheckDataResult result;
            const bool emitted = check_data_emit(types, NULL, objects, 3, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                 &fixture.bytes, &fixture.scratch, &result);
            check_test_expect(test, !emitted && result.status == expected && result.failed_record == 1 && fixture.bytes.length == 0 &&
                                   fixture.scratch.length == 0 && fixture.link.atoms.count == 1 && fixture.link.symbols.atom[map[1]] == 0,
                              "invalid literal type, size, alignment and symbol metadata fails without reading the guarded payload");
        }
    }
    check_data_test_close(&fixture);
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

static void check_test_string_pool(CheckTest* test, TypeTable* types)
{
    check_data_test_string_storage(test, types);
    check_data_test_string_boundaries(test, types);
    check_data_test_string_invalid(test, types);
}
