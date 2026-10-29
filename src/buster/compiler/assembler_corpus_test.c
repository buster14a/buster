// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Included after assembler_test.c's helpers. Independent compilers choose the instructions, directives and CFI.
// GCC/Clang produce GNU-compatible assembly; GNU as supplies the object oracle. Both frontend paths and all three
// resulting objects must execute independently computed C results and unwind through the assembled function.

static bool asm_test_corpus_program(const char* object, char* executable, const char* output, const char* errors)
{
    char caller[PATH_MAX];
    const int length = snprintf(caller, sizeof(caller), "%s-caller.o", executable);
    const bool bounded = length > 0 && (size_t)length < sizeof(caller);
    char* compile[] = {"clang", "-O2", "-fno-pie", "-c", "examples/assembler/compiler-caller.c", "-o", caller, 0};
    char* link[] = {"clang", "-O2", "-fno-pie", "-no-pie", caller, (char*)object, "-o", executable, 0};
    char* run[] = {executable, 0};
    if (bounded)
    {
        unlink(caller);
    }
    const bool result = bounded && asm_test_tool(compile, caller, output, errors) == 0 && object_test_run(link, output, errors) == 0 && object_test_run(run, output, errors) == 0;
    if (bounded)
    {
        unlink(caller);
    }
    return result;
}

static void asm_test_compiler_corpus(AsmTest* test, const char* directory, bool keep)
{
    static const char* const compilers[] = {"gcc", "clang"};
    static const char* const optimizations[] = {"-O0", "-O2"};
    static const char* const syntaxes[] = {"-masm=att", "-masm=intel"};
    Arena files = arena_reserve(1ull << 28);
    char assembly[PATH_MAX];
    char reference_path[PATH_MAX];
    char object_path[PATH_MAX];
    char scalar_path[PATH_MAX];
    char executable[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(assembly, sizeof(assembly), "%s/compiler.s", directory);
    snprintf(reference_path, sizeof(reference_path), "%s/compiler-reference.o", directory);
    snprintf(object_path, sizeof(object_path), "%s/compiler-vector.o", directory);
    snprintf(scalar_path, sizeof(scalar_path), "%s/compiler-scalar.o", directory);
    snprintf(executable, sizeof(executable), "%s/compiler-program", directory);
    snprintf(output, sizeof(output), "%s/compiler-output", directory);
    snprintf(errors, sizeof(errors), "%s/compiler-errors", directory);
    for (uint32_t compiler = 0; compiler < 2; compiler += 1)
    {
        snprintf(output, sizeof(output), "%s/compiler-version", directory);
        snprintf(errors, sizeof(errors), "%s/compiler-version-errors", directory);
        char* version[] = {(char*)compilers[compiler], "--version", 0};
        bool available = object_test_run(version, output, errors) == 0;
        const String8 description = available ? object_test_read_file(&files, output) : (String8){0};
        uint32_t line = 0;
        while (line < description.length && description.pointer[line] != '\n')
        {
            line += 1;
        }
        printf("assembler compiler corpus: %.*s\n", (int)line, description.pointer != 0 ? description.pointer : "");
        asm_test_check(test, available, "compiler corpus tool available", compilers[compiler]);
        unlink(output);
        unlink(errors);
        for (uint32_t optimization = 0; available && optimization < 2; optimization += 1)
        {
            for (uint32_t syntax = 0; syntax < 2; syntax += 1)
            {
                for (uint32_t sections = 0; sections < 2; sections += 1)
                {
                    files.length = 0;
                    char name[128];
                    snprintf(name, sizeof(name), "%s %s %s %s", compilers[compiler], optimizations[optimization], syntaxes[syntax], sections ? "separate sections" : "ordinary sections");
                    char stem[PATH_MAX];
                    snprintf(stem, sizeof(stem), "%s/%s-O%u-%s-%s", directory, compilers[compiler], optimization * 2, syntax ? "intel" : "att", sections ? "separate" : "ordinary");
                    const size_t length = strlen(stem);
                    bool paths_fit = length + 24 < sizeof(assembly);
                    const char* const suffixes[] = {".s", "-reference.o", "-vector.o", "-scalar.o", "-program", "-output", "-errors"};
                    char* const destinations[] = {assembly, reference_path, object_path, scalar_path, executable, output, errors};
                    for (uint32_t path = 0; paths_fit && path < BUSTER_ARRAY_LENGTH(suffixes); path += 1)
                    {
                        memcpy(destinations[path], stem, length);
                        strcpy(destinations[path] + length, suffixes[path]);
                    }
                    char* generate[] =
                    {
                        (char*)compilers[compiler],
                        (char*)optimizations[optimization],
                        (char*)syntaxes[syntax],
                        "-march=x86-64",
                        "-fcf-protection=none",
                        "-fno-pic",
                        "-fno-pie",
                        sections ? "-ffunction-sections" : "-fno-function-sections",
                        sections ? "-fdata-sections" : "-fno-data-sections",
                        "-S",
                        "examples/assembler/compiler-corpus.c",
                        "-o",
                        assembly,
                        compiler == 1 ? "-fno-integrated-as" : 0,
                        0,
                    };
                    bool okay = paths_fit && asm_test_tool(generate, assembly, output, errors) == 0;
                    const String8 source = okay ? object_test_read_file(&files, assembly) : (String8){0};
                    okay = okay && source.pointer != 0 && source.length != 0;
                    asm_test_check(test, okay, name, "generate compiler assembly");
                    if (okay)
                    {
                        char* gas[] = {"as", "--64", "-o", reference_path, assembly, 0};
                        char* vector[] = {"/proc/self/exe", "assemble", "-o", object_path, assembly, 0};
                        char* scalar[] = {"/proc/self/exe", "assemble", "--scalar", "-o", scalar_path, assembly, 0};
                        // Validate the fixture's expected results independently even when Buster rejects the source.
                        okay = oracle_epoch_file(assembly) && asm_test_tool(gas, reference_path, output, errors) == 0 && asm_test_corpus_program(reference_path, executable, output, errors);
                        asm_test_check(test, okay, name, "GNU object execution/unwind");
                        const AsmResult result = asm_test_pair(test, source, 1, X86_ARCH_BASELINE);
                        okay = okay && result.error == 0;
                        okay = okay && object_test_run(vector, output, errors) == 0 && object_test_run(scalar, output, errors) == 0;
                        asm_test_check(test, okay, name, "GNU as and both production assembly commands");
                        if (okay)
                        {
                            const String8 reference = object_test_read_file(&files, reference_path);
                            const String8 vector_object = object_test_read_file(&files, object_path);
                            const String8 scalar_object = object_test_read_file(&files, scalar_path);
                            okay = vector_object.length != 0 && vector_object.length == scalar_object.length && memcmp(vector_object.pointer, scalar_object.pointer, vector_object.length) == 0;
                            ObjectView ours;
                            ObjectView theirs;
                            char difference[256] = {0};
                            okay = okay && object_view_open(&ours, (const uint8_t*)vector_object.pointer, vector_object.length, false, difference, sizeof(difference));
                            okay = okay && asm_test_gnu_object(&theirs, reference, &files, difference, sizeof(difference));
                            okay = okay && object_test_same_as(&ours, &theirs, difference, sizeof(difference));
                            asm_test_check(test, okay, name, difference);
                            const char* const objects[] = {object_path, scalar_path};
                            for (uint32_t implementation = 0; implementation < 2; implementation += 1)
                            {
                                const bool executed = asm_test_corpus_program(objects[implementation], executable, output, errors);
                                asm_test_check(test, executed, name, implementation == 0 ? "vector object execution/unwind" : "scalar object execution/unwind");
                                okay = okay && executed;
                            }
                        }
                    }
                    if (!okay)
                    {
                        const String8 diagnostic = object_test_read_file(&files, errors);
                        fprintf(stderr, "assembler corpus %s: %.*s\n", name, (int)diagnostic.length, diagnostic.pointer != 0 ? diagnostic.pointer : "");
                    }
                    else
                    {
                        printf("assembler compiler case: %s: objects, execution and unwind passed\n", name);
                    }
                    if (okay && !keep)
                    {
                        for (uint32_t path = 0; path < BUSTER_ARRAY_LENGTH(destinations); path += 1)
                        {
                            unlink(destinations[path]);
                        }
                    }
                    else
                    {
                        printf("assembler compiler artifacts: %s\n", stem);
                    }
                }
            }
        }
    }
    arena_release(&files);
}
