// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Raw built-in linker command. Driver options are removed without reordering foreign_arguments' inputs, archive
// controls or library-search controls. The caller supplies startup objects and libraries in their desired order;
// runtime discovery supplies only the default interpreter for a PIE. The default is a static executable at _start.
//
// Conversion precedes optional section GC and literal merging, then the existing lane-parallel writer. The original
// Foreign and all arenas remain live through writing. Entry lookup uses Foreign's canonical names again after GC's
// remapping. No source/unwind offsets are consumed after literal merging; its offset map remains live until writing.
// Unsupported literal expressions retain the unchanged input. Other failures stop before the writer is invoked.

typedef struct ForeignDriver ForeignDriver;
struct ForeignDriver
{
    ForeignCommand command;
    String8 output;
    String8 entry;
    String8 interpreter;
    RuntimeKind kind;
    ForeignPath path;
    uint32_t lanes;
    bool gc;
    bool merge_strings;
    bool dedup_unwind;
    bool help;
};

typedef struct ForeignDriverWrite ForeignDriverWrite;
struct ForeignDriverWrite
{
    Link* link;
    ExecutableOptions options;
    ExecutableResult result;
    bool succeeded;
};

static void foreign_driver_usage(void)
{
    fprintf(stderr, "usage: link-foreign [-o OUTPUT] [-e ENTRY] [-static|-pie] [--dynamic-linker PATH]\n"
                    "                    [--gc-sections|--no-gc-sections] [--merge-strings|--no-merge-strings]\n"
                    "                    [--dedup-unwind|--no-dedup-unwind]\n"
                    "                    [--scalar] [--lanes N] INPUT... [-L DIR] [-l LIB] [-u NAME]\n"
                    "Defaults: static executable, a.out, entry _start, one lane; GC, string merging and unwind deduplication off.\n"
                    "Supply startup objects and runtime libraries explicitly, in link order.\n"
                    "Use -pie for C runtime startup; static output currently supports no-libc inputs.\n"
                    "Shared-library and static-PIE outputs are not supported by this command yet.\n"
                    "Static output searches archives by default; PIE output prefers shared libraries.\n"
                    "Archive groups, --whole-archive, --as-needed, -Bstatic and -Bdynamic retain input order.\n");
}

static bool foreign_driver_count(const char* text, uint32_t* count)
{
    uint32_t value = 0;
    bool valid = text != 0 && text[0] != 0;
    for (uint64_t i = 0; valid && text[i] != 0; i += 1)
    {
        valid = text[i] >= '0' && text[i] <= '9' && value <= BUSTER_LANE_MAX_COUNT / 10;
        if (valid)
        {
            value = value * 10 + (uint32_t)(text[i] - '0');
            valid = value <= BUSTER_LANE_MAX_COUNT;
        }
    }
    valid = valid && value != 0;
    *count = valid ? value : 0;
    return valid;
}

static bool foreign_driver_arguments(ForeignDriver* driver, int argc, char** argv, Arena* arena)
{
    *driver = (ForeignDriver){
        .output = foreign_string("a.out"),
        .entry = foreign_string("_start"),
        .kind = RUNTIME_KIND_STATIC,
        .path = FOREIGN_PATH_VECTOR,
        .lanes = 1,
    };
    char** inputs = argc >= 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)argc + 2) : 0;
    bool valid = inputs != 0;
    uint32_t count = 1;
    for (int i = 0; valid && i < argc; i += 1)
    {
        const char* argument = argv[i];
        const bool foreign_value = strcmp(argument, "-L") == 0 || strcmp(argument, "-l") == 0 || strcmp(argument, "-u") == 0 || strcmp(argument, "--undefined") == 0;
        const bool output = strcmp(argument, "-o") == 0 || strcmp(argument, "--output") == 0 || foreign_starts_with(argument, "--output=") ||
                            (foreign_starts_with(argument, "-o") && argument[2] != 0);
        const bool entry = strcmp(argument, "-e") == 0 || strcmp(argument, "--entry") == 0 || foreign_starts_with(argument, "--entry=");
        const bool interpreter = strcmp(argument, "-dynamic-linker") == 0 || strcmp(argument, "--dynamic-linker") == 0 || foreign_starts_with(argument, "--dynamic-linker=");
        const bool lanes = strcmp(argument, "--lanes") == 0 || foreign_starts_with(argument, "--lanes=");
        if (foreign_value)
        {
            inputs[count] = argv[i];
            count += 1;
            if (i + 1 < argc)
            {
                i += 1;
                inputs[count] = argv[i];
                count += 1;
            }
        }
        else if (output || entry || interpreter || lanes)
        {
            const char* equals = strchr(argument, '=');
            const char* value = output && argument[0] == '-' && argument[1] == 'o' && argument[2] != 0 ? argument + 2 : equals != 0 ? equals + 1 : 0;
            if (value == 0 && i + 1 < argc)
            {
                i += 1;
                value = argv[i];
            }
            valid = value != 0 && value[0] != 0;
            if (valid && output)
            {
                driver->output = foreign_string(value);
            }
            else if (valid && entry)
            {
                driver->entry = foreign_string(value);
            }
            else if (valid && interpreter)
            {
                driver->interpreter = foreign_string(value);
            }
            else if (valid)
            {
                valid = foreign_driver_count(value, &driver->lanes);
            }
            if (!valid)
            {
                fprintf(stderr, "link-foreign: invalid or missing value for %s\n", argument);
            }
        }
        else if (strcmp(argument, "-pie") == 0)
        {
            driver->kind = RUNTIME_KIND_DYNAMIC;
        }
        else if (strcmp(argument, "-static") == 0)
        {
            driver->kind = RUNTIME_KIND_STATIC;
            inputs[count] = argv[i];
            count += 1;
        }
        else if (strcmp(argument, "--gc-sections") == 0 || strcmp(argument, "--no-gc-sections") == 0)
        {
            driver->gc = strcmp(argument, "--gc-sections") == 0;
        }
        else if (strcmp(argument, "--merge-strings") == 0 || strcmp(argument, "--no-merge-strings") == 0)
        {
            driver->merge_strings = strcmp(argument, "--merge-strings") == 0;
        }
        else if (strcmp(argument, "--dedup-unwind") == 0 || strcmp(argument, "--no-dedup-unwind") == 0)
        {
            driver->dedup_unwind = strcmp(argument, "--dedup-unwind") == 0;
        }
        else if (strcmp(argument, "--scalar") == 0)
        {
            driver->path = FOREIGN_PATH_EXACT;
        }
        else if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0)
        {
            driver->help = true;
        }
        else
        {
            inputs[count] = argv[i];
            count += 1;
        }
    }
    if (valid && !driver->help)
    {
        inputs[0] = driver->kind == RUNTIME_KIND_STATIC ? (char*)"-Bstatic" : (char*)"-Bdynamic";
        const ForeignFailure failure = foreign_arguments(&driver->command, (int)count, inputs, arena);
        valid = failure.error == FOREIGN_ERROR_NONE && driver->command.argument_count != 0 && driver->entry.length <= UINT32_MAX;
        if (failure.error == FOREIGN_ERROR_ARGUMENTS)
        {
            fprintf(stderr, "link-foreign: invalid input option: %s\n", inputs[failure.argument]);
        }
        if (valid)
        {
            // foreign_arguments reserves count + 1 entries, leaving room for the entry's forced reference.
            driver->command.undefined[driver->command.undefined_count] = driver->entry;
            driver->command.undefined_count += 1;
        }
    }
    return valid;
}

static SymbolId foreign_driver_entry(const ForeignDriver* driver, const Foreign* foreign, const ForeignLink* converted)
{
    const uint64_t hash = link_hash((const uint8_t*)driver->entry.pointer, driver->entry.length);
    const uint32_t name = foreign_find_name(foreign, (const uint8_t*)driver->entry.pointer, (uint32_t)driver->entry.length, hash);
    return name != 0 ? converted->names[name - 1] : 0;
}

static void foreign_driver_conversion_error(const Foreign* foreign, ForeignLinkResult result, RuntimeKind kind)
{
    String8 shared_library = { 0 };
    if (kind == RUNTIME_KIND_STATIC && result.error == FOREIGN_LINK_ERROR_UNSUPPORTED)
    {
        for (uint32_t library = 0; library < foreign->library_count && shared_library.pointer == 0; library += 1)
        {
            if (foreign->libraries[library].needed)
            {
                const uint32_t input = foreign->libraries[library].input;
                shared_library = foreign->inputs[input].path;
            }
        }
    }
    if (shared_library.pointer != 0)
    {
        fprintf(stderr, "link-foreign: static output cannot use shared library %.*s; use -pie or select an archive with -Bstatic\n",
                (int)shared_library.length, shared_library.pointer);
    }
    else
    {
        static const char* const messages[] = {
            "none",
            "invalid input",
            "out of memory",
            "input exceeds linker capacity",
            "multiple definitions",
            "undefined symbol",
            "unsupported input",
            "unsupported or malformed relocation",
            "invalid link representation",
        };
        const char* message = (uint32_t)result.error < BUSTER_ARRAY_LENGTH(messages) ? messages[result.error] : "conversion failed";
        fprintf(stderr, "link-foreign: %s", message);
        if (result.name != 0 && result.name <= foreign->names.count)
        {
            const uint32_t name = result.name - 1;
            fprintf(stderr, ": %.*s", (int)foreign->names.length[name], foreign->names.bytes[name]);
        }
        if (result.object != 0 && result.object < foreign->object_count)
        {
            const uint32_t input = foreign->objects[result.object].input;
            if (input < foreign->input_count)
            {
                fprintf(stderr, " in %.*s", (int)foreign->inputs[input].path.length, foreign->inputs[input].path.pointer);
            }
        }
        fprintf(stderr, " (section %u, relocation %u, offset %llu)\n", result.import.section, result.import.relocation, (unsigned long long)result.import.offset);
    }
}

static void foreign_driver_write_lane(void* argument)
{
    ForeignDriverWrite* write = argument;
    const bool succeeded = executable_write(write->link, &write->options, &write->result);
    if (lane_index() == 0)
    {
        write->succeeded = succeeded;
    }
}

static int run_foreign_link(int argc, char** argv)
{
    Arena arena = arena_reserve(1ull << 36);
    Arena names = arena_reserve(1ull << 34);
    ForeignDriver driver = { 0 };
    bool succeeded = arena.base != 0 && names.base != 0 && foreign_driver_arguments(&driver, argc, argv, &arena);
    Foreign foreign = { 0 };
    Runtime runtime = { 0 };
    ForeignLink converted = { 0 };
    ForeignUnwindDedupLink deduplicated = { 0 };
    if (!succeeded || driver.help)
    {
        foreign_driver_usage();
    }
    if (succeeded && !driver.help && driver.kind == RUNTIME_KIND_DYNAMIC && driver.interpreter.length == 0)
    {
        succeeded = runtime_import(&runtime) == RUNTIME_ERROR_NONE && runtime.loader_path.length != 0;
        driver.interpreter = runtime.loader_path;
        if (!succeeded)
        {
            fprintf(stderr, "link-foreign: no system interpreter found; pass --dynamic-linker PATH\n");
        }
    }
    if (succeeded && !driver.help)
    {
        foreign_run(&foreign, &driver.command, driver.path, driver.lanes, 0);
        succeeded = foreign.failure.error == FOREIGN_ERROR_NONE;
        if (!succeeded)
        {
            static const char* const messages[] = {
                "none",
                "invalid input argument",
                "cannot read input",
                "library or script input not found",
                "unrecognized input format",
                "malformed input",
                "unsupported input",
                "out of memory",
                "cannot start linker workers",
            };
            const uint32_t error = (uint32_t)foreign.failure.error;
            fprintf(stderr, "link-foreign: %s", error < BUSTER_ARRAY_LENGTH(messages) ? messages[error] : "input resolution failed");
            if (foreign.failure.input < foreign.input_count)
            {
                const String8 path = foreign.inputs[foreign.failure.input].path;
                fprintf(stderr, " in %.*s", (int)path.length, path.pointer);
            }
            if (foreign.failure.system_error != 0)
            {
                fprintf(stderr, ": %s", strerror(foreign.failure.system_error));
            }
            fprintf(stderr, "\n");
        }
    }
    if (succeeded && !driver.help)
    {
        ForeignLinkResult result = driver.gc ? foreign_link_build_for_gc(&converted, &foreign, &arena, &names, driver.kind, driver.interpreter) :
                                   driver.kind == RUNTIME_KIND_DYNAMIC ? foreign_link_build_dynamic(&converted, &foreign, &arena, &names, driver.interpreter) :
                                                                        foreign_link_build(&converted, &foreign, &arena, &names);
        if (result.error == FOREIGN_LINK_ERROR_NONE && driver.gc)
        {
            const ForeignGcRoots roots = { .entry = foreign_driver_entry(&driver, &foreign, &converted) };
            ForeignLink compacted = { 0 };
            result = foreign_gc_link(&compacted, &foreign, &converted, &roots, driver.path, &arena, &names);
            if (result.error == FOREIGN_LINK_ERROR_NONE)
            {
                converted = compacted;
            }
        }
        succeeded = result.error == FOREIGN_LINK_ERROR_NONE;
        if (!succeeded)
        {
            foreign_driver_conversion_error(&foreign, result, driver.kind);
        }
    }
    if (succeeded && !driver.help && driver.dedup_unwind)
    {
        const ForeignGcRoots roots = { .entry = foreign_driver_entry(&driver, &foreign, &converted) };
        const ForeignLinkResult result = foreign_unwind_dedup_link(&deduplicated, &foreign, &converted, &roots, driver.path, &arena, &names);
        succeeded = result.error == FOREIGN_LINK_ERROR_NONE || result.error == FOREIGN_LINK_ERROR_UNSUPPORTED;
        if (result.error == FOREIGN_LINK_ERROR_NONE)
        {
            converted = deduplicated.converted;
        }
        else if (!succeeded)
        {
            fprintf(stderr, "link-foreign: unwind deduplication failed (%u, object %u, section %u, offset %llu)\n",
                    (unsigned)result.error, result.object, result.import.section, (unsigned long long)result.import.offset);
        }
    }
    if (succeeded && !driver.help && driver.merge_strings)
    {
        ForeignStringsMerge merged = { 0 };
        const ForeignStringsError error = foreign_strings_merge(&converted, &foreign, &arena, &merged);
        succeeded = error == FOREIGN_STRINGS_ERROR_NONE || error == FOREIGN_STRINGS_ERROR_UNSUPPORTED;
        if (!succeeded)
        {
            fprintf(stderr, "link-foreign: string merging failed (%u)\n", (unsigned)error);
        }
    }
    if (succeeded && !driver.help)
    {
        ForeignDriverWrite write = {
            .link = &converted.link,
            .options = {
                .path = driver.output,
                .entry = foreign_driver_entry(&driver, &foreign, &converted),
                .dynamic = driver.kind == RUNTIME_KIND_DYNAMIC ? &converted.dynamic : 0,
                .exact = driver.path == FOREIGN_PATH_EXACT,
            },
        };
        succeeded = lane_run(driver.lanes, foreign_driver_write_lane, &write, 0) && write.succeeded;
        if (!succeeded)
        {
            fprintf(stderr, "link-foreign: executable writing failed (%u, atom %u, symbol %u, fixup %u)",
                    (unsigned)write.result.error, write.result.atom, write.result.symbol, write.result.fixup);
            if (write.result.system_error != 0)
            {
                fprintf(stderr, ": %s", strerror(write.result.system_error));
            }
            fprintf(stderr, "\n");
        }
    }
    foreign_release(&foreign);
    runtime_release(&runtime);
    arena_release(&names);
    arena_release(&arena);
    return succeeded ? 0 : 1;
}
