// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Actual Tier0 records -> branch sizing -> encoder -> native CFI -> native executable -> GDB/perf. GNU as supplies
// only the process-entry wrapper and the relocatable container for the already encoded functions/CFI. The two normal
// functions' CFI is entirely unwind.c's. Fixture source rows are explicit test input, not a codegen source-map API.
// Twice: with a leaf of a fixed frame, its CFI as pairs, and with one that allocates on the stack (a frame pointer's,
// #395), its CFI with one shared CIE; GDB stops at every instruction of the leaf outside its loop.

typedef struct UnwindNativeFunction UnwindNativeFunction;
struct UnwindNativeFunction
{
    Tier0Code code;
    uint32_t* offsets;
    uint32_t* ends;
    uint8_t* bytes;
    uint32_t size;
    UnwindFunction unwind;
};

static bool unwind_native_compile(const Tier0Function* function, Arena* arena, UnwindNativeFunction* native)
{
    const Tier0Check check = tier0_validate(function);
    bool valid = check.error == TIER0_OK;
    if (!valid)
    {
        printf("unwind native stream: entry %u: %s\n", check.node, check.rule != 0 ? check.rule : "invalid");
    }
    native->code = valid ? tier0_compile(function, arena) : (Tier0Code){ 0 };
    valid &= native->code.error == TIER0_OK && native->code.count != 0;
    native->offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, native->code.count);
    native->ends = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(native->code.reference_count));
    const uint64_t capacity = BUSTER_X86_ENCODER_CAPACITY(native->code.count);
    native->bytes = arena_push(arena, capacity, 16);
    valid &= native->offsets != 0 && native->ends != 0 && native->bytes != 0;
    if (valid)
    {
        const BranchSizing sizing = size_branches(native->code.records, native->code.count, native->code.branches, native->code.targets,
                                                  native->code.branch_count, native->offsets);
        valid &= sizing.error == BRANCH_SIZING_OK && sizing.length <= UINT32_MAX;
        const int64_t encoded = valid ? x86_encode(native->code.records, native->code.count, native->bytes, capacity, 0, native->ends) : -1;
        valid &= encoded >= 0 && (uint64_t)encoded == sizing.length;
        native->size = valid ? (uint32_t)encoded : 0;
        // The frame Tier 0 published (unwind_tier0_frame, the C driver's reader); without a frame pointer, the
        // analyzer's descriptor too, which must be the same, and with one, which the analyzer doesn't take, its refusal.
        valid &= unwind_tier0_frame(&native->code, native->offsets, native->size, &native->unwind) == UNWIND_OK;
        UnwindFunction analyzed[2] = { 0 };
        const UnwindError vector = unwind_tier0(&native->code, native->offsets, native->size, &analyzed[0]);
        const UnwindError scalar = unwind_tier0_scalar(&native->code, native->offsets, native->size, &analyzed[1]);
        valid &= native->code.frame_pointer ? vector == UNWIND_ERROR_UNSUPPORTED && scalar == UNWIND_ERROR_UNSUPPORTED :
                                              vector == UNWIND_OK && scalar == UNWIND_OK && memcmp(&analyzed[0], &native->unwind, sizeof(analyzed[0])) == 0 &&
                                                  memcmp(&analyzed[1], &native->unwind, sizeof(analyzed[1])) == 0;
    }
    return valid;
}

static void unwind_native_entry(uint8_t* kinds, uint8_t* classes, uint64_t* values, uint32_t* count, uint8_t kind, uint64_t value)
{
    kinds[*count] = kind;
    classes[*count] = BUSTER_TYPED_CLASS_U64;
    values[*count] = value;
    *count += 1;
}

// The outer function, which calls the leaf, and the leaf: fifteen locals, a loop of 20,000,000 and their sum, and with
// `pointer` a variable-length array's storage first (ALLOCA), which gives it a frame pointer and moves rsp in its body,
// and a store to it: perf's samples copy the stack from rsp, and the kernel copies nothing of a page never touched.
static bool unwind_native_functions(Arena* arena, UnwindNativeFunction native[2], bool pointer)
{
    const AbiReturn result = {
        .kind = BUSTER_ABI_RESULT_REGISTERS,
        .part_bytes = 8,
        .registers = { BUSTER_ABI_RAX, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE, BUSTER_ABI_REGISTER_NONE },
    };
    const uint8_t outer_kinds[] = { BUSTER_TYPED_CALL, BUSTER_TYPED_RETURN };
    const uint8_t outer_classes[] = { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_U64 };
    const uint64_t outer_values[] = { 0, 0 };
    const SymbolId target = 1;
    const uint32_t zero = 0;
    const uint8_t nonvariadic = 0;
    const AbiCall call = {
        .stack_alignment_log2 = 4,
        .result = result,
    };
    const Tier0Function outer = {
        .kinds = outer_kinds,
        .classes = outer_classes,
        .values = outer_values,
        .count = BUSTER_ARRAY_LENGTH(outer_kinds),
        .result = result,
        .call_targets = &target,
        .call_first = &zero,
        .call_argument_counts = &zero,
        .call_variadic = &nonvariadic,
        .calls = &call,
        .call_count = 1,
    };
    uint8_t kinds[128];
    uint8_t classes[128];
    uint64_t values[128];
    uint8_t local_classes[15];
    uint8_t local_flags[15] = { BUSTER_TYPED_LOCAL_ADDRESS_TAKEN };
    memset(local_classes, BUSTER_TYPED_CLASS_U64, sizeof(local_classes));
    uint32_t count = 0;
    if (pointer)
    {
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_CONST, 4000);
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_ALLOCA, 0);
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_CONST, 1);
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_SET, 0);
    }
    for (uint32_t local = 0; local < 15; local += 1)
    {
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_CONST, local == 0 ? 20000000 : local);
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_SET_LOCAL, local);
    }
    const uint32_t loop = count;
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, 0);
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_CONST, 1);
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_SUB, 0);
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_SET_LOCAL, 0);
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, 0);
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_BRANCH_TRUE, loop);
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, 1);
    for (uint32_t local = 2; local < 15; local += 1)
    {
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_LOCAL, local);
        unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_ADD, 0);
    }
    unwind_native_entry(kinds, classes, values, &count, BUSTER_TYPED_RETURN, 0);
    const Tier0Function leaf = {
        .kinds = kinds,
        .classes = classes,
        .values = values,
        .count = count,
        .local_classes = local_classes,
        .local_flags = local_flags,
        .local_count = 15,
        .result = result,
    };
    bool valid = unwind_native_compile(&outer, arena, &native[0]) && unwind_native_compile(&leaf, arena, &native[1]);
    valid &= native[0].code.reference_count == 1 && native[1].code.reference_count == 0;
    // Every callee-saved register: rbp the frame pointer, or a value's with a fixed frame.
    valid &= pointer ? native[1].unwind.pointer == 1 && native[1].unwind.saved == 61 : native[1].unwind.pointer == 0 && native[1].unwind.saved == 63 && native[1].unwind.frame_size != 0;
    printf("unwind native producer%s: outer %u bytes / mask %u, leaf %u bytes / mask %u / frame %u / frame pointer %u: %s\n", pointer ? " (frame pointer)" : "",
           native[0].size, native[0].unwind.saved, native[1].size, native[1].unwind.saved, native[1].unwind.frame_size, native[1].unwind.pointer, valid ? "pass" : "FAIL");
    return valid;
}

// The leaf's stops for GDB, in the order it runs them: every record's start but those of its loop, which runs
// 20,000,000 times (from the first backward branch's target to the last backward branch): the entry and every
// transition's end among them.
static uint32_t unwind_native_stops(const UnwindNativeFunction* native, uint32_t* stops)
{
    uint32_t first = UINT32_MAX;
    uint32_t last = 0;
    for (uint32_t branch = 0; branch < native->code.branch_count; branch += 1)
    {
        if (native->code.targets[branch] < native->code.branches[branch])
        {
            first = native->code.targets[branch] < first ? native->code.targets[branch] : first;
            last = native->code.branches[branch] > last ? native->code.branches[branch] : last;
        }
    }
    uint32_t count = 0;
    for (uint32_t record = 0; record < native->code.count; record += 1)
    {
        if (record < first || record > last)
        {
            stops[count] = native->offsets[record];
            count += 1;
        }
    }
    return count;
}

static bool unwind_native_gdb_script(const char* path, const uint32_t* stops, uint32_t count)
{
    FILE* file = fopen(path, "w");
    bool valid = file != 0;
    if (file != 0)
    {
        fputs("set pagination off\nset confirm off\nset debuginfod enabled off\n", file);
        for (uint32_t stop = 0; stop < count; stop += 1)
        {
            fprintf(file, "break *(debug_leaf+%u)\ncommands\nsilent\nprintf \"UNWIND_STOP %u\\n\"\nbt\nframe 1\n", stops[stop], stop);
            fputs("if $rbx != 101 || $rbp != 102 || $r12 != 103 || $r13 != 104 || $r14 != 105 || $r15 != 106\n"
                  "printf \"UNWIND_BAD_REG\\n\"\nend\nframe 0\ncontinue\nend\n", file);
        }
        fputs("run\n", file);
        valid &= fclose(file) == 0;
    }
    return valid;
}

static bool unwind_native_backtraces(const char* text, uint32_t stops)
{
    bool valid = text != 0 && strstr(text, "UNWIND_BAD_REG") == 0;
    const char* cursor = text;
    uint32_t found = 0;
    while (valid && cursor != 0 && (cursor = strstr(cursor, "UNWIND_STOP ")) != 0)
    {
        const char* next = strstr(cursor + 1, "UNWIND_STOP ");
        const char* number = cursor + sizeof("UNWIND_STOP ") - 1;
        char* end = 0;
        const unsigned long id = strtoul(number, &end, 10);
        valid &= id == found && end != number && end != 0 && *end == '\n';
        const char* frames[] = { "#0 ", "#1 ", "#2 " };
        const char* names[] = { "debug_leaf", "debug_outer", "_start" };
        const char* lines[] = { "leaf.c:5", "start.c:11", "start.c:1" };
        for (uint32_t frame = 0; valid && frame < 3; frame += 1)
        {
            const char* start = strstr(cursor, frames[frame]);
            const char* finish = start != 0 ? strchr(start, '\n') : 0;
            valid &= start != 0 && finish != 0 && (next == 0 || finish < next) && debug_test_frame(start, frames[frame], names[frame], lines[frame]);
        }
        found += 1;
        cursor += 1;
    }
    return valid && found == stops;
}

static bool unwind_native_oracle_tests(void)
{
    const char complete[] = "UNWIND_STOP 0\n#0 debug_leaf () at leaf.c:5\n#1 debug_outer () at start.c:11\n#2 _start () at start.c:1\n";
    const char split[] = "UNWIND_STOP 0\n#2 _start () at start.c:1\nUNWIND_STOP 1\n#0 debug_leaf () at leaf.c:5\n"
                         "#1 debug_outer () at start.c:11\n#2 _start () at start.c:1\n";
    const char repeated[] = "UNWIND_STOP 0\n#0 debug_leaf () at leaf.c:5\n#1 debug_outer () at start.c:11\n#2 _start () at start.c:1\n"
                            "UNWIND_STOP 0\n#0 debug_leaf () at leaf.c:5\n#1 debug_outer () at start.c:11\n#2 _start () at start.c:1\n";
    const bool valid = unwind_native_backtraces(complete, 1) && !unwind_native_backtraces(complete, 2) &&
                       !unwind_native_backtraces(split, 2) && !unwind_native_backtraces(repeated, 2);
    printf("unwind GDB oracle: bounded frames and unique ordered transition stops: %s\n", valid ? "pass" : "FAIL");
    return valid;
}

static bool unwind_test_consumer(bool keep, bool pointer)
{
    char directory[] = "/tmp/buster-unwind-XXXXXX";
    Arena arena = arena_reserve(64ull << 20);
    bool valid = arena.base != 0 && mkdtemp(directory) != 0;
    UnwindNativeFunction native[2] = { 0 };
    valid = valid && unwind_native_functions(&arena, native, pointer);
    UnwindFunction descriptions[2] = { native[0].unwind, native[1].unwind };
    uint8_t frame_bytes[384];
    uint32_t locations[2];
    UnwindOutput frames = {
        .bytes = frame_bytes,
        .capacity = sizeof(frame_bytes),
        .locations = locations,
        .location_capacity = 2,
        .shared = pointer,
    };
    valid = valid && unwind_write(descriptions, 2, &frames, false) == UNWIND_OK && unwind_test_decode(descriptions, 2, &frames);
    char assembly[512];
    char object[512];
    char executable[512];
    char out[512];
    char err[512];
    char data[512];
    char commands[512];
    snprintf(assembly, sizeof(assembly), "%s/fixture.s", directory);
    snprintf(object, sizeof(object), "%s/fixture.o", directory);
    snprintf(executable, sizeof(executable), "%s/fixture", directory);
    snprintf(out, sizeof(out), "%s/stdout", directory);
    snprintf(err, sizeof(err), "%s/stderr", directory);
    snprintf(data, sizeof(data), "%s/perf.data", directory);
    snprintf(commands, sizeof(commands), "%s/check.gdb", directory);
    FILE* source = valid ? fopen(assembly, "w") : 0;
    valid &= source != 0;
    if (source != 0)
    {
        fputs(".text\n.globl _start,debug_outer,debug_leaf\n.type _start,@function\n_start:\n.cfi_startproc\n.cfi_undefined 16\n"
              "mov $101,%ebx\nmov $102,%ebp\nmov $103,%r12d\nmov $104,%r13d\nmov $105,%r14d\nmov $106,%r15d\n"
              "call debug_outer\ncmp $105,%rax\nsetne %dil\nmovzbl %dil,%edi\nmov $60,%eax\nsyscall\n.cfi_endproc\n.size _start,.-_start\n", source);
        const char* names[] = { "debug_outer", "debug_leaf" };
        for (uint32_t function = 0; function < 2; function += 1)
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/code%u", directory, function);
            valid &= debug_test_write_file(path, native[function].bytes, native[function].size);
            fprintf(source, ".balign 16\n.type %s,@function\n%s:\n.incbin \"%s\"\n.size %s,.-%s\n", names[function], names[function], path, names[function], names[function]);
            for (uint32_t ref = 0; ref < native[function].code.reference_count; ref += 1)
            {
                const LinkReference* reference = &native[function].code.references[ref];
                const uint32_t distance = x86_fixup_distance(native[function].code.records[reference->instruction].form, reference->field);
                const uint32_t field = native[function].ends[ref] - distance;
                fprintf(source, ".reloc %s+%u,R_X86_64_PC32,debug_leaf%+" PRId64 "\n", names[function], field, reference->addend - distance);
            }
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/frames", directory);
        valid &= debug_test_write_file(path, frame_bytes, frames.size);
        fprintf(source, ".section .eh_frame,\"a\",@progbits\n.balign 8\n.Lnative_frames:\n.incbin \"%s\"\n", path);
        for (uint32_t function = 0; function < 2; function += 1)
        {
            fprintf(source, ".reloc .Lnative_frames+%u,R_X86_64_PC32,%s\n", locations[function], names[function]);
        }
        fputs(".section .note.GNU-stack,\"\",@progbits\n", source);
        valid &= fclose(source) == 0;
        snprintf(path, sizeof(path), "%s/start.c", directory);
        const char start[] = "void _start(void) { /* process entry */ }\n\n\n\n\n\n\n\n\nunsigned long debug_outer(void) {\n return debug_leaf();\n}\n";
        valid &= debug_test_write_file(path, start, sizeof(start) - 1);
        snprintf(path, sizeof(path), "%s/leaf.c", directory);
        const char leaf[] = "// Original native generator fixture\n\n\nunsigned long debug_leaf(void) {\n /* local counter loop and sum 1..14 */\n}\n";
        valid &= debug_test_write_file(path, leaf, sizeof(leaf) - 1);
    }
    char* assemble[] = { "as", "--64", "-o", object, assembly, 0 };
    valid = valid && object_test_run(assemble, out, err) == 0;
    const String8 object_bytes = valid ? object_test_read_file(&arena, object) : (String8){ 0 };
    ImportObject imported = { 0 };
    LinkCapacity capacity = { 0 };
    Link link = { 0 };
    valid = valid && import_object_capacity(object_bytes, &capacity).error == IMPORT_ERROR_NONE && link_initialize(&link, &arena, &arena, capacity) &&
            import_object(&link, object_bytes, 0, BUSTER_LINK_SOURCE_PROGRAM, &arena, &imported).error == IMPORT_ERROR_NONE;
    const SymbolId start_symbol = valid ? debug_native_symbol(&link, BUSTER_S8("_start")) : 0;
    valid &= start_symbol != 0;
    const uint32_t start_size = valid ? (uint32_t)link.symbols.size[start_symbol] : 0;
    const String8 files[] = { BUSTER_S8("start.c"), BUSTER_S8("leaf.c") };
    const DebugFunction functions[] = {
        { .name = BUSTER_S8("_start"), .size = start_size, .first_row = 0, .row_count = 1, .file = 0, .line = 1 },
        { .name = BUSTER_S8("debug_outer"), .size = native[0].size, .first_row = 1, .row_count = 1, .file = 0, .line = 11 },
        { .name = BUSTER_S8("debug_leaf"), .size = native[1].size, .first_row = 2, .row_count = 1, .file = 1, .line = 5 },
    };
    const uint32_t offsets[] = { 0, 0, 0 };
    const uint32_t lines[] = { 1, 11, 5 };
    const DebugUnit unit = {
        .directory = { .pointer = directory, .length = strlen(directory) },
        .files = files,
        .file_count = 2,
        .functions = functions,
        .function_count = 3,
        .offsets = offsets,
        .lines = lines,
        .row_count = 3,
    };
    uint8_t* debug_bytes = arena_push(&arena, BUSTER_DEBUG_SECTIONS * BUSTER_DEBUG_TEST_BYTES, 8);
    uint32_t address_offsets[3];
    DebugOutput debug = debug_test_output(debug_bytes, address_offsets, 3);
    valid = valid && debug_bytes != 0 && debug_write(&unit, &debug, false) == DEBUG_OK && debug_test_native_write(object, executable, &unit, &debug);
    uint32_t* stops = valid ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, native[1].code.count) : 0;
    const uint32_t stop_count = stops != 0 ? unwind_native_stops(&native[1], stops) : 0;
    valid = valid && stops != 0 && unwind_native_gdb_script(commands, stops, stop_count);
    char* gdb[] = { "gdb", "-nx", "-batch", "-x", commands, executable, 0 };
    const bool ran_gdb = valid && object_test_run(gdb, out, err) == 0;
    const String8 backtraces = ran_gdb ? object_test_read_file(&arena, out) : (String8){ 0 };
    const bool gdb_ok = ran_gdb && unwind_native_backtraces(backtraces.pointer, stop_count);
    valid &= gdb_ok;
    printf("unwind native GDB%s: %u stops outside the leaf's loop, its entry and %u stack transitions among them, three frames and six restored registers: %s\n",
           pointer ? " (frame pointer, one CIE)" : "", stop_count, native[1].unwind.event_count, gdb_ok ? "pass" : "FAIL");
    if (!gdb_ok && backtraces.pointer != 0)
    {
        printf("%s\n", backtraces.pointer);
    }
    char* record[] = { "perf", "record", "-q", "-m", "4M", "-e", "cycles:u", "-c", "1000000", "--call-graph", "dwarf", "-o", data, "--", executable, 0 };
    char* script[] = { "perf", "script", "-i", data, "-F", "ip,sym,srcline", 0 };
    const bool recorded = valid && object_test_run(record, out, err) == 0;
    const bool scripted = recorded && object_test_run(script, out, err) == 0;
    const String8 profile = scripted ? object_test_read_file(&arena, out) : (String8){ 0 };
    const bool perf_ok = scripted && debug_test_profile(profile.pointer);
    valid &= perf_ok;
    printf("unwind native perf%s: sampled Tier0 leaf -> outer -> process entry with source rows: %s\n", pointer ? " (frame pointer, one CIE)" : "", perf_ok ? "pass" : "FAIL");
    if (keep || !valid)
    {
        printf("unwind native fixture files: %s\n", directory);
    }
    else
    {
        char* remove[] = { "rm", "-r", directory, 0 };
        valid &= object_test_run(remove, "/dev/null", "/dev/null") == 0;
    }
    arena_release(&arena);
    return valid;
}

static bool unwind_test_consumers(bool keep)
{
    const bool fixed = unwind_test_consumer(keep, false);
    const bool pointer = unwind_test_consumer(keep, true);
    return fixed && pointer;
}
