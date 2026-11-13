// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Original source fixtures for #810. Clang is an external policy oracle, not an implementation input. Both
// checker paths must agree on acceptance and diagnostic groups; rejected compilations must not emit objects.

static uint32_t check_test_pragma_diagnostics(const char* text, const char* group)
{
    uint32_t count = 0;
    for (const char* at = text; at != NULL && (at = strstr(at, group)) != NULL; at += strlen(group))
    {
        count += 1;
    }
    return count;
}

#include "c_check_fp_eval_driver_test.c"

static void check_test_driver_pragma_system(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char header_text[] = "#pragma buster_header\n#define PRAGMA_FROM_HEADER _Pragma(\"buster_macro\")\n";
    static const char source_text[] = "#include <ignored-pragma-system.h>\nint main(void){return PRAGMA_FROM_HEADER 0;}\n";
    char header[512];
    char source[512];
    snprintf(header, sizeof(header), "%s/ignored-pragma-system.h", directory);
    snprintf(source, sizeof(source), "%s/ignored-pragma-system.c", directory);
    const bool written = parse_test_write(header, header_text, sizeof(header_text) - 1) && parse_test_write(source, source_text, sizeof(source_text) - 1);
    check_test_expect(test, written, "pragma system-header and user expansion fixtures are written");
    for (uint32_t system = 0; written && system < 3; system += 1)
    {
        char* arguments[16] = { (char*)clang, "-fsyntax-only", "-fno-caret-diagnostics", "-Werror=unknown-pragmas", source, "-isystem", (char*)directory };
        uint32_t count = 7;
        if (system != 0)
        {
            arguments[count++] = "-Wsystem-headers";
        }
        if (system == 2)
        {
            arguments[count++] = "-Wno-system-headers";
        }
        char* expected = NULL;
        const int oracle = check_test_driver_spawn(arguments, &expected);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            for (uint32_t lazy = 0; lazy < 2; lazy += 1)
            {
                arguments[0] = (char*)executable;
                uint32_t extra = count;
                if (scalar != 0)
                {
                    arguments[extra++] = "--scalar";
                }
                if (lazy != 0)
                {
                    arguments[extra++] = "-flazy-system-headers";
                }
                arguments[extra] = NULL;
                char* actual = NULL;
                const int result = check_test_driver_spawn(arguments, &actual);
                const uint32_t observed = check_test_pragma_diagnostics(actual, "-Wunknown-pragmas]");
                const bool equal = oracle != 0 && result != 0 && observed == (system == 1 ? 2u : 1u) &&
                                   observed == check_test_pragma_diagnostics(expected, "-Wunknown-pragmas]");
                check_test_expect(test, equal, "pragma warning visibility uses the directive or macro invocation, not its neighboring C token");
                if (!equal)
                {
                    fprintf(stderr, "pragma system case %u scalar %u lazy %u: Clang %d (%s), checker %d (%s)\n", system, scalar, lazy,
                            oracle, expected != NULL ? expected : "", result, actual != NULL ? actual : "");
                }
                free(actual);
            }
        }
        free(expected);
    }
    unlink(header);
    unlink(source);
}

static void check_test_driver_pragma_execution(CheckTest* test, const char* executable, const char* directory)
{
    static const char source_text[] =
        "#pragma ident \"tooling\"\n#pragma GCC push_options\n#pragma GCC optimize(\"O3\")\n"
        "#pragma scalar_storage_order big-endian\n#pragma GCC target(\"avx2\")\n"
        "#pragma data_seg(\"ignored_data\")\n#pragma code_seg(\"ignored_code\")\n#pragma fenv_access(on)\n"
        "#pragma comment(lib, \"m\")\nint main(void){int sum=0;\n#pragma omp parallel for\n"
        "for(int i=0;i<4;i++)sum+=i;return sum!=6;}\n#pragma GCC pop_options\n";
    char source[512];
    char binary[512];
    char preprocessed[512];
    snprintf(source, sizeof(source), "%s/ignored-pragma-execute.c", directory);
    snprintf(binary, sizeof(binary), "%s/ignored-pragma-execute", directory);
    snprintf(preprocessed, sizeof(preprocessed), "%s/ignored-pragma-execute.i", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    check_test_expect(test, written, "ignored pragma execution fixture is written");
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        char* text = NULL;
        char* arguments[] = { (char*)executable, source, "-o", binary, scalar != 0 ? "--scalar" : NULL, NULL };
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        free(text);
        text = NULL;
        char* run[] = { binary, NULL };
        check_test_expect(test, compiled && check_test_driver_spawn(run, &text) == 0, "ignored pragmas preserve native serial execution");
        free(text);
        text = NULL;
        unlink(preprocessed);
        char* preprocess[] = { (char*)executable, "-E", "-P", "-Werror=unknown-pragmas", source, "-o", preprocessed, scalar != 0 ? "--scalar" : NULL, NULL };
        const int status = check_test_driver_spawn(preprocess, &text);
        free(text);
        char captured[4096] = { 0 };
        FILE* file = status == 0 ? fopen(preprocessed, "rb") : NULL;
        const size_t length = file != NULL ? fread(captured, 1, sizeof(captured) - 1, file) : 0;
        const bool read = file != NULL && !ferror(file) && feof(file);
        captured[length] = 0;
        if (file != NULL)
        {
            fclose(file);
        }
        check_test_expect(test, read && strstr(captured, "#pragma omp") != NULL && strstr(captured, "#pragma GCC optimize") != NULL,
                          "preprocess-only output retains pragmas without running semantic warning policy");
    }
    unlink(source);
    unlink(binary);
    unlink(preprocessed);
}

static void check_test_driver_fp_pragma_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const sources[] =
    {
        "#pragma float_control(precise, off)\n#pragma float_control(except, on)\nint value;\n",
        "#pragma float_control(precise, off)\n#pragma STDC FENV_ACCESS ON\nint value;\n",
        "#pragma float_control(except, on)\n#pragma float_control(precise, off)\nint value;\n",
        "#pragma STDC FENV_ACCESS ON\n#pragma float_control(except, off)\n#pragma float_control(precise, off)\nint value;\n",
        "#pragma clang fp reciprocal(on)\n#pragma float_control(except, on)\nint value;\n",
        "int main(void){\n#pragma float_control(push)\nreturn 0;}\n",
    };
    char source[512];
    snprintf(source, sizeof(source), "%s/fp-pragma-error.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(sources); c += 1)
    {
        const bool written = parse_test_write(source, sources[c], strlen(sources[c]));
        char* expected = NULL;
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        const int oracle = written ? check_test_driver_spawn(arguments, &expected) : -1;
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int result = written ? check_test_driver_spawn(arguments, &actual) : -1;
            const uint64_t length = expected != NULL ? strcspn(expected, "\n") : 0;
            const bool same = oracle == 1 && result == 1 && expected != NULL && actual != NULL && length != 0 &&
                              strcspn(actual, "\n") == length && memcmp(actual, expected, length) == 0;
            check_test_expect(test, same, "FP precision, exception and stack errors match Clang's message and source location");
            if (!same)
            {
                fprintf(stderr, "FP pragma case %u scalar %u: Clang %d (%s), checker %d (%s)\n", c, scalar,
                        oracle, expected != NULL ? expected : "", result, actual != NULL ? actual : "");
            }
            free(actual);
        }
        free(expected);
    }
    unlink(source);
}

static void check_test_driver_symbol_pragma_diagnostics(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const sources[] = {
        "#pragma weak missing\nint value;\n",
        "int value;\n#pragma weak missing\n",
        "#pragma weak missing\n",
        "#pragma weak value\nstatic int value=7;\nint read_value(void){return value;}\n",
        "static int value;\n#pragma weak value\nint read_value(void){return value;}\n",
        "#pragma weak value\ntypedef int value;\n",
        "typedef int value;\n#pragma weak value\n",
        "#pragma redefine_extname value renamed\nstatic int value=7;\nint read_value(void){return value;}\n",
        "static int value=7;\n#pragma redefine_extname value renamed\nint read_value(void){return value;}\n",
        "#pragma redefine_extname function renamed\nstatic int function(void){return 7;}\nint read_value(void){return function();}\n",
        "#pragma weak\nint value;\n",
        "#pragma weak alias=\nint value;\n",
        "#pragma weak value trailing\nint value;\n",
        "#pragma weak int\nint value;\n",
        "#pragma redefine_extname value\nint value;\n",
        "#pragma redefine_extname value \"renamed\"\nint value;\n",
        "#pragma redefine_extname value renamed trailing\nint value;\n",
        "#pragma clang diagnostic ignored \"-Wignored-pragmas\"\n#pragma weak\nint value;\n",
        "#pragma clang diagnostic ignored \"-Wpragmas\"\n#pragma redefine_extname value renamed\nstatic int value;\n",
        "#define TARGET absent\n#pragma weak TARGET\nint value;\n",
    };
    char path[512];
    snprintf(path, sizeof(path), "%s/symbol-pragma-warning.c", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(sources); c += 1)
    {
        const bool written = parse_test_write(path, sources[c], strlen(sources[c]));
        check_test_expect(test, written, "symbol pragma warning fixture is written");
        for (uint32_t mode = 0; written && mode < 3; mode += 1)
        {
            char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", path,
                                  mode == 1 ? "-Werror" : mode == 2 ? "-w" : NULL, NULL, NULL };
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                arguments[0] = (char*)executable;
                arguments[mode != 0 ? 6 : 5] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int result = check_test_driver_spawn(arguments, &actual);
                const uint64_t length = expected != NULL ? strcspn(expected, "\n") : 0;
                const uint64_t observed = actual != NULL ? strcspn(actual, "\n") : 0;
                const bool same = oracle == result && length == observed && (length == 0 || memcmp(actual, expected, length) == 0);
                check_test_expect(test, same, "symbol pragma warnings match Clang's source, spelling and warning controls");
                if (!same)
                {
                    fprintf(stderr, "symbol pragma warning %u mode %u scalar %u: Clang %d (%s), checker %d (%s)\n", c, mode, scalar,
                            oracle, expected != NULL ? expected : "", result, actual != NULL ? actual : "");
                }
                free(actual);
            }
            free(expected);
        }
    }
    unlink(path);
}

static void check_test_driver_symbol_pragma_objects(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source[] =
        "#define SOURCE weak_data\n#pragma weak SOURCE\nint weak_data=4;\n"
        "#pragma weak weak_function\nint weak_function(void){return 3;}\n"
        "int weak_tentative;\n#pragma weak weak_tentative\n"
        "#pragma weak weak_constant\nconst int weak_constant=5;\n"
        "#pragma weak weak_tls\n_Thread_local int weak_tls=6;\n"
        "extern int weak_after_reference;\nint read_weak(void){return weak_after_reference;}\n"
        "#pragma weak weak_after_reference\nint weak_after_reference=7;\n"
        "extern int weak_missing(void);\n#pragma weak weak_missing\nint read_missing(void){return weak_missing ? weak_missing() : -1;}\n"
        "int strong_after=8;\n#pragma weak strong_after\nint strong_function(void){return 8;}\n#pragma weak strong_function\n"
        "#define DEST renamed\n#pragma redefine_extname original DEST\nint original=9;\n"
        "int original_function(void);\n#pragma redefine_extname original_function renamed_function\nint original_function(void){return 10;}\n"
        "int rename_tentative;\n#pragma redefine_extname rename_tentative renamed_tentative\n"
        "int defined_before=11;\n#pragma redefine_extname defined_before not_emitted\n"
        "#pragma redefine_extname first_pending first_name\n#pragma redefine_extname first_pending second_name\nint first_pending=12;\n"
        "#pragma redefine_extname explicit_name not_chosen\nint explicit_name asm(\"chosen\")=13;\n"
        "extern int prior_asm asm(\"chosen_prior\");\n#pragma redefine_extname prior_asm not_prior\nint prior_asm=14;\n"
        "extern int late_value_17;\n"
        "int late_use_17(void){return late_value_17;}\n"
        "#pragma redefine_extname late_value_17 late_renamed_17\n"
        "int late_value_17=7;\n"
        "extern int late_value_18;\n"
        "int late_use_18(void){if(0)return late_value_18; return 1;}\n"
        "#pragma redefine_extname late_value_18 late_renamed_18\n"
        "int late_value_18=7;\n"
        "extern int late_value_19;\n"
        "int late_use_19(void){return 1; return late_value_19;}\n"
        "#pragma redefine_extname late_value_19 late_renamed_19\n"
        "int late_value_19=7;\n"
        "extern int late_value_20;\n"
        "int late_use_20(void){return sizeof(late_value_20);}\n"
        "#pragma redefine_extname late_value_20 late_renamed_20\n"
        "int late_value_20=7;\n"
        "extern int late_value_21;\n"
        "static int late_use_21(void){return late_value_21;}\n"
        "#pragma redefine_extname late_value_21 late_renamed_21\n"
        "int late_value_21=7;\n"
        "extern int late_value_22;\n"
        "inline int late_use_22(void){return late_value_22;}\n"
        "#pragma redefine_extname late_value_22 late_renamed_22\n"
        "int late_value_22=7;\n"
        "extern int late_value_23;\n"
        "static int late_use_23(void){return late_value_23;}\n"
        "#pragma redefine_extname late_value_23 late_renamed_23\n"
        "int late_value_23=7;\n"
        "int late_caller_23(void){return late_use_23();}\n"
        "extern int late_value_24;\n"
        "static int late_use_24(void){return late_value_24;}\n"
        "int late_caller_24(void){return late_use_24();}\n"
        "#pragma redefine_extname late_value_24 late_renamed_24\n"
        "int late_value_24=7;\n"
        "extern int late_value_25;\n"
        "int *late_pointer_25=&late_value_25;\n"
        "#pragma redefine_extname late_value_25 late_renamed_25\n"
        "int late_value_25=7;\n"
        "extern int late_value_26;\n"
        "int late_use_26(void){return _Generic(0,int:1,default:late_value_26);}\n"
        "#pragma redefine_extname late_value_26 late_renamed_26\n"
        "int late_value_26=7;\n"
        "extern int late_value_27;\n"
        "int late_use_27(void){return 0 ? late_value_27 : 1;}\n"
        "#pragma redefine_extname late_value_27 late_renamed_27\n"
        "int late_value_27=7;\n"
        "extern int late_value_28;\n"
        "int late_use_28(int c){if(c)return late_value_28; return 1;}\n"
        "#pragma redefine_extname late_value_28 late_renamed_28\n"
        "int late_value_28=7;\n"
        "extern int late_value_29;\n"
        "int late_use_29(void){return 0 && late_value_29;}\n"
        "#pragma redefine_extname late_value_29 late_renamed_29\n"
        "int late_value_29=7;\n"
        "extern int late_value_30;\n"
        "int late_use_30(void){return 1 || late_value_30;}\n"
        "#pragma redefine_extname late_value_30 late_renamed_30\n"
        "int late_value_30=7;\n"
        "extern int late_value_31;\n"
        "int late_use_31(int x){switch(1){case 2:return late_value_31;default:return x;}}\n"
        "#pragma redefine_extname late_value_31 late_renamed_31\n"
        "int late_value_31=7;\n"
        "int late_target_32(void);\n"
        "int late_use_32(void){return late_target_32();}\n"
        "#pragma redefine_extname late_target_32 late_renamed_32\n"
        "int late_target_32(void){return 7;}\n"
        "extern int late_value_33;\n"
        "int late_use_33(void){int x=late_value_33;\n"
        "#pragma redefine_extname late_value_33 late_renamed_33\n"
        "return x;}\n"
        "int late_value_33=7;\n"
        "extern int late_value_34;\n"
        "static int *late_pointer_34=&late_value_34;\n"
        "#pragma redefine_extname late_value_34 late_renamed_34\n"
        "int late_value_34=7;\n"
        "int late_use_34(void){return *late_pointer_34;}\n"
        "extern int late_value_35;\n"
        "static int *late_pointer_35=&late_value_35;\n"
        "int late_use_35(void){return *late_pointer_35;}\n"
        "#pragma redefine_extname late_value_35 late_renamed_35\n"
        "int late_value_35=7;\n"
        "extern int late_value_36;\n"
        "static int *late_pointer_36 __attribute__((used))=&late_value_36;\n"
        "#pragma redefine_extname late_value_36 late_renamed_36\n"
        "int late_value_36=7;\n"
        "extern int late_value_37;\n"
        "int late_use_37(void){static int *late_pointer_37=&late_value_37;return *late_pointer_37;}\n"
        "#pragma redefine_extname late_value_37 late_renamed_37\n"
        "int late_value_37=7;\n"
        "extern int late_value_38;\n"
        "static int late_use_38(void){static int *late_pointer_38=&late_value_38;return *late_pointer_38;}\n"
        "int late_caller_38(void){return late_use_38();}\n"
        "#pragma redefine_extname late_value_38 late_renamed_38\n"
        "int late_value_38=7;\n"
        "extern int late_value_39;\n"
        "extern inline int late_use_39(void){return late_value_39;}\n"
        "#pragma redefine_extname late_value_39 late_renamed_39\n"
        "int late_value_39=7;\n"
        "extern int late_value_40;\n"
        "static int late_use_40(void) __attribute__((used));\n"
        "static int late_use_40(void){return late_value_40;}\n"
        "#pragma redefine_extname late_value_40 late_renamed_40\n"
        "int late_value_40=7;\n"
        "extern int late_value_41;\n"
        "int *late_pointer_41;\n"
        "int late_use_41(void){late_pointer_41=&late_value_41;return 1;}\n"
        "#pragma redefine_extname late_value_41 late_renamed_41\n"
        "int late_value_41=7;\n"
        "extern int late_value_42;\n"
        "static int *late_pointer_42=&late_value_42;\n"
        "int **late_external_42=&late_pointer_42;\n"
        "#pragma redefine_extname late_value_42 late_renamed_42\n"
        "int late_value_42=7;\n"
        "extern int late_value_43;\n"
        "int late_use_43(void){return 1; label:return late_value_43;}\n"
        "#pragma redefine_extname late_value_43 late_renamed_43\n"
        "int late_value_43=7;\n"
        "extern int late_value_44;\n"
        "int late_use_44(void){goto end; return late_value_44; end:return 1;}\n"
        "#pragma redefine_extname late_value_44 late_renamed_44\n"
        "int late_value_44=7;\n"
        "extern int late_value_45;\n"
        "int late_use_45(void){while(0)return late_value_45; return 1;}\n"
        "#pragma redefine_extname late_value_45 late_renamed_45\n"
        "int late_value_45=7;\n"
        "extern int late_value_46;\n"
        "int late_use_46(void){for(;0;)return late_value_46; return 1;}\n"
        "#pragma redefine_extname late_value_46 late_renamed_46\n"
        "int late_value_46=7;\n"
        "extern int late_value_47;\n"
        "int late_use_47(void){if(1)return 1; return late_value_47;}\n"
        "#pragma redefine_extname late_value_47 late_renamed_47\n"
        "int late_value_47=7;\n"
        "extern int late_value_48;\n"
        "void late_terminal_48(void) __attribute__((noreturn));\n"
        "int late_use_48(void){late_terminal_48(); return late_value_48;}\n"
        "#pragma redefine_extname late_value_48 late_renamed_48\n"
        "int late_value_48=7;\n"
        "void late_terminal_48(void){for(;;){}}\n"
        "extern int late_value_49;\n"
        "int late_use_49(void){return 1; static int *late_pointer_49=&late_value_49; return *late_pointer_49;}\n"
        "#pragma redefine_extname late_value_49 late_renamed_49\n"
        "int late_value_49=7;\n"
        "extern int late_value_50;\n"
        "int late_use_50(void){goto end; label:return late_value_50; end:return 1;}\n"
        "#pragma redefine_extname late_value_50 late_renamed_50\n"
        "int late_value_50=7;\n"
        "extern int late_value_51;\n"
        "int late_use_51(void){do {break; return late_value_51;}while(0);return 1;}\n"
        "#pragma redefine_extname late_value_51 late_renamed_51\n"
        "int late_value_51=7;\n"
        "extern int late_value_52;\n"
        "int late_use_52(int x){switch(1){case 2:return late_value_52;default:return x;}}\n"
        "#pragma redefine_extname late_value_52 late_renamed_52\n"
        "int late_value_52=7;\n"
        "extern int late_value_53;\n"
        "int late_use_53(void){if(0){label:return late_value_53;}return 1;}\n"
        "#pragma redefine_extname late_value_53 late_renamed_53\n"
        "int late_value_53=7;\n"
        "extern int late_value_54;\n"
        "int late_use_54(void){return 0?({label:late_value_54;}):1;}\n"
        "#pragma redefine_extname late_value_54 late_renamed_54\n"
        "int late_value_54=7;\n"
        "extern int late_value_55;\n"
        "int late_use_55(void){for(;;){} return late_value_55;}\n"
        "#pragma redefine_extname late_value_55 late_renamed_55\n"
        "int late_value_55=7;\n"
        "extern int late_value_56;\n"
        "int late_use_56(void){__builtin_trap(); return late_value_56;}\n"
        "#pragma redefine_extname late_value_56 late_renamed_56\n"
        "int late_value_56=7;\n"
        "extern int late_value_57;\n"
        "void late_stop_57(int) __attribute__((overloadable,noreturn));\n"
        "void late_stop_57(double) __attribute__((overloadable));\n"
        "int late_use_57(void){late_stop_57(1);return late_value_57;}\n"
        "#pragma redefine_extname late_value_57 late_renamed_57\n"
        "int late_value_57=7;\n"
        "__attribute__((overloadable,noreturn)) void late_stop_57(int x){for(;;){}}\n"
        "__attribute__((overloadable)) void late_stop_57(double x){}\n"
        "extern int late_value_58;\n"
        "void late_stop_58(int) __attribute__((overloadable));\n"
        "void late_stop_58(double) __attribute__((overloadable,noreturn));\n"
        "int late_use_58(void){late_stop_58(1);return late_value_58;}\n"
        "#pragma redefine_extname late_value_58 late_renamed_58\n"
        "int late_value_58=7;\n"
        "__attribute__((overloadable)) void late_stop_58(int x){}\n"
        "__attribute__((overloadable,noreturn)) void late_stop_58(double x){for(;;){}}\n"
        "extern int late_value_59;\n"
        "extern void abort(void);\n"
        "int late_use_59(void){abort();return late_value_59;}\n"
        "#pragma redefine_extname late_value_59 late_renamed_59\n"
        "int late_value_59=7;\n"
        "int main(void){return weak_data+weak_function()+original+weak_constant+weak_tls;}\n";
    static const char strong[] = "int weak_data=40; int weak_function(void){return 30;} const int weak_constant=20; _Thread_local int weak_tls=30;\n";
    static const char* const names[] = {
        "weak_data", "weak_function", "weak_tentative", "weak_constant", "weak_tls", "weak_after_reference", "weak_missing",
        "strong_after", "strong_function", "renamed", "renamed_function", "renamed_tentative", "defined_before", "first_name", "chosen", "chosen_prior",
        "late_value_17",
        "late_renamed_18",
        "late_renamed_19",
        "late_renamed_20",
        "late_renamed_21",
        "late_renamed_22",
        "late_renamed_23",
        "late_renamed_24",
        "late_value_25",
        "late_renamed_26",
        "late_renamed_27",
        "late_value_28",
        "late_renamed_29",
        "late_renamed_30",
        "late_renamed_31",
        "late_target_32",
        "late_renamed_33",
        "late_renamed_34",
        "late_renamed_35",
        "late_value_36",
        "late_value_37",
        "late_renamed_38",
        "late_value_39",
        "late_value_40",
        "late_value_41",
        "late_renamed_42",
        "late_value_43",
        "late_renamed_44",
        "late_value_45",
        "late_value_46",
        "late_renamed_47",
        "late_renamed_48",
        "late_value_49",
        "late_value_50",
        "late_renamed_51",
        "late_renamed_52",
        "late_value_53",
        "late_value_54",
        "late_renamed_55",
        "late_value_56",
        "late_renamed_57",
        "late_value_58",
        "late_renamed_59",
    };
    static const char* const absent[] = {
        "original", "original_function", "rename_tentative", "not_emitted", "second_name", "not_chosen", "not_prior",
        "late_renamed_17",
        "late_value_18",
        "late_value_19",
        "late_value_20",
        "late_value_21",
        "late_value_22",
        "late_value_23",
        "late_value_24",
        "late_renamed_25",
        "late_value_26",
        "late_value_27",
        "late_renamed_28",
        "late_value_29",
        "late_value_30",
        "late_value_31",
        "late_renamed_32",
        "late_value_33",
        "late_value_34",
        "late_value_35",
        "late_renamed_36",
        "late_renamed_37",
        "late_value_38",
        "late_renamed_39",
        "late_renamed_40",
        "late_renamed_41",
        "late_value_42",
        "late_renamed_43",
        "late_value_44",
        "late_renamed_45",
        "late_renamed_46",
        "late_value_47",
        "late_value_48",
        "late_renamed_49",
        "late_renamed_50",
        "late_value_51",
        "late_value_52",
        "late_renamed_53",
        "late_renamed_54",
        "late_value_55",
        "late_renamed_56",
        "late_value_57",
        "late_renamed_58",
        "late_value_59",
    };
    char path[512];
    char strong_path[512];
    char strong_object[512];
    char objects[5][512] = { { 0 } };
    char binary[512];
    snprintf(path, sizeof(path), "%s/symbol-pragmas.c", directory);
    snprintf(strong_path, sizeof(strong_path), "%s/symbol-pragmas-strong.c", directory);
    snprintf(strong_object, sizeof(strong_object), "%s/symbol-pragmas-strong.o", directory);
    snprintf(binary, sizeof(binary), "%s/symbol-pragmas-run", directory);
    const bool written = parse_test_write(path, source, sizeof(source) - 1) && parse_test_write(strong_path, strong, sizeof(strong) - 1);
    char* text = NULL;
    char* strong_arguments[] = { (char*)clang, "-std=gnu17", "-c", strong_path, "-o", strong_object, NULL };
    const bool override = written && check_test_driver_spawn(strong_arguments, &text) == 0;
    check_test_expect(test, override, "symbol pragma strong override fixture compiles");
    free(text);
    for (uint32_t producer = 0; written && producer < BUSTER_ARRAY_LENGTH(objects); producer += 1)
    {
        snprintf(objects[producer], sizeof(objects[producer]), "%s/symbol-pragmas-%u.o", directory, producer);
        char* arguments[16] = { (char*)(producer == 0 ? clang : executable), "-std=gnu17", "-O0", "-c", path, "-o", objects[producer] };
        uint32_t count = 7;
        if (producer != 0)
        {
            arguments[count++] = "--lanes";
            arguments[count++] = producer < 3 ? "1" : "3";
        }
        if (producer >= 3)
        {
            arguments[count++] = "--poison-arenas";
        }
        if (producer == 2 || producer == 4)
        {
            arguments[count++] = "--scalar";
        }
        arguments[count] = NULL;
        text = NULL;
        const bool compiled = check_test_driver_spawn(arguments, &text) == 0;
        check_test_expect(test, compiled, "symbol pragmas compile through Clang and both checker paths");
        if (!compiled)
        {
            fprintf(stderr, "symbol pragma producer %u: %s\n", producer, text != NULL ? text : "");
        }
        free(text);
        for (uint32_t n = 0; compiled && n < BUSTER_ARRAY_LENGTH(names); n += 1)
        {
            uint16_t ours = 0;
            uint16_t theirs = 0;
            const bool same = check_test_visibility_symbol(objects[producer], names[n], &ours) &&
                              check_test_visibility_symbol(objects[0], names[n], &theirs) && ours == theirs;
            check_test_expect(test, same, names[n]);
        }
        for (uint32_t n = 0; compiled && n < BUSTER_ARRAY_LENGTH(absent); n += 1)
        {
            uint16_t flags = 0;
            check_test_expect(test, !check_test_visibility_symbol(objects[producer], absent[n], &flags), "symbol renaming emits only the selected name");
        }
        for (uint32_t replaced = 0; compiled && override && replaced < 2; replaced += 1)
        {
            char* link[] = { (char*)clang, objects[producer], "-o", binary, replaced != 0 ? strong_object : NULL, NULL };
            text = NULL;
            const bool linked = check_test_driver_spawn(link, &text) == 0;
            free(text);
            char* run[] = { binary, NULL };
            text = NULL;
            const int status = linked ? check_test_driver_spawn(run, &text) : -1;
            check_test_expect(test, linked && status == (replaced != 0 ? 129 : 27), "pragma weak definitions yield to external strong definitions while renamed references link");
            free(text);
        }
    }
    for (uint32_t i = 2; i < BUSTER_ARRAY_LENGTH(objects); i += 1)
    {
        check_test_expect(test, written && check_test_driver_same_files(objects[1], objects[i]),
                          "symbol pragma scalar/vector and poisoned multi-lane objects match byte for byte");
    }
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(objects); i += 1)
    {
        unlink(objects[i]);
    }
    unlink(path);
    unlink(strong_path);
    unlink(strong_object);
    unlink(binary);
}

// An inclusion that changes the `#pragma pack` value (#1849): clang's -Wpragma-pack warning at the include's file name with
// its note at the directive that set the value, where the file ends, whole output on both paths, at -fsyntax-only and -c;
// a header that leaves the value as it found it, or sets a struct's layout under the includer's value, has none.
static void check_test_driver_pack_includes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* contents;
    } headers[] = {
        { "pack-changes.h", "#pragma pack(push, 1)\nstruct H { char c; int i; };\n" },
        { "pack-keeps.h", "#pragma pack(push, 1)\nstruct K { char c; int i; };\n#pragma pack(pop)\n" },
        { "pack-pops.h", "#pragma pack(pop)\n" },
        { "pack-nested.h", "struct N { char c; int i; };\n#include \"pack-changes.h\"\n" },
    };
    bool written = true;
    char path[PATH_MAX];
    for (uint32_t h = 0; h < BUSTER_ARRAY_LENGTH(headers); h += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, headers[h].name);
        written &= parse_test_write(path, headers[h].contents, strlen(headers[h].contents));
    }
    static const char source[] =
        "#pragma pack(push, 2)\n#include \"pack-keeps.h\"\n#include \"pack-pops.h\"\n#pragma pack(4)\n#include \"pack-nested.h\"\n#pragma pack(pop)\n"
        "#pragma pack(pop)\nint main(void) { return sizeof(struct H) != 5; }\n";
    char main_path[PATH_MAX];
    char object[PATH_MAX];
    snprintf(main_path, sizeof(main_path), "%s/pack-includes.c", directory);
    snprintf(object, sizeof(object), "%s/pack-includes.o", directory);
    written = written && parse_test_write(main_path, source, sizeof(source) - 1);
    check_test_expect(test, written, "pack inclusion fixtures are written");
    for (uint32_t mode = 0; written && mode < 2; mode += 1)
    {
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c", main_path, "-o", object, NULL, NULL };
        char* expected = NULL;
        const int oracle = check_test_driver_spawn(arguments, &expected);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[7] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = expected != NULL && actual != NULL && status == oracle && strstr(expected, "modified in the included file") != NULL &&
                              strcmp(actual, expected) == 0;
            char description[8192];
            snprintf(description, sizeof(description), "pack inclusions (%s, %s): %s\nagainst clang's\n%s", mode == 0 ? "-fsyntax-only" : "-c",
                     scalar != 0 ? "scalar" : "vector", actual != NULL ? actual : "", expected != NULL ? expected : "");
            check_test_expect(test, same, description);
            free(actual);
        }
        free(expected);
    }
    unlink(object);
    unlink(main_path);
    for (uint32_t h = 0; h < BUSTER_ARRAY_LENGTH(headers); h += 1)
    {
        snprintf(path, sizeof(path), "%s/%s", directory, headers[h].name);
        unlink(path);
    }
}

// #2449: a target-rejected alignment is still consumed by the parser. It leaves the pack stack alone, diagnoses
// once in source order and keeps later declarations available, including when only an original pragma view survives.
static void check_test_driver_pack_errors(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* source;
        const char* option;
        uint32_t errors;
    } cases[] = {
        { "#pragma options align=mac68k\nint value;\n", NULL, 1 },
        { "#pragma align=mac68k\n#pragma align=reset\nint value;\n", NULL, 1 },
        { "#pragma pack(push,1)\n#pragma options align=mac68k\nstruct S{char c;int x;};\n"
          "_Static_assert(sizeof(struct S)==5,\"unchanged\");\n#pragma options align=reset\n"
          "struct T{char c;int x;};_Static_assert(sizeof(struct T)==8,\"restored\");\n#pragma options align=reset\n", NULL, 1 },
        { "#pragma options align=mac68k\nint value = 1 + (void)0;\n", NULL, 2 },
        { "int value = ;\n#pragma options align=mac68k\n", NULL, 2 },
        { "#pragma options align=mac68k\nint value = ;\n", NULL, 2 },
        { "int value;\n#pragma options align=mac68k\n", NULL, 1 },
        { "#define SET_ALIGN _Pragma(\"options align=mac68k\")\nSET_ALIGN\nint value;\n", NULL, 1 },
        { "#pragma clang diagnostic ignored \"-Weverything\"\n#pragma options align=mac68k\nint value;\n", "-w", 1 },
        { "#pragma options align=mac68k\n#pragma options align=reset\nint value;\n", "-Wno-pragmas", 1 },
        { "#pragma options align=mac68k\n#pragma options align=reset\nint value;\n", "-Werror=ignored-pragmas", 2 },
        { "#pragma options align=mac68k extra\nint value;\n", NULL, 0 },
    };
    static const char sentinel[] = "previous object\n";
    char source[512];
    char object[512];
    snprintf(source, sizeof(source), "%s/pack-errors.c", directory);
    snprintf(object, sizeof(object), "%s/pack-errors.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, written, "alignment error fixture is written");
        for (uint32_t language = 0; written && language < 2; language += 1)
        {
            for (uint32_t mode = 0; mode < 2; mode += 1)
            {
                char* arguments[12] = { (char*)clang, language == 0 ? "-std=gnu17" : "-std=gnu23", "-fno-caret-diagnostics",
                                        mode == 0 ? "-fsyntax-only" : "-c", source, "-o", object };
                uint32_t count = 7;
                if (cases[c].option != NULL)
                {
                    arguments[count++] = (char*)cases[c].option;
                }
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                check_test_expect(test, expected != NULL && (oracle != 0) == (cases[c].errors != 0) &&
                                  check_test_pragma_diagnostics(expected, ": error: ") == cases[c].errors,
                                  "Clang reports the alignment fixture's independent errors");
                for (uint32_t scalar = 0; scalar < 2; scalar += 1)
                {
                    const bool saved = parse_test_write(object, sentinel, sizeof(sentinel) - 1);
                    check_test_expect(test, saved, "an existing output is installed before a rejected compilation");
                    arguments[0] = (char*)executable;
                    arguments[count] = scalar != 0 ? "--scalar" : NULL;
                    char* actual = NULL;
                    const int result = check_test_driver_spawn(arguments, &actual);
                    const bool equal = expected != NULL && actual != NULL && (result == 0) == (oracle == 0) && strcmp(actual, expected) == 0;
                    if (!equal)
                    {
                        fprintf(stderr, "alignment error case %u language %u mode %u scalar %u: clang=%d buster=%d\nclang: %s\nbuster: %s\n",
                                c, language, mode, scalar, oracle, result, expected != NULL ? expected : "", actual != NULL ? actual : "");
                    }
                    check_test_expect(test, equal, "alignment errors and subsequent diagnostics match Clang's whole output");
                    if (cases[c].errors != 0)
                    {
                        char bytes[sizeof(sentinel)] = { 0 };
                        FILE* file = fopen(object, "rb");
                        const size_t length = file != NULL ? fread(bytes, 1, sizeof(bytes), file) : 0;
                        const bool preserved = file != NULL && !ferror(file) && length == sizeof(sentinel) - 1 &&
                                               memcmp(bytes, sentinel, sizeof(sentinel) - 1) == 0;
                        if (file != NULL)
                        {
                            fclose(file);
                        }
                        check_test_expect(test, preserved, "a rejected alignment leaves the existing output intact");
                    }
                    free(actual);
                }
                free(expected);
            }
        }
    }
    unlink(object);
    unlink(source);
}

static void check_test_driver_ignored_pragmas(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char target_ignored[] =
        "#pragma scalar_storage_order big-endian\n#pragma GCC scalar_storage_order little-endian\n"
        "#pragma GCC target(\"avx2\")\n#pragma section(\"unused\", read, write)\n"
        "#pragma code_seg(\"unused\")\n#pragma data_seg(\"unused\")\n#pragma bss_seg(\"unused\")\n"
        "#pragma const_seg(\"unused\")\n#pragma init_seg(user)\n#pragma fenv_access(on)\n"
        "int main(void){int result=0;\n#pragma fenv_access(off)\nreturn result;}\n";
    static const char target_malformed[] =
        "#pragma scalar_storage_order invalid\n#pragma GCC target(\n#pragma section(\n"
        "#pragma data_seg(42)\n#pragma fenv_access(ON) trailing\nint main(void){return 0;}\n";
    static const struct
    {
        const char* source;
        const char* first;
        const char* second;
        const char* third;
    } cases[] = {
        { "#pragma buster_unknown\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma GCC target(\"avx2\")\n#pragma GCC push_options\n#pragma GCC pop_options\n#pragma GCC reset_options\nint main(void){return 0;}\n", "-Wall", NULL, NULL },
        { "#pragma GCC target(\"avx2\")\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wall", NULL, NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wunknown-pragmas", "-Werror", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", "-w", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wall", "-Wno-pragmas", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", "-Wno-error=unknown-pragmas", NULL },
        { "#pragma buster_unknown\nint main(void){return 0;}\n", "-Wno-unknown-pragmas", "-Wpragmas", NULL },
        { target_ignored, NULL, NULL, NULL },
        { target_ignored, "-Wall", NULL, NULL },
        { target_ignored, "-Werror=unknown-pragmas", NULL, NULL },
        { target_ignored, "-Werror=unknown-pragmas", "-w", NULL },
        { target_malformed, "-Wunknown-pragmas", NULL, NULL },
        { target_malformed, "-Werror=unknown-pragmas", NULL, NULL },
        { "#pragma float_control(pop)\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma float_control(pop)\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "#pragma float_control(pop)\nint main(void){return 0;}\n", "-Wno-ignored-pragmas", "-Werror", NULL },
        { "#pragma float_control(pop)\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", "-w", NULL },
        { "#pragma clang diagnostic ignored \"-Wignored-pragmas\"\n#pragma float_control(pop)\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "#pragma float_control(precise, off, push)\n#pragma float_control(pop)\n#pragma float_control(except, on)\nint main(void){return 0;}\n", "-Wall", "-Werror", NULL },
        { "#pragma clang fp contract(off) reassociate(on) reciprocal(on) exceptions(maytrap)\nint main(void){return 0;}\n", "-Wall", "-Werror", NULL },
        { "#pragma ident \"tooling\"\n#pragma GCC push_options\n#pragma GCC optimize(\"O3\")\n#pragma GCC ivdep\n#pragma GCC pop_options\nint main(void){return 0;}\n", "-Wall", NULL, NULL },
        { "#pragma clang buster_unknown\n#pragma GCC buster_unknown\n#pragma STDC buster_unknown\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma clang buster_unknown\n#pragma GCC buster_unknown\n#pragma STDC buster_unknown\nint main(void){return 0;}\n", "-Werror=pragmas", NULL, NULL },
        { "#pragma align 4\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma align 4\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "#pragma align 4\nint main(void){return 0;}\n", "-Wno-ignored-pragmas", "-Werror", NULL },
        // `#pragma options align` and `#pragma align` (#1849): malformed ones and an empty stack's reset warn.
        { "#pragma options align\n#pragma options\n#pragma align\n#pragma options foo=packed\n#pragma options align=\n#pragma align=bogus\n"
          "#pragma align=packed x\n#pragma align=reset\n#pragma options align=reset\n#pragma options align = natural extra\n#pragma options align=12\n"
          "#pragma options align=packed\nstruct S{char c;int n;};\n#pragma options align=reset\nint main(void){return sizeof(struct S)!=5;}\n", NULL, NULL, NULL },
        { "#pragma options align=reset\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "#pragma pack(2)\n#pragma options align=reset\n#pragma options align=reset\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma comment(lib, \"m\")\nint main(void){return 0;}\n", "-Wall", "-Werror", NULL },
        { "#pragma comment(user, \"tooling\")\nint main(void){return 0;}\n", "-Werror=ignored-pragmas", NULL, NULL },
        { "int main(void){int sum=0;\n#pragma omp parallel for\nfor(int i=0;i<4;i++)sum+=i;return sum!=6;}\n", NULL, NULL, NULL },
        { "int main(void){int sum=0;\n#pragma omp parallel for\nfor(int i=0;i<4;i++)sum+=i;return sum!=6;}\n", "-Wall", "-Werror", NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-Wsource-uses-openmp", NULL, NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-Werror=source-uses-openmp", NULL, NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-Wopenmp", "-Werror", NULL },
        { "int main(void){\n#pragma omp parallel\n{}return 0;}\n", "-fopenmp", "-fno-openmp", NULL },
        { "#define _OPENMP 202111\nint main(void){\n#pragma omp parallel\n{}return 0;}\n", NULL, NULL, NULL },
        { "#pragma clang diagnostic error \"-Wunknown-pragmas\"\n#pragma clang diagnostic push\n#pragma GCC diagnostic ignored \"-Wunknown-pragmas\"\n#pragma buster_hidden\n#pragma clang diagnostic pop\n#pragma buster_visible\nint main(void){return 0;}\n", NULL, NULL, NULL },
        { "#pragma buster_before\n#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint main(void){return 0;}\n", "-Wunknown-pragmas", NULL, NULL },
        { "#pragma clang diagnostic ignored \"-Wpragmas\"\n#pragma align 4\n#pragma buster_hidden\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#pragma clang diagnostic error \"-Wsource-uses-openmp\"\nint main(void){\n#pragma omp parallel\n{}return 0;}\n", NULL, NULL, NULL },
        { "int main(void){return 0;}\n#pragma buster_eof\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#pragma buster_only\n", "-Wunknown-pragmas", NULL, NULL },
        { "#pragma\nint main(void){return 0;}\n", "-Werror=unknown-pragmas", NULL, NULL },
        { "#define P _Pragma(\"buster_macro\")\nint main(void){return P 0;}\n", "-Wunknown-pragmas", NULL, NULL },
        { "int value=\n#pragma buster_before_error\n;\n", "-Wunknown-pragmas", NULL, NULL },
        { "int value=;\n#pragma buster_after_error\n", "-Wunknown-pragmas", NULL, NULL },
        { "#pragma clang diagnostic error \"-Wunknown-pragmas\"\nint value=\n#pragma buster_before_error\n;\n", NULL, NULL, NULL },
    };
    char source[512];
    char object[512];
    snprintf(source, sizeof(source), "%s/ignored-pragma.c", directory);
    snprintf(object, sizeof(object), "%s/ignored-pragma.o", directory);
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(cases); c += 1)
    {
        const bool written = parse_test_write(source, cases[c].source, strlen(cases[c].source));
        check_test_expect(test, written, "ignored pragma fixture is written");
        for (uint32_t mode = 0; written && mode < 2; mode += 1)
        {
            char* arguments[16] = { (char*)clang, "-std=gnu17", "-fno-caret-diagnostics", mode == 0 ? "-fsyntax-only" : "-c", source, "-o", object };
            uint32_t count = 7;
            if (cases[c].first != NULL)
            {
                arguments[count++] = (char*)cases[c].first;
            }
            if (cases[c].second != NULL)
            {
                arguments[count++] = (char*)cases[c].second;
            }
            if (cases[c].third != NULL)
            {
                arguments[count++] = (char*)cases[c].third;
            }
            char* expected = NULL;
            const int oracle = check_test_driver_spawn(arguments, &expected);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                unlink(object);
                arguments[0] = (char*)executable;
                arguments[count] = scalar != 0 ? "--scalar" : NULL;
                char* actual = NULL;
                const int result = check_test_driver_spawn(arguments, &actual);
                bool equal = expected != NULL && actual != NULL && (result == 0) == (oracle == 0);
                static const char* const groups[] = { "-Wunknown-pragmas]", "-Wignored-pragmas]", "-Wsource-uses-openmp]" };
                for (uint32_t group = 0; group < BUSTER_ARRAY_LENGTH(groups); group += 1)
                {
                    equal &= check_test_pragma_diagnostics(actual, groups[group]) == check_test_pragma_diagnostics(expected, groups[group]);
                }
                if (!equal)
                {
                    fprintf(stderr, "pragma case %u mode %u scalar %u: clang=%d buster=%d\nclang: %s\nbuster: %s\n", c, mode, scalar, oracle, result,
                            expected != NULL ? expected : "", actual != NULL ? actual : "");
                }
                check_test_expect(test, equal, "retained pragma acceptance and warning groups match Clang");
                check_test_expect(test, result == 0 || access(object, F_OK) != 0, "a rejected pragma compilation does not emit an object");
                free(actual);
            }
            free(expected);
        }
    }
    static const struct
    {
        const char* directive;
        const char* name;
        const char* first;
        const char* second;
        // With code generation, an active OpenMP request is refused by the driver's instrumentation barrier before
        // the checker reads the source; `barrier` is the spelling that barrier must name.
        const char* barrier;
    } refused[] = {
        // A target the unit doesn't define (#1849: clang's "alias must point to a defined variable or function").
        { "#pragma weak alias = target\nextern int target;\n", "weak", NULL, NULL, NULL },
        { "#pragma omp parallel\nint object;\n", "omp", "-fopenmp", NULL, "-fopenmp" },
        { "#pragma omp parallel\nint object;\n", "omp", "-fno-openmp", "-fopenmp", "-fopenmp" },
        { "#undef _OPENMP\n#pragma omp parallel\nint object;\n", "omp", "-fopenmp", NULL, "-fopenmp" },
        { "#pragma omp parallel\nint object;\n", "omp", "-fopenmp=libiomp5", "-undef", "-fopenmp=libiomp5" },
        { "#pragma omp parallel\nint object;\n", "omp", "-fopenmp=libgomp", NULL, "-fopenmp=libgomp" },
    };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(refused); c += 1)
    {
        char body[1024];
        const int length = snprintf(body, sizeof(body), "#pragma clang diagnostic ignored \"-Weverything\"\n%s", refused[c].directive);
        const bool written = length > 0 && (uint32_t)length < sizeof(body) && parse_test_write(source, body, (uint64_t)length);
        check_test_expect(test, written, "unsupported semantic pragma fixture is written");
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            // A non-emitting action still reaches the checker's semantic refusal for every directive, OpenMP's among
            // them, and writes no object.
            unlink(object);
            char* syntax[14] = { (char*)executable, "-std=gnu17", "-fsyntax-only", source, "-w" };
            uint32_t syntax_count = 5;
            if (refused[c].first != NULL)
            {
                syntax[syntax_count++] = (char*)refused[c].first;
            }
            if (refused[c].second != NULL)
            {
                syntax[syntax_count++] = (char*)refused[c].second;
            }
            syntax[syntax_count] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int syntax_status = check_test_driver_spawn(syntax, &actual);
            check_test_expect(test, syntax_status != 0 && actual != NULL && strstr(actual, "unsupported semantic '#pragma ") != NULL &&
                              strstr(actual, refused[c].name) != NULL && access(object, F_OK) != 0,
                              "warning controls cannot suppress a named semantic pragma refusal");
            free(actual);
            // Code generation refuses an active OpenMP request by name before the source is read; the other directives
            // reach the checker's refusal and still write no object.
            char* arguments[14] = { (char*)executable, "-std=gnu17", "-c", source, "-o", object, "-w" };
            uint32_t count = 7;
            if (refused[c].first != NULL)
            {
                arguments[count++] = (char*)refused[c].first;
            }
            if (refused[c].second != NULL)
            {
                arguments[count++] = (char*)refused[c].second;
            }
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            actual = NULL;
            const int compiled = check_test_driver_spawn(arguments, &actual);
            const bool named = compiled != 0 && actual != NULL &&
                               (refused[c].barrier != NULL ? strstr(actual, "unsupported code generation option '") != NULL &&
                                                                 strstr(actual, refused[c].barrier) != NULL :
                                                             strstr(actual, "unsupported semantic '#pragma ") != NULL &&
                                                                 strstr(actual, refused[c].name) != NULL);
            check_test_expect(test, named && access(object, F_OK) != 0,
                              refused[c].barrier != NULL ? "an active OpenMP request is refused by name before code generation" :
                                                           "warning controls cannot suppress a named semantic pragma refusal or emit an object");
            free(actual);
        }
    }
    // Malformed supported directives are still owned by their semantic readers, not the unknown fallback.
    static const char* const malformed[] = { "#pragma clang loop unroll_count(\n", "#pragma clang optimize maybe\n", "#pragma clang section text=4\n" };
    for (uint32_t c = 0; c < BUSTER_ARRAY_LENGTH(malformed); c += 1)
    {
        char body[512];
        const int length = snprintf(body, sizeof(body), "%sint main(void){return 0;}\n", malformed[c]);
        const bool written = length > 0 && (uint32_t)length < sizeof(body) && parse_test_write(source, body, (uint64_t)length);
        for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
        {
            char* text = NULL;
            char* arguments[] = { (char*)executable, "-fsyntax-only", "-w", source, scalar != 0 ? "--scalar" : NULL, NULL };
            check_test_expect(test, check_test_driver_spawn(arguments, &text) != 0, "malformed supported pragma does not become an ignorable unknown");
            free(text);
        }
    }
    check_test_driver_pragma_system(test, clang, executable, directory);
    check_test_driver_pack_errors(test, clang, executable, directory);
    check_test_driver_pragma_execution(test, executable, directory);
    check_test_driver_fp_pragma_errors(test, clang, executable, directory);
    check_test_driver_fp_evaluation(test, clang, executable, directory);
    check_test_driver_symbol_pragma_diagnostics(test, clang, executable, directory);
    check_test_driver_symbol_pragma_objects(test, clang, executable, directory);
    check_test_driver_ms_diagnostics(test, clang, executable, directory);
    check_test_driver_ms_abi(test, clang, executable, directory);
    unlink(source);
    unlink(object);
}
