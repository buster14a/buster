// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Value regressions for #1114, included by c_check_debug_test.c after its process helpers. The fixture prints the
// values gdb must read before those calls, including after entering/leaving a shadowing scope. Check the native
// driver, relocated -c output, and merged translation units; a valid DIE tree alone cannot prove a location.

static void check_debug_values_test_descriptors(CheckDebugTestTally* tally)
{
    for (uint32_t change = 0; change < 19; change += 1)
    {
        uint8_t info[96] = { 92, 0, 0, 0, 5, 0, 1, 8, 0, 0, 0, 0, 1, 'a', 0, 'b', 0, 'c', 0 };
        uint8_t locations[] = { 9, 0, 0, 0, 5, 0, 8, 0, 0, 0, 0, 0, 0 };
        uint8_t ranges[16] = { 12, 0, 0, 0, 5, 0, 8, 0 };
        const uint32_t offset = 12;
        memcpy(info + 36, &offset, 4);
        memcpy(info + 40, &offset, 4);
        String8 sections[BUSTER_NATIVE_DEBUG_SECTIONS] = { 0 };
        sections[0] = (String8){ .pointer = (char*)info, .length = sizeof(info) };
        sections[4] = (String8){ .pointer = (char*)ranges, .length = sizeof(ranges) };
        sections[5] = (String8){ .pointer = (char*)locations, .length = sizeof(locations) };
        NativeDebugField fields[] = { { .offset = 36, .section = 5 }, { .offset = 40, .section = 4 } };
        NativeDebugAddress addresses[] = { { .offset = 48, .symbol = 1 } };
        NativeDebugExtra extra = { .fields = fields, .addresses = addresses, .field_count = 2, .address_count = 1 };
        switch (change)
        {
            case 0: break;
            case 1: extra.fields = NULL; break;
            case 2: extra.addresses = NULL; break;
            case 3: fields[0].offset = 8; break;
            case 4: fields[0].offset = 13; break;
            case 5: fields[1].offset = 38; break;
            case 6: fields[0].section = 6; break;
            case 7: fields[0].section = 0; break;
            case 8: fields[1].offset = 94; break;
            case 9: addresses[0].offset = 38; break;
            case 10: addresses[0].offset = 92; break;
            case 11: addresses[0].offset = 16; break;
            case 12: addresses[0].symbol = 0; break;
            case 13: locations[4] = 4; break;
            case 14: locations[8] = 1; break;
            case 15: sections[5].length -= 1; break;
            case 16: sections[5].pointer = NULL; break;
            case 17: info[0] = 48; break;
            case 18: info[36] = 13; break;
        }
        const bool valid = native_debug_extra_valid(sections, &extra);
        check_debug_test_expect(tally, valid == (change == 0), "value descriptor rejects overlapping, truncated and header-overwriting patches", NULL);
    }
}

static const char check_debug_values_fixture[] =
    "#include <stdio.h>\n"
    "#line 1 \"dbg_values.c\"\n"
    "typedef struct Node { int x; struct Node *next; unsigned flag:3; signed delta:5; } Node;\n"
    "typedef int Count;\n"
    "enum Mode { cold=-2, warm=7 };\n"
    "enum Outer { outer_first=sizeof(enum Inner { inner_only=7 }), outer_last=3 };\n"
    "union Box { int integer; float real; };\n"
    "int initialized_global=41;\n"
    "enum Outer outer_global=outer_first;\n"
    "enum Inner inner_global=inner_only;\n"
    "static int hidden_global=13;\n"
    "int zero_global;\n"
    "volatile int value_sink;\n"
    "int debug_values(int number, double fraction, int *pointer)\n"
    "{\n"
    "    Count local=number+2;\n"
    "    volatile int frame=9;\n"
    "    int array[3]={4,5,6};\n"
    "    Node node={.x=17,.next=0,.flag=4,.delta=-3};\n"
    "    union Box box={.integer=23};\n"
    "    enum Mode mode=warm;\n"
    "    const int *alias=pointer;\n"
    "    _Alignas(64) int aligned=29;\n"
    "    static int static_local=31;\n"
    "    int shadow=11;\n"
    "#line 100 \"dbg_values.c\"\n"
    "    printf(\"VALUES-A %d %.1f %d %d %d %d %d %d %d %d %d %d %d\\n\", number, fraction, *pointer, local, frame, array[2], node.x, node.flag, node.delta, box.integer, mode, *alias, aligned);\n"
    "    printf(\"VALUES-G %d %d %d %d\\n\", initialized_global, hidden_global, zero_global, static_local);\n"
    "    {\n"
    "        int shadow=99;\n"
    "#line 200 \"dbg_values.c\"\n"
    "        printf(\"VALUES-INNER %d\\n\", shadow);\n"
    "    }\n"
    "#line 300 \"dbg_values.c\"\n"
    "    printf(\"VALUES-OUTER %d\\n\", shadow);\n"
    "    return 0;\n"
    "}\n"
    "int debug_dynamic(int n)\n"
    "{\n"
    "    int array[n];\n"
    "    for (int i=0; i<n; i++) array[i]=i+3;\n"
    "    _Alignas(64) int aligned=37;\n"
    "    int local=n+7;\n"
    "#line 400 \"dbg_values.c\"\n"
    "    printf(\"VALUES-DYNAMIC %d %d %d %d %d\\n\", n, array[0], array[2], local, aligned);\n"
    "    return 0;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    debug_values(10,2.5,&initialized_global);\n"
    "    debug_dynamic(3);\n"
    "    return 0;\n"
    "}\n";

static const char* const check_debug_values_expected[] = {
    "VALUES-A 10 2.5 41 12 9 6 17 4 -3 23 7 41 29",
    "VALUES-G 41 13 0 31",
    "VALUES-INNER 99",
    "VALUES-OUTER 11",
    "VALUES-DYNAMIC 3 3 5 10 37",
};

static void check_debug_values_test_read(CheckDebugTestTally* tally, const char* program, Arena* arena)
{
    char report[PATH_MAX + 24];
    char errors[PATH_MAX + 24];
    snprintf(report, sizeof(report), "%s.values-verify", program);
    snprintf(errors, sizeof(errors), "%s.values-errors", program);
    char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", (char*)program, 0 };
    const bool verified = object_test_run(verify, report, errors) == 0;
    const String8 verification = foreign_test_read(arena, report);
    check_debug_test_expect(tally, verified && verification.pointer != NULL && strstr(verification.pointer, "No errors.") != NULL,
                            "the value/type DWARF passes llvm-dwarfdump --verify", report);
    snprintf(report, sizeof(report), "%s.values-output", program);
    char* run[] = { (char*)program, 0 };
    const bool ran = object_test_run(run, report, errors) == 0;
    const String8 output = foreign_test_read(arena, report);
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(check_debug_values_expected); e += 1)
    {
        check_debug_test_expect(tally, ran && output.pointer != NULL && strstr(output.pointer, check_debug_values_expected[e]) != NULL,
                                "the compiled fixture prints its expected values", report);
    }
    static const char* const commands[] = {
        "break dbg_values.c:100",
        "break dbg_values.c:200",
        "break dbg_values.c:300",
        "break dbg_values.c:400",
        "run",
        "printf \"GDB-VALUES-A %d %.1f %d %d %d %d %d %d %d %d %d %d %d\\n\", number, fraction, *pointer, local, frame, array[2], node.x, node.flag, node.delta, box.integer, mode, *alias, aligned",
        "printf \"GDB-VALUES-G %d %d %d %d\\n\", initialized_global, hidden_global, zero_global, static_local",
        "ptype Node",
        "ptype Count",
        "ptype enum Mode",
        "ptype union Box",
        "ptype debug_values",
        "info locals",
        "continue",
        "printf \"GDB-VALUES-INNER %d\\n\", shadow",
        "continue",
        "printf \"GDB-VALUES-OUTER %d\\n\", shadow",
        "continue",
        "printf \"GDB-VALUES-DYNAMIC %d %d %d %d %d\\n\", n, array[0], array[2], local, aligned",
        "continue",
    };
    snprintf(report, sizeof(report), "%s.values-gdb", program);
    const String8 transcript = check_debug_test_gdb(program, report, commands, BUSTER_ARRAY_LENGTH(commands), arena);
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(check_debug_values_expected); e += 1)
    {
        char expected[160];
        snprintf(expected, sizeof(expected), "GDB-%s", check_debug_values_expected[e]);
        check_debug_test_expect(tally, transcript.pointer != NULL && strstr(transcript.pointer, expected) != NULL,
                                "gdb reads the same values as the fixture, in the correct lexical scope", report);
    }
    static const char* const types[] = { "struct Node", "struct Node *next", "flag : 3", "delta : 5", "type = int", "cold = -2", "warm = 7", "union Box", "float real", "int (int, double, int *)" };
    for (uint32_t t = 0; t < BUSTER_ARRAY_LENGTH(types); t += 1)
    {
        check_debug_test_expect(tally, transcript.pointer != NULL && strstr(transcript.pointer, types[t]) != NULL,
                                "gdb ptype retains recursive records, bit fields, typedefs, enums, unions and signatures", types[t]);
    }
    static const char* const nested_enum[] = { "ptype enum Outer" };
    snprintf(report, sizeof(report), "%s.nested-enum-gdb", program);
    const String8 nested = check_debug_test_gdb(program, report, nested_enum, BUSTER_ARRAY_LENGTH(nested_enum), arena);
    check_debug_test_expect(tally, nested.pointer != NULL && strstr(nested.pointer, "outer_first") != NULL &&
                                  strstr(nested.pointer, "outer_last") != NULL && strstr(nested.pointer, "inner_only") == NULL,
                            "a nested enum's enumerators do not appear in its enclosing enum", report);
    static const char* const inner_enum[] = { "ptype enum Inner" };
    snprintf(report, sizeof(report), "%s.inner-enum-gdb", program);
    const String8 inner = check_debug_test_gdb(program, report, inner_enum, BUSTER_ARRAY_LENGTH(inner_enum), arena);
    check_debug_test_expect(tally, inner.pointer != NULL && strstr(inner.pointer, "inner_only") != NULL &&
                                  strstr(inner.pointer, "outer_first") == NULL && strstr(inner.pointer, "outer_last") == NULL,
                            "a nested enum retains only its own enumerators", report);
}

static void check_debug_values_test(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char source[PATH_MAX];
    char other[PATH_MAX];
    char outputs[3][PATH_MAX];
    char errors[PATH_MAX + 24];
    snprintf(source, sizeof(source), "%s/dbg_values.c", directory);
    snprintf(other, sizeof(other), "%s/dbg_values_other.c", directory);
    static const char other_text[] = "int other_global=47;\nstatic int other_hidden=53;\n";
    const bool written = foreign_test_write(source, check_debug_values_fixture, sizeof(check_debug_values_fixture) - 1) &&
                         foreign_test_write(other, other_text, sizeof(other_text) - 1);
    check_debug_test_expect(tally, written, "write the value/type fixtures", directory);
    String8 files[3] = { 0 };
    for (uint32_t v = 0; written && v < 3; v += 1)
    {
        snprintf(outputs[v], sizeof(outputs[v]), "%s/dbg_values_%u", directory, v);
        snprintf(errors, sizeof(errors), "%s.err", outputs[v]);
        char* arguments[] = { (char*)"/proc/self/exe", (char*)"-g", (char*)"--lanes", (char*)(v == 0 ? "1" : "8"), (char*)"-o", outputs[v], source, v == 2 ? (char*)"--scalar" : 0, 0 };
        const bool linked = object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the driver links the values with -g", errors);
        files[v] = linked ? foreign_test_read(arena, outputs[v]) : (String8){ 0 };
        if (v == 0 && linked)
        {
            check_debug_values_test_read(tally, outputs[v], arena);
        }
        if (v != 0)
        {
            check_debug_test_expect(tally, files[v].pointer != NULL && files[0].pointer != NULL && files[v].length == files[0].length && memcmp(files[v].pointer, files[0].pointer, files[0].length) == 0,
                                    "value metadata is byte-identical across lane counts and scalar/vector paths", outputs[v]);
        }
    }
    // Relocatable values must retain both CU-relative type references and section/global relocations.
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/dbg_values.o", directory);
    snprintf(errors, sizeof(errors), "%s.err", object);
    char* compile[] = { (char*)"/proc/self/exe", (char*)"-c", (char*)"-g", (char*)"-o", object, source, 0 };
    const bool compiled = written && object_test_run(compile, "/dev/null", errors) == 0;
    check_debug_test_expect(tally, compiled, "the driver writes a relocatable value/type unit", errors);
    const String8 object_bytes = compiled ? foreign_test_read(arena, object) : (String8){ 0 };
    check_debug_test_expect(tally, object_bytes.pointer != NULL && files[0].pointer != NULL && check_debug_test_same_unit(object_bytes, files[0]),
                            "all six value/line sections agree between -g and -c -g except relocation fields", object);
    for (uint32_t l = 0; compiled && l < 3; l += 1)
    {
        char program[PATH_MAX];
        snprintf(program, sizeof(program), "%s/dbg_values_link_%u", directory, l);
        snprintf(errors, sizeof(errors), "%s.err", program);
        char* arguments[] = { (char*)"cc", (char*)(l == 0 ? "-fuse-ld=bfd" : "-fuse-ld=lld"), (char*)"-o", program, object, 0 };
        const bool linked = l == 2 ? check_debug_test_link_foreign(object, program, errors, arena) : object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the value object links through bfd, lld and link-foreign", errors);
        if (linked)
        {
            check_debug_values_test_read(tally, program, arena);
        }
    }
    // A data-only unit must retain its definition addresses when all section contributions are concatenated.
    for (uint32_t v = 0; written && v < 2; v += 1)
    {
        char program[PATH_MAX];
        snprintf(program, sizeof(program), "%s/dbg_values_units_%u", directory, v);
        snprintf(errors, sizeof(errors), "%s.err", program);
        char* arguments[] = { (char*)"/proc/self/exe", (char*)"-g", (char*)"--lanes", (char*)(v == 0 ? "1" : "8"), (char*)"-o", program, source, other, 0 };
        const bool linked = object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the driver merges a data-only value unit", errors);
        if (linked)
        {
            check_debug_values_test_read(tally, program, arena);
            static const char* const commands[] = { "break main", "run", "printf \"OTHER-GLOBAL %d\\n\", other_global" };
            char report[PATH_MAX + 24];
            snprintf(report, sizeof(report), "%s.other-gdb", program);
            const String8 transcript = check_debug_test_gdb(program, report, commands, BUSTER_ARRAY_LENGTH(commands), arena);
            check_debug_test_expect(tally, transcript.pointer != NULL && strstr(transcript.pointer, "OTHER-GLOBAL 47") != NULL,
                                    "gdb reads a global from the merged data-only unit", report);
        }
    }
}
