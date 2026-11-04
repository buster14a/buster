// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #891's remaining emission-entry cases, included after c_driver_compatibility_test.c's process helpers. Clang is
// the independent oracle: compare every error's location, message and order, not just rejection or the first error.
// The explicit counts and syntax-only runs prevent a shared omission or an unrelated Sema error from passing.
// These original fixtures run through test-checker-driver and the standalone CC compatibility harness; no native
// code from the fixtures is executed. The production range-warning walk (#487) is not used as an oracle.

#define BUSTER_CC_TARGET_FEATURE_DIAGNOSTICS (16u * 1024u)

// Keep only located error headers, preserving their order and complete messages. Paths, source excerpts, carets
// and notes are not part of this comparison. An unlocated error or a full destination fails rather than truncating.
static bool cc_compat_target_feature_diagnostics(const char* text, char* output, size_t capacity, uint32_t* count)
{
    bool valid = text != NULL && capacity != 0;
    size_t used = 0;
    *count = 0;
    if (capacity != 0)
    {
        output[0] = 0;
    }
    for (const char* at = text; valid && *at != 0;)
    {
        const char* newline = strchr(at, '\n');
        const char* end = newline != NULL ? newline : at + strlen(at);
        const char* error = strstr(at, "error: ");
        if (error != NULL && error < end)
        {
            unsigned line = 0;
            unsigned column = 0;
            valid = sscanf(at, "%*[^:]:%u:%u:", &line, &column) == 2 && line != 0 && column != 0 && end - error <= INT_MAX;
            if (valid)
            {
                const int written = snprintf(output + used, capacity - used, "%u:%u: %.*s\n", line, column, (int)(end - error), error);
                valid = written >= 0 && (size_t)written < capacity - used;
                if (valid)
                {
                    used += (size_t)written;
                    *count += 1;
                }
            }
        }
        at = newline != NULL ? newline + 1 : end;
    }
    return valid;
}

static void cc_compat_target_feature_emission(CCCompatibilityTest* test)
{
    static const char preamble[] =
        "static inline __attribute__((always_inline, target(\"avx2\"))) int h(int x) { return x + 1; }\n"
        "static inline __attribute__((always_inline, target(\"avx2\"))) void hp(int *p) { *p += 1; }\n"
        "__attribute__((noreturn)) void stop(void);\n";
    static const struct
    {
        const char* name;
        const char* source;
        uint32_t errors;
    } fixtures[] = {
        {
            "constant switch: case inside if retains its condition and other cases",
            "int f(int x) { switch (0) { if (h(1)) { case 0: x = h(2); break; } case 1: x = h(3); } return x; }\n",
            3,
        },
        {
            "runtime switch: case inside if retains its condition",
            "int f(int x) { switch (x) { if (h(1)) { case 0: x = h(2); break; } case 1: x = h(3); } return x; }\n",
            3,
        },
        {
            "constant switch: default inside else cannot be collected",
            "int f(int x) { switch (9) { if (h(1)) { case 1: x = h(2); break; } else { default: x = h(3); break; } } return x; }\n",
            3,
        },
        {
            "each enclosing if emits before its nested case",
            "int f(int x) { switch (0) { if (h(1)) { if (h(2)) { case 0: x = h(3); break; } } case 1: x = h(4); } return x; }\n",
            4,
        },
        {
            "a dead prefix cannot hide an inner control containing a case",
            "int f(int x) { switch (0) { if (h(1)) { stop(); if (h(2)) { case 0: x = h(3); break; } } case 1: x = h(4); } return x; }\n",
            4,
        },
        {
            "goto into if inside switch retains both enclosing controls",
            "int f(int x) { goto lbl; switch (h(1)) { case 0: if (h(2)) { x = h(3); lbl: x = h(4); } break; } return x; }\n",
            4,
        },
        {
            "goto into nested if after a noreturn statement",
            "int f(int x) { goto lbl; if (h(1)) { stop(); if (h(2)) { x = h(3); lbl: x = h(4); } } return x; }\n",
            4,
        },
        {
            "runtime switch: case inside while retains its condition",
            "int f(int x) { switch (x) { while (h(1)) { case 0: x = h(2); break; } case 1: x = h(3); } return x; }\n",
            3,
        },
        {
            "constant switch: for init, condition, body and increment remain in emission order",
            "int f(int x) { switch (0) { for (x = h(1); h(2); x = h(4)) { case 0: x = h(3); break; } case 1: x = h(5); } return x; }\n",
            5,
        },
        {
            "constant switch: a do condition still emits after break",
            "int f(int x) { switch (0) { do { case 0: x = h(1); break; } while (h(2)); case 1: x = h(3); } return x; }\n",
            3,
        },
        {
            "constant switch: ordinary nested compounds remain collectable",
            "int f(int x) { switch (0) { { x = h(1); case 0: x = h(2); break; } case 1: x = h(3); } return x; }\n",
            1,
        },
        {
            "runtime switch: an ordinary compound's pre-case prefix stays dead",
            "int f(int x) { switch (x) { { x = h(1); case 0: x = h(2); break; } case 1: x = h(3); } return x; }\n",
            2,
        },
        {
            "an unmatched constant switch without a named label emits nothing",
            "int f(int x) { switch (9) { if (h(1)) { case 0: x = h(2); break; } } return x; }\n",
            0,
        },
        {
            "an unlabeled constant-dead if arm stays dead",
            "int f(int x) { if (0) { if (h(1)) x = h(2); } return x; }\n",
            0,
        },
        {
            "an unlabeled tail after noreturn stays dead in a whole switch",
            "int f(int x) { switch (x) { case 0: stop(); x = h(1); break; case 1: x = h(2); } return x; }\n",
            1,
        },
        {
            "goto into an ordinary compound does not revive its prefix",
            "int f(int x) { goto lbl; { x = h(1); lbl: x = h(2); } return x; }\n",
            1,
        },
        {
            "a named label revives an inner if even in a collected case",
            "int f(int x) { switch (0) { case 0: stop(); if (h(1)) { x = h(2); lbl: x = h(3); } break; case 1: x = h(4); } return x; }\n",
            3,
        },
        {
            "a nested case emits enclosing code and registers its live cleanup once",
            "int f(int x) { switch (0) { if (h(1)) { x = h(2); case 0:; int y __attribute__((cleanup(hp))) = 0; x = h(3); } } return x; }\n",
            4,
        },
        {
            "a builtin in the enclosing condition is checked too",
            "int f(int x) { switch (0) { if (__builtin_ia32_crc32si(0, x)) { case 0: x += 1; } } return x; }\n",
            1,
        },
        {
            "a statement expression's local goto retains its enclosing controls",
            "int f(int x) { return ({ goto lbl; switch (h(1)) { case 0: if (h(2)) { x = h(3); lbl: x = h(4); } break; } x; }); }\n",
            4,
        },
        {
            "labels in an unevaluated statement expression do not emit its controls",
            "int f(int x) { return sizeof(({ goto lbl; switch (h(1)) { case 0: if (h(2)) { lbl: x = h(3); } break; } x; })); }\n",
            0,
        },
        {
            "an inner switch's case roots stop at that switch, not its outer selection",
            "int f(int x) { switch (0) { case 0: switch (x) { if (h(1)) { case 0: x = h(2); break; } default: x = h(3); } break; case 1: x = h(4); } return x; }\n",
            3,
        },
        {
            "a named label on a null statement retains its enclosing condition",
            "int f(int x) { goto lbl; if (h(1)) { lbl:; } return x; }\n",
            1,
        },
        {
            "a statement expression's null label retains its enclosing condition",
            "int f(int x) { return ({ goto lbl; if (h(1)) { lbl:; } x; }); }\n",
            1,
        },
        {
            "a case on a null statement retains its enclosing condition",
            "int f(int x) { switch (0) { if (h(1)) { case 0:; } } return x; }\n",
            1,
        },
        {
            "a for without a condition retains its initializer and increment",
            "int f(int x) { switch (0) { for (x = h(1);; x = h(3)) { case 0: x = h(2); break; } case 1: x = h(4); } return x; }\n",
            4,
        },
    };
    for (uint32_t f = 0; f < sizeof(fixtures) / sizeof(fixtures[0]); f += 1)
    {
        char source[4096];
        const int length = snprintf(source, sizeof(source), "%s%s", preamble, fixtures[f].source);
        const bool prepared = length >= 0 && (size_t)length < sizeof(source) && cc_compat_write(test, "feature-entry.c", source);
        cc_compat_expect(test, prepared, fixtures[f].name);
        for (uint32_t syntax = 0; prepared && syntax < 2; syntax += 1)
        {
            char oracle[BUSTER_CC_TARGET_FEATURE_DIAGNOSTICS] = "";
            bool oracle_valid = false;
            const uint32_t expected_count = syntax != 0 ? 0 : fixtures[f].errors;
            const int expected_status = expected_count != 0 ? 1 : 0;
            for (uint32_t mode = 0; mode < 3; mode += 1)
            {
                const char* arguments[16] = { 0 };
                uint32_t count = 0;
                arguments[count++] = "-std=gnu17";
                arguments[count++] = "-march=x86-64";
                arguments[count++] = "-w";
                if (mode == 2)
                {
                    arguments[count++] = "--scalar";
                }
                arguments[count++] = syntax != 0 ? "-fsyntax-only" : "-c";
                arguments[count++] = "feature-entry.c";
                if (syntax == 0)
                {
                    arguments[count++] = "-o";
                    arguments[count++] = "feature-entry.o";
                }
                CCCompatibilityRun run = cc_compat_run(test, mode == 0 ? test->clang : test->compiler, NULL, arguments);
                char actual[BUSTER_CC_TARGET_FEATURE_DIAGNOSTICS];
                uint32_t errors = 0;
                const bool parsed = cc_compat_target_feature_diagnostics(run.errors, actual, sizeof(actual), &errors);
                const bool expected = parsed && run.status == expected_status && errors == expected_count;
                if (mode == 0)
                {
                    oracle_valid = expected;
                    if (parsed)
                    {
                        memcpy(oracle, actual, strlen(actual) + 1);
                    }
                }
                const bool same = expected && (mode == 0 || (oracle_valid && strcmp(actual, oracle) == 0));
                const char* names[] = { "clang", "default", "scalar" };
                char name[256];
                snprintf(name, sizeof(name), "target emission: %s (%s, %s)", fixtures[f].name, names[mode], syntax != 0 ? "syntax" : "object");
                cc_compat_expect(test, same, name);
                if (!same)
                {
                    fprintf(stderr, "  expected status %d and %u errors; got status %d and %u errors\n  oracle:\n%s  actual:\n%s\n",
                            expected_status, expected_count, run.status, errors, oracle, run.errors != NULL ? run.errors : "(missing diagnostics)");
                }
                cc_compat_release(&run);
            }
        }
    }
}
