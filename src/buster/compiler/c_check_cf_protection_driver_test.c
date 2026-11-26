// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// CET's protection (#3570), included by c_checker_driver_test.c after c_check_stack_protector_driver_test.c, whose
// builds (check_test_stack_protector_build) and object comparison (check_test_stack_clash_same) it takes.
// -fcf-protection, =full, =branch and =return mark the code as clang's does on x86-64 Linux (c_check_program_emit.c's
// check_program_branch_entries and check_program_emit_property, tier0.c's tier0_mark_branches): an endbr64 at the entry of
// every function an indirect call may reach, right after every call of a function that returns twice and at every label
// whose address the code takes, a notrack jmp for every jump table's jump, and the object's .note.gnu.property.
//
// The rules fixture's functions are those where clang -O0's marks and these agree: under -fcf-protection and =branch,
// each function's marks (whether its code starts with an endbr64, how many endbr64s and notrack jmps it has) are clang
// -O0's, by Tier 0 and by --tier1, which leaves a function with labels or __builtin_setjmp to Tier 0; and under =return,
// as clang's, none. Each spelling's note is clang's bytes. The program fixture, whose function pointers, setjmp,
// __builtin_setjmp, jump table and computed goto pass through the marks, prints clang's line from every build. nocf_check,
// part of a function's type under =branch, is refused there and only warned of under =return; so are __builtin_setjmp
// under =return, whose shadow stack fix the leaves don't have, and multiversioned functions under =branch, whose
// resolvers have no endbr64. Without an option, or with -fcf-protection=none last, the objects are the default's bytes.

#define BUSTER_CHECK_TEST_CF_PROTECTION_FUNCTIONS 64u

// A function's marks (check_test_cf_protection_marks): its name, whether its code starts with an endbr64, and how many
// endbr64s and notrack jmps through a register (3E, an optional REX.B, FF /4) its bytes have.
typedef struct CheckTestCfMarks CheckTestCfMarks;
struct CheckTestCfMarks
{
    char name[64];
    bool entry;
    uint32_t endbr;
    uint32_t notrack;
};

// The marks of the object's functions at `path` (`count` of them into `marks`), and its .note.gnu.property section's
// bytes into `note` (`*note_size` of them, 0 for none, at most 64). Whether the object read.
static bool check_test_cf_protection_marks(const char* path, CheckTestCfMarks* marks, uint32_t* count, uint8_t* note, uint64_t* note_size)
{
    uint64_t length = 0;
    uint8_t* bytes = check_test_driver_read_aligned(path, &length);
    CheckTestElf elf;
    bool valid = bytes != NULL && check_test_elf_open(&elf, (String8){ .pointer = (char*)bytes, .length = length });
    *count = 0;
    *note_size = 0;
    for (uint32_t s = 0; valid && s < elf.section_count; s += 1)
    {
        const Elf64_Shdr* section = &elf.sections[s];
        const char* name = check_test_elf_string(elf.section_names, elf.section_names_size, section->sh_name);
        const bool property = name != NULL && strcmp(name, ".note.gnu.property") == 0 && section->sh_size <= 64 && check_test_elf_range(elf.file, section->sh_offset, section->sh_size);
        if (property)
        {
            memcpy(note, bytes + section->sh_offset, section->sh_size);
            *note_size = section->sh_size;
        }
    }
    for (uint64_t i = 1; valid && i < elf.symbol_count; i += 1)
    {
        const Elf64_Sym* symbol = &elf.symbols[i];
        const char* name = check_test_elf_string(elf.symbol_names, elf.symbol_names_size, symbol->st_name);
        const Elf64_Shdr* section = symbol->st_shndx < elf.section_count ? &elf.sections[symbol->st_shndx] : NULL;
        const bool function = ELF64_ST_TYPE(symbol->st_info) == STT_FUNC && name != NULL && section != NULL && section->sh_type == SHT_PROGBITS &&
                              symbol->st_value <= section->sh_size && symbol->st_size <= section->sh_size - symbol->st_value &&
                              check_test_elf_range(elf.file, section->sh_offset, section->sh_size);
        valid = !function || (*count < BUSTER_CHECK_TEST_CF_PROTECTION_FUNCTIONS && strlen(name) < sizeof(marks[0].name));
        if (function && valid)
        {
            const uint8_t* code = bytes + section->sh_offset + symbol->st_value;
            const uint64_t size = symbol->st_size;
            CheckTestCfMarks* entry = &marks[(*count)++];
            *entry = (CheckTestCfMarks){ .entry = size >= 4 && memcmp(code, "\xf3\x0f\x1e\xfa", 4) == 0 };
            snprintf(entry->name, sizeof(entry->name), "%s", name);
            for (uint64_t at = 0; at + 3 <= size; at += 1)
            {
                const bool rex = at + 4 <= size && code[at + 1] == 0x41;
                const uint8_t* operation = code + at + (rex ? 2 : 1);
                entry->endbr += at + 4 <= size && memcmp(code + at, "\xf3\x0f\x1e\xfa", 4) == 0 ? 1 : 0;
                entry->notrack += code[at] == 0x3e && operation[0] == 0xff && (operation[1] & 0xf8) == 0xe0 ? 1 : 0;
            }
        }
    }
    free(bytes);
    return valid;
}

// Each of `ours`'s functions' marks against clang's function of its name (a function of clang's that Tier 0 spliced
// away has no marks of ours to compare), the differences on stderr. Whether every one matched.
static bool check_test_cf_protection_same(const CheckTestCfMarks* ours, uint32_t our_count, const CheckTestCfMarks* clangs, uint32_t clang_count)
{
    bool same = our_count != 0;
    for (uint32_t i = 0; i < our_count; i += 1)
    {
        const CheckTestCfMarks* found = NULL;
        for (uint32_t k = 0; found == NULL && k < clang_count; k += 1)
        {
            found = strcmp(ours[i].name, clangs[k].name) == 0 ? &clangs[k] : NULL;
        }
        // (A helper of ours, which clang's object has no function for, is called directly alone: no mark.)
        const bool helper = found == NULL && strncmp(ours[i].name, ".Lbuster_", 9) == 0 && !ours[i].entry && ours[i].endbr == 0 && ours[i].notrack == 0;
        const bool matched = helper || (found != NULL && found->entry == ours[i].entry && found->endbr == ours[i].endbr && found->notrack == ours[i].notrack);
        if (!matched)
        {
            fprintf(stderr, "  %s: ours entry=%d endbr64=%u notrack=%u, clang's %s entry=%d endbr64=%u notrack=%u\n", ours[i].name, ours[i].entry, ours[i].endbr,
                    ours[i].notrack, found != NULL ? "" : "(none)", found != NULL && found->entry, found != NULL ? found->endbr : 0, found != NULL ? found->notrack : 0);
        }
        same &= matched;
    }
    return same;
}

// buster on `source` with `options` (up to 4, null-terminated) and -c: its exit status, and its messages whether they
// hold `wanted` (or none is wanted).
static bool check_test_cf_protection_says(const char* executable, const char* source, const char* output, const char* const* options, int status, const char* wanted)
{
    char* arguments[12];
    uint32_t count = 0;
    arguments[count++] = (char*)executable;
    for (uint32_t k = 0; options[k] != NULL && k < 4; k += 1)
    {
        arguments[count++] = (char*)options[k];
    }
    arguments[count++] = "-c";
    arguments[count++] = "-o";
    arguments[count++] = (char*)output;
    arguments[count++] = (char*)source;
    arguments[count] = NULL;
    char* text = NULL;
    const int result = check_test_driver_spawn(arguments, &text);
    const bool says = result == status && (wanted == NULL || (text != NULL && strstr(text, wanted) != NULL));
    if (!says)
    {
        fprintf(stderr, "  status %d: %.400s\n", result, text != NULL ? text : "");
    }
    free(text);
    return says;
}

static void check_test_driver_cf_protection_policy(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                                   const CheckDriverProgramInput* inputs)
{
    (void)clang;
    char* policy[4];
    uint32_t status = 0;
    bool valid = check_test_driver_input_fields(test, inputs + 1, policy, BUSTER_ARRAY_LENGTH(policy)) &&
                 check_test_driver_input_count(policy[1], &status) && status <= 1;
    if (valid)
    {
        valid = strcmp(policy[0], "-fcf-protection") == 0 || strcmp(policy[0], "-fcf-protection=full") == 0 ||
                strcmp(policy[0], "-fcf-protection=branch") == 0 || strcmp(policy[0], "-fcf-protection=return") == 0 ||
                strcmp(policy[0], "-fcf-protection=none") == 0;
    }
    check_test_expect(test, valid, "CET's policy specifies a supported option and exit status");
    if (valid)
    {
        char source[512];
        char object[512];
        snprintf(source, sizeof(source), "%s/cf-protection-refused.c", directory);
        snprintf(object, sizeof(object), "%s/cf-protection.o", directory);
        const bool written = parse_test_write(source, inputs[0].bytes, inputs[0].length);
        const char* const options[] = { policy[0], NULL };
        const char* message = strcmp(policy[2], "-") == 0 ? NULL : policy[2];
        check_test_expect(test, written && check_test_cf_protection_says(executable, source, object, options, (int)status, message), policy[3]);
    }
}

static void check_test_driver_cf_protection_inputs(CheckTest* test, const char* clang, const char* executable, const char* directory,
                                                   const CheckDriverProgramInput* inputs)
{
    char rules[512];
    char program[512];
    char refused[512];
    char object[512];
    char other[512];
    char binary[512];
    char reference[512];
    snprintf(rules, sizeof(rules), "%s/cf-protection-rules.c", directory);
    snprintf(program, sizeof(program), "%s/cf-protection-program.c", directory);
    snprintf(refused, sizeof(refused), "%s/cf-protection-refused.c", directory);
    snprintf(object, sizeof(object), "%s/cf-protection.o", directory);
    snprintf(other, sizeof(other), "%s/cf-protection-other.o", directory);
    snprintf(binary, sizeof(binary), "%s/cf-protection", directory);
    snprintf(reference, sizeof(reference), "%s/cf-protection-clang", directory);
    const bool written = parse_test_write(rules, inputs[0].bytes, inputs[0].length) && parse_test_write(program, inputs[1].bytes, inputs[1].length);
    check_test_expect(test, written, "CET's fixtures are written");
    // Each spelling's marks and note: clang -O0's, by Tier 0 and by --tier1. (=return's and =full's rules leave
    // __builtin_setjmp out, whose leaves are refused there: below.)
    static const char* const spellings[3] = { "-fcf-protection", "-fcf-protection=branch", "-fcf-protection=return" };
    for (uint32_t s = 0; written && s < 3; s += 1)
    {
        const char* const clang_options[] = { spellings[s], s != 1 ? "-DNO_BUILTIN_SETJMP" : NULL, NULL };
        CheckTestCfMarks* expected = calloc(BUSTER_CHECK_TEST_CF_PROTECTION_FUNCTIONS, sizeof(CheckTestCfMarks));
        CheckTestCfMarks* actual = calloc(BUSTER_CHECK_TEST_CF_PROTECTION_FUNCTIONS, sizeof(CheckTestCfMarks));
        uint8_t clang_note[64] = { 0 };
        uint64_t clang_note_size = 0;
        uint32_t expected_count = 0;
        const bool referenced = expected != NULL && actual != NULL && check_test_stack_protector_build(clang, true, rules, object, true, clang_options) &&
                                check_test_cf_protection_marks(object, expected, &expected_count, clang_note, &clang_note_size) && expected_count != 0 &&
                                clang_note_size == 32;
        check_test_expect(test, referenced, "clang -O0 marks the fixture's functions");
        for (uint32_t tier = 0; referenced && tier < 2; tier += 1)
        {
            const char* const options[] = { spellings[s], s != 1 ? "-DNO_BUILTIN_SETJMP" : "-UNO_BUILTIN_SETJMP", tier != 0 ? "--tier1" : NULL, NULL };
            uint8_t note[64] = { 0 };
            uint64_t note_size = 0;
            uint32_t count = 0;
            const bool built = check_test_stack_protector_build(executable, false, rules, object, true, options) &&
                               check_test_cf_protection_marks(object, actual, &count, note, &note_size);
            char label[192];
            snprintf(label, sizeof(label), "%s%s: the rules' marks are clang -O0's", spellings[s], tier != 0 ? " --tier1" : "");
            check_test_expect(test, built && check_test_cf_protection_same(actual, count, expected, expected_count), label);
            snprintf(label, sizeof(label), "%s%s: the object's note is clang's", spellings[s], tier != 0 ? " --tier1" : "");
            check_test_expect(test, built && note_size == clang_note_size && memcmp(note, clang_note, note_size) == 0, label);
        }
        free(expected);
        free(actual);
    }
    // The program: clang's line from each build, Tier 0's, --tier1's and -fPIE's.
    char expected_line[256] = { 0 };
    const char* const plain[] = { NULL };
    const bool ran = written && check_test_stack_protector_build(clang, true, program, reference, false, plain) &&
                     check_test_driver_capture(reference, expected_line, sizeof(expected_line)) && expected_line[0] != 0;
    check_test_expect(test, ran, "clang's build of CET's program runs");
    static const char* const builds[4][4] = {
        { "-fcf-protection=branch", NULL, NULL, NULL },
        { "-fcf-protection=branch", "--tier1", NULL, NULL },
        { "-fcf-protection", "-DNO_BUILTIN_SETJMP", NULL, NULL },
        { "-fcf-protection", "-fPIE", "-DNO_BUILTIN_SETJMP", NULL },
    };
    for (uint32_t b = 0; ran && b < 4; b += 1)
    {
        char line[256] = { 0 };
        const bool built = check_test_stack_protector_build(executable, false, program, binary, false, builds[b]);
        char label[192];
        snprintf(label, sizeof(label), "%s %s %s: CET's program prints clang's line", builds[b][0], builds[b][1] != NULL ? builds[b][1] : "",
                 builds[b][1] != NULL && builds[b][2] != NULL ? builds[b][2] : "");
        check_test_expect(test, built && check_test_driver_capture(binary, line, sizeof(line)) && strcmp(line, expected_line) == 0, label);
    }
    // The refusals, each by name, and nocf_check's warning where the protection is the returns' alone.
    if (written)
    {
        check_test_driver_programs(test, clang, executable, directory, "cf-protection-policy", "program.c\tpolicy.expected", check_test_driver_cf_protection_policy);
    }
    // Without an option, or with -fcf-protection=none last, the objects are the default's bytes, without a note.
    static const char* const off[2][3] = {
        { "-fcf-protection", "-fcf-protection=none", NULL },
        { "-fcf-protection=none", NULL, NULL },
    };
    const bool base = written && check_test_stack_protector_build(executable, false, rules, object, true, NULL);
    for (uint32_t k = 0; base && k < 2; k += 1)
    {
        const bool built = check_test_stack_protector_build(executable, false, rules, other, true, off[k]);
        char label[192];
        snprintf(label, sizeof(label), "%s %s: the default's object", off[k][0], off[k][1] != NULL ? off[k][1] : "");
        check_test_expect(test, built && check_test_stack_clash_same(object, other), label);
    }
    unlink(rules);
    unlink(program);
    unlink(refused);
    unlink(object);
    unlink(other);
    unlink(binary);
    unlink(reference);
}

static void check_test_driver_cf_protection(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_programs(test, clang, executable, directory, "cf-protection", "rules.c\tprogram.c", check_test_driver_cf_protection_inputs);
}
