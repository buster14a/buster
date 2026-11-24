// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GCC's cast to a union (#2301, clang's CK_ToUnion), run against clang -O0's build of the same programs: a value of a
// member's type initializes that member of a temporary union (check_lower_union_cast), and a static object's
// initializer's cast its member's bytes (check_init_value). The other bytes of a temporary are left as clang's code
// leaves them, so the programs read only the initialized member. Include after c_check_program_test.c's source and
// execution helpers.

// Each kind of member a cast selects: an integer's, a narrower one's, a pointer's, a floating value's, a wide one's, a
// complex one's, a vector's, a struct's, a function's pointer from its designator, a pointer from an array's decay, an
// _Atomic member passed over for a plain one, and a named bit-field; as an initializer, an assignment, an argument, a
// result, a member access and a conditional's operand.

// A static object's initializer: the union's own, an aggregate's member and an array's element, a block's static object,
// and an address constant.

// A transparent union's cast, which selects its member as any union's does, passed to a function taking the union.

static void check_test_program_union_casts(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/program-union-casts", clang);
}
