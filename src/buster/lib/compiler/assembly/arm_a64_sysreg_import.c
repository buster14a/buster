#include <stdlib.h>

/*
 * Build-driver importer for the official Arm A-profile SysReg XML.  The
 * licensed XML is deliberately not included in the repository.  Normal builds
 * consume the checked-in pointer-free projection.  The developer import path
 * validates a complete independently obtained source tree, then delegates the
 * actual XML parse/feature evaluation/projection to the checked-in Python
 * generator.  The normal compiler remains dependency-free because this path
 * is only entered by the explicit import command.
 */
typedef struct ArmA64SysregImportOptions ArmA64SysregImportOptions;
struct ArmA64SysregImportOptions
{
    String8 source_directory;
    String8 output_directory;
    bool output_directory_set;
};

typedef struct ArmA64SysregSourceFile ArmA64SysregSourceFile;
struct ArmA64SysregSourceFile
{
    String8 relative;
};

enum
{
    ARM_A64_EXPECTED_XML_FILE_COUNT = 1943,
    ARM_A64_EXPECTED_RELEVANT_MECHANISM_COUNT = 1400,
    ARM_A64_EXPECTED_ACCEPTED_MECHANISM_COUNT = 402,
};

BUSTER_GLOBAL_LOCAL int arm_a64_sysreg_source_file_compare(const void* left_pointer, const void* right_pointer)
{
    ArmA64SysregSourceFile const* left = left_pointer;
    ArmA64SysregSourceFile const* right = right_pointer;
    u64 count = BUSTER_MIN(left->relative.length, right->relative.length);
    int result = count ? memcmp(left->relative.pointer, right->relative.pointer, count) : 0;
    return result ? result : (left->relative.length > right->relative.length) - (left->relative.length < right->relative.length);
}

BUSTER_GLOBAL_LOCAL u32 arm_a64_sysreg_count_sequence(String8 text, String8 sequence)
{
    u32 count = 0;
    u64 position = 0;
    while (position < text.length)
    {
        u64 found = string_first_sequence(string_slice(text, position, text.length), sequence);
        if (found == BUSTER_STRING_NO_MATCH) break;
        position += found + sequence.length;
        count += 1;
    }
    return count;
}

BUSTER_GLOBAL_LOCAL bool arm_a64_sysreg_xml_valid(String8 text)
{
    bool result = text.length && string_first_sequence(text, S8("<register_page")) != BUSTER_STRING_NO_MATCH &&
                  string_first_sequence(text, S8("</register_page>")) != BUSTER_STRING_NO_MATCH;
    if (result)
    {
        // A truncated page must not be accepted merely because an opening tag was
        // copied into the input.  Every access mechanism is self-closing in the
        // release schema and therefore has an explicit closing marker.
        // Match the singular element's separating space; `<access_mechanisms>`
        // is the enclosing container and must not be counted as an opening page.
        u32 opens = arm_a64_sysreg_count_sequence(text, S8("<access_mechanism "));
        u32 closes = arm_a64_sysreg_count_sequence(text, S8("</access_mechanism>"));
        result = opens == closes;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool arm_a64_sysreg_check_file(Arena* scratch, String8 path, String8 expected, char const* label)
{
    ByteSlice actual = file_read(scratch, path, (FileReadOptions){0});
    bool result = actual.length == expected.length && (!expected.length || (actual.pointer && memcmp(actual.pointer, expected.pointer, expected.length) == 0));
    if (!result)
    {
        fprintf(stderr, "error: checked-in AArch64 sysreg %s drifted or is missing (%.*s)\n", label, (int)path.length, path.pointer);
    }

    return result;
}

BUSTER_GLOBAL_LOCAL bool arm_a64_sysreg_copy_outputs(Arena* arena, String8 output_directory, bool check_only)
{
    TemporalArena scratch_scope = scratch_begin(&arena, 1);
    Arena* scratch = scratch_scope.arena;
    static String8 names[] = {
        S8_INITIALIZER("aarch64-system-registers.generated.h"),
        S8_INITIALIZER("aarch64-system-registers.generated.jsonl"),
        S8_INITIALIZER("aarch64-system-registers-manifest.json"),
    };
    bool result = true;
    for (u32 index = 0; index < BUSTER_ARRAY_LENGTH(names); index += 1)
    {
        String8 source_path = path_join(scratch, S8("src/buster/lib/compiler/assembly/generated"), names[index]);
        String8 output_path = path_join(scratch, output_directory, names[index]);
        ByteSlice expected = file_read(scratch, source_path, (FileReadOptions){0});
        if (!expected.pointer || !expected.length)
        {
            result = false;
            break;
        }
        if (check_only)
        {
            result = arm_a64_sysreg_check_file(scratch, output_path, (String8){.pointer = (char8*)expected.pointer, .length = expected.length}, (char*)names[index].pointer) && result;
        }
        else
        {
            result = file_write(output_path, expected) && result;
        }
    }
    scratch_end(scratch_scope);
    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult arm_a64_sysreg_run_generator(Arena* arena, String8 source_directory, String8 output_directory, bool check_only)
{
    String8 arguments[4] = {0};
    u32 argument_count = 0;
    arguments[argument_count++] = S8("python3");
    arguments[argument_count++] = S8("tools/generate_aarch64_system_registers.py");
    if (check_only) arguments[argument_count++] = S8("--check");
    arguments[argument_count++] = source_directory;
    // Keep the command's output directory as the final positional input.
    // The fixed array is intentionally sized for python, script, source,
    // and output; check mode omits output because the script uses a temp
    // directory and compares against the checked-in projection.
    bool arguments_fit = check_only || argument_count < BUSTER_ARRAY_LENGTH(arguments);
    if (!check_only && arguments_fit)
    {
        arguments[argument_count++] = output_directory;
    }

    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arguments_fit)
    {
        ProcessSpawnResult spawn = os_process_spawn((SliceString8){.pointer = arguments, .length = argument_count}, (SliceString8){0}, (SliceString8){0},
                                                    (ProcessSpawnOptions){.use_process_environment = true});
        if (!spawn.handle)
        {
            fprintf(stderr, "error: unable to launch tools/generate_aarch64_system_registers.py (python3 is required for import)\n");
        }
        else
        {
            ProcessWaitResult wait = os_process_wait_sync(arena, spawn);
            result = wait.result;
        }
    }

    return result;
}

BUSTER_GLOBAL_LOCAL ProcessResult arm_a64_sysreg_import_run(Arena* arena, ArmA64SysregImportOptions options)
{
    // The three failure paths used to each call scratch_end before returning;
    // converging on one exit leaves exactly one release of the scope.
    ProcessResult result = PROCESS_RESULT_FAILED;
    if (arena && options.source_directory.length && options.output_directory.length)
    {
        TemporalArena scratch_scope = scratch_begin(&arena, 1);
        Arena* scratch = scratch_scope.arena;
        ArmA64SysregSourceFile* files = arena_allocate(arena, ArmA64SysregSourceFile, 5000);
        u32 file_count = 0;
        if (!arm_a64_source_tree_collect(arena, scratch, options.source_directory, (String8){0}, (ArmA64SourceFile*)files, &file_count, 5000))
        {
            fprintf(stderr, "error: failed to enumerate official Arm A-profile SysReg source directory\n");
        }
        else
        {
            qsort(files, file_count, sizeof(files[0]), arm_a64_sysreg_source_file_compare);
            u32 xml_count = 0, relevant_count = 0;
            bool valid = true;
            for (u32 index = 0; index < file_count && valid; index += 1)
            {
                String8 relative = files[index].relative;
                if (relative.length < 4 || memcmp(relative.pointer + relative.length - 4, S8(".xml").pointer, 4) != 0) continue;
                xml_count += 1;
                String8 path = path_join(scratch, options.source_directory, relative);
                ByteSlice bytes = file_read(scratch, path, (FileReadOptions){.end_padding = 1});
                if (!bytes.pointer || !bytes.length)
                {
                    valid = false;
                    continue;
                }
                String8 text = {.pointer = (char8*)bytes.pointer, .length = bytes.length};
                // The release also ships register_index/link and DTD/XSL resources;
                // they are part of the complete archive but are not register pages.
                // A file that advertises register content without a page root is
                // malformed rather than an ignorable index.
                if (string_first_sequence(text, S8("<register_page")) == BUSTER_STRING_NO_MATCH)
                {
                    if (string_first_sequence(text, S8("<access_mechanism ")) != BUSTER_STRING_NO_MATCH)
                    {
                        fprintf(stderr, "error: SysReg content without register_page root: %.*s\n", (int)relative.length, relative.pointer);
                        valid = false;
                        continue;
                    }
                    arena_set_position(scratch, scratch_scope.position);
                    continue;
                }
                if (!arm_a64_sysreg_xml_valid(text))
                {
                    fprintf(stderr, "error: malformed or truncated SysReg XML page: %.*s\n", (int)relative.length, relative.pointer);
                    valid = false;
                    continue;
                }
                if (string_first_sequence(text, S8("execution_state=\"AArch64\"")) == BUSTER_STRING_NO_MATCH) continue;
                relevant_count += arm_a64_sysreg_count_sequence(text, S8("accessor=\"MRS "));
                relevant_count += arm_a64_sysreg_count_sequence(text, S8("accessor=\"MSRregister "));
                relevant_count += arm_a64_sysreg_count_sequence(text, S8("accessor=\"MRRS "));
                relevant_count += arm_a64_sysreg_count_sequence(text, S8("accessor=\"MSRRregister "));
                arena_set_position(scratch, scratch_scope.position);
            }
            if (!valid || xml_count != ARM_A64_EXPECTED_XML_FILE_COUNT || relevant_count != ARM_A64_EXPECTED_RELEVANT_MECHANISM_COUNT)
            {
                fprintf(stderr, "error: SysReg source inventory mismatch xml=%u relevant=%u (expected xml=%u relevant=%u)\n", xml_count, relevant_count,
                        ARM_A64_EXPECTED_XML_FILE_COUNT, ARM_A64_EXPECTED_RELEVANT_MECHANISM_COUNT);
            }
            else
            {
                bool check_only = arm_a64_check_mode_enabled();
                if (!check_only) make_directory_recursive(arena, options.output_directory);
                ProcessResult generated = arm_a64_sysreg_run_generator(arena, options.source_directory, options.output_directory, check_only);
                bool copied = generated == PROCESS_RESULT_SUCCESS && (!check_only || arm_a64_sysreg_copy_outputs(arena, options.output_directory, true));
                if (copied)
                {
                    fprintf(stdout, "Arm A-profile SysReg checked deterministic artifacts: xml=%u relevant=%u accepted=%u output=%.*s\n", xml_count,
                            relevant_count, ARM_A64_EXPECTED_ACCEPTED_MECHANISM_COUNT, (int)options.output_directory.length,
                            options.output_directory.pointer);
                }
                result = copied ? PROCESS_RESULT_SUCCESS : PROCESS_RESULT_FAILED;
            }
        }
        scratch_end(scratch_scope);
    }

    return result;
}
