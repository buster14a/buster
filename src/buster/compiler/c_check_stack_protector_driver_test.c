// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The stack protector (#3558), included by c_checker_driver_test.c after c_check_stack_clash_driver_test.c, whose object
// comparison (check_test_stack_clash_same) it takes. -fstack-protector, -fstack-protector-strong and -fstack-protector-all
// guard functions as clang's code does (check_lower_guard, tier0_emit_guard_store and tier0_emit_guard_check): a copy of
// the thread's canary in a slot above the frame's objects, compared before each return and musttail call, and
// __stack_chk_fail called where it changed.
//
// The rules fixture's functions are those where the lowering's rules and LLVM's agree: under each option the functions
// whose code calls __stack_chk_fail, read from the object's relocations, are clang -O0's, by Tier 0 and by --tier1 (which
// leaves a guarded function to Tier 0). The differences docs/c-checker.md's "The stack protector" names are pinned on
// their own: clang's IR pads a union and an over-aligned struct with arrays of bytes that -fstack-protector counts, and a
// constant alloca under 8 bytes is guarded here. The smash fixture overflows a buffer of each frame's shape (a plain one,
// a realigned one, a variable-length array, an alloca, a result in registers and an x87 one, a musttail call, a
// preserving convention, a dirty upper state and an address-taken scalar): each build must end in glibc's "stack smashing
// detected" abort, and its untouched run print clang's line. Without an option, or with -fno-stack-protector last, the
// objects are the default's bytes.

static const char check_test_stack_protector_rules_source[] =
    "void use(void*);\n"
    "struct S { int a; char b[4]; };\n"
    "struct T { int a; int b; };\n"
    "struct U { char big[16]; };\n"
    "int f_char8(void) { char b[8]; use(b); return 0; }\n"
    "int f_char7(void) { char b[7]; use(b); return 0; }\n"
    "int f_int4(void) { int b[4]; use(b); return 0; }\n"
    "int f_int_noescape(void) { int b[4] = { 1, 2, 3, 4 }; return b[1]; }\n"
    "int f_addr(void) { int x; use(&x); return x; }\n"
    "int f_struct_arr(void) { struct S s; use(&s); return s.a; }\n"
    "int f_struct_noarr(void) { struct T t; use(&t); return t.a; }\n"
    "int f_struct_big(void) { struct U u; use(&u); return 0; }\n"
    "int f_vla(int n) { char b[n]; use(b); return 0; }\n"
    "int f_alloca(int n) { char* b = __builtin_alloca(n); use(b); return 0; }\n"
    "int f_alloca_const(void) { char* b = __builtin_alloca(8); use(b); return 0; }\n"
    "int f_plain(int x) { return x + 1; }\n"
    "int f_addr_noescape(int x) { int* p = &x; return *p; }\n"
    "int f_param_addr(int x) { use(&x); return 0; }\n"
    "int f_char_ptr_local(void) { char* p = 0; use(&p); return 0; }\n"
    "int f_struct_nested(void) { struct { struct S s; } w; use(&w); return 0; }\n"
    "int f_nested_big(void) { struct { int x; struct { char n[9]; } in; } s; use(&s); return 0; }\n"
    "int f_union_arr(void) { union { char c[2]; int i; } u; use(&u); return 0; }\n"
    "int f_union16(void) { union { int i; char c[16]; } u; use(&u); return 0; }\n"
    "int f_2d(void) { char b[4][4]; use(b); return 0; }\n"
    "int f_array_of_records(void) { struct { char c[8]; } s[2]; use(s); return 0; }\n"
    "int f_bool(void) { _Bool b[8]; use(b); return 0; }\n"
    "int f_uchar(void) { unsigned char b[8]; use(b); return 0; }\n"
    "int f_compound(void) { use((char[16]){ 0 }); return 0; }\n"
    "int f_string(void) { char s[] = \"hello world\"; use(s); return 0; }\n"
    "int f_volatile_array(void) { volatile char b[16]; b[0] = 1; return b[0]; }\n"
    "__attribute__((no_stack_protector)) int f_before(void);\n"
    "int f_before(void) { char b[16]; use(b); return 0; }\n"
    "__attribute__((no_stack_protector)) int f_exempt(int x) { char b[64]; use(b); return x; }\n"
    "int f_callee(int);\n"
    "int f_musttail(int x) { char b[16]; use(b); __attribute__((musttail)) return f_callee(x); }\n";

// Tier 0's splices of an always_inline callee: a caller is guarded where its callee is, but where its own
// no_stack_protector attribute exempts it, as clang's. (--tier1 doesn't expand a guarded callee: the call stays, and
// the callee keeps its own guard.)
static const char check_test_stack_protector_inlining_source[] =
    "void use(void*);\n"
    "static inline __attribute__((always_inline)) int inlined(void) { char b[16]; use(b); return b[0]; }\n"
    "__attribute__((no_stack_protector)) int f_exempt_caller(void) { return inlined(); }\n"
    "int f_caller(int x) { return inlined() + x; }\n"
    "int f_plain_caller(int x) { return x * 2; }\n";

// The functions whose guards differ from clang -O0's by design, and the guarded ones among them under -fstack-protector
// alone: clang's IR pads a union and an over-aligned struct with arrays of bytes (not guarded here), and a constant
// alloca under 8 bytes is (any ALLOCA is).
static const char check_test_stack_protector_differences_source[] =
    "void use(void*);\n"
    "int d_aligned(void) { struct { char c; } __attribute__((aligned(16))) s; use(&s); return 0; }\n"
    "int d_union_padding(void) { union { double d; char c[4]; int x[3]; } u; use(&u); return 0; }\n"
    "int d_alloca_small(void) { char* p = __builtin_alloca(7); use(p); return 0; }\n";

static const char check_test_stack_protector_smash_source[] =
    "#include <alloca.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "typedef struct { long a, b; } Pair;\n"
    "typedef int v8si __attribute__((vector_size(32)));\n"
    "static volatile int extra;\n"
    "__attribute__((noinline)) void fill(char* p, int n) { memset(p, 'A', (size_t)n); }\n"
    "__attribute__((noinline)) int plain(void) { char b[16]; fill(b, 16 + extra); return b[0]; }\n"
    "__attribute__((noinline)) int aligned64(void) { _Alignas(64) char b[100]; fill(b, 100 + extra); return b[99]; }\n"
    "__attribute__((noinline)) int vla(int n) { char b[n]; fill(b, n + 8 * extra); return b[0]; }\n"
    "__attribute__((noinline)) int with_alloca(int n) { char* b = alloca(n); fill(b, n + 8 * extra); return b[0]; }\n"
    "__attribute__((noinline)) Pair pair(void) { char b[16]; fill(b, 16 + extra); return (Pair){ b[0], 2 }; }\n"
    "__attribute__((noinline)) long double ld(void) { char b[16]; fill(b, 16 + extra); return b[0] * 1.5L; }\n"
    "int callee(int x);\n"
    "__attribute__((noinline)) int callee(int x) { return x + 7; }\n"
    "__attribute__((noinline)) int tail(int x) { char b[16]; fill(b, 16 + extra); __attribute__((musttail)) return callee(x + b[1]); }\n"
    "__attribute__((noinline, preserve_most)) int preserving(int x) { char b[16]; fill(b, 16 + extra); return x + b[4]; }\n"
    "__attribute__((noinline)) v8si vec(v8si a) { char b[16]; fill(b, 16 + extra); return a + b[5]; }\n"
    "__attribute__((noinline)) int addressed(void) { long x; fill((char*)&x, 8 + extra); return (int)x; }\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    extra = argc > 1 ? 64 : 0;\n"
    "    const int c = argc > 1 ? atoi(argv[1]) : -1;\n"
    "    v8si v = { 1, 2, 3, 4, 5, 6, 7, 8 };\n"
    "    if (c < 0)\n"
    "    {\n"
    "        Pair p = pair();\n"
    "        printf(\"%d %d %d %d %ld %ld %d %d %d %d %d\\n\", plain(), aligned64(), vla(40), with_alloca(40), p.a, p.b, (int)ld(), tail(1),\n"
    "               preserving(1), vec(v)[7], addressed());\n"
    "        return 0;\n"
    "    }\n"
    "    const int r = c == 0 ? plain() : c == 1 ? aligned64() : c == 2 ? vla(40) : c == 3 ? with_alloca(40) : c == 4 ? (int)pair().a : c == 5 ? (int)ld() :\n"
    "                  c == 6 ? tail(1) : c == 7 ? preserving(1) : c == 8 ? vec(v)[0] : addressed();\n"
    "    printf(\"not detected %d\\n\", r);\n"
    "    return 0;\n"
    "}\n";

#define BUSTER_CHECK_TEST_STACK_PROTECTOR_SMASHES 10u
#define BUSTER_CHECK_TEST_STACK_PROTECTOR_NAMES 64u

static int check_test_stack_protector_order(const void* left, const void* right)
{
    return strcmp(*(const char* const*)left, *(const char* const*)right);
}

// The functions of the object at `path` whose code calls __stack_chk_fail (a relocation against it inside the function's
// bytes), into `names` as each name and a space, sorted. Whether the object read.
static bool check_test_stack_protector_guarded(const char* path, char* names, size_t capacity)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    CheckTestElf elf;
    bool valid = bytes != NULL && check_test_elf_open(&elf, (String8){ .pointer = (char*)bytes, .length = length });
    const char* found[BUSTER_CHECK_TEST_STACK_PROTECTOR_NAMES];
    uint32_t count = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = &elf.sections[s];
        const bool relocations = section->sh_type == SHT_RELA && section->sh_entsize == sizeof(Elf64_Rela) && check_test_elf_range(elf.file, section->sh_offset, section->sh_size);
        const Elf64_Rela* records = relocations ? (const Elf64_Rela*)(bytes + section->sh_offset) : NULL;
        for (uint64_t r = 0; records != NULL && valid && r < section->sh_size / sizeof(Elf64_Rela); r += 1)
        {
            const uint64_t index = ELF64_R_SYM(records[r].r_info);
            const char* target = index < elf.symbol_count ? check_test_elf_string(elf.symbol_names, elf.symbol_names_size, elf.symbols[index].st_name) : NULL;
            const bool reports = target != NULL && strcmp(target, "__stack_chk_fail") == 0;
            const char* function = NULL;
            for (uint64_t i = 1; reports && function == NULL && i < elf.symbol_count; i += 1)
            {
                const Elf64_Sym* symbol = &elf.symbols[i];
                const bool inside = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && symbol->st_shndx == section->sh_info && records[r].r_offset >= symbol->st_value &&
                                    records[r].r_offset < symbol->st_value + symbol->st_size;
                function = inside ? check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name) : NULL;
            }
            bool known = function == NULL;
            for (uint32_t k = 0; !known && k < count; k += 1)
            {
                known = strcmp(found[k], function) == 0;
            }
            valid = known || count < BUSTER_CHECK_TEST_STACK_PROTECTOR_NAMES;
            if (!known && valid)
            {
                found[count++] = function;
            }
        }
    }
    qsort(found, count, sizeof(found[0]), check_test_stack_protector_order);
    size_t used = 0;
    names[0] = 0;
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        const int written = snprintf(names + used, capacity - used, "%s ", found[k]);
        valid = written > 0 && (size_t)written < capacity - used;
        used += valid ? (size_t)written : 0;
    }
    free(bytes);
    return valid;
}

// `compiler` (buster or clang) on `source` into `output`, with -c where `object`, -O0 for clang, the target's features
// that the smash fixture's vector needs, and `options` (up to 4, null-terminated). Whether it succeeded.
static bool check_test_stack_protector_build(const char* compiler, bool clang, const char* source, const char* output, bool object, const char* const* options)
{
    char* arguments[16];
    uint32_t count = 0;
    arguments[count++] = (char*)compiler;
    arguments[count++] = clang ? "-O0" : "-w";
    arguments[count++] = "-march=x86-64-v3";
    for (uint32_t k = 0; options != NULL && options[k] != NULL && k < 4; k += 1)
    {
        arguments[count++] = (char*)options[k];
    }
    if (object)
    {
        arguments[count++] = "-c";
    }
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const bool built = check_test_driver_spawn(arguments, &text) == 0;
    if (!built && text != NULL)
    {
        fprintf(stderr, "  %s: %.400s\n", compiler, text);
    }
    free(text);
    return built;
}

static void check_test_driver_stack_protector(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    char rules[512];
    char inlining[512];
    char differences[512];
    char smash[512];
    char object[512];
    char program[512];
    char reference[512];
    snprintf(rules, sizeof(rules), "%s/stack-protector-rules.c", directory);
    snprintf(inlining, sizeof(inlining), "%s/stack-protector-inlining.c", directory);
    snprintf(differences, sizeof(differences), "%s/stack-protector-differences.c", directory);
    snprintf(smash, sizeof(smash), "%s/stack-protector-smash.c", directory);
    snprintf(object, sizeof(object), "%s/stack-protector.o", directory);
    snprintf(program, sizeof(program), "%s/stack-protector", directory);
    snprintf(reference, sizeof(reference), "%s/stack-protector-clang", directory);
    const bool written = parse_test_write(rules, check_test_stack_protector_rules_source, sizeof(check_test_stack_protector_rules_source) - 1) &&
                         parse_test_write(inlining, check_test_stack_protector_inlining_source, sizeof(check_test_stack_protector_inlining_source) - 1) &&
                         parse_test_write(differences, check_test_stack_protector_differences_source, sizeof(check_test_stack_protector_differences_source) - 1) &&
                         parse_test_write(smash, check_test_stack_protector_smash_source, sizeof(check_test_stack_protector_smash_source) - 1);
    check_test_expect(test, written, "the stack protector's fixtures are written");
    static const char* const levels[3] = { "-fstack-protector", "-fstack-protector-strong", "-fstack-protector-all" };
    // Each option's guarded functions: clang -O0's, by Tier 0 and by --tier1 for the rules, by Tier 0 for the splices.
    for (uint32_t run = 0; written && run < 6; run += 1)
    {
        const uint32_t level = run % 3;
        const char* source = run < 3 ? rules : inlining;
        const char* const clang_options[] = { levels[level], NULL };
        char expected[2048] = { 0 };
        const bool referenced = check_test_stack_protector_build(clang, true, source, object, true, clang_options) &&
                                check_test_stack_protector_guarded(object, expected, sizeof(expected)) && expected[0] != 0;
        check_test_expect(test, referenced, "clang -O0 guards the fixture's functions");
        for (uint32_t tier = 0; referenced && tier < (run < 3 ? 2u : 1u); tier += 1)
        {
            const char* const options[] = { levels[level], tier != 0 ? "--tier1" : NULL, NULL };
            char actual[2048] = { 0 };
            const bool built = check_test_stack_protector_build(executable, false, source, object, true, options) &&
                               check_test_stack_protector_guarded(object, actual, sizeof(actual));
            char label[192];
            snprintf(label, sizeof(label), "%s%s: the %s's guarded functions are clang -O0's", levels[level], tier != 0 ? " --tier1" : "", run < 3 ? "rules" : "splices");
            check_test_expect(test, built && strcmp(actual, expected) == 0, label);
            if (built && strcmp(actual, expected) != 0)
            {
                fprintf(stderr, "  ours:    %s\n  clang's: %s\n", actual, expected);
            }
        }
    }
    // The differences by design: -fstack-protector's guards are the small alloca's alone, -fstack-protector-strong's all
    // three, as clang's.
    static const char* const designed[2] = { "d_alloca_small ", "d_aligned d_alloca_small d_union_padding " };
    for (uint32_t level = 0; written && level < 2; level += 1)
    {
        const char* const options[] = { levels[level], NULL };
        char actual[256] = { 0 };
        const bool built = check_test_stack_protector_build(executable, false, differences, object, true, options) &&
                           check_test_stack_protector_guarded(object, actual, sizeof(actual));
        check_test_expect(test, built && strcmp(actual, designed[level]) == 0,
                          level == 0 ? "-fstack-protector: clang's IR padding isn't a buffer, and a constant alloca under 8 bytes is guarded" :
                                       "-fstack-protector-strong: the differences' functions are all guarded");
        if (built && strcmp(actual, designed[level]) != 0)
        {
            fprintf(stderr, "  ours: %s\n", actual);
        }
    }
    // The smash fixture: its line without an overflow is clang's, and each case's overflow ends in glibc's abort.
    char expected_line[256] = { 0 };
    const char* const plain[] = { "-fno-stack-protector", NULL };
    const bool referenced = written && check_test_stack_protector_build(clang, true, smash, reference, false, plain) &&
                            check_test_driver_capture(reference, expected_line, sizeof(expected_line)) && expected_line[0] != 0;
    check_test_expect(test, referenced, "clang's build of the smash fixture runs");
    static const char* const builds[4][3] = {
        { "-fstack-protector-strong", NULL, NULL },
        { "-fstack-protector-all", NULL, NULL },
        { "-fstack-protector-strong", "--tier1", NULL },
        { "-fstack-protector-all", "-fno-omit-frame-pointer", NULL },
    };
    for (uint32_t b = 0; referenced && b < 4; b += 1)
    {
        const bool built = check_test_stack_protector_build(executable, false, smash, program, false, builds[b]);
        char label[192];
        snprintf(label, sizeof(label), "%s %s: the smash fixture builds and prints clang's line", builds[b][0], builds[b][1] != NULL ? builds[b][1] : "");
        char line[256] = { 0 };
        check_test_expect(test, built && check_test_driver_capture(program, line, sizeof(line)) && strcmp(line, expected_line) == 0, label);
        for (uint32_t c = 0; built && c < BUSTER_CHECK_TEST_STACK_PROTECTOR_SMASHES; c += 1)
        {
            char which[16];
            snprintf(which, sizeof(which), "%u", c);
            char* arguments[] = { program, which, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            snprintf(label, sizeof(label), "%s %s: smash case %u aborts on its guard", builds[b][0], builds[b][1] != NULL ? builds[b][1] : "", c);
            check_test_expect(test, status == -1 && text != NULL && strstr(text, "stack smashing detected") != NULL, label);
            free(text);
        }
    }
    // Without an option, or with -fno-stack-protector last, the objects are the default's bytes, and no function is
    // guarded.
    char other[512];
    snprintf(other, sizeof(other), "%s/stack-protector-other.o", directory);
    static const char* const off[3][3] = {
        { "-fstack-protector-all", "-fno-stack-protector", NULL },
        { "-fstack-protector-strong", "-fno-stack-protector", NULL },
        { "-fno-stack-protector", NULL, NULL },
    };
    const bool base = written && check_test_stack_protector_build(executable, false, rules, object, true, NULL);
    char none[64] = { 0 };
    check_test_expect(test, base && check_test_stack_protector_guarded(object, none, sizeof(none)) && none[0] == 0, "without the options, no function is guarded");
    for (uint32_t k = 0; base && k < 3; k += 1)
    {
        const bool built = check_test_stack_protector_build(executable, false, rules, other, true, off[k]);
        char label[192];
        snprintf(label, sizeof(label), "%s %s: the default's object", off[k][0], off[k][1] != NULL ? off[k][1] : "");
        check_test_expect(test, built && check_test_stack_clash_same(object, other), label);
    }
    unlink(rules);
    unlink(inlining);
    unlink(differences);
    unlink(smash);
    unlink(object);
    unlink(other);
    unlink(program);
    unlink(reference);
}
