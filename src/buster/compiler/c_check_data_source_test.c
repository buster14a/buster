// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source-to-static-data fixtures with independently stated x86-64 bytes, bindings and relocation identities.
// Each source uses both parser/binding/checker schedules and fresh type tables before check_data_input consumes it.
// Include from c_checker_test.c after its shared helpers. No fixture imports external code or expected output.
#ifndef BUSTER_C_CHECK_DATA_SOURCE_TEST_INCLUDED
#define BUSTER_C_CHECK_DATA_SOURCE_TEST_INCLUDED

typedef struct CheckDataSourceSymbol CheckDataSourceSymbol;
struct CheckDataSourceSymbol
{
    const char* name;
    uint8_t flags;
    uint8_t class;
    uint8_t alignment;
    uint8_t size;
    unsigned __int128 bits;
    SymbolId target;
    int64_t addend;
};

typedef struct CheckDataSourceFixture CheckDataSourceFixture;
struct CheckDataSourceFixture
{
    const char* name;
    const char* source;
    const CheckDataSourceSymbol* symbols;
    uint32_t symbol_count;
    CheckDataInputStatus status;
    const char* failed_name;
    bool capacities;
};

static bool check_test_data_source_bytes(const uint8_t* bytes, uint32_t count, unsigned __int128 expected)
{
    bool equal = bytes != 0;
    for (uint32_t i = 0; equal && i < count; i += 1)
    {
        equal = bytes[i] == (uint8_t)(expected >> (i * 8));
    }
    return equal;
}

static bool check_test_data_source_name(const Link* link, SymbolId symbol, const char* expected)
{
    const NameId name = link->symbols.name[symbol];
    const size_t length = strlen(expected);
    return name != 0 && name < link->names.count && link->names.length[name] == length && memcmp(link->names.bytes[name], expected, length) == 0 && link->names.bytes[name][length] == 0;
}

static void check_test_data_source_output(CheckTest* test, const CheckDataSourceFixture* fixture, CheckDataInput* data, const CheckInput* input)
{
    Link* link = &data->link;
    check_test_expect(test, link_check(link) == LINK_ERROR_NONE && link->symbols.count == fixture->symbol_count + 1 && data->object_count == input->token_count + 1, "source static data has exact symbol count and a valid Link");
    AtomId next_atom = 1;
    FixupId next_fixup = 1;
    uint64_t byte_count = 0;
    for (uint32_t i = 0; i < fixture->symbol_count && i + 1 < link->symbols.count; i += 1)
    {
        const SymbolId symbol = i + 1;
        const CheckDataSourceSymbol* expected = &fixture->symbols[i];
        char description[256];
        snprintf(description, sizeof(description), "%s: symbol %u (%s) has independently expected identity and flags", fixture->name, symbol, expected->name);
        check_test_expect(test, check_test_data_source_name(link, symbol, expected->name) && link->symbols.flags[symbol] == expected->flags && link->symbols.value[symbol] == 0 && link->symbols.size[symbol] == expected->size, description);
        uint32_t mappings = 0;
        for (uint32_t object = 1; object < data->object_count; object += 1)
        {
            mappings += data->object_symbols[object] == symbol;
        }
        check_test_expect(test, mappings == 1, "each published static symbol has exactly one source object identity");
        const AtomId atom = link->symbols.atom[symbol];
        if (expected->class == BUSTER_LINK_CLASS_NONE)
        {
            check_test_expect(test, atom == 0 && link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE, "referenced external target remains undefined");
        }
        else
        {
            snprintf(description, sizeof(description), "%s: atom %u (%s) has independently expected class, size and alignment", fixture->name, atom, expected->name);
            const bool atom_valid = atom == next_atom && atom < link->atoms.count;
            check_test_expect(test, atom_valid, "static definitions retain declaration identity order");
            if (atom_valid)
            {
                check_test_expect(test, link->symbols.source[symbol] == BUSTER_LINK_SOURCE_PROGRAM && link->atoms.class[atom] == expected->class && link->atoms.size[atom] == expected->size && link->atoms.alignment_log2[atom] == expected->alignment && link->atoms.unit[atom] == 0, description);
                snprintf(description, sizeof(description), "%s: %s has independently expected little-endian target bytes", fixture->name, expected->name);
                const bool bytes = expected->class == BUSTER_LINK_CLASS_BSS ? link->atoms.bytes[atom] == 0 : check_test_data_source_bytes(link->atoms.bytes[atom], expected->size, expected->bits);
                check_test_expect(test, bytes, description);
                byte_count += expected->class != BUSTER_LINK_CLASS_BSS ? expected->size : 0;
                check_test_expect(test, link->atoms.fixup_count[atom] == (uint32_t)(expected->target != 0), "static atom has exactly its expected relocation count");
                if (expected->target != 0)
                {
                    const FixupId fixup = link->atoms.fixup_begin[atom];
                    const bool fixup_valid = fixup == next_fixup && fixup < link->fixups.count;
                    check_test_expect(test, fixup_valid, "static relocations have stable contiguous identities");
                    if (fixup_valid)
                    {
                        check_test_expect(test, link->fixups.offset[fixup] == 0 && link->fixups.target[fixup] == expected->target && link->fixups.kind[fixup] == BUSTER_LINK_FIXUP_ABS64 && link_fixup_addend(link, fixup) == expected->addend, "static relocation has exact target identity, offset, kind and scaled addend");
                    }
                    next_fixup += 1;
                }
            }
            next_atom += 1;
        }
    }
    check_test_expect(test, link->atoms.count == next_atom && link->fixups.count == next_fixup && data->emission.atom_count == next_atom - 1 && data->emission.fixup_count == next_fixup - 1 && data->emission.byte_count == byte_count, "source emission counts exactly match independently expected objects, fixups and bytes");
}

static bool check_test_data_source_cleared(const CheckDataInput* data)
{
    return data->object_symbols == 0 && data->object_count == 0 && data->link.names.count == 0 && data->link.atoms.count == 0 && data->link.symbols.count == 0 && data->link.fixups.count == 0 && data->emission.atom_begin == 0;
}

static void check_test_data_source_capacities(CheckTest* test, const CheckInput* input, const CheckStream* stream, const Parser* parser, const InternTable* identifiers, TypeTable* types)
{
    Arena backing[3] = {
        arena_reserve(UINT64_C(1) << 16),
        arena_reserve(UINT64_C(1) << 16),
        arena_reserve(UINT64_C(1) << 16),
    };
    const bool ready = backing[0].base != 0 && backing[1].base != 0 && backing[2].base != 0;
    check_test_expect(test, ready, "static-data rollback storage allocation");
    for (uint32_t selected = 0; ready && selected < 3; selected += 1)
    {
        bool completed = false;
        for (uint64_t capacity = 17; !completed && capacity < backing[selected].capacity - 16; capacity += 1)
        {
            Arena arenas[3] = {
                backing[0],
                backing[1],
                backing[2],
            };
            for (uint32_t i = 0; i < 3; i += 1)
            {
                memset(arenas[i].base, 0xa5, (size_t)arenas[i].capacity);
                arenas[i].length = 17;
            }
            arenas[selected].capacity = capacity;
            CheckDataInput data;
            memset(&data, 0xa5, sizeof(data));
            completed = check_data_input(&data, input, stream, parser, identifiers, types, &arenas[0], &arenas[1], &arenas[2]);
            bool canaries = true;
            for (uint32_t i = 0; i < 3; i += 1)
            {
                canaries = canaries && check_test_graph_canary(arenas[i].base, 0, 17);
            }
            canaries = canaries && check_test_graph_canary(arenas[selected].base, capacity, capacity + 16);
            check_test_expect(test, canaries && arenas[2].length == 17, "static-data emission preserves live arena prefixes, capacity tails and scratch mark");
            if (!completed)
            {
                check_test_expect(test, data.status == CHECK_DATA_INPUT_RESOURCE && check_test_data_source_cleared(&data) && arenas[0].length == 17 && arenas[1].length == 17, "partial static-data allocation fails atomically with resource status");
            }
        }
        check_test_expect(test, completed, "static-data allocation capacity sweep reaches successful exact capacity");
    }
    if (ready)
    {
        CheckStream invalid = *stream;
        invalid.count += 1;
        CheckDataInput data;
        const bool accepted = check_data_input(&data, input, &invalid, parser, identifiers, types, &backing[0], &backing[1], &backing[2]);
        check_test_expect(test, !accepted && data.status == CHECK_DATA_INPUT_INVALID && check_test_data_source_cleared(&data) && backing[0].length == 0 && backing[1].length == 0 && backing[2].length == 0, "mismatched checked source is invalid input rather than capacity failure");
    }
    for (uint32_t i = 0; i < 3; i += 1)
    {
        arena_release(&backing[i]);
    }
}

static void check_test_data_source(CheckTest* test, const CheckDataSourceFixture* fixture)
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
    preprocessed = preprocessed && pp_run_source(&pp, fixture->name, (const uint8_t*)fixture->source, (uint32_t)strlen(fixture->source));
    check_test_expect(test, preprocessed, "static-data source preprocessing");
    for (uint32_t path = 0; preprocessed && path < 2; path += 1)
    {
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 24);
        Arena stream_arena = arena_reserve(UINT64_C(1) << 24);
        Arena output = arena_reserve(UINT64_C(1) << 20);
        Arena bytes = arena_reserve(UINT64_C(1) << 20);
        Arena scratch = arena_reserve(UINT64_C(1) << 24);
        const ParseOptions parse_options = {
            .scalar = path == 0,
            .extension_metadata = true,
        };
        bool ready = check_test_types_open(&types) && parse_initialize(&parser, &parse_options, pp_options.language) && input_arena.base != 0 && stream_arena.base != 0 && output.base != 0 && bytes.base != 0 && scratch.base != 0;
        ready = ready && parse_translation_unit(&parser, &pp) && parser.errors == 0 && parser.fatal == PARSE_MESSAGE_NONE;
        CheckInput input = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch);
        const CheckOptions options = {
            .scalar = path == 0,
        };
        CheckStream stream = { 0 };
        ready = ready && check_graph_run(&types.table, &input.graph, &stream_arena, &scratch, &options, &stream) && stream.report_count == 0;
        char description[256];
        snprintf(description, sizeof(description), "%s: %s parser/adapter/checker source accepted", fixture->name, path == 0 ? "scalar" : "vector");
        check_test_expect(test, ready, description);
        if (ready)
        {
            CheckDataInput data;
            const bool emitted = check_data_input(&data, &input, &stream, &parser, &identifiers, &types.table, &output, &bytes, &scratch);
            snprintf(description, sizeof(description), "%s: static-data status %u expected, %u observed on %s path", fixture->name, fixture->status, data.status, path == 0 ? "scalar" : "vector");
            check_test_expect(test, emitted == (fixture->status == CHECK_DATA_INPUT_OK) && data.status == fixture->status && scratch.length == 0, description);
            if (emitted)
            {
                check_test_data_source_output(test, fixture, &data, &input);
                if (fixture->capacities)
                {
                    check_test_data_source_capacities(test, &input, &stream, &parser, &identifiers, &types.table);
                }
            }
            else
            {
                check_test_expect(test, check_test_data_source_cleared(&data) && output.length == 0 && bytes.length == 0, "rejected source emission retains no output or bytes");
                if (fixture->failed_name != 0)
                {
                    const bool located = data.token < parser.tokens.count;
                    const IdentId name = located ? parser.tokens.names[data.token] : 0;
                    const size_t length = strlen(fixture->failed_name);
                    check_test_expect(test, located && intern_length(&identifiers, name) == length && memcmp(intern_bytes(&identifiers, name), fixture->failed_name, length) == 0, "unsupported static emission identifies the responsible declaration");
                }
            }
        }
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&stream_arena);
        arena_release(&input_arena);
        check_test_types_close(&types);
        parse_release(&parser);
    }
    pp_release(&pp);
    intern_release(&identifiers);
}

static void check_test_data_sources(CheckTest* test)
{
    const CheckDataSourceSymbol scalars[] = {
        {
            .name = "zero",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_BSS,
            .alignment = 2,
            .size = 4,
        },
        {
            .name = "little",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 1,
            .size = 2,
            .bits = 0x1234,
        },
        {
            .name = "ro",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_RODATA,
            .alignment = 2,
            .size = 4,
            .bits = 7,
        },
        {
            .name = "live",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            // Clang 23 emits const-volatile zero definitions as constant data too; volatile governs their reads.
            .class = BUSTER_LINK_CLASS_RODATA,
            .alignment = 2,
            .size = 4,
        },
        {
            .name = "f",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 0x3fc00000,
        },
        {
            .name = "minus",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .bits = UINT64_C(0x8000000000000000),
        },
        {
            .name = "extended",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 4,
            .size = 16,
            .bits = ((unsigned __int128)0x3fff << 64) | UINT64_C(0x8000000000000000),
        },
        {
            .name = "truth",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .size = 1,
            .bits = 1,
        },
        {
            .name = "null",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_BSS,
            .alignment = 3,
            .size = 8,
        },
    };
    const CheckDataSourceSymbol definitions[] = {
        {
            .name = "value",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 0x12345678,
        },
        {
            .name = "local",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 9,
        },
    };
    const CheckDataSourceSymbol composite[] = {
        {
            .name = "fn",
            .flags = BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL,
        },
        {
            .name = "slot",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 1,
        },
    };
    const CheckDataSourceSymbol references[] = {
        {
            .name = "later",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 19,
        },
        {
            .name = "forward",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 1,
        },
        {
            .name = "internal",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 3,
        },
        {
            .name = "local_ref",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 3,
        },
        {
            .name = "readonly",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA_RELRO,
            .alignment = 3,
            .size = 8,
            .target = 1,
            .addend = 4,
        },
        {
            .name = "external",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
        },
        {
            .name = "external_ref",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 6,
        },
        {
            .name = "function",
            .flags = BUSTER_LINK_SYMBOL_FUNCTION | BUSTER_LINK_SYMBOL_GLOBAL,
        },
        {
            .name = "external_fn",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 8,
        },
    };
    const CheckDataSourceSymbol locals[] = {
        {
            .name = "same",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 1,
        },
        {
            .name = "same",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 2,
        },
        {
            .name = "p",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 2,
        },
        {
            .name = "same",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 2,
            .size = 4,
            .bits = 4,
        },
        {
            .name = "p",
            .flags = BUSTER_LINK_SYMBOL_OBJECT,
            .class = BUSTER_LINK_CLASS_DATA,
            .alignment = 3,
            .size = 8,
            .target = 4,
        },
    };
    const CheckDataSourceSymbol array[] = {
        {
            .name = "array",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_BSS,
            .alignment = 2,
            .size = 8,
        },
    };
    const CheckDataSourceSymbol record[] = {
        {
            .name = "record",
            .flags = BUSTER_LINK_SYMBOL_OBJECT | BUSTER_LINK_SYMBOL_GLOBAL,
            .class = BUSTER_LINK_CLASS_BSS,
            .alignment = 2,
            .size = 4,
        },
    };
    const CheckDataSourceFixture fixtures[] = {
        {
            .name = "static-scalars.c",
            .source = "int zero; unsigned short little=0x1234; const int ro=7; const volatile int live=0; float f=1.5f; double minus=-0.0; long double extended=1.0L; _Bool truth=9; void *null=0;",
            .symbols = scalars,
            .symbol_count = BUSTER_ARRAY_LENGTH(scalars),
            .capacities = true,
        },
        {
            .name = "static-definitions.c",
            .source = "extern int value; int value; int value=0x12345678; extern int value; int value; static int local; extern int local; static int local=9;",
            .symbols = definitions,
            .symbol_count = BUSTER_ARRAY_LENGTH(definitions),
        },
        {
            .name = "static-composite.c",
            .source = "int fn(); int (*slot)() = fn; int (*slot)(void);",
            .symbols = composite,
            .symbol_count = BUSTER_ARRAY_LENGTH(composite),
        },
        {
            .name = "static-references.c",
            .source = "extern int later; int *forward=&later; int later=19; static int internal=3; static int *local_ref=&internal; int *const readonly=&later+1; extern int external; int *external_ref=&external; int function(void); int (*external_fn)(void)=function;",
            .symbols = references,
            .symbol_count = BUSTER_ARRAY_LENGTH(references),
        },
        {
            .name = "static-local-identities.c",
            .source = "int same=1; int a(void){static int same=2; static int *p=&same; int automatic=3; return automatic;} int b(void){static int same=4; static int *p=&same; return 0;}",
            .symbols = locals,
            .symbol_count = BUSTER_ARRAY_LENGTH(locals),
        },
        {
            .name = "static-empty.c",
            .source = "extern int unused; static int f(void){return 0;}",
        },
        {
            .name = "static-array-zero.c",
            .source = "int array[2];",
            .symbols = array,
            .symbol_count = BUSTER_ARRAY_LENGTH(array),
        },
        {
            .name = "static-record-zero.c",
            .source = "struct R { int value; }; struct R record;",
            .symbols = record,
            .symbol_count = BUSTER_ARRAY_LENGTH(record),
        },
        {
            .name = "static-internal-function.c",
            .source = "static int f(void){return 0;} int (*p)(void)=f;",
            .status = CHECK_DATA_INPUT_UNSUPPORTED,
            .failed_name = "f",
        },
        {
            .name = "static-overload-needs-symbol-mangling.c",
            .source = "int f(void) __attribute__((overloadable)); int (*p)(void)=f;",
            .status = CHECK_DATA_INPUT_UNSUPPORTED,
            .failed_name = "f",
        },
        {
            .name = "static-unused-overload-needs-no-symbol.c",
            .source = "int f(void) __attribute__((overloadable));",
        },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        check_test_data_source(test, &fixtures[i]);
    }
}

#endif
