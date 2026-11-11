// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Side-effect-free CC information probes. These use Buster's runtime search and the process PATH; they neither
// invoke another compiler nor create/import a runtime snapshot. 0.0.0 identifies the unversioned development driver,
// not the Clang version whose predefined-macro compatibility tables Buster uses.
#define BUSTER_DRIVER_VERSION "0.0.0"

static bool driver_information_program(const char* name)
{
    bool found = false;
    bool result = true;
    const char* path = getenv("PATH");
    const char* first = path != NULL ? path : "";
    if (strchr(name, '/') != NULL)
    {
        result = printf("%s\n", name) >= 0;
        found = true;
    }
    while (!found && path != NULL)
    {
        const char* colon = strchr(first, ':');
        const size_t length = colon != NULL ? (size_t)(colon - first) : strlen(first);
        const size_t named = strlen(name);
        char* candidate = length <= SIZE_MAX - named - 3 ? malloc(length + named + 3) : NULL;
        if (candidate != NULL)
        {
            const size_t directory = length != 0 ? length : 1;
            memcpy(candidate, length != 0 ? first : ".", directory);
            candidate[directory] = '/';
            memcpy(candidate + directory + 1, name, named + 1);
            struct stat status;
            found = stat(candidate, &status) == 0 && S_ISREG(status.st_mode) && access(candidate, X_OK) == 0;
            if (found)
            {
                result = printf("%s\n", candidate) >= 0;
            }
            free(candidate);
        }
        else
        {
            result = false;
            break;
        }
        path = colon;
        first = colon != NULL ? colon + 1 : first;
    }
    if (result && !found)
    {
        result = printf("%s\n", name) >= 0;
    }
    return result;
}

static bool driver_information_files(const char* name, int count, char** arguments, bool directories)
{
    Runtime runtime = { 0 };
    bool result = runtime_begin(&runtime);
    if (result)
    {
        const bool gcc = runtime_find_gcc(&runtime, runtime_gcc_bases, BUSTER_ARRAY_LENGTH(runtime_gcc_bases));
        if (directories)
        {
            char executable[PATH_MAX];
            const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
            if (length > 0)
            {
                executable[length] = 0;
                char* slash = strrchr(executable, '/');
                if (slash != NULL)
                {
                    slash[1] = 0;
                }
            }
            const char* path = getenv("PATH");
            result = printf("install: %s\nprograms: =%s\nlibraries: =", length > 0 ? executable : "", path != NULL ? path : "") >= 0;
            if (gcc)
            {
                result = printf("%.*s:", (int)runtime.gcc_directory.length, runtime.gcc_directory.pointer) >= 0 && result;
            }
            for (uint32_t d = 0; d < BUSTER_ARRAY_LENGTH(runtime_library_directories); d += 1)
            {
                result = printf("%s%s", d != 0 ? ":" : "", runtime_library_directories[d]) >= 0 && result;
            }
            result = putchar('\n') != EOF && result;
        }
        // An empty -print-file-name= is the empty spelling. Probing "directory/" would be a directory, and a
        // directory is never a file this lookup may print.
        if (result && name != NULL && name[0] == 0)
        {
            result = printf("\n") >= 0;
        }
        else if (result && name != NULL)
        {
            String8 file = { 0 };
            struct stat status;
            if (strchr(name, '/') != NULL && stat(name, &status) == 0 && S_ISREG(status.st_mode))
            {
                file = runtime_copy_path(&runtime, name);
            }
            for (int i = 0; file.length == 0 && i < count; i += 1)
            {
                const char* argument = arguments[i];
                if (strcmp(argument, "--") == 0)
                {
                    break;
                }
                if (pp_option_takes_value(argument) && i + 1 < count)
                {
                    i += 1;
                }
                else if (strncmp(argument, "-L", 2) == 0)
                {
                    const char* directory = argument[2] != 0 ? argument + 2 : i + 1 < count ? arguments[++i] : NULL;
                    char candidate[PATH_MAX];
                    const int length = directory != NULL ? snprintf(candidate, sizeof(candidate), "%s/%s", directory, name) : -1;
                    if (length > 0 && (size_t)length < sizeof(candidate) && stat(candidate, &status) == 0 && S_ISREG(status.st_mode))
                    {
                        file = runtime_copy_path(&runtime, candidate);
                    }
                }
            }
            if (file.length == 0 && gcc)
            {
                file = runtime_find_file(&runtime, name, true);
            }
            if (file.length == 0)
            {
                file = runtime_find_file(&runtime, name, false);
            }
            if (file.length != 0)
            {
                struct stat found_status;
                const bool regular = file.pointer != NULL && stat(file.pointer, &found_status) == 0 && S_ISREG(found_status.st_mode);
                file = regular ? file : (String8){ 0 };
            }
            result = printf("%.*s\n", (int)(file.length != 0 ? file.length : strlen(name)), file.length != 0 ? file.pointer : name) >= 0;
        }
    }
    if (!result)
    {
        fprintf(stderr, "error: can't produce driver search information\n");
    }
    runtime_release(&runtime);
    return result;
}

// Information actions may follow other options; an operand spelling an action is never interpreted again. A null
// status tests for an action so implicit assembly dispatch leaves it to the driver. A bare -v prints to stderr only.
// noinline: its search paths' buffers would take 23 KB of its caller's frame, which a compile runs below (#652).
static __attribute__((noinline)) bool main_information(int count, char** arguments, int* status)
{
    const char* target = NULL;
    const char* file = NULL;
    const char* program = NULL;
    bool missing_target = false;
    bool version = false;
    bool machine = false;
    bool numeric = false;
    bool directories = false;
    bool verbose = false;
    bool input = false;
    for (int i = 0; i < count; i += 1)
    {
        const char* argument = arguments[i];
        // The argument's option IDs (c_option_table.c).
        const COptionMatch match = c_option_find(argument);
        const COption option = match.exact;
        if (option == C_OPTION_END_OF_OPTIONS)
        {
            input |= i + 1 < count;
            break;
        }
        if (option == C_OPTION_TARGET)
        {
            missing_target = i + 1 >= count;
            target = !missing_target ? arguments[++i] : NULL;
        }
        else if (match.joined == C_OPTION_TARGET_EQ)
        {
            target = argument + 9;
            missing_target = false;
        }
        else if (option == C_OPTION_VERSION)
        {
            version = true;
        }
        else if (option == C_OPTION_DUMPMACHINE)
        {
            machine = true;
        }
        else if (option == C_OPTION_DUMPVERSION || option == C_OPTION_DUMPFULLVERSION)
        {
            numeric = true;
        }
        else if (option == C_OPTION_PRINT_SEARCH_DIRS)
        {
            directories = true;
        }
        else if (match.joined == C_OPTION_PRINT_FILE_NAME_EQ)
        {
            file = argument + 17;
        }
        else if (match.joined == C_OPTION_PRINT_PROG_NAME_EQ)
        {
            program = argument + 17;
        }
        else if (option == C_OPTION_V)
        {
            verbose = true;
        }
        else if ((pp_option_id_takes_value(option) || option == C_OPTION_LANES || option == C_OPTION_LANE_CPUS || option == C_OPTION_WORKER_BUDGET ||
                  option == C_OPTION_ID_ORDER || option == C_OPTION_L_LIBRARY || option == C_OPTION_L_DIRECTORY || option == C_OPTION_XLINKER) &&
                 i + 1 < count)
        {
            i += 1;
        }
        else
        {
            input |= argument[0] != '-' || option == C_OPTION_STANDARD_INPUT;
        }
    }
    const bool information = version || machine || numeric || directories || file != NULL || program != NULL;
    const bool found = information || (verbose && !input);
    if (found && status != NULL)
    {
        *status = 1;
        if (missing_target || (target != NULL && !predefines_target_valid(target)))
        {
            fprintf(stderr, "error: unsupported or missing target for driver information\n");
        }
        else
        {
            const PredefinesOptions options = { .target = target };
            const size_t length = strlen(target != NULL ? target : BUSTER_CLANG_DEFAULT_TARGET);
            char* triple = length <= SIZE_MAX - 32 ? malloc(length + 32) : NULL;
            if (triple != NULL)
            {
                predefines_target_triple(&options, triple, length + 32);
                bool printed = true;
                if (verbose)
                {
                    printed = fprintf(stderr, "buster (development build)\nTarget: %s\n", triple) >= 0;
                }
                if (machine)
                {
                    printed = printf("%s\n", triple) >= 0 && printed;
                }
                else if (numeric)
                {
                    printed = puts(BUSTER_DRIVER_VERSION) >= 0 && printed;
                }
                else if (file != NULL || directories)
                {
                    printed = driver_information_files(file, count, arguments, directories) && printed;
                }
                else if (program != NULL)
                {
                    printed = driver_information_program(program) && printed;
                }
                else if (version)
                {
                    printed = printf("buster (development build)\nTarget: %s\n", triple) >= 0 && printed;
                }
                printed = fflush(stdout) == 0 && printed;
                *status = printed ? 0 : 1;
                free(triple);
            }
            else
            {
                fprintf(stderr, "error: cannot allocate target triple\n");
            }
        }
    }
    return found;
}
