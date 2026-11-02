// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The default command's heterogeneous inputs (#1120). Classify once, before any frontend reads a file: -x is
// positional, option operands are never inputs, and -x none restores suffix selection. Pure C retains the direct
// checker/linker path. A mixed link compiles C units together on their existing lanes, assembles each assembly
// source, then gives the ordered inputs to the foreign linker with the usual runtime objects around them.
//
// Each produced ELF object has its own arena, retained through the final link and released on every exit. The
// foreign command borrows those bytes; there are no temporary files, subprocess compilers or external linkers.
// Foreign resolution retains its existing archive, symbol and relocation policies. This adapter does not implement
// new object formats or silently fall back to a different linker.

typedef enum SourceDriverKind
{
    SOURCE_DRIVER_AUTO,
    SOURCE_DRIVER_C,
    SOURCE_DRIVER_PREPROCESSED,
    SOURCE_DRIVER_ASSEMBLY,
    SOURCE_DRIVER_ASSEMBLY_CPP,
    SOURCE_DRIVER_LINK,
    SOURCE_DRIVER_INVALID,
} SourceDriverKind;

typedef struct SourceDriverInput SourceDriverInput;
struct SourceDriverInput
{
    char* path;
    SourceDriverKind kind;
    uint32_t source;
    CheckDriverSource assembly;
};

typedef struct SourceDriver SourceDriver;
struct SourceDriver
{
    SourceDriverInput* inputs;
    uint32_t input_count;
    CheckDriverSource* sources;
    uint32_t source_count;
    uint32_t assembly_count;
    char** checking;
    uint32_t checking_count;
    char** preprocessing;
    uint32_t preprocessing_count;
    char** linking;
    uint32_t linking_count;
    char** libraries;
    uint32_t library_count;
    const char* output;
    const char* lanes;
    bool scalar;
    bool compile;
    bool checking_only;
    bool intel;
    bool timing;
};

static SourceDriverKind source_driver_language(const char* language)
{
    return strcmp(language, "none") == 0 ? SOURCE_DRIVER_AUTO :
           strcmp(language, "c") == 0 || strcmp(language, "c-header") == 0 ? SOURCE_DRIVER_C :
           strcmp(language, "cpp-output") == 0 ? SOURCE_DRIVER_PREPROCESSED :
           strcmp(language, "assembler") == 0 ? SOURCE_DRIVER_ASSEMBLY :
           strcmp(language, "assembler-with-cpp") == 0 ? SOURCE_DRIVER_ASSEMBLY_CPP : SOURCE_DRIVER_INVALID;
}

static SourceDriverKind source_driver_suffix(const char* path)
{
    const char* slash = strrchr(path, '/');
    const char* name = slash != NULL ? slash + 1 : path;
    const char* suffix = strrchr(name, '.');
    return suffix != NULL && (strcmp(suffix, ".c") == 0 || strcmp(suffix, ".h") == 0) ? SOURCE_DRIVER_C :
           suffix != NULL && strcmp(suffix, ".i") == 0 ? SOURCE_DRIVER_PREPROCESSED :
           suffix != NULL && strcmp(suffix, ".s") == 0 ? SOURCE_DRIVER_ASSEMBLY :
           suffix != NULL && strcmp(suffix, ".S") == 0 ? SOURCE_DRIVER_ASSEMBLY_CPP : SOURCE_DRIVER_LINK;
}

// Options removed by the semantic driver before pp_parse_options. Their operands remain attached, even when an
// operand spells a source suffix or another option. The assembler consumes the common preprocessor options instead.
static bool source_driver_checker_option(const char* argument)
{
    static const char* const options[] = {
        "--verify", "--dump-typed", "--dump-static", "-flazy-system-headers", "--lower-all", "--poison-arenas", "--shared-parts",
        "--time-stages", "--tier1", "--tier1-report", "--tier1-refusals", "--tier1-no-rematerialize", "--tier1-gvn", "--tier1-no-lifetimes",
    };
    bool found = strcmp(argument, "--lanes") == 0 || strncmp(argument, "--lanes=", 8) == 0 ||
                 strcmp(argument, "--worker-budget") == 0 || strncmp(argument, "--worker-budget=", 16) == 0 ||
                 strcmp(argument, "--id-order") == 0 || strncmp(argument, "--id-order=", 11) == 0;
    for (uint32_t i = 0; !found && i < BUSTER_ARRAY_LENGTH(options); i += 1)
    {
        found = strcmp(argument, options[i]) == 0;
    }
    return found;
}

static bool source_driver_arguments(SourceDriver* driver, int count, char** arguments, Arena* arena)
{
    const uint64_t capacity = (uint64_t)count + 8;
    *driver = (SourceDriver){
        .inputs = BUSTER_ARENA_PUSH_ARRAY(arena, SourceDriverInput, capacity),
        .sources = BUSTER_ARENA_PUSH_ARRAY(arena, CheckDriverSource, capacity),
        .checking = BUSTER_ARENA_PUSH_ARRAY(arena, char*, capacity * 2),
        .preprocessing = BUSTER_ARENA_PUSH_ARRAY(arena, char*, capacity),
        .linking = BUSTER_ARENA_PUSH_ARRAY(arena, char*, capacity),
        .libraries = BUSTER_ARENA_PUSH_ARRAY(arena, char*, capacity),
    };
    bool valid = driver->inputs != NULL && driver->sources != NULL && driver->checking != NULL && driver->preprocessing != NULL &&
                 driver->linking != NULL && driver->libraries != NULL;
    SourceDriverKind language = SOURCE_DRIVER_AUTO;
    bool operands = false;
    if (!valid)
    {
        fprintf(stderr, "fatal error: can't reserve source driver arguments\n");
    }
    for (int i = 0; valid && i < count; i += 1)
    {
        char* argument = arguments[i];
        const bool option = !operands && argument[0] == '-' && argument[1] != 0;
        const bool valued = option && (pp_option_takes_value(argument) || strcmp(argument, "--lanes") == 0 ||
                            strcmp(argument, "--worker-budget") == 0 || strcmp(argument, "--id-order") == 0 ||
                            strcmp(argument, "-l") == 0 || strcmp(argument, "-L") == 0);
        const char* value = valued && i + 1 < count ? arguments[i + 1] : NULL;
        if (valued && value == NULL)
        {
            fprintf(stderr, "error: missing argument to '%s'\n", argument);
            valid = false;
        }
        else if (option && strcmp(argument, "--") == 0)
        {
            operands = true;
        }
        else if (option && (strcmp(argument, "-x") == 0 || (argument[1] == 'x' && argument[2] != 0)))
        {
            const char* spelling = valued ? value : argument + 2;
            language = source_driver_language(spelling);
            valid = language != SOURCE_DRIVER_INVALID;
            if (!valid)
            {
                fprintf(stderr, "error: unsupported language '%s'\n", spelling);
            }
        }
        else if (option && (strcmp(argument, "-o") == 0 || (argument[1] == 'o' && argument[2] != 0)))
        {
            driver->output = valued ? value : argument + 2;
        }
        else if (option && (argument[1] == 'l' || argument[1] == 'L'))
        {
            driver->linking[driver->linking_count++] = argument;
            driver->libraries[driver->library_count++] = argument;
            if (valued)
            {
                driver->linking[driver->linking_count++] = arguments[i + 1];
                driver->libraries[driver->library_count++] = arguments[i + 1];
            }
        }
        else if (option && (strncmp(argument, "-Wl,", 4) == 0 || strcmp(argument, "-Xlinker") == 0))
        {
            // The preprocessor accepts other -W options, but cannot consume a linker request on its behalf.
            fprintf(stderr, "error: unsupported linker option '%s'\n", argument);
            valid = false;
        }
        else if (option && strcmp(argument, "-c") == 0)
        {
            driver->compile = true;
        }
        else if (option && (strcmp(argument, "--intel") == 0 || strcmp(argument, "--timing") == 0))
        {
            driver->intel |= strcmp(argument, "--intel") == 0;
            driver->timing |= strcmp(argument, "--timing") == 0;
        }
        else if (option && strncmp(argument, "--isa=", 6) == 0)
        {
            char* target = strcmp(argument + 6, "x86-64") == 0 ? "-march=x86-64" :
                           strcmp(argument + 6, "avx2") == 0 ? "-march=x86-64-v3" :
                           strcmp(argument + 6, "avx512f") == 0 ? "-march=x86-64-v4" : NULL;
            valid = target != NULL;
            if (valid)
            {
                driver->preprocessing[driver->preprocessing_count++] = target;
            }
            else
            {
                fprintf(stderr, "error: unsupported assembly ISA '%s'\n", argument + 6);
            }
        }
        else if (option)
        {
            driver->scalar |= strcmp(argument, "--scalar") == 0;
            driver->checking_only |= strcmp(argument, "-fsyntax-only") == 0 || strcmp(argument, "--dump-typed") == 0 || strcmp(argument, "--dump-static") == 0;
            const bool checker = source_driver_checker_option(argument) || strcmp(argument, "-fsyntax-only") == 0;
            driver->checking[driver->checking_count++] = argument;
            if (!checker)
            {
                driver->preprocessing[driver->preprocessing_count++] = argument;
            }
            if (valued)
            {
                driver->checking[driver->checking_count++] = arguments[i + 1];
                if (!checker)
                {
                    driver->preprocessing[driver->preprocessing_count++] = arguments[i + 1];
                }
            }
            if (strcmp(argument, "--lanes") == 0 || strncmp(argument, "--lanes=", 8) == 0)
            {
                driver->lanes = valued ? value : argument + 8;
            }
        }
        else
        {
            // Prefix names following -- so downstream option parsers cannot reinterpret them. Every input gets its
            // own name allocation: pointer identity attaches its produced bytes to precisely that foreign argument,
            // even when the same path occurs twice under different -x modes.
            const uint64_t length = strlen(argument);
            const bool dash = argument[0] == '-';
            char* path = arena_push(arena, length + (dash ? 3 : 1), 1);
            valid = path != NULL;
            if (valid)
            {
                if (dash)
                {
                    memcpy(path, "./", 2);
                }
                memcpy(path + (dash ? 2 : 0), argument, length + 1);
                const SourceDriverKind kind = language != SOURCE_DRIVER_AUTO ? language : source_driver_suffix(path);
                const bool c = kind == SOURCE_DRIVER_C || kind == SOURCE_DRIVER_PREPROCESSED;
                driver->inputs[driver->input_count++] = (SourceDriverInput){ .path = path, .kind = kind, .source = driver->source_count };
                if (c)
                {
                    driver->sources[driver->source_count++] = (CheckDriverSource){ .preprocessed = kind == SOURCE_DRIVER_PREPROCESSED };
                }
                driver->assembly_count += kind == SOURCE_DRIVER_ASSEMBLY || kind == SOURCE_DRIVER_ASSEMBLY_CPP;
                driver->linking[driver->linking_count++] = path;
            }
        }
        i += valued ? 1 : 0;
    }
    if (valid && driver->input_count == 0)
    {
        fprintf(stderr, "error: no input files\n");
        valid = false;
    }
    return valid;
}

// Assembly borrows the command's parsed preprocessor options. Its output arena owns source text, assembly records
// and the object; releasing it is the command's responsibility after the foreign link (or after writing -c's file).
static bool source_driver_assemble(SourceDriverInput* input, const SourceDriver* driver, PPOptions options)
{
    Arena* arena = &input->assembly.arena;
    *arena = arena_reserve(UINT64_C(1) << 34);
    String8 source = { 0 };
    const bool preprocessed = input->kind == SOURCE_DRIVER_ASSEMBLY_CPP;
    const uint64_t begin = driver->timing ? asm_clock() : 0;
    X86MatchIndex* index = BUSTER_ARENA_PUSH_ARRAY(arena, X86MatchIndex, 1);
    bool valid = index != NULL && x86_encoder_initialize() && x86_semantics_initialize() &&
                 (preprocessed ? asm_preprocess_file(input->path, options, arena, &source) : asm_read_file(input->path, arena, &source));
    if (!valid)
    {
        fprintf(stderr, "%s: unable to initialize or read assembly input (maximum 64 MiB)\n", input->path);
    }
    else
    {
        x86_match_initialize(index, &options.target);
        const AsmOptions assembly_options = { .vectorized = !driver->scalar, .intel = driver->intel };
        AsmResult assembled = asm_assemble(source, index, assembly_options, arena);
        valid = assembled.error == NULL;
        if (!valid)
        {
            asm_diagnostic(input->path, source, assembled, preprocessed);
        }
        else
        {
            const Object object = asm_object(&assembled, foreign_string(input->path), arena);
            valid = object.error == OBJECT_ERROR_NONE;
            if (valid)
            {
                input->assembly.bytes = (String8){ .pointer = (char*)object.bytes, .length = object.size };
            }
            else
            {
                fprintf(stderr, "%s: unable to build ELF object\n", input->path);
            }
        }
        if (driver->timing)
        {
            fprintf(stderr, "assembly: instructions=%u total_ns=%llu\n", assembled.instructions, (unsigned long long)(asm_clock() - begin));
        }
    }
    return valid;
}

// Add runtime paths, not imported runtime bytes: foreign resolution owns reading and resolving them, just as for
// user inputs. Search uses the same GCC/glibc discovery as the direct C link; no external driver is invoked.
static int source_driver_link(const SourceDriver* driver, Arena* arena)
{
    Runtime runtime;
    bool valid = runtime_begin(&runtime) && runtime_search(&runtime) == RUNTIME_ERROR_NONE;
    char** arguments = BUSTER_ARENA_PUSH_ARRAY(arena, char*, (uint64_t)driver->linking_count + 32);
    valid = valid && arguments != NULL;
    uint32_t count = 0;
    int result = 1;
    if (!valid)
    {
        fprintf(stderr, "error: can't discover the C runtime%s%s\n", runtime.failed_file[0] != 0 ? ": " : "", runtime.failed_file);
    }
    else
    {
        arguments[count++] = "-pie";
        arguments[count++] = "-o";
        arguments[count++] = (char*)(driver->output != NULL ? driver->output : "a.out");
        if (driver->scalar)
        {
            arguments[count++] = "--scalar";
        }
        if (driver->lanes != NULL)
        {
            arguments[count++] = "--lanes";
            arguments[count++] = (char*)driver->lanes;
        }
        for (uint32_t i = 0; i < RUNTIME_OBJECT_END; i += 1)
        {
            arguments[count++] = runtime.object_paths[i].pointer;
        }
        memcpy(arguments + count, driver->linking, (uint64_t)driver->linking_count * sizeof(*arguments));
        count += driver->linking_count;
        // Match the runtime's driver order: libgcc, as-needed libgcc_s, libc, then libgcc/libgcc_s once more.
        for (uint32_t pass = 0; pass < 2; pass += 1)
        {
            arguments[count++] = runtime.archives[0].path.pointer;
            if (runtime.gcc_s_path.length != 0)
            {
                arguments[count++] = "--push-state";
                arguments[count++] = "--as-needed";
                arguments[count++] = runtime.gcc_s_path.pointer;
                arguments[count++] = "--pop-state";
            }
            if (pass == 0)
            {
                arguments[count++] = runtime.libc_script.pointer;
            }
        }
        arguments[count++] = runtime.object_paths[RUNTIME_OBJECT_END].pointer;
        arguments[count++] = runtime.object_paths[RUNTIME_OBJECT_INIT_EPILOGUE].pointer;
        ForeignDriverRun run = { .begin = foreign_driver_now(), .result = 1 };
        run.arena = arena_reserve(UINT64_C(1) << 36);
        run.names = arena_reserve(UINT64_C(1) << 34);
        run.succeeded = run.arena.base != NULL && run.names.base != NULL && foreign_driver_arguments(&run.driver, (int)count, arguments, &run.arena);
        // The command's argument parser borrows each path unchanged. Walk in order, rather than matching names:
        // duplicated inputs and different languages for the same path must retain separate output ownership.
        uint32_t input = 0;
        const uint32_t input_count = driver->input_count;
        for (uint32_t a = 0; run.succeeded && input < input_count && a < run.driver.command.argument_count; a += 1)
        {
            ForeignArgument* argument = run.driver.command.arguments + a;
            const SourceDriverInput* source = driver->inputs + input;
            if (argument->name.pointer == source->path)
            {
                argument->bytes = source->kind == SOURCE_DRIVER_C || source->kind == SOURCE_DRIVER_PREPROCESSED ? driver->sources[source->source].bytes :
                                  source->assembly.bytes;
                input += 1;
            }
        }
        if (run.succeeded && input != input_count)
        {
            fprintf(stderr, "internal compiler error: mixed link input order was lost\n");
            run.succeeded = false;
        }
        result = run_prepared_foreign_link(&run, true);
    }
    runtime_release(&runtime);
    return result;
}

static int run_source_driver(int count, char** arguments)
{
    Arena arena = arena_reserve(UINT64_C(1) << 32);
    SourceDriver driver;
    bool valid = source_driver_arguments(&driver, count, arguments, &arena);
    const uint32_t input_count = valid ? driver.input_count : 0;
    const uint32_t c_count = valid ? driver.source_count : 0;
    const uint32_t produced = c_count + (valid ? driver.assembly_count : 0);
    const bool mixed = valid && c_count != input_count;
    const bool link = valid && !driver.compile && !driver.checking_only;
    int result = valid ? 0 : 1;
    if (valid && driver.compile && !driver.checking_only && driver.output != NULL && produced > 1)
    {
        fprintf(stderr, "error: cannot specify -o when generating multiple output files\n");
        valid = false;
        result = 1;
    }
    // Check mixed -c's output names together, before either frontend can overwrite another input's output.
    const char** outputs = valid ? BUSTER_ARENA_PUSH_ARRAY(&arena, const char*, (uint64_t)input_count + 1) : NULL;
    if (valid && outputs == NULL)
    {
        fprintf(stderr, "fatal error: can't reserve source driver output names\n");
        valid = false;
        result = 1;
    }
    for (uint32_t i = 0; valid && i < input_count; i += 1)
    {
        outputs[i] = driver.inputs[i].kind != SOURCE_DRIVER_LINK ? check_driver_object_name(&arena, driver.inputs[i].path) : NULL;
        if (driver.inputs[i].kind != SOURCE_DRIVER_LINK && outputs[i] == NULL)
        {
            valid = false;
            result = 1;
        }
        for (uint32_t j = 0; valid && mixed && driver.compile && !driver.checking_only && j < i; j += 1)
        {
            if (outputs[i] != NULL && outputs[j] != NULL && strcmp(outputs[i], outputs[j]) == 0)
            {
                fprintf(stderr, "error: multiple inputs would write object file %s\n", outputs[i]);
                valid = false;
                result = 1;
            }
        }
    }
    PPOptions preprocessing = { 0 };
    if (valid && (driver.assembly_count != 0 || c_count == 0) && !driver.checking_only)
    {
        const char* source = NULL;
        const char* output = NULL;
        bool markers = false;
        driver.preprocessing[driver.preprocessing_count++] = "assembly.s";
        valid = pp_parse_options((int)driver.preprocessing_count, driver.preprocessing, &preprocessing, &arena, &source, &output, &markers);
        if (!valid)
        {
            fprintf(stderr, "error: unsupported source driver options\n");
            result = 1;
        }
    }
    if (valid && c_count != 0)
    {
        if (driver.compile || (mixed && link))
        {
            driver.checking[driver.checking_count++] = "-c";
        }
        if (driver.output != NULL && (!mixed || !link))
        {
            driver.checking[driver.checking_count++] = "-o";
            driver.checking[driver.checking_count++] = (char*)driver.output;
        }
        if (!mixed)
        {
            memcpy(driver.checking + driver.checking_count, driver.libraries, (uint64_t)driver.library_count * sizeof(char*));
            driver.checking_count += driver.library_count;
        }
        for (uint32_t i = 0; i < input_count; i += 1)
        {
            const SourceDriverInput* input = driver.inputs + i;
            if (input->kind == SOURCE_DRIVER_C || input->kind == SOURCE_DRIVER_PREPROCESSED)
            {
                driver.checking[driver.checking_count++] = input->path;
                driver.sources[input->source].capture = mixed && link;
            }
        }
        // Reusable mode joins the C workers and frees their state before the foreign link starts another session.
        result = run_check_command_sources((int)driver.checking_count, driver.checking, true, mixed, driver.sources);
    }
    for (uint32_t i = 0; valid && i < input_count; i += 1)
    {
        SourceDriverInput* input = driver.inputs + i;
        const bool assembly = input->kind == SOURCE_DRIVER_ASSEMBLY || input->kind == SOURCE_DRIVER_ASSEMBLY_CPP;
        if (assembly && !driver.checking_only)
        {
            bool assembled = source_driver_assemble(input, &driver, preprocessing);
            if (assembled && driver.compile)
            {
                const char* output = driver.output != NULL ? driver.output : outputs[i];
                assembled = object_write_file(output, (const uint8_t*)input->assembly.bytes.pointer, input->assembly.bytes.length) == OBJECT_ERROR_NONE;
                if (!assembled)
                {
                    fprintf(stderr, "error: could not write object file %s\n", output);
                }
            }
            result = assembled ? result : 1;
        }
        else if ((input->kind == SOURCE_DRIVER_LINK && !link) || (assembly && driver.checking_only))
        {
            fprintf(stderr, "warning: %s: '%s' input unused because linking is not done\n", input->path, assembly ? "assembler" : "linker");
        }
    }
    if (valid && mixed && link && result == 0)
    {
        result = source_driver_link(&driver, &arena);
    }
    for (uint32_t i = 0; i < c_count; i += 1)
    {
        arena_release(&driver.sources[i].arena);
    }
    for (uint32_t i = 0; i < input_count; i += 1)
    {
        arena_release(&driver.inputs[i].assembly.arena);
    }
    arena_release(&arena);
    return result;
}
