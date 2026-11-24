// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU's alias, ifunc and weakref attributes (#147), run against clang -O0's build of the same programs: aliases of
// static functions and objects (thread-local ones too), global and local, and chains of them, with static data that
// refers to local ones before they have their places (check_data_emit's local targets); ifuncs whose resolvers
// return a function pointer and a void pointer, called directly and through pointers; and weakrefs to the harness's
// function and object (check_test_program_harness's external_helper and external_data), to a name nothing defines, to a
// static function and to a static declaration nothing defines, whose weak reference is a global symbol's.
// The oracle renames the program's global definitions (check_test_program_oracle), so each attribute's name is a
// static's, which both objects keep apart (an ifunc is global, so no alias names one), and only a function, whose copies
// compute alike, has a weak alias, which the link takes the oracle's of. Include after c_check_program_test.c's source
// and execution helpers.

static void check_test_program_aliases(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-aliases", clang);
}
