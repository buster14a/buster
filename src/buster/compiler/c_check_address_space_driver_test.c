// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// The address-space qualifiers and objc_gc's (#1447, CheckInputAttribute.space and type_extend): original fixtures whose
// whole diagnostic output is compared with clang's on both paths, at each warning policy, with -fsyntax-only and -c.
// Include after c_check_warning_driver_test.c
// (check_test_driver_warning_fixtures).

static const CheckDriverDefaultWarningFixture check_driver_address_space_fixtures[] =
{
    {
        // GNU's and OpenCL's address spaces on file-scope objects, typedefs, pointers and arrays, and their spellings in clang's
        // diagnostics: a target's as its attribute, OpenCL's by keyword, a typedef's name after the space; opencl_global_device's
        // and opencl_global_host's deprecation; a second attribute of the same space.
        .name = "address spaces of declarations",
        .source = "#define AS1 __attribute__((address_space(1)))\n"
                  "AS1 int a;\n"
                  "int b __attribute__((address_space(2)));\n"
                  "int * __attribute__((address_space(3))) c;\n"
                  "__attribute__((opencl_global)) int g;\n"
                  "__attribute__((opencl_global_device)) int gd;\n"
                  "__attribute__((opencl_global_host)) int gh;\n"
                  "__attribute__((opencl_local)) const int *l;\n"
                  "typedef __attribute__((address_space(4))) int T4;\n"
                  "T4 t4[3];\n"
                  "int __attribute__((address_space(5))) __attribute__((address_space(5))) twice;\n"
                  "__seg_gs int *gs;\n"
                  "__seg_fs int *fs;\n"
                  "__attribute__((address_space(0))) int zero;\n"
                  "__attribute__((address_space(8388580))) int highest;\n"
                  "AS1 int *f(void) { return &a; }\n"
                  "AS1 int (*h(void))[3];\n",
        .warnings = 3,
    },
    {
        // A typedef's name under an address space, and one whose type has one, in an error's types (its aka); a source's
        // own qualifiers, the pointer's space among them, as clang names a conversion's source.
        .name = "typedef names and qualifiers in an address space's error",
        .source = "typedef int T;\n"
                  "typedef __attribute__((address_space(3))) int U;\n"
                  "__attribute__((address_space(1))) T *a;\n"
                  "const U *d;\n"
                  "__attribute__((address_space(1))) int * __attribute__((address_space(2))) const c;\n"
                  "int *q;\n"
                  "void f(int n) { if (n) q = a; else if (n > 1) q = d; else q = c; }\n",
        .errors = 3,
    },
    {
        // clang's CheckVariableDeclarationType: a block's variable, not a static or extern one.
        .name = "an automatic variable with an address space",
        .source = "void f(void) {\n"
                  "  static __attribute__((address_space(1))) int s;\n"
                  "  extern __attribute__((address_space(1))) int e;\n"
                  "  __attribute__((address_space(1))) int local;\n"
                  "}\n",
        .errors = 1,
    },
    {
        // The array's element's space.
        .name = "an automatic array of an address space",
        .source = "void f(void) { __attribute__((address_space(1))) int a[2]; }\n",
        .errors = 1,
    },
    {
        // A second space on one type.
        .name = "multiple address spaces",
        .source = "int __attribute__((address_space(1))) __attribute__((address_space(2))) two;\n",
        .errors = 1,
    },
    {
        // OpenCL's keyword spaces against address_space's.
        .name = "an OpenCL space after a target's",
        .source = "__attribute__((opencl_global)) int __attribute__((address_space(1))) both;\n",
        .errors = 1,
    },
    {
        // address_space's argument's bounds.
        .name = "a negative address space",
        .source = "int __attribute__((address_space(-1))) negative;\n",
        .errors = 1,
    },
    {
        // clang's largest, 2^23 less its own address spaces.
        .name = "an address space past the largest",
        .source = "int __attribute__((address_space(8388581))) large;\n",
        .errors = 1,
    },
    {
        // address_space's argument is an integer constant.
        .name = "an address space of no integer constant",
        .source = "int n;\n"
                  "int __attribute__((address_space(n))) a;\n",
        .errors = 1,
    },
    {
        // address_space's argument is an integer constant.
        .name = "an address space of a floating constant",
        .source = "int __attribute__((address_space(1.0))) a;\n",
        .errors = 1,
    },
    {
        // address_space's count.
        .name = "address_space without its argument",
        .source = "int __attribute__((address_space)) a;\n",
        .errors = 1,
    },
    {
        // OpenCL's spaces take none.
        .name = "an OpenCL space with an argument",
        .source = "int __attribute__((opencl_local(1))) a;\n",
        .errors = 1,
    },
    {
        // clang's CheckFieldDecl.
        .name = "a member with an address space",
        .source = "struct S { int x; __attribute__((address_space(1))) int m; };\n",
        .errors = 1,
    },
    {
        // clang's CheckParameter, through a typedef.
        .name = "a parameter with an address space",
        .source = "typedef __attribute__((address_space(1))) int T;\n"
                  "void f(T x);\n",
        .errors = 1,
    },
    {
        // An address space after a function's declarator qualifies its type.
        .name = "a function type with an address space",
        .source = "int f(void) __attribute__((address_space(1)));\n",
        .errors = 1,
    },
    {
        // The specifiers' function type.
        .name = "a typedef's function type with an address space",
        .source = "typedef void F(void);\n"
                  "__attribute__((address_space(1))) F f;\n",
        .errors = 1,
    },
    {
        // A function's compound literal.
        .name = "a compound literal with an address space",
        .source = "int *f(void) { return (__attribute__((address_space(1))) int[]){ 1 }; }\n",
        .errors = 1,
    },
    {
        // clang's checkPointerTypesForAssignment, the error's operands in clang's order.
        .name = "assigning a pointer to another address space",
        .source = "__attribute__((address_space(1))) int *p;\n"
                  "int *q;\n"
                  "void f(void) { q = p; }\n",
        .errors = 1,
    },
    {
        // At the declarator's name.
        .name = "initializing a pointer of another address space",
        .source = "__attribute__((address_space(1))) int a[3];\n"
                  "int (*p)[3] = &a;\n",
        .errors = 1,
    },
    {
        // At the returned expression's first token.
        .name = "returning a pointer to another address space",
        .source = "__attribute__((address_space(1))) int a;\n"
                  "int *f(void) { return &a; }\n",
        .errors = 1,
    },
    {
        // The note at the parameter's name.
        .name = "passing a pointer to another address space",
        .source = "void take(int n, int *to);\n"
                  "void f(__attribute__((address_space(2))) int *p) { take(0, p); }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        // The note just past an unnamed parameter.
        .name = "passing a pointer to an unnamed parameter",
        .source = "void take(int *);\n"
                  "void f(__seg_gs int *p) { take(p); }\n",
        .errors = 1,
        .notes = 1,
    },
    {
        // Neither space includes the other.
        .name = "assigning between two target spaces",
        .source = "__attribute__((address_space(1))) int *p;\n"
                  "__attribute__((address_space(2))) int *r;\n"
                  "void f(void) { r = p; }\n",
        .errors = 1,
    },
    {
        // A void pointee's space too.
        .name = "assigning to a void pointer of another space",
        .source = "__attribute__((address_space(1))) void *v;\n"
                  "int *q;\n"
                  "void f(void) { v = q; }\n",
        .errors = 1,
    },
    {
        // clang's err_typecheck_op_on_nonoverlapping_address_space_pointers.
        .name = "subtracting pointers to non-overlapping spaces",
        .source = "__attribute__((opencl_global)) int *g;\n"
                  "__attribute__((opencl_local)) int *l;\n"
                  "long f(void) { return g - l; }\n",
        .errors = 1,
    },
    {
        // At the conditional's `?`.
        .name = "a conditional of non-overlapping spaces",
        .source = "__attribute__((address_space(1))) int *p;\n"
                  "int *q;\n"
                  "void f(int c) { void *v = c ? p : q; }\n",
        .errors = 1,
    },
    {
        // clang's "changes address space of nested pointer": a level's space differing under pointers is an error,
        // where a level's const only warns.
        .name = "nested pointer address spaces",
        .source = "__attribute__((address_space(1))) int **pp;\n"
                  "__attribute__((address_space(2))) int **qq;\n"
                  "__attribute__((address_space(1))) int **pp2;\n"
                  "const int **cp;\n"
                  "int **plain;\n"
                  "void f(void) {\n"
                  "  pp = qq;\n"
                  "  plain = pp;\n"
                  "  pp = plain;\n"
                  "  pp = pp2;\n"
                  "  cp = plain;\n"
                  "}\n",
        .errors = 3,
        .warnings = 1,
    },
    {
        // The nested space error reaches levels the walk compares: a depth mismatch below it stays clang's
        // incompatible-pointer-types, a base-type one under a reached space still names the space, and the
        // argument's note names the parameter, not an attribute's argument.
        .name = "nested pointer address spaces at mismatched depths",
        .source = "__attribute__((address_space(1))) int ***deep;\n"
                  "__attribute__((address_space(1))) int **mid;\n"
                  "__attribute__((address_space(1))) int **ai;\n"
                  "double **dp;\n"
                  "struct S { int x; } **sp;\n"
                  "int **ip;\n"
                  "void take(__attribute__((address_space(2))) int **p);\n"
                  "void f(void) {\n"
                  "  deep = mid;\n"
                  "  ai = dp;\n"
                  "  ai = sp;\n"
                  "  deep = ip;\n"
                  "  take(ai);\n"
                  "}\n",
        .errors = 5,
        .notes = 1,
    },
    {
        // A value has no address space: a deduced type drops the initializer's, while a pointer's keeps its pointee's.
        .name = "a deduced type has no address space",
        .source = "__attribute__((address_space(1))) int a;\n"
                  "void f(void) {\n"
                  "  __auto_type b = a;\n"
                  "  int *p = &b;\n"
                  "  __auto_type c = &a;\n"
                  "  __attribute__((address_space(1))) int *q = c;\n"
                  "}\n",
    },
    {
        // String literals initialize arrays of an address space's character, sized and unsized, braced too.
        .name = "string literals into address-space arrays",
        .source = "__attribute__((address_space(1))) char a[] = \"hi\";\n"
                  "__attribute__((address_space(1))) char b[4] = \"hi\";\n"
                  "__attribute__((address_space(1))) char c[] = {\"hi\"};\n"
                  "__attribute__((address_space(1))) char d[2][3] = {\"ab\", \"cd\"};\n",
    },
    {
        // OpenCL's generic includes every space but constant, global its device and host parts; a
        // conditional's result drops its operands' spaces unless they are one type (its type in a later error's words);
        // comparisons and casts of any spaces.
        .name = "conversions within an address space",
        .source = "__attribute__((opencl_global)) int *gl;\n"
                  "__attribute__((opencl_generic)) int *ge;\n"
                  "__attribute__((opencl_global_device)) int *gd;\n"
                  "__attribute__((address_space(1))) int *p;\n"
                  "const __attribute__((address_space(1))) int *cp;\n"
                  "int *q;\n"
                  "void f(int c) {\n"
                  "  ge = gl; gl = gd; ge = gd;\n"
                  "  q = (int *)p; p = (__attribute__((address_space(1))) int *)q;\n"
                  "  (void)(p == q); (void)(p < q); (void)(gl == ge);\n"
                  "  (void)(c ? gl : gl); (void)(c ? ge : gl); (void)(c ? p : 0);\n"
                  "  long d = ge - gl;\n"
                  "}\n",
        .warnings = 1,
    },
    {
        // A conditional's type, as _Generic selects it: pointers of two overlapping spaces make one of none (clang's C),
        // one type keeps its space, a void pointee takes the other's; a value has no space, and __builtin_types_compatible_p
        // compares types without theirs.
        .name = "a conditional's result drops its spaces",
        .source = "__attribute__((opencl_global)) int *gl;\n"
                  "__attribute__((opencl_generic)) int *ge;\n"
                  "__attribute__((address_space(1))) int *p;\n"
                  "__attribute__((address_space(1))) void *v;\n"
                  "const __attribute__((address_space(1))) int *cp;\n"
                  "_Static_assert(_Generic(0 ? ge : gl, int *: 1, default: 0), \"overlapping spaces drop\");\n"
                  "_Static_assert(_Generic(0 ? gl : gl, __attribute__((opencl_global)) int *: 1, default: 0), \"one type keeps\");\n"
                  "_Static_assert(_Generic(0 ? p : v, __attribute__((address_space(1))) void *: 1, default: 0), \"void takes the space\");\n"
                  "_Static_assert(_Generic(0 ? p : cp, const int *: 1, default: 0), \"qualifiers merge, space dropped\");\n"
                  "_Static_assert(_Generic(0 ? p : 0, __attribute__((address_space(1))) int *: 1, default: 0), \"null pointer\");\n"
                  "_Static_assert(_Generic(p, int *: 0, __attribute__((address_space(1))) int *: 1), \"distinct association types\");\n"
                  "_Static_assert(__builtin_types_compatible_p(__typeof__(*p), int) == 1, \"qualifiers dropped\");\n"
                  "_Static_assert(sizeof(*p) == 4 && _Alignof(__attribute__((address_space(1))) long) == 8, \"size and alignment\");\n"
                  "_Static_assert(_Generic(*p, int: 1, default: 0), \"a value has no space\");\n"
                  "_Static_assert(_Generic(cp, const __attribute__((address_space(1))) int *: 1, default: 0), \"qualified pointee\");\n"
                  "int x = _Generic(*cp, int: 1);\n",
    },
    {
        // A member's lvalue takes its record's space.
        .name = "members through an address space",
        .source = "struct S { int m; };\n"
                  "__attribute__((address_space(1))) struct S s;\n"
                  "int *f(void) { return &s.m; }\n",
        .errors = 1,
    },
    {
        // objc_gc's weak and strong on pointers, an inner pointer's and an array's element's too; a local's and a
        // member's weak warnings; the qualifier dropped or added by an assignment.
        .name = "garbage collection qualifiers",
        .source = "int * __attribute__((objc_gc(weak))) w;\n"
                  "int * __attribute__((objc_gc(strong))) s;\n"
                  "__attribute__((objc_gc(weak))) int *moved;\n"
                  "int * __attribute__((objc_gc(weak))) *inner;\n"
                  "int * __attribute__((objc_gc(weak))) array[2];\n"
                  "struct M { int * __attribute__((objc_gc(weak))) m; };\n"
                  "int **plain;\n"
                  "void f(void) {\n"
                  "  int * __attribute__((objc_gc(weak))) local;\n"
                  "  int * __attribute__((objc_gc(strong))) kept;\n"
                  "  plain = &w; inner = plain;\n"
                  "}\n",
        .warnings = 2,
    },
    {
        // Two on one pointer.
        .name = "multiple garbage collection qualifiers",
        .source = "int * __attribute__((objc_gc(weak))) __attribute__((objc_gc(strong))) w;\n",
        .errors = 1,
    },
    {
        // __seg_gs's and __seg_fs's loads and stores, which take the segment's override (#1447), at -c too.
        .name = "segment accesses",
        .source = "struct S { int a, b; };\n"
                  "int f(__seg_gs int *p) { return *p + p[1]; }\n"
                  "void g(__seg_fs struct S *s, int v) { s->b = v; }\n",
    },
    {
        // A generic atomic builtin's pointers that take the object's pointer type (a store's value, an exchange's result,
        // a compare-exchange's desired) in another address space than the object's (#2233): clang's "changes address
        // space of pointer". The others take a pointer to the object's value type, which drops the space, as clang's do.
        .name = "generic atomic builtins' pointers in another address space",
        .source = "void a(int __seg_gs *p, int *v, int *o) { __atomic_exchange(p, v, o, 5); }\n"
                  "void b(int __seg_gs *p, int *r) { __atomic_load(p, r, 5); }\n"
                  "void c(int __seg_gs *p, int *v) { __atomic_store(p, v, 5); }\n"
                  "_Bool d(int __seg_gs *p, int *e, int *w) { return __atomic_compare_exchange(p, e, w, 0, 5, 5); }\n"
                  "void a2(int __seg_gs *p, int __seg_gs *v, int *o) { __atomic_exchange(p, v, o, 5); }\n"
                  "void a3(int *p, int __seg_gs *v, int *o) { __atomic_exchange(p, v, o, 5); }\n"
                  "void b3(int *p, int __seg_gs *r) { __atomic_load(p, r, 5); }\n"
                  "void c3(int *p, int __seg_gs *v) { __atomic_store(p, v, 5); }\n"
                  "_Bool d3(int *p, int __seg_gs *e, int __seg_gs *w) { return __atomic_compare_exchange(p, e, w, 0, 5, 5); }\n",
        .errors = 6,
    },
};

static void check_test_driver_address_spaces(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    check_test_driver_warning_fixtures(test, clang, executable, directory, check_driver_address_space_fixtures,
                                       BUSTER_ARRAY_LENGTH(check_driver_address_space_fixtures));
    // #1844: the specifiers' space survives deduction at its own level, with the same storage and redeclaration
    // errors as an explicitly spelled type. Sources remain runtime data, so adding cases does not bake them into the
    // compiler. The shared reader fails on missing, truncated or NUL-containing input.
    static const CheckDriverDefaultWarningFixture cases[] =
    {
        {
            .name = "valid",
        },
        {
            .name = "automatic",
            .errors = 3,
        },
        {
            .name = "redeclaration",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-macro",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-pointer",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-drop",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-macro-qualified",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-macro-atomic",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-macro-trailing",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-macro-trailing-atomic",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "redeclaration-qualified",
            .errors = 1,
            .notes = 1,
        },
        {
            .name = "restrict-specifier",
            .errors = 1,
        },
        {
            .name = "negative",
            .errors = 1,
        },
        {
            .name = "too-large",
            .errors = 1,
        },
        {
            .name = "nonconstant",
            .errors = 1,
        },
        {
            .name = "noninteger",
            .errors = 1,
        },
        {
            .name = "conflicting",
            .errors = 1,
        },
        {
            .name = "duplicate",
            .warnings = 1,
        },
    };
    CheckTestFixtures files = { .storage = arena_reserve(CHECK_TEST_FIXTURE_BYTES + 4096) };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(cases); i += 1)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "tests/driver/auto-address/%s.c", cases[i].name);
        uint64_t length = 0;
        CheckDriverDefaultWarningFixture fixture = cases[i];
        fixture.source = files.storage.base != NULL ? check_test_fixture_read(&files, path, &length) : NULL;
        check_test_expect(test, fixture.source != NULL, path);
        if (fixture.source != NULL)
        {
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
            fixture.c23 = true;
            check_test_driver_warning_fixtures(test, clang, executable, directory, &fixture, 1);
        }
        arena_pop_to(&files.storage, 0);
    }
    arena_release(&files.storage);
}
