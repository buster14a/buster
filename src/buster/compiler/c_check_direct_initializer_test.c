// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang's C direct initializer after attributes or an asm label (#1252). The initializer helpers run the scalar
// and coded paths and compare static bytes with Clang; the program helper compares native results too.
#ifndef BUSTER_C_CHECK_DIRECT_INITIALIZER_TEST_INCLUDED
#define BUSTER_C_CHECK_DIRECT_INITIALIZER_TEST_INCLUDED

static void check_test_direct_initializers(CheckTest* test, const char* clang)
{
    static const char source[] =
        "int absent(void);extern int absent_object;\n"
        "int direct_one __attribute__((unused)) (3);\n"
        "int renamed __asm__(\"direct_asm\") (4);\n"
        "int direct_first __attribute__((unused)) (5,absent(),&absent_object,(void)0);\n"
        "char direct_string[] __attribute__((unused)) (\"abc\");\n"
        "int direct_size __attribute__((unused)) (sizeof direct_size), after=9;\n"
        "__auto_type direct_auto __attribute__((unused)) (11);\n";
    static const CheckInitExpected expected[] = {
        { .name = "direct_one", .bytes = { 3 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "direct_asm", .bytes = { 4 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "direct_first", .bytes = { 5 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "direct_string", .bytes = { 'a', 'b', 'c', 0 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "direct_size", .bytes = { 4 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "after", .bytes = { 9 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
        { .name = "direct_auto", .bytes = { 11 }, .size = 4, .class = BUSTER_LINK_CLASS_DATA },
    };
    check_init_test_source(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang);
    static const char* const invalid[] = {
        "struct P{int a,b;} p __attribute__((unused)) (1);",
        "int x __attribute__((unused)) (unknown);",
        "int x __attribute__((unused)) (3,unknown);",
        "int x __attribute__((unused)) (3,*(void)0);",
        "int f(void);int x __attribute__((unused)) (f(),3);",
        "char a[] __attribute__((unused)) (\"abc\",4);",
        "__auto_type x __attribute__((unused)) (3,4);",
        "__auto_type x __attribute__((unused)) (x);",
        "typedef int T __attribute__((unused)) (3);",
        "void f(void){extern int x __attribute__((unused)) (3);}",
        "void f(int n){int x[n] __attribute__((unused)) (1);}",
        "int x __attribute__((unused)) (3);int x __attribute__((unused)) (4);",
        "int x __attribute__((unused)) ();",
        "int x(3);",
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        check_init_test_source(test, invalid[i], NULL, 0, false, false, clang);
    }
    const CLanguage c23 = { .standard = 2023, .gnu = true };
    check_init_test_source_language(test, "auto x __attribute__((unused)) (3);constexpr int y __attribute__((unused)) (4);",
                                   NULL, 0, true, false, clang, c23);
    check_init_test_source_language(test, "auto x __attribute__((unused)) (3,4);", NULL, 0, false, false, clang, c23);
    check_test_program_source(test,
        "int absent(void);int g;int tested(int n){g=0;int x __attribute__((unused)) (n+3,g++,absent());"
        "int y __asm__(\"local_y\") (x*2);return x+y+g*100;}", "direct-first-argument-only", clang);
    check_test_program_source(test,
        "int absent(void);int tested(int n){int a=0;int x __attribute__((unused)) ((a++,n+3),a++,absent());"
        "int y __attribute__((unused)) (n? a++,n+4 : n-2,absent());return a*100+x+y;}", "direct-nested-comma-and-conditional", clang);
    check_test_program_source(test,
        "int tested(int n){struct P{int a,b;};struct P a={n,n+7};"
        "struct P b __attribute__((unused)) (a,n++);int *p __attribute__((unused)) (&b.a);"
        "char text[] __attribute__((unused)) (\"abc\");return *p+b.b+text[1]+sizeof text;}", "direct-aggregate-pointer-and-string", clang);
    check_test_program_source(test,
        "int tested(int n){int sum=0;for(int i __attribute__((unused)) (0);i<n+4;i++)sum+=i;"
        "int a __attribute__((unused)) (sizeof a),b __attribute__((unused)) (a+2);"
        "__auto_type c __attribute__((unused)) (b);return sum+a+b+c;}", "direct-scope-for-and-deduction", clang);
}

#endif
