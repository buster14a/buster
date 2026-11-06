// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Input classification is positional: -x applies only to following inputs, until -x none. Only C inputs enter the
// checker. Known unsupported source suffixes are refused; unknown suffixes remain linker inputs, never speculative C,
// and the foreign reader diagnoses their format.

typedef enum CheckDriverInputKind
{
    CHECK_DRIVER_INPUT_AUTO,
    CHECK_DRIVER_INPUT_C,
    CHECK_DRIVER_INPUT_ASSEMBLY,
    CHECK_DRIVER_INPUT_ASSEMBLY_CPP,
    CHECK_DRIVER_INPUT_LINK,
    CHECK_DRIVER_INPUT_INVALID,
} CheckDriverInputKind;

typedef struct CheckDriverInput CheckDriverInput;
struct CheckDriverInput
{
    CheckDriverInputKind kind;
    uint32_t argument;
    bool unknown;
};

static CheckDriverInputKind check_driver_language(const char* name)
{
    CheckDriverInputKind result = CHECK_DRIVER_INPUT_INVALID;
    if (strcmp(name, "none") == 0)
    {
        result = CHECK_DRIVER_INPUT_AUTO;
    }
    else if (strcmp(name, "c") == 0)
    {
        result = CHECK_DRIVER_INPUT_C;
    }
    else if (strcmp(name, "assembler") == 0)
    {
        result = CHECK_DRIVER_INPUT_ASSEMBLY;
    }
    else if (strcmp(name, "assembler-with-cpp") == 0)
    {
        result = CHECK_DRIVER_INPUT_ASSEMBLY_CPP;
    }
    return result;
}

static CheckDriverInput check_driver_classify(const char* path, CheckDriverInputKind language)
{
    CheckDriverInput result = { .kind = language };
    if (language == CHECK_DRIVER_INPUT_AUTO)
    {
        const char* slash = strrchr(path, '/');
        const char* name = slash != NULL ? slash + 1 : path;
        const char* suffix = strrchr(name, '.');
        result.kind = CHECK_DRIVER_INPUT_LINK;
        if (suffix != NULL && (strcmp(suffix, ".c") == 0 || strcmp(suffix, ".h") == 0 || strcmp(suffix, ".i") == 0))
        {
            result.kind = CHECK_DRIVER_INPUT_C;
        }
        else if (suffix != NULL && (strcmp(suffix, ".s") == 0 || strcmp(suffix, ".S") == 0))
        {
            result.kind = strcmp(suffix, ".S") == 0 ? CHECK_DRIVER_INPUT_ASSEMBLY_CPP : CHECK_DRIVER_INPUT_ASSEMBLY;
        }
        else
        {
            const char* shared = strstr(name, ".so.");
            result.unknown = !(suffix != NULL && (strcmp(suffix, ".o") == 0 || strcmp(suffix, ".a") == 0 || strcmp(suffix, ".so") == 0)) &&
                             !(shared != NULL && shared[4] != 0);
            // A known source suffix takes precedence over the versioned shared-library heuristic.
            if (suffix != NULL)
            {
                static const char* const unsupported[] = {
                    ".C",
                    ".H",
                    ".M",
                    ".adb",
                    ".ads",
                    ".asm",
                    ".bc",
                    ".c++",
                    ".cc",
                    ".cpp",
                    ".cu",
                    ".cxx",
                    ".h++",
                    ".hh",
                    ".hip",
                    ".hpp",
                    ".hxx",
                    ".ii",
                    ".iih",
                    ".ll",
                    ".m",
                    ".mii",
                    ".mm",
                    ".sycl",
                };
                for (uint32_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i += 1)
                {
                    if (strcmp(suffix, unsupported[i]) == 0)
                    {
                        result.kind = CHECK_DRIVER_INPUT_INVALID;
                        result.unknown = false;
                    }
                }
            }
        }
    }
    return result;
}
