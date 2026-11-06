// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original eval_method diagnostics: scope history, consumed preprocessing uses, command-line conflicts and typedef
// availability. The ordinary driver and --scalar compare every error line and macro-note count with pinned Clang.

static void check_test_driver_fp_evaluation(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* source;
        const char* first;
        const char* second;
        uint32_t errors;
    } fixtures[] = {
        { "#pragma clang fp eval_method(source)\nint x=__FLT_EVAL_METHOD__;\n", NULL, NULL, 1 },
        { "#define METHOD __FLT_EVAL_METHOD__\n#pragma clang fp eval_method(double)\nint x=METHOD;\n", NULL, NULL, 1 },
        { "int f(void){\n#pragma clang fp eval_method(double)\nreturn __FLT_EVAL_METHOD__;}\nint x=__FLT_EVAL_METHOD__;\n", NULL, NULL, 1 },
        { "int f(void){\n#pragma clang fp eval_method(double)\nreturn 0;}\nint x=__FLT_EVAL_METHOD__;\n", NULL, NULL, 0 },
        { "#pragma clang fp eval_method(double)\n#pragma clang fp eval_method(source)\nint x=__FLT_EVAL_METHOD__;\n", NULL, NULL, 1 },
        { "#pragma float_control(push)\n#pragma clang fp eval_method(double)\n#pragma float_control(pop)\nint x=__FLT_EVAL_METHOD__;\n", NULL, NULL, 1 },
        // Consumed uses need separate errors even at EOF, where no C token exists and the report count exceeds it.
        { "#pragma clang fp eval_method(double)\n#if __FLT_EVAL_METHOD__ == 0\n#endif\n#if __FLT_EVAL_METHOD__ == 0\n#endif\n", NULL, NULL, 2 },
        { "#pragma clang fp eval_method(double)\n#if __FLT_EVAL_METHOD__ == 0\n#endif\n#pragma clang fp eval_method(source)\n#if __FLT_EVAL_METHOD__ == 0\n#endif\nint x;\n", NULL, NULL, 2 },
        { "#define CONTROL _Pragma(\"clang fp eval_method(double)\")\n#define METHOD __FLT_EVAL_METHOD__\nint f(void){CONTROL return METHOD;}\n", NULL, NULL, 1 },
        { "#pragma clang fp eval_method(double)\nint x;\n", "-ffast-math", NULL, 3 },
        { "#pragma clang fp eval_method(source)\nint x;\n", "-freciprocal-math", NULL, 1 },
        { "#pragma clang fp eval_method(double)\nint x;\n", "-fassociative-math", NULL, 0 },
        { "#pragma clang fp eval_method(double)\nint x;\n", "-fassociative-math", "-fno-signed-zeros", 1 },
        { "#pragma clang fp eval_method(double)\nint x;\n", "-ffast-math", "-fno-fast-math", 0 },
        { "#pragma clang fp eval_method(double)\n#pragma clang fp contract(on) reassociate(on)\nint x;\n", NULL, NULL, 1 },
        { "#pragma clang fp eval_method(source)\n#pragma clang fp reciprocal(on)\nint x;\n", NULL, NULL, 1 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma clang fp eval_method(double)\nT x;\n", "-ffp-eval-method=source", NULL, 1 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma clang fp eval_method(double)\nT x;\n", NULL, NULL, 0 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma clang fp eval_method(source)\nT x;\n", "-ffp-eval-method=source", NULL, 0 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma clang fp eval_method(double)\nint f(void){return sizeof(T)+sizeof((T)1)+sizeof(__typeof__((T)1));}\n",
          "-ffp-eval-method=source", NULL, 3 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));typedef float T;\n#pragma clang fp eval_method(double)\nT x;\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float T;\n#pragma clang fp eval_method(double)\nT before;\n#pragma clang fp eval_method(source)\ntypedef float T __attribute__((available_only_in_default_eval_method));\n#pragma clang fp eval_method(double)\nT after;\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float float_t;typedef double double_t;\n#pragma clang fp eval_method(double)\nfloat_t x;double_t y;\n", "-ffp-eval-method=source", NULL, 2 },
        { "typedef float float_t;int f(void){\n#pragma clang fp eval_method(double)\ntypedef double float_t;float_t x=0;return sizeof(x);}\n",
          "-ffp-eval-method=source", NULL, 0 },
    };
    char source[512];
    snprintf(source, sizeof(source), "%s/fp-evaluation.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(fixtures); c += 1)
    {
        const bool written = parse_test_write(source, fixtures[c].source, strlen(fixtures[c].source));
        check_test_expect(test, written, "floating evaluation diagnostic fixture is written");
        char* arguments[12] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source };
        uint32_t count = 5;
        if (fixtures[c].first != NULL)
        {
            arguments[count++] = (char*)fixtures[c].first;
        }
        if (fixtures[c].second != NULL)
        {
            arguments[count++] = (char*)fixtures[c].second;
        }
        char* expected = NULL;
        const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
        char expected_lines[8192];
        const bool expected_whole = check_test_driver_error_lines(expected, source, expected_lines, sizeof(expected_lines));
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int result = check_test_driver_spawn(arguments, &actual);
            char actual_lines[8192];
            const bool actual_whole = check_test_driver_error_lines(actual, source, actual_lines, sizeof(actual_lines));
            const bool same = oracle == result && oracle == (fixtures[c].errors != 0 ? 1 : 0) && expected_whole && actual_whole &&
                              strcmp(actual_lines, expected_lines) == 0 && check_test_pragma_diagnostics(actual, ": error: ") == fixtures[c].errors &&
                              check_test_pragma_diagnostics(actual, ": note: ") == check_test_pragma_diagnostics(expected, ": note: ");
            check_test_expect(test, same, "evaluation scope, macro provenance, typedef restrictions and option conflicts match Clang");
            if (!same)
            {
                fprintf(stderr, "FP evaluation case %u scalar %u: Clang %d (%s), checker %d (%s)\n", c, scalar,
                        oracle, expected != NULL ? expected : "", result, actual != NULL ? actual : "");
            }
            free(actual);
        }
        free(expected);
    }
    // Ordinary preprocessing retains raw pragmas and permits the builtin, including consumed #if uses.
    const uint32_t preprocessed[] = { 0, 6 };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(preprocessed); i += 1)
    {
        const char* text = fixtures[preprocessed[i]].source;
        const bool written = parse_test_write(source, text, strlen(text));
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            char* output = NULL;
            char* arguments[] = { (char*)executable, "-E", "-P", source, scalar != 0 ? "--scalar" : NULL, NULL };
            check_test_expect(test, check_test_driver_spawn(arguments, &output) == 0 && check_test_pragma_diagnostics(output, ": error: ") == 0,
                              "preprocess-only expansion does not run semantic evaluation-scope restrictions");
            free(output);
        }
    }
    unlink(source);
}
