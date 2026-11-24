// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Transparent unions' arguments, parameters and va_arg (#147), which clang passes as each union's first member
// (useFirstFieldIfTransparentUnion), run against clang -O0's build of the same programs: their calls of the harness's
// functions (check_test_program_harness's, clang's code), the harness's calls of theirs through pointers and their own
// calls of each other, with each kind of member value a conversion to a union selects (check_lower_transparent_value)
// and the unions' own values, named and unnamed, in registers and on the stack; and what the lowering refuses by name.
// Include after c_check_program_test.c's source and execution helpers.

// Calls of the harness's function with each union's members and values; a definition of the program's that takes them
// and passes them on, called by the program and by the harness through a pointer; the registers a short, an unsigned
// short and a _Bool arrive in, which clang's caller extends as their types (signext, zeroext); and an _Atomic union's
// argument, which passes as its atomic type.

// The unions as unnamed arguments, which pass as their first members too, and va_arg's reading of them: the program's
// calls of the harness's variadic function and of its own, and the harness's call of the program's through a pointer.

// The unions after 6 longs and 8 doubles, all on the stack: passed to the harness, received from it through a pointer
// and passed on.

// A union of ExternalTF's layout made transparent after initializers that name no function, and before one that does,
// which clang's code passes as its first member, two floats' bytes in xmm0, where the union's own classes take a general
// register: called with its value and with its first member, directly and through the pointer, the callee the harness's
// clang code (check_test_program_harness's external_transparent_first, of ExternalTF).

// Unions of a stack slot of their own (#1844, BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS): the harness's clang functions
// called with each member's value, a union's own and a member's of another type (a long for an aggregate's union), in
// registers and on the stack; the program's own definitions, called by the program and by the harness through pointers;
// a volatile struct's bytes, which its copy into the union's object reads once.

// The register-classified unions of a slot of their own as unnamed arguments, which pass as their members, and va_arg's
// reading of them from the save area and the overflow area: the program's calls of the harness's variadic function and
// of its own, and the harness's call of the program's through a pointer.

// Unions of a stack slot of their own of a pair's and a vector's members (#1844, BUSTER_CHECK_TEST_TRANSPARENT_OWN_UNIONS):
// a 128-bit integer's in a union of 32 and in a packed one, a 16-byte vector's struct and a complex long, passed to the
// harness's clang functions with the members' and the unions' values, in registers and after 5 longs and 8 doubles, and
// the program's own definitions called by the harness through pointers; and as unnamed arguments, read with va_arg.

// Unions of a LARGER first member in memory (#3400, BUSTER_CHECK_TEST_TRANSPARENT_GROWN_UNIONS), which pass as clang's
// code passes the member: the harness's clang functions called with the members' and the unions' values, in a byval
// slot of the union's bytes where clang has a general register, and as the GROWN member's integer where it has none, in
// r9 and in 8-byte slots (FIRST and LOW past 8 and 16 bytes); the program's own definitions, called by the program and
// by the harness through pointers; and as unnamed arguments, read with va_arg, both ways.

// The reason the lowering refuses a union made transparent after clang's code may have classified a function's type.
static const char check_test_program_transparent_late[] =
    "unions made transparent after a function definition or an initializer naming a function, whose first members pass otherwise";

static void check_test_program_transparent(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/transparent-union-calls", clang);
    check_test_program_boundary_reason(test,
        "union U{struct{float x,y;}p;struct{int a,b;}q;};int f(union U u);int early(union U u){return f(u);}"
        "typedef union U T __attribute__((transparent_union));int tested(int n){return n;}",
        "transparent-union-made-after-a-definition-apart", true, false, 1, 0, check_test_program_transparent_late);
    // The same after a static object's initializer that names a function, whose address clang's code takes where it emits
    // the object, before any definition (#801's review): clang 23.1.4 passes `external`'s and `saved`'s arguments as the
    // union, in a general register, and an aggregate's initializer names it as well. Refused by name, with 0 to 17
    // declarations before them, which put the initializer's declaration in each lane of the vector path's scan and in its
    // tail; the same without a function in the initializers is taken (check_test_program_transparent_first runs it).
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t references;
        bool late;
    } initializers[] =
    {
        { "transparent-union-made-after-an-initializer", "int(*saved)(union U)=external;", 2, true },
        { "transparent-union-made-after-an-aggregate-initializer", "struct S{int(*f)(union U);}s={&external};int(*saved)(union U);", 2, true },
        { "transparent-union-made-after-other-initializers", "int x=1;int*saved_address=&x;int(*saved)(union U);", 2, false },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(initializers); i += 1)
    {
        for (uint32_t padding = 0; padding < 18; padding += 1)
        {
            char source[1024];
            char name[160];
            int length = 0;
            for (uint32_t p = 0; p < padding; p += 1)
            {
                length += snprintf(source + length, sizeof(source) - (size_t)length, "int p%u;", p);
            }
            snprintf(source + length, sizeof(source) - (size_t)length,
                     "union U{struct{float x,y;}p;struct{int a,b;}q;};int external(union U);%stypedef union U T __attribute__((transparent_union));"
                     "int tested(int n){union U u={.q={n,2}};return external(u)+saved(u);}",
                     initializers[i].source);
            snprintf(name, sizeof(name), "%s after %u declarations", initializers[i].name, padding);
            check_test_program_boundary_reason(test, source, name, true, !initializers[i].late, initializers[i].references, 1,
                                               initializers[i].late ? check_test_program_transparent_late : NULL);
        }
    }
    check_test_program_suite(test, "checker/programs/transparent-union-made-between-initializers", clang);
    // A member of an _Atomic type, which clang passes uncounted, keeps its refusal.
    check_test_program_boundary_reason(test,
        "typedef union __attribute__((transparent_union,aligned(32))){_Atomic long w;_Atomic long v;}A;int take(A);int tested(int n){return take(n);}",
        "transparent-union-aligned-atomic", true, false, 1, 0,
        "transparent unions sized or aligned otherwise than their first member, a vector wider than 16 bytes or an _Atomic value");
}
