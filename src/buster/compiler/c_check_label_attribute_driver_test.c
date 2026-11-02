// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// GNU attributes after a label's colon and a statement's attributes (#147, check_input_label_attributes and
// check_input_statement_attributes) by the driver against clang, on original fixtures. clang's parser takes every GNU
// attribute after a label's colon as the label's in C, whatever follows it, and its Sema takes unused and the attributes
// it ignores there, and refuses those whose subjects have no label among them (check_input_label_refusal). Before a
// statement, clang ignores GNU's noinline and always_inline, takes fallthrough on a null statement in a switch and
// musttail on a return, and refuses the others. The errors are compared, their places and messages, in order, with the
// exit status, by both paths of -fsyntax-only (each function's first error alone, as the checker reports them); then a
// program of labels with attributes before null statements, statements, a compound statement and a declaration, whose
// attributes are the label's and not the declaration's, runs as clang's build does. Warnings aren't compared. Include
// after c_check_musttail_driver_test.c, whose reader this takes.

static void check_test_driver_label_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const struct
    {
        const char* name;
        const char* source;
    } fixtures[] = {
        { "unused before a null statement", "int g(int x) {\ngoto out;\nout: __attribute__((unused));\nreturn x;\n}\n" },
        { "unused before a return", "int g(int x) {\nout: __attribute__((unused)) return x;\n}\n" },
        { "__unused__ before an expression", "int g(int x) {\nout: __attribute__((__unused__)) x += 1;\nreturn x;\n}\n" },
        { "two lists before a compound statement", "int g(int x) {\nout: __attribute__((unused)) __attribute__((cold, hot)) { x += 1; }\nreturn x;\n}\n" },
        { "ignored attributes", "int g(int x) {\nout: __attribute__((aligned(8), deprecated, noreturn, used, weak, const, packed));\nreturn x;\n}\n" },
        { "before a declaration", "int g(int x) {\nout: __attribute__((unused)) int y = x;\nreturn y;\n}\n" },
        { "section", "int g(int x) {\nout: __attribute__((section(\"s\")));\nreturn x;\n}\n" },
        { "__section__", "int g(int x) {\nout: __attribute__((__section__(\"s\")));\nreturn x;\n}\n" },
        { "fallthrough", "int g(int x) {\nswitch (x) {\ncase 0:\nout: __attribute__((fallthrough));\ncase 1:\nreturn x;\n}\nreturn 0;\n}\n" },
        { "musttail", "int f(int);\nint g(int x) {\nout: __attribute__((musttail)) return f(x);\n}\n" },
        { "weakref", "int g(int x) {\nout: __attribute__((weakref));\nreturn x;\n}\n" },
        { "alias", "int g(int x) {\nout: __attribute__((alias(\"h\")));\nreturn x;\n}\n" },
        { "no_sanitize", "int g(int x) {\nl: __attribute__((no_sanitize(\"address\")));\nreturn x;\n}\n" },
        { "no_sanitize_address", "int g(int x) {\nl: __attribute__((no_sanitize_address));\nreturn x;\n}\n" },
        { "no_sanitize_thread", "int g(int x) {\nl: __attribute__((no_sanitize_thread));\nreturn x;\n}\n" },
        { "no_address_safety_analysis", "int g(int x) {\nl: __attribute__((no_address_safety_analysis));\nreturn x;\n}\n" },
        { "error", "int g(int x) {\nl: __attribute__((error(\"e\")));\nreturn x;\n}\n" },
        { "warning", "int g(int x) {\nl: __attribute__((warning(\"w\")));\nreturn x;\n}\n" },
        { "flatten", "int g(int x) {\nl: __attribute__((flatten));\nreturn x;\n}\n" },
        { "overloadable", "int g(int x) {\nl: __attribute__((overloadable));\nreturn x;\n}\n" },
        { "target", "int g(int x) {\nl: __attribute__((target(\"avx2\")));\nreturn x;\n}\n" },
        { "target_clones", "int g(int x) {\nl: __attribute__((target_clones(\"avx2\", \"default\")));\nreturn x;\n}\n" },
        { "min_vector_width", "int g(int x) {\nl: __attribute__((min_vector_width(128)));\nreturn x;\n}\n" },
        { "mode", "int g(int x) {\nl: __attribute__((mode(SI)));\nreturn x;\n}\n" },
        { "tls_model", "int g(int x) {\nl: __attribute__((tls_model(\"local-exec\")));\nreturn x;\n}\n" },
        { "a refused one after an ignored one", "int g(int x) {\nout: __attribute__((unused, section(\"s\")));\nreturn x;\n}\n" },
        { "a refused one in a second list", "int g(int x) {\nout: __attribute__((unused)) __attribute__((flatten)) return x;\n}\n" },
        // A statement's attributes, without a label.
        { "unused before a null statement", "int g(int x) {\n__attribute__((unused));\nreturn x;\n}\n" },
        { "__unused__ before a return", "int g(int x) {\n__attribute__((__unused__)) return x;\n}\n" },
        { "C23's before a null statement", "int g(int x) {\n[[maybe_unused]];\nreturn x;\n}\n" },
        { "C23's after a label's GNU one", "int g(int x) {\nout: __attribute__((unused)) [[maybe_unused]];\nreturn x;\n}\n" },
        { "cold before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((cold)) if (x) f(x);\nreturn x;\n}\n" },
        { "hot before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((hot)) while (x) x--;\nreturn x;\n}\n" },
        { "deprecated before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((deprecated)) { x++; }\nreturn x;\n}\n" },
        { "aligned before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((aligned(8))) for (; x; ) x--;\nreturn x;\n}\n" },
        { "section before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((section(\"s\"))) do x--; while (x);\nreturn x;\n}\n" },
        { "weak before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((weak)) switch (x) { default: break; }\nreturn x;\n}\n" },
        { "used before a statement's keyword", "int f(int);\nint g(int x) {\n__attribute__((used)) goto l;\nl:\nreturn x;\n}\n" },
        { "noinline and always_inline, ignored", "int f(int);\nint g(int x) {\n__attribute__((noinline)) x = f(x);\n__attribute__((always_inline)) if (x) f(x);\n"
                                                 "__attribute__((__noinline__, always_inline)) { x++; }\n__attribute__((noinline));\n__attribute__((noinline)) return f(x);\n}\n" },
        { "fallthrough before an if", "int f(int);\nint g(int x) {\n__attribute__((fallthrough)) if (x) f(x);\nreturn x;\n}\n" },
        { "fallthrough before an expression", "int f(int);\nint g(int x) {\n__attribute__((fallthrough)) x = f(x);\nreturn x;\n}\n" },
        { "fallthrough after noinline before a return", "int f(int);\nint g(int x) {\n__attribute__((noinline, fallthrough)) return f(x);\n}\n" },
        { "fallthrough outside a switch", "int g(int x) {\n__attribute__((fallthrough));\nreturn x;\n}\n" },
        { "C23's fallthrough outside a switch", "int g(int x) {\n[[fallthrough]];\nreturn x;\n}\n" },
        { "a refused one after an ignored one, before a return", "int f(int);\nint g(int x) {\n__attribute__((noinline, unused)) return f(x);\n}\n" },
        { "fallthrough in a switch, beside noinline", "int f(int);\nint g(int x) {\nswitch (x) {\ncase 0:\nx = f(x);\n__attribute__((noinline, fallthrough));\n"
                                                      "case 1:\nreturn x;\n}\nreturn 0;\n}\n" },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/label-attributes.c", directory);
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(fixtures); i += 1)
    {
        const bool made = parse_test_write(source, fixtures[i].source, strlen(fixtures[i].source));
        char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", source, NULL };
        char* reference = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &reference) : -1;
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, 0, expected, sizeof(expected));
        free(reference);
        char description[512];
        snprintf(description, sizeof(description), "label attributes, %s: clang runs it", fixtures[i].name);
        check_test_expect(test, made && (oracle == 0 || oracle == 1) && expected_valid && (oracle == 0) == (expected[0] == 0), description);
        for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", scalar != 0 ? "--scalar" : source,
                                scalar != 0 ? source : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, 0, actual, sizeof(actual));
            free(text);
            snprintf(description, sizeof(description), "label attributes, %s, by the %s path: clang's\n%sagainst\n%s", fixtures[i].name,
                     scalar != 0 ? "scalar" : "vector", expected, actual);
            check_test_expect(test, status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
        }
    }
    unlink(source);
    // A label's attributes are its own, a declaration's after it among them: aligned(64) doesn't align y, which the
    // program reads through its address's remainder, as clang's build does.
    static const char program[] =
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "static int f(int x) {\n"
        "    goto out;\n"
        "out: __attribute__((aligned(64))) char y = (char)x;\n"
        "    char pad = 3;\n"
        "    return y + pad + (int)((uintptr_t)&y % 64 == 0 && (uintptr_t)&pad % 64 == 0) * 100;\n"
        "}\n"
        "static int g(int x) {\n"
        "    if (x) goto a;\n"
        "    x += 10;\n"
        "a: __attribute__((unused)) __attribute__((cold));\n"
        "    return x;\n"
        "}\n"
        "static int h(int x) {\n"
        "b: __attribute__((unused)) { x += 1; }\n"
        "c: __attribute__((__unused__)) x *= 2;\n"
        "d: __attribute__((deprecated)) if (x < 100) goto b;\n"
        "    return x;\n"
        "}\n"
        "int main(void) { printf(\"%d %d %d %d\\n\", f(4), g(1), g(0), h(3)); return 0; }\n";
    char program_path[PATH_MAX];
    char reference_path[PATH_MAX];
    char built_path[PATH_MAX];
    snprintf(program_path, sizeof(program_path), "%s/label-attributes-program.c", directory);
    snprintf(reference_path, sizeof(reference_path), "%s/label-attributes-clang", directory);
    snprintf(built_path, sizeof(built_path), "%s/label-attributes-buster", directory);
    const bool written = parse_test_write(program_path, program, strlen(program));
    char* reference_arguments[] = { (char*)clang, "-std=gnu17", "-O0", "-w", "-o", reference_path, program_path, NULL };
    char* reference_text = NULL;
    const int reference_status = written ? check_test_driver_spawn(reference_arguments, &reference_text) : -1;
    free(reference_text);
    char* run_reference[] = { reference_path, NULL };
    char* expected_output = NULL;
    const int expected_status = reference_status == 0 ? check_test_driver_spawn_stream(run_reference, &expected_output, true) : -1;
    for (uint32_t scalar = 0; written && scalar < 2; scalar += 1)
    {
        char* built_arguments[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-o", built_path, scalar != 0 ? "--scalar" : program_path,
                                    scalar != 0 ? program_path : NULL, NULL };
        char* built_text = NULL;
        const int built_status = check_test_driver_spawn(built_arguments, &built_text);
        char* run_built[] = { built_path, NULL };
        char* actual_output = NULL;
        const int actual_status = built_status == 0 ? check_test_driver_spawn_stream(run_built, &actual_output, true) : -1;
        char description[1024];
        snprintf(description, sizeof(description), "label attributes' program, by the %s path: clang's build prints\n%s, and ours\n%s%s", scalar != 0 ? "scalar" : "vector",
                 expected_output != NULL ? expected_output : "(nothing)", actual_output != NULL ? actual_output : "(nothing)", built_text != NULL ? built_text : "");
        check_test_expect(test, expected_status == 0 && actual_status == 0 && expected_output != NULL && actual_output != NULL &&
                                    strcmp(expected_output, actual_output) == 0 && strcmp(expected_output, "7 1 10 158\n") == 0,
                          description);
        free(built_text);
        free(actual_output);
        unlink(built_path);
    }
    free(expected_output);
    unlink(program_path);
    unlink(reference_path);
}
