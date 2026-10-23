// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Clang-compatible excess initializer warnings. Source checking still visits ignored expressions, while the
// resulting plans contain only actual initialization. Direct strict planning is checked by the shared helper.
#ifndef BUSTER_C_CHECK_INITIALIZER_EXCESS_TEST_INCLUDED
#define BUSTER_C_CHECK_INITIALIZER_EXCESS_TEST_INCLUDED

static void check_init_test_excess(CheckTest* test, const char* clang)
{
    static const char source[] =
        "int ignored_call(void);extern int ignored_target;\n"
        "int excess_scalar={7,ignored_call(),&ignored_target,\"ignored\"};\n"
        "int excess_array[2]={1,2,3,ignored_call()};\n"
        "struct E{int a;} excess_record={4,ignored_call(),{5,6}};\n"
        "union U{int a;long b;} excess_union={5,ignored_call()};\n"
        "int excess_nested[2][1]={{6,ignored_call(),[0]=9},{7,8,[0]=10}};\n"
        "int excess_designated[1]={1,2,[0]=9};struct E excess_record_designated={1,2,.a=8};\n"
        "int excess_designator_range[1]={1,2,[9]=3};struct E excess_missing_member={2,3,.missing=4};\n"
        "int designated_override[1]={1,[0]=9};\n"
        "int inferred_excess[][1]={{1,2},{3,4}};int inferred_bytes=sizeof inferred_excess;\n"
        "int ignored_fault={3,1/0};char excess_string[2]=\"long\";int excess_wide[1]=L\"ab\";\n";
    static const CheckInitExpected expected[] = {
        {
            .name = "excess_scalar",
            .bytes = {
                7,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_array",
            .bytes = {
                [0] = 1,
                [4] = 2,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_record",
            .bytes = {
                4,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_union",
            .bytes = {
                5,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_nested",
            .bytes = {
                [0] = 6,
                [4] = 7,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_designated",
            .bytes = {
                1,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_record_designated",
            .bytes = {
                1,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_designator_range",
            .bytes = {
                1,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_missing_member",
            .bytes = {
                2,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "designated_override",
            .bytes = {
                9,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "inferred_excess",
            .bytes = {
                [0] = 1,
                [4] = 3,
            },
            .size = 8,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "inferred_bytes",
            .bytes = {
                8,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "ignored_fault",
            .bytes = {
                3,
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_string",
            .bytes = {
                'l',
                'o',
            },
            .size = 2,
            .class = BUSTER_LINK_CLASS_DATA,
        },
        {
            .name = "excess_wide",
            .bytes = {
                'a',
            },
            .size = 4,
            .class = BUSTER_LINK_CLASS_DATA,
        },
    };
    const CLanguage languages[] = {
        {
            .standard = 1999,
        },
        {
            .standard = 2017,
            .gnu = true,
        },
        {
            .standard = 2023,
        },
    };
    for (uint32_t language = 0; language < BUSTER_ARRAY_LENGTH(languages); language += 1)
    {
        check_init_test_source_language(test, source, expected, BUSTER_ARRAY_LENGTH(expected), true, true, clang, languages[language]);
        check_init_test_source_language(test, "int f(void);int g(int n){int x={n,f()};return x;}\n", 0, 0, true, false, clang, languages[language]);
        check_init_test_source_language(test, "int x={1,*1};\n", 0, 0, false, false, clang, languages[language]);
        check_init_test_source_language(test, "int f(int);int a[1]={1,f()};\n", 0, 0, false, false, clang, languages[language]);
        check_init_test_source_language(test, "int a[1]={1,2,[-1]=3};\n", 0, 0, false, false, clang, languages[language]);
        check_init_test_source_language(test, "int n;int a[1]={1,2,[n]=3};\n", 0, 0, false, false, clang, languages[language]);
        check_init_test_source_language(test, "int f(void){char a[8]=__func__;return sizeof a;}\n", 0, 0, false, false, clang, languages[language]);
        check_init_test_source_language(test, "int f(void){char a[8]={__func__};return sizeof a;}\n", 0, 0, false, false, clang, languages[language]);
        check_init_test_source_language(test, "int f(void){static const char *p=__func__;return p[0];}\n", 0, 0, true, true, clang, languages[language]);
    }
    char directory[] = "/tmp/buster-excess-initializer-XXXXXX";
    const bool created = mkdtemp(directory) != 0;
    char path[160];
    snprintf(path, sizeof(path), "%s/accepted.c", directory);
    char* pointer = path;
    const bool written = created && parse_test_write(path, source, sizeof(source) - 1);
    const char* modes[] = {
        "-std=c99",
        "-std=gnu17",
        "-std=c23",
    };
    for (uint32_t mode = 0; mode < BUSTER_ARRAY_LENGTH(modes); mode += 1)
    {
        ParseTestClang outcome = { 0 };
        check_test_expect(test, written && parse_test_clang(clang, modes + mode, 1, &pointer, 1, &outcome) && !outcome.rejected,
                          "Clang accepts ignored excess elements in ordinary C99, GNU17 and C23 modes");
    }
    if (created)
    {
        unlink(path);
        rmdir(directory);
    }
}

#endif
