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
        { "minsize", "int g(int x) {\nl: __attribute__((minsize));\nreturn x;\n}\n" },
        { "no_split_stack", "int g(int x) {\nl: __attribute__((no_split_stack));\nreturn x;\n}\n" },
        { "no_sanitize_memory", "int g(int x) {\nl: __attribute__((no_sanitize_memory));\nreturn x;\n}\n" },
        { "nomerge", "int g(int x) {\nl: __attribute__((nomerge));\nreturn x;\n}\n" },
        { "zero_call_used_regs", "int g(int x) {\nl: __attribute__((zero_call_used_regs(\"skip\")));\nreturn x;\n}\n" },
        { "suppress", "int g(int x) {\nl: __attribute__((suppress));\nreturn x;\n}\n" },
        { "assume", "int g(int x) {\nl: __attribute__((assume(1)));\nreturn x;\n}\n" },
        { "lifetimebound", "int g(int x) {\nl: __attribute__((lifetimebound));\nreturn x;\n}\n" },
        { "counted_by", "int g(int x) {\nl: __attribute__((counted_by(x)));\nreturn x;\n}\n" },
        { "btf_decl_tag", "int g(int x) {\nl: __attribute__((btf_decl_tag(\"t\")));\nreturn x;\n}\n" },
        { "minsize after unused", "int g(int x) {\nl: __attribute__((unused, minsize));\nreturn x;\n}\n" },
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
        { "nomerge before a return", "int g(int (*p)(void)) {\n__attribute__((nomerge)) return p();\n}\n" },
        { "nomerge before a call", "int g(int (*p)(void)) {\n__attribute__((nomerge)) p();\nreturn 0;\n}\n" },
        { "suppress before an expression", "int g(int x) {\n__attribute__((suppress(\"x\"))) x++;\nreturn x;\n}\n" },
        { "suppress without strings", "int g(int x) {\n__attribute__((suppress)) x++;\nreturn x;\n}\n" },
        { "nomerge and suppress before an if", "int g(int x) {\n__attribute__((nomerge, suppress(\"a\", \"b\"))) if (x) x++;\nreturn x;\n}\n" },
        { "clang::nomerge before a return", "int g(int (*p)(void)) {\n[[clang::nomerge]] return p();\n}\n" },
        { "fallthrough in a switch, beside noinline", "int f(int);\nint g(int x) {\nswitch (x) {\ncase 0:\nx = f(x);\n__attribute__((noinline, fallthrough));\n"
                                                      "case 1:\nreturn x;\n}\nreturn 0;\n}\n" },
        { "fallthrough at the switch's end", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: g(); __attribute__((fallthrough));\n}\nreturn 0;\n}\n" },
        { "fallthrough in a while's body", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: while (y) { __attribute__((fallthrough)); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a call", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); g(); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough in an if before a call", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: if (y) { __attribute__((fallthrough)); } g(); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough after default, at the end", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ndefault: __attribute__((fallthrough));\n}\nreturn 0;\n}\n" },
        { "fallthrough in a do's body", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: do { __attribute__((fallthrough)); } while (y); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a plain label and a case", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); lbl: case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a call in a block", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: { __attribute__((fallthrough)); g(); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough in a for's body", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: for (;;) { __attribute__((fallthrough)); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before another fallthrough", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); __attribute__((fallthrough)); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough after a break in a while", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: while (y) { if (x) break; __attribute__((fallthrough)); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough after a goto, before its label", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: goto skip; __attribute__((fallthrough)); skip: case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough in an if's block", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: if (y) { __attribute__((fallthrough)); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough in a block", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: { { __attribute__((fallthrough)); } } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough in both branches", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: if (y) { g(); __attribute__((fallthrough)); } else { __attribute__((fallthrough)); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a null statement", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); ; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough at an inner switch's end, before the outer's case", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: switch (y) { case 3: __attribute__((fallthrough)); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough after a return", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: return 0; __attribute__((fallthrough)); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough in an if's else", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: if (y) return 1; else __attribute__((fallthrough)); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before default", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: g(); __attribute__((fallthrough)); default: return 1;\n}\nreturn 0;\n}\n" },
        // #1126's review: clang looks through blocks without executable statements to the fallthrough before them: a plain
        // label on a null statement, an empty block or another such label, and a block of one, before a case or one that
        // begins a block; but not a label on a statement or on the case itself.
        { "fallthrough before a label on a null statement", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); here: ; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a label on an empty block", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); here: { ; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before two labels on a null statement", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); here: there: ; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a block of a label", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { here: ; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a block that begins with a case", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { case 2: return 1; }\n}\nreturn 0;\n}\n" },
        { "fallthrough before a label on a block that begins with a case", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); here: { case 2: return 1; }\n}\nreturn 0;\n}\n" },
        { "fallthrough before a label on a call", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); here: g(); case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a label on a null statement and a call", "void g(void);\nint f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); here: ; g(); case 2: return 1;\n}\nreturn 0;\n}\n" },
        // Declarations that declare no variable, which clang's CFG doesn't add (#1126's review): a typedef, a static
        // assertion, a tag's, a function's with a parameter, an enumerator's; but a variable's, an extern one's, a
        // function pointer's whose type names a parameter, and a typedef of a variably modified type are statements.
        { "fallthrough before a block of a typedef", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { typedef int T; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a static assertion and a tag", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); _Static_assert(1, \"\"); struct S; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a function's declaration", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { int g(int p); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before an enumeration and a struct", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { enum { Q = 3 }; struct T { int a; }; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a function pointer type", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { typedef int (*F)(int q); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a variable", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { extern int z; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a function pointer variable", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { int (*h)(int r); } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "fallthrough before a variably modified typedef", "int f(int x, int y) {\nswitch (x) {\ncase 1: __attribute__((fallthrough)); { typedef int V[x]; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        // A fallthrough that can't run (#1126's review): clang takes it, with only a warning, when its CFG block reaches a case
        // label, after a return, a break, a goto whose label comes later, a constant-true if's return and an infinite loop,
        // through straight-line code, a block of it and an if without an else; but not past another return, a loop or the
        // switch's end, nor where a label (or a case before it) lets control reach it.
        { "an unreachable fallthrough after a return", "int f(int x, int y) {\nswitch (x) {\ncase 1: return 2; __attribute__((fallthrough)); x++; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough after a break", "int f(int x, int y) {\nswitch (x) {\ncase 1: break; __attribute__((fallthrough)); x++; { int z = y; } case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough before a goto's later label", "int f(int x, int y) {\nswitch (x) {\ncase 1: goto skip; __attribute__((fallthrough)); x++; skip: ; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough after if (1) and an infinite loop", "int f(int x, int y) {\nswitch (x) {\ncase 1: if (1) return 2; __attribute__((fallthrough)); if (y) x++; case 2: for (;;) ; __attribute__((fallthrough)); y++; case 3: return 1;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough before a return", "int f(int x, int y) {\nswitch (x) {\ncase 1: return 2; __attribute__((fallthrough)); x++; return 3; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough at the switch's end", "int f(int x, int y) {\nswitch (x) {\ncase 1: return 2; __attribute__((fallthrough)); x++;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough before a conditional", "int f(int x, int y) {\nswitch (x) {\ncase 1: return 2; __attribute__((fallthrough)); x = y ? 1 : 2; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "an unreachable fallthrough before a label no goto names", "int f(int x, int y) {\nswitch (x) {\ncase 1: return 2; __attribute__((fallthrough)); here: ; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a fallthrough after a conditional return", "int f(int x, int y) {\nswitch (x) {\ncase 1: if (y) return 2; __attribute__((fallthrough)); x++; case 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a fallthrough after a return and a case", "int f(int x, int y) {\nswitch (x) {\ncase 1: return 2; case 5: __attribute__((fallthrough)); x++; case 2: return 1;\n}\nreturn 0;\n}\n" },
        // Statements that never complete through calls (#1126's review): a function declared noreturn, by any spelling and
        // through redeclarations, called wherever the statement runs (an operand, an argument, an initializer, a declaration's,
        // an if's, a switch's, a loop's condition); a library function and a builtin clang marks noreturn; __builtin_assume
        // of a false condition. And conditions folded as clang's CFG builder folds them, through && and ||, `!` and a const
        // object. But a call in a short-circuited operand, a conditional's arm, sizeof, through a pointer, a declaration
        // alone, a for's increment, and a condition that doesn't fold leave the fallthrough reachable.
        { "unreachable fallthroughs after calls that never return",
          "__attribute__((noreturn)) void die(void);\n_Noreturn void quit(int);\nvoid stop(void);\n_Noreturn void stop(void);\n_Noreturn int fail(void);\nint fail(void);\n"
          "void abort(void);\nvoid exit(int);\nint g(int);\nint f(int x, int y) {\nswitch (x) {\n"
          "case 1: die(); __attribute__((fallthrough)); x++;\ncase 2: (void)quit(y); __attribute__((fallthrough)); y++;\n"
          "case 3: stop(); __attribute__((fallthrough)); y++;\ncase 4: x = fail() + 1; __attribute__((fallthrough)); y++;\n"
          "case 5: (&die)(); __attribute__((fallthrough)); y++;\ncase 6: if (y) die(); else quit(x); __attribute__((fallthrough)); y++;\n"
          "case 7: { quit(x++); } __attribute__((fallthrough)); y++;\ncase 8: g(fail()); __attribute__((fallthrough)); y++;\n"
          "case 9: y++; int z = fail(); __attribute__((fallthrough)); y += z;\ncase 10: abort(); __attribute__((fallthrough)); y++;\n"
          "case 11: exit(1); __attribute__((fallthrough)); y++;\ncase 12: __builtin_abort(); __attribute__((fallthrough)); y++;\n"
          "case 13: __builtin_assume(0); __attribute__((fallthrough)); y++;\ncase 14: if (fail()) y++; __attribute__((fallthrough)); y++;\n"
          "case 15: switch (fail()) { } __attribute__((fallthrough)); y++;\ncase 16: while (fail()) y++; __attribute__((fallthrough)); y++;\n"
          "case 17: do y++; while (fail()); __attribute__((fallthrough)); y++;\ncase 18: for (int i = fail(); ;) break; __attribute__((fallthrough)); y++;\n"
          "case 19: y++; long q[2] = { fail() }; __attribute__((fallthrough)); y += (int)q[0];\n"
          "case 20: (void)__builtin_expect(fail(), 0); __attribute__((fallthrough)); y++;\ncase 21: return y;\n}\nreturn 0;\n}\n" },
        { "unreachable fallthroughs after folded conditions",
          "enum { K = 1 };\nstatic const int k = 1;\nint f(int x, int y) {\nswitch (x) {\n"
          "case 1: if (2 - 1) return 2; __attribute__((fallthrough)); x++;\ncase 2: if (k) return 3; __attribute__((fallthrough)); x++;\n"
          "case 3: if (K && sizeof(int) == 4) return 4; __attribute__((fallthrough)); x++;\ncase 4: if (y || 1) return 5; __attribute__((fallthrough)); x++;\n"
          "case 5: if (y && 0) ; else return 6; __attribute__((fallthrough)); x++;\ncase 6: if (!(y && 0)) return 7; __attribute__((fallthrough)); x++;\n"
          "case 7: while (2 - 1) ; __attribute__((fallthrough)); x++;\ncase 8: for (; 1 + 1;) ; __attribute__((fallthrough)); x++;\n"
          "case 9: do ; while (!0); __attribute__((fallthrough)); x++;\ncase 10: if (1.5) return 8; __attribute__((fallthrough)); x++;\n"
          "case 11: if ((void*)0) ; else return 9; __attribute__((fallthrough)); x++;\ncase 12: if ((y++, 1)) return 10; __attribute__((fallthrough)); x++;\n"
          "case 13: return 1;\n}\nreturn 0;\n}\n" },
        // Each alone: clang's analysis of a function doesn't run after an earlier function's error.
        { "a reachable fallthrough after a short-circuited call", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; y && (die(), 1); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a conditional's arm", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; y ? die() : g(); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after sizeof", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; (void)sizeof(fail()); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a call through a pointer", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; { void (*p)(void) = die; p(); } __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after __builtin_assume of a variable", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; __builtin_assume(y); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a condition that doesn't fold", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; if (y || 0) return 2; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a variable condition", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; int k2 = 1; if (k2) return 3; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after an and of a variable", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; if (y && 1) return 4; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a declaration of a noreturn function", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; { _Noreturn void quit(void); } __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a for's increment", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; for (;; fail()) break; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after one branch", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; if (y) die(); else g(); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a right operand", "_Noreturn void die(void);\n_Noreturn int fail(void);\nvoid g(void);\n"
          "int f(int x, int y) {\nswitch (x) {\ncase 1: y++; (void)(y && fail()); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        // Clang's analysis runs at a function's end only where no error came before it.
        { "a misplaced fallthrough after an undeclared identifier",
          "int f(int x) {\nswitch (x) {\ncase 1: undeclared_thing; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a misplaced fallthrough before a later function's undeclared identifier",
          "int f(int x) {\nswitch (x) {\ncase 1: x++; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\nint g(void) {\nreturn nope;\n}\n" },
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
    // clang's analysis of a function's CFG, which reports a misplaced fallthrough, doesn't run under -w, nor for a
    // function in a system header without -Wsystem-headers: the same header's misplaced fallthrough found through
    // -isystem and through -I, with each option, must give clang's errors (or none) by both paths.
    char system_directory[PATH_MAX];
    char header[PATH_MAX];
    snprintf(system_directory, sizeof(system_directory), "%s/fallthrough-headers", directory);
    snprintf(header, sizeof(header), "%s/fallthrough.h", system_directory);
    static const char header_text[] = "static inline int fallthrough_header(int x) {\nswitch (x) {\ncase 1: __attribute__((fallthrough));\n}\nreturn x;\n}\n";
    static const char includer[] = "#include <fallthrough.h>\nint g(int x) { return fallthrough_header(x); }\n";
    const bool headers = (mkdir(system_directory, 0700) == 0 || errno == EEXIST) && parse_test_write(header, header_text, strlen(header_text)) &&
                         parse_test_write(source, includer, strlen(includer));
    static const char* const searches[] = { "-isystem", "-I" };
    static const char* const options[] = { "-std=gnu17", "-Wsystem-headers", "-w" };
    for (uint32_t s = 0; headers && s < BUSTER_ARRAY_LENGTH(searches); s += 1)
    {
        for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
        {
            char* arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-color-diagnostics", (char*)options[o], (char*)searches[s], system_directory, source, NULL };
            char* reference = NULL;
            const int oracle = check_test_driver_spawn(arguments, &reference);
            char expected[8192];
            const bool expected_valid = check_test_cleanup_errors(reference, 0, expected, sizeof(expected));
            free(reference);
            for (uint32_t scalar = 0; scalar < 2; scalar += 1)
            {
                char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu17", "-fsyntax-only", (char*)options[o], (char*)searches[s],
                                    system_directory, source, scalar != 0 ? "--scalar" : NULL, NULL };
                char* text = NULL;
                const int status = check_test_driver_spawn(checked, &text);
                char actual[8192];
                const bool actual_valid = check_test_cleanup_errors(text, 0, actual, sizeof(actual));
                free(text);
                char description[512];
                snprintf(description, sizeof(description), "a header's misplaced fallthrough through %s with %s, by the %s path: clang's (status %d)\n%sagainst (status %d)\n%s",
                         searches[s], options[o], scalar != 0 ? "scalar" : "vector", oracle, expected, status, actual);
                check_test_expect(test, (oracle == 0 || oracle == 1) && status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0,
                                  description);
            }
        }
    }
    unlink(header);
    unlink(source);
    rmdir(system_directory);
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
