// Build-driver-only optnone audit (#1376). Included by build.c after
// tools/clang_analyze.c, whose compile-database readers and spawn helper it
// reuses; this is not a compiler module.
//
// The unity `ide` compiles its test bodies optnone to bound compile memory
// (#781). `#pragma clang optimize off` marks only the functions *defined*
// inside its region, but a production function still inherits optnone when
// the production header defining it is first included from a test source,
// and nothing fails when that happens: every test still passes while the
// trusted Release `ide` runs that function at -O0. optnone_audit replays the
// configured Clang compile of src/buster/apps/ide/ide.c through the frontend
// only (-S -emit-llvm -Xclang -disable-llvm-passes, line tables only), maps
// every defined function to the file of its DISubprogram, and fails when a
// function outside src/buster/tests/ carries optnone. The frontend replay of
// the unity TU takes seconds; no optimization or code generation runs.
//
// Map:
//   optnone_audit_scan          two passes over textual IR: attribute groups
//                               and debug files/subprograms, then definitions
//   optnone_audit_command       the replay argv derived from one database row
//   optnone_audit_main          CLI: optnone_audit BUILD_DIRECTORY [--config C]
//   optnone_audit_self_test     scan fixture with a production violation
//   optnone_audit_command_add   schedule beside clang_analyze

#define BUSTER_OPTNONE_AUDIT_TIMEOUT_SECONDS 600
#define BUSTER_OPTNONE_AUDIT_REPORT_LIMIT 64

typedef struct OptnoneAuditFile OptnoneAuditFile;
struct OptnoneAuditFile
{
    u64 id;
    String8 filename;
};

typedef struct OptnoneAuditSubprogram OptnoneAuditSubprogram;
struct OptnoneAuditSubprogram
{
    u64 id;
    u64 file;
};

typedef struct OptnoneAuditResult OptnoneAuditResult;
struct OptnoneAuditResult
{
    String8List violations;
    u64 defined;
    u64 optnone_tests;
    u64 optnone_outside;
    bool valid;
};

// Decimal digits at text[*index]; advances past them.
BUSTER_GLOBAL_LOCAL u64 optnone_audit_number(String8 text, u64* index)
{
    u64 value = 0;
    while (*index < text.length && text.pointer[*index] >= '0' && text.pointer[*index] <= '9')
    {
        value = value * 10 + (u64)(text.pointer[*index] - '0');
        *index += 1;
    }
    return value;
}

BUSTER_GLOBAL_LOCAL bool optnone_audit_starts_with(String8 line, String8 prefix)
{
    return line.length >= prefix.length && memcmp(line.pointer, prefix.pointer, prefix.length) == 0;
}

// Offset of `needle` in `line` at or after `from`, or line.length.
BUSTER_GLOBAL_LOCAL u64 optnone_audit_find(String8 line, u64 from, String8 needle)
{
    u64 result = line.length;
    for (u64 i = from; result == line.length && needle.length <= line.length && i + needle.length <= line.length; i += 1)
    {
        if (memcmp(line.pointer + i, needle.pointer, needle.length) == 0)
        {
            result = i;
        }
    }
    return result;
}

// Metadata ids appear in ascending order in textual IR, so both tables are
// already sorted and a binary search resolves a definition's debug file.
BUSTER_GLOBAL_LOCAL String8 optnone_audit_filename(OptnoneAuditSubprogram const* subprograms, u64 subprogram_count, OptnoneAuditFile const* files,
                                                   u64 file_count, u64 subprogram_id)
{
    String8 result = S8("?");
    u64 low = 0;
    u64 high = subprogram_count;
    while (low < high)
    {
        u64 middle = low + (high - low) / 2;
        if (subprograms[middle].id < subprogram_id)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    if (low < subprogram_count && subprograms[low].id == subprogram_id)
    {
        u64 file_id = subprograms[low].file;
        u64 file_low = 0;
        u64 file_high = file_count;
        while (file_low < file_high)
        {
            u64 middle = file_low + (file_high - file_low) / 2;
            if (files[middle].id < file_id)
            {
                file_low = middle + 1;
            }
            else
            {
                file_high = middle;
            }
        }
        if (file_low < file_count && files[file_low].id == file_id)
        {
            result = files[file_low].filename;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL OptnoneAuditResult optnone_audit_scan(Arena* arena, String8 ir)
{
    OptnoneAuditResult result = {.valid = true};
    String8 file_marker = S8(" = !DIFile(filename: \"");
    String8 subprogram_marker = S8(" = distinct !DISubprogram(");
    // A counting pass sizes the three tables exactly: the unity TU's IR runs
    // to millions of lines but only tens of attribute groups, hundreds of
    // files and thousands of subprograms.
    u64 group_capacity = 0;
    u64 file_capacity = 0;
    u64 subprogram_capacity = 0;
    for (u64 start = 0; start < ir.length;)
    {
        u64 end = start;
        while (end < ir.length && ir.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 line = {.pointer = ir.pointer + start, .length = end - start};
        group_capacity += optnone_audit_starts_with(line, S8("attributes #"));
        if (line.length && line.pointer[0] == '!')
        {
            file_capacity += optnone_audit_find(line, 1, file_marker) < line.length;
            subprogram_capacity += optnone_audit_find(line, 1, subprogram_marker) < line.length;
        }
        start = end + 1;
    }
    // Attribute groups carrying optnone: few, so a linear set suffices.
    u64* optnone_groups = arena_allocate(arena, u64, group_capacity + 1);
    u64 optnone_group_count = 0;
    OptnoneAuditFile* files = arena_allocate(arena, OptnoneAuditFile, file_capacity + 1);
    u64 file_count = 0;
    OptnoneAuditSubprogram* subprograms = arena_allocate(arena, OptnoneAuditSubprogram, subprogram_capacity + 1);
    u64 subprogram_count = 0;
    for (u64 start = 0; start < ir.length;)
    {
        u64 end = start;
        while (end < ir.length && ir.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 line = {.pointer = ir.pointer + start, .length = end - start};
        if (optnone_audit_starts_with(line, S8("attributes #")))
        {
            u64 index = 12;
            u64 group = optnone_audit_number(line, &index);
            u64 attribute = optnone_audit_find(line, index, S8(" optnone"));
            u64 after = attribute + 8;
            if (attribute < line.length && (after == line.length || line.pointer[after] == ' ' || line.pointer[after] == '}'))
            {
                optnone_groups[optnone_group_count++] = group;
            }
        }
        else if (line.length && line.pointer[0] == '!')
        {
            u64 index = 1;
            u64 id = optnone_audit_number(line, &index);
            u64 file = optnone_audit_find(line, index, file_marker);
            u64 subprogram = file == line.length ? optnone_audit_find(line, index, subprogram_marker) : line.length;
            if (file == index)
            {
                u64 name_start = file + file_marker.length;
                u64 name_end = optnone_audit_find(line, name_start, S8("\""));
                files[file_count++] = (OptnoneAuditFile){
                    .id = id,
                    .filename = {.pointer = line.pointer + name_start, .length = name_end - name_start},
                };
            }
            else if (subprogram == index)
            {
                u64 file_reference = optnone_audit_find(line, subprogram, S8("file: !"));
                if (file_reference < line.length)
                {
                    u64 file_index = file_reference + 7;
                    subprograms[subprogram_count++] = (OptnoneAuditSubprogram){
                        .id = id,
                        .file = optnone_audit_number(line, &file_index),
                    };
                }
            }
        }
        start = end + 1;
    }
    for (u64 start = 0; start < ir.length;)
    {
        u64 end = start;
        while (end < ir.length && ir.pointer[end] != '\n')
        {
            end += 1;
        }
        String8 line = {.pointer = ir.pointer + start, .length = end - start};
        u64 name = optnone_audit_starts_with(line, S8("define ")) ? optnone_audit_find(line, 7, S8("@")) : line.length;
        if (name < line.length)
        {
            result.defined += 1;
            // The parameter list may nest parentheses (byval(%struct.S) and
            // the like); the function attribute group follows its close.
            u64 open = optnone_audit_find(line, name, S8("("));
            u64 cursor = open;
            u64 depth = 0;
            bool closed = false;
            while (cursor < line.length && !closed)
            {
                depth += line.pointer[cursor] == '(';
                depth -= line.pointer[cursor] == ')';
                closed = line.pointer[cursor] == ')' && depth == 0;
                cursor += 1;
            }
            bool optnone = false;
            u64 debug = line.length;
            for (u64 index = cursor; index < line.length; index += 1)
            {
                if (line.pointer[index] == '#' && index + 1 < line.length && line.pointer[index + 1] >= '0' && line.pointer[index + 1] <= '9')
                {
                    u64 number_index = index + 1;
                    u64 group = optnone_audit_number(line, &number_index);
                    for (u64 g = 0; g < optnone_group_count; g += 1)
                    {
                        optnone = optnone || optnone_groups[g] == group;
                    }
                }
                else if (debug == line.length && line.pointer[index] == '!' && optnone_audit_starts_with(
                             (String8){.pointer = line.pointer + index, .length = line.length - index}, S8("!dbg !")))
                {
                    debug = index + 6;
                }
            }
            if (optnone)
            {
                u64 debug_index = debug;
                String8 filename = debug < line.length ? optnone_audit_filename(subprograms, subprogram_count, files, file_count,
                                                                                optnone_audit_number(line, &debug_index))
                                                       : S8("?");
                if (string_contains(filename, S8("buster/tests/")))
                {
                    result.optnone_tests += 1;
                }
                else
                {
                    result.optnone_outside += 1;
                    u64 name_end = open < line.length ? open : line.length;
                    String8 function = {.pointer = line.pointer + name + 1, .length = name_end - name - 1};
                    string8_list_push(arena, &result.violations, string_format(arena, S8("{S8} ({S8})"), function, filename));
                }
            }
        }
        start = end + 1;
    }
    result.valid = result.defined != 0;
    return result;
}

// The database row's compile, redirected to frontend-only IR at `output`:
// no object, no dependency file, line tables only.
BUSTER_GLOBAL_LOCAL SliceString8 optnone_audit_command(Arena* arena, SliceString8 arguments, String8 output)
{
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    for (u64 i = 0; i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        bool takes_value = string_equal(argument, S8("-o")) || string_equal(argument, S8("-MT")) || string_equal(argument, S8("-MF")) ||
                           string_equal(argument, S8("-MQ"));
        bool drop = takes_value || string_equal(argument, S8("-c")) || string_equal(argument, S8("-MD")) || string_equal(argument, S8("-MMD"));
        if (!drop)
        {
            os_argument_builder_append(&builder, argument);
        }
        i += takes_value;
    }
    String8 appended[] = {S8("-S"), S8("-emit-llvm"), S8("-Xclang"), S8("-disable-llvm-passes"), S8("-gline-tables-only"), S8("-o"), output};
    for (u64 i = 0; i < BUSTER_ARRAY_LENGTH(appended); i += 1)
    {
        os_argument_builder_append(&builder, appended[i]);
    }
    return os_argument_builder_flush(&builder);
}

BUSTER_GLOBAL_LOCAL bool optnone_audit_report(OptnoneAuditResult audit, bool quiet)
{
    bool pass = audit.valid && audit.optnone_outside == 0;
    if (!quiet || !pass)
    {
        string_print(S8("OPTNONE_AUDIT defined={u64} optnone_tests={u64} optnone_outside_tests={u64} result={S8}\n"), audit.defined,
                     audit.optnone_tests, audit.optnone_outside, pass ? S8("pass") : S8("fail"));
    }
    u64 reported = 0;
    for (String8Node* node = audit.violations.first; node && reported < BUSTER_OPTNONE_AUDIT_REPORT_LIMIT; node = node->next)
    {
        string_print(S8("error: production function compiled optnone: {S8}\n"), node->string);
        reported += 1;
    }
    if (!audit.valid)
    {
        string_print(S8("error: optnone audit found no function definitions in the replayed IR\n"));
    }
    return pass;
}

BUSTER_GLOBAL_LOCAL bool optnone_audit_self_test(Arena* arena)
{
    String8 ir = S8("define internal i32 @test_body(i32 %0) #0 !dbg !10 {\n"
                    "define dso_local i32 @production(ptr byval(%struct.S) align 8 %0) #1 !dbg !12 {\n"
                    "define internal fastcc i32 @inherited(i32 %0) unnamed_addr #0 !dbg !14 {\n"
                    "declare i32 @declared(i32) #0\n"
                    "attributes #0 = { noinline nounwind optnone uwtable }\n"
                    "attributes #1 = { nounwind uwtable \"target-cpu\"=\"x86-64\" }\n"
                    "!3 = !DIFile(filename: \"src/buster/tests/x_test.c\", directory: \"/r\")\n"
                    "!4 = !DIFile(filename: \"src/buster/lib/x.h\", directory: \"/r\")\n"
                    "!10 = distinct !DISubprogram(name: \"test_body\", scope: !3, file: !3, line: 1, unit: !1)\n"
                    "!12 = distinct !DISubprogram(name: \"production\", scope: !4, file: !4, line: 2, unit: !1)\n"
                    "!14 = distinct !DISubprogram(name: \"inherited\", scope: !4, file: !4, line: 3, unit: !1)\n");
    OptnoneAuditResult audit = optnone_audit_scan(arena, ir);
    bool pass = audit.valid && audit.defined == 3 && audit.optnone_tests == 1 && audit.optnone_outside == 1 && audit.violations.first &&
                string_equal(audit.violations.first->string, S8("inherited (src/buster/lib/x.h)"));
    SliceString8 command = optnone_audit_command(arena, (SliceString8)BUSTER_ARRAY_TO_SLICE(((String8[]){
        S8("clang"), S8("-O3"), S8("-MD"), S8("-MT"), S8("t.o"), S8("-MF"), S8("t.d"), S8("-o"), S8("t.o"), S8("-c"), S8("ide.c"),
    })), S8("/out/ide.ll"));
    pass = pass && command.length == 10 && string_equal(command.pointer[1], S8("-O3")) && string_equal(command.pointer[2], S8("ide.c")) &&
           string_equal(command.pointer[3], S8("-S")) && string_equal(command.pointer[9], S8("/out/ide.ll"));
    if (!pass)
    {
        string_print(S8("error: optnone_audit self-test failed\n"));
    }
    return pass;
}

BUSTER_GLOBAL_LOCAL ProcessResult optnone_audit_main(Arena* arena, SliceString8 arguments)
{
    String8 build_directory = {0};
    String8 config = {0};
    bool quiet = false;
    bool self_test = false;
    bool valid = true;
    for (u64 i = 0; valid && i < arguments.length; i += 1)
    {
        String8 argument = arguments.pointer[i];
        if (string_equal(argument, S8("--config")) && i + 1 < arguments.length)
        {
            config = arguments.pointer[++i];
        }
        else if (string_equal(argument, S8("--quiet")))
        {
            quiet = true;
        }
        else if (string_equal(argument, S8("--self-test")))
        {
            self_test = true;
        }
        else if (!build_directory.length && argument.length && argument.pointer[0] != '-')
        {
            build_directory = argument;
        }
        else
        {
            string_print(S8("error: optnone_audit: unexpected argument {S8}\nusage: optnone_audit BUILD_DIRECTORY [--config CONFIG] [--quiet] | --self-test\n"),
                         argument);
            valid = false;
        }
    }
    bool pass = valid && optnone_audit_self_test(arena);
    if (pass && !self_test)
    {
        String8 database = clang_analyze_read(arena, clang_analyze_compile_commands_path(arena, build_directory));
        JsonParser parser = {.text = database};
        bool parsed = database.pointer && json_consume(&parser, '[');
        bool done = parsed && json_consume(&parser, ']');
        CompileCommandEntry chosen = {0};
        SliceString8 chosen_arguments = {0};
        while (parsed && !done && !chosen_arguments.length)
        {
            CompileCommandEntry entry = json_parse_compile_command_entry(arena, &parser, &parsed);
            SliceString8 entry_arguments = entry.arguments;
            if (parsed && !entry_arguments.length && entry.command.length)
            {
                entry_arguments = shell_split(arena, entry.command, &parsed);
            }
            bool ide = string_ends_with_sequence(entry.file, S8("apps/ide/ide.c")) || string_ends_with_sequence(entry.file, S8("apps\\ide\\ide.c"));
            if (parsed && ide && entry_arguments.length && clang_analyze_entry_matches_config(arena, entry, entry_arguments, config))
            {
                chosen = entry;
                chosen_arguments = entry_arguments;
            }
            done = parsed && !chosen_arguments.length && json_consume(&parser, ']');
            parsed = parsed && (done || chosen_arguments.length || json_consume(&parser, ','));
        }
        // Only a GNU-style Clang driver has a frontend-only replay here; the
        // pragma under audit is Clang's, and clang-cl rows are out of scope.
        String8 compiler = chosen_arguments.length ? chosen_arguments.pointer[0] : (String8){0};
        bool clang = string_contains(compiler, S8("clang")) && !string_contains(compiler, S8("clang-cl"));
        if (!parsed || !chosen_arguments.length)
        {
            string_print(S8("error: optnone_audit: no {S8} row for apps/ide/ide.c in {S8}\n"), config.length ? config : S8("any-config"), build_directory);
            pass = false;
        }
        else if (!clang)
        {
            string_print(S8("OPTNONE_AUDIT skipped: {S8} is not a GNU-style Clang driver\n"), compiler);
        }
        else
        {
            String8 directory = os_path_absolute_lexical(arena, path_join(arena, build_directory, S8("optnone-audit")), true);
            os_make_directory(directory);
            String8 output = path_join(arena, directory, S8("ide.ll"));
            SliceString8 command = optnone_audit_command(arena, chosen_arguments, output);
            ProcessSpawnResult spawn = clang_analyze_spawn(arena, command, chosen.directory);
            ProcessWaitResult wait = os_process_wait_deadline(arena, spawn, (u64)BUSTER_OPTNONE_AUDIT_TIMEOUT_SECONDS * 1000000);
            if (wait.result != PROCESS_RESULT_SUCCESS)
            {
                String8 error_output = {.pointer = (char8*)wait.streams[STANDARD_STREAM_ERROR].pointer, .length = wait.streams[STANDARD_STREAM_ERROR].length};
                string_print(S8("error: optnone_audit: the frontend replay failed\n{S8}\n"), error_output);
                pass = false;
            }
            else
            {
                // The unity TU's IR is hundreds of megabytes; map it rather
                // than copy it into the driver's arena.
                FileMapRead map = file_map_read(arena, output, (FileReadOptions){.map_required = 1});
                String8 ir = {.pointer = (char8*)map.bytes.pointer, .length = map.bytes.length};
                pass = ir.pointer && optnone_audit_report(optnone_audit_scan(arena, ir), quiet);
                if (!ir.pointer)
                {
                    string_print(S8("error: optnone_audit: could not map {S8}\n"), output);
                }
                file_map_unmap(map);
            }
        }
    }
    return pass ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
}

BUSTER_GLOBAL_LOCAL void optnone_audit_command_add(Arena* arena, String8 build_directory, CmakeBuildOptions options)
{
    BuildStep* step = step_add(arena);
    ProcessRun* run = run_add(arena, step);
    OsArgumentBuilder builder = os_argument_builder_start(arena);
    String8 self = program_state->input.arguments.length ? program_state->input.arguments.pointer[0] : S8("build/build");
    os_argument_builder_append(&builder, self);
    os_argument_builder_append(&builder, S8("optnone_audit"));
    os_argument_builder_append(&builder, build_directory);
    os_argument_builder_append(&builder, S8("--config"));
    os_argument_builder_append(&builder, cmake_build_config(options));
    if (options.quiet)
    {
        os_argument_builder_append(&builder, S8("--quiet"));
    }

    *run = (ProcessRun){
        .arguments = os_argument_builder_flush(&builder),
        .spawn_options =
            (ProcessSpawnOptions){
                .use_process_environment = 1,
            },
    };
}
