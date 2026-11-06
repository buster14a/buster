// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Original eval_method diagnostics: scope history, consumed preprocessing uses, command-line boundaries and typedef
// availability. Both kernels compare every error line, source-note location/order and total note count with Clang.

// Scratch-space line numbers are implementation details. Retain the ordinary fixture's complete diagnostic
// lines instead, so an error and its explicit pragma note must have the same locations, text and relative order.
static bool check_test_driver_fp_source_lines(const char* text, const char* path, char* lines, size_t capacity)
{
    const size_t path_length = strlen(path);
    size_t used = 0;
    bool room = true;
    lines[0] = 0;
    for (const char* at = text != NULL ? text : ""; *at != 0;)
    {
        const char* end = strchr(at, '\n');
        const size_t length = end != NULL ? (size_t)(end - at) : strlen(at);
        const bool own = length > path_length && memcmp(at, path, path_length) == 0 && at[path_length] == ':';
        if (own)
        {
            const size_t copied = length - path_length;
            const bool fits = copied + 2 <= capacity - used;
            room &= fits;
            if (fits)
            {
                memcpy(lines + used, at + path_length, copied);
                used += copied;
                lines[used++] = '\n';
                lines[used] = 0;
            }
        }
        at = end != NULL ? end + 1 : at + length;
    }
    return room;
}

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
        // float_control(pop) restores arithmetic precision but retains the last method for typedef restrictions.
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma float_control(push)\n#pragma clang fp eval_method(double)\n#pragma float_control(pop)\n_Static_assert(sizeof(+1.0f)==4,\"restored arithmetic\");T value;\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma clang fp eval_method(double)\n#pragma float_control(push)\n#pragma clang fp eval_method(source)\n#pragma float_control(pop)\n_Static_assert(sizeof(+1.0f)==8,\"restored arithmetic\");T value;\n",
          "-ffp-eval-method=source", NULL, 0 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma float_control(push)\n#pragma clang fp eval_method(double)\n#pragma float_control(push)\n#pragma clang fp eval_method(extended)\n#pragma float_control(pop)\n#pragma float_control(pop)\n_Static_assert(sizeof(+1.0f)==4,\"twice restored arithmetic\");T value;\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float float_t;\n#pragma float_control(push)\n#pragma clang fp eval_method(double)\n_Pragma(\"float_control(pop)\")\n_Static_assert(sizeof(+1.0f)==4,\"macro pop arithmetic\");float_t value;\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma float_control(push)\n#pragma clang fp eval_method(double)\n#pragma float_control(pop)\nvoid inner(void){\n#pragma clang fp eval_method(source)\nT value;(void)value;}T outside;\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma float_control(push)\n#pragma clang fp eval_method(double)\n#pragma float_control(pop)\nvoid inner(void){T value;(void)value;}\n",
          "-ffp-eval-method=source", NULL, 1 },
        { "typedef float T __attribute__((available_only_in_default_eval_method));\n#pragma float_control(push)\n#pragma float_control(precise,on)\n#pragma float_control(pop)\nT value;\n",
          "-ffp-eval-method=source", NULL, 0 },
        // A compound restores its enclosing pragma's source; a float_control pop retains the most recent one.
        { "#pragma clang fp eval_method(source)\nint f(void){\n#pragma clang fp eval_method(extended)\nreturn __FLT_EVAL_METHOD__;}\nint x=__FLT_EVAL_METHOD__;\n",
          NULL, NULL, 2 },
        { "#pragma clang fp eval_method(double)\n#pragma float_control(push)\n#pragma clang fp eval_method(source)\n#pragma float_control(pop)\nint x=__FLT_EVAL_METHOD__;\n",
          NULL, NULL, 1 },
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
        char expected_source[8192];
        const bool expected_source_whole = check_test_driver_fp_source_lines(expected, source, expected_source, sizeof(expected_source));
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int result = check_test_driver_spawn(arguments, &actual);
            char actual_lines[8192];
            const bool actual_whole = check_test_driver_error_lines(actual, source, actual_lines, sizeof(actual_lines));
            char actual_source[8192];
            const bool actual_source_whole = check_test_driver_fp_source_lines(actual, source, actual_source, sizeof(actual_source));
            const bool same = oracle == result && oracle == (fixtures[c].errors != 0 ? 1 : 0) && expected_whole && actual_whole &&
                              strcmp(actual_lines, expected_lines) == 0 && expected_source_whole && actual_source_whole &&
                              strcmp(actual_source, expected_source) == 0 && check_test_pragma_diagnostics(actual, ": error: ") == fixtures[c].errors &&
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
    // These global modes still belong to the driver's explicit code-generation boundary. Their semantic-reader
    // conflicts are covered separately with parsed PPOptions; reaching a pragma must not weaken driver admission.
    static const struct { const char* first; const char* second; } refused[] = {
        { "-ffast-math", NULL }, { "-freciprocal-math", NULL }, { "-fassociative-math", NULL },
        { "-fassociative-math", "-fno-signed-zeros" }, { "-ffast-math", "-fno-fast-math" },
    };
    static const char option_source[] = "#pragma clang fp eval_method(double)\nint x;\n";
    const bool options_written = parse_test_write(source, option_source, sizeof(option_source) - 1);
    check_test_expect(test, options_written, "evaluation option boundary fixture is written");
    for (uint32_t i = 0; options_written && i < BUSTER_ARRAY_LENGTH(refused); i += 1)
    {
        char expected[256];
        snprintf(expected, sizeof(expected), "error: unsupported code generation option '%s'", refused[i].first);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* arguments[9] = { (char*)executable, "-std=gnu17", "-fsyntax-only", source, (char*)refused[i].first };
            uint32_t count = 5;
            if (refused[i].second != NULL)
            {
                arguments[count++] = (char*)refused[i].second;
            }
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int result = check_test_driver_spawn(arguments, &actual);
            check_test_expect(test, result == 1 && actual != NULL && strstr(actual, expected) != NULL,
                              "unsupported global floating modes keep their named driver refusal");
            free(actual);
        }
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
