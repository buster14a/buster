// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Real resource files, independently stated object bytes and Clang's object oracle exercise the compact handoff.
// Resource length must not change parser-token, graph-node or initializer-part counts for a flat byte array.

static void check_embed_test_driver(CheckTest* test, const char* directory, const char* resource)
{
    char source_path[192];
    char executable[192];
    char out[192];
    char err[192];
    snprintf(source_path, sizeof(source_path), "%s/driver.c", directory);
    snprintf(executable, sizeof(executable), "%s/driver", directory);
    snprintf(out, sizeof(out), "%s/out", directory);
    snprintf(err, sizeof(err), "%s/err", directory);
    char source[2048];
    snprintf(source, sizeof(source),
        "static const unsigned char global_blob[]={\n#embed \"%s\" limit(513) prefix(5,) suffix(,6)\n};\n"
        "int get(int i){return global_blob[i];}\n"
        "int check(void){unsigned char local_blob[520]={\n#embed \"%s\" limit(513) prefix(5,) suffix(,6)\n};"
        "for(int i=0;i<513;i++){unsigned char value=i*37+i/251;"
        "if(local_blob[i+1]!=value||get(i+1)!=value)return 1;}"
        "return sizeof global_blob!=515||sizeof local_blob!=520||get(0)!=5||get(514)!=6||"
        "local_blob[0]!=5||local_blob[514]!=6||local_blob[519]!=0;}\n"
        "int main(void){return check();}\n", resource, resource);
    const bool written = parse_test_write(source_path, source, strlen(source));
    check_test_expect(test, written, "production-driver embed source fixture");
    for (uint32_t path = 0; written && path < 4; path += 1)
    {
        char* lanes = path < 2 ? "--lanes=1" : "--lanes=3";
        char* vector[] = { "/proc/self/exe", "-std=c23", lanes, "-o", executable, source_path, NULL };
        char* scalar[] = { "/proc/self/exe", "--scalar", "-std=c23", lanes, "-o", executable, source_path, NULL };
        char* run[] = { executable, NULL };
        const bool compiled = object_test_run(path % 2 == 0 ? vector : scalar, out, err) == 0;
        check_test_expect(test, compiled && object_test_run(run, out, err) == 0,
                          "production C23 driver compiles and runs static and automatic embed bytes with independent assertions");
    }
    char* deferred[] = { "/proc/self/exe", "test-checker-corpus", "--deferred", "3", "--symbols", "--symbols-minimum", "1", "-std=c23", source_path, NULL };
    const bool deferred_checked = written && object_test_run(deferred, out, err) == 0;
    Arena scratch = arena_reserve(UINT64_C(1) << 16);
    const String8 deferred_output = deferred_checked ? object_test_read_file(&scratch, out) : (String8){ 0 };
    const char summary[] = "units=1 source_order=0 bodies=3 differences=0";
    bool merged = false;
    for (uint64_t i = 0; deferred_output.pointer != NULL && i + sizeof(summary) - 1 <= deferred_output.length; i += 1)
    {
        merged |= memcmp(deferred_output.pointer + i, summary, sizeof(summary) - 1) == 0;
    }
    check_test_expect(test, deferred_checked && merged, "three deferred body lanes merge resource initializer modes and borrowed PP spans with source-order and Clang agreement");
    snprintf(source, sizeof(source), "unsigned char excess[3]={\n#embed \"%s\" limit(7) suffix(,99,98)\n};\n", resource);
    const bool excess_written = parse_test_write(source_path, source, strlen(source));
    char* syntax[] = { "/proc/self/exe", "-std=c23", "-fsyntax-only", source_path, NULL };
    const bool checked = excess_written && object_test_run(syntax, out, err) == 0;
    arena_pop_to(&scratch, 0);
    const String8 diagnostic = checked ? object_test_read_file(&scratch, err) : (String8){ 0 };
    uint32_t warnings = 0;
    for (uint64_t i = 0; diagnostic.pointer != NULL && i + 8 <= diagnostic.length; i += 1)
    {
        warnings += memcmp(diagnostic.pointer + i, "warning:", 8) == 0;
    }
    check_test_expect(test, checked && warnings == 1, "production driver emits exactly one excess warning before the ignored resource and suffix tail");
    arena_release(&scratch);
    unlink(source_path);
    unlink(executable);
    unlink(out);
    unlink(err);
}

static void check_embed_test_compact(CheckTest* test, const char* resource, const uint8_t* expected, uint32_t length, bool truncated)
{
    char source[256];
    if (truncated)
    {
        snprintf(source, sizeof(source), "const unsigned char compact[%u]={\n#embed \"%s\" suffix(,99,98)\n};\n", length, resource);
    }
    else
    {
        snprintf(source, sizeof(source), "const unsigned char compact[]={\n#embed \"%s\"\n};\n", resource);
    }
    for (uint32_t path = 0; path < 2; path += 1)
    {
        static InternTable identifiers;
        static Preprocessor pp;
        static Parser parser;
        CheckTestTypes types = { 0 };
        Arena input_arena = arena_reserve(UINT64_C(1) << 26);
        Arena output = arena_reserve(UINT64_C(1) << 26);
        Arena bytes = arena_reserve(UINT64_C(1) << 26);
        Arena scratch = arena_reserve(UINT64_C(1) << 26);
        const PPOptions pp_options = {
            .language = { .standard = 2023 },
            .no_standard_includes = true,
            .scalar = path == 0,
        };
        const ParseOptions parse_options = { .scalar = path == 0 };
        const CheckOptions options = { .scalar = path == 0 };
        bool ready = intern_initialize(&identifiers) && pp_initialize(&pp, &pp_options, &identifiers) &&
                     pp_run_source(&pp, "compact-embed.c", (const uint8_t*)source, (uint32_t)strlen(source)) && check_test_types_open(&types) &&
                     parse_initialize(&parser, &parse_options, pp_options.language) && parse_translation_unit(&parser, &pp);
        CheckInput input = { 0 };
        CheckStream stream = { 0 };
        CheckInitializers initializers = { 0 };
        CheckDataInput data = { 0 };
        ready = ready && check_input(&input, &parser, &pp, &types.table, &input_arena, &scratch) && input.report_count == 0 &&
                check_graph_run(&types.table, &input.graph, &input_arena, &scratch, &options, &stream) && stream.report_count == 0 &&
                check_initializers_run(&initializers, &input, &stream, &parser, &types.table, &input_arena, &scratch);
        check_test_expect(test, ready, "large embed resource reaches checked initializer plans on both paths");
        if (ready)
        {
            const CheckInitializer* plan = initializers.count == 1 ? initializers.plans : NULL;
            const CheckInitPart* part = plan != NULL && plan->count == 1 ? plan->parts : NULL;
            check_test_expect(test, pp.embed_count == 1 && pp.output.count < 32 && parser.tokens.count < 32 && input.graph.count < 16 &&
                                   part != NULL && plan->object_size == length && part->kind == CHECK_INIT_BYTES && part->byte_count == length &&
                                   plan->warning_count == (uint32_t)truncated && initializers.warning_count == (uint32_t)truncated &&
                                   memcmp(part->bytes, expected, length) == 0,
                              "resource size leaves token, graph and part counts bounded and preserves every byte without a terminator");
            const bool emitted = check_data_input(&data, &input, &stream, &parser, &identifiers, &types.table, &output, &bytes, &scratch);
            const SymbolId symbol = emitted ? check_init_test_symbol(&data.link, "compact") : 0;
            const AtomId atom = symbol != 0 ? data.link.symbols.atom[symbol] : 0;
            check_test_expect(test, atom != 0 && data.link.atoms.size[atom] == length && data.link.atoms.bytes[atom] != NULL &&
                                   memcmp(data.link.atoms.bytes[atom], expected, length) == 0,
                              "large embed static object matches independently generated resource bytes");
        }
        check_test_expect(test, scratch.length == 0, "embed plans and static emission restore scratch");
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&bytes);
        arena_release(&output);
        arena_release(&input_arena);
    }
}

static void check_test_embed_initializers(CheckTest* test, const char* clang)
{
    char directory[] = "/tmp/buster-check-embed-XXXXXX";
    const bool created = mkdtemp(directory) != NULL;
    char resource[160];
    char empty[160];
    snprintf(resource, sizeof(resource), "%s/data.bin", directory);
    snprintf(empty, sizeof(empty), "%s/empty.bin", directory);
    const uint8_t bytes[] = { 0, 1, 127, 128, 254, 255, 42 };
    const bool written = created && parse_test_write(resource, (const char*)bytes, sizeof(bytes)) && parse_test_write(empty, "", 0);
    check_test_expect(test, written, "embed fixture resource files");
    if (written)
    {
        char source[4096];
        snprintf(source, sizeof(source),
            "unsigned char embedded[]={\n#embed \"%s\"\n};\n"
            "signed char signed_bytes[]={\n#embed \"%s\"\n};\n"
            "unsigned char surrounded[10]={\n#embed \"%s\" prefix(9,) suffix(,8)\n};\n"
            "unsigned char limited[]={\n#embed \"%s\" limit(3)\n};\n"
            "unsigned char offset_bytes[]={\n#embed \"%s\" clang::offset(2) limit(3)\n};\n"
            "unsigned char empty_bytes[]={\n#embed \"%s\" prefix(9,) suffix(,8) if_empty(5,6)\n};\n"
            "unsigned char zero_limit[]={\n#embed \"%s\" limit(0) prefix(9,) suffix(,8) if_empty(4)\n};\n"
            "unsigned char designated[12]={[3]=\n#embed \"%s\"\n};\n"
            "unsigned char rows[][3]={\n#embed \"%s\" limit(6)\n};\n"
            "struct Packet{unsigned char a[3];unsigned char b[4];} packet={\n#embed \"%s\"\n};\n"
            "unsigned char excess[3]={\n#embed \"%s\"\n};\n"
            "int one[]={\n#embed \"%s\" limit(1) prefix(3,)\n};\n",
            resource, resource, resource, resource, resource, empty, resource, resource, resource, resource, resource, resource);
        static const CheckInitExpected expected[] = {
            {
                .name = "embedded",
                .bytes = { 0, 1, 127, 128, 254, 255, 42 },
                .size = 7,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "signed_bytes",
                .bytes = { 0, 1, 127, 128, 254, 255, 42 },
                .size = 7,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "surrounded",
                .bytes = { 9, 0, 1, 127, 128, 254, 255, 42, 8, 0 },
                .size = 10,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "limited",
                .bytes = { 0, 1, 127 },
                .size = 3,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "offset_bytes",
                .bytes = { 127, 128, 254 },
                .size = 3,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "empty_bytes",
                .bytes = { 5, 6 },
                .size = 2,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "zero_limit",
                .bytes = { 4 },
                .size = 1,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "designated",
                .bytes = { 0, 0, 0, 0, 1, 127, 128, 254, 255, 42, 0, 0 },
                .size = 12,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "rows",
                .bytes = { 0, 1, 127, 128, 254, 255 },
                .size = 6,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "packet",
                .bytes = { 0, 1, 127, 128, 254, 255, 42 },
                .size = 7,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "excess",
                .bytes = { 0, 1, 127 },
                .size = 3,
                .class = BUSTER_LINK_CLASS_DATA,
            },
            {
                .name = "one",
                .bytes = { 3, 0, 0, 0, 0, 0, 0, 0 },
                .size = 8,
                .class = BUSTER_LINK_CLASS_DATA,
            },
        };
        const CLanguage language = { .standard = 2023, .gnu = true };
        check_init_test_source_language(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang, language);
        check_embed_test_compact(test, resource, bytes, 3, true);
        snprintf(source, sizeof(source), "int wider[]={\n#embed \"%s\"\n};\n", resource);
        check_init_test_source_language(test, source, NULL, 0, false, false, NULL, language);
        snprintf(source, sizeof(source), "unsigned char ranged[10]={[0 ... 3]=\n#embed \"%s\"\n};\n", resource);
        check_init_test_source_language(test, source, NULL, 0, false, false, NULL, language);
        snprintf(source, sizeof(source), "unsigned char changed[]={\n#embed \"%s\" prefix(+)\n};\n", resource);
        check_init_test_source_language(test, source, NULL, 0, false, false, NULL, language);
        snprintf(source, sizeof(source), "int take(int,int);int f(void){return take(\n#embed \"%s\" limit(2)\n);}\n", resource);
        check_init_test_source_language(test, source, NULL, 0, false, false, NULL, language);
        snprintf(source, sizeof(source),
            "int tested(int x){unsigned char a[12]={\n#embed \"%s\" prefix(9,) suffix(,8)\n};"
            "signed char b[]={\n#embed \"%s\"\n};"
            "return a[(unsigned)x%%12]+b[(unsigned)x%%7]+sizeof a+sizeof b;}\n", resource, resource);
        check_test_program_source(test, source, "automatic compact embed, signed conversions and zero tail", clang);

        const uint32_t large_size = 8u << 20;
        uint8_t* large = malloc(large_size);
        char large_resource[160];
        snprintf(large_resource, sizeof(large_resource), "%s/large.bin", directory);
        if (large != NULL)
        {
            for (uint32_t i = 0; i < large_size; i += 1)
            {
                large[i] = (uint8_t)(i * 37 + i / 251);
            }
        }
        const bool large_written = large != NULL && parse_test_write(large_resource, (const char*)large, large_size);
        check_test_expect(test, large_written, "large embed resource fixture");
        if (large_written)
        {
            check_embed_test_compact(test, large_resource, large, large_size, false);
            check_embed_test_driver(test, directory, large_resource);
            snprintf(source, sizeof(source),
                "int tested(int x){unsigned char a[520]={\n#embed \"%s\" limit(513) prefix(5,) suffix(,6)\n};"
                "return a[(unsigned)x%%520]+a[0]+a[513]+a[514]+a[519]+sizeof a;}\n", large_resource);
            check_test_program_source_limits(test, source, "pooled automatic embed keeps code bounded", clang, false, 1, 256);
        }
        unlink(large_resource);
        free(large);
    }
    if (created)
    {
        unlink(resource);
        unlink(empty);
        rmdir(directory);
    }
}
