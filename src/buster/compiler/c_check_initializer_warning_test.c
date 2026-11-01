// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original brace/override diagnostics. Clang runs externally; no upstream fixture or implementation is included.
// Include after the driver spawn, diagnostic-count and file-comparison helpers. The table is also usable by
// focused source adapters. Compare complete no-caret diagnostics: location, wording, option, notes and order.
// The four PP timing controls isolate the existing #warning option-suffix/global-promotion boundary below.

typedef struct CheckDriverInitWarningFixture CheckDriverInitWarningFixture;
struct CheckDriverInitWarningFixture
{
    const char* name;
    const char* source;
    uint32_t warnings;
    uint32_t notes;
    bool preprocessing;
};

static const CheckDriverInitWarningFixture check_driver_init_warning_fixtures[] =
{
    {
        .name = "ordinary scalar initializer is quiet",
        .source = "int value=1;\n",
    },
    {
        .name = "first scalar brace is quiet",
        .source = "int value={1};\n",
    },
    {
        .name = "one extra scalar brace",
        .source = "int value={{1}};\n",
        .warnings = 1,
    },
    {
        .name = "two extra scalar braces have separate events",
        .source = "int value={{{1}}};\n",
        .warnings = 2,
    },
    {
        .name = "repeated record member targets the later value",
        .source = "struct S{int x;};\nstruct S value={.x=1,.x=2};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "repeated array element targets the later value",
        .source = "int value[2]={[0]=1,[0]=2};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "changing union member targets its designator",
        .source = "union U{int x;int y;};\nunion U value={.x=1,.y=2};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "repeating union member targets its value",
        .source = "union U{int x;int y;};\nunion U value={.x=1,.x=2};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "whole array replacement notes the old container",
        .source = "struct S{int a[2];};\nstruct S value={.a[0]=1,.a={2,3}};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "array element replacement notes the old leaf",
        .source = "struct S{int a[2];};\nstruct S value={.a={2,3},.a[0]=1};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "implicit array zero is not prior initialization",
        .source = "struct S{int a[2];};\nstruct S value={.a={1},.a[1]=2};\n",
    },
    {
        .name = "whole record replacement notes the old container",
        .source = "struct S{int x;int y;};struct P{struct S s;};\nstruct P value={.s.x=1,.s={2,3}};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "record field replacement notes the old leaf",
        .source = "struct S{int x;int y;};struct P{struct S s;};\nstruct P value={.s={2,3},.s.x=1};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "string element update partially replaces a byte span",
        .source = "struct S{char a[4];};\nstruct S value={.a=\"abc\",.a[1]='x'};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "range replay retains one warning per replaced subobject",
        .source = "int value[4]={[0 ... 3]=1,[1 ... 2]=2};\n",
        .warnings = 2,
        .notes = 2,
    },
    {
        .name = "individual replacements retain each range origin",
        .source = "int value[3]={[0 ... 2]=1,[0]=2,[1]=3,[2]=4};\n",
        .warnings = 3,
        .notes = 3,
    },
    {
        .name = "overridden scalar call notes omitted side effects",
        .source = "extern int next(void);\nvoid f(void){int value[1]={[0]=next(),[0]=2};(void)value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "partially replaced aggregate call remains a prior initializer",
        .source = "struct S{int x;int y;};struct P{struct S s;};extern struct S make(void);\n"
                  "void f(void){struct P value={.s=make(),.s.x=1};(void)value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "whole aggregate replacement notes omitted side effects",
        .source = "struct S{int x;int y;};struct P{struct S s;};extern struct S make(void);\n"
                  "void f(void){struct P value={.s=make(),.s={1,2}};(void)value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "equal sized child replacement is still partial",
        .source = "struct I{int x;};struct O{struct I i;};struct P{struct O o;};extern struct O make(void);\n"
                  "void f(void){struct P value={.o=make(),.o.i={1}};(void)value;}\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "scalar brace completion crosses preprocessing events",
        .source = "int value={\n#warning before-extra\n{\n#warning after-extra\n1}};\n",
        .warnings = 3,
        .preprocessing = true,
    },
    {
        .name = "override completion follows the complete expression",
        .source = "int value[1]={[0]=1,[0]=(2\n#warning inside-replacement\n+3)\n#warning after-replacement\n};\n",
        .warnings = 3,
        .notes = 1,
        .preprocessing = true,
    },
    {
        .name = "union member change observes designator lookahead",
        .source = "union U{int x;int y;};\nunion U value={.x=1,\n#warning before-designator\n.y\n#warning after-designator\n=2};\n",
        .warnings = 3,
        .notes = 1,
        .preprocessing = true,
    },
    {
        .name = "whole array replacement observes opening brace lookahead",
        .source = "struct S{int a[2];};\nstruct S value={.a[0]=1,.a=\n#warning before-brace\n{\n#warning after-brace\n2,3}};\n",
        .warnings = 3,
        .notes = 1,
        .preprocessing = true,
    },
    {
        .name = "macro values retain original primary and prior locations",
        .source = "#define OLD 1\n#define NEW 2\nint value[1]={[0]=OLD,[0]=NEW};\n",
        .warnings = 1,
        .notes = 3,
    },
    {
        .name = "presumed locations retain semantic override order",
        .source = "int value[1]={\n#line 900 \"earlier-init.c\"\n[0]=1,\n#line 2 \"later-init.c\"\n[0]=2};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "source policy keeps override notes with their primary",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Winitializer-overrides\"\n"
                  "int quiet[1]={[0]=1,[0]=2};\n#pragma clang diagnostic pop\nint loud[1]={[0]=3,[0]=4};\n",
        .warnings = 1,
        .notes = 1,
    },
    {
        .name = "source policy suppresses only the extra brace family",
        .source = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Wmany-braces-around-scalar-init\"\n"
                  "int quiet={{{1}}};\n#pragma clang diagnostic pop\nint loud={{2}};\n",
        .warnings = 1,
    },
};

// Existing PP text lacks Clang's #warning option label and does not implement its global -Werror mapping.
// Only these eight original timing-control messages may omit that suffix. Every source header, message,
// note and relative event stays exact; brace/override option labels never pass through this exception.
static void check_test_driver_initializer_pp_options(char* text)
{
    static const char* const controls[] =
    {
        "before-extra",
        "after-extra",
        "inside-replacement",
        "after-replacement",
        "before-designator",
        "after-designator",
        "before-brace",
        "after-brace",
    };
    static const char option[] = " [-W#warnings]";
    for (char* at = text; at != NULL && *at != 0;)
    {
        char* end = strchr(at, '\n');
        const uint64_t length = end != NULL ? (uint64_t)(end - at) : strlen(at);
        char* primary = memmem(at, length, ": warning: ", 11);
        if (primary != NULL)
        {
            char* message = primary + 11;
            const uint64_t available = length - (uint64_t)(message - at);
            bool removed = false;
            for (uint32_t i = 0; !removed && i < BUSTER_ARRAY_LENGTH(controls); i += 1)
            {
                const uint64_t control = strlen(controls[i]);
                if (available == control + sizeof(option) - 1 && memcmp(message, controls[i], control) == 0 &&
                    memcmp(message + control, option, sizeof(option) - 1) == 0)
                {
                    char* suffix = message + control;
                    memmove(suffix, suffix + sizeof(option) - 1, strlen(suffix + sizeof(option) - 1) + 1);
                    if (end != NULL)
                    {
                        end -= sizeof(option) - 1;
                    }
                    removed = true;
                }
            }
        }
        at = end != NULL ? end + 1 : NULL;
    }
}

static void check_test_driver_initializer_warnings(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const policies[] = { NULL, "-Werror", "-w" };
    char source[PATH_MAX];
    char objects[3][PATH_MAX];
    snprintf(source, sizeof(source), "%s/initializer-warning.c", directory);
    for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
    {
        snprintf(objects[route], sizeof(objects[route]), "%s/initializer-warning-%u.o", directory, route);
    }
    for (uint32_t f = 0; f < BUSTER_ARRAY_LENGTH(check_driver_init_warning_fixtures); f += 1)
    {
        const CheckDriverInitWarningFixture* fixture = check_driver_init_warning_fixtures + f;
        const bool written = parse_test_write(source, fixture->source, strlen(fixture->source));
        check_test_expect(test, written, "original initializer diagnostic fixture is written");
        const uint32_t policy_count = fixture->preprocessing ? 1 : BUSTER_ARRAY_LENGTH(policies);
        for (uint32_t policy = 0; written && policy < policy_count; policy += 1)
        {
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                char* arguments[12] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", "-fno-color-diagnostics",
                                        mode == 0 ? "-fsyntax-only" : "-c", source, "-o", objects[2] };
                uint32_t count = 8;
                if (policies[policy] != NULL)
                {
                    arguments[count++] = (char*)policies[policy];
                }
                char* expected = NULL;
                unlink(objects[2]);
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const CheckDriverDiagnosticCounts counts = check_test_driver_diagnostic_counts(expected);
                const bool oracle_ready = (oracle == 0 || oracle == 1) && expected != NULL;
                if (fixture->preprocessing)
                {
                    check_test_driver_initializer_pp_options(expected);
                }
                char description[256];
                snprintf(description, sizeof(description), "%s policy=%u mode=%u: independent Clang oracle", fixture->name, policy, mode);
                check_test_expect(test, oracle_ready && (policy != 0 || (oracle == 0 && counts.warnings == fixture->warnings &&
                                       counts.errors == 0 && counts.notes == fixture->notes)), description);
                for (uint32_t scalar = 0; oracle_ready && scalar < 2; scalar += 1)
                {
                    arguments[0] = (char*)(executable != NULL ? executable : "/proc/self/exe");
                    arguments[7] = objects[scalar];
                    arguments[count] = scalar != 0 ? "--scalar" : NULL;
                    arguments[count + 1] = NULL;
                    unlink(objects[scalar]);
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    if (fixture->preprocessing)
                    {
                        check_test_driver_initializer_pp_options(actual);
                    }
                    snprintf(description, sizeof(description), "%s policy=%u mode=%u %s: exact diagnostic text, location, notes and order",
                             fixture->name, policy, mode, scalar != 0 ? "scalar" : "vector");
                    check_test_expect(test, status == oracle && actual != NULL && strcmp(actual, expected) == 0, description);
                    check_test_expect(test, (access(objects[scalar], F_OK) == 0) == (mode != 0 && status == 0),
                                      "initializer warning severity controls object publication");
                    free(actual);
                }
                if (oracle_ready && mode != 0 && oracle == 0)
                {
                    check_test_expect(test, check_test_driver_same_files(objects[0], objects[1]),
                                      "initializer warning policy preserves identical scalar/vector objects");
                }
                free(expected);
                for (uint32_t route = 0; route < BUSTER_ARRAY_LENGTH(objects); route += 1)
                {
                    unlink(objects[route]);
                }
            }
        }
    }
    unlink(source);
}
