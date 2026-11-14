// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original typed-read and pointer-provenance controls, checked independently by external Clang.
// Include after CheckSourceFixture and check_test_source; no native-program helpers are needed.


static void check_test_const_alias_sources(CheckTest* test, const char* clang)
{
    // Its source fixtures are runtime data (#2053): tests/checker/const-alias, which check_test_suite reads and runs.
    check_test_suite(test, clang, "checker/const-alias", "typed const-alias source Clang oracle is available");
}
