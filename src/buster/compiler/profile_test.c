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
//   - a program: clang's assembly of a program that prints __func__ of a static function with spills, labeled,
//     assembled and run, must print what the original prints (clang on PATH);
//   - ab: the rotating order, then runs of programs that succeed, fail and don't exist, with two argument lists, and
//     through a lock (env) pinned to an allowed CPU, with every heading and failure in the output;
//   - time-runs: shell commands that log their order, statuses kept and changed, and an executable that can't start;
//     copies of scripts (two builds of two placements) that log their tags and paths, in the slots' order, removed
//     afterwards, and a copy that fails; and the builds' comparison on values made by hand, with placements and
//     copies, without either, and with more placements than the reference's.
//
// The exit status is 1 if a case fails.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// objdump -p's lines for a file /t/bin whose LOAD segment maps the file at its own addresses, as GNU ld's do.
#define BUSTER_PROFILE_TEST_IMAGE                                                                          \
    "/t/bin:     file format elf64-x86-64\n"                                                            \
    "\n"                                                                                                \
    "Program Header:\n"                                                                                 \
    "    LOAD off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12\n" \
    "         filesz 0x0000000000100000 memsz 0x0000000000100000 flags r-x\n"                           \
    "\n"

// The branches of branch-misses' growth check: more than the table's first capacity holds.
#define BUSTER_PROFILE_TEST_BRANCHES 10000

static const char profile_test_disassembly[] = "\n" BUSTER_PROFILE_TEST_IMAGE
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

static const char profile_test_samples[] = "    401016 f+0x6 (/t/bin+0x1016)\n"
                                           "    401016 f+0x6 (/t/bin+0x1016)\n"
                                           "    401016 f+0x6 (/t/bin+0x1016)\n"
                                           "    401019 f+0x9 (/t/bin+0x1019)\n"
                                           "    40101a f+0xa (/t/bin+0x101a)\n"
                                           "    401019 f+0x9 (/t/bin+0x1019)\n"
                                           "    401019 f+0x9 (/t/bin+0x1019)\n"
                                           "    401023 f+0x13 (/t/bin+0x1023)\n"
                                           "    401023 f+0x13 (/t/bin+0x1023)\n"
                                           "    401023 f+0x13 (/t/bin+0x1023)\n"
                                           "    401025 f+0x15 (/t/bin+0x1025)\n"
                                           "    401025 f+0x15 (/t/bin+0x1025)\n"
                                           "    401010 f+0x0 (/t/bin+0x1010)\n"
                                           "    401032 f+0x22 (/t/bin+0x1032)\n"
                                           "    401000 other+0x0 (/t/bin+0x1000)\n"
                                           "    402000 f+0x0 (/t/bin+0x2000)\n"
                                           "    402001 f+0x1 (/t/bin+0x2001)\n"
                                           "    400050 f+0x0 (/t/bin+0x50)\n"
                                           "    402000 ff+0x6\n"
                                           "    7f0000 [unknown]\n";

static const char profile_test_hot_loops[] = "hot-loops: 14 samples in f (12 instructions, 2 loops), the copy at 1010, the busiest of 2 (2 samples in the others), 1 samples named f outside its copies\n"
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
                                            "\tleaq\tf.counter(%rip), %rdi           # f's static local\n"
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
                                            "\tret\n"
                                            "f.counter:\n"
                                            "\t.long\t1\n"
                                            "\t.asciz\t\"f\"\n";

static const char profile_test_labeled[] = "\t.file\t0 \"/src\" \"ReduceSpills.c\"\n"
                                           "\t.text\n"
                                           "\t.type\tf,@function\n"
                                           "f:                                      # @f\n"
                                           "\t.loc\t0 10 3 prologue_end            # ReduceSpills.c:10:3\n"
                                           "buster_spill.a.0:\n"
                                           "\tmovq\t%rsi, 64(%rsp)                  # 8-byte Spill\n"
                                           "buster_spill.a.1:\n"
                                           "\tvmovups\t%zmm0, -128(%rsp)               # 64-byte Spill\n"
                                           "\t.loc\t0 12 5                          # ReduceSpills.c:12:5\n"
                                           "buster_spill.a.2:\n"
                                           "\tvaddps\t-128(%rsp), %zmm1, %zmm1         # 64-byte Folded Reload\n"
                                           "\tmovq\t%rax, %rbx                      # Reload the count from Spill.c\n"
                                           "\tleaq\tf.counter(%rip), %rdi           # f's static local\n"
                                           "buster_spill.a.3:\n"
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
                                           "\tret\n"
                                           "f.counter:\n"
                                           "\t.long\t1\n"
                                           "\t.asciz\t\"f\"\n";

static const char profile_test_spill_disassembly[] = "\n"
                                                     "/p/program:     file format elf64-x86-64\n"
                                                    "\n"
                                                    "Program Header:\n"
                                                    "    LOAD off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12\n"
                                                    "         filesz 0x0000000000100000 memsz 0x0000000000100000 flags r-x\n"
                                                     "\n"
                                                     "Disassembly of section .text:\n"
                                                     "\n"
                                                     "0000000000001000 <f>:\n"
                                                     "    1000:\tpush   %rbp\n"
                                                     "\n"
                                                     "0000000000001010 <buster_spill.a.0>:\n"
                                                     "    1010:\tmov    %rsi,0x40(%rsp)\n"
                                                     "\n"
                                                     "0000000000001020 <buster_spill.a.1>:\n"
                                                     "    1020:\tvmovups %zmm0,-0x80(%rsp)\n"
                                                     "    1024:\tnop\n"
                                                     "\n"
                                                     "0000000000001030 <buster_spill.a.2>:\n"
                                                     "    1030:\tvaddps -0x80(%rsp),%zmm1,%zmm1\n"
                                                     "    1036:\tmov    %rax,%rbx\n"
                                                     "\n"
                                                     "0000000000001040 <buster_spill.a.3>:\n"
                                                     "    1040:\tmov    0x40(%rsp),%rax\n"
                                                     "    1045:\tret\n"
                                                     "\n"
                                                     "0000000000001060 <f2>:\n"
                                                     "    1060:\tret\n"
                                                     "\n"
                                                     "0000000000003000 <f>:\n"
                                                     "    3000:\tret\n"
                                                     "\n"
                                                     "0000000000005000 <f>:\n"
                                                     "    5000:\tpush   %rbp\n"
                                                     "\n"
                                                     "0000000000005010 <buster_spill.b.0>:\n"
                                                     "    5010:\tmov    %rsi,0x40(%rsp)\n"
                                                     "    5015:\tret\n";

static const char profile_test_spill_samples[] = " 401000 f+0x0 (/p/program+0x1000)\n"
                                                 " 401010 buster_spill.a.0+0x0 (/p/program+0x1010)\n"
                                                 " 401010 buster_spill.a.0+0x0 (/p/program+0x1010)\n"
                                                 " 401020 buster_spill.a.1+0x0 (/p/program+0x1020)\n"
                                                 " 401020 buster_spill.a.1+0x0 (/p/program+0x1020)\n"
                                                 " 401020 buster_spill.a.1+0x0 (/p/program+0x1020)\n"
                                                 " 401020 buster_spill.a.1+0x0 (/p/program+0x1020)\n"
                                                 " 401020 buster_spill.a.1+0x0 (/p/program+0x1020)\n"
                                                 " 401024 buster_spill.a.1+0x4 (/p/program+0x1024)\n"
                                                 " 401024 buster_spill.a.1+0x4 (/p/program+0x1024)\n"
                                                 " 401024 buster_spill.a.1+0x4 (/p/program+0x1024)\n"
                                                 " 401030 buster_spill.a.2+0x0 (/p/program+0x1030)\n"
                                                 " 401030 buster_spill.a.2+0x0 (/p/program+0x1030)\n"
                                                 " 401030 buster_spill.a.2+0x0 (/p/program+0x1030)\n"
                                                 " 401030 buster_spill.a.2+0x0 (/p/program+0x1030)\n"
                                                 " 401040 buster_spill.a.3+0x0 (/p/program+0x1040)\n"
                                                 " 401060 f2+0x0 (/p/program+0x1060)\n"
                                                 " 403000 f+0x0 (/p/program+0x3000)\n"
                                                 " 403000 f+0x0 (/p/program+0x3000)\n"
                                                 " 405000 f+0x0 (/p/program+0x5000)\n"
                                                 " 405010 buster_spill.b.0+0x0 (/p/program+0x5010)\n"
                                                 " 7f001010 library_work+0x0 (/l/libnoise.so+0x1010)\n"
                                                 " 7f001010 library_work+0x0 (/l/libnoise.so+0x1010)\n"
                                                 " 7f001020 library_work+0x10 (/l/libnoise.so+0x1020)\n"
                                                 " 7f0000 [unknown]\n";

static const char profile_test_spill_report[] = "spill-report: 16 samples in f at 1000, 12 (75.00%) at its 4 spills and reloads: 7 spills, 5 reloads, in 2 slots\n"
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
                       "hot-loops: 14 samples in f (12 instructions, 2 loops), the copy at 1010, the busiest of 2 (2 samples in the others), 1 samples named f outside its copies\n"
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
    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), profile_test_string(BUSTER_PROFILE_TEST_IMAGE "0000000000001000 <f>:\n    1000:\tjmp    1000 <f>\n"),
                           profile_test_string("401000 f+0x0 (/t/bin+0x1000)\n40100e f+0xe (/t/bin+0x100e)\n40100f f+0xf (/t/bin+0x100f)\n"), 6);
    profile_test_check(test, "hot-loops past the last instruction", ok, true, profile_report_text(&test->report),
                       "hot-loops: 2 samples in f (1 instructions, 1 loops), 1 samples named f outside its copies\n"
                       "loops by samples:\n"
                       "  1000-1000: 1 instructions, 0 stack references, 100.0% of the samples\n"
                       "the smallest loop with half the samples:\n"
                       "  1000-1000: 1 instructions, 0 stack references, 100.0% of the samples\n"
                       "         2  1000  jmp    1000 <f>\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("other"), disassembly, profile_test_string("1 other+0x0 (/t/bin+0x1000)\n1 other+0x0 (/t/libother.so+0x1000)\n"), 6);
    profile_test_check(test, "hot-loops without loops", ok, true, profile_report_text(&test->report),
                       "hot-loops: 1 samples in other (1 instructions, 0 loops)\nloops by samples:\nno loop has half the samples\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), profile_test_string("0000000000001000 <f>:\n    1004:\tret\n    1000:\tret\n"), samples, 6);
    profile_test_check(test, "hot-loops out of order", ok, false, profile_report_text(&test->report),
                       "hot-loops: the disassembly has an instruction before its first symbol, or out of address order\n");

    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), profile_test_string("    1000:\tret\n0000000000001000 <f>:\n"), samples, 6);
    profile_test_check(test, "hot-loops before a symbol", ok, false, profile_report_text(&test->report),
                       "hot-loops: the disassembly has an instruction before its first symbol, or out of address order\n");

    ProfileReport labeled = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
    ok = profile_label_spills(&labeled, &test->report, profile_test_string("f"), profile_test_string("a"), profile_test_string(profile_test_assembly));
    profile_test_check(test, "label-spills", ok, true, profile_report_text(&test->report), "label-spills: 4 spills and reloads labeled in f\n");
    profile_test_check(test, "label-spills' output", ok, true, profile_report_text(&labeled), profile_test_labeled);
    labeled.arena.length = 0;
    ok = profile_label_spills(&labeled, &test->report, profile_test_string("h"), profile_test_string("a"), profile_test_string(profile_test_assembly));
    profile_test_check(test, "label-spills of a missing function", ok, false, profile_report_text(&test->report), "label-spills: h isn't in the assembly\n");
    profile_test_check(test, "label-spills' output of a missing function", ok, false, profile_report_text(&labeled), "");
    ok = profile_label_spills(&labeled, &test->report, profile_test_string("f"), profile_test_string("a.b"), profile_test_string(profile_test_assembly));
    profile_test_check(test, "label-spills with an invalid tag", ok, false, profile_report_text(&test->report), "label-spills: the tag a.b isn't letters, digits and '_'\n");
    arena_release(&labeled.arena);

    // The labeled copy at 1000, beside a static f of another unit (3000) and variant b's labeled copy (5000), whose
    // samples don't count; f.counter, a static local's label, isn't the function.
    const String8 spill_labeled = profile_test_string(profile_test_labeled);
    const String8 spill_disassembly = profile_test_string(profile_test_spill_disassembly);
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), spill_labeled, spill_disassembly, profile_test_string(profile_test_spill_samples), 24);
    profile_test_check(test, "spill-report", ok, true, profile_report_text(&test->report), profile_test_spill_report);

    // #236's reviews: no sample before the first label, and another copy's before it in address order.
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), spill_labeled, spill_disassembly,
                              profile_test_string("403004 f+0x4 (/p/program+0x3004)\n403008 f+0x8 (/p/program+0x3008)\n401010 buster_spill.a.0+0x0 (/p/program+0x1010)\n"), 1);
    profile_test_check(test, "spill-report beside another copy", ok, true, profile_report_text(&test->report),
                       "spill-report: 1 samples in f at 1000, 1 (100.00%) at its 4 spills and reloads: 1 spills, 0 reloads, in 2 slots\n"
                       "gpr  64(%rsp)     spills      1 reloads      0  | hottest spill 1 at ReduceSpills.c:10:3\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), spill_labeled, spill_disassembly,
                              profile_test_string("405000 f+0x0 (/p/program+0x5000)\n405010 buster_spill.b.0+0x0 (/p/program+0x5010)\n405010 buster_spill.b.0+0x0 (/p/program+0x5010)\n"), 1);
    profile_test_check(test, "spill-report with another variant's samples", ok, true, profile_report_text(&test->report),
                       "spill-report: 0 samples in f at 1000, 0 (0.00%) at its 4 spills and reloads: 0 spills, 0 reloads, in 2 slots\n"
                       "gpr  64(%rsp)     spills      0 reloads      0\n");

    // #236's fourth review: a later function of the same name (another unit's static f) ends the copy.
    const String8 adjacent_labeled = profile_test_string("f:\nbuster_spill.a.0:\n\tmovq\t%rsi, 64(%rsp)  # 8-byte Spill\n\tret\n.Lfunc_end0:\n");
    const String8 adjacent = profile_test_string(BUSTER_PROFILE_TEST_IMAGE "0000000000001140 <f>:\n    1140:\tpush   %rbp\n"
                                                 "0000000000001141 <buster_spill.a.0>:\n    1141:\tmov    %rsi,0x40(%rsp)\n    1146:\tret\n"
                                                 "0000000000001160 <f>:\n    1160:\tret\n0000000000001170 <external>:\n    1170:\tret\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), adjacent_labeled, adjacent,
                              profile_test_string("401141 buster_spill.a.0+0x0 (/t/bin+0x1141)\n401160 f+0x0 (/t/bin+0x1160)\n401160 f+0x0 (/t/bin+0x1160)\n401160 f+0x0 (/t/bin+0x1160)\n"), 24);
    profile_test_check(test, "spill-report beside an adjacent copy", ok, true, profile_report_text(&test->report),
                       "spill-report: 1 samples in f at 1140, 1 (100.00%) at its 1 spills and reloads: 1 spills, 0 reloads, in 1 slots\n"
                       "gpr  64(%rsp)     spills      1 reloads      0  | hottest spill 1 at -\n");

    // An lld executable: its executable segment at file offset 0x6c0 and address 0x16c0.
    const String8 lld = profile_test_string("/b/lld_prog:     file format elf64-x86-64\n\nProgram Header:\n"
                                            "    LOAD off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12\n"
                                            "         filesz 0x0000000000000658 memsz 0x0000000000000658 flags r--\n"
                                            "    LOAD off    0x00000000000006c0 vaddr 0x00000000000016c0 paddr 0x00000000000016c0 align 2**12\n"
                                            "         filesz 0x0000000000001000 memsz 0x0000000000001000 flags r-x\n\n"
                                            "00000000000017e0 <f>:\n    17e0:\tdec    %ecx\n"
                                            "00000000000017f0 <buster_spill.a.0>:\n    17f0:\tmov    %rsi,0x40(%rsp)\n    17f5:\tjne    17e0 <f>\n    17f7:\tret\n");
    const String8 lld_samples = profile_test_string("5557e0 f+0x0 (/b/lld_prog+0x7e0)\n5557f0 buster_spill.a.0+0x0 (/b/lld_prog+0x7f0)\n5557f5 buster_spill.a.0+0x5 (/b/lld_prog+0x7f5)\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), adjacent_labeled, lld, lld_samples, 24);
    profile_test_check(test, "spill-report of an lld executable", ok, true, profile_report_text(&test->report),
                       "spill-report: 3 samples in f at 17e0, 1 (33.33%) at its 1 spills and reloads: 1 spills, 0 reloads, in 1 slots\n"
                       "gpr  64(%rsp)     spills      1 reloads      0  | hottest spill 1 at -\n");
    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), lld, lld_samples, 6);
    profile_test_check(test, "hot-loops of an lld executable", ok, true, profile_report_text(&test->report),
                       "hot-loops: 3 samples in f (4 instructions, 1 loops)\n"
                       "loops by samples:\n"
                       "  17e0-17f5: 3 instructions, 1 stack references, 100.0% of the samples\n"
                       "the smallest loop with half the samples:\n"
                       "  17e0-17f5: 3 instructions, 1 stack references, 100.0% of the samples\n"
                       "         1  17e0  dec    %ecx\n"
                       "         1  17f0  mov    %rsi,0x40(%rsp)\n"
                       "         1  17f5  jne    17e0 <f>\n");

    // Without objdump -p's lines, offsets in the file can't be addresses.
    const String8 bare = profile_test_string("0000000000001140 <f>:\n    1140:\tpush   %rbp\n0000000000001141 <buster_spill.a.0>:\n    1141:\tret\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), adjacent_labeled, bare, profile_test_string(""), 24);
    profile_test_check(test, "spill-report without program headers", ok, false, profile_report_text(&test->report),
                       "spill-report: the disassembly needs its file's LOAD segments (objdump -d -p) and path (absolute, or found from here)\n");
    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), bare, profile_test_string(""), 6);
    profile_test_check(test, "hot-loops without program headers", ok, false, profile_report_text(&test->report),
                       "hot-loops: the disassembly needs its file's LOAD segments (objdump -d -p) and path (absolute, or found from here)\n");

    // #236's fifth review: another tree's build/compiler shares the name, not the path; a relative path that doesn't
    // resolve is no identity; and a spill that is the first instruction has its label at the function's address, which
    // only the symbol table (-t) shows.
    const String8 tree_a = profile_test_string("/w/tree-a/build/compiler:     file format elf64-x86-64\n\nProgram Header:\n"
                                               "    LOAD off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12\n"
                                               "         filesz 0x0000000000100000 memsz 0x0000000000100000 flags r-x\n\n"
                                               "0000000000001140 <f>:\n    1140:\tpush   %rbp\n"
                                               "0000000000001141 <buster_spill.a.0>:\n    1141:\tmov    %rsi,0x40(%rsp)\n    1146:\tret\n"
                                               "0000000000001170 <external>:\n    1170:\tret\n");
    const String8 two_trees = profile_test_string("401141 buster_spill.a.0+0x0 (/w/tree-a/build/compiler+0x1141)\n"
                                                  "401141 buster_spill.a.0+0x0 (/w/tree-b/build/compiler+0x1141)\n"
                                                  "401140 f+0x0 (/w/tree-b/build/compiler+0x1140)\n401140 f+0x0 (/w/tree-b/build/compiler+0x1140)\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), adjacent_labeled, tree_a, two_trees, 24);
    profile_test_check(test, "spill-report with another tree's samples", ok, true, profile_report_text(&test->report),
                       "spill-report: 1 samples in f at 1140, 1 (100.00%) at its 1 spills and reloads: 1 spills, 0 reloads, in 1 slots\n"
                       "gpr  64(%rsp)     spills      1 reloads      0  | hottest spill 1 at -\n");
    ok = profile_hot_loops(&test->arena, &test->report, profile_test_string("f"), tree_a, two_trees, 6);
    profile_test_check(test, "hot-loops with another tree's samples", ok, true, profile_report_text(&test->report),
                       "hot-loops: 1 samples in f (3 instructions, 0 loops)\nloops by samples:\nno loop has half the samples\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), adjacent_labeled,
                              profile_test_string("buster-test-profile-no-such-file:     file format elf64-x86-64\n\nProgram Header:\n"
                                                  "    LOAD off    0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**12\n"
                                                  "         filesz 0x0000000000100000 memsz 0x0000000000100000 flags r-x\n\n"
                                                  "0000000000001140 <f>:\n    1140:\tpush   %rbp\n0000000000001141 <buster_spill.a.0>:\n    1141:\tret\n"),
                              profile_test_string(""), 24);
    profile_test_check(test, "spill-report of a relative path that doesn't resolve", ok, false, profile_report_text(&test->report),
                       "spill-report: the disassembly needs its file's LOAD segments (objdump -d -p) and path (absolute, or found from here)\n");
    const String8 entry_labeled = profile_test_string("f:\nbuster_spill.a.0:\n\tmovss\t%xmm0, -4(%rsp)  # 4-byte Spill\n\tret\n.Lfunc_end0:\n");
    const String8 entry_headers = profile_test_string(BUSTER_PROFILE_TEST_IMAGE "0000000000001140 <f>:\n    1140:\tvmovss %xmm0,-0x4(%rsp)\n    1146:\tret\n"
                                                      "0000000000001150 <main>:\n    1150:\tret\n");
    const String8 entry_table = profile_test_string(BUSTER_PROFILE_TEST_IMAGE "SYMBOL TABLE:\n"
                                                    "0000000000001140 l       .text\t0000000000000000              buster_spill.a.0\n"
                                                    "0000000000001140 g     F .text\t0000000000000010              f\n\n"
                                                    "0000000000001140 <f>:\n    1140:\tvmovss %xmm0,-0x4(%rsp)\n    1146:\tret\n"
                                                    "0000000000001150 <main>:\n    1150:\tret\n");
    const String8 entry_samples = profile_test_string("401140 f+0x0 (/t/bin+0x1140)\n401146 f+0x6 (/t/bin+0x1146)\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), entry_labeled, entry_table, entry_samples, 24);
    profile_test_check(test, "spill-report of a spill at the function's address", ok, true, profile_report_text(&test->report),
                       "spill-report: 2 samples in f at 1140, 1 (50.00%) at its 1 spills and reloads: 1 spills, 0 reloads, in 1 slots\n"
                       "xmm  -4(%rsp)     spills      1 reloads      0  | hottest spill 1 at -\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), entry_labeled, entry_headers, entry_samples, 24);
    profile_test_check(test, "spill-report of a spill at the function's address without the symbol table", ok, false, profile_report_text(&test->report),
                       "spill-report: the disassembly (objdump -d -p -t) has 0 of the 1 labels buster_spill.a.0 to buster_spill.a.0, 0 at two addresses\n");

    // Errors: samples without file offsets, labels the disassembly lacks or has twice, no copy before them, labels out
    // of order or of another tag, and a label last.
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), spill_labeled, spill_disassembly, profile_test_string("401010 buster_spill.a.0+0x0\n"), 24);
    profile_test_check(test, "spill-report without file offsets", ok, false, profile_report_text(&test->report),
                       "spill-report: 1 samples have no offset in the file (perf script's dsoff field)\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), spill_labeled,
                              profile_test_string("0000000000001000 <f>:\n    1000:\tret\n0000000000001010 <buster_spill.a.0>:\n    1010:\tret\n"), profile_test_string(""), 24);
    profile_test_check(test, "spill-report with labels missing", ok, false, profile_report_text(&test->report),
                       "spill-report: the disassembly (objdump -d -p -t) has 1 of the 4 labels buster_spill.a.0 to buster_spill.a.3, 0 at two addresses\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("e"), profile_test_string("e:\nbuster_spill.a.0:\n\tret\n"),
                              profile_test_string("0000000000001000 <e>:\n    1000:\tret\n0000000000001010 <buster_spill.a.0>:\n    1010:\tret\n"
                                                  "0000000000002010 <buster_spill.a.0>:\n    2010:\tret\n"), profile_test_string(""), 24);
    profile_test_check(test, "spill-report with a label twice", ok, false, profile_report_text(&test->report),
                       "spill-report: the disassembly (objdump -d -p -t) has 1 of the 1 labels buster_spill.a.0 to buster_spill.a.0, 1 at two addresses\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("e"), profile_test_string("e:\nbuster_spill.a.0:\n\tret\nbuster_spill.a.1:\n\tret\n"),
                              profile_test_string("0000000000001000 <e>:\n    1000:\tret\n0000000000001010 <buster_spill.a.1>:\n    1010:\tret\n"
                                                  "0000000000001020 <buster_spill.a.0>:\n    1020:\tret\n"), profile_test_string(""), 24);
    profile_test_check(test, "spill-report with labels out of address order", ok, false, profile_report_text(&test->report),
                       "spill-report: the labels are out of address order in the disassembly\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("e"), profile_test_string("e:\nbuster_spill.a.0:\n\tret\n"),
                              profile_test_string("0000000000001000 <buster_spill.a.0>:\n    1000:\tret\n0000000000002000 <e>:\n    2000:\tret\n"), profile_test_string(""), 24);
    profile_test_check(test, "spill-report without a copy before its labels", ok, false, profile_report_text(&test->report),
                       "spill-report: no e in the disassembly before buster_spill.a.0\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), profile_test_string("f:\nbuster_spill.a.1:\n\tret\n"), spill_disassembly, profile_test_string(""), 24);
    profile_test_check(test, "spill-report out of order", ok, false, profile_report_text(&test->report),
                       "spill-report: the labels aren't label-spills' (numbered from 0 in order, each before an instruction)\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), profile_test_string("f:\nbuster_spill.a.0:\n\tret\nbuster_spill.b.1:\n\tret\n"),
                              spill_disassembly, profile_test_string(""), 24);
    profile_test_check(test, "spill-report with another tag's label", ok, false, profile_report_text(&test->report),
                       "spill-report: the labels aren't label-spills' (numbered from 0 in order, each before an instruction)\n");
    ok = profile_spill_report(&test->arena, &test->report, profile_test_string("f"), profile_test_string("f:\nbuster_spill.a.0:\n"), spill_disassembly, profile_test_string(""), 24);
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

// time-runs on shell commands that log their order into a file: the warm-ups in list order, then each run's lists from
// an order that rotates by run; statuses kept as the first warm-up's (0 and 3) pass, and one that changes after it
// fails each run, as does a list that can't start.
static void profile_test_time_runs(ProfileTest* test)
{
    test->cases += 1;
    char directory[] = "/tmp/buster-time-runs-XXXXXX";
    bool ok = mkdtemp(directory) != 0;
    char log[64];
    char flag[64];
    char first[160];
    char second[160];
    char changing[160];
    snprintf(log, sizeof(log), "%s/log", directory);
    snprintf(flag, sizeof(flag), "%s/flag", directory);
    snprintf(first, sizeof(first), "echo a >> %s", log);
    snprintf(second, sizeof(second), "echo b >> %s; exit 3", log);
    snprintf(changing, sizeof(changing), "test -e %s && exit 1; touch %s", flag, flag);
    char* arguments[] = { "-c", first, "-c", second, "-c", changing };
    const uint64_t starts[] = { 0, 2, 4 };
    const uint64_t counts[] = { 2, 2, 2 };
    const int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
    const char* executables[] = { "/bin/sh" };
    const char* const names[] = { "sh" };
    const uint64_t builds[] = { 0 };
    const uint64_t placements[] = { 0 };
    ProfileTimeRuns t = {
        .executables = executables,
        .builds = builds,
        .placements = placements,
        .executable_count = 1,
        .build_names = names,
        .build_count = 1,
        .arguments = arguments,
        .starts = starts,
        .counts = counts,
        .list_count = 2,
        .runs = 3,
        .warmups = 1,
        .after_scalar_ms = 1,
        .output = null,
    };
    ok = ok && null != -1 && profile_time_runs(&test->arena, &t) == 0;
    String8 text = ok ? profile_read_file(&test->arena, log) : (String8){ 0 };
    ok = ok && text.length == 16 && memcmp(text.pointer, "a\nb\na\nb\nb\na\na\nb\n", 16) == 0;
    t.starts = starts + 2;
    t.list_count = 1;
    ok = ok && profile_time_runs(&test->arena, &t) == 3;
    executables[0] = "/nonexistent/compiler";
    ok = ok && profile_time_runs(&test->arena, &t) == 4;
    if (null != -1)
    {
        close(null);
    }
    unlink(log);
    unlink(flag);
    rmdir(directory);
    if (!ok)
    {
        test->failures += 1;
        printf("  time-runs: FAILED\n");
    }
}

// time-runs' copies (#509): two builds of two placements each, given out of the slots' order, run from two copies each
// for two runs after a warm-up. Each executable is a script that logs its tag, its path after the copies' directory
// (in the test's directory, where any other path stays whole) and its argument, so the log must be the slots' order:
// placement by placement, each build's in turn, then each copy, rotating by run. The copies must be gone afterwards,
// and so must they after a copy that fails, which fails the command once without running anything.
static void profile_test_time_copies(ProfileTest* test)
{
    test->cases += 1;
    char directory[] = "/tmp/buster-time-copies-XXXXXX";
    bool ok = mkdtemp(directory) != 0;
    const char* const tags[] = { "a0", "a1", "b0", "b1" };
    char scripts[4][96];
    for (uint64_t i = 0; ok && i < 4; i += 1)
    {
        snprintf(scripts[i], sizeof(scripts[i]), "%s/%s", directory, tags[i]);
        char text[512];
        const int length = snprintf(text, sizeof(text), "#!/bin/sh\necho \"%s ${0#%s/buster-time-runs-*/} $1\" >> %s/log\n", tags[i], directory, directory);
        const int fd = open(scripts[i], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0700);
        ok = fd != -1 && write(fd, text, (size_t)length) == length;
        ok = fd != -1 && close(fd) == 0 && ok;
    }
    // In the slots' order: a0, b0, a1, b1.
    const char* const executables[] = { scripts[0], scripts[2], scripts[1], scripts[3] };
    const char* const names[] = { "a", "b" };
    const uint64_t builds[] = { 0, 1, 0, 1 };
    const uint64_t placements[] = { 0, 0, 1, 1 };
    char* arguments[] = { "x" };
    const uint64_t starts[] = { 0 };
    const uint64_t counts[] = { 1 };
    const int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
    ProfileTimeRuns t = {
        .executables = executables,
        .builds = builds,
        .placements = placements,
        .executable_count = 4,
        .build_names = names,
        .build_count = 2,
        .copies = 2,
        .directory = directory,
        .arguments = arguments,
        .starts = starts,
        .counts = counts,
        .list_count = 1,
        .runs = 2,
        .warmups = 1,
        .output = null,
    };
    ok = ok && null != -1 && profile_time_runs(&test->arena, &t) == 0;
    char log[96];
    snprintf(log, sizeof(log), "%s/log", directory);
    const String8 text = ok ? profile_read_file(&test->arena, log) : (String8){ 0 };
    // The warm-up and the first run in slot order, the second from slot 1.
    const char* expected = "a0 000-000 x\nb0 001-000 x\na1 002-000 x\nb1 003-000 x\na0 000-001 x\nb0 001-001 x\na1 002-001 x\nb1 003-001 x\n"
                           "a0 000-000 x\nb0 001-000 x\na1 002-000 x\nb1 003-000 x\na0 000-001 x\nb0 001-001 x\na1 002-001 x\nb1 003-001 x\n"
                           "b0 001-000 x\na1 002-000 x\nb1 003-000 x\na0 000-001 x\nb0 001-001 x\na1 002-001 x\nb1 003-001 x\na0 000-000 x\n";
    ok = ok && profile_equal(text, profile_test_string(expected));
    unlink(log);
    // A copy of an executable that doesn't exist.
    const char* const missing[] = { scripts[0], "/nonexistent/compiler" };
    t.executables = missing;
    t.executable_count = 2;
    t.placements = placements;
    ok = ok && profile_time_runs(&test->arena, &t) == 1 && access(log, F_OK) != 0;
    for (uint64_t i = 0; i < 4; i += 1)
    {
        unlink(scripts[i]);
    }
    // Only the copies' directories could be left: rmdir fails on a directory that isn't empty.
    ok = ok && rmdir(directory) == 0;
    if (null != -1)
    {
        close(null);
    }
    if (!ok)
    {
        test->failures += 1;
        printf("  time-runs' copies: FAILED (%s)\n", directory);
    }
    test->arena.length = 0;
}

// profile_time_compare on values made by hand, against reports worked out by hand: two builds of two placements, run
// from two copies three times each, whose medians, spreads and ranges each come from other values; the second list
// of two builds without copies or placements; and a build of two placements against a reference of one, with copies,
// of one run each.
static void profile_test_time_compare(ProfileTest* test)
{
    const char* const names[] = { "main", "mine", "odd" };
    const char* const executables[] = { "e0", "e1", "e2", "e3" };
    const uint64_t builds[] = { 0, 1, 0, 1 };
    const uint64_t placements[] = { 0, 0, 1, 1 };
    ProfileTimeRuns t = {
        .executables = executables,
        .builds = builds,
        .placements = placements,
        .executable_count = 4,
        .build_names = names,
        .build_count = 2,
        .copies = 2,
        .list_count = 1,
        .runs = 3,
    };
    // values[r * 8 + c * 4 + e]: each unit's runs are its median less 1, its median, and its median plus 2.
    const uint64_t unit_medians[2][4] = {
        { 1000, 1010, 1020, 1030 },
        { 1004, 1012, 1018, 1026 },
    };
    uint64_t values[24];
    for (uint64_t c = 0; c < 2; c += 1)
    {
        for (uint64_t e = 0; e < 4; e += 1)
        {
            values[0 * 8 + c * 4 + e] = unit_medians[c][e] + 2;
            values[1 * 8 + c * 4 + e] = unit_medians[c][e] - 1;
            values[2 * 8 + c * 4 + e] = unit_medians[c][e];
        }
    }
    bool ok = profile_time_compare(&test->arena, &test->report, &t, values, 0, "cycles", 1);
    profile_test_check(test, "time-runs' comparison over placements and copies", ok, true, profile_report_text(&test->report),
                       "list 1, cycles:\n"
                       "  main: 12 runs, median 1017.000; placements 1003.000, 1020.000 (spread 1.69%); copies within 0.40%\n"
                       "  mine: 12 runs, median 1025.000; placements 1012.000, 1029.000 (spread 1.68%); copies within 0.39%; against main +0.79% "
                       "(over placements -0.78% to +2.59%; over placements and copies -0.98% to +3.00%)\n");

    // Two lists of one placement each and no copies: values[r * 4 + l * 2 + e], list 1's never read.
    const uint64_t one_placements[] = { 0, 0 };
    const uint64_t list_values[12] = { 9, 9, 200, 210, 9, 9, 201, 209, 9, 9, 199, 212 };
    t.placements = one_placements;
    t.executable_count = 2;
    t.copies = 0;
    t.list_count = 2;
    ok = profile_time_compare(&test->arena, &test->report, &t, list_values, 1, "wall", 1);
    profile_test_check(test, "time-runs' comparison of one placement", ok, true, profile_report_text(&test->report),
                       "list 2, wall:\n"
                       "  main: 3 runs, median 200.000\n"
                       "  mine: 3 runs, median 210.000; against main +5.00%\n");

    // main and mine with copies alone, and odd at two placements against main's one: values[c * 4 + e].
    const uint64_t odd_builds[] = { 0, 1, 2, 2 };
    const uint64_t odd_placements[] = { 0, 0, 0, 1 };
    const uint64_t odd_values[8] = { 100, 104, 110, 120, 102, 101, 110, 121 };
    t.builds = odd_builds;
    t.placements = odd_placements;
    t.executable_count = 4;
    t.build_count = 3;
    t.copies = 2;
    t.list_count = 1;
    t.runs = 1;
    ok = profile_time_compare(&test->arena, &test->report, &t, odd_values, 0, "user cycles (thousands)", 1000);
    profile_test_check(test, "time-runs' comparison of unequal placements", ok, true, profile_report_text(&test->report),
                       "list 1, user cycles (thousands):\n"
                       "  main: 2 runs, median 0.102; copies within 2.00%\n"
                       "  mine: 2 runs, median 0.104; copies within 2.97%; against main +1.96% (over copies -0.98% to +4.00%)\n"
                       "  odd: 4 runs, median 0.120; placements 0.110, 0.121 (spread 10.00%); copies within 0.83%; against main +17.65% "
                       "(over placements +7.84% to +18.63%; over placements and copies +7.84% to +21.00%)\n");
}

// Runs argv with its output into `output` (a path), and returns whether it exited with status 0.
static bool profile_test_command(char* const* argv, const char* output)
{
    int status = 1;
    const pid_t child = fork();
    if (child == 0)
    {
        const int fd = open(output, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (fd != -1 && dup2(fd, STDOUT_FILENO) != -1)
        {
            execvp(argv[0], argv);
        }
        _exit(127);
    }
    const bool waited = child != -1 && waitpid(child, &status, 0) == child;
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// #236's reviews: clang's assembly of a program whose static function `function` (an assembler keyword too) has spills
// and prints __func__, labeled, must be the assembly with the labels' lines alone added, and assembled and run must
// print what the program did. Needs clang on PATH.
static void profile_test_program(ProfileTest* test)
{
    static const char source[] = "#include <stdio.h>\n"
                                 "static __attribute__((noinline)) float function(float a)\n"
                                 "{\n"
                                 "    puts(__func__);\n"
                                 "    return a;\n"
                                 "}\n"
                                 "float (*volatile selected)(float) = function;\n"
                                 "int main(int argc, char **argv)\n"
                                 "{\n"
                                 "    (void)argv;\n"
                                 "    return selected((float)argc) != (float)argc;\n"
                                 "}\n";
    const char* temporary = getenv("TMPDIR");
    char directory[512];
    snprintf(directory, sizeof(directory), "%s/buster-test-profile-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    char paths[7][600];
    static const char* const names[7] = { "func.c", "func.s", "labeled.s", "original", "instrumented", "original.txt", "instrumented.txt" };
    const bool made = mkdtemp(directory) != 0;
    for (uint64_t i = 0; i < 7; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", directory, names[i]);
    }
    bool ok = made && profile_write_file(paths[0], profile_test_string(source));
    char* compile[] = { "clang", "-O3", "-g", "-S", paths[0], "-o", paths[1], 0 };
    ok = ok && profile_test_command(compile, "/dev/null");
    const String8 assembly = ok ? profile_read_file(&test->arena, paths[1]) : (String8){ 0 };
    ProfileReport labeled = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
    ok = ok && assembly.pointer != 0 && profile_label_spills(&labeled, &test->report, profile_test_string("function"), profile_test_string("a"), assembly);
    // The function must have spills to label, and the output must be the input with the labels' lines alone added.
    const String8 text = profile_report_text(&labeled);
    ok = ok && profile_find(text, profile_string("buster_spill.a.0:")) != text.length;
    uint64_t at = 0;
    uint64_t input_at = 0;
    while (ok && at < text.length)
    {
        const String8 line = profile_line(text, &at);
        if (!profile_starts_with(line, profile_string("buster_spill.a.")))
        {
            const String8 input_line = profile_line(assembly, &input_at);
            ok = profile_equal(line, input_line);
        }
    }
    ok = ok && input_at == assembly.length;
    ok = ok && profile_write_file(paths[2], text);
    char* link_original[] = { "clang", paths[1], "-o", paths[3], 0 };
    char* link_labeled[] = { "clang", paths[2], "-o", paths[4], 0 };
    ok = ok && profile_test_command(link_original, "/dev/null") && profile_test_command(link_labeled, "/dev/null");
    char* run_original[] = { paths[3], 0 };
    char* run_labeled[] = { paths[4], 0 };
    ok = ok && profile_test_command(run_original, paths[5]) && profile_test_command(run_labeled, paths[6]);
    const String8 printed = ok ? profile_read_file(&test->arena, paths[5]) : (String8){ 0 };
    const String8 printed_labeled = ok ? profile_read_file(&test->arena, paths[6]) : (String8){ 0 };
    ok = ok && printed.pointer != 0 && printed_labeled.pointer != 0 && profile_equal(printed, profile_test_string("function\n")) && profile_equal(printed_labeled, printed);
    test->cases += 1;
    if (!ok)
    {
        test->failures += 1;
        printf("  a labeled program: FAILED (clang's assembly of a program printing __func__, labeled, didn't build and print \"function\"; files in %s)\n", directory);
    }
    else
    {
        for (uint64_t i = 0; i < 7; i += 1)
        {
            unlink(paths[i]);
        }
        rmdir(directory);
    }
    arena_release(&labeled.arena);
    test->report.arena.length = 0;
    test->arena.length = 0;
}

// #236's fifth review: a function whose first instruction is a spill (its argument, around an asm that clobbers every
// vector register), labeled, linked and disassembled with objdump -d -p -t, whose headers show only the function at the
// label's address: spill-report must find the label in the symbol table. Needs clang and objdump on PATH.
static void profile_test_entry_program(ProfileTest* test)
{
    static const char source[] = "__attribute__((noinline)) float f(float a)\n"
                                 "{\n"
                                 "    __asm__ volatile(\"\"\n"
                                 "                     :\n"
                                 "                     :\n"
                                 "                     : \"xmm0\", \"xmm1\", \"xmm2\", \"xmm3\", \"xmm4\", \"xmm5\", \"xmm6\", \"xmm7\",\n"
                                 "                       \"xmm8\", \"xmm9\", \"xmm10\", \"xmm11\", \"xmm12\", \"xmm13\", \"xmm14\", \"xmm15\",\n"
                                 "                       \"xmm16\", \"xmm17\", \"xmm18\", \"xmm19\", \"xmm20\", \"xmm21\", \"xmm22\", \"xmm23\",\n"
                                 "                       \"xmm24\", \"xmm25\", \"xmm26\", \"xmm27\", \"xmm28\", \"xmm29\", \"xmm30\", \"xmm31\");\n"
                                 "    return a;\n"
                                 "}\n"
                                 "int main(int argc, char** argv)\n"
                                 "{\n"
                                 "    (void)argv;\n"
                                 "    return f((float)argc) != (float)argc;\n"
                                 "}\n";
    const char* temporary = getenv("TMPDIR");
    char directory[512];
    snprintf(directory, sizeof(directory), "%s/buster-test-profile-XXXXXX", temporary != 0 && temporary[0] != 0 ? temporary : "/tmp");
    char paths[6][600];
    static const char* const names[6] = { "entry.c", "entry.s", "labeled.s", "entry", "entry.dis", "samples.txt" };
    const bool made = mkdtemp(directory) != 0;
    for (uint64_t i = 0; i < 6; i += 1)
    {
        snprintf(paths[i], sizeof(paths[i]), "%s/%s", directory, names[i]);
    }
    bool ok = made && profile_write_file(paths[0], profile_test_string(source)) && profile_write_file(paths[5], profile_test_string(""));
    char* compile[] = { "clang", "-O3", "-g", "-march=native", "-S", paths[0], "-o", paths[1], 0 };
    ok = ok && profile_test_command(compile, "/dev/null");
    const String8 assembly = ok ? profile_read_file(&test->arena, paths[1]) : (String8){ 0 };
    ProfileReport labeled = { .arena = arena_reserve(BUSTER_PROFILE_REPORT_CAPACITY) };
    ok = ok && assembly.pointer != 0 && profile_label_spills(&labeled, &test->report, profile_test_string("f"), profile_test_string("entry"), assembly);
    // The first spill must be the function's first instruction: its label right after the function's prologue lines.
    const String8 text = profile_report_text(&labeled);
    ok = ok && profile_find(text, profile_string("buster_spill.entry.0:")) != text.length && profile_write_file(paths[2], text);
    char* link[] = { "clang", paths[2], "-o", paths[3], 0 };
    char* run[] = { paths[3], 0 };
    char* disassemble[] = { "objdump", "-d", "-p", "-t", "--no-show-raw-insn", paths[3], 0 };
    ok = ok && profile_test_command(link, "/dev/null") && profile_test_command(run, "/dev/null") && profile_test_command(disassemble, paths[4]);
    const String8 disassembly = ok ? profile_read_file(&test->arena, paths[4]) : (String8){ 0 };
    test->report.arena.length = 0;
    ok = ok && disassembly.pointer != 0 && profile_spill_report(&test->arena, &test->report, profile_test_string("f"), text, disassembly, profile_test_string(""), 24);
    const String8 report = profile_report_text(&test->report);
    ok = ok && profile_starts_with(report, profile_string("spill-report: 0 samples in f at "));
    test->cases += 1;
    if (!ok)
    {
        test->failures += 1;
        printf("  a spill at a function's address: FAILED (%.*s; files in %s)\n", (int)report.length, report.pointer, directory);
    }
    else
    {
        for (uint64_t i = 0; i < 6; i += 1)
        {
            unlink(paths[i]);
        }
        rmdir(directory);
    }
    arena_release(&labeled.arena);
    test->report.arena.length = 0;
    test->arena.length = 0;
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
        profile_test_program(&test);
        profile_test_entry_program(&test);
        profile_test_ab(&test);
        profile_test_time_runs(&test);
        profile_test_time_copies(&test);
        profile_test_time_compare(&test);
        printf("test-profile: %llu checks, %llu failed\n", (unsigned long long)test.cases, (unsigned long long)test.failures);
        result = test.failures == 0 ? 0 : 1;
        arena_release(&test.report.arena);
        arena_release(&test.arena);
    }
    return result;
}
