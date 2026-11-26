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

static void check_test_program_transparent(CheckTest* test, const char* clang)
{
    check_test_program_suite(test, "checker/programs/transparent-union-calls", clang);
    check_test_program_suite(test, "checker/programs/transparent-union-late", clang);
    check_test_program_boundary_reason(test,
        "union U{struct{float x,y;}p;struct{int a,b;}q;};int f(union U u);int early(union U u){return f(u);}"
        "typedef union U T __attribute__((transparent_union));int tested(int n){return n;}",
        "transparent-union-made-after-a-definition-apart", true, true, 1, 0, NULL);
    // The same after an object's initializer that names a function, whose address clang's code takes where it emits
    // the object, before any definition (#801's review): clang 23.1.4 passes `external`'s and `saved`'s arguments as the
    // union, in a general register, and an aggregate's initializer names it as well. Accepted, with 0 to 17
    // declarations before them, which put the initializer's declaration in each lane of the vector path's scan and in its
    // tail; the same without a function in the initializers is taken (check_test_program_transparent_first runs it).
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t references;
    } initializers[] =
    {
        { "transparent-union-made-after-an-initializer", "int(*saved)(union U)=external;", 2 },
        { "transparent-union-made-after-an-aggregate-initializer", "struct S{int(*f)(union U);}s={&external};int(*saved)(union U);", 2 },
        { "transparent-union-made-after-other-initializers", "int x=1;int*saved_address=&x;int(*saved)(union U);", 2 },
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
            check_test_program_boundary_reason(test, source, name, true, true, initializers[i].references, 1, NULL);
        }
    }
    check_test_program_suite(test, "checker/programs/transparent-union-made-between-initializers", clang);
    // An atomic first member keeps the union's own byval slot while passing uncounted in memory.
    check_test_program_boundary_reason(test,
        "typedef union __attribute__((transparent_union,aligned(32))){_Atomic long w;_Atomic long v;}A;int take(A);int tested(int n){return take(n);}",
        "transparent-union-aligned-atomic", true, true, 1, 0, NULL);
    // A dump's synthetic printer signatures still have a named boundary; accepting source calls must not silently
    // pass their late context in the first member's registers after clang has frozen the ordinary union convention.
    check_test_program_boundary_reason(test,
        "union U{struct{float x,y;}p;struct{int a,b;}q;};int printer(union U,const char*,...);"
        "int seed(union U u){return printer(u,\"\");}typedef union U T __attribute__((transparent_union));"
        "int tested(int n){struct E{}e;union U u={.q={n,n+1}};__builtin_dump_struct(&e,printer,u);return n;}",
        "transparent-union-late-dump-printer", true, false, 2, 1,
        "__builtin_dump_struct for a printer's late transparent union");
    // These function-type attributes have additional clang ABI-cache metadata. Their late, ABI-changing unions keep
    // a named boundary; an attribute in place before any emission, an ignored attribute, or an unchanged ABI does not.
    static const struct
    {
        const char* name;
        const char* function;
        const char* parameter;
    } metadata[] =
    {
        { "regparm", "__attribute__((regparm(3)))", "" },
        { "noescape", "", "__attribute__((noescape))" },
        { "pass-object-size", "", "__attribute__((pass_object_size(0)))" },
        { "pass-dynamic-object-size", "", "__attribute__((pass_dynamic_object_size(0)))" },
    };
    for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(metadata); a += 1)
    {
        for (uint32_t early = 0; early < 2; early += 1)
        {
            char source[1024];
            char name[128];
            const char* transparent = "typedef union U T __attribute__((transparent_union));";
            snprintf(source, sizeof(source),
                     "union U{struct{float x,y;}p;struct{int a,b;}q;};%s"
                     "int %s take(union U u,char*const p %s){return u.q.a;}%s"
                     "int tested(int n){union U u={.q={n,2}};return take(u,0);}",
                     early ? transparent : "", metadata[a].function, metadata[a].parameter, early ? "" : transparent);
            snprintf(name, sizeof(name), "transparent-union-%s-%s", early ? "early" : "late", metadata[a].name);
            check_test_program_boundary_reason(test, source, name, true, early != 0, 1, 1,
                early ? NULL : "late transparent unions with regparm, noescape, object-size or retained-result function attributes");
        }
    }
    check_test_program_boundary_reason(test,
        "union U{struct{float x,y;}p;struct{int a,b;}q;};int take(union U u,int p){return u.q.a;}"
        "typedef union U T __attribute__((transparent_union));int tested(int n){union U u={.q={n,2}};return take(u,0);}",
        "transparent-union-late-default-metadata", true, true, 1, 1, NULL);
    check_test_program_boundary_reason(test,
        "union U{struct{float x,y;}p;struct{int a,b;}q;};int take(union U u,int p __attribute__((noescape))){return u.q.a;}"
        "typedef union U T __attribute__((transparent_union));int tested(int n){union U u={.q={n,2}};return take(u,0);}",
        "transparent-union-late-ignored-metadata", true, true, 1, 1, NULL);
    check_test_program_boundary_reason(test,
        "union U{long p;void*q;};int __attribute__((regparm(3))) take(union U u){return (int)u.p;}"
        "typedef union U T __attribute__((transparent_union));int tested(int n){union U u={.p=n};return take(u);}",
        "transparent-union-late-metadata-unchanged-abi", true, true, 1, 1, NULL);
}
