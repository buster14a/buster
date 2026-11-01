// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Included by c_checker_test.c after its CheckTest helpers. object_writer_test.c supplies the existing independent
// ELF reader and bounded external-tool runner. Fixtures below are original; Clang is an external byte/relocation
// oracle, not an imported implementation. No generated-program/backend support is implied by a valid data object.
#ifndef BUSTER_C_CHECK_DATA_TEST_INCLUDED
#define BUSTER_C_CHECK_DATA_TEST_INCLUDED

typedef struct CheckDataTestLink CheckDataTestLink;
struct CheckDataTestLink
{
    Link link;
    Arena tables;
    Arena names;
    Arena bytes;
    Arena scratch;
};

static bool check_data_test_open(CheckDataTestLink* test_link, uint32_t count)
{
    *test_link = (CheckDataTestLink){ 0 };
    const LinkCapacity capacity = {
        .names = count,
        .atoms = count,
        .symbols = count,
        .fixups = count,
        .wide_addends = count,
    };
    test_link->tables = arena_reserve(link_capacity_bytes(capacity));
    test_link->names = arena_reserve(16384);
    test_link->bytes = arena_reserve(16384);
    test_link->scratch = arena_reserve(65536);
    return link_initialize(&test_link->link, &test_link->tables, &test_link->names, capacity);
}

static void check_data_test_close(CheckDataTestLink* test_link)
{
    arena_release(&test_link->scratch);
    arena_release(&test_link->bytes);
    arena_release(&test_link->names);
    arena_release(&test_link->tables);
}

static SymbolId check_data_test_symbol(Link* link, const char* name, uint8_t flags)
{
    const NameId id = link_add_name(link, (const uint8_t*)name, (uint32_t)strlen(name));
    return id != 0 ? link_add_symbol(link, id, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) : 0;
}

static uint64_t check_data_test_find_symbol(const ObjectView* view, const char* name)
{
    uint64_t result = 0;
    for (uint64_t i = 1; result == 0 && i < view->symbol_count; i += 1)
    {
        const char* candidate = object_view_symbol_name(view, i);
        if (candidate != 0 && strcmp(candidate, name) == 0)
        {
            result = i;
        }
    }
    return result;
}

static bool check_data_test_symbol_bytes(const ObjectView* view, uint64_t symbol, const uint8_t* expected, uint64_t size, uint8_t class)
{
    bool matches = symbol != 0 && symbol < view->symbol_count;
    if (matches)
    {
        const Elf64_Sym* entry = &view->symbols[symbol];
        const uint64_t section_id = object_view_symbol_section(view, symbol);
        matches = entry->st_size == size && section_id != 0 && section_id < view->section_count;
        if (matches)
        {
            const Elf64_Shdr* section = &view->sections[section_id];
            const bool bss = class == BUSTER_LINK_CLASS_BSS;
            const bool writable = (section->sh_flags & SHF_WRITE) != 0;
            const char* section_name = object_view_section_name(view, section_id);
            const bool relro = section_name != 0 && strncmp(section_name, ".data.rel.ro", 12) == 0;
            matches = entry->st_value <= section->sh_size && size <= section->sh_size - entry->st_value &&
                      (section->sh_type == SHT_NOBITS) == bss && writable == (class != BUSTER_LINK_CLASS_RODATA) &&
                      relro == (class == BUSTER_LINK_CLASS_DATA_RELRO);
            if (matches && !bss)
            {
                matches = memcmp(view->file + section->sh_offset + entry->st_value, expected, size) == 0;
            }
        }
    }
    return matches;
}

static bool check_data_test_relocation(const ObjectView* view, const char* from, const char* target, int64_t addend)
{
    const uint64_t symbol = check_data_test_find_symbol(view, from);
    const uint64_t target_symbol = check_data_test_find_symbol(view, target);
    const uint64_t section = symbol != 0 ? object_view_symbol_section(view, symbol) : 0;
    const uint64_t offset = symbol != 0 ? view->symbols[symbol].st_value : 0;
    uint32_t matches = 0;
    for (uint64_t i = 1; symbol != 0 && target_symbol != 0 && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* relocations = &view->sections[i];
        if (relocations->sh_type == SHT_RELA && relocations->sh_info == section)
        {
            const Elf64_Rela* entries = (const Elf64_Rela*)(view->file + relocations->sh_offset);
            const uint64_t count = relocations->sh_size / sizeof(*entries);
            for (uint64_t j = 0; j < count; j += 1)
            {
                matches += entries[j].r_offset == offset && ELF64_R_TYPE(entries[j].r_info) == R_X86_64_64 &&
                           ELF64_R_SYM(entries[j].r_info) == target_symbol && entries[j].r_addend == addend;
            }
        }
    }
    return matches == 1;
}

typedef struct CheckDataTestFixture CheckDataTestFixture;
struct CheckDataTestFixture
{
    const char* name;
    TypeId type;
    CheckValue value;
    uint8_t bytes[16];
    uint8_t class;
    bool implicit_zero;
};

static void check_data_test_objects(CheckTest* test, TypeTable* types, const char* clang)
{
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    const TypeId byte_pointer = type_pointer(types, BUSTER_TYPE_UCHAR);
    const TypeId bit9 = type_bit_int(types, 9, false);
    const TypeId bit65 = type_bit_int(types, 65, true);
    CheckDataTestFixture fixtures[] = {
        {
            .name = "data_i8",
            .type = BUSTER_TYPE_SCHAR,
            .value = check_test_integer(BUSTER_TYPE_SCHAR, 249),
            .bytes = { 0xf9 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_u16",
            .type = BUSTER_TYPE_USHORT,
            .value = check_test_integer(BUSTER_TYPE_USHORT, 0xa1b2),
            .bytes = {
                0xb2,
                0xa1,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_i32",
            .type = BUSTER_TYPE_INT,
            .value = check_test_integer(BUSTER_TYPE_INT, UINT32_C(0xfffffffd)),
            .bytes = {
                0xfd,
                0xff,
                0xff,
                0xff,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_u64",
            .type = BUSTER_TYPE_ULONG,
            .value = check_test_integer(BUSTER_TYPE_ULONG, UINT64_C(0xfedcba9876543210)),
            .bytes = {
                0x10,
                0x32,
                0x54,
                0x76,
                0x98,
                0xba,
                0xdc,
                0xfe,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_u128",
            .type = BUSTER_TYPE_UINT128,
            .value = check_test_integer(BUSTER_TYPE_UINT128, ((unsigned __int128)UINT64_C(0xffeeddccbbaa9988) << 64) | UINT64_C(0x7766554433221100)),
            .bytes = {
                0x00,
                0x11,
                0x22,
                0x33,
                0x44,
                0x55,
                0x66,
                0x77,
                0x88,
                0x99,
                0xaa,
                0xbb,
                0xcc,
                0xdd,
                0xee,
                0xff,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_f32",
            .type = BUSTER_TYPE_FLOAT,
            .value = check_test_float(BUSTER_TYPE_FLOAT, -0.0L),
            .bytes = { [3] = 0x80 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_f64",
            .type = BUSTER_TYPE_DOUBLE,
            .value = check_test_float(BUSTER_TYPE_DOUBLE, 6.0L),
            .bytes = {
                [6] = 0x18,
                [7] = 0x40,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_f80",
            .type = BUSTER_TYPE_LONG_DOUBLE,
            .value = check_test_float(BUSTER_TYPE_LONG_DOUBLE, -1.5L),
            .bytes = {
                [7] = 0xc0,
                [8] = 0xff,
                [9] = 0xbf,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_zero",
            .type = BUSTER_TYPE_INT,
            .class = BUSTER_LINK_CLASS_BSS,
            .implicit_zero = true,
        },
        {
            .name = "data_const_zero",
            .type = BUSTER_TYPE_INT | BUSTER_TYPE_CONST,
            .class = BUSTER_LINK_CLASS_RODATA,
            .implicit_zero = true,
        },
        {
            .name = "data_target",
            .type = BUSTER_TYPE_INT,
            .value = check_test_integer(BUSTER_TYPE_INT, 42),
            .bytes = { 42 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_pointer",
            .type = pointer,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_const_pointer",
            .type = pointer | BUSTER_TYPE_CONST,
            .class = BUSTER_LINK_CLASS_DATA_RELRO,
        },
        {
            .name = "data_wide",
            .type = byte_pointer,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_absolute",
            .type = pointer,
            .bytes = {
                [1] = 0x10,
                [5] = 0x80,
                [6] = 0xff,
                [7] = 0xff,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_null",
            .type = pointer,
            .class = BUSTER_LINK_CLASS_BSS,
        },
        {
            .name = "data_volatile",
            .type = BUSTER_TYPE_INT | BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE,
            .value = check_test_integer(BUSTER_TYPE_INT, 17),
            .bytes = { 17 },
            .class = BUSTER_LINK_CLASS_RODATA,
        },
        {
            .name = "data_aligned",
            .type = type_aligned(types, BUSTER_TYPE_INT, 5),
            .value = check_test_integer(BUSTER_TYPE_INT, 11),
            .bytes = { 11 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_tiny",
            .type = BUSTER_TYPE_LONG_DOUBLE,
            .value = check_test_float(BUSTER_TYPE_LONG_DOUBLE, 0x1p-16445L),
            .bytes = { 1 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_large",
            .type = BUSTER_TYPE_LONG_DOUBLE,
            .value = check_test_float(BUSTER_TYPE_LONG_DOUBLE, 0x1.fffffffffffffffep16383L),
            .bytes = {
                0xff,
                0xff,
                0xff,
                0xff,
                0xff,
                0xff,
                0xff,
                0xff,
                0xfe,
                0x7f,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_bool",
            .type = BUSTER_TYPE_BOOL,
            .value = check_test_integer(BUSTER_TYPE_BOOL, 1),
            .bytes = { 1 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_bit9",
            .type = bit9,
            .value = check_test_integer(bit9, 511),
            .bytes = {
                0xff,
                0xff,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_bit65",
            .type = bit65,
            .value = check_test_integer(bit65, ((unsigned __int128)1 << 64) | 3),
            .bytes = {
                [0] = 3,
                [8] = 1,
            },
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "data_atomic",
            .type = type_atomic(types, BUSTER_TYPE_INT),
            .value = check_test_integer(BUSTER_TYPE_INT, 29),
            .bytes = { 29 },
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    enum
    {
        COUNT = BUSTER_ARRAY_LENGTH(fixtures),
        MAP_COUNT = COUNT * 2 + 5,
        EXTERNAL_OBJECT = MAP_COUNT - 1,
    };
    CheckDataTestLink test_link;
    const bool initialized = check_data_test_open(&test_link, COUNT + 4);
    check_test_expect(test, initialized, "static-data Link allocation");
    SymbolId map[MAP_COUNT] = { 0 };
    CheckDataObject objects[COUNT];
    CheckValue values[COUNT];
    CheckDiagnostic diagnostics[COUNT] = { 0 };
    CheckStream stream = {
        .values = values,
        .diagnostics = diagnostics,
        .count = COUNT,
    };
    bool ready = initialized;
    if (ready)
    {
        assert(arena_commit(&test_link.bytes, test_link.bytes.capacity));
        memset(test_link.bytes.base, 0xa5, test_link.bytes.capacity);
        assert(arena_commit(&test_link.scratch, test_link.scratch.capacity));
        memset(test_link.scratch.base, 0x5a, test_link.scratch.capacity);
        map[EXTERNAL_OBJECT] = check_data_test_symbol(&test_link.link, "data_external", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        ready = map[EXTERNAL_OBJECT] != 0;
    }
    for (uint32_t i = 0; ready && i < COUNT; i += 1)
    {
        const uint32_t object = i * 2 + 1;
        map[object] = check_data_test_symbol(&test_link.link, fixtures[i].name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        ready = map[object] != 0;
        objects[i] = (CheckDataObject){
            .type = fixtures[i].type,
            .object = object,
            .initializer = fixtures[i].implicit_zero ? BUSTER_CHECK_NONE : i,
            .token = i + 100,
        };
        values[i] = fixtures[i].value;
    }
    for (uint32_t i = 11; i <= 15; i += 1)
    {
        values[i] = (CheckValue){
            .type = i == 13 ? byte_pointer : pointer,
            .category = CHECK_RVALUE,
            .flags = BUSTER_CHECK_STATIC_OBJECT,
            .constant_kind = CHECK_CONSTANT_ADDRESS,
            .object = i <= 12 ? 21 : (i == 13 ? EXTERNAL_OBJECT : 0),
            .offset = i == 11 ? 4 : (i == 13 ? INT64_C(4294967296) : (i == 14 ? (int64_t)UINT64_C(0xffff800000001000) : 0)),
        };
    }
    CheckDataResult result;
    const bool emitted = ready && check_data_emit(types, &stream, objects, COUNT, map, MAP_COUNT, &test_link.link, 0, &test_link.bytes, &test_link.scratch, &result);
    check_test_expect(test, emitted, "static-data scalar fixture emission");
    if (emitted)
    {
        check_test_expect(test, result.atom_count == COUNT && result.fixup_count == 3 && result.wide_addend_count == 1 &&
                               test_link.scratch.length == 0 && test_link.bytes.length == result.byte_count, "static-data exact publication counts");
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            const AtomId atom = test_link.link.symbols.atom[map[objects[i].object]];
            const uint64_t size = type_size(types, fixtures[i].type);
            const uint8_t* bytes = test_link.link.atoms.bytes[atom];
            check_test_expect(test, atom == result.atom_begin + i && test_link.link.atoms.class[atom] == fixtures[i].class &&
                                   test_link.link.atoms.size[atom] == size && test_link.link.atoms.alignment_log2[atom] == type_alignment_log2(types, fixtures[i].type),
                              "static-data size, alignment, class and symbol identity");
            check_test_expect(test, fixtures[i].class == BUSTER_LINK_CLASS_BSS ? bytes == 0 : bytes != 0 && memcmp(bytes, fixtures[i].bytes, size) == 0,
                              "static-data independently stated target bytes");
        }
        const FixupId wide = test_link.link.atoms.fixup_begin[test_link.link.symbols.atom[map[27]]];
        check_test_expect(test, wide != 0 && test_link.link.fixups.target[wide] == map[EXTERNAL_OBJECT] &&
                               link_fixup_addend(&test_link.link, wide) == INT64_C(4294967296), "static-data preserves wide symbolic addend");
        check_test_expect(test, link_check(&test_link.link) == LINK_ERROR_NONE, "static-data actual link_check");
    }
    Arena object_arena = arena_reserve(1u << 20);
    ObjectUnits units;
    ObjectSymbols symbols;
    Object object = { 0 };
    ObjectView view;
    char error[512] = "";
    const ObjectOptions options = { 0 };
    bool built = emitted && object_prepare(&units, &test_link.link, 1, &object_arena) && object_map_symbols(&symbols, &test_link.link, &object_arena);
    if (built)
    {
        object = object_build(&test_link.link, &units, &symbols, 0, options, BUSTER_S8("static-data.c"), &object_arena);
        built = object.error == OBJECT_ERROR_NONE && object_view_open(&view, object.bytes, object.size, false, error, sizeof(error));
    }
    check_test_expect(test, built, "static-data actual object writer and independent ELF reader");
    if (built)
    {
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            const uint64_t symbol = check_data_test_find_symbol(&view, fixtures[i].name);
            check_test_expect(test, check_data_test_symbol_bytes(&view, symbol, fixtures[i].bytes, type_size(types, fixtures[i].type), fixtures[i].class),
                              "static-data object symbol has expected target bytes");
        }
        check_test_expect(test, check_data_test_relocation(&view, "data_pointer", "data_target", 4) &&
                               check_data_test_relocation(&view, "data_const_pointer", "data_target", 0) &&
                               check_data_test_relocation(&view, "data_wide", "data_external", INT64_C(4294967296)), "static-data ELF ABS64 target/addend oracle");
    }
    // Clang independently emits these same original C definitions. Compare bytes by symbol, not section layout,
    // because padding between objects is an object-writer choice. x87's own six padding bytes must be zero in ours.
    static const char source[] =
        "signed char data_i8 = -7; unsigned short data_u16 = 0xa1b2; int data_i32 = -3;\n"
        "unsigned long data_u64 = 0xfedcba9876543210ul;\n"
        "unsigned __int128 data_u128 = ((unsigned __int128)0xffeeddccbbaa9988ull << 64) | 0x7766554433221100ull;\n"
        "float data_f32 = -0.0f; double data_f64 = 6.0; long double data_f80 = -1.5L;\n"
        "int data_zero; const int data_const_zero; int data_target = 42;\n"
        "int *data_pointer = &data_target + 1; int * const data_const_pointer = &data_target;\n"
        "extern unsigned char data_external[]; unsigned char *data_wide = data_external + 4294967296ull;\n"
        "int *data_absolute = (int *)0xffff800000001000ull; int *data_null = 0;\n"
        "const volatile int data_volatile = 17; int data_aligned __attribute__((aligned(32))) = 11;\n"
        "long double data_tiny = 0x1p-16445L; long double data_large = 0x1.fffffffffffffffep16383L;\n"
        "_Bool data_bool = 1; _BitInt(9) data_bit9 = -1; unsigned _BitInt(65) data_bit65 = ((unsigned _BitInt(65))1 << 64) | 3;\n"
        "_Atomic int data_atomic = 29;\n";
    char directory[] = "/tmp/buster-checker-data-XXXXXX";
    const bool directory_ready = mkdtemp(directory) != 0;
    char source_path[160];
    char object_path[160];
    char output_path[160];
    char error_path[160];
    snprintf(source_path, sizeof(source_path), "%s/oracle.c", directory);
    snprintf(object_path, sizeof(object_path), "%s/oracle.o", directory);
    snprintf(output_path, sizeof(output_path), "%s/out", directory);
    snprintf(error_path, sizeof(error_path), "%s/err", directory);
    char* arguments[] = {
        (char*)clang,
        "-std=gnu17",
        "-fno-common",
        "-c",
        source_path,
        "-o",
        object_path,
        0,
    };
    bool oracle = built && directory_ready && parse_test_write(source_path, source, sizeof(source) - 1) && object_test_run(arguments, output_path, error_path) == 0;
    String8 oracle_file = { 0 };
    ObjectView theirs;
    if (oracle)
    {
        oracle_file = object_test_read_file(&object_arena, object_path);
        oracle = oracle_file.pointer != 0 && object_view_open(&theirs, (const uint8_t*)oracle_file.pointer, oracle_file.length, true, error, sizeof(error));
    }
    check_test_expect(test, oracle, "static-data independent Clang object oracle ran");
    for (uint32_t i = 0; oracle && i < COUNT; i += 1)
    {
        const uint64_t symbol = check_data_test_find_symbol(&theirs, fixtures[i].name);
        char description[160];
        snprintf(description, sizeof(description), "static-data bytes match independent Clang static object %s", fixtures[i].name);
        check_test_expect(test, check_data_test_symbol_bytes(&theirs, symbol, fixtures[i].bytes, type_size(types, fixtures[i].type), fixtures[i].class),
                          description);
    }
    if (oracle)
    {
        check_test_expect(test, check_data_test_relocation(&theirs, "data_pointer", "data_target", 4) &&
                               check_data_test_relocation(&theirs, "data_const_pointer", "data_target", 0) &&
                               check_data_test_relocation(&theirs, "data_wide", "data_external", INT64_C(4294967296)), "static-data Clang ABS64 target/addend oracle");
    }
    if (directory_ready)
    {
        unlink(source_path);
        unlink(object_path);
        unlink(output_path);
        unlink(error_path);
        rmdir(directory);
    }
    arena_release(&object_arena);
    check_data_test_close(&test_link);
}

static void check_data_test_limits(CheckTest* test, TypeTable* types)
{
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    for (uint32_t failure = 0; failure < 19; failure += 1)
    {
        CheckDataTestLink test_link;
        const bool initialized = check_data_test_open(&test_link, 4);
        check_test_expect(test, initialized, "static-data rejection fixture Link allocation");
        if (initialized)
        {
            SymbolId map[4] = { 0 };
            map[1] = check_data_test_symbol(&test_link.link, "first", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            map[2] = check_data_test_symbol(&test_link.link, "second", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            map[3] = check_data_test_symbol(&test_link.link, "external", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            CheckValue values[2] = {
                check_test_integer(BUSTER_TYPE_INT, 71),
                {
                    .type = pointer,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = 3,
                    .offset = INT64_C(4294967296),
                },
            };
            CheckDiagnostic diagnostics[2] = { 0 };
            CheckStream stream = {
                .values = values,
                .diagnostics = diagnostics,
                .count = 2,
            };
            CheckDataObject objects[2] = {
                {
                    .type = BUSTER_TYPE_INT,
                    .object = 1,
                    .initializer = 0,
                    .token = 101,
                },
                {
                    .type = pointer,
                    .object = 2,
                    .initializer = 1,
                    .token = 202,
                },
            };
            CheckDataStatus expected = CHECK_DATA_RESOURCE;
            switch (failure)
            {
                case 0:
                    test_link.link.atoms.capacity = test_link.link.atoms.count + 1;
                    break;
                case 1:
                    test_link.link.fixups.capacity = test_link.link.fixups.count;
                    break;
                case 2:
                    test_link.link.fixups.wide_capacity = test_link.link.fixups.wide_count;
                    break;
                case 3:
                    test_link.bytes.capacity = 11;
                    break;
                case 4:
                    test_link.scratch.capacity = 1;
                    break;
                case 5:
                    objects[1].object = 1;
                    expected = CHECK_DATA_SYMBOL;
                    break;
                case 6:
                    values[1].object = 4;
                    expected = CHECK_DATA_SYMBOL;
                    break;
                case 7:
                    values[1].flags = 0;
                    expected = CHECK_DATA_NON_CONSTANT;
                    break;
                case 8:
                    values[1].fault = CHECK_FAULT_SIGNED_OVERFLOW;
                    expected = CHECK_DATA_NON_CONSTANT;
                    break;
                case 9:
                    objects[1].type = type_array(types, BUSTER_TYPE_INT, 2);
                    expected = CHECK_DATA_UNSUPPORTED;
                    break;
                case 10:
                    test_link.link.symbols.flags[map[2]] = BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL;
                    expected = CHECK_DATA_SYMBOL;
                    break;
                case 11:
                    test_link.link.symbols.flags[map[3]] = BUSTER_LINK_SYMBOL_TLS | BUSTER_LINK_SYMBOL_GLOBAL;
                    expected = CHECK_DATA_UNSUPPORTED;
                    break;
                case 12:
                    test_link.link.symbols.flags[map[3]] = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL;
                    expected = CHECK_DATA_SYMBOL;
                    break;
                case 13:
                    values[1].type = BUSTER_TYPE_LONG;
                    expected = CHECK_DATA_INVALID;
                    break;
                case 14:
                    objects[1].initializer = 2;
                    expected = CHECK_DATA_INVALID;
                    break;
                case 15:
                    stream.report_count = 1;
                    expected = CHECK_DATA_CHECK_FAILED;
                    break;
                case 16:
                    objects[1].type = type_atomic(types, BUSTER_TYPE_INT);
                    expected = CHECK_DATA_INVALID;
                    break;
                case 17:
                    // A scalar type the data path doesn't take, nullptr_t (a __float128 is 16 zero bytes without an
                    // initializer, #137, and the half-precision types are their bits, #136).
                    objects[1].type = BUSTER_TYPE_NULLPTR;
                    expected = CHECK_DATA_UNSUPPORTED;
                    break;
                case 18:
                    objects[1].alignment_log2 = BUSTER_LINK_MAX_ALIGNMENT_LOG2 + 1;
                    expected = CHECK_DATA_UNSUPPORTED;
                    break;
            }
            uint8_t table_copy[4096];
            const bool fits = test_link.tables.length <= sizeof(table_copy);
            check_test_expect(test, fits, "static-data table snapshot capacity");
            if (fits)
            {
                memcpy(table_copy, test_link.tables.base, test_link.tables.length);
                const Link before = test_link.link;
                CheckDataResult result;
                const bool accepted = check_data_emit(types, &stream, objects, 2, map, 4, &test_link.link, 0, &test_link.bytes, &test_link.scratch, &result);
                check_test_expect(test, !accepted && result.status == expected, "static-data semantic/capacity failure is explicit");
                check_test_expect(test, before.error == test_link.link.error && before.atoms.count == test_link.link.atoms.count &&
                                       before.symbols.count == test_link.link.symbols.count && before.fixups.count == test_link.link.fixups.count &&
                                       before.fixups.wide_count == test_link.link.fixups.wide_count && before.names.count == test_link.link.names.count &&
                                       memcmp(table_copy, test_link.tables.base, test_link.tables.length) == 0 && test_link.bytes.length == 0 && test_link.scratch.length == 0,
                                  "static-data failed batch publishes no Link record or bytes");
                if (failure <= 3)
                {
                    check_test_expect(test, result.atom_count == 2 && result.fixup_count == 1 && result.wide_addend_count == 1 && result.byte_count == 12,
                                      "static-data capacity failure retains exact required counts");
                }
                if (failure >= 5 && failure != 15)
                {
                    check_test_expect(test, result.failed_record == 1 && result.failed_token == 202, "static-data failure preserves declaration location");
                }
            }
        }
        // Restore backing capacities before releasing the mappings changed by the deliberate limit fixtures.
        test_link.bytes.capacity = 16384;
        test_link.scratch.capacity = 65536;
        check_data_test_close(&test_link);
    }
}

static void check_data_test_floating(CheckTest* test)
{
    const long double values[] = {
        0.0L,
        -0.0L,
        0x1p-16445L,
        0x1p-16382L,
        1.0L,
        -1.5L,
        0x1.fffffffffffffffep16383L,
        INFINITY,
        -INFINITY,
        NAN,
    };
    const uint64_t significands[] = {
        0,
        0,
        1,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x8000000000000000),
        UINT64_C(0xc000000000000000),
        UINT64_MAX,
        UINT64_C(0x8000000000000000),
        UINT64_C(0x8000000000000000),
        UINT64_C(0xc000000000000000),
    };
    const uint16_t exponents[] = {
        0,
        0x8000,
        0,
        1,
        0x3fff,
        0xbfff,
        0x7ffe,
        0x7fff,
        0xffff,
        0x7fff,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(values); i += 1)
    {
        uint8_t bytes[16] = { 0 };
        check_data_float_bytes(bytes, BUSTER_TYPE_LONG_DOUBLE, values[i]);
        uint64_t significand = 0;
        for (uint32_t b = 0; b < 8; b += 1)
        {
            significand |= (uint64_t)bytes[b] << (8 * b);
        }
        const uint16_t exponent = (uint16_t)((uint16_t)bytes[8] | ((uint16_t)bytes[9] << 8));
        check_test_expect(test, significand == significands[i] && exponent == exponents[i], "static-data independent x87 fields including subnormals/specials");
        for (uint32_t b = 10; b < 16; b += 1)
        {
            check_test_expect(test, bytes[b] == 0, "static-data x87 padding has no host bytes");
        }
    }
}

static void check_data_test_composite(CheckTest* test, TypeTable* types)
{
    const TypeId old_function = type_function(types, BUSTER_TYPE_INT, 0, 0, 0);
    const TypeId new_function = type_function(types, BUSTER_TYPE_INT, 0, 0, BUSTER_TYPE_FLAG_PROTOTYPE);
    const TypeId old_array = type_incomplete_array(types, BUSTER_TYPE_INT);
    const TypeId new_array = type_array(types, BUSTER_TYPE_INT, 3);
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    const TypeId old_types[] = {
        type_pointer(types, old_function),
        type_pointer(types, old_array),
        pointer,
    };
    const TypeId new_types[] = {
        type_pointer(types, new_function),
        type_pointer(types, new_array),
        pointer,
    };
    CheckDataTestLink test_link;
    const bool initialized = check_data_test_open(&test_link, 8);
    check_test_expect(test, initialized, "static-data composite pointer Link allocation");
    if (initialized)
    {
        SymbolId map[7] = { 0 };
        map[1] = check_data_test_symbol(&test_link.link, "function_pointer", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        map[2] = check_data_test_symbol(&test_link.link, "array_pointer", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        map[3] = check_data_test_symbol(&test_link.link, "local_pointer", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL);
        map[4] = check_data_test_symbol(&test_link.link, "local_target", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL);
        map[5] = check_data_test_symbol(&test_link.link, "external_function", BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL);
        map[6] = check_data_test_symbol(&test_link.link, "external_array", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        CheckValue values[3];
        CheckDiagnostic diagnostics[3] = { 0 };
        CheckDataObject objects[4];
        for (uint32_t i = 0; i < 3; i += 1)
        {
            values[i] = (CheckValue){
                .type = old_types[i],
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_STATIC_OBJECT,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = i == 2 ? 4 : i + 5,
                .offset = i == 2 ? -17 : 0,
            };
            objects[i] = (CheckDataObject){
                .type = new_types[i],
                .object = i + 1,
                .initializer = i,
                .token = i + 10,
            };
        }
        objects[3] = (CheckDataObject){
            .type = BUSTER_TYPE_INT,
            .object = 4,
            .initializer = BUSTER_CHECK_NONE,
            .token = 13,
        };
        const CheckStream stream = {
            .values = values,
            .diagnostics = diagnostics,
            .count = 3,
        };
        CheckDataResult result;
        const bool emitted = check_data_emit(types, &stream, objects, 4, map, 7, &test_link.link, 7, &test_link.bytes, &test_link.scratch, &result);
        check_test_expect(test, emitted && result.fixup_count == 3, "static-data compatible function/array pointer completion");
        check_test_expect(test, emitted && link_check(&test_link.link) == LINK_ERROR_NONE, "static-data local forward reference validates");
        if (emitted)
        {
            const AtomId atom = test_link.link.symbols.atom[map[3]];
            const FixupId fixup = test_link.link.atoms.fixup_begin[atom];
            check_test_expect(test, test_link.link.fixups.target[fixup] == map[4] && link_fixup_addend(&test_link.link, fixup) == -17,
                              "static-data negative addend and local identity survive");
        }
    }
    check_data_test_close(&test_link);
}

static void check_data_test_partitions(CheckTest* test, TypeTable* types)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    const uint64_t widths[] = {
        sizeof(CheckDataObject),
        sizeof(CheckValue),
        sizeof(CheckDiagnostic),
        sizeof(SymbolId),
    };
    uint8_t* mappings[BUSTER_ARRAY_LENGTH(widths)];
    uint64_t readable[BUSTER_ARRAY_LENGTH(widths)];
    bool ready = true;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        readable[i] = (widths[i] * 34 + page - 1) / page * page;
        mappings[i] = mmap(0, readable[i] + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready = ready && mappings[i] != MAP_FAILED;
        if (mappings[i] != MAP_FAILED)
        {
            ready = mprotect(mappings[i] + readable[i], page, PROT_NONE) == 0 && ready;
        }
    }
    check_test_expect(test, ready, "static-data protected input tails allocation");
    for (uint32_t count = 0; ready && count <= 33; count += 1)
    {
        CheckDataObject* objects = (CheckDataObject*)(mappings[0] + readable[0] - count * widths[0]);
        CheckValue* values = (CheckValue*)(mappings[1] + readable[1] - count * widths[1]);
        CheckDiagnostic* diagnostics = (CheckDiagnostic*)(mappings[2] + readable[2] - count * widths[2]);
        SymbolId* map = (SymbolId*)(mappings[3] + readable[3] - (count + 1) * widths[3]);
        map[0] = 0;
        CheckDataTestLink whole;
        CheckDataTestLink partitioned;
        const bool opened_whole = check_data_test_open(&whole, count + 1);
        const bool opened_partitioned = check_data_test_open(&partitioned, count + 1);
        bool valid = opened_whole && opened_partitioned;
        check_test_expect(test, valid, "static-data partition Link allocation");
        for (uint32_t i = 0; valid && i < count; i += 1)
        {
            char name[32];
            snprintf(name, sizeof(name), "object_%u", i);
            map[i + 1] = check_data_test_symbol(&whole.link, name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            const SymbolId other = check_data_test_symbol(&partitioned.link, name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            valid = map[i + 1] != 0 && map[i + 1] == other;
            objects[i] = (CheckDataObject){
                .type = BUSTER_TYPE_INT | (i % 2 == 0 ? BUSTER_TYPE_CONST : 0),
                .object = i + 1,
                .initializer = i % 3 == 0 ? BUSTER_CHECK_NONE : i,
                .token = i + 100,
            };
            values[i] = check_test_integer(BUSTER_TYPE_INT, i * 137 + 3);
            diagnostics[i] = CHECK_DIAG_NONE;
        }
        const CheckStream stream = {
            .values = values,
            .diagnostics = diagnostics,
            .count = count,
        };
        CheckDataResult result;
        valid = valid && check_data_emit(types, &stream, objects, count, map, count + 1, &whole.link, 0, &whole.bytes, &whole.scratch, &result);
        check_test_expect(test, valid, "static-data protected logical tail emission");
        for (uint32_t start = 0; valid && start < count;)
        {
            const uint32_t desired = (start % 7) + 1;
            const uint32_t batch = desired < count - start ? desired : count - start;
            valid = check_data_emit(types, &stream, objects + start, batch, map, count + 1, &partitioned.link, 0, &partitioned.bytes, &partitioned.scratch, &result);
            start += batch;
        }
        check_test_expect(test, valid && whole.link.atoms.count == partitioned.link.atoms.count && whole.bytes.length == partitioned.bytes.length,
                          "static-data legal batch partitions preserve counts");
        for (uint32_t atom = 1; valid && atom < whole.link.atoms.count; atom += 1)
        {
            const bool bss = whole.link.atoms.class[atom] == BUSTER_LINK_CLASS_BSS;
            const bool same_bytes = bss ? partitioned.link.atoms.bytes[atom] == 0 :
                                    memcmp(whole.link.atoms.bytes[atom], partitioned.link.atoms.bytes[atom], whole.link.atoms.size[atom]) == 0;
            check_test_expect(test, whole.link.atoms.class[atom] == partitioned.link.atoms.class[atom] &&
                                   whole.link.atoms.size[atom] == partitioned.link.atoms.size[atom] &&
                                   whole.link.symbols.atom[atom] == partitioned.link.symbols.atom[atom] && same_bytes,
                              "static-data legal batch partitions preserve identities and bytes");
        }
        check_data_test_close(&partitioned);
        check_data_test_close(&whole);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(widths); i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], readable[i] + page);
        }
    }
}

static void check_data_test_nonzero(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "static-data zero scan guarded allocation");
    for (uint64_t size = 0; ready && size <= 193; size += 1)
    {
        uint8_t* bytes = mapping + page - size;
        memset(bytes, 0, size);
        check_test_expect(test, !check_data_nonzero(bytes, size), "static-data zero scan bounded blocks and every masked tail");
        for (uint64_t at = 0; at < size; at += 1)
        {
            bytes[at] = (uint8_t)(1u << (at % 8));
            bool scalar = false;
            for (uint64_t byte = 0; byte < size; byte += 1)
            {
                scalar = scalar || bytes[byte] != 0;
            }
            check_test_expect(test, check_data_nonzero(bytes, size) == scalar, "static-data zero scan matches scalar oracle at every guarded byte");
            bytes[at] = 0;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

static void check_data_test_retained_payloads(CheckTest* test, TypeTable* types)
{
    enum
    {
        SIZE = (1u << 20) + 64,
        COUNT = 7,
    };
    const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, SIZE);
    const TypeId readonly = type_qualify(types, array, BUSTER_TYPE_CONST);
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_UCHAR);
    const TypeId pointers = type_array(types, pointer, SIZE / 8);
    CheckDataTestLink fixture;
    const bool ready = check_data_test_open(&fixture, COUNT + 1);
    check_test_expect(test, ready, "static-data retained-payload fixture allocation");
    if (ready)
    {
        arena_release(&fixture.bytes);
        fixture.bytes = arena_reserve((COUNT + 1ull) * SIZE);
        uint8_t* literal = arena_push(&fixture.bytes, SIZE, 1);
        CheckInitPart* parts = BUSTER_ARENA_PUSH_ARRAY(&fixture.scratch, CheckInitPart, 3);
        const bool inputs = literal != 0 && parts != 0;
        check_test_expect(test, inputs, "static-data inputs in stable live arena prefixes");
        if (inputs)
        {
            memset(literal, 0x6d, SIZE);
            parts[0] = (CheckInitPart){
                .kind = CHECK_INIT_VALUE,
                .type = BUSTER_TYPE_UCHAR,
                .value = check_test_integer(BUSTER_TYPE_UCHAR, 1),
            };
            parts[1] = parts[0];
            parts[1].value.integer = 0;
            parts[2] = (CheckInitPart){
                .kind = CHECK_INIT_VALUE,
                .type = pointer,
                .bit_offset = (SIZE - 8ull) * 8,
                .value = {
                    .type = pointer,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = COUNT + 1,
                    .offset = INT64_C(4294967296),
                },
            };
            CheckInitPart last = parts[0];
            last.bit_offset = (SIZE - 1ull) * 8;
            CheckInitializer initializers[4] = {
                {
                    .type = array,
                    .parts = parts,
                    .count = 1,
                },
                {
                    .type = array,
                    .parts = parts + 1,
                    .count = 1,
                },
                {
                    .type = pointers,
                    .parts = parts + 2,
                    .count = 1,
                },
                {
                    .type = readonly,
                    .parts = &last,
                    .count = 1,
                },
            };
            SymbolId map[COUNT + 2] = { 0 };
            CheckDataObject objects[COUNT] = { 0 };
            for (uint32_t i = 0; i < COUNT; i += 1)
            {
                char name[32];
                snprintf(name, sizeof(name), "retained_%u", i);
                const uint8_t kind = i == 5 ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
                map[i + 1] = check_data_test_symbol(&fixture.link, name, kind | BUSTER_LINK_SYMBOL_GLOBAL);
                objects[i] = (CheckDataObject){
                    .type = array,
                    .object = i + 1,
                    .initializer = BUSTER_CHECK_NONE,
                    .initializer_plan = &initializers[0],
                    .token = i + 100,
                    .alignment_log2 = 6,
                };
            }
            objects[1].initializer_plan = &initializers[1];
            objects[2].type = readonly;
            objects[3].initializer_plan = 0;
            objects[3].literal_bytes = literal;
            objects[3].literal_size = SIZE;
            objects[3].string_pool = true;
            objects[4].type = pointers;
            objects[4].initializer_plan = &initializers[2];
            objects[5].type = readonly;
            objects[5].initializer_plan = &initializers[3];
            objects[6].type = readonly;
            objects[6].initializer_plan = 0;
            map[COUNT + 1] = check_data_test_symbol(&fixture.link, "external", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            const uint64_t byte_start = fixture.bytes.length;
            const uint64_t scratch_start = fixture.scratch.length;
            // A full payload cannot fit scratch. Every successful materialized atom must use retained storage.
            fixture.scratch.capacity = 8192;
            uint8_t table_copy[8192];
            const bool fits = fixture.tables.length <= sizeof(table_copy);
            check_test_expect(test, fits, "static-data retained transaction table snapshot fits");
            for (uint32_t failure = 0; fits && failure < 4; failure += 1)
            {
                memcpy(table_copy, fixture.tables.base, fixture.tables.length);
                const Link before = fixture.link;
                const uint32_t atoms = fixture.link.atoms.capacity;
                const uint8_t flags = fixture.link.symbols.flags[map[COUNT + 1]];
                const uint64_t byte_capacity = fixture.bytes.capacity;
                if (failure == 0)
                {
                    fixture.link.atoms.capacity = fixture.link.atoms.count;
                }
                else if (failure == 1)
                {
                    fixture.link.symbols.flags[map[COUNT + 1]] = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_LOCAL;
                    memcpy(table_copy, fixture.tables.base, fixture.tables.length);
                }
                else if (failure == 2)
                {
                    fixture.bytes.capacity = byte_start + (COUNT - 2ull) * SIZE;
                }
                CheckDataResult result;
                const bool emitted = check_data_emit(types, 0, objects, COUNT, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 11,
                                                     &fixture.bytes, &fixture.scratch, &result);
                check_test_expect(test, emitted == (failure == 3), "static-data large payloads fit metadata-only scratch");
                check_test_expect(test, fixture.scratch.length == scratch_start && literal[0] == 0x6d && literal[SIZE - 1] == 0x6d &&
                                       parts[0].value.integer == 1 && parts[2].value.offset == INT64_C(4294967296),
                                  "static-data direct construction preserves live input prefixes");
                if (!emitted)
                {
                    if (failure != 1)
                    {
                        check_test_expect(test, result.status == CHECK_DATA_RESOURCE && result.atom_count == COUNT &&
                                               result.byte_count == (COUNT - 1ull) * SIZE && result.fixup_count == 1 && result.wide_addend_count == 1,
                                          "static-data exact capacity demand needs no implicit-zero scratch image");
                    }
                    check_test_expect(test, fixture.link.error == before.error && fixture.link.atoms.count == before.atoms.count &&
                                           fixture.link.fixups.count == before.fixups.count && fixture.link.fixups.wide_count == before.fixups.wide_count &&
                                           memcmp(table_copy, fixture.tables.base, fixture.tables.length) == 0 && fixture.bytes.length == byte_start,
                                      "static-data late capacity/binding failure rolls back retained suffix and all Link tables");
                }
                else
                {
                    const uint8_t classes[COUNT] = {
                        BUSTER_LINK_CLASS_DATA,
                        BUSTER_LINK_CLASS_BSS,
                        BUSTER_LINK_CLASS_RODATA,
                        BUSTER_LINK_CLASS_STRINGS,
                        BUSTER_LINK_CLASS_DATA,
                        BUSTER_LINK_CLASS_TDATA,
                        BUSTER_LINK_CLASS_RODATA,
                    };
                    uint64_t offset = byte_start;
                    check_test_expect(test, result.byte_count == (COUNT - 1ull) * SIZE && result.fixup_count == 1 &&
                                           result.wide_addend_count == 1 && fixture.bytes.length == offset + result.byte_count,
                                      "static-data retained bytes exclude reclaimed BSS and keep exact counts");
                    for (uint32_t i = 0; i < COUNT; i += 1)
                    {
                        const AtomId atom = fixture.link.symbols.atom[map[i + 1]];
                        const bool zero_fill = i == 1;
                        const uint8_t* payload = fixture.link.atoms.bytes[atom];
                        bool exact = zero_fill ? payload == 0 : payload == fixture.bytes.base + offset;
                        for (uint64_t byte = 0; exact && !zero_fill && byte < SIZE; byte += 1)
                        {
                            const uint8_t expected = i == 3 ? 0x6d : i == 0 || i == 2 ? byte == 0 : i == 5 ? byte == SIZE - 1 : 0;
                            exact = payload[byte] == expected;
                        }
                        check_test_expect(test, exact && fixture.link.atoms.class[atom] == classes[i] && fixture.link.atoms.size[atom] == SIZE &&
                                               fixture.link.atoms.alignment_log2[atom] == 6 && fixture.link.atoms.unit[atom] == 11,
                                          "static-data retained payload has exact bytes, class, alignment and record order");
                        offset += zero_fill ? 0 : SIZE;
                    }
                    const AtomId atom = fixture.link.symbols.atom[map[5]];
                    const FixupId fixup = fixture.link.atoms.fixup_begin[atom];
                    check_test_expect(test, fixture.link.fixups.offset[fixup] == SIZE - 8 && fixture.link.fixups.target[fixup] == map[COUNT + 1] &&
                                           link_fixup_addend(&fixture.link, fixup) == INT64_C(4294967296) && link_check(&fixture.link) == LINK_ERROR_NONE,
                                      "static-data retained payload preserves relocation target and wide addend");
                }
                fixture.link.atoms.capacity = atoms;
                fixture.link.symbols.flags[map[COUNT + 1]] = flags;
                fixture.bytes.capacity = byte_capacity;
            }
        }
        fixture.scratch.capacity = 65536;
    }
    check_data_test_close(&fixture);
}

static void check_data_test_aggregate_transaction(CheckTest* test, TypeTable* types)
{
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    const TypeId array = type_qualify(types, type_array(types, pointer, 4), BUSTER_TYPE_CONST);
    CheckInitPart parts[5] = { 0 };
    const uint32_t offsets[] = {
        0,
        16,
        8,
        8,
        0,
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(parts); i += 1)
    {
        parts[i] = (CheckInitPart){
            .kind = i == 3 ? CHECK_INIT_CLEAR : CHECK_INIT_VALUE,
            .type = pointer,
            .token = i + 10,
            .bit_offset = (uint64_t)offsets[i] * 8,
            .byte_count = i == 3 ? 8 : 0,
            .value = {
                .type = pointer,
                .category = CHECK_RVALUE,
                .flags = BUSTER_CHECK_STATIC_OBJECT,
                .constant_kind = CHECK_CONSTANT_ADDRESS,
                .object = i == 2 ? 4 : 3,
                .offset = i == 4 ? INT64_C(4294967296) : 4,
            },
        };
    }
    const CheckInitializer initializer = {
        .type = array,
        .parts = parts,
        .count = BUSTER_ARRAY_LENGTH(parts),
    };
    const CheckDataObject object = {
        .type = array,
        .object = 1,
        .initializer = BUSTER_CHECK_NONE,
        .token = 123,
        .initializer_plan = &initializer,
        .alignment_log2 = 5,
    };
    for (uint32_t failure = 0; failure < 4; failure += 1)
    {
        CheckDataTestLink fixture = { 0 };
        const bool ready = check_data_test_open(&fixture, 8);
        check_test_expect(test, ready, "aggregate transaction fixture storage");
        if (ready)
        {
            SymbolId map[5] = { 0 };
            map[1] = check_data_test_symbol(&fixture.link, "aggregate", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            map[3] = check_data_test_symbol(&fixture.link, "target", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            uint32_t* references = 0;
            uint32_t reference_count = 0;
            Arena tiny = fixture.bytes;
            tiny.capacity = 0;
            const CheckInitStatus too_small = check_initializer_references(types, &initializer, &tiny, &fixture.scratch, &references, &reference_count);
            check_test_expect(test, too_small == CHECK_INIT_RESOURCE && references == 0 && reference_count == 0 && tiny.length == 0 && fixture.scratch.length == 0,
                              "reference collection capacity failure publishes no partial object identities");
            const CheckInitStatus reference_status = check_initializer_references(types, &initializer, &fixture.bytes, &fixture.scratch, &references, &reference_count);
            check_test_expect(test, reference_status == CHECK_INIT_OK && reference_count == 2 && references[0] == 3 && references[1] == 3 && fixture.scratch.length == 0,
                              "only final references survive with distinct fields and exact duplicate identities");
            arena_pop_to(&fixture.bytes, 0);
            if (failure == 0)
            {
                fixture.link.atoms.capacity = fixture.link.atoms.count;
            }
            else if (failure == 1)
            {
                fixture.link.fixups.capacity = fixture.link.fixups.count + 1;
            }
            else if (failure == 2)
            {
                fixture.bytes.capacity = 31;
            }
            uint8_t before[8192];
            const bool fits = fixture.tables.length <= sizeof(before);
            check_test_expect(test, fits, "aggregate transaction snapshot fits");
            if (fits)
            {
                memcpy(before, fixture.tables.base, fixture.tables.length);
                const Link previous = fixture.link;
                CheckDataResult result = { 0 };
                const bool emitted = check_data_emit(types, 0, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                     &fixture.bytes, &fixture.scratch, &result);
                check_test_expect(test, emitted == (failure == 3) && result.atom_count == 1 && result.fixup_count == 2 && result.wide_addend_count == 1 && result.byte_count == 32,
                                  "aggregate emission preflights exact final bytes and relocations after overwrites");
                if (!emitted)
                {
                    check_test_expect(test, result.status == CHECK_DATA_RESOURCE && previous.error == fixture.link.error &&
                                           previous.atoms.count == fixture.link.atoms.count && previous.symbols.count == fixture.link.symbols.count &&
                                           previous.fixups.count == fixture.link.fixups.count && previous.fixups.wide_count == fixture.link.fixups.wide_count &&
                                           memcmp(before, fixture.tables.base, fixture.tables.length) == 0 && fixture.bytes.length == 0 && fixture.scratch.length == 0,
                                      "aggregate capacity failure leaves Link, symbols, bytes and scratch unchanged");
                }
                else
                {
                    const AtomId atom = fixture.link.symbols.atom[map[1]];
                    const FixupId first = fixture.link.atoms.fixup_begin[atom];
                    check_test_expect(test, link_check(&fixture.link) == LINK_ERROR_NONE && fixture.link.atoms.class[atom] == BUSTER_LINK_CLASS_DATA_RELRO &&
                                           fixture.link.atoms.alignment_log2[atom] == 5 &&
                                           fixture.link.fixups.offset[first] == 0 && fixture.link.fixups.offset[first + 1] == 16 &&
                                           link_fixup_addend(&fixture.link, first) == INT64_C(4294967296) && link_fixup_addend(&fixture.link, first + 1) == 4,
                                      "aggregate fixups are sorted with exact addends and discarded unmapped targets need no symbol");
                }
            }
            fixture.bytes.capacity = 16384;
        }
        check_data_test_close(&fixture);
    }
}

static void check_data_test_many_fixups(CheckTest* test, TypeTable* types)
{
    enum
    {
        COUNT = 1024,
    };
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    const TypeId array = type_array(types, pointer, COUNT);
    Arena parts_arena = arena_reserve(UINT64_C(1) << 20);
    CheckInitPart* parts = BUSTER_ARENA_PUSH_ARRAY(&parts_arena, CheckInitPart, COUNT + 1);
    CheckDataTestLink fixture = { 0 };
    bool ready = parts != 0 && check_data_test_open(&fixture, COUNT + 8);
    if (ready)
    {
        arena_release(&fixture.scratch);
        fixture.scratch = arena_reserve(UINT64_C(1) << 20);
        ready = fixture.scratch.base != 0;
    }
    check_test_expect(test, ready, "reverse-designated reference fixture storage");
    if (ready)
    {
        SymbolId map[5] = { 0 };
        map[1] = check_data_test_symbol(&fixture.link, "reverse", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        map[3] = check_data_test_symbol(&fixture.link, "even", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        map[4] = check_data_test_symbol(&fixture.link, "odd", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        for (uint32_t i = 0; i < COUNT; i += 1)
        {
            const uint32_t index = COUNT - i - 1;
            parts[i] = (CheckInitPart){
                .kind = CHECK_INIT_VALUE,
                .type = pointer,
                .bit_offset = (uint64_t)index * 64,
                .value = {
                    .type = pointer,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = 3 + (index & 1),
                    .offset = (int64_t)index * 4,
                },
            };
        }
        parts[COUNT] = (CheckInitPart){
            .kind = CHECK_INIT_CLEAR,
            .type = array,
            .bit_offset = (uint64_t)(COUNT / 4) * 64,
            .byte_count = (uint64_t)(COUNT / 2) * 8,
        };
        const CheckInitializer initializer = {
            .type = array,
            .parts = parts,
            .count = COUNT + 1,
        };
        const CheckDataObject object = {
            .type = array,
            .object = 1,
            .initializer = BUSTER_CHECK_NONE,
            .initializer_plan = &initializer,
        };
        uint32_t* references = 0;
        uint32_t count = 0;
        const CheckInitStatus status = check_initializer_references(types, &initializer, &fixture.bytes, &fixture.scratch, &references, &count);
        check_test_expect(test, status == CHECK_INIT_OK && count == COUNT / 2 && references[0] == 4 && references[count - 1] == 3,
                          "reverse designated reference coverage removes the exact overwritten range and preserves source order");
        arena_pop_to(&fixture.bytes, 0);
        CheckDataResult result = { 0 };
        const bool emitted = check_data_emit(types, 0, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0, &fixture.bytes, &fixture.scratch, &result);
        check_test_expect(test, emitted && result.fixup_count == COUNT / 2 && link_check(&fixture.link) == LINK_ERROR_NONE,
                          "reverse designated reference emission publishes a valid complete atom");
        if (emitted)
        {
            const AtomId atom = fixture.link.symbols.atom[map[1]];
            const FixupId first = fixture.link.atoms.fixup_begin[atom];
            for (uint32_t i = 0; i < COUNT / 2; i += 1)
            {
                const uint32_t index = i < COUNT / 4 ? i : i + COUNT / 2;
                check_test_expect(test, fixture.link.fixups.offset[first + i] == index * 8 &&
                                       fixture.link.fixups.target[first + i] == map[3 + (index & 1)] &&
                                       link_fixup_addend(&fixture.link, first + i) == (int64_t)index * 4,
                                  "reverse designated final relocation order and identity are exact");
            }
        }
    }
    check_data_test_close(&fixture);
    arena_release(&parts_arena);
}

static void check_data_test_address_bit_fields(CheckTest* test, TypeTable* types)
{
    const TypeId array = type_array(types, BUSTER_TYPE_ULONG, 2);
    for (uint32_t variant = 0; variant < 3; variant += 1)
    {
        CheckDataTestLink fixture = { 0 };
        const bool ready = check_data_test_open(&fixture, 4);
        check_test_expect(test, ready, "symbolic integer bit-field fixture storage");
        if (ready)
        {
            SymbolId map[3] = { 0 };
            map[1] = check_data_test_symbol(&fixture.link, "bits", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            map[2] = check_data_test_symbol(&fixture.link, "target", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            CheckInitPart part = {
                .kind = CHECK_INIT_VALUE,
                .type = BUSTER_TYPE_ULONG,
                .bit_offset = variant == 2 ? 1 : 0,
                .bit_width = variant == 1 ? 63 : 64,
                .value = {
                    .type = BUSTER_TYPE_ULONG,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = 2,
                    .offset = 5,
                },
            };
            const CheckInitializer initializer = {
                .type = array,
                .parts = &part,
                .count = 1,
            };
            const CheckDataObject object = {
                .type = array,
                .object = 1,
                .initializer = BUSTER_CHECK_NONE,
                .initializer_plan = &initializer,
            };
            const Link before = fixture.link;
            CheckDataResult result = { 0 };
            const bool emitted = check_data_emit(types, 0, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                  &fixture.bytes, &fixture.scratch, &result);
            check_test_expect(test, !emitted && result.status == CHECK_DATA_INVALID && memcmp(&before, &fixture.link, sizeof(before)) == 0 &&
                                   fixture.link.symbols.atom[map[1]] == 0 && fixture.bytes.length == 0,
                              "symbolic bit-field fails without changing Link or publishing bytes");
            check_test_expect(test, fixture.scratch.length == 0, "symbolic bit-field planning restores scratch");
        }
        check_data_test_close(&fixture);
    }
}

static void check_data_test_complex(CheckTest* test, TypeTable* types)
{
    const TypeId complex_float = type_complex(types, BUSTER_TYPE_FLOAT);
    const TypeId complex_double = type_complex(types, BUSTER_TYPE_DOUBLE);
    const TypeId complex_long = type_complex(types, BUSTER_TYPE_LONG_DOUBLE);
    const TypeId complex_int = type_complex(types, BUSTER_TYPE_INT);
    const TypeId complex_half = type_complex(types, BUSTER_TYPE_FLOAT16);
    for (uint32_t failure = 0; failure < 17; failure += 1)
    {
        CheckDataTestLink fixture;
        const bool initialized = check_data_test_open(&fixture, 2);
        check_test_expect(test, initialized, "complex static-data refusal fixture initializes");
        if (initialized)
        {
            SymbolId map[3] = { 0 };
            map[1] = check_data_test_symbol(&fixture.link, "before_complex", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            map[2] = check_data_test_symbol(&fixture.link, "bad_complex", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            CheckValue values[2] = {
                check_test_integer(BUSTER_TYPE_INT, 71),
                {
                    .type = complex_float,
                    .category = CHECK_RVALUE,
                    .constant_kind = CHECK_CONSTANT_COMPLEX,
                    .floating = 1.5L,
                    .imaginary = -2.0L,
                },
            };
            CheckDiagnostic diagnostics[2] = { 0 };
            CheckStream stream = {
                .values = values,
                .diagnostics = diagnostics,
                .count = 2,
            };
            CheckDataObject objects[2] = {
                {
                    .type = BUSTER_TYPE_INT,
                    .object = 1,
                    .initializer = 0,
                    .token = 101,
                },
                {
                    .type = complex_float,
                    .object = 2,
                    .initializer = 1,
                    .token = 202,
                },
            };
            CheckDataStatus expected = CHECK_DATA_INVALID;
            switch (failure)
            {
                case 0: values[1].floating = 1.0L + 0x1p-25L; break;
                case 1: values[1].imaginary = 1.0L + 0x1p-25L; break;
                case 2:
                    values[1].type = objects[1].type = complex_double;
                    values[1].imaginary = 1.0L + 0x1p-60L;
                    break;
                case 3:
                case 4:
                    values[1].type = objects[1].type = complex_int;
                    values[1].integer = (unsigned __int128)1 << (failure == 3 ? 32 : 96);
                    break;
                case 5:
                    values[1].constant_kind = CHECK_CONSTANT_FLOATING;
                    expected = CHECK_DATA_NON_CONSTANT;
                    break;
                case 6: values[1].type = BUSTER_TYPE_FLOAT; break;
                case 7: values[1].category = CHECK_LVALUE; break;
                case 8:
                    values[1].flags = BUSTER_CHECK_SIDE_EFFECT;
                    expected = CHECK_DATA_NON_CONSTANT;
                    break;
                case 9:
                    values[1].fault = CHECK_FAULT_DIVIDE_BY_ZERO;
                    expected = CHECK_DATA_NON_CONSTANT;
                    break;
                case 10:
                    values[1].type = objects[1].type = type_complex(types, BUSTER_TYPE_FLOAT128);
                    expected = CHECK_DATA_UNSUPPORTED;
                    break;
                case 11:
                    values[1].type = objects[1].type = type_complex(types, BUSTER_TYPE_INT128);
                    expected = CHECK_DATA_UNSUPPORTED;
                    break;
                case 12:
                    values[1].type = objects[1].type = complex_long;
                    fixture.bytes.capacity = 35;
                    expected = CHECK_DATA_RESOURCE;
                    break;
                case 13:
                    diagnostics[1] = CHECK_DIAG_INVALID_TYPE;
                    expected = CHECK_DATA_CHECK_FAILED;
                    break;
                case 14:
                    stream.status = CHECK_STATUS_INVALID_GRAPH;
                    expected = CHECK_DATA_CHECK_FAILED;
                    break;
                case 15:
                case 16:
                    values[1].type = objects[1].type = complex_half;
                    values[1].floating = failure == 15 ? 1.0L + 0x1p-12L : 1.0L;
                    values[1].imaginary = failure == 16 ? 1.0L + 0x1p-12L : 1.0L;
                    break;
            }
            uint8_t tables[4096];
            const bool fits = fixture.tables.length <= sizeof(tables);
            check_test_expect(test, fits, "complex refusal table snapshot fits");
            if (fits)
            {
                memcpy(tables, fixture.tables.base, fixture.tables.length);
                const Link before = fixture.link;
                CheckDataResult result = { 0 };
                const bool emitted = check_data_emit(types, &stream, objects, BUSTER_ARRAY_LENGTH(objects), map, BUSTER_ARRAY_LENGTH(map),
                                                     &fixture.link, 0, &fixture.bytes, &fixture.scratch, &result);
                check_test_expect(test, !emitted && result.status == expected && memcmp(&before, &fixture.link, sizeof(before)) == 0 &&
                                       memcmp(tables, fixture.tables.base, fixture.tables.length) == 0 && fixture.bytes.length == 0 && fixture.scratch.length == 0,
                                  "invalid complex values and exhausted output leave the entire static batch unpublished");
            }
        }
        fixture.bytes.capacity = 16384;
        check_data_test_close(&fixture);
    }
    CheckDataTestLink fixture;
    const bool initialized = check_data_test_open(&fixture, 1);
    check_test_expect(test, initialized, "complex host-padding fixture initializes");
    if (initialized)
    {
        SymbolId map[2] = { 0 };
        map[1] = check_data_test_symbol(&fixture.link, "complex_padding", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        CheckValue value;
        memset(&value, 0xa5, sizeof(value));
        value.type = complex_long;
        value.category = CHECK_RVALUE;
        value.constant_kind = CHECK_CONSTANT_COMPLEX;
        value.flags = 0;
        value.fault = CHECK_FAULT_NONE;
        value.floating = 1.5L;
        value.imaginary = -2.0L;
        CheckDiagnostic diagnostic = CHECK_DIAG_NONE;
        const CheckStream stream = {
            .values = &value,
            .diagnostics = &diagnostic,
            .count = 1,
        };
        const CheckDataObject object = {
            .type = complex_long,
            .object = 1,
            .initializer = 0,
        };
        CheckDataResult result = { 0 };
        static const uint8_t expected[32] = {
            [7] = 0xc0,
            [8] = 0xff,
            [9] = 0x3f,
            [23] = 0x80,
            [25] = 0xc0,
        };
        const bool emitted = check_data_emit(types, &stream, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                             &fixture.bytes, &fixture.scratch, &result);
        const AtomId atom = fixture.link.symbols.atom[map[1]];
        check_test_expect(test, emitted && atom != 0 && fixture.link.atoms.size[atom] == 32 &&
                               memcmp(fixture.link.atoms.bytes[atom], expected, sizeof(expected)) == 0 && link_check(&fixture.link) == LINK_ERROR_NONE,
                          "both x87 complex components serialize ten meaningful bytes and six zero padding bytes despite poisoned host storage");
    }
    check_data_test_close(&fixture);
    static const long double real[5] = { 1.5L, -0.0L, 0x1p-24L, INFINITY, NAN };
    static const long double imaginary[5] = { -2.0L, 0.0L, -0x1p-24L, -INFINITY, -NAN };
    static const uint8_t expected_half[5][4] = {
        { 0x00, 0x3e, 0x00, 0xc0 },
        { 0x00, 0x80, 0x00, 0x00 },
        { 0x01, 0x00, 0x01, 0x80 },
        { 0x00, 0x7c, 0x00, 0xfc },
        { 0x00, 0x7e, 0x00, 0xfe },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(real); i += 1)
    {
        const bool half_ready = check_data_test_open(&fixture, 1);
        check_test_expect(test, half_ready, "complex binary16 static-data fixture initializes");
        if (half_ready)
        {
            SymbolId map[2] = { 0 };
            map[1] = check_data_test_symbol(&fixture.link, "complex_half", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            CheckValue value = {
                .type = complex_half,
                .category = CHECK_RVALUE,
                .constant_kind = CHECK_CONSTANT_COMPLEX,
                .floating = real[i],
                .imaginary = imaginary[i],
            };
            CheckDiagnostic diagnostic = CHECK_DIAG_NONE;
            const CheckStream stream = { .values = &value, .diagnostics = &diagnostic, .count = 1 };
            const CheckDataObject object = { .type = complex_half, .object = 1, .initializer = 0 };
            CheckDataResult result = { 0 };
            const bool emitted = check_data_emit(types, &stream, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                 &fixture.bytes, &fixture.scratch, &result);
            const AtomId atom = fixture.link.symbols.atom[map[1]];
            check_test_expect(test, emitted && atom != 0 && fixture.link.atoms.size[atom] == 4 &&
                                   memcmp(fixture.link.atoms.bytes[atom], expected_half[i], 4) == 0 && link_check(&fixture.link) == LINK_ERROR_NONE,
                              "complex binary16 constants serialize both parts' exact bytes, including signed zero, subnormals, infinities and canonical NaNs");
        }
        check_data_test_close(&fixture);
    }
}

static void check_data_test_zero_bits(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool ready = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, ready, "zero classification guarded storage");
    for (uint32_t size = 0; ready && size <= 257; size += 1)
    {
        uint8_t* bytes = mapping + page - size;
        memset(bytes, 0, size);
        check_test_expect(test, !check_data_nonzero_bits(bytes, 0, (uint64_t)size * 8, false) &&
                               !check_data_nonzero_bits(bytes, 0, (uint64_t)size * 8, true),
                          "zero byte scans agree at every vector tail and guarded end");
        for (uint32_t tail = 0; size != 0 && tail < 8; tail += 1)
        {
            for (uint32_t head = 0; head < 8; head += 1)
            {
                const uint64_t end = (uint64_t)size * 8 - tail;
                const uint64_t begin = head < end ? head : end;
                const uint64_t positions[] = { 0, begin, end - 1, (uint64_t)size * 8 - 1 };
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(positions); i += 1)
                {
                    const uint64_t bit = positions[i];
                    bytes[bit / 8] = (uint8_t)(1u << (bit % 8));
                    const bool expected = bit >= begin && bit < end;
                    check_test_expect(test, check_data_nonzero_bits(bytes, begin, end, false) == expected &&
                                           check_data_nonzero_bits(bytes, begin, end, true) == expected,
                                      "scalar/vector zero scans preserve partial-byte ownership");
                    bytes[bit / 8] = 0;
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
}

static void check_data_test_symbolic_zero(CheckTest* test, TypeTable* types)
{
    const uint64_t size = UINT64_C(1) << 30;
    const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, size);
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    const uint8_t copied[] = { 0, 0, 0x80, 0, 0, 0, 0, 0 };
    for (uint32_t variant = 0; variant < 16; variant += 1)
    {
        CheckDataTestLink fixture = { 0 };
        const bool ready = check_data_test_open(&fixture, 4);
        check_test_expect(test, ready, "symbolic zero fixture");
        if (ready)
        {
            SymbolId map[3] = { 0 };
            const bool tls = (variant % 2) != 0;
            map[1] = check_data_test_symbol(&fixture.link, "huge_zero", BUSTER_LINK_SYMBOL_GLOBAL | (tls ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT));
            CheckInitPart parts[3] = {
                {
                    .kind = CHECK_INIT_CLEAR,
                    .type = array,
                    .byte_count = size,
                },
                {
                    .kind = CHECK_INIT_VALUE,
                    .type = BUSTER_TYPE_UCHAR,
                    .bit_offset = (size - 1) * 8,
                    .value = check_test_integer(BUSTER_TYPE_UCHAR, 0),
                },
                {
                    .kind = CHECK_INIT_CLEAR,
                    .type = BUSTER_TYPE_UCHAR,
                    .bit_offset = (size - 1) * 8,
                    .byte_count = 1,
                },
            };
            const CheckInitializer initializer = {
                .type = array,
                .parts = parts,
                .count = variant < 2 ? 2 : BUSTER_ARRAY_LENGTH(parts),
            };
            const CheckDataObject object = {
                .type = array,
                .object = 1,
                .initializer = BUSTER_CHECK_NONE,
                .initializer_plan = &initializer,
                .alignment_log2 = 6,
            };
            if (variant >= 2 && variant < 4)
            {
                parts[1].value = check_test_integer(BUSTER_TYPE_UCHAR, 255);
                parts[2] = parts[1];
                parts[2].value.integer = 0;
            }
            else if (variant >= 4 && variant < 6)
            {
                parts[1].kind = CHECK_INIT_BYTES;
                parts[1].bit_offset = (size - sizeof(copied)) * 8;
                parts[1].bytes = copied;
                parts[1].byte_count = sizeof(copied);
                parts[2].bit_offset = parts[1].bit_offset + 2 * 8 + 7;
                parts[2].bit_width = 1;
            }
            else if (variant >= 6 && variant < 8)
            {
                parts[1].type = BUSTER_TYPE_UINT;
                parts[1].bit_offset = (size - 2) * 8 + 5;
                parts[1].bit_width = 9;
                parts[1].value = check_test_integer(BUSTER_TYPE_UINT, 511);
                parts[2].bit_offset = parts[1].bit_offset;
                parts[2].bit_width = 9;
            }
            else if (variant >= 8 && variant < 10)
            {
                parts[1].type = pointer;
                parts[1].bit_offset = (size - 8) * 8;
                parts[1].value = (CheckValue){
                    .type = pointer,
                    .category = CHECK_RVALUE,
                    .flags = BUSTER_CHECK_STATIC_OBJECT,
                    .constant_kind = CHECK_CONSTANT_ADDRESS,
                    .object = 2,
                    .offset = 17,
                };
                parts[2].bit_offset = parts[1].bit_offset;
                parts[2].byte_count = 8;
            }
            else if (variant >= 10)
            {
                parts[1].value.integer = 1;
                parts[2] = parts[0];
            }
            assert(arena_commit(&fixture.scratch, fixture.scratch.capacity));
            memset(fixture.scratch.base, 0x5a, fixture.scratch.capacity);
            CheckDataStatus expected = CHECK_DATA_OK;
            if (variant == 12 || variant == 13)
            {
                fixture.scratch.capacity = variant == 12 ? 64 : 128;
                expected = CHECK_DATA_RESOURCE;
            }
            else if (variant == 14)
            {
                parts[1].value.flags |= BUSTER_CHECK_SIDE_EFFECT;
                expected = CHECK_DATA_NON_CONSTANT;
            }
            else if (variant == 15)
            {
                parts[1].bit_offset = size * 8;
                expected = CHECK_DATA_INVALID;
            }
            const Link previous = fixture.link;
            uint8_t previous_tables[4096];
            const bool fits = fixture.tables.length <= sizeof(previous_tables);
            check_test_expect(test, fits, "symbolic zero rollback snapshot fits");
            if (fits)
            {
                memcpy(previous_tables, fixture.tables.base, fixture.tables.length);
            }
            CheckDataResult result = { 0 };
            const bool emitted = check_data_emit(types, 0, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                 &fixture.bytes, &fixture.scratch, &result);
            const AtomId atom = fixture.link.symbols.atom[map[1]];
            if (expected == CHECK_DATA_OK)
            {
                check_test_expect(test, emitted && result.byte_count == 0 && result.fixup_count == 0 && result.atom_count == 1 &&
                                   fixture.bytes.length == 0 && fixture.scratch.length == 0 && link_check(&fixture.link) == LINK_ERROR_NONE &&
                                   atom != 0 && fixture.link.atoms.size[atom] == size && fixture.link.atoms.bytes[atom] == 0 &&
                                   fixture.link.atoms.class[atom] == (tls ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_BSS) &&
                                   fixture.link.atoms.alignment_log2[atom] == 6 && fixture.link.symbols.size[map[1]] == size,
                              "one-GiB zero aggregate preserves size/alignment/symbol/TLS with only 64-KiB scratch");
            }
            else
            {
                check_test_expect(test, !emitted && result.status == expected && fits &&
                                       memcmp(&previous, &fixture.link, sizeof(previous)) == 0 &&
                                       memcmp(previous_tables, fixture.tables.base, fixture.tables.length) == 0 &&
                                       fixture.bytes.length == 0 && fixture.scratch.length == 0,
                                  "zero-plan metadata exhaustion and invalid overwritten writes leave all publication state unchanged");
            }
            fixture.scratch.capacity = 65536;
        }
        check_data_test_close(&fixture);
    }
}

static void check_data_test_zero_batch(CheckTest* test, TypeTable* types)
{
    enum { COUNT = 1024 };
    const uint64_t size = UINT64_C(1) << 30;
    const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, size);
    CheckInitPart parts[2] = {
        {
            .kind = CHECK_INIT_CLEAR,
            .type = array,
            .byte_count = size,
        },
        {
            .kind = CHECK_INIT_VALUE,
            .type = BUSTER_TYPE_UCHAR,
            .value = check_test_integer(BUSTER_TYPE_UCHAR, 0),
        },
    };
    const CheckInitializer initializer = {
        .type = array,
        .parts = parts,
        .count = BUSTER_ARRAY_LENGTH(parts),
    };
    CheckDataObject objects[COUNT] = { 0 };
    SymbolId map[COUNT + 1] = { 0 };
    CheckDataTestLink fixture = { 0 };
    bool ready = check_data_test_open(&fixture, COUNT + 2);
    for (uint32_t i = 0; ready && i < COUNT; i += 1)
    {
        char name[32];
        snprintf(name, sizeof(name), "zero_%u", i);
        map[i + 1] = check_data_test_symbol(&fixture.link, name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        ready = map[i + 1] != 0;
        objects[i] = (CheckDataObject){
            .type = array,
            .object = i + 1,
            .initializer = BUSTER_CHECK_NONE,
            .initializer_plan = &initializer,
        };
    }
    check_test_expect(test, ready, "zero batch fixture symbols and names");
    if (ready)
    {
        CheckDataResult result = { 0 };
        const bool emitted = check_data_emit(types, 0, objects, COUNT, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                             &fixture.bytes, &fixture.scratch, &result);
        check_test_expect(test, emitted && result.atom_count == COUNT && result.byte_count == 0 &&
                               fixture.bytes.length == 0 && fixture.scratch.length == 0 && link_check(&fixture.link) == LINK_ERROR_NONE,
                          "1024 one-GiB BSS objects release unused write caches inside a 64-KiB scratch batch");
    }
    check_data_test_close(&fixture);
}

static void check_data_test_boundary_sort(CheckTest* test)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mappings[2];
    bool ready = true;
    for (uint32_t i = 0; i < 2; i += 1)
    {
        mappings[i] = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ready &= mappings[i] != MAP_FAILED && mprotect(mappings[i] + page, page, PROT_NONE) == 0;
    }
    check_test_expect(test, ready, "boundary sort guarded storage");
    for (uint32_t shape = 0; ready && shape < 6; shape += 1)
    {
        for (uint32_t count = 0; count <= 200; count += 1)
        {
            uint64_t* keys = (uint64_t*)(mappings[0] + page) - count;
            uint64_t* temporary = (uint64_t*)(mappings[1] + page) - count;
            uint64_t scalar[200];
            uint64_t scalar_temporary[200];
            uint64_t expected[200];
            for (uint32_t i = 0; i < count; i += 1)
            {
                uint64_t value = ((uint64_t)check_test_random(test) << 32) | check_test_random(test);
                value = shape == 1 ? value & UINT32_MAX : shape == 2 ? value << 32 : shape == 3 ? UINT64_MAX :
                        shape == 4 ? count - i : shape == 5 ? (i % 2 == 0 ? UINT64_MAX : 0) : value;
                keys[i] = scalar[i] = value;
                uint32_t at = i;
                while (at != 0 && expected[at - 1] > value)
                {
                    expected[at] = expected[at - 1];
                    at -= 1;
                }
                expected[at] = value;
            }
            const uint64_t* vector = check_data_sort_boundaries(keys, temporary, count, false);
            const uint64_t* twin = check_data_sort_boundaries(scalar, scalar_temporary, count, true);
            check_test_expect(test, memcmp(vector, expected, (uint64_t)count * 8) == 0 && memcmp(twin, expected, (uint64_t)count * 8) == 0,
                              "64-bit boundary sort matches independent insertion oracle and scalar twin at guarded tails");
        }
    }
    for (uint32_t i = 0; i < 2; i += 1)
    {
        if (mappings[i] != MAP_FAILED)
        {
            munmap(mappings[i], page * 2);
        }
    }
}

static void check_data_test_dense_cache(CheckTest* test, TypeTable* types)
{
    enum { COUNT = 8, SIZE = 256 };
    const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, SIZE);
    CheckInitPart parts[SIZE];
    uint8_t expected[SIZE];
    for (uint32_t i = 0; i < SIZE; i += 1)
    {
        // Eight final zero writes keep this on the cached path even when the bounded direct-writer proof exists.
        expected[i] = i + BUSTER_CHECK_DATA_WITNESS_PARTS >= SIZE ? 0 : (uint8_t)(i + 1);
        parts[i] = (CheckInitPart){
            .kind = CHECK_INIT_VALUE,
            .type = BUSTER_TYPE_UCHAR,
            .bit_offset = (uint64_t)i * 8,
            .value = check_test_integer(BUSTER_TYPE_UCHAR, expected[i]),
        };
    }
    const CheckInitializer initializer = { .type = array, .parts = parts, .count = SIZE };
    CheckDataObject objects[COUNT];
    SymbolId map[COUNT + 1] = { 0 };
    CheckDataTestLink fixture = { 0 };
    bool ready = check_data_test_open(&fixture, COUNT + 2);
    for (uint32_t i = 0; ready && i < COUNT; i += 1)
    {
        char name[32];
        snprintf(name, sizeof(name), "dense_%u", i);
        map[i + 1] = check_data_test_symbol(&fixture.link, name, BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
        ready = map[i + 1] != 0;
        objects[i] = (CheckDataObject){
            .type = array,
            .object = i + 1,
            .initializer = BUSTER_CHECK_NONE,
            .initializer_plan = &initializer,
        };
    }
    check_test_expect(test, ready, "dense byte-array cache fixture");
    if (ready)
    {
        fixture.scratch.capacity = 4096;
        CheckDataResult result = { 0 };
        bool valid = check_data_emit(types, 0, objects, COUNT, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                     &fixture.bytes, &fixture.scratch, &result);
        for (uint32_t i = 0; valid && i < COUNT; i += 1)
        {
            const AtomId atom = fixture.link.symbols.atom[map[i + 1]];
            valid = atom != 0 && fixture.link.atoms.class[atom] == BUSTER_LINK_CLASS_DATA &&
                    memcmp(fixture.link.atoms.bytes[atom], expected, SIZE) == 0;
        }
        check_test_expect(test, valid && result.byte_count == COUNT * SIZE && fixture.scratch.length == 0,
                          "dense byte objects use compact scalar slots and release each cache before the next object within 4-KiB scratch");
        fixture.scratch.capacity = 65536;
    }
    check_data_test_close(&fixture);
}

static void check_data_test_direct_writes(CheckTest* test, TypeTable* types)
{
    const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, 16);
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    for (uint32_t mode = 0; mode < 6; mode += 1)
    {
        CheckDataTestLink fixture = { 0 };
        const bool ready = check_data_test_open(&fixture, 4);
        check_test_expect(test, ready, "direct validated write fixture");
        if (ready)
        {
            SymbolId map[3] = { 0 };
            map[1] = check_data_test_symbol(&fixture.link, "direct", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            CheckInitPart parts[] = {
                { .kind = CHECK_INIT_CLEAR, .type = array, .byte_count = 16 },
                { .kind = CHECK_INIT_VALUE, .type = BUSTER_TYPE_UCHAR, .value = check_test_integer(BUSTER_TYPE_UCHAR, mode == 1 ? 0 : 17) },
                { .kind = CHECK_INIT_VALUE, .type = BUSTER_TYPE_UCHAR, .bit_offset = 8, .value = check_test_integer(BUSTER_TYPE_UCHAR, 18) },
                { .kind = CHECK_INIT_VALUE, .type = BUSTER_TYPE_UCHAR, .bit_offset = 8, .value = check_test_integer(BUSTER_TYPE_UCHAR, 0) },
            };
            if (mode == 2 || mode == 3)
            {
                parts[1] = (CheckInitPart){
                    .kind = CHECK_INIT_VALUE,
                    .type = pointer,
                    .bit_offset = 64,
                    .value = {
                        .type = pointer,
                        .category = CHECK_RVALUE,
                        .flags = BUSTER_CHECK_STATIC_OBJECT,
                        .constant_kind = CHECK_CONSTANT_ADDRESS,
                        .object = 2,
                    },
                };
                parts[2] = (CheckInitPart){ .kind = CHECK_INIT_CLEAR, .type = pointer, .bit_offset = 64, .byte_count = 8 };
                parts[3] = (CheckInitPart){ .kind = CHECK_INIT_VALUE, .type = BUSTER_TYPE_UCHAR, .value = check_test_integer(BUSTER_TYPE_UCHAR, 17) };
                if (mode == 3)
                {
                    parts[2].byte_count = 0;
                    map[2] = check_data_test_symbol(&fixture.link, "target", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
                }
            }
            else if (mode >= 4)
            {
                parts[1].value.flags |= BUSTER_CHECK_SIDE_EFFECT;
                parts[3].bit_offset = 15 * 8;
                parts[3].value.integer = 19;
                if (mode == 5)
                {
                    fixture.bytes.capacity = 0;
                }
            }
            const CheckInitializer initializer = { .type = array, .parts = parts, .count = BUSTER_ARRAY_LENGTH(parts) };
            const CheckDataObject object = { .type = array, .object = 1, .initializer = BUSTER_CHECK_NONE, .initializer_plan = &initializer };
            const Link previous = fixture.link;
            uint8_t tables[4096];
            const bool fits = fixture.tables.length <= sizeof(tables);
            if (fits)
            {
                memcpy(tables, fixture.tables.base, fixture.tables.length);
            }
            CheckDataResult result = { 0 };
            const bool emitted = check_data_emit(types, 0, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                 &fixture.bytes, &fixture.scratch, &result);
            const AtomId atom = fixture.link.symbols.atom[map[1]];
            if (mode < 4)
            {
                uint8_t expected[16] = { 0 };
                expected[0] = mode == 1 ? 0 : 17;
                const uint8_t class = mode == 1 ? BUSTER_LINK_CLASS_BSS : BUSTER_LINK_CLASS_DATA;
                check_test_expect(test, emitted && atom != 0 && fixture.link.atoms.class[atom] == class &&
                                       (mode == 1 ? fixture.link.atoms.bytes[atom] == 0 : memcmp(fixture.link.atoms.bytes[atom], expected, 16) == 0) &&
                                       result.fixup_count == (mode == 3 ? 1u : 0u) && fixture.scratch.length == 0 &&
                                       link_check(&fixture.link) == LINK_ERROR_NONE,
                                  "direct/cached writes preserve overwritten integer and relocation bytes, including final zero classification");
            }
            else
            {
                check_test_expect(test, !emitted && result.status == CHECK_DATA_NON_CONSTANT && fits &&
                                       memcmp(&previous, &fixture.link, sizeof(previous)) == 0 &&
                                       memcmp(tables, fixture.tables.base, fixture.tables.length) == 0 &&
                                       fixture.bytes.length == 0 && fixture.scratch.length == 0,
                                  "direct admission and insufficient retained capacity preserve source-order diagnostics and complete rollback");
            }
            fixture.bytes.capacity = 16384;
        }
        check_data_test_close(&fixture);
    }
}

static void check_data_test_overwrite_oracle(CheckTest* test, TypeTable* types)
{
    enum { SIZE = 96, COUNT = 33 };
    const TypeId array = type_array(types, BUSTER_TYPE_UCHAR, SIZE);
    for (uint32_t run = 0; run < 128; run += 1)
    {
        CheckDataTestLink fixture = { 0 };
        const bool ready = check_data_test_open(&fixture, 4);
        check_test_expect(test, ready, "overwrite oracle fixture");
        if (ready)
        {
            SymbolId map[2] = { 0 };
            map[1] = check_data_test_symbol(&fixture.link, "overwrites", BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL);
            CheckInitPart parts[COUNT] = { 0 };
            uint8_t copied[COUNT][SIZE] = { 0 };
            uint8_t expected[SIZE] = { 0 };
            for (uint32_t i = 0; i < COUNT; i += 1)
            {
                const bool clear = i + 1 == COUNT || (check_test_random(test) % 4) == 0;
                const bool bytes = !clear && (check_test_random(test) % 2) == 0;
                const uint32_t begin = i + 1 == COUNT && run % 2 == 0 ? 0 : check_test_random(test) % (SIZE * 8);
                const uint32_t available = SIZE * 8 - begin;
                const uint32_t width = i + 1 == COUNT && run % 2 == 0 ? available : 1 + check_test_random(test) % (available < 32 ? available : 32);
                parts[i] = (CheckInitPart){
                    .kind = clear ? CHECK_INIT_CLEAR : bytes ? CHECK_INIT_BYTES : CHECK_INIT_VALUE,
                    .type = BUSTER_TYPE_UINT,
                    .bit_offset = bytes ? begin / 8 * 8 : begin,
                    .bit_width = bytes ? 0 : width,
                    .bytes = copied[i],
                    .byte_count = bytes ? (width + 7) / 8 : 0,
                    .value = check_test_integer(BUSTER_TYPE_UINT, check_test_random(test)),
                };
                const uint32_t start = (uint32_t)parts[i].bit_offset;
                const uint32_t bits = bytes ? (uint32_t)parts[i].byte_count * 8 : width;
                for (uint32_t bit = 0; bit < bits; bit += 1)
                {
                    const bool set = !clear && (bytes ? (check_test_random(test) % 2) != 0 : ((parts[i].value.integer >> bit) & 1) != 0);
                    copied[i][bit / 8] |= (uint8_t)(set << (bit % 8));
                    const uint8_t mask = (uint8_t)(1u << ((start + bit) % 8));
                    expected[(start + bit) / 8] = (uint8_t)((expected[(start + bit) / 8] & ~mask) | (set ? mask : 0));
                }
            }
            const bool readonly = (run % 4) == 0;
            const CheckInitializer initializer = {
                .type = readonly ? type_qualify(types, array, BUSTER_TYPE_CONST) : array,
                .parts = parts,
                .count = COUNT,
            };
            const CheckDataObject object = {
                .type = initializer.type,
                .object = 1,
                .initializer = BUSTER_CHECK_NONE,
                .initializer_plan = &initializer,
            };
            uint8_t nonzero = 0;
            for (uint32_t i = 0; i < SIZE; i += 1)
            {
                nonzero |= expected[i];
            }
            CheckDataResult result = { 0 };
            const bool emitted = check_data_emit(types, 0, &object, 1, map, BUSTER_ARRAY_LENGTH(map), &fixture.link, 0,
                                                 &fixture.bytes, &fixture.scratch, &result);
            const AtomId atom = fixture.link.symbols.atom[map[1]];
            const uint8_t class = readonly ? BUSTER_LINK_CLASS_RODATA : nonzero != 0 ? BUSTER_LINK_CLASS_DATA : BUSTER_LINK_CLASS_BSS;
            check_test_expect(test, emitted && atom != 0 && fixture.link.atoms.class[atom] == class &&
                                   (link_class_is_zero_fill(class) ? fixture.link.atoms.bytes[atom] == 0 :
                                    memcmp(fixture.link.atoms.bytes[atom], expected, SIZE) == 0) && fixture.scratch.length == 0,
                              "symbolic final-byte classification matches independent bit-store oracle including zero RODATA");
        }
        check_data_test_close(&fixture);
    }
}
#include "c_check_data_bit_field_test.c"

static __attribute__((noinline)) void check_test_data(CheckTest* test, TypeTable* types, const char* clang)
{
    check_data_test_objects(test, types, clang);
    check_data_test_nonzero(test);
    check_data_test_retained_payloads(test, types);
    check_data_test_aggregate_transaction(test, types);
    check_data_test_many_fixups(test, types);
    check_data_test_address_bit_fields(test, types);
    check_data_test_bit_fields(test, types);
    check_data_test_complex(test, types);
    check_data_test_limits(test, types);
    check_data_test_floating(test);
    check_data_test_composite(test, types);
    check_data_test_partitions(test, types);
    check_data_test_zero_bits(test);
    check_data_test_symbolic_zero(test, types);
    check_data_test_zero_batch(test, types);
    check_data_test_boundary_sort(test);
    check_data_test_dense_cache(test, types);
    check_data_test_direct_writes(test, types);
    check_data_test_overwrite_oracle(test, types);
}

#endif
