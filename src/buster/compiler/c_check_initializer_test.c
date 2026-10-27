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
    const char* clang_name;
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
    if (check_init_test_symbol(link, "names") != 0)
    {
        const AtomId names = link->symbols.atom[check_init_test_symbol(link, "names")];
        const AtomId sized = link->symbols.atom[check_init_test_symbol(link, "sized")];
        check_test_expect(test, strings == 4 && link->atoms.fixup_count[names] == 2 && link->atoms.fixup_count[sized] == 2,
                          "pointer arrays retain their four string references without pooling the copied character array");
    }
    if (check_init_test_symbol(link, "excess_scalar") != 0)
    {
        check_test_expect(test, strings == 0 && check_init_test_symbol(link, "ignored_target") == 0 && check_init_test_symbol(link, "ignored_call") == 0,
                          "ignored excess expressions retain no string atoms or external references");
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
    if (built && check_init_test_symbol(link, "flexible_refs") != 0)
    {
        const AtomId atom = link->symbols.atom[check_init_test_symbol(link, "flexible_refs")];
        check_test_expect(test, check_init_test_symbol(link, "discarded") == 0 && link->atoms.fixup_count[atom] == 1 &&
                               check_init_test_elf_reference(&ours, "flexible_refs", 8, "flexible_target", 4),
                          "flexible tail replacement removes discarded references and preserves its final ABS64 addend");
    }
    if (built && check_init_test_symbol(link, "complex_values") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&ours, "complex_real", 0, "complex_values", 16) &&
                               check_init_test_elf_reference(&ours, "complex_imag", 0, "complex_values", 24) &&
                               check_init_test_elf_reference(&ours, "complex_long_part", 0, "complex_l", 16) &&
                               check_init_test_elf_reference(&ours, "complex_bits", 0, "complex_values", 43),
                          "complex component addresses preserve array offsets, component widths and symbolic integer addends");
    }
    if (built && check_init_test_symbol(link, "overload_pair") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&ours, "overload_pair", 0, "_Z6choosei", 0) &&
                               check_init_test_elf_reference(&ours, "overload_pair", 8, "_Z6choosed", 0),
                          "source initializer fields preserve selected overload identities through the supplied ELF symbol map");
    }
    if (built && check_init_test_symbol(link, "plain_address") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&ours, "plain_address", 0, "plain", 0) &&
                               check_init_test_elf_reference(&ours, "plain_record", 0, "plain", 0),
                          "an unmarked overload member keeps its ordinary C name in standalone data publication");
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
        CheckInitExpected reference = expected[i];
        reference.name = reference.clang_name != NULL ? reference.clang_name : reference.name;
        check_test_expect(test, check_init_test_elf_bytes(&theirs, &reference), description);
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
    if (oracle && check_init_test_symbol(link, "flexible_refs") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "flexible_refs", 8, "flexible_target", 4),
                          "Clang confirms the surviving flexible-array pointer relocation");
    }
    if (oracle && check_init_test_symbol(link, "complex_values") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "complex_real", 0, "complex_values", 16) &&
                               check_init_test_elf_reference(&theirs, "complex_imag", 0, "complex_values", 24) &&
                               check_init_test_elf_reference(&theirs, "complex_long_part", 0, "complex_l", 16) &&
                               check_init_test_elf_reference(&theirs, "complex_bits", 0, "complex_values", 43),
                          "Clang confirms all complex component address relocations and addends");
    }
    if (oracle && check_init_test_symbol(link, "overload_pair") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "overload_pair", 0, "_Z6choosei", 0) &&
                               check_init_test_elf_reference(&theirs, "overload_pair", 8, "_Z6choosed", 0),
                          "Clang independently confirms the two context-selected function relocations");
    }
    if (oracle && check_init_test_symbol(link, "plain_address") != 0)
    {
        check_test_expect(test, check_init_test_elf_reference(&theirs, "plain_address", 0, "plain", 0) &&
                               check_init_test_elf_reference(&theirs, "plain_record", 0, "plain", 0),
                          "Clang confirms the selected unmarked function's ordinary C relocations");
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

#include "c_check_initializer_overload_test.c"

// Reverse the pool, as a body merge can reorder its literals, and remap the published token index. Check syntax
// and object lookup independently, without assuming that pool order, token order and object identity coincide.
static void check_init_test_string_index(CheckTest* test, const CheckInput* input, const Parser* parser, TypeTable* types, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    CheckInitString* strings = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckInitString, input->string_count);
    uint32_t* ids = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)parser->tokens.count + 1);
    bool valid = strings != NULL && ids != NULL;
    for (uint32_t token = 0; valid && token < parser->tokens.count; token += 1)
    {
        const uint32_t id = input->string_ids[token];
        valid = id == BUSTER_CHECK_NONE || id < input->string_count;
        ids[token] = id != BUSTER_CHECK_NONE ? input->string_count - 1 - id : BUSTER_CHECK_NONE;
    }
    for (uint32_t i = 0; valid && i < input->string_count; i += 1)
    {
        strings[input->string_count - 1 - i] = input->strings[i];
    }
    CheckValue value = { 0 };
    CheckStream stream = {
        .values = &value,
        .count = 1,
    };
    CheckInitWalker walker = {
        .types = types,
        .parser = parser,
        .stream = &stream,
        .strings = strings,
        .string_count = input->string_count,
        .string_ids = ids,
    };
    for (uint32_t i = 0; valid && i < input->string_count; i += 1)
    {
        const CheckInitString* string = strings + i;
        value = (CheckValue){
            .type = string->type,
            .object = string->object,
        };
        const CheckInitString* expected = string->predefined ? NULL : string;
        valid = check_init_string(&walker, 0, 0, 0) == expected;
        if (!string->predefined)
        {
            valid = valid && check_init_string(&walker, string->token, string->token + 1, BUSTER_CHECK_NONE) == string;
            value.offset = 1;
            valid = valid && check_init_string(&walker, 0, 0, 0) == NULL;
            const uint32_t id = ids[string->token];
            ids[string->token] = input->string_count;
            valid = valid && check_init_string(&walker, string->token, string->token + 1, BUSTER_CHECK_NONE) == NULL;
            ids[string->token] = id;
        }
    }
    check_test_expect(test, valid, "string initializer indices survive pool reordering, preserve object offsets and exclude predefined arrays");
    arena_pop_to(scratch, mark);
}

static void check_init_test_source_language(CheckTest* test, const char* source, const CheckInitExpected* expected, uint32_t count,
                                            bool accept, bool emit, const char* clang, CLanguage language)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const PPOptions pp_options = {
        .language = language,
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
            if (input.string_count != 0)
            {
                check_init_test_string_index(test, &input, &parser, &types.table, &scratch);
            }
            check_init_test_overload_parts(test, &input, &stream, &parser, &identifiers, &types.table, &initializers, source, clang);
            for (uint32_t i = 0; i < initializers.count; i += 1)
            {
                if (initializers.plans[i].warning_count != 0)
                {
                    const uint32_t declaration = initializers.declaration_tokens[i];
                    CheckInitializer strict = { 0 };
                    const bool accepted = check_initializer_run(&types.table, &parser, input.token_nodes, &stream, input.strings, input.string_count, input.string_ids,
                                                                 input.declared_types[declaration], input.initializer_begins[declaration], input.initializer_ends[declaration],
                                                                 input.static_objects[declaration] != 0, &output, &scratch, &strict);
                    check_test_expect(test, !accepted && strict.status == CHECK_INIT_INVALID && strict.parts == 0 && strict.count == 0 && output.length == 0 && scratch.length == 0,
                                      "direct strict planner still rejects source-compatible excess elements transactionally");
                    check_test_expect(test, initializers.warning_count != 0 && initializers.plans[i].warning_token < input.initializer_ends[declaration],
                                      "source excess elements retain warning positions separately from successful plans");
                }
            }
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
                    const bool partial = check_initializer_plan(&types.table, &parser, input.token_nodes, &stream, input.strings, input.string_count, input.string_ids,
                                                                input.declared_types[declaration], input.initializer_begins[declaration], input.initializer_ends[declaration],
                                                                input.static_objects[declaration] != 0, false, false, &tiny, &scratch, &rejected, &input.graph);
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

static void check_init_test_source(CheckTest* test, const char* source, const CheckInitExpected* expected, uint32_t count,
                                   bool accept, bool emit, const char* clang)
{
    const CLanguage language = {
        .standard = 2017,
        .gnu = true,
    };
    check_init_test_source_language(test, source, expected, count, accept, emit, clang, language);
}

static void check_init_test_overload_constraints(CheckTest* test, const char* clang)
{
    static const char* const source[] = {
        "int f(int) __attribute__((overloadable));double f(double) __attribute__((overloadable));struct S{float(*p)(float);} x={f};",
        "int f(int) __attribute__((overloadable));double f(double) __attribute__((overloadable));struct S{int(*p)();} x={f};",
        "int f(int) __attribute__((overloadable));double f(double) __attribute__((overloadable));struct S{_Atomic(int(*)(int)) p;} x={f};",
        "int f(int) __attribute__((overloadable));struct S{_Atomic(int(*)(int)) p;} x={f};",
    };
    char directory[] = "/tmp/buster-initializer-overload-XXXXXX";
    const bool available = clang != NULL && mkdtemp(directory) != NULL;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(source); i += 1)
    {
        const bool accept = i == BUSTER_ARRAY_LENGTH(source) - 1;
        check_init_test_source(test, source[i], NULL, 0, accept, false, NULL);
        if (available)
        {
            char path[192];
            snprintf(path, sizeof(path), "%s/case-%u.c", directory, i);
            char* paths[] = { path };
            const char* arguments[] = { "-std=gnu17" };
            ParseTestClang oracle = { 0 };
            const bool ran = parse_test_write(path, source[i], strlen(source[i])) &&
                             parse_test_clang(clang, arguments, BUSTER_ARRAY_LENGTH(arguments), paths, 1, &oracle);
            check_test_expect(test, ran && oracle.rejected != accept,
                              "Clang confirms exact aggregate target constraints, preserving the atomic target distinction");
            unlink(path);
        }
    }
    check_test_expect(test, clang == NULL || available, "initializer overload constraint oracle storage");
    if (available)
    {
        rmdir(directory);
    }
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

static void check_init_test_flexible(CheckTest* test, const char* clang)
{
    static const char source[] =
        "struct F{int n;int v[];} flexible={1,{2,3}},over={.v={1,2,3},.v={4}},sparse_tail={.v={[3]=7}},empty_tail={1,{}};\n"
        "struct F bare={1,2,3},reset_empty={.v={1,2,3},.v={}};struct Empty{struct F f;} empty_nested={{1,{}}};\n"
        "struct C{int n;char v[];} tail_text={3,\"ab\"},shorter={.v=\"hello\",.v=\"x\"};int type_size=sizeof flexible;\n"
        "struct Pad{long n;char c;char v[];} short_tail={1,2,\"a\"},long_tail={1,2,\"abcdefghij\"};\n"
        "struct S{unsigned char a,b,c[2];};struct T{unsigned char s[16],a;};struct V{struct S s;struct T t;unsigned char a;};\n"
        "struct W{struct V t;struct S s[];} nested_tail={{1,2,3,4},{1,2,3,4,5}};\n"
        "extern int discarded;int flexible_target=9;struct R{int n;int *v[];} flexible_refs={.v={&discarded,&discarded},.v={&flexible_target+1}};\n";
    static const CheckInitExpected expected[] = {
        {
            .name = "flexible",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 3,
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "over",
            .bytes = {
                [4] = 4,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "bare",
            .bytes = {
                [0] = 1,
                [4] = 2,
                [8] = 3,
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "reset_empty",
            .size = 4,
            .class = BUSTER_LINK_CLASS_BSS,
        },
        {
            .name = "empty_nested",
            .bytes = {
                1,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "sparse_tail",
            .bytes = {
                [16] = 7,
            },
            .size = 20,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "empty_tail",
            .bytes = {
                1,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "tail_text",
            .bytes = {
                [0] = 3,
                [4] = 'a',
                [5] = 'b',
            },
            .size = 7,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "shorter",
            .bytes = {
                [4] = 'x',
            },
            .size = 6,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "type_size",
            .bytes = {
                4,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "short_tail",
            .bytes = {
                [0] = 1,
                [8] = 2,
                [9] = 'a',
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "long_tail",
            .bytes = {
                [0] = 1,
                [8] = 2,
                [9] = 'a',
                [10] = 'b',
                [11] = 'c',
                [12] = 'd',
                [13] = 'e',
                [14] = 'f',
                [15] = 'g',
                [16] = 'h',
                [17] = 'i',
                [18] = 'j',
            },
            .size = 20,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "nested_tail",
            .bytes = {
                [0] = 1,
                [1] = 2,
                [2] = 3,
                [3] = 4,
                [22] = 1,
                [23] = 2,
                [24] = 3,
                [25] = 4,
                [26] = 5,
            },
            .size = 30,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "flexible_target",
            .bytes = {
                9,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "flexible_refs",
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    static const char strings[] =
        "const char *names[]={\"label\",\"Infinity\",0};int names_size=sizeof names;\n"
        "const char *sized[2]={\"hello\",\"world\"};char copied[]=\"copy\";\n";
    static const CheckInitExpected string_expected[] = {
        {
            .name = "names",
            .size = 24,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "names_size",
            .bytes = {
                24,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "sized",
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "copied",
            .bytes = {
                'c',
                'o',
                'p',
                'y',
                0,
            },
            .size = 5,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, strings, string_expected, BUSTER_ARRAY_LENGTH(string_expected), true, true, clang);
    static const char indexed[] =
        "_Static_assert(sizeof \"condition\" == 10,\"message\" \" tail\");\n"
        "_Static_assert(sizeof (char[][4]){\"abc\",\"def\"} == 8,\"nested strings\");\n"
        "char joined[][4]={\"a\" \"b\",(\"cd\")};\n"
        "int wide_joined[][3]={L\"a\" L\"b\",(L\"cd\")};\n"
        "unsigned short short_joined[][3]={u\"a\" u\"b\",(u\"cd\")};\n"
        "struct StringRow{char text[4];int wide[3];} string_rows[]={\n"
        "{\"a\" \"b\",L\"cd\"},{(\"cd\"),L\"a\" L\"b\"}};\n";
    static const CheckInitExpected indexed_expected[] = {
        {
            .name = "joined",
            .bytes = {
                [0] = 'a',
                [1] = 'b',
                [4] = 'c',
                [5] = 'd',
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "wide_joined",
            .bytes = {
                [0] = 'a',
                [4] = 'b',
                [12] = 'c',
                [16] = 'd',
            },
            .size = 24,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "short_joined",
            .bytes = {
                [0] = 'a',
                [2] = 'b',
                [6] = 'c',
                [8] = 'd',
            },
            .size = 12,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "string_rows",
            .bytes = {
                [0] = 'a',
                [1] = 'b',
                [4] = 'c',
                [8] = 'd',
                [16] = 'c',
                [17] = 'd',
                [20] = 'a',
                [24] = 'b',
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, indexed, indexed_expected, BUSTER_ARRAY_LENGTH(indexed_expected), true, true, clang);
    check_init_test_source(test, "void f(void){struct F{int n;int v[];} empty={1,{}};}", 0, 0, true, false, clang);
    static const char* invalid[] = {
        "void f(void){struct F{int n;int v[];} local={1,{2,3}};}",
        "struct F{int n;int v[];};struct O{struct F f;} nested={{1,{2,3}}};",
        "struct F{int n;int v[];} *p=&(struct F){1,{2,3}};",
        "void f(void){struct F{int n;int v[];} local={.v[1]=3};}",
        "struct F{int n;int v[];} direct={.v[3]=7};",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        check_init_test_source(test, invalid[i], 0, 0, false, false, clang);
    }
}

static void check_init_test_complex(CheckTest* test, const char* clang)
{
    static const char source[] =
        "float _Complex complex_f=1.5f+2.25fi;double _Complex complex_d=-3.0+4.5i;\n"
        "long double _Complex complex_l=1.5L-2.0Li;float _Complex imaginary_f=0x1.8p+1fi;\n"
        "double _Complex imaginary_d=5j;long double _Complex imaginary_l=0x1.8p+2Li;\n"
        "__complex__ int complex_int=3+4i;__complex__ short complex_short=-2-3i;\n"
        "double _Complex complex_zero,negative_zero=-0.0i;const float _Complex complex_const=1.0f-2.0fi;\n"
        "_Thread_local long double _Complex complex_tls=2.0L+3.0Li;\n"
        "double complex_component=__imag__ (3.0+4.0i);double _Complex complex_values[3];\n"
        "double *complex_real=&__real__ complex_values[1],*complex_imag=&__imag__ complex_values[1];\n"
        "long double *complex_long_part=&__imag__ complex_l;unsigned long complex_bits=(unsigned long)&__imag__ complex_values[2]+3;\n";
    static const CheckInitExpected expected[] = {
        {
            .name = "complex_f",
            .bytes = {
                [2] = 0xc0,
                [3] = 0x3f,
                [6] = 0x10,
                [7] = 0x40,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_d",
            .bytes = {
                [6] = 0x08,
                [7] = 0xc0,
                [14] = 0x12,
                [15] = 0x40,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_l",
            .bytes = {
                [7] = 0xc0,
                [8] = 0xff,
                [9] = 0x3f,
                [23] = 0x80,
                [25] = 0xc0,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "imaginary_f",
            .bytes = {
                [6] = 0x40,
                [7] = 0x40,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "imaginary_d",
            .bytes = {
                [14] = 0x14,
                [15] = 0x40,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "imaginary_l",
            .bytes = {
                [23] = 0xc0,
                [24] = 0x01,
                [25] = 0x40,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_int",
            .bytes = {
                [0] = 3,
                [4] = 4,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_short",
            .bytes = {
                0xfe,
                0xff,
                0xfd,
                0xff,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_zero",
            .size = 16,
            .class = BUSTER_LINK_CLASS_BSS,
        },
        {
            .name = "negative_zero",
            .bytes = {
                [7] = 0x80,
                [15] = 0x80,
            },
            .size = 16,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_const",
            .bytes = {
                [2] = 0x80,
                [3] = 0x3f,
                [7] = 0xc0,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_RODATA,
        },
        {
            .name = "complex_tls",
            .bytes = {
                [7] = 0x80,
                [9] = 0x40,
                [23] = 0xc0,
                [25] = 0x40,
            },
            .size = 32,
            .class = BUSTER_LINK_CLASS_TDATA,
        },
        {
            .name = "complex_component",
            .bytes = {
                [6] = 0x10,
                [7] = 0x40,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_values",
            .size = 48,
            .class = BUSTER_LINK_CLASS_BSS,
        },
        {
            .name = "complex_real",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_imag",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_long_part",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "complex_bits",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    check_init_test_source(test, "double _Complex z=1+2i;double x=__real__ z;", 0, 0, false, false, clang);
}

#include "c_check_initializer_diagnostic_test.c"
#include "c_check_initializer_excess_test.c"

static void check_init_test_vector_objects(CheckTest* test, const char* clang)
{
    static const char source[] =
        "typedef int I __attribute__((vector_size(16)));typedef int E __attribute__((ext_vector_type(4)));"
        "typedef _Bool B __attribute__((ext_vector_type(4)));typedef float F __attribute__((vector_size(16)));\n"
        "const I named_first={1,2,3,4};const I named_second=named_first+1;\n"
        "I named_copy=named_second;I named_chain=named_second*2;int named_index=named_first[2];\n"
        "const E named_extended={5,6,7,8};E named_swizzle=(+named_extended).wzyx;\n"
        "const B named_bits={1,0,1,0};B named_bit_copy=named_bits;\n"
        "_Thread_local const I named_tls={9};I named_tls_copy=named_tls+1;\n"
        "I named_comma=(1,named_first);const F named_float={1.0f,-0.0f,2.0f,4.0f};F named_float_copy=named_float;\n"
        "I *local_vectors(void){const I a=(I){2,4,6,8};const I b=a+1;static I named_local=b*2;return &named_local;}\n";
    static const CheckInitExpected expected[] = {
        { .name = "named_first", .bytes = { [0] = 1, [4] = 2, [8] = 3, [12] = 4 }, .size = 16, .class = BUSTER_LINK_CLASS_RODATA },
        { .name = "named_second", .bytes = { [0] = 2, [4] = 3, [8] = 4, [12] = 5 }, .size = 16, .class = BUSTER_LINK_CLASS_RODATA },
        { .name = "named_copy", .bytes = { [0] = 2, [4] = 3, [8] = 4, [12] = 5 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_chain", .bytes = { [0] = 4, [4] = 6, [8] = 8, [12] = 10 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_index", .bytes = { 3 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_extended", .bytes = { [0] = 5, [4] = 6, [8] = 7, [12] = 8 }, .size = 16, .class = BUSTER_LINK_CLASS_RODATA },
        { .name = "named_swizzle", .bytes = { [0] = 8, [4] = 7, [8] = 6, [12] = 5 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_bits", .bytes = { 5 }, .size = 1, .class = BUSTER_LINK_CLASS_RODATA },
        { .name = "named_bit_copy", .bytes = { 5 }, .size = 1, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_tls", .bytes = { 9 }, .size = 16, .class = BUSTER_LINK_CLASS_TDATA },
        { .name = "named_tls_copy", .bytes = { [0] = 10, [4] = 1, [8] = 1, [12] = 1 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_comma", .bytes = { [0] = 1, [4] = 2, [8] = 3, [12] = 4 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_float", .bytes = { [2] = 0x80, [3] = 0x3f, [7] = 0x80, [11] = 0x40, [14] = 0x80, [15] = 0x40 }, .size = 16, .class = BUSTER_LINK_CLASS_RODATA },
        { .name = "named_float_copy", .bytes = { [2] = 0x80, [3] = 0x3f, [7] = 0x80, [11] = 0x40, [14] = 0x80, [15] = 0x40 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "named_local", .clang_name = "local_vectors.named_local", .bytes = { [0] = 6, [4] = 10, [8] = 14, [12] = 18 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    check_init_test_diagnostic_source(test, "typedef int I __attribute__((vector_size(16)));const I a={1,2,3,4};I b=a+1;\nint f(void){return missing;}\n",
                                      BUSTER_CHECK_NONE, true, 0);
    check_init_test_diagnostic_source(test, "typedef int I __attribute__((vector_size(16)));const I a={1,2,3,4};I b=a+1;\nint f(void){return *1;}\n",
                                      BUSTER_CHECK_NONE, false, 0);
}

static void check_init_test_vector_constants(CheckTest* test, const char* clang)
{
    static const char source[] =
        "typedef signed char V __attribute__((vector_size(8)));typedef unsigned char W __attribute__((vector_size(8)));"
        "typedef int I __attribute__((vector_size(16)));typedef unsigned U __attribute__((vector_size(16)));"
        "typedef int E __attribute__((ext_vector_type(4)));typedef int E2 __attribute__((ext_vector_type(2)));"
        "typedef float F __attribute__((vector_size(16)));typedef float EF __attribute__((ext_vector_type(4)));"
        "typedef _Bool B __attribute__((ext_vector_type(4)));typedef unsigned __int128 Q __attribute__((vector_size(32)));\n"
        "V vector_arith=((V){1,2,3,4,5,6,7,8}+(V){8,7,6,5,4,3,2,1})*2;\n"
        "I vector_chain=((I){1,2,3,4}+5)*(I){2,3,4,5}-(I){1,2,3,4};\n"
        "V vector_negative=-(V){1,-2,127,-128,0,3,-4,5};V vector_complement=~(V){0,1,2,3,4,5,6,7};\n"
        "V vector_bits=((V){-16,-86,85,0,-16,51,112,-128}&(V){15,-1,15,-1,-16,-16,15,-1})^(V){1,2,3,4,5,6,7,8};\n"
        "V vector_divide=(V){-127,-8,9,15,32,7,1,-1}/(V){-1,2,2,4,-8,3,1,1};\n"
        "V vector_remainder=(V){-127,-8,9,15,32,7,1,-1}%(V){-1,2,2,4,-8,3,1,1};\n"
        "V vector_shift=(V){1,-2,3,-4,5,-6,7,-8}<<(V){0,1,2,3,4,5,6,7};\n"
        "V vector_reverse=(V){2,4,8,16,-2,-4,-8,-16}<<-1;\n"
        "V vector_overflow=(V){127,-128,-1,0,1,2,3,4}+1;\n"
        "I vector_overshift=(I){1,-1,7,-8}<<32;I vector_signed_div=(I){(-2147483647-1),1,2,3}/(I){-1,1,1,1};\n"
        "I vector_compare=(I){-1,0,3,4}<(I){0,0,4,3};V vector_unsigned=(W){255,0,128,127,1,2,3,0}>0;\n"
        "F vector_float_arith=(F){1,2,3,4}*2+1;F vector_float_negative=-(F){1,-0.0f,2,-4};\n"
        "I vector_float_compare=(F){1,2,3,4}==(F){1,0,3,5};\n"
        "E vector_swizzle=(+(E){1,2,3,4}).yxwz;E vector_swizzle_chain=((+(E){1,2,3,4}).wzyx+10).yxwz;\n"
        "E2 vector_half=(+(E){1,2,3,4}).hi;E2 vector_odd=(+(E){1,2,3,4}).odd;\n"
        "B vector_bool_logic=(!(B){1,0,1,0})&(B){1,1,0,1};\n"
        "E vector_logic=(E){1,0,3,4}&&(E){1,1,2,0};\n"
        "E vector_selected=1?(E){5,6,7,8}:(E){1,2,3,4};\n"
        "EF vector_payload=((EF)(U){0x7fc12345,0x80000000,0x3f800000,0}).yxwz;\n"
        "Q vector_wide=((Q){((unsigned __int128)1<<100),3}>>1)+1;\n"
        "int vector_index=((I){11,22,33,44})[2];int vector_member=((E){5,6,7,8}).y;\n"
        "int vector_index_math=2*((I){1,2,3,4})[1]+1;E vector_index_splat=(E)(((I){1,2,3,4})[1]);\n"
        "E vector_not=!(E){1,0,-1,0};E vector_or=(E){1,0,-1,0}||(E){0,0,0,4};\n";
    static const CheckInitExpected expected[] = {
        { .name = "vector_arith", .bytes = { 18,18,18,18,18,18,18,18 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_chain", .bytes = { [0] = 11, [4] = 19, [8] = 29, [12] = 41 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_negative", .bytes = { 255,2,129,128,0,253,4,251 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_complement", .bytes = { 255,254,253,252,251,250,249,248 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_bits", .bytes = { 1,168,6,4,245,54,7,136 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_divide", .bytes = { 127,252,4,3,252,2,1,255 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_remainder", .bytes = { 0,0,1,3,0,1,0,0 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_shift", .bytes = { 1,252,12,224,80,64,192,0 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_reverse", .bytes = { 1,2,4,8,255,254,252,248 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_overflow", .bytes = { 128,129,0,1,2,3,4,5 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_overshift", .bytes = { [3] = 128, [7] = 128, [11] = 128 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_signed_div", .bytes = { [3] = 128, [4] = 1, [8] = 2, [12] = 3 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_compare", .bytes = { 255,255,255,255,0,0,0,0,255,255,255,255 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_unsigned", .bytes = { 255,0,255,255,255,255,255,0 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_float_arith", .bytes = { [2] = 0x40, [3] = 0x40, [6] = 0xa0, [7] = 0x40, [10] = 0xe0, [11] = 0x40, [14] = 0x10, [15] = 0x41 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_float_negative", .bytes = { [2] = 0x80, [3] = 0xbf, [11] = 0xc0, [14] = 0x80, [15] = 0x40 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_float_compare", .bytes = { 255,255,255,255,0,0,0,0,255,255,255,255 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_swizzle", .bytes = { [0] = 2, [4] = 1, [8] = 4, [12] = 3 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_swizzle_chain", .bytes = { [0] = 13, [4] = 14, [8] = 11, [12] = 12 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_half", .bytes = { [0] = 3, [4] = 4 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_odd", .bytes = { [0] = 2, [4] = 4 }, .size = 8, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_bool_logic", .bytes = { 10 }, .size = 1, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_logic", .bytes = { [0] = 1, [8] = 1 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_selected", .bytes = { [0] = 5, [4] = 6, [8] = 7, [12] = 8 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_payload", .bytes = { [3] = 0x80, [4] = 0x45, [5] = 0x23, [6] = 0xc1, [7] = 0x7f, [14] = 0x80, [15] = 0x3f }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_wide", .bytes = { [0] = 1, [12] = 8, [16] = 2 }, .size = 32, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_index", .bytes = { 33 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_member", .bytes = { 6 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_index_math", .bytes = { 5 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_index_splat", .bytes = { [0] = 2, [4] = 2, [8] = 2, [12] = 2 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_not", .bytes = { 0,0,0,0,255,255,255,255,0,0,0,0,255,255,255,255 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_or", .bytes = { [0] = 1, [8] = 1, [12] = 1 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    static const char* rejected[] = {
        "typedef int I __attribute__((vector_size(16)));I x;I y=x+(I){1,2,3,4};",
        "typedef int I __attribute__((vector_size(16)));int f(void);I y=(I){1,2,3,4}+f();",
        "typedef int I __attribute__((vector_size(16)));I y=(I){1,2,3,4}/(I){0,1,1,1};",
        "typedef int E __attribute__((ext_vector_type(4)));E y=(E){1,0,1,0}?(E){1,2,3,4}:(E){5,6,7,8};",
        "typedef int I __attribute__((vector_size(16)));unsigned __int128 y=(unsigned __int128)((I){1,2,3,4});",
        "typedef int I __attribute__((vector_size(16)));int y=((I){1,2,3,4}+1)[2];",
        "typedef int E __attribute__((ext_vector_type(4)));int y=((E){1,2,3,4}).yx.x;",
        "typedef int E __attribute__((ext_vector_type(4)));E y=((E){1,2,3,4}).yxwz;",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        check_init_test_source(test, rejected[i], NULL, 0, false, false, NULL);
    }
}

static void check_init_test_vectors(CheckTest* test, const char* clang)
{
    check_init_test_vector_objects(test, clang);
    check_init_test_vector_constants(test, clang);
    static const char source[] =
        "typedef int I __attribute__((vector_size(16)));typedef int E __attribute__((ext_vector_type(4)));"
        "typedef int E3 __attribute__((ext_vector_type(3)));typedef float F __attribute__((vector_size(16)));"
        "typedef _Bool B __attribute__((ext_vector_type(4)));\n"
        "I vector_lanes={1,2,3,4};I vector_partial={5};const I vector_const={6,7};I vector_zero;\n"
        "E vector_splat=8;E vector_cast=(E)9;E vector_braced={10};E3 vector_three={11,12,13};\n"
        "I vector_copy=(I){14,15};I vector_bitcast=(I)(__int128)0x1234;\n"
        "I vector_float_bits=(I)(F){1.0f,-0.0f,2.0f,0.5f};\n"
        "B vector_bool={1,0,2,0};B vector_bool_splat=1;\n"
        "I vector_rows[][4]={{ {1,2,3,4} }};\n"
        "E vector_array[]={1,2};I vector_elision[]={1,2,3,4,5};\n"
        "_Thread_local I vector_tls={17};_Thread_local E vector_tls_zero;\n";
    static const CheckInitExpected expected[] = {
        { .name = "vector_lanes", .bytes = { [0] = 1, [4] = 2, [8] = 3, [12] = 4 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_partial", .bytes = { 5 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_const", .bytes = { [0] = 6, [4] = 7 }, .size = 16, .class = BUSTER_LINK_CLASS_RODATA, .alignment_log2 = 4 },
        { .name = "vector_zero", .size = 16, .class = BUSTER_LINK_CLASS_BSS, .alignment_log2 = 4 },
        { .name = "vector_splat", .bytes = { [0] = 8, [4] = 8, [8] = 8, [12] = 8 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_cast", .bytes = { [0] = 9, [4] = 9, [8] = 9, [12] = 9 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_braced", .bytes = { 10 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_three", .bytes = { [0] = 11, [4] = 12, [8] = 13 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_copy", .bytes = { [0] = 14, [4] = 15 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_bitcast", .bytes = { 0x34, 0x12 }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_float_bits", .bytes = { [2] = 0x80, [3] = 0x3f, [7] = 0x80, [11] = 0x40, [15] = 0x3f }, .size = 16, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_bool", .bytes = { 5 }, .size = 1, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_bool_splat", .bytes = { 15 }, .size = 1, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "vector_rows", .bytes = { [0] = 1, [4] = 2, [8] = 3, [12] = 4 }, .size = 64, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_array", .bytes = { [0] = 1, [4] = 1, [8] = 1, [12] = 1, [16] = 2, [20] = 2, [24] = 2, [28] = 2 }, .size = 32, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_elision", .bytes = { [0] = 1, [4] = 2, [8] = 3, [12] = 4, [16] = 5 }, .size = 32, .class = BUSTER_LINK_CLASS_DATA, .alignment_log2 = 4 },
        { .name = "vector_tls", .bytes = { 17 }, .size = 16, .class = BUSTER_LINK_CLASS_TDATA, .alignment_log2 = 4 },
        { .name = "vector_tls_zero", .size = 16, .class = BUSTER_LINK_CLASS_TBSS, .alignment_log2 = 4 },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    static const char* accepted[] = {
        "typedef int I __attribute__((vector_size(16)));int f(int x){I a={x,2,3,4};return a[0];}",
        "typedef int E __attribute__((ext_vector_type(4)));E f(int x){E a=x;return a;}",
        "typedef int I __attribute__((vector_size(16)));I f(I x){I a={x};return a;}",
        "typedef int I __attribute__((vector_size(16)));I f(I x){I a={x,1};return a;}",
        "typedef int I __attribute__((vector_size(16)));I a={1,2,3,4,5};",
        "typedef int I __attribute__((vector_size(16)));I a={{1,2},3};",
    };
    static const char* rejected[] = {
        "typedef int I __attribute__((vector_size(16)));I a=1;",
        "typedef int I __attribute__((vector_size(16)));I a={[1]=2};",
        "typedef int E __attribute__((ext_vector_type(4)));E a={.x=1};",
        "typedef int E __attribute__((ext_vector_type(4)));typedef int E2 __attribute__((ext_vector_type(2)));E a={(E2){1,2},(E2){3,4}};",
        "typedef int I __attribute__((vector_size(16)));typedef int E3 __attribute__((ext_vector_type(3)));E3 a=(I){1,2,3,4};",
        "typedef int I __attribute__((vector_size(16)));int f(void);I a={1,f()};",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(accepted); i += 1)
    {
        check_init_test_source(test, accepted[i], NULL, 0, true, false, NULL);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(rejected); i += 1)
    {
        check_init_test_source(test, rejected[i], NULL, 0, false, false, NULL);
    }
}

static __attribute__((noinline)) void check_test_initializers(CheckTest* test, const char* clang)
{
    check_init_test_overload_constraints(test, clang);
    static const char overloads[] =
        "#define O __attribute__((overloadable))\n"
        "int choose(int) O; int (*overload_early)(int)=choose;\n"
        "double choose(double) O;\n"
        "int (*overload_i)(int)=(choose); double (*overload_d)(double)=&choose;\n"
        "struct Pair{int (*i)(int);double (*d)(double);};\n"
        "struct Pair overload_pair={.d=choose,.i=choose};\n"
        "struct Pair overload_rows[2]={{choose,choose},{.d=&choose,.i=(int(*)(int))choose}};\n"
        "int choose(int) O __attribute__((unavailable));\n"
        "struct Pair overload_after={_Generic(0,int:choose),choose};\n"
        "void body(void){struct Pair overload_auto={choose,choose};}\n";
    check_init_test_source(test, overloads, NULL, 0, true, false, clang);
    static const char unmarked[] =
        "int plain(int); double plain(double) __attribute__((overloadable));\n"
        "int (*plain_address)(int)=plain; struct P{int (*p)(int);} plain_record={plain};\n";
    static const CheckInitExpected plain[] = {
        {
            .name = "plain_address",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "plain_record",
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    check_init_test_source(test, unmarked, plain, BUSTER_ARRAY_LENGTH(plain), true, true, clang);
    check_init_test_vectors(test, clang);
    check_init_test_ranges(test, clang);
    check_init_test_flexible(test, clang);
    check_init_test_complex(test, clang);
    check_init_test_diagnostics(test, clang);
    check_init_test_excess(test, clang);
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
        "struct S{int a;} s={.missing=1};",
        "int a[2]={[-1]=3};",
        "int a[2]={[2]=3};",
        "struct S{int *p;} s={3};",
        "char a[3]=\"abc\"+1;",
        "char a[3]={[0]=\"ab\"};",
        "int x;struct S{int a;} s={x};",
        "struct S{int x,y;} value=((struct S[2]){{1,2},{3,4}})[1];",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        check_init_test_source(test, invalid[i], 0, 0, false, false, clang);
    }
}

#endif
