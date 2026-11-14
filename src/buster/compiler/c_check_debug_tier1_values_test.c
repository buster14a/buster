// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Tier 1's retained object locations (#1114). The source passes objects' addresses across a translation-unit
// boundary, changes them through aliases, and keeps them alive across calls. GDB stops inside the callee and reads
// its caller, so a location must also be valid at the suspended caller's adjusted PC. Snapshots must keep their old
// values after alias mutation; an assignment cannot show a call's result until it returns. An inlined callee's local
// must never become one of the caller's declarations. The functions gdb stops in or reads as callers are noinline, and
// so are the other unit's, with which the relocatable variant compiles them: #2541 step 2 expands an external
// definition into its callers.

// The late publication boundary rejects bad source IDs and record ranges before publishing a count. Interleaved
// source locals become separate sorted lists; a record interval whose instructions encode to no bytes disappears.
static void check_debug_tier1_values_locations(CheckDebugTestTally* tally)
{
    const uint32_t offsets[] = { 0, 2, 4, 4, 7, 9, 10, 15, 16 };
    const Tier0Home home = { .mode = BUSTER_X86_MEMORY_BASE, .reg = BUSTER_ABI_RBP, .displacement = -16 };
    for (uint32_t change = 0; change < 12; change += 1)
    {
        EmittedLocalRange ranges[] = {
            { .home = home, .local = 2, .begin = 1, .end = 4 },
            { .home = home, .local = 0, .begin = 0, .end = 1 },
            { .home = home, .value = UINT64_C(0xfedcba9876543210), .local = 2, .begin = 4, .end = 6, .value_bytes = 8 },
            { .home = home, .local = 1, .begin = 2, .end = 3 },
        };
        uint32_t first[5] = { 0 };
        CheckDebugLocation locations[4] = { 0 };
        uint32_t scratch_bytes[16] = { 0 };
        Arena scratch = { .base = (uint8_t*)scratch_bytes, .capacity = sizeof(scratch_bytes) };
        CheckEmitBody body = { .debug_locations = locations, .debug_location_first = first };
        EmittedCode code = { .count = BUSTER_ARRAY_LENGTH(offsets) - 1, .local_ranges = ranges, .local_range_count = BUSTER_ARRAY_LENGTH(ranges) };
        switch (change)
        {
            case 0: break;
            case 1: ranges[0].local = 4; break;
            case 2: ranges[0].begin = ranges[0].end; break;
            case 3: ranges[0].end = code.count + 1; break;
            case 4: ranges[2].begin = 3; break;
            case 5: ranges[0].begin = 4; ranges[0].end = 6; ranges[2].begin = 1; ranges[2].end = 4; break;
            case 6: code.local_ranges = NULL; break;
            case 7: body.debug_locations = NULL; break;
            case 8: body.debug_location_first = NULL; break;
            case 9: scratch.capacity = 0; break;
            case 10: code.local_ranges = NULL; code.local_range_count = 0; break;
            case 11: ranges[0].value_bytes = 9; break;
        }
        const CheckProgramStatus status = check_program_emit_locations(&body, &code, 4, offsets, &scratch);
        const CheckProgramStatus expected = change == 0 || change == 10 ? CHECK_PROGRAM_OK : change >= 7 && change <= 9 ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
        check_debug_test_expect(tally, status == expected && (change == 0 || body.debug_location_count == 0),
                                "optimized locations reject invalid local IDs, bounds, overlaps, ordering and missing storage without publication", NULL);
        if (change == 0)
        {
            const uint32_t expected_first[] = { 0, 1, 1, 3, 3 };
            check_debug_test_expect(tally, body.debug_location_count == 3 && memcmp(first, expected_first, sizeof(first)) == 0 &&
                                          locations[0].begin == 0 && locations[0].end == 2 && locations[1].begin == 2 && locations[1].end == 7 &&
                                          locations[2].begin == 7 && locations[2].end == 10 && locations[2].home.reg == BUSTER_ABI_RBP &&
                                          locations[2].home.displacement == -16 && locations[2].value == UINT64_C(0xfedcba9876543210) && locations[2].value_bytes == 8,
                                    "optimized locations use final encoded boundaries and group by stable local, dropping empty byte ranges", NULL);
        }
    }
    // No locations are advertised in the entry padding, including when the first real location starts at byte 0.
    // Locations are symbol-relative (sled minus prefix), while encoded instructions move by the entire sled.
    for (uint32_t prefix = 0; prefix <= 8; prefix += 4)
    {
        const uint8_t original[] = { 0xb8, 1, 0, 0, 0, 0x90, 0x90, 0xc3 };
        uint8_t bytes[24] = { 0 };
        memcpy(bytes, original, sizeof(original));
        uint32_t row_offsets[3] = { 0, 6, 0 };
        uint32_t row_tokens[3] = { 14, 15, 0 };
        CheckDebugValueSpan spans[] = { { .begin = 0, .end = 4, .token = 14 }, { .begin = 6, .end = 8, .token = 15 } };
        CheckDebugLocation locations[] = { { .home = home, .begin = 0, .end = 4 }, { .home = home, .begin = 6, .end = 8 } };
        CheckEmitBody body = {
            .row_offsets = row_offsets,
            .row_tokens = row_tokens,
            .row_count = 2,
            .debug_spans = spans,
            .debug_span_count = BUSTER_ARRAY_LENGTH(spans),
            .debug_locations = locations,
            .debug_location_count = BUSTER_ARRAY_LENGTH(locations),
        };
        CheckProgramBody function = { .declaration = 42 };
        const CheckProgram program = { .bodies = &function };
        CheckProgramLowered lowered = { 0 };
        CheckEmitRun run = { .program = &program, .lowered = &lowered, .sources = true };
        const EmittedCode code = { 0 };
        check_program_emit_patchable(&run, &body, &code, 0, bytes, sizeof(original), 8, prefix, NULL);
        const uint32_t entry = 8 - prefix;
        check_debug_test_expect(tally, memcmp(bytes + 8, original, sizeof(original)) == 0 && body.sled == 8 && body.prefix == prefix &&
                                      body.body_offset == entry && locations[0].begin == entry && locations[0].end == 4 + entry &&
                                      locations[1].begin == 6 + entry && locations[1].end == 8 + entry &&
                                      row_offsets[0] == 0 && (entry == 0 || (body.row_count == 3 && row_offsets[1] == entry && row_tokens[0] == 42)),
                                "patchable entry padding shifts exact debug ranges from the symbol and never extends them over NOPs", NULL);
    }
}

// Defined after the Tier 1 debug helpers in c_check_debug_tier1_parameters_test.c.
static void check_debug_tier1_values_parameters(CheckDebugTestTally* tally);

static const char check_debug_tier1_values_source[] =
    "#line 1 \"dbg_tier1_values.c\"\n"
    "int printf(const char *, ...);\n"
    "int puts(const char *);\n"
    "struct DebugRecord { int first; signed char small; int *pointer; };\n"
    "void t1_keep(int *, volatile int *, int *, struct DebugRecord *, int *, int **);\n"
    "void t1_mutate(int *, volatile int *, int *, struct DebugRecord *, int *, int **);\n"
    "void t1_keep_scalar(volatile int *);\n"
    "void t1_keep_aligned(volatile int *);\n"
    "int t1_checkpoint(int);\n"
    "static inline __attribute__((always_inline)) int t1_inline(int value)\n"
    "{\n"
    "    int inline_only=value+2;\n"
    "    return inline_only;\n"
    "}\n"
    "__attribute__((noinline)) int t1_values(int number)\n"
    "{\n"
    "    volatile int frame=9;\n"
    "    int escaped=number+7;\n"
    "    struct DebugRecord record={23,-5,&escaped};\n"
    "    int array[3]={4,5,6};\n"
    "    int *pointer=&escaped;\n"
    "    volatile int shadow=11;\n"
    "    int promoted=escaped;\n"
    "    int mutable_value=7;\n"
    "    int inlined=t1_inline(number);\n"
    "    t1_keep(&number,&frame,&escaped,&record,array,&pointer);\n"
    "    t1_keep_scalar(&shadow);\n"
    "    mutable_value=t1_checkpoint(1);\n"
    "    printf(\"T1-BASE %d %d %d %d %d %d %d %d %d\\n\",number,frame,escaped,record.first,record.small,*record.pointer,array[2],*pointer,shadow);\n"
    "    t1_mutate(&number,&frame,&escaped,&record,array,&pointer);\n"
    "    t1_checkpoint(2);\n"
    "    printf(\"T1-MUTATED %d %d %d %d %d %d %d %d %d\\n\",number,frame,escaped,record.first,record.small,*record.pointer,array[2],*pointer,shadow);\n"
    "    {\n"
    "        volatile int shadow=99;\n"
    "        t1_keep_scalar(&shadow);\n"
    "        t1_checkpoint(3);\n"
    "        printf(\"T1-INNER %d\\n\",shadow);\n"
    "    }\n"
    "    t1_checkpoint(4);\n"
    "    printf(\"T1-OUTER %d\\n\",shadow);\n"
    "    int total=0;\n"
    "    for (int i=0;i<2;i++)\n"
    "    {\n"
    "        _Alignas(16) volatile int loop=70+i;\n"
    "        t1_keep_scalar(&loop);\n"
    "        t1_checkpoint(5);\n"
    "        printf(\"T1-LOOP %d\\n\",loop);\n"
    "        total+=loop;\n"
    "    }\n"
    "    return number!=15 || frame!=-12 || escaped!=37 || record.first!=51 || record.small!=-7 || *record.pointer!=37 || array[2]!=66 || *pointer!=37 || shadow!=11 || promoted!=17 || mutable_value!=101 || inlined!=12 || total!=141;\n"
    "}\n"
    "__attribute__((noinline)) int t1_aligned(void)\n"
    "{\n"
    "    _Alignas(64) volatile int aligned[16];\n"
    "    aligned[0]=29;\n"
    "    aligned[15]=31;\n"
    "    t1_keep_aligned(aligned);\n"
    "    t1_checkpoint(6);\n"
    "    printf(\"T1-ALIGNED %d %d\\n\",aligned[0],aligned[15]);\n"
    "    return aligned[0]+aligned[15];\n"
    "}\n"
    "__attribute__((noinline)) int t1_saved(long number)\n"
    "{\n"
    "    int result=t1_checkpoint(7);\n"
    "    return (int)number+result;\n"
    "}\n"
    "__attribute__((noinline)) int t1_leaf(int number)\n"
    "{\n"
    "    volatile int leaf=number+3;\n"
    "#line 700 \"dbg_tier1_values.c\"\n"
    "    return leaf;\n"
    "}\n"
    "int t1_global=41;\n"
    "__attribute__((noinline)) int t1_scalars(int number,long wide,float fraction,double precision,int *pointer)\n"
    "{\n"
    "    int literal=29;\n"
    "    signed char negative=-5;\n"
    "    unsigned long literal_bits=0xfffffffffffffff0UL;\n"
    "    volatile int barrier=number;\n"
    "#line 800 \"dbg_tier1_values.c\"\n"
    "    return number+barrier+(int)wide+(int)fraction+(int)precision+*pointer+literal+negative+(int)(literal_bits&15);\n"
    "}\n"
    "__attribute__((noinline)) int t1_straight(int input)\n"
    "{\n"
    "    int computed=input*3+2;\n"
    "#line 900 \"dbg_tier1_values.c\"\n"
    "    return computed^(computed>>3);\n"
    "}\n"
    "__attribute__((noinline)) int t1_branch(int choose,int input)\n"
    "{\n"
    "    int value=input+1;\n"
    "    if (choose)\n"
    "    {\n"
    "        value=input*3+2;\n"
    "        t1_checkpoint(8);\n"
    "        printf(\"T1-BRANCH-LEFT %d\\n\",value);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        value=input*5-3;\n"
    "        t1_checkpoint(9);\n"
    "        printf(\"T1-BRANCH-RIGHT %d\\n\",value);\n"
    "    }\n"
    "    t1_checkpoint(10);\n"
    "    printf(\"T1-BRANCH-JOIN %d\\n\",value);\n"
    "    return value+1;\n"
    "}\n"
    "__attribute__((noinline)) int t1_assignment_loop(int input,int limit)\n"
    "{\n"
    "    int value=input;\n"
    "    int total=0;\n"
    "    for (int turn=0;turn<limit;turn++)\n"
    "    {\n"
    "        value=value*2+turn;\n"
    "        t1_checkpoint(11);\n"
    "        printf(\"T1-ASSIGN-LOOP %d %d %d\\n\",turn,value,total);\n"
    "        total+=value;\n"
    "    }\n"
    "    t1_checkpoint(12);\n"
    "    printf(\"T1-ASSIGN-END %d %d\\n\",value,total);\n"
    "    return total^value;\n"
    "}\n"
    "struct DebugNamed { long a[5]; };\n"
    "__attribute__((noinline)) struct DebugNamed t1_named(int n)\n"
    "{\n"
    "    struct DebugNamed named;\n"
    "    for (int i=0;i<5;i++) named.a[i]=n+i;\n"
    "    t1_checkpoint(13);\n"
    "    printf(\"T1-NAMED %ld %ld\\n\",named.a[0],named.a[4]);\n"
    "    return named;\n"
    "}\n"
    "int main(void)\n"
    "{\n"
    "    int failed=t1_values(10);\n"
    "    failed|=t1_aligned()!=60;\n"
    "    failed|=t1_saved(19)!=126;\n"
    "    int leaf=t1_leaf(8);\n"
    "    printf(\"T1-LEAF %d\\n\",leaf);\n"
    "    failed|=leaf!=11;\n"
    "    failed|=t1_scalars(10,1000L,2.5f,3.75,&t1_global)!=1090;\n"
    "    printf(\"T1-PARAMS %d %ld %.2f %.2f %d\\n\",10,1000L,2.5,3.75,t1_global);\n"
    "    printf(\"T1-CONSTANTS %d %d %llu\\n\",29,-5,18446744073709551600ULL);\n"
    "    failed|=t1_straight(10)!=36;\n"
    "    printf(\"T1-COMPUTED %d\\n\",3*10+2);\n"
    "    failed|=t1_branch(1,10)!=33;\n"
    "    failed|=t1_branch(0,10)!=48;\n"
    "    failed|=t1_assignment_loop(3,3)!=51;\n"
    "    struct DebugNamed named=t1_named(5);\n"
    "    failed|=named.a[1]!=6;\n"
    "    if (!failed) puts(\"T1-RUN-OK\");\n"
    "    return failed;\n"
    "}\n";

static const char check_debug_tier1_values_support[] =
    "#line 1 \"dbg_tier1_support.c\"\n"
    "struct DebugRecord { int first; signed char small; int *pointer; };\n"
    "volatile int t1_sink;\n"
    "__attribute__((noinline)) void t1_keep(int *number,volatile int *frame,int *escaped,struct DebugRecord *record,int *array,int **pointer)\n"
    "{\n"
    "    t1_sink=*number+*frame+*escaped+record->first+record->small+array[2]+**pointer;\n"
    "}\n"
    "__attribute__((noinline)) void t1_mutate(int *number,volatile int *frame,int *escaped,struct DebugRecord *record,int *array,int **pointer)\n"
    "{\n"
    "    *number=15;\n"
    "    *frame=-12;\n"
    "    *escaped=37;\n"
    "    record->first=51;\n"
    "    record->small=-7;\n"
    "    array[2]=66;\n"
    "    *pointer=escaped;\n"
    "}\n"
    "__attribute__((noinline)) void t1_keep_scalar(volatile int *value)\n"
    "{\n"
    "    t1_sink=*value;\n"
    "}\n"
    "__attribute__((noinline)) void t1_keep_aligned(volatile int *array)\n"
    "{\n"
    "    t1_sink=array[0]+array[15];\n"
    "}\n"
    "__attribute__((noinline)) int t1_checkpoint(int phase)\n"
    "{\n"
    "    t1_sink+=phase;\n"
    "    return phase+100;\n"
    "}\n";

static const char* const check_debug_tier1_values_expected[] = {
    "T1-BASE 10 9 17 23 -5 17 6 17 11",
    "T1-MUTATED 15 -12 37 51 -7 37 66 37 11",
    "T1-INNER 99",
    "T1-OUTER 11",
    "T1-LOOP 70",
    "T1-LOOP 71",
    "T1-ALIGNED 29 31",
    "T1-LEAF 11",
    "T1-PARAMS 10 1000 2.50 3.75 41",
    "T1-CONSTANTS 29 -5 18446744073709551600",
    "T1-COMPUTED 32",
    "T1-BRANCH-LEFT 32",
    "T1-BRANCH-JOIN 32",
    "T1-BRANCH-RIGHT 47",
    "T1-BRANCH-JOIN 47",
    "T1-ASSIGN-LOOP 0 6 0",
    "T1-ASSIGN-LOOP 1 13 6",
    "T1-ASSIGN-LOOP 2 28 19",
    "T1-ASSIGN-END 28 47",
    "T1-NAMED 5 9",
};

// Unlike the short-command helper, a checked command file neither drops commands at an argv limit nor accepts a
// successful inferior run as proof that GDB reached all value assertions. GDB's Python exception boundary lets a
// failed read report its own error without hiding subsequent checkpoints. Errors, missing exact values and missing
// DONE are all failures; catching the error only preserves the rest of the diagnostic transcript.
// Scheduling may put the scalar return's first source row at function entry. Resolve the actual line PCs before
// advancing from the parameter stop, so coincident entry/constant breakpoints still perform both exact reads.
static const char check_debug_tier1_values_script[] =
    "set pagination off\n"
    "set confirm off\n"
    "set width 0\n"
    "set debuginfod enabled off\n"
    "python\n"
    "import gdb\n"
    "class T1Read(gdb.Command):\n"
    "    def __init__(self):\n"
    "        super().__init__('t1-read', gdb.COMMAND_DATA)\n"
    "    def invoke(self, command, from_tty):\n"
    "        try:\n"
    "            gdb.execute(command, from_tty=from_tty)\n"
    "        except gdb.error as error:\n"
    "            gdb.write('\\nGDB-T1-READ-ERROR: ' + str(error) + '\\n')\n"
    "T1Read()\n"
    "end\n"
    "break *t1_checkpoint\n"
    "break dbg_tier1_values.c:700\n"
    "break *t1_scalars\n"
    "break dbg_tier1_values.c:800\n"
    "break dbg_tier1_values.c:900\n"
    "run\n"
    "up\n"
    "t1-read printf \"GDB-T1-BASE %d %d %d %d %d %d %d %d %d\\n\",number,frame,escaped,record.first,record.small,*record.pointer,array[2],*pointer,shadow\n"
    "printf \"GDB-T1-LOCALS-BEGIN\\n\"\n"
    "t1-read info locals\n"
    "printf \"GDB-T1-LOCALS-END\\n\"\n"
    "t1-read ptype promoted\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-MUTATED %d %d %d %d %d %d %d %d %d\\n\",number,frame,escaped,record.first,record.small,*record.pointer,array[2],*pointer,shadow\n"
    "printf \"GDB-T1-AFTER-BEGIN\\n\"\n"
    "t1-read info locals\n"
    "printf \"GDB-T1-AFTER-END\\n\"\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-INNER %d\\n\",shadow\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-OUTER %d\\n\",shadow\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-LOOP %d\\n\",loop\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-LOOP %d\\n\",loop\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-ALIGNED %d %d\\n\",aligned[0],aligned[15]\n"
    "down\n"
    "continue\n"
    "up\n"
    "printf \"GDB-T1-CALL-ARGS-BEGIN\\n\"\n"
    "t1-read info args\n"
    "printf \"GDB-T1-CALL-ARGS-END\\n\"\n"
    "down\n"
    "continue\n"
    "t1-read printf \"GDB-T1-LEAF %d\\n\",leaf\n"
    "continue\n"
    "t1-read printf \"GDB-T1-PARAMS %d %ld %.2f %.2f %d\\n\",number,wide,fraction,precision,*pointer\n"
    "python\n"
    "t1_literal_sites = gdb.decode_line('dbg_tier1_values.c:800')[1]\n"
    "if not t1_literal_sites:\n"
    "    raise gdb.GdbError('T1 constant checkpoint has no code location')\n"
    "if gdb.selected_frame().pc() not in [site.pc for site in t1_literal_sites]:\n"
    "    gdb.execute('continue')\n"
    "end\n"
    "t1-read printf \"GDB-T1-CONSTANTS %d %d %llu\\n\",literal,negative,(unsigned long long)literal_bits\n"
    "continue\n"
    "t1-read printf \"GDB-T1-COMPUTED %d\\n\",computed\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-BRANCH-LEFT %d\\n\",value\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-BRANCH-JOIN %d\\n\",value\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-BRANCH-RIGHT %d\\n\",value\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-BRANCH-JOIN %d\\n\",value\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-ASSIGN-LOOP %d %d %d\\n\",turn,value,total\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-ASSIGN-LOOP %d %d %d\\n\",turn,value,total\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-ASSIGN-LOOP %d %d %d\\n\",turn,value,total\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-ASSIGN-END %d %d\\n\",value,total\n"
    "down\n"
    "continue\n"
    "up\n"
    "t1-read printf \"GDB-T1-NAMED %ld %ld\\n\",named.a[0],named.a[4]\n"
    "down\n"
    "continue\n"
    "printf \"GDB-T1-DONE\\n\"\n";

typedef struct CheckDebugTier1ValuesVariant CheckDebugTier1ValuesVariant;
struct CheckDebugTier1ValuesVariant
{
    const char* name;
    const char* schedule;
    bool scalar;
    bool frame_pointer;
    bool no_rematerialize;
};

static bool check_debug_tier1_values_compile(CheckDebugTestTally* tally, const char* source, const char* support, const char* output,
                                             const CheckDebugTier1ValuesVariant* variant, const char* lanes, bool debug, bool object, Arena* arena)
{
    char errors[PATH_MAX + 24];
    snprintf(errors, sizeof(errors), "%s.err", output);
    char* arguments[32] = {
        (char*)"/proc/self/exe",
        (char*)"--tier1",
        (char*)"--tier1-strict",
        (char*)"--verify",
        (char*)"--tier1-report",
        (char*)"-march=x86-64",
        (char*)"--lanes",
        (char*)lanes,
    };
    uint32_t count = 8;
    arguments[count] = debug ? (char*)"-g" : NULL;
    count += debug;
    arguments[count] = object ? (char*)"-c" : NULL;
    count += object;
    arguments[count] = variant->scalar ? (char*)"--scalar" : NULL;
    count += variant->scalar;
    arguments[count] = variant->frame_pointer ? (char*)"-fno-omit-frame-pointer" : NULL;
    count += variant->frame_pointer;
    arguments[count] = variant->no_rematerialize ? (char*)"--tier1-no-rematerialize" : NULL;
    count += variant->no_rematerialize;
    arguments[count] = (char*)variant->schedule;
    count += variant->schedule != NULL;
    arguments[count++] = (char*)"-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = (char*)support;
    count += support != NULL;
    arguments[count] = NULL;
    const bool built = object_test_run(arguments, "/dev/null", errors) == 0;
    check_debug_test_expect(tally, built, "the optimized value fixture compiles strictly through Tier 1", errors);
    const String8 report = built ? foreign_test_read(arena, errors) : (String8){ 0 };
    bool inlined = false;
    const char* cursor = report.pointer;
    while (cursor != NULL && (cursor = strstr(cursor, "\tinlined=")) != NULL)
    {
        cursor += strlen("\tinlined=");
        inlined = strtoull(cursor, NULL, 10) != 0 || inlined;
    }
    check_debug_test_expect(tally, built && inlined, "the fixture actually expands its inline callee before debug publication", errors);
    return built && inlined;
}

// Debug sections are not allocated. The build ID hashes the whole file, so that one allocated note is expected to
// differ; all runtime sections, including dynamic relocation records and their referenced tables, must be identical.
static bool check_debug_tier1_values_same_code(String8 debug, String8 plain)
{
    const ExecutableTestImage images[] = { executable_test_image_of(debug, ET_DYN), executable_test_image_of(plain, ET_DYN) };
    const String8 files[] = { debug, plain };
    bool same = images[0].valid && images[1].valid && images[0].header->e_entry == images[1].header->e_entry;
    uint32_t counts[2] = { 0 };
    bool text = false;
    for (uint32_t side = 0; same && side < 2; side += 1)
    {
        const ExecutableTestImage* image = images + side;
        for (uint32_t s = 1; same && s < image->header->e_shnum; s += 1)
        {
            const Elf64_Shdr* section = image->section_headers + s;
            const char* name = image->names + section->sh_name;
            if ((section->sh_flags & SHF_ALLOC) != 0 && strcmp(name, ".note.gnu.build-id") != 0)
            {
                const Elf64_Shdr* other = executable_test_section(images + (side ^ 1), name);
                counts[side] += 1;
                same = other != NULL && section->sh_type == other->sh_type && section->sh_flags == other->sh_flags && section->sh_addr == other->sh_addr &&
                       section->sh_size == other->sh_size && section->sh_addralign == other->sh_addralign && section->sh_entsize == other->sh_entsize;
                if (same && section->sh_type != SHT_NOBITS)
                {
                    same = section->sh_offset <= files[side].length && section->sh_size <= files[side].length - section->sh_offset &&
                           other->sh_offset <= files[side ^ 1].length && other->sh_size <= files[side ^ 1].length - other->sh_offset &&
                           memcmp(files[side].pointer + section->sh_offset, files[side ^ 1].pointer + other->sh_offset, section->sh_size) == 0;
                }
                text = text || (same && strcmp(name, ".text") == 0 && section->sh_size != 0);
            }
        }
    }
    return same && text && counts[0] == counts[1];
}

static void check_debug_tier1_values_read(CheckDebugTestTally* tally, const char* program, const char* script, const char* directory, Arena* arena)
{
    char report[PATH_MAX + 24];
    char errors[PATH_MAX + 24];
    snprintf(report, sizeof(report), "%s.t1-verify", program);
    snprintf(errors, sizeof(errors), "%s.t1-errors", program);
    char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", (char*)program, NULL };
    const bool verified = object_test_run(verify, report, errors) == 0;
    const String8 verification = verified ? foreign_test_read(arena, report) : (String8){ 0 };
    check_debug_test_expect(tally, verified && verification.pointer != NULL && strstr(verification.pointer, "No errors.") != NULL,
                            "the optimized values' DWARF passes llvm-dwarfdump --verify", report);
    snprintf(report, sizeof(report), "%s.t1-run", program);
    char* run[] = { (char*)program, NULL };
    const bool ran = object_test_run(run, report, errors) == 0;
    const String8 output = ran ? foreign_test_read(arena, report) : (String8){ 0 };
    check_debug_test_expect(tally, output.pointer != NULL && strstr(output.pointer, "T1-RUN-OK\n") != NULL,
                            "the optimized fixture checks its mutations, snapshots, loop and aligned/red-zone results", report);
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(check_debug_tier1_values_expected); e += 1)
    {
        check_debug_test_expect(tally, output.pointer != NULL && strstr(output.pointer, check_debug_tier1_values_expected[e]) != NULL,
                                "the optimized fixture prints independently specified values", check_debug_tier1_values_expected[e]);
    }
    snprintf(report, sizeof(report), "%s.t1-gdb", program);
    char* gdb[] = { (char*)"gdb", (char*)"-nx", (char*)"-batch", (char*)"-directory", (char*)directory, (char*)"-x", (char*)script, (char*)program, NULL };
    const bool inspected = object_test_run(gdb, report, errors) == 0;
    const String8 transcript = foreign_test_read(arena, report);
    check_debug_test_expect(tally, inspected && transcript.pointer != NULL && strstr(transcript.pointer, "GDB-T1-DONE\n") != NULL &&
                                  strstr(transcript.pointer, "GDB-T1-READ-ERROR:") == NULL,
                            "GDB reaches every optimized value checkpoint without a command error", report);
    for (uint32_t e = 0; e < BUSTER_ARRAY_LENGTH(check_debug_tier1_values_expected); e += 1)
    {
        char expected[192];
        snprintf(expected, sizeof(expected), "GDB-%s\n", check_debug_tier1_values_expected[e]);
        check_debug_test_expect(tally, transcript.pointer != NULL && strstr(transcript.pointer, expected) != NULL,
                                "GDB reads the actual retained objects in the suspended caller and its lexical scope", expected);
    }
    const char* begin = transcript.pointer != NULL ? strstr(transcript.pointer, "GDB-T1-LOCALS-BEGIN\n") : NULL;
    const char* end = begin != NULL ? strstr(begin, "GDB-T1-LOCALS-END\n") : NULL;
    const char* promoted = begin != NULL ? strstr(begin, "promoted = 17\n") : NULL;
    const char* mutable_value = begin != NULL ? strstr(begin, "mutable_value = ") : NULL;
    const char* inline_only = begin != NULL ? strstr(begin, "inline_only") : NULL;
    check_debug_test_expect(tally, end != NULL && promoted != NULL && promoted < end && strstr(end, "type = int") != NULL,
                            "a promoted snapshot retains its name, type and actual pre-mutation value", report);
    check_debug_test_expect(tally, end != NULL && mutable_value != NULL && mutable_value < end &&
                                  (strncmp(mutable_value, "mutable_value = 7\n", strlen("mutable_value = 7\n")) == 0 ||
                                   strncmp(mutable_value, "mutable_value = <optimized out>\n", strlen("mutable_value = <optimized out>\n")) == 0),
                            "a suspended assignment exposes only its previous value or a gap, never its future call result", report);
    check_debug_test_expect(tally, end != NULL && (inline_only == NULL || inline_only >= end),
                            "an inlined callee's local is not reported as a caller local", report);
    const char* after_begin = transcript.pointer != NULL ? strstr(transcript.pointer, "GDB-T1-AFTER-BEGIN\n") : NULL;
    const char* after_end = after_begin != NULL ? strstr(after_begin, "GDB-T1-AFTER-END\n") : NULL;
    const char* after_promoted = after_begin != NULL ? strstr(after_begin, "promoted = 17\n") : NULL;
    const char* after_value = after_begin != NULL ? strstr(after_begin, "mutable_value = 101\n") : NULL;
    check_debug_test_expect(tally, after_end != NULL && after_promoted != NULL && after_promoted < after_end && after_value != NULL && after_value < after_end,
                            "after the call and alias mutation, the saved snapshot is still 17 and the completed assignment is 101", report);
    const char* call_begin = transcript.pointer != NULL ? strstr(transcript.pointer, "GDB-T1-CALL-ARGS-BEGIN\n") : NULL;
    const char* call_end = call_begin != NULL ? strstr(call_begin, "GDB-T1-CALL-ARGS-END\n") : NULL;
    const char* saved = call_begin != NULL ? strstr(call_begin, "number = 19\n") : NULL;
    const char* unavailable = call_begin != NULL ? strstr(call_begin, "number = <optimized out>\n") : NULL;
    check_debug_test_expect(tally, call_end != NULL && ((saved != NULL && saved < call_end) || (unavailable != NULL && unavailable < call_end)),
                            "a suspended caller's immutable parameter is recovered correctly from a preserved copy or explicitly unavailable", report);
}

static void check_debug_tier1_values_test(CheckDebugTestTally* tally, const char* directory, Arena* arena)
{
    char source[PATH_MAX];
    char support[PATH_MAX];
    char script[PATH_MAX];
    snprintf(source, sizeof(source), "%s/dbg_tier1_values.c", directory);
    snprintf(support, sizeof(support), "%s/dbg_tier1_support.c", directory);
    snprintf(script, sizeof(script), "%s/dbg_tier1_values.gdb", directory);
    const bool written = foreign_test_write(source, check_debug_tier1_values_source, sizeof(check_debug_tier1_values_source) - 1) &&
                         foreign_test_write(support, check_debug_tier1_values_support, sizeof(check_debug_tier1_values_support) - 1) &&
                         foreign_test_write(script, check_debug_tier1_values_script, sizeof(check_debug_tier1_values_script) - 1);
    check_debug_test_expect(tally, written, "write the Tier 1 value fixtures and checked debugger script", directory);
    static const CheckDebugTier1ValuesVariant variants[] = {
        { .name = "default" },
        { .name = "scalar", .scalar = true },
        { .name = "pre", .schedule = "--tier1-schedule=pre" },
        { .name = "post", .schedule = "--tier1-schedule=post" },
        { .name = "frame-pointer", .frame_pointer = true },
        { .name = "no-rematerialize", .no_rematerialize = true },
    };
    String8 baseline = { 0 };
    for (uint32_t v = 0; written && v < BUSTER_ARRAY_LENGTH(variants); v += 1)
    {
        char program[PATH_MAX];
        char plain[PATH_MAX];
        snprintf(program, sizeof(program), "%s/dbg_t1_values_%s", directory, variants[v].name);
        snprintf(plain, sizeof(plain), "%s/dbg_t1_values_%s_plain", directory, variants[v].name);
        const bool built = check_debug_tier1_values_compile(tally, source, support, program, variants + v, "1", true, false, arena);
        const bool unadorned = check_debug_tier1_values_compile(tally, source, support, plain, variants + v, "1", false, false, arena);
        const String8 debug = built ? foreign_test_read(arena, program) : (String8){ 0 };
        const String8 other = unadorned ? foreign_test_read(arena, plain) : (String8){ 0 };
        check_debug_test_expect(tally, built && unadorned && check_debug_tier1_values_same_code(debug, other),
                                "-g preserves executable code, data, unwind and runtime relocations for each Tier 1 option", program);
        if (built)
        {
            check_debug_tier1_values_read(tally, program, script, directory, arena);
        }
        baseline = v == 0 ? debug : baseline;
    }
    if (written)
    {
        char program[PATH_MAX];
        snprintf(program, sizeof(program), "%s/dbg_t1_values_lanes8", directory);
        const bool built = check_debug_tier1_values_compile(tally, source, support, program, variants, "8", true, false, arena);
        const String8 other = built ? foreign_test_read(arena, program) : (String8){ 0 };
        check_debug_test_expect(tally, baseline.pointer != NULL && other.pointer != NULL && baseline.length == other.length &&
                                      memcmp(baseline.pointer, other.pointer, baseline.length) == 0,
                                "Tier 1 debug metadata and code are byte-identical across lane counts", program);
    }
    // Use one relocatable object for the three existing linker paths. Its helper definitions are deliberately not
    // always_inline, so its checkpoint calls remain just as in the multi-unit native build above.
    char combined[PATH_MAX];
    char object[PATH_MAX];
    snprintf(combined, sizeof(combined), "%s/dbg_tier1_values_object.c", directory);
    snprintf(object, sizeof(object), "%s/dbg_tier1_values.o", directory);
    const char* definitions = strstr(check_debug_tier1_values_support, "volatile int t1_sink;");
    const uint64_t first_bytes = sizeof(check_debug_tier1_values_source) - 1;
    const uint64_t other_bytes = definitions != NULL ? strlen(definitions) : 0;
    char* combined_text = BUSTER_ARENA_PUSH_ARRAY(arena, char, first_bytes + other_bytes + 1);
    bool combined_written = written && combined_text != NULL && definitions != NULL;
    if (combined_written)
    {
        memcpy(combined_text, check_debug_tier1_values_source, first_bytes);
        memcpy(combined_text + first_bytes, definitions, other_bytes + 1);
        combined_written = foreign_test_write(combined, combined_text, first_bytes + other_bytes);
    }
    check_debug_test_expect(tally, combined_written, "write the optimized value fixture for relocatable output", combined);
    const bool compiled = combined_written && check_debug_tier1_values_compile(tally, combined, NULL, object, variants, "1", true, true, arena);
    char* verify[] = { (char*)"llvm-dwarfdump", (char*)"--verify", object, NULL };
    check_debug_test_expect(tally, compiled && object_test_run(verify, "/dev/null", "/dev/null") == 0,
                            "the optimized value relocatable object passes DWARF verification", object);
    for (uint32_t linker = 0; compiled && linker < 3; linker += 1)
    {
        char program[PATH_MAX];
        char errors[PATH_MAX + 24];
        snprintf(program, sizeof(program), "%s/dbg_t1_values_link_%u", directory, linker);
        snprintf(errors, sizeof(errors), "%s.err", program);
        char* arguments[] = { (char*)"cc", (char*)(linker == 0 ? "-fuse-ld=bfd" : "-fuse-ld=lld"), (char*)"-o", program, object, NULL };
        const bool linked = linker == 2 ? check_debug_test_link_foreign(object, program, errors, arena) : object_test_run(arguments, "/dev/null", errors) == 0;
        check_debug_test_expect(tally, linked, "the optimized value object links through bfd, lld and link-foreign", errors);
        if (linked)
        {
            check_debug_tier1_values_read(tally, program, script, directory, arena);
        }
    }
}
