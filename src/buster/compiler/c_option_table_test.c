// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #1444's item 5: the option IDs (c_option_table.c) and the argument loops that compare them. test-checker's request
// tests run it (check_test_requests, so test-checker --requests and the default run's part that takes them):
//
// - the table: its spellings strictly in strcmp's order, each one's offset and length, and c_option_find against a
//   linear scan of the table, its oracle, on every spelling and its neighbours: one byte more ('=', 'x', a byte past
//   0x7f) or one less, its first letter's case flipped, with a value joined by '=', and arguments that are no option's;
//   c_option_starts and c_option_is against strncmp and strcmp on the same texts;
// - every option form, through the driver's routing as main's (main_source_command_reusable, in a child): joined and
//   separate values (-D, -U, -I, -isystem, -iquote, -idirafter, -include, -o, -x, -MF, -MT, -MQ, -target and
//   --target=, --lanes, --worker-budget, --id-order), options that take a value missing it, -- and the inputs past it,
//   a value spelled as an option (-o -M, -o --version, -L --version), the forwarded -W forms (-Wl,, -Wa,, -Wp, and the
//   malformed -Wl and -Wl=), the -W options the driver reads (-Wreturn-type's family, -w), aliases (-mpascal-strings),
//   the families whose last option decides, the predefined macros that options set (-E -dM), the information probes,
//   and the refusals' and errors' messages, each case's exit status, output and first errors as they were before the
//   IDs (#1444): the texts are the strcmp chains' output.
#ifndef BUSTER_C_OPTION_TABLE_TEST_INCLUDED
#define BUSTER_C_OPTION_TABLE_TEST_INCLUDED

int main_source_command_reusable(int argc, char** argv);

// The oracle: the ID whose spelling is the `length` bytes at `text`, by a linear scan with memcmp.
static COption option_test_scan(const char* text, size_t length)
{
    COption result = C_OPTION_NONE;
    for (uint32_t id = C_OPTION_NONE + 1; id < C_OPTION_COUNT; id += 1)
    {
        const bool same = c_option_length((COption)id) == length && memcmp(c_option_spelling((COption)id), text, length) == 0;
        result = same ? (COption)id : result;
    }
    return result;
}

// c_option_find, c_option_exact, c_option_starts and c_option_is on `text`, against the oracle and libc.
static bool option_test_lookup(const char* text)
{
    const size_t length = strlen(text);
    const char* equals = strchr(text, '=');
    const bool option = text[0] == '-';
    const COption exact = option ? option_test_scan(text, length) : C_OPTION_NONE;
    const COption joined = option && equals != NULL ? option_test_scan(text, (size_t)(equals - text) + 1) : C_OPTION_NONE;
    const COptionMatch match = c_option_find(text);
    bool result = match.exact == exact && match.joined == joined && c_option_exact(text) == exact;
    static const char* const literals[] = { "-", "--", "-f", "-fno-", "-fno-builtin-", "-Wl,", "-mno-", "-fsyntax-only", "--time-stages", "-x" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(literals); i += 1)
    {
        result &= c_option_starts(text, literals[i]) == (strncmp(text, literals[i], strlen(literals[i])) == 0);
        result &= c_option_is(text, literals[i]) == (strcmp(text, literals[i]) == 0);
    }
    return result;
}

static void check_test_option_lookup(CheckTest* test)
{
    bool ordered = true;
    bool measured = true;
    for (uint32_t id = C_OPTION_NONE + 1; id < C_OPTION_COUNT; id += 1)
    {
        const char* spelling = c_option_spelling((COption)id);
        measured &= strlen(spelling) == c_option_length((COption)id) && spelling[0] == '-';
        ordered &= id == C_OPTION_NONE + 1 || strcmp(c_option_spelling((COption)(id - 1)), spelling) < 0;
        if (!ordered && id > C_OPTION_NONE + 1 && strcmp(c_option_spelling((COption)(id - 1)), spelling) >= 0)
        {
            printf("option table: '%s' must come before '%s' (BUSTER_C_OPTIONS is sorted as strcmp orders)\n", spelling, c_option_spelling((COption)(id - 1)));
        }
    }
    check_test_expect(test, ordered, "option table: the spellings strictly in strcmp's order");
    check_test_expect(test, measured && c_option_length(C_OPTION_NONE) == 0, "option table: each spelling's offset and length");
    uint32_t wrong = 0;
    uint32_t texts = 0;
    for (uint32_t id = C_OPTION_NONE + 1; id < C_OPTION_COUNT; id += 1)
    {
        const char* spelling = c_option_spelling((COption)id);
        const size_t length = strlen(spelling);
        char text[128];
        // The spelling, one byte more or less, its first letter's case flipped, and with a joined value.
        static const char* const suffixes[] = { "", "=", "x", "\x80", "=1", "=a=b", "-" };
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(suffixes); s += 1)
        {
            snprintf(text, sizeof(text), "%s%s", spelling, suffixes[s]);
            wrong += !option_test_lookup(text);
            texts += 1;
        }
        snprintf(text, sizeof(text), "%.*s", (int)length - 1, spelling);
        wrong += !option_test_lookup(text);
        snprintf(text, sizeof(text), "%s", spelling);
        for (uint32_t i = 1; text[i] != 0; i += 1)
        {
            if ((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z'))
            {
                text[i] ^= 0x20;
                break;
            }
        }
        wrong += !option_test_lookup(text);
        texts += 2;
    }
    static const char* const others[] = { "", "-", "--", "---", "empty.c", "/dev/null", "x-fsyntax-only", "=", "-=", "--=", "-\x80", "-\xff", "-march", "-std" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(others); i += 1)
    {
        wrong += !option_test_lookup(others[i]);
        texts += 1;
    }
    check_test_expect(test, wrong == 0 && texts > 3000, "option table: c_option_find, c_option_starts and c_option_is against a linear scan, strncmp and strcmp");
    if (wrong != 0)
    {
        printf("option table: %u of %u texts looked up wrongly\n", wrong, texts);
    }
}

// One command line of the driver's: `arguments` after the program's name, its exit status, a text its standard output
// must contain (`output`) and one it must not (`absent`), how its standard error must start (`errors`: "" for none),
// and a file it must write whole (`file`, `text`); a null field isn't checked.
typedef struct OptionTestCase OptionTestCase;
struct OptionTestCase
{
    const char* arguments[16];
    int status;
    const char* output;
    const char* absent;
    const char* errors;
    const char* file;
    const char* text;
};

#define BUSTER_OPTION_TEST_USAGE_E "usage: compiler -E/-M/-MM "
#define BUSTER_OPTION_TEST_USAGE_CHECK "usage: compiler [check] "
#define BUSTER_OPTION_TEST_TIER1_PASSES "error: --tier1-passes= takes a comma-separated list of +NAME and -NAME, NAME being sccp, dce, cfg, merge, licm, sroa, gvn, inline or all\n"

static const OptionTestCase option_test_cases[] = {
    // Joined and separate values, each family's order.
    { { "-E", "-P", "m.c", "-DX=1", "-D", "Y=2" }, 0, "1 2 FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-DX=1", "-UX" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-DX=1", "-U", "X" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "inc.c", "-Iinc", "-isystem", "sys", "-iquote", "quote", "-idirafter", "after", "-include", "pre.h", "-DX=6" }, 0, "1 2 3 4 5 6\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "inc.c", "-I", "inc", "-isystem", "sys", "-iquote", "quote", "-idirafter", "after", "-include", "pre.h" }, 0, "1 2 3 4 5 X\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "-x", "c", "m.txt" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "-xc", "m.txt" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-o", "out.i" }, 0, NULL, "FROM_INC", "", "out.i", "X Y FROM_INC\n" },
    { { "-M", "m.c", "-MF", "dep1" }, 0, NULL, NULL, "", "dep1", "m.o: m.c\n" },
    { { "-M", "m.c", "-MFdep2", "-MT", "tgt", "-MQ", "q$" }, 0, NULL, NULL, "", "dep2", "tgt q$$: m.c\n" },
    { { "-M", "m.c", "-MTt2", "-MQq2" }, 0, "t2 q2: m.c\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-MD", "-MF", "dep3" }, 0, "X Y FROM_INC\n", NULL, "", "dep3", "m.o: m.c\n" },
    { { "-E", "-P", "m.c", "-MMD", "-MP", "-MF", "dep4", "-MT", "x" }, 0, "X Y FROM_INC\n", NULL, "", "dep4", "x: m.c\n" },
    // Standard input, and the inputs past --.
    { { "-E", "-P", "-" }, 0, NULL, "FROM_INC", "", NULL, NULL },
    { { "-E", "-P", "--", "m.c" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--", "-fbuster-unknown" }, 0, NULL, NULL, "warning: -fbuster-unknown: linker input unused because linking was not requested\n", NULL, NULL },
    // A value spelled as an option is a value: -M isn't -E's routing, --version isn't a probe, -c isn't -c.
    { { "-o", "-M", "-fsyntax-only", "ret.c" }, 0, NULL, ":", "ret.c:1:15: warning: non-void function does not return a value [-Wreturn-type]\n", NULL, NULL },
    { { "-o", "--version", "-fsyntax-only", "empty.c" }, 0, NULL, "buster", "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-L", "--version" }, 0, NULL, "buster", "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-Xlinker", "-c" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-rpath", "-c" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-rpath", "-M" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-o", "-fsyntax-only" }, 0, NULL, NULL, "", NULL, NULL },
    // The information probes.
    { { "--version", "-target", "x86_64-unknown-linux-gnu" }, 0, "buster (development build)\nTarget: x86_64-unknown-linux-gnu\n", NULL, "", NULL, NULL },
    { { "--version", "--target=amd64-linux-gnu" }, 0, "Target: amd64-unknown-linux-gnu\n", NULL, "", NULL, NULL },
    { { "-dumpmachine", "-target", "x86_64-unknown-linux-gnu" }, 0, "x86_64-unknown-linux-gnu\n", NULL, "", NULL, NULL },
    { { "-dumpversion" }, 0, "0.0.0\n", NULL, "", NULL, NULL },
    { { "-dumpfullversion" }, 0, "0.0.0\n", NULL, "", NULL, NULL },
    { { "-print-file-name=nonexistent.xyz" }, 0, "nonexistent.xyz\n", NULL, "", NULL, NULL },
    { { "-print-prog-name=buster-nonexistent-program" }, 0, "buster-nonexistent-program\n", NULL, "", NULL, NULL },
    { { "-v" }, 0, NULL, NULL, "buster (development build)\nTarget: ", NULL, NULL },
    { { "--version", "-target" }, 1, NULL, NULL, "error: unsupported or missing target for driver information\n", NULL, NULL },
    // The predefined macros the options set, and the families whose last option decides.
    { { "-E", "-dM", "empty.c", "-std=c89" }, 0, "#define __STRICT_ANSI__ 1\n", "__STDC_VERSION__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-std=gnu17", "-ansi" }, 0, "#define __STRICT_ANSI__ 1\n", "__STDC_VERSION__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fsigned-char", "-funsigned-char" }, 0, "#define __CHAR_UNSIGNED__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-funsigned-char", "-fsigned-char" }, 0, NULL, "__CHAR_UNSIGNED__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-O2" }, 0, "#define __OPTIMIZE__ 1\n", "__NO_INLINE__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-Os" }, 0, "#define __OPTIMIZE_SIZE__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-Ofast" }, 0, "#define __FAST_MATH__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fpic" }, 0, "#define __PIC__ 1\n", "__PIE__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fPIE", "-fpie" }, 0, "#define __PIE__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fPIC", "-fno-pic" }, 0, NULL, "__PIC__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fstack-protector-strong" }, 0, "#define __SSP_STRONG__ 2\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fstack-protector-all", "-fno-stack-protector" }, 0, NULL, "__SSP", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-march=znver5", "-mno-avx512f" }, 0, "#define __znver5 1\n", "__AVX512F__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-msse4" }, 0, "#define __SSE4_2__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fshort-wchar" }, 0, "#define __WCHAR_MAX__ 65535\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-ffast-math", "-fno-finite-math-only" }, 0, "#define __FINITE_MATH_ONLY__ 0\n", "__FAST_MATH__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fopenmp" }, 0, "#define _OPENMP 202011\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fsanitize=address" }, 0, "#define __SANITIZE_ADDRESS__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fcf-protection" }, 0, "#define __CET__ 3\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fcf-protection=none" }, 0, NULL, "__CET__", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fno-dwarf2-cfi-asm" }, 0, NULL, "__GCC_HAVE_DWARF2_CFI_ASM", "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-fexceptions" }, 0, "#define __EXCEPTIONS 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-mapxf" }, 0, "#define __APX_F__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-mcmodel=large" }, 0, "#define __code_model_large__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-dM", "empty.c", "-mpascal-strings" }, 0, "#define __PASCAL_STRINGS__ 1\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-mretpoline", "-mno-retpoline" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-fmodules", "-fno-modules" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    // The forwarded -W forms.
    { { "-E", "-P", "m.c", "-Wl,--as-needed", "-Wa,-x", "-Wp,-y" }, 0, "X Y FROM_INC\n", NULL, "", NULL, NULL },
    { { "-E", "-P", "m.c", "-Wl" }, 1, NULL, NULL, "error: malformed linker option '-Wl' (use '-Wl,OPTION')\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-Wl=x" }, 1, NULL, NULL, "error: malformed linker option '-Wl=x' (use '-Wl,OPTION')\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-Wl" }, 1, NULL, NULL, "error: missing argument to linker option '-Wl'\n", NULL, NULL },
    // Options without their values.
    { { "-E", "-P", "m.c", "-x" }, 1, NULL, NULL, "error: unsupported or missing input language ''\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-MF" }, 1, NULL, NULL, "error: missing argument to '-MF'\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-Xlinker" }, 1, NULL, NULL, "error: missing argument to linker option '-Xlinker'\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-rpath" }, 1, NULL, NULL, "error: missing argument to linker option '-rpath'\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-include" }, 1, NULL, NULL, "unknown argument: -include\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-I" }, 1, NULL, NULL, BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-E", "-P", "m.c", "-I-" }, 1, NULL, NULL, "error: '-I-' not supported, please use -iquote instead\n" BUSTER_OPTION_TEST_USAGE_E, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-D" }, 1, NULL, NULL, BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    // The preprocessor's refusals and errors.
    { { "-E", "-P", "m.c", "-ferror-limit" }, 1, NULL, NULL, "error: unknown argument '-ferror-limit'; use '-ferror-limit=N'\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-ferror-limit=0x" }, 1, NULL, NULL, "error: invalid integral value '0x' in '-ferror-limit 0x'\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-gstabs+" }, 1, NULL, NULL, "error: unsupported option '-gstabs+'\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-mbuster-unknown" }, 1, NULL, NULL, "error: unknown or unsupported option '-mbuster-unknown' for x86-64 Linux\n", NULL, NULL },
    { { "-E", "-P", "m.c", "--buster-unknown" }, 1, NULL, NULL, "unknown argument: --buster-unknown\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-fpreprocessed" }, 1, NULL, NULL, "error: unsupported option '-fpreprocessed' (already-preprocessed input is not read that way)\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-fmodules" }, 1, NULL, NULL, "error: unsupported option '-fmodules' (clang's modules, which buster doesn't have)\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-fmodule-name=x" }, 1, NULL, NULL, "error: unsupported option '-fmodule-name=x' (clang's modules, which buster doesn't have)\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-fbuiltin-module-map" }, 1, NULL, NULL, "error: unsupported option '-fbuiltin-module-map' (clang's modules, which buster doesn't have)\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-fexec-charset=latin1" }, 1, NULL, NULL, "error: invalid value 'latin1' in '-fexec-charset=latin1'\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-mcpu=x" }, 1, NULL, NULL, "error: unsupported option '-mcpu=x' (it changes what clang predefines or compiles, which buster doesn't follow)\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-fptrauth-calls" }, 1, NULL, NULL, "error: unsupported option '-fptrauth-calls' (it changes what clang predefines or compiles, which buster doesn't follow)\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-mfunction-return=bogus" }, 1, NULL, NULL, "error: invalid value 'bogus' in '-mfunction-return=bogus'\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-mrecip=bogus" }, 1, NULL, NULL, "error: invalid value 'bogus' in '-mrecip=bogus'\n", NULL, NULL },
    { { "-E", "-P", "m.c", "-mseses", "-mlvi-hardening" }, 1, NULL, NULL, "error: invalid argument 'mlvi-hardening' not allowed with 'mseses'\n", NULL, NULL },
    // The checker's driver: the -W options it reads, its own options with their values, and the request's refusals.
    { { "-fsyntax-only", "ret.c" }, 0, NULL, NULL, "ret.c:1:15: warning: non-void function does not return a value [-Wreturn-type]\n", NULL, NULL },
    { { "-fsyntax-only", "ret.c", "-Wno-return-type" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "ret.c", "-Werror=return-type" }, 1, NULL, NULL, "ret.c:1:15: error: non-void function does not return a value [-Werror,-Wreturn-type]\n", NULL, NULL },
    { { "-fsyntax-only", "ret.c", "-Werror=return-type", "-Wno-error=return-type" }, 0, NULL, NULL, "ret.c:1:15: warning: non-void function does not return a value [-Wreturn-type]\n", NULL, NULL },
    { { "-fsyntax-only", "ret.c", "-w" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "--intel", "empty.c" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--lanes", "2" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--lanes=2" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--lanes=0" }, 1, NULL, NULL, BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--lanes" }, 1, NULL, NULL, BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--worker-budget", "1", "--id-order", "3" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--worker-budget=1", "--id-order=3" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--id-order=x" }, 1, NULL, NULL, BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--units-memory=0" }, 1, NULL, NULL, BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1-schedule=bogus" }, 1, NULL, NULL, "error: --tier1-schedule= takes none, post or pre\n" BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    // #139's --tier1-passes=: signed names in turn; an unsigned, unknown or empty item and a trailing comma are refused.
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=-sccp,-dce,-cfg,-merge,+licm,+sroa,+gvn,-inline" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=+all,-all,+sccp" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=sccp" }, 1, NULL, NULL, BUSTER_OPTION_TEST_TIER1_PASSES BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=+bogus" }, 1, NULL, NULL, BUSTER_OPTION_TEST_TIER1_PASSES BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=+licm," }, 1, NULL, NULL, BUSTER_OPTION_TEST_TIER1_PASSES BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=" }, 1, NULL, NULL, BUSTER_OPTION_TEST_TIER1_PASSES BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1", "--tier1-passes=+" }, 1, NULL, NULL, BUSTER_OPTION_TEST_TIER1_PASSES BUSTER_OPTION_TEST_USAGE_CHECK, NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--tier1-passes=+licm" }, 1, NULL, NULL, "error: --tier1-passes= needs --tier1\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "--helper-forms=bogus" }, 1, NULL, NULL, "error: --helper-forms= takes a comma-separated list of fence, atomic-access, atomic-rmw and asm, or all\n", NULL, NULL },
    { { "-fsyntax-only", "-target", "bogus", "empty.c" }, 1, NULL, NULL, "error: unsupported target 'bogus': buster predefines for x86_64-linux-gnu only\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-mpascal-strings" }, 1, NULL, NULL, "error: unsupported code generation option '-mpascal-strings'\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-mno-red-zone" }, 1, NULL, NULL, "error: unsupported code generation option '-mno-red-zone'\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-fstack-protector-strong", "-fno-stack-protector" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-c", "empty.c", "-o", "out.o", "-fstack-protector-strong" }, 1, NULL, NULL, "error: unsupported code generation option '-fstack-protector-strong'\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-fno-builtin-memcpy" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-fno-builtin-" }, 1, NULL, NULL, "error: unsupported code generation option '-fno-builtin-'\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-ftrivial-auto-var-init=uninitialized" }, 0, NULL, NULL, "", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-ftrivial-auto-var-init=zero" }, 1, NULL, NULL, "error: unsupported code generation option '-ftrivial-auto-var-init=zero'\n", NULL, NULL },
    { { "-c", "empty.c", "-o", "out.o", "-fvisibility=hidden" }, 1, NULL, NULL, "error: unsupported code generation option '-fvisibility=hidden'\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-fvisibility" }, 1, NULL, NULL, "error: unsupported code generation option '-fvisibility'\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-fshort-enums" }, 1, NULL, NULL, "error: unsupported option '-fshort-enums' (the checker does not implement its ABI layout)\n", NULL, NULL },
    { { "-fsyntax-only", "empty.c", "-fpack-struct=x" }, 1, NULL, NULL, "error: unsupported option '-fpack-struct=x' (the checker does not implement its ABI layout)\n", NULL, NULL },
};

// The text of the file at `path`, malloc'd (the caller's to free), or null.
static char* option_test_read(const char* path)
{
    char* result = NULL;
    FILE* file = fopen(path, "rb");
    if (file != NULL)
    {
        size_t used = 0;
        size_t capacity = 4096;
        result = malloc(capacity + 1);
        size_t read = 0;
        while (result != NULL && (read = fread(result + used, 1, capacity - used, file)) > 0)
        {
            used += read;
            if (used == capacity)
            {
                capacity *= 2;
                result = realloc(result, capacity + 1);
            }
        }
        if (result != NULL)
        {
            result[used] = 0;
        }
        fclose(file);
    }
    return result;
}

// One case in a child, in `directory`, as main routes a source command (main_source_command_reusable), with standard
// input from /dev/null and its output and errors in files: whether it did as `expected` says.
static bool option_test_case(const char* directory, const OptionTestCase* expected)
{
    char output_path[PATH_MAX];
    char errors_path[PATH_MAX];
    char file_path[PATH_MAX];
    snprintf(output_path, sizeof(output_path), "%s/.output", directory);
    snprintf(errors_path, sizeof(errors_path), "%s/.errors", directory);
    snprintf(file_path, sizeof(file_path), "%s/%s", directory, expected->file != NULL ? expected->file : ".none");
    unlink(file_path);
    fflush(NULL);
    const pid_t child = fork();
    if (child == 0)
    {
        const int in = open("/dev/null", O_RDONLY);
        const int out = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        const int err = open(errors_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (in < 0 || out < 0 || err < 0 || dup2(in, STDIN_FILENO) < 0 || dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0 || chdir(directory) != 0)
        {
            _exit(126);
        }
        char* arguments[BUSTER_ARRAY_LENGTH(expected->arguments) + 2] = { "compiler" };
        int count = 1;
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(expected->arguments) && expected->arguments[i] != NULL; i += 1)
        {
            arguments[count] = (char*)expected->arguments[i];
            count += 1;
        }
        const int code = main_source_command_reusable(count, arguments);
        fflush(NULL);
        _exit(code);
    }
    int status = 0;
    pid_t waited = -1;
    do
    {
        waited = child > 0 ? waitpid(child, &status, 0) : -1;
    }
    while (waited < 0 && errno == EINTR);
    char* output = option_test_read(output_path);
    char* errors = option_test_read(errors_path);
    char* file = expected->file != NULL ? option_test_read(file_path) : NULL;
    bool result = waited == child && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == expected->status && output != NULL && errors != NULL;
    result = result && (expected->output == NULL || strstr(output, expected->output) != NULL);
    result = result && (expected->absent == NULL || strstr(output, expected->absent) == NULL);
    result = result && (expected->errors == NULL || (expected->errors[0] == 0 ? errors[0] == 0 : strncmp(errors, expected->errors, strlen(expected->errors)) == 0));
    result = result && (expected->file == NULL || (file != NULL && strcmp(file, expected->text) == 0));
    if (!result)
    {
        printf("option forms: status %d, output \"%.200s\", errors \"%.300s\" for:", WIFEXITED(status) ? WEXITSTATUS(status) : -1, output != NULL ? output : "",
               errors != NULL ? errors : "");
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(expected->arguments) && expected->arguments[i] != NULL; i += 1)
        {
            printf(" %s", expected->arguments[i]);
        }
        printf("\n");
    }
    free(output);
    free(errors);
    free(file);
    unlink(file_path);
    return result;
}

static void check_test_option_forms(CheckTest* test)
{
    char directory[] = "/tmp/buster-options-XXXXXX";
    const bool made = mkdtemp(directory) != NULL;
    static const struct
    {
        const char* path;
        const char* text;
    } fixtures[] = {
        { "empty.c", "" },
        { "m.c", "X Y FROM_INC\n" },
        { "m.txt", "X Y FROM_INC\n" },
        { "ret.c", "int f(void) { }\n" },
        { "pre.h", "#define PRE 5\n" },
        { "inc.c", "#include \"h.h\"\n#include <s.h>\n#include \"q.h\"\n#include <a.h>\nFROM_INC FROM_SYS FROM_QUOTE FROM_AFTER PRE X\n" },
        { "inc/h.h", "#define FROM_INC 1\n" },
        { "sys/s.h", "#define FROM_SYS 2\n" },
        { "quote/q.h", "#define FROM_QUOTE 3\n" },
        { "after/a.h", "#define FROM_AFTER 4\n" },
    };
    static const char* const subdirectories[] = { "inc", "sys", "quote", "after" };
    bool ready = made;
    char path[PATH_MAX];
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(subdirectories); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, subdirectories[i]);
        ready = mkdir(path, 0700) == 0;
    }
    for (uint32_t i = 0; ready && i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, fixtures[i].path);
        ready = parse_test_write(path, fixtures[i].text, strlen(fixtures[i].text));
    }
    check_test_expect(test, ready, "option forms: the fixtures' directory and files");
    uint32_t wrong = 0;
    for (uint32_t c = 0; ready && c < BUSTER_ARRAY_LENGTH(option_test_cases); c += 1)
    {
        wrong += !option_test_case(directory, &option_test_cases[c]);
    }
    check_test_expect(test, ready && wrong == 0, "option forms: each command line's status, output and errors");
    static const char* const leftovers[] = { ".output", ".errors", "out.o", "-fsyntax-only" };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(leftovers); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, leftovers[i]);
        unlink(path);
    }
    for (uint32_t i = BUSTER_ARRAY_LENGTH(fixtures); i > 0; i -= 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, fixtures[i - 1].path);
        unlink(path);
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(subdirectories); i += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, subdirectories[i]);
        rmdir(path);
    }
    if (made)
    {
        rmdir(directory);
    }
}

static void check_test_option_table(CheckTest* test)
{
    check_test_option_lookup(test);
    check_test_option_forms(test);
}

#endif
