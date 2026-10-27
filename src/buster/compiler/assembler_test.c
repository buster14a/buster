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

// High bases or XMM registers make both movq encodings equally long; segment offsets must not hide their row.
static const char asm_test_segmented_movq[] =
    "movq %fs:(%r8),%xmm0\nmovq %gs:(%r8),%xmm0\n"
    "movq %fs:(%rax),%xmm8\nmovq %gs:(%rax),%xmm8\n"
    "movq %xmm0,%fs:(%r8)\nmovq %xmm0,%gs:(%r8)\n"
    "movq %xmm8,%fs:(%rax)\nmovq %xmm8,%gs:(%rax)\n";

static bool asm_test_gnu_object(ObjectView* view, String8 bytes, Arena* scratch, char* error, uint64_t error_size)
{
    // GAS appends relaxed branch relocations after ones made during parsing. ELF permits that order; the shared
    // comparator expects offset order. Normalize only the GNU copy, using the importer's already tested sorter.
    // Instruction bytes, relocation fields/targets/addends, and Buster's own relocation order remain checked.
    ImportElf elf;
    bool valid = import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE;
    for (uint32_t index = 1; valid && index < elf.section_count; index += 1)
    {
        const Elf64_Shdr header = import_section_header(&elf, index);
        if (header.sh_type == SHT_RELA)
        {
            uint64_t count = 0;
            valid = import_table(&header, sizeof(Elf64_Rela), &count) && count <= UINT32_MAX && import_within(bytes.length, header.sh_offset, header.sh_size);
            if (valid && count > 1)
            {
                const uint64_t saved = scratch->length;
                const char* sorted = import_vector_sort_relocations(bytes.pointer + header.sh_offset, count, scratch);
                valid = sorted != 0;
                if (valid)
                {
                    memcpy(bytes.pointer + header.sh_offset, sorted, header.sh_size);
                }
                arena_pop_to(scratch, saved);
            }
        }
    }
    if (!valid)
    {
        snprintf(error, error_size, "cannot normalize GNU as relocation order");
    }
    return valid && object_view_open(view, (const uint8_t*)bytes.pointer, bytes.length, true, error, error_size);
}

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
        passed = object.error == OBJECT_ERROR_NONE && object_view_open(&ours, object.bytes, object.size, false, error, sizeof(error)) && asm_test_gnu_object(&theirs, theirs_bytes, &test->vector, error, sizeof(error));
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
        "#define VALUE 200\n#define IMM(x) $x\n#define CAT(a,b) a ## b\n#define STR(x) #x\n#define PASTE(x) $ ## x\n#define REG(x) % ## x\n"
        "#if !defined(__ASSEMBLER__) || defined(REMOVE) || OVERRIDE != 7\n#error assembly options\n#endif\n";
    static const char source[] =
        "#include <values.h>\n# ordinary assembly comment VALUE\n"
        ".text\n.globl cpp_function\n.type cpp_function,@function\ncpp_function:\n"
        "movl $VALUE, %eax # inline comment VALUE\naddl IMM(OVERRIDE), %eax\n"
        "addl $\\\nVALUE, %eax\nmovl PASTE(OVERRIDE), REG(ecx) # 1$e+5 cost\njmp 1f\n1: ret\n.size cpp_function, .-cpp_function\n"
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

static void asm_test_register_words(AsmTest* test, const uint64_t* words, const uint64_t* lengths)
{
    uint64_t descriptions[8];
    _mm512_storeu_si512(descriptions, asm_word_registers_vector(_mm512_loadu_si512(words), _mm512_loadu_si512(lengths)));
    for (uint32_t i = 0; i < 8; i += 1)
    {
        X86Operand reference = {0};
        const bool found = asm_register((String8){.pointer = (char*)(words + i), .length = lengths[i]}, &reference);
        const uint32_t expected = found ? X86_OPERAND_REGISTER | reference.reg << 8 | reference.class << 16 | reference.width << 24 : 0;
        const bool cached = descriptions[i] & BUSTER_ASM_REGISTER_CACHED;
        asm_test_check(test, !cached || (descriptions[i] & ~BUSTER_ASM_REGISTER_CACHED) == expected, "packed register spelling agrees with independent scalar grammar", 0);
        asm_test_check(test, cached, "all bounded register spellings are classified", 0);
    }
}

static void asm_test_registers(AsmTest* test)
{
    uint64_t words[8];
    uint64_t lengths[8];
    // Every two-byte key, including every collision and non-ASCII byte; then every three-letter mixed-case key.
    for (uint32_t base = 0; base < 65536; base += 8)
    {
        for (uint32_t i = 0; i < 8; i += 1)
        {
            words[i] = base + i;
            lengths[i] = 2;
        }
        asm_test_register_words(test, words, lengths);
    }
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (uint32_t base = 0; base < 52 * 52 * 52; base += 8)
    {
        for (uint32_t i = 0; i < 8; i += 1)
        {
            const uint32_t value = base + i;
            words[i] = letters[value % 52] | letters[value / 52 % 52] << 8 | letters[value / (52 * 52) % 52] << 16;
            lengths[i] = 3;
        }
        asm_test_register_words(test, words, lengths);
    }
    static const char* const prefixes[] =
    {
        "r",
        "xmm",
        "ymm",
        "zmm",
        "k",
    };
    static const char* const suffixes[] =
    {
        "",
        "b",
        "w",
        "d",
        "q",
    };
    for (uint32_t prefix = 0; prefix < BUSTER_ARRAY_LENGTH(prefixes); prefix += 1)
    {
        for (uint32_t suffix = 0; suffix < BUSTER_ARRAY_LENGTH(suffixes); suffix += 1)
        {
            for (uint32_t digits = 1; digits <= 6; digits += 1)
            {
                for (uint32_t base = 0; base < 136; base += 8)
                {
                    for (uint32_t capitals = 0; capitals < 32; capitals += 1)
                    {
                        for (uint32_t i = 0; i < 8; i += 1)
                        {
                            char text[32];
                            const uint32_t length = snprintf(text, sizeof(text), "%s%0*u%s", prefixes[prefix], digits, base + i, suffixes[suffix]);
                            lengths[i] = length < 8 ? length : 8;
                            words[i] = 0;
                            for (uint32_t at = 0; at < length && at < 5; at += 1)
                            {
                                if ((capitals & (1u << at)) && text[at] >= 'a' && text[at] <= 'z')
                                {
                                    text[at] -= 'a' - 'A';
                                }
                            }
                            memcpy(words + i, text, lengths[i]);
                        }
                        asm_test_register_words(test, words, lengths);
                    }
                }
            }
        }
    }
    asm_test_pair(test, BUSTER_S8(".intel_syntax noprefix\n.quad r000000008,r000000000009,zmm000001,k000003\n"), 1, X86_ARCH_V4);
}

static void asm_test_register_diagnostics(AsmTest* test)
{
    // Exercise every RIP spelling and both operand positions before and after the 16-row batch boundary. A later
    // bad directive must not hide the earlier invalid operand; both paths must report that operand's exact byte.
    for (uint32_t leading = 0; leading <= 17; leading += 1)
    {
        for (uint32_t capitals = 0; capitals < 8; capitals += 1)
        {
            char name[] = "rip";
            for (uint32_t i = 0; i < 3; i += 1)
            {
                if (capitals & (1u << i))
                {
                    name[i] -= 'a' - 'A';
                }
            }
            for (uint32_t operand = 0; operand < 2; operand += 1)
            {
                for (uint32_t later_error = 0; later_error < 2; later_error += 1)
                {
                    char source[512];
                    uint32_t length = snprintf(source, sizeof(source), ".intel_syntax noprefix\n");
                    for (uint32_t row = 0; row < leading; row += 1)
                    {
                        length += snprintf(source + length, sizeof(source) - length, "mov rax,rbx\n");
                    }
                    const uint32_t location = length + (operand == 0 ? 4 : 8);
                    length += snprintf(source + length, sizeof(source) - length, "mov %s,%s\n%s", operand == 0 ? name : "rax", operand == 1 ? name : "rax", later_error ? ".bad\n" : "");
                    const AsmResult result = asm_test_pair(test, (String8){.pointer = source, .length = length}, 0, 0);
                    const bool correct = result.error != 0 && strcmp(result.error, "invalid register operand (AT&T indirect branches require '*')") == 0 && result.location == location;
                    asm_test_check(test, correct, "standalone RIP keeps its diagnostic before later source errors", result.error);
                }
            }
        }
    }
}

static void asm_test_numbers(AsmTest* test)
{
    uint64_t words[8];
    uint64_t lengths[8];
    uint64_t descriptions[8];
    uint64_t values[8];
    // Every possible byte in each digit position at every supported length. Include partial batches and verify
    // inactive stores, using the scalar number grammar as the oracle for each accepted decimal spelling.
    for (uint32_t length = 0; length <= 8; length += 1)
    {
        for (uint32_t position = 0; position < (length != 0 ? length : 1); position += 1)
        {
            for (uint32_t byte = 0; byte < 256; byte += 8)
            {
                for (uint32_t count = 0; count <= 8; count += 1)
                {
                    for (uint32_t i = 0; i < 8; i += 1)
                    {
                        words[i] = 0;
                        memset(words + i, '1', length);
                        ((uint8_t*)(words + i))[position] = byte + i;
                        lengths[i] = length;
                        descriptions[i] = 0;
                        values[i] = UINT64_MAX;
                    }
                    asm_simple_words_vector(words, lengths, count, descriptions, values);
                    for (uint32_t i = 0; i < 8; i += 1)
                    {
                        if (i < count)
                        {
                            const String8 text = {.pointer = (char*)(words + i), .length = length};
                            __int128 value = 0;
                            const bool decimal = length != 0 && !(length > 1 && text.pointer[0] == '0') && asm_number(text, &value);
                            const bool found = descriptions[i] == X86_OPERAND_IMMEDIATE;
                            asm_test_check(test, found == decimal && (!found || values[i] == value), "bounded decimal reduction agrees with scalar grammar", 0);
                        }
                        else
                        {
                            asm_test_check(test, descriptions[i] == 0 && values[i] == UINT64_MAX, "partial decimal batch leaves inactive stores untouched", 0);
                        }
                    }
                }
            }
        }
    }
}

static void asm_test_matcher(AsmTest* test, uint64_t* random)
{
    X86Target target;
    x86_target_parse("x86-64-v4", 9, &target);
    x86_match_initialize(test->index, &target);
    test->architecture = X86_ARCH_V4;
    static const __int128 values[] =
    {
        -129,
        -128,
        -1,
        0,
        1,
        127,
        128,
        255,
        256,
        32767,
        65535,
        INT32_MIN,
        INT32_MAX,
        UINT32_MAX,
        INT64_MIN,
        UINT64_MAX,
        (__int128)UINT64_MAX + 1,
        (__int128)INT64_MIN - 1,
    };
    uint32_t length_checks = 0;
    for (uint32_t f = 0; f < BUSTER_X86_FORM_LEGACY_MODES; f += 1)
    {
        const X86FormDescription* form = &x86_form_descriptions[f];
        uint8_t roles[4];
        const uint32_t count = x86_match_roles(form->syntax, roles);
        if (test->index->allowed[f] && x86_match_supported(form) && count <= 4)
        {
            const String8 name = {.pointer = (char*)form->mnemonic, .length = strlen(form->mnemonic)};
            const uint32_t bucket = x86_match_bucket(test->index, name);
            uint32_t occurrences = 0;
            for (uint32_t at = test->index->begin[bucket]; at < test->index->begin[bucket + 1]; at += 1)
            {
                occurrences += test->index->forms[at] == f;
            }
            asm_test_check(test, occurrences == 1, "exact mnemonic index contains every eligible form once", form->mnemonic);
            for (uint32_t variant = 0; variant < 64; variant += 1)
            {
                X86MatchRequest request = {.mnemonic = name, .count = count};
                request.width = variant % 4 == 0 ? form->operand_bytes : 0;
                request.broadcast = variant % 3 == 0 && (form->evex & BUSTER_X86_EVEX_BROADCAST) ? 64 / form->disp8_scale : 0;
                request.rounding = variant % 11 == 0;
                request.decorations = variant % 7 == 0 ? variant & 0xff : 0;
                for (uint32_t i = 0; i < count; i += 1)
                {
                    X86Operand* operand = &request.operands[i];
                    const uint32_t role = roles[i];
                    const uint16_t descriptor = role == X86_MATCH_RM ? form->rm : role == X86_MATCH_REG ? form->reg : form->vvvv;
                    const uint64_t bits = asm_test_random(random);
                    operand->width = form->operand_bytes;
                    operand->value = values[(variant + i) % BUSTER_ARRAY_LENGTH(values)];
                    operand->symbolic = variant % 9 == 0;
                    if (role == X86_MATCH_IMM || role == X86_MATCH_REL || role == X86_MATCH_ONE)
                    {
                        operand->kind = X86_OPERAND_IMMEDIATE;
                    }
                    else if (role == X86_MATCH_RM && (descriptor & BUSTER_X86_OPERAND_MEMORY) && (variant & 1))
                    {
                        operand->kind = X86_OPERAND_MEMORY;
                        operand->width = variant % 3 == 0 ? 0 : form->operand_bytes;
                        operand->mode = BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_BASE;
                        operand->base = bits & 31;
                        operand->segment = (bits >> 5) % 3;
                        if (variant & 2)
                        {
                            operand->mode |= BUSTER_X86_MODE_INDEX | ((bits >> 10) & 3);
                            operand->index = (bits >> 12) & 31;
                        }
                        if (variant % 5 == 0)
                        {
                            operand->mode = BUSTER_X86_MODE_MEMORY | BUSTER_X86_MODE_RIP;
                            operand->base = 0;
                            operand->index = 0;
                        }
                    }
                    else
                    {
                        operand->kind = X86_OPERAND_REGISTER;
                        operand->class = descriptor & BUSTER_X86_OPERAND_CLASS;
                        operand->reg = bits & 31;
                        if (role == X86_MATCH_ACC || role == X86_MATCH_CL)
                        {
                            operand->class = role == X86_MATCH_CL ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 1 ? BUSTER_X86_CLASS_GPR8 : form->operand_bytes == 2 ? BUSTER_X86_CLASS_GPR16 : form->operand_bytes == 4 ? BUSTER_X86_CLASS_GPR32 : BUSTER_X86_CLASS_GPR64;
                            operand->reg = role == X86_MATCH_CL ? 1 : 0;
                        }
                        if (role == X86_MATCH_ACC && variant % 2 == 0)
                        {
                            operand->class = variant / 2 % 16;
                        }
                        if (operand->class == BUSTER_X86_CLASS_GPR8 && variant % 3 == 0)
                        {
                            operand->reg = (bits & 3) + 4 + BUSTER_X86_REGISTER_HIGH_BYTE;
                        }
                    }
                }
                const X86Match scalar = x86_match(test->index, &request, false);
                const X86Match vector = x86_match(test->index, &request, true);
                const bool same = scalar.found == vector.found && scalar.ambiguous == vector.ambiguous && (!scalar.found || (scalar.length == vector.length && memcmp(&scalar.instruction, &vector.instruction, sizeof(X86Instruction)) == 0));
                asm_test_check(test, same, "catalogue matcher scalar/vector operands and selected bytes", form->mnemonic);
                if (scalar.found)
                {
                    uint8_t checked[BUSTER_X86_ENCODER_CAPACITY(1)];
                    uint8_t direct[BUSTER_X86_ENCODER_CAPACITY(1)];
                    const int64_t checked_length = x86_encode_scalar(&scalar.instruction, 1, checked, sizeof(checked), 0, 0);
                    const uint32_t direct_length = encode_one(&scalar.instruction, direct, X86_ENCODER_MODE_64);
                    const bool identical = checked_length > 0 && checked_length == scalar.length && direct_length == scalar.length && direct_length <= BUSTER_X86_MAX_INSTRUCTION_BYTES && memcmp(checked, direct, direct_length) == 0;
                    asm_test_check(test, identical, "validated matcher length and bytes against checked scalar encoder", form->mnemonic);
                    length_checks += 1;
                }
            }
        }
    }
    asm_test_check(test, length_checks != 0, "catalogue matcher checked-length coverage", "supported forms");
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

// asm_assemble_code by both paths (#147: C's inline assembly), which must give the same records, branch columns and
// references, and each failure alike, with both arenas as they were after it and scratch as it was after a success.
// `expected` is the record count, -1 for a failure.
static AsmCode asm_test_code_pair(AsmTest* test, const char* source, int64_t expected, const char* name)
{
    const String8 text = { .pointer = (char*)source, .length = strlen(source) };
    test->scalar.length = 0;
    test->vector.length = 0;
    // The scalar path's result in the scalar arena, its scratch the vector one; then the other way round, above it.
    const AsmCode scalar = asm_assemble_code(text, test->index, (AsmOptions){ 0 }, &test->scalar, &test->vector);
    const bool scalar_restored = test->vector.length == 0 && (scalar.error == 0 || test->scalar.length == 0);
    const uint64_t kept = test->scalar.length;
    const AsmCode vector = asm_assemble_code(text, test->index, (AsmOptions){ .vectorized = true }, &test->vector, &test->scalar);
    const bool vector_restored = test->scalar.length == kept && (vector.error == 0 || test->vector.length == 0);
    bool same = (scalar.error == 0) == (vector.error == 0) && scalar.count == vector.count && scalar.branch_count == vector.branch_count &&
                scalar.reference_count == vector.reference_count && (scalar.error == 0 || (strcmp(scalar.error, vector.error) == 0 && scalar.location == vector.location));
    same = same && (scalar.count == 0 || memcmp(scalar.records, vector.records, scalar.count * sizeof(X86Instruction)) == 0);
    same = same && (scalar.branch_count == 0 || (memcmp(scalar.branches, vector.branches, scalar.branch_count * sizeof(uint32_t)) == 0 &&
                                                 memcmp(scalar.targets, vector.targets, scalar.branch_count * sizeof(uint32_t)) == 0));
    for (uint32_t i = 0; same && i < scalar.reference_count; i += 1)
    {
        const AsmCodeReference* a = scalar.references + i;
        const AsmCodeReference* b = vector.references + i;
        same = a->record == b->record && a->field == b->field && a->kind == b->kind && a->addend == b->addend && a->name.length == b->name.length &&
               memcmp(a->name.pointer, b->name.pointer, a->name.length) == 0;
    }
    asm_test_check(test, same && scalar_restored && vector_restored, name, "inline code differs between the paths or leaves an arena moved");
    asm_test_check(test, expected < 0 ? vector.error != 0 : vector.error == 0 && vector.count == (uint64_t)expected, name, vector.error);
    return vector;
}

static void asm_test_code(AsmTest* test)
{
    // Every feature, as the C lowering's index has them: a template's instructions are its author's to choose.
    const X86Target everything = { .features = (UINT64_C(1) << X86_FEATURE_COUNT) - 1, .architecture = X86_ARCH_OTHER };
    x86_match_initialize(test->index, &everything);
    test->architecture = X86_ARCH_OTHER;
    // A numeric label backward in a loop: the branch's rel8 form at displacement 0, its target the label's next record.
    const AsmCode loop = asm_test_code_pair(test, "1:\n\taddl $2, %eax\n\tdecl %ecx\n\tjnz 1b", 3, "inline code: a loop");
    asm_test_check(test, loop.branch_count == 1 && loop.branches[0] == 2 && loop.targets[0] == 0 && x86_form_description(loop.records[2].form)->relative_bytes == 1 &&
                         loop.records[2].displacement == 0 && loop.reference_count == 0, "inline code: a loop's branch column", 0);
    // Labels forward, the last one the end (the record count).
    const AsmCode forward = asm_test_code_pair(test, "testl %esi, %esi\n\tjle .Lneg\n\tmovl $5, %eax\n\tjmp .Lend\n.Lneg:\n\tmovl $7, %eax\n.Lend:", 5,
                                               "inline code: labels forward");
    asm_test_check(test, forward.branch_count == 2 && forward.branches[0] == 1 && forward.targets[0] == 4 && forward.branches[1] == 3 && forward.targets[1] == 5,
                   "inline code: forward branches' columns", 0);
    asm_test_code_pair(test, ".intel_syntax noprefix\nmov eax, 1\n.att_syntax prefix\naddl $2, %eax\n", 2, "inline code: the syntax directives");
    asm_test_code_pair(test, "", 0, "inline code: an empty source");
    asm_test_code_pair(test, "  # a comment, %eax\n\n", 0, "inline code: a comment alone");
    // The newly admitted processor-state instructions, each a record.
    asm_test_code_pair(test, "cpuid\nxgetbv\nxchgq %rbx,%rsi\nfnstcw (%rax)\nfldcw 2(%rsp)\nfnstsw (%rdx)\nfnclex\nstmxcsr -4(%rsp)\nldmxcsr (%r12)\npcmpeqd %xmm1,%xmm2\n"
                             "bswapl %eax\nbswapq %r9", 12,
                       "inline code: processor-state instructions");
    // Symbols the source doesn't define: references by name, a call's preemptible, the records marked.
    const AsmCode symbols = asm_test_code_pair(test, "call external\n\tmovq data+8(%rip), %rax\n\tmovl $sym+4, %eax", 3, "inline code: references");
    const bool marked = symbols.count == 3 && (symbols.records[0].form & BUSTER_X86_FORM_FIXUP) != 0 && (symbols.records[1].form & BUSTER_X86_FORM_FIXUP) != 0 &&
                        (symbols.records[2].form & BUSTER_X86_FORM_FIXUP) != 0;
    const bool referred = symbols.reference_count == 3 && symbols.references[0].record == 0 && symbols.references[0].kind == BUSTER_LINK_FIXUP_CALL &&
                          symbols.references[0].field == X86_FIXUP_DISPLACEMENT && symbols.references[0].name.length == 8 &&
                          memcmp(symbols.references[0].name.pointer, "external", 8) == 0 && symbols.references[1].kind == BUSTER_LINK_FIXUP_PC32 &&
                          symbols.references[1].addend == 8 && symbols.references[2].field == X86_FIXUP_IMMEDIATE && symbols.references[2].kind == BUSTER_LINK_FIXUP_ABS32 &&
                          symbols.references[2].addend == 4;
    asm_test_check(test, marked && referred, "inline code: references' names, kinds, fields and addends", 0);
    // Named labels, at their next records (the last one the end), in the order the source first names them; and the
    // source's own named labels as data, a call's target and a branch's with an addend, which are references to them
    // (the branch's from its rel32 form), beside a branch that branch sizing sizes.
    const AsmCode labels = asm_test_code_pair(test, "leaq here(%rip), %rax\nhere: call here\njmp here+2\njmp here\n.Lend:", 4, "inline code: named labels");
    const bool placed = labels.label_count == 2 && labels.labels[0].record == 1 && labels.labels[0].name.length == 4 &&
                        memcmp(labels.labels[0].name.pointer, "here", 4) == 0 && labels.labels[1].record == 4 && labels.labels[1].name.length == 5 &&
                        memcmp(labels.labels[1].name.pointer, ".Lend", 5) == 0;
    const bool own = labels.reference_count == 3 && labels.references[0].record == 0 && labels.references[0].kind == BUSTER_LINK_FIXUP_PC32 &&
                     labels.references[1].record == 1 && labels.references[1].kind == BUSTER_LINK_FIXUP_CALL && labels.references[2].record == 2 &&
                     labels.references[2].kind == BUSTER_LINK_FIXUP_CALL && labels.references[2].addend == 2 &&
                     (labels.records[2].form & BUSTER_X86_FORM_NUMBER) == X86_FORM_JMP_REL32 && labels.branch_count == 1 && labels.branches[0] == 3 &&
                     labels.targets[0] == 1 && (labels.records[3].form & BUSTER_X86_FORM_FIXUP) == 0;
    asm_test_check(test, placed && own, "inline code: named labels and references to them", 0);
    // Labels another source defines, a .L one among them: references by name, which the caller resolves.
    const AsmCode others = asm_test_code_pair(test, "jne .Lother\n\tleaq x86_test_return(%rip), %rax", 2, "inline code: other sources' labels");
    asm_test_check(test, others.reference_count == 2 && others.label_count == 0 && others.references[0].kind == BUSTER_LINK_FIXUP_CALL &&
                         others.references[0].name.length == 7 && memcmp(others.references[0].name.pointer, ".Lother", 7) == 0 &&
                         x86_form_description(others.records[0].form)->relative_bytes == 4 && others.references[1].kind == BUSTER_LINK_FIXUP_PC32,
                   "inline code: other sources' labels by name", 0);
    // What naked functions' templates take: SSE2's movq (mov's 64-bit form without an XMM operand), the x87's loads and
    // stores of each width, the string instructions with their prefixes (AT&T's l the dword one), the flags, and in
    // Intel syntax AVX-512's 64-bit element moves, memory pushes and pops and an indirect jump.
    asm_test_code_pair(test, "movq 56(%r12), %xmm0\nmovq %xmm1, 160(%r12)\nmovq %rax, %xmm2\nmovq %xmm3, %rcx\nmovq %xmm4, %xmm5\nmovq %rsp, %rbp\n"
                             "movq $5, 8(%rax)\nfstpt 176(%r12)\nfldt (%rax)\nfstpl 8(%rsp)\nflds 4(%rsp)\nrep movsb\nrep stosq\nrepe cmpsb\nrepnz scasw\n"
                             "movsl\nlodsq\npushfq\npopfq\ncld",
                       20, "inline code: naked functions' instructions");
    asm_test_code_pair(test, asm_test_segmented_movq, 8, "inline code: segmented movq encoding choice");
    const AsmCode intel = asm_test_code_pair(test, ".intel_syntax noprefix\nvmovdqu64 zmm0, zmmword ptr [rdi + 192]\nvmovdqa64 zmmword ptr [rdi], zmm15\n"
                                                   "push qword ptr [rdi + 128]\npop qword ptr [rdi + 128]\njmp qword ptr [rip + target]\nrep movsb\n.att_syntax prefix\n",
                                             6, "inline code: naked functions' Intel instructions");
    asm_test_check(test, intel.reference_count == 1 && intel.references[0].record == 4 && intel.references[0].kind == BUSTER_LINK_FIXUP_PC32,
                   "inline code: an indirect jump's RIP-relative reference", 0);
    // What it refuses: directives but the syntax's, its numeric labels and `.` but as a jmp's or jcc's target without
    // an addend, undefined numeric labels, a dword string instruction's l in Intel syntax, and what the matcher doesn't
    // take.
    static const char* const refused[] = {
        ".byte 0x90",
        ".globl x\nx: nop",
        ".section .data\n.long 1",
        ".p2align 4\nnop",
        "loop 1f\n1:",
        "call 1f\n1:",
        "jmp 1f+2\n1: nop",
        "leaq 1f(%rip), %rax\n1:",
        "leaq .(%rip), %rax",
        "loop here\nhere:",
        "jmp 9f",
        ".intel_syntax noprefix\nrep movsl",
        "fstp (%rax)",
        "bogus %eax",
        ".cfi_startproc\nnop\n.cfi_endproc",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        asm_test_code_pair(test, refused[i], -1, refused[i]);
    }
    // A failure without room: nothing kept in either arena.
    Arena tiny = arena_reserve(4096);
    tiny.length = 16;
    tiny.capacity = 64;
    const AsmCode exhausted = asm_assemble_code(BUSTER_S8("nop\nnop\nnop\n"), test->index, (AsmOptions){ .vectorized = true }, &test->vector, &tiny);
    asm_test_check(test, exhausted.error != 0 && exhausted.count == 0 && tiny.length == 16, "inline code: an exhausted scratch arena keeps nothing", 0);
    tiny.capacity = 4096;
    arena_release(&tiny);
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

// A source through both paths, then the bytes of one of its atoms: 1 is .text, 2 the next section the source opens.
static void asm_test_bytes(AsmTest* test, const char* source, AtomId atom, const uint8_t* expected, uint32_t size, const char* name)
{
    const AsmResult result = asm_test_pair(test, (String8){.pointer = (char*)source, .length = strlen(source)}, 1, 0);
    const bool same = result.error == 0 && atom < result.link.atoms.count && result.link.atoms.size[atom] == size && memcmp(result.link.atoms.bytes[atom], expected, size) == 0;
    asm_test_check(test, same, name, result.error);
}

// Code padding of every length to 135 bytes, and of 200 and 255, after an instruction and after data: GNU as pads with
// its NOPs, starts with a nop after data and jumps over the rest past 87 bytes. Then fills of 0x90 and 0xcc, maximum
// skips, padding that follows padding, and each section keeping its own last statement across section changes.
static String8 asm_test_padding_source(char* source, uint32_t capacity)
{
    uint32_t length = snprintf(source, capacity, ".text\npadding:\n");
    for (uint32_t padding = 1; padding <= 137; padding += 1)
    {
        const uint32_t bytes = padding <= 135 ? padding : padding == 136 ? 200 : 255;
        for (uint32_t data = 0; data < 2; data += 1)
        {
            length += snprintf(source + length, capacity - length, ".p2align 8\n.zero %u\n%s\n.p2align 8\n", 255 - bytes, data != 0 ? ".byte 0x90" : "nop");
        }
    }
    length += snprintf(source + length, capacity - length,
                       "nop\n.p2align 4,,7\nnop\n.p2align 4,0x90,15\n.byte 1\n.p2align 4,0x90\nnop\n.p2align 4,0xcc\n"
                       ".byte 1\n.data\nnop\n.text\n.p2align 4\nnop\n.data\n.byte 1\n.text\n.p2align 4\n.byte 1\n.balign 2\n.balign 16\nret\n");
    return (String8){.pointer = source, .length = length < capacity ? length : 0};
}

// Whether this GNU as pads code as binutils 2.47 does, which the padding fixture's exact comparison assumes: 2.42
// orders its long NOPs differently. After a nop, 2.47 pads 31 bytes as a 9-byte NOP and then two 11-byte ones.
static bool asm_test_gas_pads_like_2_47(AsmTest* test, const char* directory)
{
    // A label, without which GNU as writes no symbol table, and object_view_open refuses the object.
    static const char probe[] = "f: nop\n.p2align 5\nret\n";
    static const uint8_t expected[] =
        {
            0x90,
            0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xc3,
    };
    char source[PATH_MAX];
    char object[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(source, sizeof(source), "%s/oracle.s", directory);
    snprintf(object, sizeof(object), "%s/oracle.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    char* arguments[] = {"as", "--64", "-o", object, source, 0};
    bool same = object_write_file(source, (const uint8_t*)probe, sizeof(probe) - 1) == OBJECT_ERROR_NONE && object_test_run(arguments, output, errors) == 0;
    const String8 bytes = same ? object_test_read_file(&test->vector, object) : (String8){0};
    ObjectView view;
    char error[256] = {0};
    same = same && object_view_open(&view, (const uint8_t*)bytes.pointer, bytes.length, true, error, sizeof(error));
    bool found = false;
    for (uint64_t i = 1; same && !found && i < view.section_count; i += 1)
    {
        const char* name = object_view_section_name(&view, i);
        found = name != 0 && strcmp(name, ".text") == 0;
        same = !found || (view.sections[i].sh_size == sizeof(expected) && memcmp(view.file + view.sections[i].sh_offset, expected, sizeof(expected)) == 0);
    }
    return same && found;
}

// The driver reads a pipe, which has no size, to its end, and refuses a file one byte past the limit.
static void asm_test_reader(AsmTest* test, const char* directory)
{
    static const char text[] = "nop\nret\n";
    Arena arena = arena_reserve(1ull << 26);
    int ends[2];
    bool okay = pipe(ends) == 0;
    if (okay)
    {
        okay = write(ends[1], text, sizeof(text) - 1) == sizeof(text) - 1;
        close(ends[1]);
        String8 source;
        okay = okay && asm_read_descriptor(ends[0], &arena, &source) && source.length == sizeof(text) - 1 && memcmp(source.pointer, text, source.length) == 0;
        okay = okay && arena.length == (uint64_t)((uint8_t*)source.pointer - arena.base) + source.length + 1;
        close(ends[0]);
    }
    asm_test_check(test, okay, "assembly input read from a pipe to its end", 0);
    // A pipe has no size to refuse beforehand: a child writes one byte past the limit into one, and the read refuses it.
    okay = pipe(ends) == 0;
    if (okay)
    {
        const pid_t child = fork();
        if (child == 0)
        {
            static const uint8_t chunk[1u << 16];
            close(ends[0]);
            uint64_t left = BUSTER_ASM_MAX_SOURCE + 1;
            ssize_t written = 1;
            while (left != 0 && written > 0)
            {
                written = write(ends[1], chunk, left < sizeof(chunk) ? left : sizeof(chunk));
                left -= written > 0 ? (uint64_t)written : 0;
            }
            _exit(0);
        }
        close(ends[1]);
        String8 source;
        const uint64_t before = arena.length;
        okay = child > 0 && !asm_read_descriptor(ends[0], &arena, &source) && arena.length == before && source.pointer == 0;
        close(ends[0]);
        if (child > 0)
        {
            waitpid(child, 0, 0);
        }
    }
    asm_test_check(test, okay, "assembly input past the limit refused from a pipe", 0);
    char large[PATH_MAX];
    snprintf(large, sizeof(large), "%s/large.s", directory);
    const int fd = open(large, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    okay = fd >= 0 && ftruncate(fd, BUSTER_ASM_MAX_SOURCE + 1) == 0;
    if (fd >= 0)
    {
        close(fd);
    }
    String8 source;
    const uint64_t before = arena.length;
    okay = okay && !asm_read_file(large, &arena, &source) && arena.length == before && source.pointer == 0;
    asm_test_check(test, okay, "assembly input past the limit refused", 0);
    unlink(large);
    arena_release(&arena);
}

// .S pastes and '$' as assembler-with-cpp has them, through both preprocessor kernels and without external tools:
// `$ ## X` is $7 and `% ## eax` is %eax, as in GCC and Clang. A comment holds 20,000 pp-numbers whose '$' splits each
// into five tokens, more than their dollars would bound.
// Check -E's text with the external consumer: our assembler tolerates spaces beside '$', while GNU as does not.
static void asm_test_preprocessed_spacing(AsmTest* test, const char* directory)
{
    char input[PATH_MAX];
    char printed[PATH_MAX];
    char object[PATH_MAX];
    char output[PATH_MAX];
    char errors[PATH_MAX];
    snprintf(input, sizeof(input), "%s/dollar-spacing.S", directory);
    snprintf(printed, sizeof(printed), "%s/dollar-output.s", directory);
    snprintf(object, sizeof(object), "%s/dollar-output.o", directory);
    snprintf(output, sizeof(output), "%s/output", directory);
    snprintf(errors, sizeof(errors), "%s/errors", directory);
    // The parameters check argument boundaries; a fixed body keeps parameter substitution out of this regression.
    static const char source[] =
        ".text\n.macro load value, reg\nmovl $5, %eax\n.endm\n"
        "#define V 5\nload $V %eax\nfoo$bar: jmp foo$bar\n";
    static const uint8_t expected[] = {0xb8, 0x05, 0x00, 0x00, 0x00, 0xeb, 0xfe};
    const bool written = object_write_file(input, (const uint8_t*)source, sizeof(source) - 1) == OBJECT_ERROR_NONE;
    Arena files = arena_reserve(1ull << 24);
    for (uint32_t scalar = 0; scalar < 2; scalar += 1)
    {
        char* preprocess[] = {"/proc/self/exe", "-E", "-P", "-x", "assembler-with-cpp", "-o", printed, input, scalar ? "--scalar" : 0, 0};
        char* assemble[] = {"as", "--64", "-o", object, printed, 0};
        bool okay = written && object_test_run(preprocess, output, errors) == 0 && object_test_run(assemble, output, errors) == 0;
        const String8 bytes = okay ? object_test_read_file(&files, object) : (String8){0};
        ObjectView view;
        char difference[256] = {0};
        okay = okay && object_view_open(&view, (const uint8_t*)bytes.pointer, bytes.length, true, difference, sizeof(difference));
        bool code = false;
        bool symbol = false;
        for (uint64_t i = 0; okay && i < view.section_count; i += 1)
        {
            const char* name = object_view_section_name(&view, i);
            if (name != 0 && strcmp(name, ".text") == 0)
            {
                const Elf64_Shdr* section = &view.sections[i];
                code = section->sh_size == sizeof(expected) && memcmp(bytes.pointer + section->sh_offset, expected, sizeof(expected)) == 0;
            }
        }
        for (uint64_t i = 0; okay && i < view.symbol_count; i += 1)
        {
            const char* name = object_view_symbol_name(&view, i);
            symbol |= name != 0 && strcmp(name, "foo$bar") == 0 && view.symbols[i].st_value == 5 && view.symbols[i].st_shndx != SHN_UNDEF;
        }
        asm_test_check(test, okay && code && symbol, "-E dollar spacing: GNU as macro arguments, bytes and symbol", difference);
        arena_pop_to(&files, 0);
    }
    unlink(input);
    unlink(printed);
    unlink(object);
    arena_release(&files);
}

static void asm_test_preprocessed(AsmTest* test, const char* directory)
{
    char input[PATH_MAX];
    snprintf(input, sizeof(input), "%s/dollars.S", directory);
    Arena files = arena_reserve(1ull << 28);
    const uint32_t capacity = 1u << 20;
    char* text = arena_push(&files, capacity, 1);
    bool okay = text != 0;
    if (okay)
    {
        uint32_t length = snprintf(text, capacity, "#define X 7\n#define IMM(x) $ ## x\n#define REG(x) %% ## x\nmovl IMM(X), REG(eax)\nret #");
        for (uint32_t i = 0; i < 20000; i += 1)
        {
            length += snprintf(text + length, capacity - length, " 1$e+5");
        }
        length += snprintf(text + length, capacity - length, "\n");
        okay = length < capacity && object_write_file(input, (const uint8_t*)text, length) == OBJECT_ERROR_NONE;
    }
    static const uint8_t expected[] = {0xb8, 0x07, 0x00, 0x00, 0x00, 0xc3};
    for (uint32_t vectorized = 0; vectorized < 2; vectorized += 1)
    {
        char* arguments[] = {input};
        PPOptions options = {0};
        const char* path = 0;
        const char* output = 0;
        bool markers = false;
        String8 source = {0};
        bool assembled = okay && pp_parse_options(1, arguments, &options, &files, &path, &output, &markers);
        options.scalar = vectorized == 0;
        assembled = assembled && asm_preprocess_file(input, options, &files, &source);
        const AsmResult result = assembled ? asm_assemble(source, test->index, (AsmOptions){.vectorized = vectorized != 0}, &files) : (AsmResult){.error = "preprocessing failed"};
        assembled = result.error == 0 && result.link.atoms.count == 2 && result.link.atoms.size[1] == sizeof(expected) && memcmp(result.link.atoms.bytes[1], expected, sizeof(expected)) == 0;
        asm_test_check(test, assembled, ".S pastes and '$' splits as assembler-with-cpp has them", result.error);
    }
    unlink(input);
    arena_release(&files);
}

#include "assembler_compat_test.c"
#include "assembler_corpus_test.c"

static int run_assembler_tests(int count, char** arguments)
{
    uint64_t seed = 1;
    uint32_t fuzz = 2000;
    bool cross_check = false;
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < count; i += 1)
    {
        if (strcmp(arguments[i], "--cross-check") == 0)
        {
            cross_check = true;
        }
        else if (strcmp(arguments[i], "--keep") == 0)
        {
            keep = true;
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
                "leal (%rdi),%rax\n",
                "pushw %eax\n",
                ".intel_syntax noprefix\ncall dword ptr callback\n",
                ".intel_syntax noprefix\nmov eax,[rax-4*rbx]\n",
                ".data\nfirst: .byte 1\n.zero 200000000\n.byte 2\n.zero 200000000\n",
                ".intel_syntax noprefix\nmov eax,[rax + -rsi]\n",
                ".intel_syntax noprefix\nmov eax,[rax + - -rsi]\n",
                ".intel_syntax noprefix\n.quad rsi\n",
                ".intel_syntax noprefix\nmov rax,offset rsi\n",
                ".intel_syntax noprefix\nmov eax,dword ptr eax\n",
                ".intel_syntax noprefix\ncall qword ptr rax\n",
                ".intel_syntax noprefix\nmov eax,fs:rax\n",
                ".intel_syntax noprefix\n.quad RAX\n",
                ".intel_syntax noprefix\nvaddps zmm1{k01},zmm2,zmm3\n",
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(bad); i += 1)
        {
            asm_test_pair(&test, (String8){.pointer = (char*)bad[i], .length = strlen(bad[i])}, 0, 0);
        }
        // The defects #227's review found (#235), each also compared with GNU as by --cross-check.
        static const char* const reviewed[] =
            {
                ".intel_syntax noprefix\ncall qword ptr callback\njmp qword ptr callback\ncall qword ptr [rax]\n.data\ncallback: .quad external\n",
                ".data\nstart: .long .-start\n.quad ., .\n.long 1f - .\n.byte 0\n1:\n",
                "pushq $external\npush $external+8\n.intel_syntax noprefix\npush offset external\n",
                ".data\nd: .byte 1\n.p2align 2,0,0\n.byte 2\n.balign 8,0,0\n.byte 3\n",
                "f: leaq (%rdi),%rax\nleal 4(%rdi,%rsi,2),%eax\nleaw 8(%rdi),%ax\nlea (%rdi),%rax\n",
                "t: jnb t\njnl t\nsal %cl,%rax\nsalq $2,%rax\njnz t\n",
                "f: pushw %ax\npushw (%rax)\npushw $200\npopw %ax\npush %ax\n",
                ".intel_syntax noprefix\nf: mov eax,dword ptr [rdi+4*rsi]\nlea rax,[8*rcx+rdx+16]\nmov ecx,dword ptr [rbx+2*r12-4]\nmov eax,dword ptr [rax+-8]\nmov eax,dword ptr [rax - -8]\nmov eax,dword ptr [rax+4*-2]\n",
                ".intel_syntax prefix\nf: mov %eax,dword ptr [%rdi+4*%rsi]\n",
                ".intel_syntax noprefix\nf: mov eax,[rax + +rsi]\nmov eax,[rax + +rsi*4]\nmov eax,[+rsi]\nmov eax,[rax + rsi + +8]\n",
                "f: .quad rsi\nmovq rax,%rcx\n.intel_syntax prefix\nmov %eax,dword ptr rsi\n",
                ".intel_syntax noprefix\nf: mov rax,r08\nmov rax,xmm00\nmov rcx,r08d\n.quad r08, k00, xmm00\n",
                ".intel_syntax noprefix\nf: mov rax,RAX\nmov eax,[RSI]\nadd rax,R8\nmov rax,Rax\nmov ecx,R10D\n.att_syntax prefix\nmovq %RAX,%RCX\n",
        };
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(reviewed); i += 1)
        {
            asm_test_pair(&test, (String8){.pointer = (char*)reviewed[i], .length = strlen(reviewed[i])}, 1, 0);
        }
        static const uint8_t indirect[] = {0xff, 0x14, 0x25, 0, 0, 0, 0, 0xff, 0x24, 0x25, 0, 0, 0, 0};
        asm_test_bytes(&test, ".intel_syntax noprefix\ncall qword ptr callback\njmp qword ptr callback\n.data\ncallback: .quad 0\n", 1, indirect, sizeof(indirect), "an explicit memory width makes a branch indirect");
        static const uint8_t dots[] = {0, 0, 0, 0, 4, 0, 0, 0};
        asm_test_bytes(&test, ".data\nstart: .long .-start, .-start\n", 2, dots, sizeof(dots), "'.' in data is the address of its own datum");
        static const uint8_t unlimited[] = {1, 0, 0, 0, 2};
        asm_test_bytes(&test, ".data\n.byte 1\n.p2align 2,0,0\n.byte 2\n", 2, unlimited, sizeof(unlimited), "an alignment maximum of 0 is no maximum");
        static const uint8_t after_instruction[] = {0x90, 0x0f, 0x1f, 0x00, 0xc3};
        asm_test_bytes(&test, "nop\n.p2align 2\nret\n", 1, after_instruction, sizeof(after_instruction), "code padding is GNU as's NOPs");
        static const uint8_t after_data[] = {0x01, 0x90, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00, 0xc3};
        asm_test_bytes(&test, ".byte 1\n.p2align 3\nret\n", 1, after_data, sizeof(after_data), "code padding after data starts with a nop");
        static const uint8_t zero_padded[] = {7, 0, 0, 0, 0, 0, 0, 0};
        asm_test_bytes(&test, ".intel_syntax noprefix\n.equ r08,7\n.data\n.quad r08\n", 2, zero_padded, sizeof(zero_padded), "a zero-padded register number is a symbol's name, as in GNU as");
        static const uint8_t plus_register[] = {0x8b, 0x04, 0x30};
        asm_test_bytes(&test, ".intel_syntax noprefix\n.equ rsi,8\nmov eax,[rax + +rsi]\n", 1, plus_register, sizeof(plus_register), "a register after a unary plus is the register, not a symbol of its name");
        const AsmResult pushed = asm_test_pair(&test, BUSTER_S8("push $external\n"), 1, 0);
        asm_test_check(&test, pushed.error == 0 && pushed.link.fixups.count == 2 && pushed.link.fixups.kind[1] == BUSTER_LINK_FIXUP_ABS32S, "PUSH's sign-extended imm32 relocates as 32S", pushed.error);
        const uint32_t padding_capacity = 64u << 10;
        char* padding_text = arena_push(&arena, padding_capacity, 1);
        const String8 padding = padding_text != 0 ? asm_test_padding_source(padding_text, padding_capacity) : (String8){0};
        asm_test_check(&test, padding.length != 0, "padding source", 0);
        asm_test_pair(&test, padding, 1, 0);
        static const int64_t boundaries[] = {-129, -128, 127, 128, 200, 255, 256, INT32_MIN, INT32_MAX};
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(boundaries); i += 1)
        {
            char source[256];
            const int length = snprintf(source, sizeof(source), ".intel_syntax noprefix\nmov rax,%lld\nadd rax,%lld\ncmp rax,%lld\nret\n", (long long)boundaries[i], (long long)boundaries[i], (long long)boundaries[i]);
            asm_test_pair(&test, (String8){.pointer = source, .length = length}, 1, 0);
        }
        uint64_t random = seed != 0 ? seed : 1;
        uint64_t matcher_random = random;
        asm_test_matcher(&test, &matcher_random);
        asm_test_registers(&test);
        asm_test_register_diagnostics(&test);
        asm_test_numbers(&test);
        asm_test_code(&test);
        asm_test_valid_batches(&test, &random);
        static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789.,:;[](){}%$@+-*/<>=&|~!^\\\"# \n\t";
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
        char directory[] = "/tmp/buster-assembler-XXXXXX";
        if (mkdtemp(directory) != 0)
        {
            asm_test_reader(&test, directory);
            asm_test_preprocessed(&test, directory);
            asm_test_compatibility(&test, directory, cross_check);
            if (cross_check)
            {
                asm_test_oracle(&test, asm_test_function, directory, 0);
                asm_test_oracle(&test, ".intel_syntax noprefix\nfixture: test al,200\ntest ax,200\ntest eax,200\ntest rax,200\nnop dword ptr [rax]\n", directory, 0);
                asm_test_oracle(&test, ".intel_syntax noprefix\n.text\nf: mov rax,200\nadd rax,200\nsub rcx,-129\nret\n.data\n.globl x\n.type x,@object\nx: .quad f+4, ext-8\n.size x,16\n", directory, 0);
                asm_test_oracle(&test, ".intel_syntax noprefix\nsimd:\nmov ah,bl\nmov r8,[r12+r9*4-129]\nmov eax,dword ptr fs:[8]\naddps xmm1,xmm2\nvaddps ymm1,ymm2,ymm3\nvaddps zmm1{k2}{z},zmm2,zmm3\nvaddps zmm1,zmm2,dword ptr [rax]{1to16}\n", directory, X86_ARCH_V4);
                asm_test_oracle(&test, ".intel_syntax noprefix\nf: jmp done\n.zero 120\njmp f\n.zero 20\n.p2align 5,0,31\ndone: ret\n.data\n.long external+7\n.quad f-8\n", directory, 0);
                asm_test_oracle(&test, ".text\nf: movq %rdi,%rax\naddq $200,%rax\nmovl 4(%rdi,%rcx,4),%edx\nshlq %cl,%rax\nshrdq %cl,%rbx,%rax\nret\n.size f,.-f\n", directory, 0);
                asm_test_oracle(&test, ".data\nexpr:\n.byte ~0,~1,~0+256\n.long -1|0,~0&0xffffffff\n.quad ~0xffffffffffffffff\n", directory, 0);
                // The processor-state instructions C's inline assembly reaches (#147).
                asm_test_oracle(&test, ".text\nstate: cpuid\nxgetbv\nxchgq %rbx,%rsi\nxchgq %rsi,%rbx\nxchgl %ecx,(%rdi)\nxchgb %al,%bl\n"
                                       "fnstcw (%rax)\nfldcw 2(%rsp)\nfnstsw (%rdx)\nfnclex\nstmxcsr -4(%rsp)\nldmxcsr (%r12)\n"
                                       "pcmpeqd %xmm1,%xmm2\npcmpeqd 16(%rax),%xmm15\nbswapl %eax\nbswapq %r9\nbswap %r11d\n", directory, X86_ARCH_V3);
                // And naked functions' (#147): SSE2's movq, the x87's loads and stores, the string instructions, the flags,
                // and AVX-512's 64-bit element moves.
                asm_test_oracle(&test, ".text\nnaked: movq 56(%r12),%xmm0\nmovq %xmm1,160(%r12)\nmovq %rax,%xmm2\nmovq %xmm3,%rcx\nmovq %xmm4,%xmm5\n"
                                       "movq %xmm9,%xmm12\nmovq (%rsp),%xmm15\nmovq %rsp,%rbp\nmovq $5,8(%rax)\nfstpt 176(%r12)\nfldt (%rax)\nfstpl 8(%rsp)\n"
                                       "fldl -8(%rbp)\nflds 4(%rsp)\nfstps (%r9)\nrep movsb\nrep movsl\nrep movsq\nrep stosb\nrep stosw\nrep stosq\nrep lodsb\n"
                                       "movsb\nmovsw\nmovsl\nmovsq\nstosl\nlodsq\ncmpsb\nscasq\nrepe cmpsb\nrepe cmpsq\nrepne cmpsl\nrepe scasb\nrepne scasw\n"
                                       "repz scasl\nrepnz cmpsw\nrep cmpsb\npushfq\npopfq\n"
                                       ".intel_syntax noprefix\nvmovdqu64 zmm0,zmmword ptr [rdi+192]\nvmovdqu64 zmmword ptr [rdi+1152],zmm15\n"
                                       "vmovdqa64 zmm1,zmmword ptr [rax]\nvmovdqa64 zmmword ptr [rsp],zmm2\nvmovdqu64 zmm3,zmm4\npush qword ptr [rdi+128]\n"
                                       "pop qword ptr [rdi+128]\nrep movsb\nrep stosd\n", directory, X86_ARCH_V4);
                asm_test_oracle(&test, asm_test_segmented_movq, directory, 0);
                for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(reviewed); i += 1)
                {
                    asm_test_oracle(&test, reviewed[i], directory, 0);
                }
                if (padding.length != 0 && asm_test_gas_pads_like_2_47(&test, directory))
                {
                    asm_test_oracle(&test, padding.pointer, directory, 0);
                }
                else if (padding.length != 0)
                {
                    printf("assembler: this GNU as orders code padding unlike binutils 2.47, so only the known padding bytes were checked\n");
                }
                asm_test_execution(&test, directory);
                asm_test_driver(&test, directory);
                asm_test_target_driver(&test, directory);
                asm_test_preprocessing(&test, directory);
                asm_test_preprocessed_spacing(&test, directory);
                asm_test_compiler_corpus(&test, directory, keep);
            }
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
    printf("assembler: %llu checks, %llu failures (seed %llu, fuzz %u)\n", (unsigned long long)test.checks, (unsigned long long)test.failures, (unsigned long long)seed, fuzz);
    arena_release(&test.scalar);
    arena_release(&test.vector);
    arena_release(&arena);
    return valid && test.failures == 0 ? 0 : 1;
}
