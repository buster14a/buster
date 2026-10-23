// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original source fixtures exercise the actual parser/adapter/planner/emitter path. Independently stated target
// bytes are checked in Link, the existing ELF writer, and Clang's external object output. Include after data tests.
#ifndef BUSTER_C_CHECK_INITIALIZER_TEST_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_TEST_INCLUDED

typedef struct CheckInitExpected CheckInitExpected;
struct CheckInitExpected
{
    const char* name;
    uint8_t bytes[64];
    uint32_t size;
    uint8_t class;
    uint8_t alignment_log2;
};

static SymbolId check_init_test_symbol(const Link* link, const char* name)
{
    SymbolId found = 0;
    const size_t size = strlen(name);
    for (SymbolId symbol = 1; symbol < link->symbols.count && found == 0; symbol += 1)
    {
        const NameId id = link->symbols.name[symbol];
        if (link->names.length[id] == size && memcmp(link->names.bytes[id], name, size) == 0)
        {
            found = symbol;
        }
    }
    return found;
}

static bool check_init_test_elf_bytes(const ObjectView* view, const CheckInitExpected* expected)
{
    const uint64_t symbol = check_data_test_find_symbol(view, expected->name);
    bool valid = symbol != 0;
    if (valid)
    {
        const Elf64_Sym* entry = view->symbols + symbol;
        const uint64_t section_id = object_view_symbol_section(view, symbol);
        valid = section_id != 0 && section_id < view->section_count && entry->st_size == expected->size;
        if (valid)
        {
            const Elf64_Shdr* section = view->sections + section_id;
            const bool zero = link_class_is_zero_fill(expected->class);
            const bool tls = link_class_is_tls(expected->class);
            valid = (section->sh_type == SHT_NOBITS) == zero && ((section->sh_flags & SHF_TLS) != 0) == tls &&
                    entry->st_value <= section->sh_size && expected->size <= section->sh_size - entry->st_value;
            if (valid && expected->alignment_log2 != 0)
            {
                const uint64_t alignment = UINT64_C(1) << expected->alignment_log2;
                valid = section->sh_addralign >= alignment && entry->st_value % alignment == 0;
            }
            if (valid && !zero)
            {
                valid = memcmp(view->file + section->sh_offset + entry->st_value, expected->bytes, expected->size) == 0;
            }
        }
    }
    return valid;
}

static bool check_init_test_elf_reference(const ObjectView* view, const char* name, uint32_t offset, const char* target, int64_t addend)
{
    const uint64_t symbol = check_data_test_find_symbol(view, name);
    const uint64_t destination = check_data_test_find_symbol(view, target);
    const uint64_t section = symbol != 0 ? object_view_symbol_section(view, symbol) : 0;
    uint32_t found = 0;
    for (uint64_t i = 1; symbol != 0 && destination != 0 && i < view->section_count; i += 1)
    {
        const Elf64_Shdr* header = view->sections + i;
        if (header->sh_type == SHT_RELA && header->sh_info == section)
        {
            const Elf64_Rela* entries = (const Elf64_Rela*)(view->file + header->sh_offset);
            for (uint64_t j = 0; j < header->sh_size / sizeof(*entries); j += 1)
            {
                found += entries[j].r_offset == view->symbols[symbol].st_value + offset &&
                         ELF64_R_SYM(entries[j].r_info) == destination && ELF64_R_TYPE(entries[j].r_info) == R_X86_64_64 &&
                         entries[j].r_addend == addend;
            }
        }
    }
    return found == 1;
}

static void check_init_test_objects(CheckTest* test, Link* link, const CheckInitExpected* expected, uint32_t count,
                                    const char* source, const char* clang)
{
    check_test_expect(test, link_check(link) == LINK_ERROR_NONE, "initializer source produces a valid Link");
    for (uint32_t i = 0; i < count; i += 1)
    {
        const SymbolId symbol = check_init_test_symbol(link, expected[i].name);
        const AtomId atom = symbol != 0 ? link->symbols.atom[symbol] : 0;
        const bool bytes = atom != 0 && (link_class_is_zero_fill(expected[i].class) ? link->atoms.bytes[atom] == 0 :
                           link->atoms.bytes[atom] != 0 && memcmp(link->atoms.bytes[atom], expected[i].bytes, expected[i].size) == 0);
        char description[192];
        snprintf(description, sizeof(description), "initializer %s has independent size, section and target bytes", expected[i].name);
        check_test_expect(test, bytes && link->atoms.size[atom] == expected[i].size && link->atoms.class[atom] == expected[i].class &&
                               (expected[i].alignment_log2 == 0 || link->atoms.alignment_log2[atom] == expected[i].alignment_log2), description);
    }
    uint32_t strings = 0;
    for (AtomId atom = 1; atom < link->atoms.count; atom += 1)
    {
        strings += link->atoms.class[atom] == BUSTER_LINK_CLASS_STRINGS;
    }
    if (check_init_test_symbol(link, "message") != 0)
    {
        check_test_expect(test, strings == 2, "only the two addressed byte string literals enter STRINGS");
    }
    const SymbolId switched = check_init_test_symbol(link, "switched");
    if (switched != 0)
    {
        check_test_expect(test, link->atoms.fixup_count[link->symbols.atom[switched]] == 0, "union member replacement removes an earlier symbolic relocation");
    }

    Arena arena = arena_reserve(UINT64_C(1) << 22);
    ObjectUnits units = { 0 };
    ObjectSymbols symbols = { 0 };
    ObjectView ours = { 0 };
    Object object = { 0 };
    char error[512] = "";
    bool built = object_prepare(&units, link, 1, &arena) && object_map_symbols(&symbols, link, &arena);
    if (built)
    {
        const ObjectOptions options = { 0 };
        object = object_build(link, &units, &symbols, 0, options, BUSTER_S8("initializers.c"), &arena);
        built = object.error == OBJECT_ERROR_NONE && object_view_open(&ours, object.bytes, object.size, false, error, sizeof(error));
    }
    check_test_expect(test, built, "aggregate/string/TLS initializer ELF writer and independent reader");
    for (uint32_t i = 0; built && i < count; i += 1)
    {
        check_test_expect(test, check_init_test_elf_bytes(&ours, expected + i), "initializer ELF symbol matches independently stated bytes and TLS section kind");
    }
    if (built && check_init_test_symbol(link, "pointers") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&ours, "pointers", 0, "target", 0) &&
                               check_init_test_elf_reference(&ours, "pointers", 16, "target", 4) &&
                               check_init_test_elf_reference(&ours, "relocated", 0, "target", 3) &&
                               check_init_test_elf_reference(&ours, "tls_pointer", 0, "target", 0),
                          "aggregate and TLS ELF references preserve subobject offsets, object identity and addends");
    }
    if (built && check_init_test_symbol(link, "range_refs") != 0)
    {
        const AtomId atom = link->symbols.atom[check_init_test_symbol(link, "range_refs")];
        check_test_expect(test, link->atoms.fixup_count[atom] == 2 &&
                               check_init_test_elf_reference(&ours, "range_refs", 0, "target", 0) &&
                               check_init_test_elf_reference(&ours, "range_refs", 24, "target", 0),
                          "overlapping range initialization retains exactly its two live pointer relocations");
    }
    if (built && check_init_test_symbol(link, "compound_reference") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&ours, "compound_reference", 0, "compound_target", 4),
                          "compound subobject copy translates its live pointer field and preserves the addend");
    }
    char directory[] = "/tmp/buster-initializer-XXXXXX";
    const bool directory_ready = clang != 0 && mkdtemp(directory) != 0;
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
    bool oracle = directory_ready && parse_test_write(source_path, source, strlen(source)) && object_test_run(arguments, output_path, error_path) == 0;
    ObjectView theirs = { 0 };
    if (oracle)
    {
        const String8 file = object_test_read_file(&arena, object_path);
        oracle = file.pointer != 0 && object_view_open(&theirs, (const uint8_t*)file.pointer, file.length, true, error, sizeof(error));
    }
    if (clang != 0)
    {
        check_test_expect(test, oracle, "independent Clang aggregate/string/TLS object oracle ran");
    }
    for (uint32_t i = 0; oracle && i < count; i += 1)
    {
        char description[192];
        snprintf(description, sizeof(description), "Clang confirms initializer bytes and section for %s", expected[i].name);
        check_test_expect(test, check_init_test_elf_bytes(&theirs, expected + i), description);
    }
    if (oracle && check_init_test_symbol(link, "pointers") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "pointers", 0, "target", 0) &&
                               check_init_test_elf_reference(&theirs, "pointers", 16, "target", 4) &&
                               check_init_test_elf_reference(&theirs, "relocated", 0, "target", 3) &&
                               check_init_test_elf_reference(&theirs, "tls_pointer", 0, "target", 0),
                          "Clang confirms aggregate and TLS absolute pointer references");
    }
    if (oracle && check_init_test_symbol(link, "range_refs") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "range_refs", 0, "target", 0) &&
                               check_init_test_elf_reference(&theirs, "range_refs", 24, "target", 0),
                          "Clang confirms live pointer relocations after overlapping GNU ranges");
    }
    if (oracle && check_init_test_symbol(link, "compound_reference") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "compound_reference", 0, "compound_target", 4),
                          "Clang confirms the relocated field copied from a compound subobject");
    }
    if (directory_ready)
    {
        unlink(source_path);
        unlink(object_path);
        unlink(output_path);
        unlink(error_path);
        rmdir(directory);
    }
    arena_release(&arena);
}

static void check_init_test_source(CheckTest* test, const char* source, const CheckInitExpected* expected, uint32_t count,
                                   bool accept, bool emit, const char* clang)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = {
            .standard = 2017,
            .gnu = true,
        },
        .no_standard_includes = true,
    };
    bool preprocessed = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers);
    preprocessed = preprocessed && pp_run_source(&pp, "initializer-test.c", (const uint8_t*)source, (uint32_t)strlen(source));
    check_test_expect(test, preprocessed, "initializer fixture preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 24);
        Arena stream_arena = arena_reserve(UINT64_C(1) << 24);
        Arena plans = arena_reserve(UINT64_C(1) << 22);
        Arena output = arena_reserve(UINT64_C(1) << 22);
        Arena bytes = arena_reserve(UINT64_C(1) << 22);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = { .scalar = path == 0 };
        bool ready = check_test_types_open(&types) && parse_initialize(&parser, &parse_options, pp_options.language);
        ready = ready && parse_translation_unit(&parser, &pp) && parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE;
        CheckInput input = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch);
        const CheckOptions options = { .scalar = path == 0 };
        CheckStream stream = { 0 };
        ready = ready && check_graph_run(&types.table, &input.graph, &stream_arena, &scratch, &options, &stream) && stream.report_count == 0;
        CheckInitializers initializers = { 0 };
        const bool checked = ready && check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &plans, &scratch);
        char description[256];
        snprintf(description, sizeof(description), "initializer %.24s: %s path accepts=%u expected=%u (input %u at %u, stream %u, plan %u at %u)", source,
                 path == 0 ? "scalar" : "vector", checked, accept, input.report_count,
                 input.report_count != 0 ? input.reports[0].token : BUSTER_CHECK_NONE, stream.report_count, initializers.status, initializers.token);
        check_test_expect(test, checked == accept, description);
        if (checked)
        {
            for (uint32_t i = 0; i < initializers.count; i += 1)
            {
                const uint32_t declaration = initializers.declaration_tokens[i];
                const uint32_t name = parser.tokens.names[declaration];
                if (intern_length(&identifiers, name) == 10 && memcmp(intern_bytes(&identifiers, name), "range_once", 10) == 0)
                {
                    const CheckInitializer* plan = initializers.plans + i;
                    bool shared = plan->count == 4;
                    for (uint32_t part = 0; shared && part < plan->count; part += 1)
                    {
                        shared = plan->parts[part].kind == CHECK_INIT_VALUE && plan->parts[part].node == plan->parts[0].node &&
                                 plan->parts[part].bit_offset == (uint64_t)part * 32;
                    }
                    check_test_expect(test, shared, "automatic GNU range retains one shared expression node for four distinct destinations");
                }
            }
            for (uint32_t i = 0; i < initializers.count; i += 1)
            {
                if (initializers.plans[i].count > 1)
                {
                    const uint32_t declaration = initializers.declaration_tokens[i];
                    Arena tiny = output;
                    tiny.capacity = sizeof(CheckInitPart);
                    CheckInitializer rejected = { 0 };
                    const bool partial = check_initializer_run(&types.table, &parser, input.token_nodes, &stream, input.strings, input.string_count,
                                                                input.declared_types[declaration], input.initializer_begins[declaration], input.initializer_ends[declaration],
                                                                input.static_objects[declaration] != 0, &tiny, &scratch, &rejected);
                    check_test_expect(test, !partial && rejected.status == CHECK_INIT_RESOURCE && rejected.parts == 0 && rejected.count == 0 && tiny.length == 0 && scratch.length == 0,
                                      "partially planned source initializer rolls back all parts on output exhaustion");
                    break;
                }
            }
        }
        if (checked && emit)
        {
            CheckDataInput data = { 0 };
            const bool emitted = check_data_input(&data, &input, &stream, &parser, &identifiers, &types.table, &output, &bytes, &scratch);
            snprintf(description, sizeof(description), "initializer source data adapter succeeds (status %u at %u)", data.status, data.token);
            check_test_expect(test, emitted, description);
            if (emitted)
            {
                check_init_test_objects(test, &data.link, expected, count, source, clang);
            }
        }
        check_test_expect(test, scratch.length == 0 && (checked || plans.length == 0), "initializer planning restores scratch and rolls back rejected output");
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&plans);
        arena_release(&stream_arena);
        arena_release(&input_arena);
        check_test_types_close(&types);
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_init_test_ranges(CheckTest* test, const char* clang)
{
    static const char source[] =
        "int range[8]={[1 ... 5]=9,[4 ... 6]=3};int inferred_range[]={[2 ... 4]=7,8};int range_extent=sizeof inferred_range;\n"
        "int matrix_range[3][4]={[0 ... 1][1 ... 2]=5,[1][2]=9};\n"
        "struct Pair{int a,b;} pairs[3]={[0 ... 2]={1,2},[1]={4}};\n"
        "struct Pair elided[3]={[0 ... 1]=1,2,3},partial[2]={[0].b=9,[0 ... 1]=1};\n"
        "int nested_ranges[3][4]={[0 ... 1]={[1 ... 2]=4},[1][2]=8};\n"
        "union U{struct{int a,b;}s;long n;} unions[2]={[0].s.a=3,[1].n=9,[0 ... 1].s.b=5};\n"
        "char range_text[4][3]={[0 ... 2]=\"ab\",[1]=\"x\"};\n"
        "int target;int *range_refs[4]={[0 ... 3]=&target,[1 ... 2]=0};\n"
        "_Thread_local int tls_range[3]={[0 ... 2]=4};\n";
    static const CheckInitExpected expected[] = {
        {
            .name = "range",
            .bytes = {
                [4] = 9,
                [8] = 9,
                [12] = 9,
                [16] = 3,
                [20] = 3,
                [24] = 3,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "inferred_range",
            .bytes = {
                [8] = 7,
                [12] = 7,
                [16] = 7,
                [20] = 8,
            },
            .size = 24,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "range_extent",
            .bytes = {
                24,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "matrix_range",
            .bytes = {
                [4] = 5,
                [8] = 5,
                [20] = 5,
                [24] = 9,
            },
            .size = 48,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "pairs",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 4,
                [16] = 1,
                [20] = 2,
            },
            .size = 24,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "elided",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 1,
                [12] = 2,
                [16] = 3,
            },
            .size = 24,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "partial",
            .bytes = {
                [0] = 1,
                [4] = 9,
                [8] = 1,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "nested_ranges",
            .bytes = {
                [4] = 4,
                [8] = 4,
                [20] = 4,
                [24] = 8,
            },
            .size = 48,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "unions",
            .bytes = {
                [0] = 3,
                [4] = 5,
                [12] = 5,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "range_text",
            .bytes = {
                [0] = 'a',
                [1] = 'b',
                [3] = 'x',
                [6] = 'a',
                [7] = 'b',
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "target",
            .size = 4,
            .class = BUSTER_LINK_CLASS_BSS,
        },
        {
            .name = "range_refs",
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "tls_range",
            .bytes = {
                [0] = 4,
                [4] = 4,
                [8] = 4,
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_TDATA,
        },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    check_init_test_source(test, "int f(void);int g(void){int range_once[4]={[0 ... 3]=f()};return range_once[2];}", 0, 0, true, false, clang);
    static const char* invalid[] = {
        "int a[3]={[2 ... 1]=7};",
        "int a[3]={[0 ... 3]=7};",
        "int a[3]={[-1 ... 1]=7};",
        "int n;int a[3]={[0 ... n]=7};",
        "int target;struct B{unsigned long field:63;} value={(unsigned long)&target};",
        "int target;struct B{unsigned long field:64;} value={(unsigned long)&target};",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        check_init_test_source(test, invalid[i], 0, 0, false, false, clang);
    }
}

static void check_test_initializers(CheckTest* test, const char* clang)
{
    check_init_test_ranges(test, clang);
    static const char source[] =
        "int target=17; int grid[][2]={1,2,3,4}; int extent=sizeof grid;\n"
        "int sparse[5]={[3]=7,[1]=9,4};\n"
        "struct Outer{char a;int grid[2][2];long b;} outer={1,2,3,4,5,6};\n"
        "struct Bits{unsigned a:3;signed b:5;unsigned c:10;} bits={5,-2,0x155};\n"
        "union U{struct {int a,b;} s;long n;int *p;} both={.s.a=1,.s.b=2},switched={.p=&target,.n=9};\n"
        "struct O{int a[3];int b;} replaced={.a={1,2,3},.a={9},.b=4};\n"
        "struct O continued={.a[1]=7,8,.b=9};int inferred[][2]={[2][1]=7,8};\n"
        "struct A{struct{int x,y;};int z;} anonymous={.y=2,3};int scalar={{{5}}};\n"
        "union U switched_back={.n=9,.s.b=2};union C{const char *p;long n;} dropped={.p=\"discarded\",.n=5};\n"
        "int *pointers[3]={[2]=&target+1,[0]=&target};\n"
        "unsigned long relocated=(unsigned long)&target+3;\n"
        "char text[]=\"a\" \"b\\0c\";char exact[2]={\"xy\"};char rows[][4]={\"ab\",\"c\"};unsigned short wide[]=u\"A\\U0001f600\";\n"
        "struct R{char c;int *p;} const readonly={.p=&target};\n"
        "const char *message=\"hi\";const char *other=\"bye\";\n"
        "_Thread_local int tls_zero;_Thread_local int tls_value=7;_Thread_local int *tls_pointer=&target;\n";
    static const CheckInitExpected expected[] = {
        {
            .name = "target",
            .bytes = {
                17,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "grid",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 3,
                [12] = 4,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "extent",
            .bytes = {
                16,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "sparse",
            .bytes = {
                [4] = 9,
                [8] = 4,
                [12] = 7,
            },
            .size = 20,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "outer",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 3,
                [12] = 4,
                [16] = 5,
                [24] = 6,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "bits",
            .bytes = {
                0xf5,
                0x55,
                0x01,
                0,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "both",
            .bytes = {
                [0] = 1,
                [4] = 2,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "switched",
            .bytes = {
                9,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "replaced",
            .bytes = {
                [0] = 9,
                [12] = 4,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "continued",
            .bytes = {
                [4] = 7,
                [8] = 8,
                [12] = 9,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "inferred",
            .bytes = {
                [20] = 7,
                [24] = 8,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "anonymous",
            .bytes = {
                [4] = 2,
                [8] = 3,
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "scalar",
            .bytes = {
                5,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "switched_back",
            .bytes = {
                [4] = 2,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "dropped",
            .bytes = {
                5,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "pointers",
            .size = 24,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "relocated",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "text",
            .bytes = {
                'a',
                'b',
                0,
                'c',
                0,
            },
            .size = 5,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "exact",
            .bytes = {
                'x',
                'y',
            },
            .size = 2,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "rows",
            .bytes = {
                'a',
                'b',
                0,
                0,
                'c',
                0,
                0,
                0,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "wide",
            .bytes = {
                'A',
                0,
                0x3d,
                0xd8,
                0,
                0xde,
                0,
                0,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "readonly",
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA_RELRO,
        },
        {
            .name = "message",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "other",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "tls_zero",
            .size = 4,
            .class = BUSTER_LINK_CLASS_TBSS,
        },
        {
            .name = "tls_value",
            .bytes = {
                7,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_TDATA,
        },
        {
            .name = "tls_pointer",
            .size = 8,
            .class = BUSTER_LINK_CLASS_TDATA,
        },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    static const char padding_source[] =
        "struct __attribute__((packed)) P{unsigned a:5;unsigned b:9;char c;} packed={17,257,7};\n"
        "struct B{unsigned a:4;unsigned :4;unsigned b:8;} braced_bits={.a={7},.b={3},.a={}};\n"
        "const int zeroes[3];struct Pad{char c;long double x;} padded={1,1.5L};\n"
        "extern int aligned[2] __attribute__((aligned(32)));int aligned[2]={1,2};extern int aligned[2] __attribute__((aligned(16)));\n";
    static const CheckInitExpected padding_expected[] = {
        {
            .name = "packed",
            .bytes = {
                0x31,
                0x20,
                7,
            },
            .size = 3,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "braced_bits",
            .bytes = {
                [1] = 3,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "zeroes",
            .size = 12,
            .class = BUSTER_LINK_CLASS_RODATA,
        },
        {
            .name = "aligned",
            .bytes = {
                [0] = 1,
                [4] = 2,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment_log2 = 5,
        },
        {
            .name = "padded",
            .bytes = {
                [0] = 1,
                [23] = 0xc0,
                [24] = 0xff,
                [25] = 0x3f,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, padding_source, padding_expected, BUSTER_ARRAY_LENGTH(padding_expected), true, true, clang);
    static const char compound_source[] =
        "struct Pair{int x,y;};struct Pair plain=(struct Pair){3,4};struct Pair paren=((struct Pair){5,6});\n"
        "struct Wrap{int a;struct Pair p;} wrapped={1,(struct Pair){2,3}};struct Pair *address=&(struct Pair){7,8};\n"
        "struct Pair selected=((struct Wrap){.a=99,.p={11,12}}).p;\n"
        "int compound_target=9;struct Ref{int *p;int v;};struct BigRef{int x;struct Ref r;};\n"
        "struct Ref compound_reference=((struct BigRef){.r={&compound_target+1,7}}).r;\n"
        "union CU{struct{int a,b;}s;long n;};struct CW{int a;union CU u;};\n"
        "union CU copied_union=((struct CW){.u.s={3,4}}).u;\n";
    static const CheckInitExpected compound_expected[] = {
        {
            .name = "plain",
            .bytes = {
                [0] = 3,
                [4] = 4,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "paren",
            .bytes = {
                [0] = 5,
                [4] = 6,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "wrapped",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 3,
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "address",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "selected",
            .bytes = {
                [0] = 11,
                [4] = 12,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "compound_target",
            .bytes = {
                9,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "compound_reference",
            .bytes = {
                [8] = 7,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "copied_union",
            .bytes = {
                [0] = 3,
                [4] = 4,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, compound_source, compound_expected, BUSTER_ARRAY_LENGTH(compound_expected), true, true, clang);
    // Clang 23 accepts this initializer's semantics but its object emitter reports an unimplemented partial
    // compound-copy override. Keep the source, independent target bytes and ELF reader checks for that case.
    static const char copy_override[] =
        "union U{struct{int a,b;}s;long n;};struct W{int a;union U u;};\n"
        "struct H{union U u;} copy_override={.u.n=9,.u=((struct W){.u.s={3,4}}).u,.u.s.b=5};\n";
    static const CheckInitExpected copy_override_expected[] = {
        {
            .name = "copy_override",
            .bytes = {
                [0] = 3,
                [4] = 5,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, copy_override, copy_override_expected, BUSTER_ARRAY_LENGTH(copy_override_expected), true, true, 0);
    static const char automatic[] = "int f(int x){struct S{int a,b;};struct S s={x,x+1};int a[2]={x,3};return s.a+a[0];}";
    check_init_test_source(test, automatic, 0, 0, true, false, clang);
    static const char* invalid[] = {
        "int a[2]={1,2,3};",
        "struct S{int a;} s={.missing=1};",
        "int a[2]={[-1]=3};",
        "int a[2]={[2]=3};",
        "struct S{int *p;} s={3};",
        "char a[2]=\"long\";",
        "char a[3]=\"abc\"+1;",
        "char a[3]={[0]=\"ab\"};",
        "int x;struct S{int a;} s={x};",
        "int f(int x){int a[2]={1,2,3};return x;}",
        "struct S{int x,y;} value=((struct S[2]){{1,2},{3,4}})[1];",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        check_init_test_source(test, invalid[i], 0, 0, false, false, clang);
    }
}

#endif
