// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #622: distinguish the lowering's hard limits from its storage, without allocating a limit's worth of entries;
// then compile and execute a generated function whose 65,537 locals cross the old 65,536-local boundary.

static void check_test_lower_capacity(CheckTest* test)
{
    for (uint32_t shape = 0; shape < 6; shape += 1)
    {
        const bool entries = shape >= 4;
        const bool limit = (shape & 1) == 0;
        const uint32_t count = limit ? BUSTER_TIER0_MAX_ENTRIES : 1;
        const uint32_t locals = limit ? BUSTER_TIER0_MAX_LOCALS : 1;
        CheckProgramLowered lowered = { 0 };
        CheckLowerWriter writer = {
            .output = &lowered,
            .token = 37,
            .count = entries ? count : 0,
            .capacity = limit ? BUSTER_TIER0_MAX_ENTRIES + 1 : count,
            .local_count = entries ? 0 : locals,
            .local_capacity = limit ? BUSTER_TIER0_MAX_LOCALS + 1 : locals,
            .class_limit = BUSTER_TYPED_CLASSES,
        };
        if (entries)
        {
            check_lower_emit(&writer, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U32, 0);
        }
        else if (shape < 2)
        {
            check_lower_temporary(&writer, BUSTER_TYPED_CLASS_U32);
        }
        else
        {
            check_lower_object(&writer, 16, 4);
        }
        const char* reason = entries ? (limit ? "Tier 0 per-function entry limit" : "lowering entry storage") :
                                      (limit ? "Tier 0 per-function local limit" : "lowering local storage");
        check_test_expect(test, lowered.status == CHECK_PROGRAM_RESOURCE && lowered.token == 37 && lowered.reason != NULL &&
                                strcmp(lowered.reason, reason) == 0 && writer.count == (entries ? count : 0) &&
                                writer.local_count == (entries ? 0 : locals),
                          "entry, value-local and object-local exhaustion name the hard limit or storage before accessing columns");
        writer.token = 99;
        check_lower_resource(&writer, "later failure");
        check_test_expect(test, lowered.token == 37 && lowered.reason != NULL && strcmp(lowered.reason, reason) == 0,
                          "capacity reporting preserves the first failure's token and reason");
        Arena output = arena_reserve(4096);
        uint64_t* sentinel = BUSTER_ARENA_PUSH_ARRAY(&output, uint64_t, 1);
        if (sentinel != NULL)
        {
            *sentinel = UINT64_C(0x622);
        }
        const uint64_t mark = output.length;
        arena_push(&output, 64, 8);
        check_lower_failed(&lowered, &output, mark);
        check_test_expect(test, sentinel != NULL && *sentinel == UINT64_C(0x622) && output.length == mark && lowered.functions == NULL &&
                                lowered.status == CHECK_PROGRAM_RESOURCE && lowered.token == 37 && lowered.reason != NULL &&
                                strcmp(lowered.reason, reason) == 0,
                          "lowering rollback retains the named capacity and publishes no function");
        arena_release(&output);
        FILE* diagnostics = tmpfile();
        check_test_expect(test, diagnostics != NULL, "capacity diagnostic output is available");
        if (diagnostics != NULL)
        {
            Parser parser = { 0 };
            Preprocessor pp = { .options = { .diagnostics = diagnostics } };
            const CheckProgramCode code = { .status = lowered.status, .token = BUSTER_CHECK_NONE, .reason = lowered.reason };
            check_driver_emission_failed(&parser, &pp, &code);
            char* text = NULL;
            size_t length = 0;
            const bool read = pp_test_read_output(diagnostics, &text, &length);
            char expected[160];
            snprintf(expected, sizeof(expected), "error: code output capacity exhausted: %s\n", reason);
            check_test_expect(test, read && text != NULL && strstr(text, expected) != NULL && pp.errors == 1,
                              "the production driver renders the exhausted capacity's name");
            free(text);
            fclose(diagnostics);
        }
    }
    static const struct
    {
        uint32_t rows;
        uint32_t room;
        bool valid;
    } operation_rooms[] = {
        { 0, 4096, true },
        { 1, 4097, true },
        { 4097, 8193, true },
        { UINT32_MAX - 4096, UINT32_MAX, true },
        { UINT32_MAX - 4095, 0, false },
        { UINT32_MAX, 0, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(operation_rooms); i += 1)
    {
        uint32_t room = 17;
        check_test_expect(test, check_lower_operation_room(operation_rooms[i].rows, &room) == operation_rooms[i].valid &&
                                room == operation_rooms[i].room,
                          "placed-asm operation room preserves the non-asm allowance and rejects overflow before narrowing");
    }
    Tier0Operation rows[1] = { 0 };
    const Tier0Operation different = { .record_count = 1 };
    CheckProgramLowered lowered = { 0 };
    CheckLowerWriter writer = {
        .output = &lowered,
        .token = 37,
        .operations = rows,
        .operation_count = 1,
        .operation_capacity = 1,
    };
    check_test_expect(test, check_lower_operation_add(&writer, rows) == 0 && lowered.status == CHECK_PROGRAM_OK && writer.operation_count == 1,
                      "a full operation table still accepts a duplicate row without another slot");
    check_lower_operation_add(&writer, &different);
    check_test_expect(test, lowered.status == CHECK_PROGRAM_RESOURCE && lowered.token == 37 && lowered.reason != NULL &&
                            strcmp(lowered.reason, "lowering machine-operation storage") == 0 && writer.operation_count == 1 && rows[0].record_count == 0,
                      "new-row storage exhaustion has a reason and leaves the full operation table unchanged");
    writer.token = 99;
    check_lower_resource(&writer, "later failure");
    Arena output = arena_reserve(4096);
    const uint64_t mark = output.length;
    arena_push(&output, 64, 8);
    check_lower_failed(&lowered, &output, mark);
    check_test_expect(test, output.length == mark && lowered.status == CHECK_PROGRAM_RESOURCE && lowered.token == 37 &&
                            lowered.reason != NULL && strcmp(lowered.reason, "lowering machine-operation storage") == 0 && lowered.functions == NULL,
                      "operation-storage failure preserves its first reason and token through output rollback");
    arena_release(&output);
}

static void check_test_driver_large_function(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    // One parameter plus two locals per block: the first function size the old local cap couldn't compile.
    const uint32_t blocks = 32768;
    char source[256];
    char objects[2][256];
    char output[256];
    snprintf(source, sizeof(source), "%s/large-function.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/large-function.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/large-function-scalar.o", directory);
    snprintf(output, sizeof(output), "%s/large-function", directory);
    FILE* file = fopen(source, "w");
    bool written = file != NULL && fprintf(file, "unsigned f(unsigned x)\n{\n") > 0;
    for (uint32_t b = 0; written && b < blocks; b += 1)
    {
        written = fprintf(file, "{ unsigned x_%u=x+%uu; unsigned v=x_%u*3u; x^=v; }\n", b % 64, b, b % 64) > 0;
    }
    written = written && fprintf(file, "return x;\n}\nint main(void)\n{\n"
                                      "unsigned inputs[4]={0,1,0x80000000u,0xffffffffu};\n"
                                      "for(unsigned k=0;k<4;k++){unsigned x=inputs[k];\n"
                                      "for(unsigned i=0;i<%uu;i++){x^=(x+i)*3u;}\n"
                                      "if(f(inputs[k])!=x)return 1;}\nreturn 0;\n}\n", blocks) > 0;
    if (file != NULL)
    {
        written = fclose(file) == 0 && written;
    }
    check_test_expect(test, written, "large function and independent unsigned arithmetic model are written");
    char* text = NULL;
    char* const oracle[] = { (char*)clang, "-O0", source, "-o", output, NULL };
    const bool accepted = written && check_test_driver_spawn(oracle, &text) == 0;
    free(text);
    text = NULL;
    check_test_expect(test, accepted, "clang compiles the function beyond the former local limit");
    char* const execute[] = { output, NULL };
    const bool matched = accepted && check_test_driver_spawn(execute, &text) == 0;
    free(text);
    text = NULL;
    check_test_expect(test, matched, "clang's generated function agrees with the loop model on four inputs");
    bool compiled[2] = { false, false };
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        CheckCorpusOracle checked = { 0 };
        uint32_t warnings = 0;
        compiled[path] = check_test_driver_run(executable, source, objects[path], path != 0, false, false, false, &checked, &warnings) &&
                         !checked.rejected && warnings == 0;
        check_test_expect(test, compiled[path], path == 0 ? "vector driver compiles 65,537 locals" : "scalar driver compiles 65,537 locals");
        char* const link[] = { (char*)clang, objects[path], "-o", output, NULL };
        const bool linked = compiled[path] && check_test_driver_spawn(link, &text) == 0;
        free(text);
        text = NULL;
        check_test_expect(test, linked, "large function's object links with the system toolchain");
        const bool runs = linked && check_test_driver_spawn(execute, &text) == 0;
        free(text);
        text = NULL;
        check_test_expect(test, runs, "large function's native code agrees with the independent loop model on four inputs");
    }
    check_test_expect(test, compiled[0] && compiled[1] && check_test_driver_same_files(objects[0], objects[1]),
                      "both paths emit identical objects beyond the former local limit");
    unlink(source);
    unlink(objects[0]);
    unlink(objects[1]);
    unlink(output);
}

// A distinct placed template takes one machine-operation row, while a repeated template reuses one. Cross the
// former 4096-row boundary in one body, retain a below-boundary body, and execute all three against unsigned models.
static void check_test_driver_large_asm(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    const uint32_t counts[] = { 4095, 4125, 4125 };
    static const char* const names[] = { "below", "distinct", "repeated" };
    char source[256];
    char objects[2][256];
    char output[256];
    snprintf(source, sizeof(source), "%s/large-asm.c", directory);
    snprintf(objects[0], sizeof(objects[0]), "%s/large-asm.o", directory);
    snprintf(objects[1], sizeof(objects[1]), "%s/large-asm-scalar.o", directory);
    snprintf(output, sizeof(output), "%s/large-asm", directory);
    FILE* file = fopen(source, "w");
    bool written = file != NULL && fprintf(file, "typedef unsigned V __attribute__((vector_size(16)));\n") > 0;
    for (uint32_t shape = 0; written && shape < BUSTER_ARRAY_LENGTH(counts); shape += 1)
    {
        written = fprintf(file, "unsigned %s(unsigned x)\n{\n", names[shape]) > 0;
        if (written && shape == 1)
        {
            written = fprintf(file, "V v={x,x,x,x};v=v+(V){1,2,3,4};\n") > 0;
        }
        for (uint32_t i = 0; written && i < counts[shape]; i += 1)
        {
            written = fprintf(file, "__asm__ volatile(\"addl $%u, %%0\" : \"+r\"(x));\n", shape == 2 ? 1 : i) > 0;
        }
        written = written && fprintf(file, shape == 1 ? "return x+v[0];\n}\n" : "return x;\n}\n") > 0;
    }
    written = written && fprintf(file, "int main(void)\n{\n"
                                      "unsigned inputs[4]={0,1,0x80000000u,0xffffffffu};\n"
                                      "for(unsigned k=0;k<4;k++){unsigned x=inputs[k];\n"
                                      "if(below(x)!=x+%uu || distinct(x)!=x+%uu+x+1u || repeated(x)!=x+%uu)return 1;}\n"
                                      "return 0;\n}\n",
                                 counts[0] * (counts[0] - 1) / 2, counts[1] * (counts[1] - 1) / 2, counts[2]) > 0;
    if (file != NULL)
    {
        written = fclose(file) == 0 && written;
    }
    check_test_expect(test, written, "distinct and repeated asm-template boundary fixtures and independent unsigned models are written");
    char* text = NULL;
    char* const oracle[] = { (char*)clang, "-O0", source, "-o", output, NULL };
    const bool accepted = written && check_test_driver_spawn(oracle, &text) == 0;
    free(text);
    text = NULL;
    check_test_expect(test, accepted, "Clang compiles functions below and above the former distinct-asm row limit");
    char* const execute[] = { output, NULL };
    check_test_expect(test, accepted && check_test_driver_spawn(execute, &text) == 0,
                      "Clang's distinct and repeated asm functions match the independent unsigned models");
    free(text);
    text = NULL;
    bool compiled[2] = { false, false };
    for (uint32_t path = 0; written && path < 2; path += 1)
    {
        CheckCorpusOracle checked = { 0 };
        uint32_t warnings = 0;
        compiled[path] = check_test_driver_run(executable, source, objects[path], path != 0, false, false, false, &checked, &warnings) &&
                         !checked.rejected && warnings == 0;
        check_test_expect(test, compiled[path], path == 0 ? "vector driver compiles 4,125 distinct asm rows" : "scalar driver compiles 4,125 distinct asm rows");
        char* const link[] = { (char*)clang, objects[path], "-o", output, NULL };
        const bool linked = compiled[path] && check_test_driver_spawn(link, &text) == 0;
        free(text);
        text = NULL;
        check_test_expect(test, linked, "large asm functions' object links with the system toolchain");
        check_test_expect(test, linked && check_test_driver_spawn(execute, &text) == 0,
                          "compiled asm functions agree with independent unsigned models on four inputs");
        free(text);
        text = NULL;
    }
    check_test_expect(test, compiled[0] && compiled[1] && check_test_driver_same_files(objects[0], objects[1]),
                      "both kernels emit identical objects beyond the former distinct-asm row limit");
    unlink(source);
    unlink(objects[0]);
    unlink(objects[1]);
    unlink(output);
}
