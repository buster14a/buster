// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent fixtures, GNU as object comparison, executable behavior, and scalar/vector malformed-input tests.

typedef struct AsmTest AsmTest;
struct AsmTest
{
    X86MatchIndex* index;
    X86Architecture architecture;
    Arena scalar;
    Arena vector;
    uint64_t checks;
    uint64_t failures;
};

static void asm_test_check(AsmTest* test, bool accepted, const char* name, const char* detail)
{
    test->checks += 1;
    if (!accepted)
    {
        if (test->failures < 16)
        {
            fprintf(stderr, "assembler: %s: %s\n", name, detail != 0 ? detail : "failed");
        }
        test->failures += 1;
    }
}

static AsmResult asm_test_pair(AsmTest* test, String8 source, int expected, X86Architecture architecture)
{
    if (test->architecture != architecture)
    {
        static const char* const names[] = {"x86-64", "x86-64-v2", "x86-64-v3", "x86-64-v4", "znver5", "native"};
        X86Target target;
        const bool parsed = x86_target_parse(names[architecture], strlen(names[architecture]), &target);
        asm_test_check(test, parsed, "target configuration", 0);
        x86_match_initialize(test->index, &target);
        test->architecture = architecture;
    }
    test->scalar.length = 0;
    test->vector.length = 0;
    AsmParser lex_scalar =
        {
            .source = source,
            .tokens = BUSTER_ARENA_PUSH_ARRAY(&test->scalar, AsmToken, source.length + 4),
    };
    AsmParser lex_vector =
        {
            .source = source,
            .tokens = BUSTER_ARENA_PUSH_ARRAY(&test->vector, AsmToken, source.length + 4),
    };
    if (lex_scalar.tokens != 0 && lex_vector.tokens != 0)
    {
        asm_lex_scalar(&lex_scalar);
        asm_lex_vector(&lex_vector);
        const bool same = lex_scalar.token_count == lex_vector.token_count && memcmp(lex_scalar.tokens, lex_vector.tokens, (lex_scalar.token_count + 1) * sizeof(AsmToken)) == 0;
        asm_test_check(test, same, "scalar/vector token spans and kinds", 0);
    }
    else
    {
        asm_test_check(test, false, "token allocation", 0);
    }
    test->scalar.length = 0;
    test->vector.length = 0;
    const AsmOptions scalar_options = {0};
    const AsmOptions vector_options = {.vectorized = true};
    AsmResult scalar = asm_assemble(source, test->index, scalar_options, &test->scalar);
    AsmResult vector = asm_assemble(source, test->index, vector_options, &test->vector);
    const bool same_error = (scalar.error == 0) == (vector.error == 0) && (scalar.error == 0 || (strcmp(scalar.error, vector.error) == 0 && scalar.location == vector.location));
    asm_test_check(test, same_error, "scalar/vector acceptance and diagnostic", vector.error);
    if (expected >= 0)
    {
        asm_test_check(test, (vector.error == 0) == (expected != 0), "fixture acceptance", vector.error);
        if ((vector.error == 0) != (expected != 0))
        {
            fprintf(stderr, "source: %.*s\nlocation: %u\n", (int)source.length, source.pointer, vector.location);
        }
    }
    if (scalar.error == 0 && vector.error == 0)
    {
        const Object a = asm_object(&scalar, BUSTER_S8("fixture.s"), &test->scalar);
        const Object b = asm_object(&vector, BUSTER_S8("fixture.s"), &test->vector);
        asm_test_check(test, a.error == OBJECT_ERROR_NONE && b.error == OBJECT_ERROR_NONE && a.size == b.size && memcmp(a.bytes, b.bytes, a.size) == 0, "scalar/vector ELF", "objects differ");
    }
    return vector;
}

static const char asm_test_function[] =
    ".intel_syntax noprefix\n"
    ".text\n.globl assembly_sum\n.type assembly_sum,@function\n"
    "assembly_sum:\n"
    "push rbp\nmov rbp, rsp\n"
    "xor eax, eax\nxor ecx, ecx\njmp check\n"
    "again:\nadd rax, qword ptr [rdi+rcx*8]\ninc rcx\n"
    "check:\ncmp rcx, rsi\njb again\n"
    "add rax, 200\nmov rdi, rax\ncall external_twice\n"
    "add rax, qword ptr [rip+bias]\npop rbp\nret\n"
    ".size assembly_sum, .-assembly_sum\n"
    ".section .rodata\n.balign 8\n.type bias,@object\n"
    "bias: .quad 3\n.size bias, 8\n";

static bool asm_test_oracle(AsmTest* test, const char* source, const char* directory, X86Architecture architecture)
{
    const String8 text = {.pointer = (char*)source, .length = strlen(source)};
    AsmResult result = asm_test_pair(test, text, 1, architecture);
    bool passed = result.error == 0;
    char assembly[PATH_MAX];
    char theirs_path[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(assembly, sizeof(assembly), "%s/oracle.s", directory);
    snprintf(theirs_path, sizeof(theirs_path), "%s/oracle.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    if (passed)
    {
        // The shared writer always emits the non-executable-stack marker. Ask GNU as for the same metadata.
        const size_t capacity = strlen(source) + 64;
        char* oracle_source = arena_push(&test->scalar, capacity, 1);
        const int length = oracle_source != 0 ? snprintf(oracle_source, capacity, "%s\n.section .note.GNU-stack,\"\",@progbits\n", source) : -1;
        passed = length > 0 && object_write_file(assembly, (const uint8_t*)oracle_source, length) == OBJECT_ERROR_NONE;
        char* arguments[] = {"as", "--64", "-o", theirs_path, assembly, 0};
        passed = passed && object_test_run(arguments, output, errors) == 0;
    }
    if (passed)
    {
        const Object object = asm_object(&result, BUSTER_S8("fixture.s"), &test->vector);
        const String8 theirs_bytes = object_test_read_file(&test->vector, theirs_path);
        ObjectView ours;
        ObjectView theirs;
        char error[256] = {0};
        passed = object.error == OBJECT_ERROR_NONE && object_view_open(&ours, object.bytes, object.size, false, error, sizeof(error)) && object_view_open(&theirs, (const uint8_t*)theirs_bytes.pointer, theirs_bytes.length, true, error, sizeof(error));
        passed = passed && object_test_same_as(&ours, &theirs, error, sizeof(error));
        asm_test_check(test, passed, "GNU as sections/symbols/relocations", error);
    }
    else
    {
        asm_test_check(test, false, "GNU as fixture", result.error);
    }
    return passed;
}

static void asm_test_execution(AsmTest* test, const char* directory)
{
    AsmResult result = asm_test_pair(test, (String8){.pointer = (char*)asm_test_function, .length = sizeof(asm_test_function) - 1}, 1, 0);
    if (result.error == 0)
    {
        char object_path[PATH_MAX];
        char c_path[PATH_MAX];
        char executable[PATH_MAX];
        char output[PATH_MAX];
        char errors[PATH_MAX];
        snprintf(object_path, sizeof(object_path), "%s/function.o", directory);
        snprintf(c_path, sizeof(c_path), "%s/caller.c", directory);
        snprintf(executable, sizeof(executable), "%s/program", directory);
        snprintf(output, sizeof(output), "%s/output", directory);
        snprintf(errors, sizeof(errors), "%s/errors", directory);
        const Object object = asm_object(&result, BUSTER_S8("function.s"), &test->vector);
        static const char caller[] =
            "extern long assembly_sum(const long*, long);\n"
            "long external_twice(long value) { return value * 2; }\n"
            "int main(void) { const long values[] = {1, 2, 3, 4}; return assembly_sum(values, 4) == 423 && assembly_sum(values, 0) == 403 ? 0 : 1; }\n";
        bool okay = object.error == OBJECT_ERROR_NONE && object_write_file(object_path, object.bytes, object.size) == OBJECT_ERROR_NONE && object_write_file(c_path, (const uint8_t*)caller, sizeof(caller) - 1) == OBJECT_ERROR_NONE;
        char* compile[] = {"clang", c_path, object_path, "-o", executable, 0};
        char* run[] = {executable, 0};
        okay = okay && object_test_run(compile, output, errors) == 0 && object_test_run(run, output, errors) == 0;
        asm_test_check(test, okay, "link and execute external call/data fixture", 0);
    }
    static const char self_contained[] =
        ".intel_syntax noprefix\n.globl _start\n_start:\n"
        "mov edi, 0\nmov eax, 60\nsyscall\n";
    result = asm_test_pair(test, (String8){.pointer = (char*)self_contained, .length = sizeof(self_contained) - 1}, 1, 0);
    if (result.error == 0)
    {
        const Object object = asm_object(&result, BUSTER_S8("start.s"), &test->vector);
        const String8 file = {.pointer = (char*)object.bytes, .length = object.size};
        LinkCapacity capacity = {0};
        Link imported;
        ImportObject imported_object;
        bool okay = import_object_capacity(file, &capacity).error == IMPORT_ERROR_NONE && link_initialize(&imported, &test->scalar, &test->scalar, capacity);
        okay = okay && import_object(&imported, file, 0, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, &test->scalar, &imported_object).error == IMPORT_ERROR_NONE;
        SymbolId entry = 0;
        for (SymbolId i = 1; okay && i < imported.symbols.count; i += 1)
        {
            const NameId name = imported.symbols.name[i];
            if (imported.names.length[name] == 6 && memcmp(imported.names.bytes[name], "_start", 6) == 0)
            {
                entry = i;
            }
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/direct", directory);
        ExecutableResult built;
        okay = okay && entry != 0 && executable_test_write(&imported, entry, (String8){.pointer = path, .length = strlen(path)}, 1, &built, 0, 0);
        uint8_t output[256];
        uint64_t length = 0;
        int status = -1;
        okay = okay && executable_test_run(path, output, &length, &status) && status == 0;
        asm_test_check(test, okay, "shared importer and linker consume assembler ELF", 0);
    }
}

static void asm_test_driver(AsmTest* test, const char* directory)
{
    char input[PATH_MAX];
    char preprocessed[PATH_MAX];
    char object[PATH_MAX];
    char scalar_object[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(input, sizeof(input), "%s/driver.s", directory);
    snprintf(preprocessed, sizeof(preprocessed), "%s/driver.S", directory);
    snprintf(object, sizeof(object), "%s/driver.o", directory);
    snprintf(scalar_object, sizeof(scalar_object), "%s/scalar.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    bool okay = object_write_file(input, (const uint8_t*)asm_test_function, sizeof(asm_test_function) - 1) == OBJECT_ERROR_NONE;
    char* vector[] = { "/proc/self/exe", "assemble", "-o", object, input, 0 };
    char* scalar[] = { "/proc/self/exe", "-c", "--scalar", "-o", scalar_object, input, 0 };
    okay = okay && object_test_run(vector, output, errors) == 0 && object_test_run(scalar, output, errors) == 0;
    const uint64_t saved = test->scalar.length;
    const String8 a = object_test_read_file(&test->scalar, object);
    const String8 b = object_test_read_file(&test->scalar, scalar_object);
    okay = okay && a.length != 0 && a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
    asm_test_check(test, okay, "driver assemble/-c scalar and vector", 0);
    char* unsupported[] = {"/proc/self/exe", "assemble", "-m32", "-o", object, input, 0};
    asm_test_check(test, object_test_run(unsupported, output, errors) == 1, "unsupported output-mode flag is rejected", 0);
    static const char invalid[] = ".intel_syntax noprefix\nadd rax,0x80000000\n";
    okay = object_write_file(input, (const uint8_t*)invalid, sizeof(invalid) - 1) == OBJECT_ERROR_NONE;
    okay = okay && object_test_run(vector, output, errors) == 1;
    const String8 diagnostic = object_test_read_file(&test->scalar, errors);
    const String8 kept = object_test_read_file(&test->scalar, object);
    bool located = false;
    for (uint64_t at = 0; at + 12 <= diagnostic.length; at += 1)
    {
        located |= memcmp(diagnostic.pointer + at, ":2:1: error:", 12) == 0;
    }
    okay = okay && located;
    okay = okay && kept.length == a.length && memcmp(kept.pointer, a.pointer, a.length) == 0;
    asm_test_check(test, okay, "source diagnostic and existing output preserved on failure", 0);
    okay = object_write_file(preprocessed, (const uint8_t*)asm_test_function, sizeof(asm_test_function) - 1) == OBJECT_ERROR_NONE;
    vector[4] = preprocessed;
    okay = okay && object_test_run(vector, output, errors) == 0;
    const String8 cpp = object_test_read_file(&test->scalar, object);
    // The file symbol names the actual .S input; compare the object contents through the ELF view below instead.
    ObjectView original;
    ObjectView preprocessor;
    char difference[256] = {0};
    okay = okay && object_view_open(&original, (const uint8_t*)a.pointer, a.length, false, difference, sizeof(difference)) && object_view_open(&preprocessor, (const uint8_t*)cpp.pointer, cpp.length, false, difference, sizeof(difference));
    okay = okay && object_test_same_as(&original, &preprocessor, difference, sizeof(difference));
    asm_test_check(test, okay, ".S through shared preprocessor", difference);
    test->scalar.length = saved;
}

static void asm_test_preprocessing(AsmTest* test, const char* directory)
{
    char input[PATH_MAX];
    char header[PATH_MAX];
    char object[PATH_MAX];
    char scalar_object[PATH_MAX];
    char clang_source[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(input, sizeof(input), "%s/driver.S", directory);
    snprintf(header, sizeof(header), "%s/values.h", directory);
    snprintf(object, sizeof(object), "%s/driver.o", directory);
    snprintf(scalar_object, sizeof(scalar_object), "%s/scalar.o", directory);
    snprintf(clang_source, sizeof(clang_source), "%s/preprocessed.s", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    static const char definitions[] =
        "#define VALUE 200\n#define IMM(x) $x\n#define CAT(a,b) a ## b\n#define STR(x) #x\n"
        "#if !defined(__ASSEMBLER__) || defined(REMOVE) || OVERRIDE != 7\n#error assembly options\n#endif\n";
    static const char source[] =
        "#include <values.h>\n# ordinary assembly comment VALUE\n"
        ".text\n.globl cpp_function\n.type cpp_function,@function\ncpp_function:\n"
        "movl $VALUE, %eax # inline comment VALUE\naddl IMM(OVERRIDE), %eax\n"
        "addl $\\\nVALUE, %eax\njmp 1f\n1: ret\n.size cpp_function, .-cpp_function\n"
        ".data\nCAT(data_,label): .quad external+5\n"
        ".ascii STR(VALUE), \"$VALUE # /* still a string */\"\n"
        ".intel_syntax noprefix\n.text\nmov eax, OVERRIDE\n.att_syntax prefix\nret\n";
    bool okay = object_write_file(header, (const uint8_t*)definitions, sizeof(definitions) - 1) == OBJECT_ERROR_NONE && object_write_file(input, (const uint8_t*)source, sizeof(source) - 1) == OBJECT_ERROR_NONE;
    char* vector[] = {"/proc/self/exe", "assemble", "-I", (char*)directory, "-DOVERRIDE=7", "-DREMOVE", "-UREMOVE", "-o", object, input, 0};
    char* scalar[] = {"/proc/self/exe", "assemble", "--scalar", "-I", (char*)directory, "-DOVERRIDE=7", "-DREMOVE", "-UREMOVE", "-o", scalar_object, input, 0};
    char* clang[] = {"clang", "-E", "-P", "-x", "assembler-with-cpp", "-I", (char*)directory, "-DOVERRIDE=7", "-DREMOVE", "-UREMOVE", "-o", clang_source, input, 0};
    okay = okay && object_test_run(vector, output, errors) == 0 && object_test_run(scalar, output, errors) == 0 && object_test_run(clang, output, errors) == 0;
    Arena files = arena_reserve(1ull << 28);
    const String8 a = object_test_read_file(&files, object);
    const String8 b = object_test_read_file(&files, scalar_object);
    const String8 text = object_test_read_file(&files, clang_source);
    okay = okay && a.length != 0 && a.length == b.length && memcmp(a.pointer, b.pointer, a.length) == 0;
    asm_test_check(test, okay, ".S macros, includes, comments and scalar/vector output", 0);
    if (okay)
    {
        char* terminated = arena_push(&files, text.length + 1, 1);
        okay = terminated != 0;
        if (okay)
        {
            memcpy(terminated, text.pointer, text.length);
            terminated[text.length] = 0;
            // Independently preprocess with Clang, assemble with GNU as, then compare Buster's .S object with both.
            okay = asm_test_oracle(test, terminated, directory, 0);
            ObjectView ours;
            ObjectView oracle;
            char difference[256] = {0};
            char oracle_path[PATH_MAX];
            snprintf(oracle_path, sizeof(oracle_path), "%s/oracle.o", directory);
            const String8 oracle_bytes = object_test_read_file(&files, oracle_path);
            okay = okay && object_view_open(&ours, (const uint8_t*)a.pointer, a.length, false, difference, sizeof(difference)) && object_view_open(&oracle, (const uint8_t*)oracle_bytes.pointer, oracle_bytes.length, true, difference, sizeof(difference));
            okay = okay && object_test_same_as(&ours, &oracle, difference, sizeof(difference));
            asm_test_check(test, okay, ".S against Clang preprocessing and GNU as", difference);
        }
        asm_test_check(test, okay, ".S oracle allocation and comparison", 0);
    }
    static const char bad_header[] = "#line 73 \"assembly-source.h\"\nthis_is_not_an_instruction\n";
    static const char bad_source[] = "#include <values.h>\n";
    okay = object_write_file(header, (const uint8_t*)bad_header, sizeof(bad_header) - 1) == OBJECT_ERROR_NONE && object_write_file(input, (const uint8_t*)bad_source, sizeof(bad_source) - 1) == OBJECT_ERROR_NONE;
    okay = okay && object_test_run(vector, output, errors) == 1;
    const String8 diagnostic = object_test_read_file(&files, errors);
    const String8 kept = object_test_read_file(&files, object);
    static const char expected[] = "assembly-source.h:73:1: error:";
    bool located = false;
    for (uint64_t at = 0; at + sizeof(expected) - 1 <= diagnostic.length; at += 1)
    {
        located |= memcmp(diagnostic.pointer + at, expected, sizeof(expected) - 1) == 0;
    }
    okay = okay && located && kept.length == a.length && memcmp(kept.pointer, a.pointer, a.length) == 0;
    if (!okay)
    {
        fprintf(stderr, "preprocessed diagnostic: %.*s\n", (int)diagnostic.length, diagnostic.pointer);
    }
    asm_test_check(test, okay, ".S included #line diagnostic and existing output preserved", 0);
    arena_release(&files);
}

static uint64_t asm_test_random(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static void asm_test_targets(AsmTest* test)
{
    // Every syntax-admitted row needs explicit ISA metadata, independently of the index's own filtering.
    for (uint32_t f = 0; f < BUSTER_X86_FORM_LEGACY_MODES; f += 1)
    {
        uint8_t roles[4];
        const X86FormDescription* form = &x86_form_descriptions[f];
        if (x86_match_supported(form) && x86_match_roles(form->syntax, roles) <= 4)
        {
            asm_test_check(test, x86_target_requirements[f] != 0, "matched form has ISA metadata", form->mnemonic);
        }
    }
    const X86Target native = x86_target_native();
    for (X86Architecture architecture = X86_ARCH_BASELINE; architecture <= X86_ARCH_NATIVE; architecture += 1)
    {
        const bool avx2 = architecture == X86_ARCH_NATIVE ? (native.features & BUSTER_X86_FEATURE(AVX2)) != 0 : architecture >= X86_ARCH_V3;
        const bool avx512 = architecture == X86_ARCH_NATIVE ? (native.features & BUSTER_X86_FEATURE(AVX512F)) != 0 : architecture >= X86_ARCH_V4;
        asm_test_pair(test, BUSTER_S8(".intel_syntax noprefix\nmov rax,200\naddps xmm0,xmm1\nret\n"), 1, architecture);
        asm_test_pair(test, BUSTER_S8(".intel_syntax noprefix\nvpaddd ymm0,ymm1,ymm2\n"), avx2, architecture);
        asm_test_pair(test, BUSTER_S8(".intel_syntax noprefix\nvaddps zmm0,zmm1,zmm2\n"), avx512, architecture);
        asm_test_pair(test, BUSTER_S8(".intel_syntax noprefix\nvmovdqa32 zmm0,[rax]\nvpsubd zmm0,zmm1,zmm2\n"), avx512, architecture);
        for (uint32_t i = 0; i < test->index->begin[BUSTER_X86_MATCH_BUCKETS]; i += 1)
        {
            asm_test_check(test, x86_target_form_allowed(test->index->allowed, test->index->forms[i]), "all indexed records eligible", 0);
        }
    }
}

static void asm_test_target_driver(AsmTest* test, const char* directory)
{
    char input[PATH_MAX];
    char object[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(input, sizeof(input), "%s/target.S", directory);
    snprintf(object, sizeof(object), "%s/target.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    static const char* const targets[] = {"-march=x86-64", "-march=x86-64-v2", "-march=x86-64-v3", "-march=x86-64-v4", "-march=znver5", "-march=native"};
    static const char source[] =
        ".intel_syntax noprefix\n"
        "#if defined(__AVX512F__)\nvaddps zmm0,zmm1,zmm2\n"
        "#elif defined(__AVX2__)\nvpaddd ymm0,ymm1,ymm2\n"
        "#else\naddps xmm0,xmm1\n#endif\n";
    bool okay = object_write_file(input, (const uint8_t*)source, sizeof(source) - 1) == OBJECT_ERROR_NONE;
    for (uint32_t i = 0; okay && i < BUSTER_ARRAY_LENGTH(targets); i += 1)
    {
        char* arguments[] = {"/proc/self/exe", "assemble", (char*)targets[i], "-mtune=znver5", "-o", object, input, 0};
        asm_test_check(test, object_test_run(arguments, output, errors) == 0, ".S macros agree with instruction eligibility", targets[i]);
    }
    static const char vector_source[] = ".intel_syntax noprefix\nvpaddd ymm0,ymm1,ymm2\n";
    okay = okay && object_write_file(input, (const uint8_t*)vector_source, sizeof(vector_source) - 1) == OBJECT_ERROR_NONE;
    char* baseline[] = {"/proc/self/exe", "assemble", "-mtune=znver5", "-o", object, input, 0};
    char* v3[] = {"/proc/self/exe", "assemble", "-march=x86-64-v3", "-o", object, input, 0};
    char* order[] = {"/proc/self/exe", "assemble", "--isa=avx2", "-march=x86-64", "-o", object, input, 0};
    char* alias[] = {"/proc/self/exe", "assemble", "-march=x86-64", "--isa=avx2", "-o", object, input, 0};
    okay = okay && object_test_run(baseline, output, errors) == 1 && object_test_run(v3, output, errors) == 0;
    okay = okay && object_test_run(order, output, errors) == 1 && object_test_run(alias, output, errors) == 0;
    asm_test_check(test, okay, "driver default, independent tuning and last target option", 0);
    unlink(input);
    unlink(object);
}

static void asm_test_lex_guard(AsmTest* test)
{
    const long page = sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool okay = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_READ | PROT_WRITE) == 0;
    asm_test_check(test, okay, "guard allocation", 0);
    if (okay)
    {
        static const char text[] = ".intel_syntax noprefix\nmov rax,123\n# comment\n/* multiline\n*/ .ascii \"a\\\"b\"\n";
        for (uint32_t length = 0; length < 1100; length += 1)
        {
            for (uint32_t side = 0; side < 2; side += 1)
            {
                uint8_t* source = side == 0 ? mapping + page : mapping + 2 * page - length;
                for (uint32_t i = 0; i < length; i += 1)
                {
                    source[i] = text[i % (sizeof(text) - 1)];
                }
                asm_test_pair(test, (String8){.pointer = (char*)source, .length = length}, -1, 0);
            }
        }
        munmap(mapping, page * 3);
    }
}

static void asm_test_valid_batches(AsmTest* test, uint64_t* random)
{
    static const char* const registers[] = {"rax", "rcx", "rdx", "rbx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    static const char* const mnemonics[] = {"mov", "add", "sub", "xor", "and", "or", "cmp"};
    char storage[8192 + 64];
    for (uint32_t placement = 0; placement < 64; placement += 1)
    {
        char* source = storage + placement;
        for (uint32_t n = 0; n <= 65; n += 1)
        {
            uint32_t length = snprintf(source, 8192, ".intel_syntax noprefix\n");
            for (uint32_t row = 0; row < n; row += 1)
            {
                const char* mnemonic = mnemonics[asm_test_random(random) % BUSTER_ARRAY_LENGTH(mnemonics)];
                const char* destination = registers[asm_test_random(random) % BUSTER_ARRAY_LENGTH(registers)];
                if (asm_test_random(random) & 1)
                {
                    const char* value = registers[asm_test_random(random) % BUSTER_ARRAY_LENGTH(registers)];
                    length += snprintf(source + length, 8192 - length, "%s %s,%s\n", mnemonic, destination, value);
                }
                else
                {
                    const int value = (int)(asm_test_random(random) % 200000001) - 100000000;
                    length += snprintf(source + length, 8192 - length, "%s %s,%d\n", mnemonic, destination, value);
                }
            }
            asm_test_pair(test, (String8){.pointer = source, .length = length}, 1, 0);
        }
    }
}

static int run_assembler_tests(int count, char** arguments)
{
    uint64_t seed = 1;
    uint32_t fuzz = 2000;
    bool cross_check = false;
    bool valid = true;
    for (int i = 0; i < count; i += 1)
    {
        if (strcmp(arguments[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(arguments[i], "--seed") == 0 && i + 1 < count)
        {
            seed = strtoull(arguments[++i], 0, 10);
        }
        else if (strcmp(arguments[i], "--fuzz") == 0 && i + 1 < count)
        {
            fuzz = strtoul(arguments[++i], 0, 10);
        }
        else
        {
            valid = false;
        }
    }
    Arena arena = arena_reserve(1ull << 30);
    AsmTest test =
        {
            .index = BUSTER_ARENA_PUSH_ARRAY(&arena, X86MatchIndex, 1),
            .scalar = arena_reserve(1ull << 30),
            .vector = arena_reserve(1ull << 30),
    };
    valid = valid && test.index != 0 && x86_encoder_initialize();
    if (valid)
    {
        const X86Target baseline = {0};
        x86_match_initialize(test.index, &baseline);
        asm_test_targets(&test);
        Arena tiny = arena_reserve(4096);
        tiny.length = 16;
        char oversized[1024];
        memset(oversized, ' ', sizeof(oversized));
        const AsmResult exhausted = asm_assemble((String8){.pointer = oversized, .length = sizeof(oversized)}, test.index, (AsmOptions){.vectorized = true}, &tiny);
        asm_test_check(&test, exhausted.error != 0 && exhausted.link.atoms.count == 0 && tiny.length == 16, "arena failure rolls back the component", 0);
        arena_release(&tiny);
        static const char* const good[] =
            {
                "",
                ".intel_syntax noprefix\nret\n",
                ".intel_syntax noprefix\nmov rax, 200\nadd rax,200\nmov rax,0x80000000\nmov rax,0xffffffff\nmov rax,0xffffffffffffffff\n",
                ".intel_syntax noprefix\nmov ah, bl\nmov r8, [r12+r9*4-129]\nmov eax,dword ptr fs:[8]\n",
                ".intel_syntax noprefix\n1: inc eax\njne 1b\njmp 2f\n.zero 140\n.balign 32,0\n2: ret\n",
                ".intel_syntax noprefix\nf: jmp done\n.zero 120\njmp f\n.zero 20\n.p2align 5,0,31\ndone: ret\n",
                ".intel_syntax noprefix\n.equ amount, 200\n.set n, 3\nadd eax,amount\n.byte n\n.set n,4\n.byte n\n",
                ".text\nf: movq %rdi, %rax\naddq $200,%rax\nmovl 4(%rdi,%rcx,4),%edx\nret\n.size f,.-f\n",
                "shlq %cl,%rax\nshrdq %cl,%rbx,%rax\n",
                "pushw $200\npushq $200\nretq\n",
                ".data\n.byte ~0,~1,~0+256\n.long -1|0,~0&0xffffffff\n.quad ~0xffffffffffffffff\n",
                ".intel_syntax noprefix\n.Lasm.1.1: nop\n1: jmp 1b\n",
                ".intel_syntax noprefix\nmov eax,1\n.att_syntax prefix\naddl $2,%eax\n.intel_syntax noprefix\nret\n",
                ".data\n.local a\n.type a,@object\na: .byte 255,-128\n.word 65535\n.long ext+7\n.quad ext-8\n.size a, .-a\n.bss\n.zero 32\n",
                ".section .rodata,\"a\",@progbits\na: .ascii \"hello\",\" world\"\n.asciz \"\\101\\n\"\n",
                ".intel_syntax noprefix\naddps xmm0,xmm1\nmovdqu xmm2,xmmword ptr [rax]\n",
                ".intel_syntax noprefix\nvaddps ymm1,ymm2,ymm3\nvzeroupper\n",
                ".intel_syntax noprefix\nvaddps zmm1{k2}{z},zmm2,zmm3\nvaddps zmm1,zmm2,dword ptr [rax]{1to16}\n",
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(good); i += 1)
        {
            asm_test_pair(&test, (String8){.pointer = (char*)good[i], .length = strlen(good[i])}, 1, X86_ARCH_V4);
        }
        static const char* const bad[] =
            {
                "!",
                ".unknown\n",
                ".intel_syntax noprefix\nmov [rax], 1\n",
                ".globl 1\n",
                "retw\n",
                "callw external\n",
                "pushl $1\n",
                ".intel_syntax noprefix\ncall dword ptr [rax]\n",
                ".intel_syntax noprefix\njmp 200\n",
                ".intel_syntax noprefix\ncall 200\n",
                "jmp 200\n",
                ".set destination,200\ncall destination\n",
                ".quad x\n.equ x,1\n",
                ".local .\n",
                ".intel_syntax noprefix\nmov ah, r8b\n",
                ".intel_syntax noprefix\nmov eax, [rax+rsp*2]\n",
                ".intel_syntax noprefix\nadd rax,0x80000000\n",
                ".intel_syntax noprefix\nadd al,256\n",
                "f: nop\nf: ret\n",
                ".intel_syntax noprefix\njmp 1b\n",
                ".intel_syntax noprefix\njmp 1f\n",
                ".intel_syntax noprefix\nloop end\n.zero 130\nend: ret\n",
                ".intel_syntax noprefix\nloop external\n",
                ".quad a-b\n",
                ".byte external\n",
                ".long 4294967296\n",
                ".quad 18446744073709551616\n",
                ".quad 18446744073709551615+1\n",
                ".quad 1/0\n",
                ".bss\n.byte 1\n",
                ".zero -1\n",
                ".balign 3\n",
                ".intel_syntax noprefix\nmov rax,[rax-rcx]\n",
                ".intel_syntax noprefix\nmov rax,[rip+rax]\n",
                ".intel_syntax noprefix\nvaddps ymm0,ymm1,ymm2\n",
                ".intel_syntax noprefix\nmov eax,1,\n",
                ".ascii \"unterminated\n",
                "/*unterminated",
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad); i += 1)
        {
            asm_test_pair(&test, (String8){.pointer = (char*)bad[i], .length = strlen(bad[i])}, 0, 0);
        }
        AsmResult padding = asm_test_pair(&test, BUSTER_S8("nop\n.p2align 2\nret\n"), 1, 0);
        static const uint8_t padding_bytes[] = {0x90, 0x90, 0x90, 0x90, 0xc3};
        asm_test_check(&test, padding.error == 0 && padding.link.atoms.count == 2 && padding.link.atoms.size[1] == sizeof(padding_bytes) && memcmp(padding.link.atoms.bytes[1], padding_bytes, sizeof(padding_bytes)) == 0, "default text alignment uses executable NOP padding", 0);
        static const int64_t boundaries[] = {-129, -128, 127, 128, 200, 255, 256, INT32_MIN, INT32_MAX};
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(boundaries); i += 1)
        {
            char source[256];
            const int length = snprintf(source, sizeof(source), ".intel_syntax noprefix\nmov rax,%lld\nadd rax,%lld\ncmp rax,%lld\nret\n", (long long)boundaries[i], (long long)boundaries[i], (long long)boundaries[i]);
            asm_test_pair(&test, (String8){.pointer = source, .length = length}, 1, 0);
        }
        uint64_t random = seed != 0 ? seed : 1;
        asm_test_valid_batches(&test, &random);
        static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789.,:;[](){}%$@+-*/\\\"# \n\t";
        for (uint32_t i = 0; i < fuzz; i += 1)
        {
            char source[512];
            const uint32_t length = asm_test_random(&random) % sizeof(source);
            for (uint32_t j = 0; j < length; j += 1)
            {
                const uint64_t value = asm_test_random(&random);
                source[j] = i % 11 == 0 ? value & 255 : alphabet[value % (sizeof(alphabet) - 1)];
            }
            asm_test_pair(&test, (String8){.pointer = source, .length = length}, -1, X86_ARCH_V4);
        }
        asm_test_lex_guard(&test);
        if (cross_check)
        {
            char directory[] = "/tmp/buster-assembler-XXXXXX";
            if (mkdtemp(directory) != 0)
            {
                asm_test_oracle(&test, asm_test_function, directory, 0);
                asm_test_oracle(&test, ".intel_syntax noprefix\n.text\nf: mov rax,200\nadd rax,200\nsub rcx,-129\nret\n.data\n.globl x\n.type x,@object\nx: .quad f+4, ext-8\n.size x,16\n", directory, 0);
                asm_test_oracle(&test, ".intel_syntax noprefix\nsimd:\nmov ah,bl\nmov r8,[r12+r9*4-129]\nmov eax,dword ptr fs:[8]\naddps xmm1,xmm2\nvaddps ymm1,ymm2,ymm3\nvaddps zmm1{k2}{z},zmm2,zmm3\nvaddps zmm1,zmm2,dword ptr [rax]{1to16}\n", directory, X86_ARCH_V4);
                asm_test_oracle(&test, ".intel_syntax noprefix\nf: jmp done\n.zero 120\njmp f\n.zero 20\n.p2align 5,0,31\ndone: ret\n.data\n.long external+7\n.quad f-8\n", directory, 0);
                asm_test_oracle(&test, ".text\nf: movq %rdi,%rax\naddq $200,%rax\nmovl 4(%rdi,%rcx,4),%edx\nshlq %cl,%rax\nshrdq %cl,%rbx,%rax\nret\n.size f,.-f\n", directory, 0);
                asm_test_oracle(&test, ".data\nexpr:\n.byte ~0,~1,~0+256\n.long -1|0,~0&0xffffffff\n.quad ~0xffffffffffffffff\n", directory, 0);
                asm_test_execution(&test, directory);
                asm_test_driver(&test, directory);
                asm_test_target_driver(&test, directory);
                asm_test_preprocessing(&test, directory);
                const char* files[] = {"oracle.s", "oracle.o", "output", "errors", "function.o", "caller.c", "program", "direct", "driver.s", "driver.S", "driver.o", "scalar.o", "values.h", "preprocessed.s"};
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(files); i += 1)
                {
                    char path[PATH_MAX];
                    snprintf(path, sizeof(path), "%s/%s", directory, files[i]);
                    unlink(path);
                }
                rmdir(directory);
            }
            else
            {
                asm_test_check(&test, false, "temporary directory", 0);
            }
        }
    }
    printf("assembler: %llu checks, %llu failures (seed %llu, fuzz %u)\n", (unsigned long long)test.checks, (unsigned long long)test.failures, (unsigned long long)seed, fuzz);
    arena_release(&test.scalar);
    arena_release(&test.vector);
    arena_release(&arena);
    return valid && test.failures == 0 ? 0 : 1;
}
