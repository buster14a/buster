// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The profiling tools' tests, for the test-profile command:
//
//   build/compiler test-profile
//
// main.c includes this file after profile.c and calls run_profile_tests for the command. Each tool runs on texts in
// objdump's, clang's and perf script's formats, and its report must be exactly the expected one, with the expected
// success:
//   - hot-loops: a function with a loop nested in another, an indirect jump and samples of another function, of a
//     symbol whose name only starts with the function's, and of none; the loops' ranking, the smallest loop with half
//     the samples and its instructions; --loops 1; a function that isn't there; and a disassembly out of order;
//   - label-spills: spills and reloads inside the function (whose label has clang's comment), folded ones among them,
//     labeled in order, and none of another function's, one whose name starts with the function's among them; source
//     paths and another comment that hold the words, which aren't spills; the static function renamed everywhere, and
//     a global one not; a function that isn't there;
//   - spill-report: label-spills' output with samples at labels, after them, at a label it doesn't have, in another
//     function and in other copies of the static function, before its labels too (#236's review): the slots, their widths and the source locations of their hottest spills and reloads; labels that aren't
//     numbered in order;
//   - branch-misses: records with M, P and neither, a field that isn't one and one of a branch at 0, with the load bias
//     and the disassembly's instructions, and an address the disassembly doesn't have; then 10,000 branches, which grow
//     the table, without a disassembly;
//   - ab: the rotating order, then runs of programs that succeed, fail and don't exist, with two argument lists, and
//     through a lock (env) pinned to an allowed CPU, with every heading and failure in the output.
//
// The exit status is 1 if a case fails.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// The branches of branch-misses' growth check: more than the table's first capacity holds.
#define BUSTER_PROFILE_TEST_BRANCHES 10000

static const char profile_test_disassembly[] = "\n"
                                               "bin:     file format elf64-x86-64\n"
                                               "\n"
                                               "\n"
                                               "Disassembly of section .text:\n"
                                               "\n"
                                               "0000000000001000 <other>:\n"
                                               "    1000:\tret\n"
                                               "\n"
                                               "0000000000001010 <f>:\n"
                                               "    1010:\tpush   %rbp\n"
                                               "    1011:\tmov    %rsp,%rbp\n"
                                               "    1014:\txor    %eax,%eax\n"
                                               "    1016:\tadd    $0x1,%eax\n"
                                               "    1019:\tvpaddd %zmm1,%zmm0,%zmm0\n"
                                               "    101f:\tcmp    %ecx,0x8(%rsp)\n"
                                               "    1023:\tjne    1016 <f+0x6>\n"
                                               "    1025:\tdec    %edx\n"
                                               "    1027:\tjne    1014 <f+0x4>\n"
                                               "    1029:\tjmp    *%rax\n"
                                               "    102b:\tlea    0x10(%rip),%rax        # 1040 <g>\n"
                                               "    1032:\tret\n"
                                               "\n"
                                               "0000000000002000 <f>:\n"
                                               "    2000:\tpush   %rbp\n"
                                               "    2001:\tret\n";

static const char profile_test_samples[] = "    401016 f+0x6 (/bin/x+0x1016)\n"
                                           "    401016 f+0x6 (/bin/x+0x1016)\n"
                                           "    401016 f+0x6 (/bin/x+0x1016)\n"
                                           "    401019 f+0x9 (/bin/x+0x1019)\n"
                                           "    40101a f+0xa (/bin/x+0x101a)\n"
                                           "    401019 f+0x9 (/bin/x+0x1019)\n"
                                           "    401019 f+0x9 (/bin/x+0x1019)\n"
                                           "    401023 f+0x13 (/bin/x+0x1023)\n"
                                           "    401023 f+0x13 (/bin/x+0x1023)\n"
                                           "    401023 f+0x13 (/bin/x+0x1023)\n"
                                           "    401025 f+0x15 (/bin/x+0x1025)\n"
                                           "    401025 f+0x15 (/bin/x+0x1025)\n"
                                           "    401010 f+0x0 (/bin/x+0x1010)\n"
                                           "    401032 f+0x22 (/bin/x+0x1032)\n"
                                           "    401000 other+0x0 (/bin/x+0x1000)\n"
                                           "    402000 f+0x0 (/bin/x+0x2000)\n"
                                           "    402001 f+0x1 (/bin/x+0x2001)\n"
                                           "    400050 f+0x0 (/bin/x+0x50)\n"
                                           "    402000 ff+0x6\n"
                                           "    7f0000 [unknown]\n";

static const char profile_test_hot_loops[] = "hot-loops: 14 samples in f (12 instructions, 2 loops), the copy at 1010, the busiest of 2 (2 samples in the others), 1 samples at file offsets outside its copies\n"
                                             "loops by samples:\n"
                                             "  1014-1027: 7 instructions, 1 stack references, 85.7% of the samples\n"
                                             "  1016-1023: 4 instructions, 1 stack references, 71.4% of the samples\n"
                                             "the smallest loop with half the samples:\n"
                                             "  1016-1023: 4 instructions, 1 stack references, 71.4% of the samples\n"
                                             "         3  1016  add    $0x1,%eax\n"
                                             "         4  1019  vpaddd %zmm1,%zmm0,%zmm0\n"
                                             "         0  101f  cmp    %ecx,0x8(%rsp)\n"
                                             "         3  1023  jne    1016 <f+0x6>\n";

static const char profile_test_assembly[] = "\t.file\t0 \"/src\" \"ReduceSpills.c\"\n"
                                            "\t.text\n"
                                            "\t.type\tf,@function\n"
                                            "f:                                      # @f\n"
                                            "\t.loc\t0 10 3 prologue_end            # ReduceSpills.c:10:3\n"
                                            "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                            "\tvmovups\t%zmm0, -128(%rsp)               # 64-byte Spill\n"
                                            "\t.loc\t0 12 5                          # ReduceSpills.c:12:5\n"
                                            "\tvaddps\t-128(%rsp), %zmm1, %zmm1         # 64-byte Folded Reload\n"
                                            "\tmovq\t%rax, %rbx                      # Reload the count from Spill.c\n"
                                            "\tmovq\t64(%rsp), %rax                  # 8-byte Reload\n"
                                            "\tret\n"
                                            ".Lfunc_end0:\n"
                                            "\t.size\tf, .Lfunc_end0-f\n"
                                            "f2:                                     # @f2\n"
                                            "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                            "\tcall\tf\n"
                                            ".Lfunc_end1:\n"
                                            "g:\n"
                                            "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                            "\tret\n";

static const char profile_test_labeled[] = "\t.file\t0 \"/src\" \"ReduceSpills.c\"\n"
                                           "\t.text\n"
                                           "\t.type\tf.labeled,@function\n"
                                           "f.labeled:                                      # @f.labeled\n"
                                           "\t.loc\t0 10 3 prologue_end            # ReduceSpills.c:10:3\n"
                                           "buster_spill_0:\n"
                                           "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                           "buster_spill_1:\n"
                                           "\tvmovups\t%zmm0, -128(%rsp)               # 64-byte Spill\n"
                                           "\t.loc\t0 12 5                          # ReduceSpills.c:12:5\n"
                                           "buster_spill_2:\n"
                                           "\tvaddps\t-128(%rsp), %zmm1, %zmm1         # 64-byte Folded Reload\n"
                                           "\tmovq\t%rax, %rbx                      # Reload the count from Spill.c\n"
                                           "buster_spill_3:\n"
                                           "\tmovq\t64(%rsp), %rax                  # 8-byte Reload\n"
                                           "\tret\n"
                                           ".Lfunc_end0:\n"
                                           "\t.size\tf.labeled, .Lfunc_end0-f.labeled\n"
                                           "f2:                                     # @f2\n"
                                           "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                           "\tcall\tf.labeled\n"
                                           ".Lfunc_end1:\n"
                                           "g:\n"
                                           "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                           "\tret\n";

static const char profile_test_spill_samples[] = " 401000 f.labeled+0x0\n"
                                                 " 300004 f+0x4\n"
                                                 " 500010 f+0x10\n"
                                                 " 401010 buster_spill_0+0x0\n"
                                                 " 401010 buster_spill_0+0x0\n"
                                                 " 401020 buster_spill_1+0x0\n"
                                                 " 401020 buster_spill_1+0x0\n"
                                                 " 401020 buster_spill_1+0x0\n"
                                                 " 401020 buster_spill_1+0x0\n"
                                                 " 401020 buster_spill_1+0x0\n"
                                                 " 401024 buster_spill_1+0x4\n"
                                                 " 401024 buster_spill_1+0x4\n"
                                                 " 401024 buster_spill_1+0x4\n"
                                                 " 401030 buster_spill_2+0x0\n"
                                                 " 401030 buster_spill_2+0x0\n"
                                                 " 401030 buster_spill_2+0x0\n"
                                                 " 401030 buster_spill_2+0x0\n"
                                                 " 401040 buster_spill_3+0x0\n"
                                                 " 401050 buster_spill_9+0x0\n"
                                                 " 401060 g+0x0\n"
                                                 " 401060 g+0x0\n";

static const char profile_test_spill_report[] = "spill-report: 16 samples in f, 12 (75.00%) at its 4 spills and reloads: 7 spills, 5 reloads, in 2 slots\n"
                                                "zmm  -128(%rsp)   spills      5 reloads      4  | hottest spill 5 at ReduceSpills.c:10:3  | hottest reload 4 at ReduceSpills.c:12:5\n"
                                                "gpr  64(%rsp)     spills      2 reloads      1  | hottest spill 2 at ReduceSpills.c:10:3  | hottest reload 1 at ReduceSpills.c:12:5\n";

static const char profile_test_brstack[] = " 0x401005/0x401000/M/-/-/3/  0x401005/0x401000/P/-/-/1/ 0x40100a/0x401020/M/-/-/2/\n"
                                           " 0x401005/0x401000/M/-/-/1/ junk 0x0/0x1/M/-/-/1/ 0x401011/0x401000/-/-/-/1/\n";

static const char profile_test_branch_disassembly[] = "0000000000001000 <f>:\n"
                                                      "    1000:\tdec    %ecx\n"
                                                      "    1005:\tjne    1000 <f>\n"
                                                      "    100a:\tjmp    1020 <g>\n"
                                                      "\n"
                                                      "0000000000001020 <g>:\n"
                                                      "    1020:\tret\n";

static const char profile_test_branch_misses[] = "branch-misses: 5 records of 3 branches, 3 mispredicted (60.00%), 2 fields that aren't records\n"
                                                 "     address  mispredicted   records  share\n"
                                                 "        1005             2         3  66.67%  f+0x5: jne    1000 <f>\n"
                                                 "        100a             1         1  33.33%  f+0xa: jmp    1020 <g>\n"
                                                 "        1011             0         1   0.00%  (not an instruction of the disassembly)\n";

typedef struct ProfileTest ProfileTest;
struct ProfileTest
{
    Arena arena;
    ProfileReport report;
    uint64_t cases;
    uint64_t failures;
};

static String8 profile_test_string(const char* text)
{
    return (String8){ .pointer = (char*)text, .length = strlen(text) };
}

// Checks a tool's success and report, then empties the report and the arena for the next case.
static void profile_test_check(ProfileTest* test, const char* name, bool succeeded, bool expected_success, String8 report, const char* expected)
{
    const String8 wanted = profile_test_string(expected);
    test->cases += 1;
    if (succeeded != expected_success || !profile_equal(report, wanted))
    {
        test->failures += 1;
        printf("  %s: FAILED (%s, expected %s)\n--- got:\n%.*s--- expected:\n%s---\n", name, succeeded ? "succeeded" : "failed", expected_success ? "success" : "failure",
               (int)report.length, report.pointer, expected);
    }
    test->report.arena.length = 0;
    test->report.full = false;
    test->arena.length = 0;
}

static void profile_test_text_tools(ProfileTest* test)
{
    const String8 disassembly = profile_test_string(profile_test_disassembly);
    const String8 samples = profile_test_string(profile_test_samples);
    bool ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), disassembly, samples, 6);
    profile_test_check(test, "hot-loops", ok, true, profile_report_text(&test->report), profile_test_hot_loops);

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), disassembly, samples, 1);
    profile_test_check(test, "hot-loops --loops 1", ok, true, profile_report_text(&test->report),
                       "hot-loops: 14 samples in f (12 instructions, 2 loops), the copy at 1010, the busiest of 2 (2 samples in the others), 1 samples at file offsets outside its copies\n"
                       "loops by samples:\n"
                       "  1014-1027: 7 instructions, 1 stack references, 85.7% of the samples\n"
                       "the smallest loop with half the samples:\n"
                       "  1016-1023: 4 instructions, 1 stack references, 71.4% of the samples\n"
                       "         3  1016  add    $0x1,%eax\n"
                       "         4  1019  vpaddd %zmm1,%zmm0,%zmm0\n"
                       "         0  101f  cmp    %ecx,0x8(%rsp)\n"
                       "         3  1023  jne    1016 <f+0x6>\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("h"), disassembly, samples, 6);
    profile_test_check(test, "hot-loops of a missing function", ok, false, profile_report_text(&test->report), "hot-loops: h isn't in the disassembly\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), disassembly, profile_test_string("401016 f+0x6\n401000 other+0x0\n401016 f+0x6\n"), 6);
    profile_test_check(test, "hot-loops without file offsets", ok, false, profile_report_text(&test->report),
                       "hot-loops: 2 samples of f have no offset in the file (perf script's dsoff field)\n");

    // Past the last symbol's last instruction, which is at most 15 bytes long.
    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), profile_test_string("0000000000001000 <f>:\n    1000:\tjmp    1000 <f>\n"),
                           profile_test_string("401000 f+0x0 (/bin/x+0x1000)\n40100e f+0xe (/bin/x+0x100e)\n40100f f+0xf (/bin/x+0x100f)\n"), 6);
    profile_test_check(test, "hot-loops past the last instruction", ok, true, profile_report_text(&test->report),
                       "hot-loops: 2 samples in f (1 instructions, 1 loops), 1 samples at file offsets outside its copies\n"
                       "loops by samples:\n"
                       "  1000-1000: 1 instructions, 0 stack references, 100.0% of the samples\n"
                       "the smallest loop with half the samples:\n"
                       "  1000-1000: 1 instructions, 0 stack references, 100.0% of the samples\n"
                       "         2  1000  jmp    1000 <f>\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("other"), disassembly, profile_test_string("1 other+0x0 (/bin/x+0x1000)\n"), 6);
    profile_test_check(test, "hot-loops without loops", ok, true, profile_report_text(&test->report),
                       "hot-loops: 1 samples in other (1 instructions, 0 loops)\nloops by samples:\nno loop has half the samples\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), profile_test_string("0000000000001000 <f>:\n    1004:\tret\n    1000:\tret\n"), samples, 6);
    profile_test_check(test, "hot-loops out of order", ok, false, profile_report_text(&test->report),
                       "hot-loops: the disassembly has an instruction before its first symbol, or out of address order\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), profile_test_string("    1000:\tret\n0000000000001000 <f>:\n"), samples, 6);
    profile_test_check(test, "hot-loops before a symbol", ok, false, profile_report_text(&test->report),
                       "hot-loops: the disassembly has an instruction before its first symbol, or out of address order\n");

    ProfileReport labeled = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
    ok = profile_label_spills(&labeled, &test->report, profile_test_string("f"), profile_test_string(profile_test_assembly));
    profile_test_check(test, "label-spills", ok, true, profile_report_text(&test->report), "label-spills: 4 spills and reloads labeled in f, renamed f.labeled\n");
    profile_test_check(test, "label-spills' output", ok, true, profile_report_text(&labeled), profile_test_labeled);
    labeled.arena.length = 0;
    ok = profile_label_spills(&labeled, &test->report, profile_test_string("h"), profile_test_string(profile_test_assembly));
    profile_test_check(test, "label-spills of a missing function", ok, false, profile_report_text(&test->report), "label-spills: h isn't in the assembly\n");
    profile_test_check(test, "label-spills' output of a missing function", ok, false, profile_report_text(&labeled), "");
    arena_release(&labeled.arena);

    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), profile_test_string(profile_test_labeled), profile_test_string(profile_test_spill_samples), 24);
    profile_test_check(test, "spill-report", ok, true, profile_report_text(&test->report), profile_test_spill_report);

    // #236's review: samples of another copy of a static function, before the labeled copy's first label in address
    // order, and none of the labeled copy's own before its first label, mustn't count.
    ProfileReport small = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
    ok = profile_label_spills(&small, &test->report, profile_test_string("e"), profile_test_string("e:\n\tmovq\t%rsi, 64(%rsp)  # 8-byte Spill\n\tret\n.Lfunc_end0:\n"));
    profile_test_check(test, "label-spills of one spill", ok, true, profile_report_text(&test->report), "label-spills: 1 spills and reloads labeled in e, renamed e.labeled\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("e"), profile_report_text(&small),
                              profile_test_string("300004 e+0x4\n300008 e+0x8\n30000c e+0xc\n401010 buster_spill_0+0x0\n"), 24);
    profile_test_check(test, "spill-report beside another copy", ok, true, profile_report_text(&test->report),
                       "spill-report: 1 samples in e, 1 (100.00%) at its 1 spills and reloads: 1 spills, 0 reloads, in 1 slots\n"
                       "gpr  64(%rsp)     spills      1 reloads      0  | hottest spill 1 at -\n");

    // A global function keeps its name, which other translation units may use.
    small.arena.length = 0;
    ok = profile_label_spills(&small, &test->report, profile_test_string("h"), profile_test_string("\t.globl\th                               # -- Begin function h\nh:\n\tmovq\t%rsi, 64(%rsp)  # 8-byte Spill\n.Lfunc_end0:\n"));
    profile_test_check(test, "label-spills of a global function", ok, true, profile_report_text(&test->report), "label-spills: 1 spills and reloads labeled in h\n");
    profile_test_check(test, "label-spills' output of a global function", ok, true, profile_report_text(&small),
                       "\t.globl\th                               # -- Begin function h\nh:\nbuster_spill_0:\n\tmovq\t%rsi, 64(%rsp)  # 8-byte Spill\n.Lfunc_end0:\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("h"), profile_report_text(&small), profile_test_string("1 h+0x0\n2 buster_spill_0+0x0\n"), 24);
    profile_test_check(test, "spill-report of a global function", ok, true, profile_report_text(&test->report),
                       "spill-report: 2 samples in h, 1 (50.00%) at its 1 spills and reloads: 1 spills, 0 reloads, in 1 slots\n"
                       "gpr  64(%rsp)     spills      1 reloads      0  | hottest spill 1 at -\n");
    arena_release(&small.arena);

    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), profile_test_string("f:\nbuster_spill_1:\n\tret\n"), profile_test_string(""), 24);
    profile_test_check(test, "spill-report out of order", ok, false, profile_report_text(&test->report),
                       "spill-report: the labels aren't label-spills' (numbered from 0 in order, each before an instruction)\n");

    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), profile_test_string("f:\nbuster_spill_0:\n"), profile_test_string(""), 24);
    profile_test_check(test, "spill-report with a label last", ok, false, profile_report_text(&test->report),
                       "spill-report: the labels aren't label-spills' (numbered from 0 in order, each before an instruction)\n");

    ok = profile_branch_misses(&test->arena, &test->report, profile_test_string(profile_test_brstack), 0x400000, profile_test_string(profile_test_branch_disassembly), 30);
    profile_test_check(test, "branch-misses", ok, true, profile_report_text(&test->report), profile_test_branch_misses);
}

// 10,000 branches, branch b recorded b % 3 + 1 times and mispredicted b % 3 times: the table grows past its first
// capacity and must keep every count.
static void profile_test_branch_growth(ProfileTest* test)
{
    Arena text = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY);
    ProfileReport brstack = { .arena = text };
    for (uint64_t b = 0; b < BUSTER_PROFILE_TEST_BRANCHES; b += 1)
    {
        for (uint64_t r = 0; r <= b % 3; r += 1)
        {
            profile_print(&brstack, " 0x%llx/0x10/%c/-/-/1/", (unsigned long long)(0x10000 + 16 * b), r < b % 3 ? 'M' : 'P');
        }
        profile_print(&brstack, "\n");
    }
    // Branches with 2 mispredictions are b % 3 == 2: the first are b = 2, 5 and 8.
    const uint64_t records = BUSTER_PROFILE_TEST_BRANCHES * 2 - 1;
    const uint64_t mispredicted = BUSTER_PROFILE_TEST_BRANCHES - 1;
    char expected[1024];
    snprintf(expected, sizeof(expected),
             "branch-misses: %llu records of %u branches, %llu mispredicted (%.2f%%)\n"
             "     address  mispredicted   records  share\n"
             "       10020             2         3   0.02%%\n"
             "       10050             2         3   0.02%%\n"
             "       10080             2         3   0.02%%\n",
             (unsigned long long)records, BUSTER_PROFILE_TEST_BRANCHES, (unsigned long long)mispredicted, 100.0 * (double)mispredicted / (double)records);
    const bool ok = profile_branch_misses(&test->arena, &test->report, profile_report_text(&brstack), 0, profile_test_string(""), 3);
    profile_test_check(test, "branch-misses' table growth", ok, true, profile_report_text(&test->report), expected);
    arena_release(&brstack.arena);
}

// Runs ab into a temporary file, whose text must be `expected`, with `failures` failed runs.
static void profile_test_ab_case(ProfileTest* test, const char* name, const ProfileAb* ab, uint64_t failures, const char* expected)
{
    FILE* file = tmpfile();
    ProfileAb into = *ab;
    into.output = file != 0 ? fileno(file) : -1;
    const uint64_t failed = file != 0 ? profile_ab_run(&test->arena, &into) : 0;
    char output[4096];
    uint64_t length = 0;
    if (file != 0)
    {
        rewind(file);
        length = fread(output, 1, sizeof(output), file);
        fclose(file);
    }
    profile_test_check(test, name, file != 0 && failed == failures, true, (String8){ .pointer = output, .length = length }, expected);
}

static void profile_test_ab(ProfileTest* test)
{
    test->cases += 1;
    bool order = true;
    for (uint64_t round = 0; round < 7; round += 1)
    {
        for (uint64_t position = 0; position < 3; position += 1)
        {
            order = order && profile_ab_tree(round, position, 3) == (round + position) % 3;
        }
    }
    order = order && profile_ab_tree(1, 0, 2) == 1 && profile_ab_tree(1, 1, 2) == 0;
    if (!order)
    {
        test->failures += 1;
        printf("  ab's order: FAILED\n");
    }

    // The first CPU this process may run on, for --cpu.
    uint64_t mask[BUSTER_PROFILE_CPUS / 64] = { 0 };
    int64_t cpu = -1;
    if (syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0)
    {
        for (int64_t c = BUSTER_PROFILE_CPUS - 1; c >= 0; c -= 1)
        {
            cpu = (mask[c / 64] >> (c % 64)) & 1 ? c : cpu;
        }
    }

    const char* const executables[] = { "/usr/bin/true", "/usr/bin/false", "/nonexistent/compiler" };
    const char* const names[] = { "a", "b", "c" };
    char* arguments[] = { "x", "y" };
    const uint64_t starts[] = { 0, 0 };
    const uint64_t counts[] = { 0, 2 };
    const ProfileAb ab = {
        .executables = executables,
        .names = names,
        .tree_count = 3,
        .arguments = arguments,
        .starts = starts,
        .counts = counts,
        .configuration_count = 2,
        .rounds = 2,
        .cpu = -1,
    };
    profile_test_ab_case(test, "ab", &ab, 8,
                         "== round 1 tree a config []\n"
                         "== round 1 tree b config []\n"
                         "ab: round 1 tree b failed\n"
                         "== round 1 tree c config []\n"
                         "ab: can't run /nonexistent/compiler\n"
                         "ab: round 1 tree c failed\n"
                         "== round 1 tree a config [x y]\n"
                         "== round 1 tree b config [x y]\n"
                         "ab: round 1 tree b failed\n"
                         "== round 1 tree c config [x y]\n"
                         "ab: can't run /nonexistent/compiler\n"
                         "ab: round 1 tree c failed\n"
                         "== round 2 tree b config []\n"
                         "ab: round 2 tree b failed\n"
                         "== round 2 tree c config []\n"
                         "ab: can't run /nonexistent/compiler\n"
                         "ab: round 2 tree c failed\n"
                         "== round 2 tree a config []\n"
                         "== round 2 tree b config [x y]\n"
                         "ab: round 2 tree b failed\n"
                         "== round 2 tree c config [x y]\n"
                         "ab: can't run /nonexistent/compiler\n"
                         "ab: round 2 tree c failed\n"
                         "== round 2 tree a config [x y]\n"
                         "ab: done, 8 failed runs\n");

    // Through a lock (env, which runs the rest of its arguments) and pinned.
    ProfileAb locked = ab;
    locked.tree_count = 2;
    locked.configuration_count = 1;
    locked.rounds = 1;
    locked.cpu = cpu;
    locked.lock = "/usr/bin/env";
    profile_test_ab_case(test, "ab through a lock", &locked, 1,
                         "== round 1 tree a config []\n"
                         "== round 1 tree b config []\n"
                         "ab: round 1 tree b failed\n"
                         "ab: done, 1 failed runs\n");
}

static int run_profile_tests(int argc, char** argv)
{
    (void)argv;
    int result = 1;
    if (argc != 0)
    {
        fprintf(stderr, "usage: test-profile\n");
    }
    else
    {
        ProfileTest test = {
            .arena = arena_reserve(BUSTER_PROFILE_ARENA_CAPACITY),
            .report = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) },
        };
        printf("test-profile:\n");
        profile_test_text_tools(&test);
        profile_test_branch_growth(&test);
        profile_test_ab(&test);
        printf("test-profile: %llu checks, %llu failed\n", (unsigned long long)test.cases, (unsigned long long)test.failures);
        result = test.failures == 0 ? 0 : 1;
        arena_release(&test.report.arena);
        arena_release(&test.arena);
    }
    return result;
}
