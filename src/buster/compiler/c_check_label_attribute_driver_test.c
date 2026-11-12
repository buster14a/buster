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
        // A suppress whose argument is no string literal, which clang's parser drops (#1844): with no GNU attribute left, the
        // statement is a declaration, whose first token is clang's parser's second error; one left keeps it a statement.
        { "malformed suppress before an expression", "int g(int x) {\n__attribute__((suppress(1))) x++;\nreturn x;\n}\n" },
        { "malformed __suppress__ before a call", "int f(int);\nint g(int x) {\n__attribute__((__suppress__(x))) f(x);\nreturn x;\n}\n" },
        { "two malformed suppresses before an increment", "int g(int x) {\n__attribute__((suppress(1), suppress(2))) ++x;\nreturn x;\n}\n" },
        { "malformed suppresses in two lists before a constant", "int g(int x) {\n__attribute__((suppress(1))) __attribute__((suppress(\"a\", 2))) 3;\nreturn x;\n}\n" },
        { "malformed suppress beside a kept one", "typedef int T;\nint g(int x) {\n__attribute__((suppress(\"a\"), suppress(1))) x++;\n"
                                                  "__attribute__((suppress(1))) __attribute__((suppress)) x++;\n__attribute__((suppress(((\"a\"))))) x++;\n"
                                                  "__attribute__((suppress(1))) T y = x;\n__attribute__((suppress(1))) return y;\n}\n" },
        { "malformed suppress after a control head", "int g(int x) {\nif (x) __attribute__((suppress(1))) x++;\nreturn x;\n}\n" },
        // Before a label's name too, the parser's error drops it before Sema's subject error would refuse it on the label
        // (#2332).
        { "malformed suppress before a label's name", "int g(int x) {\n__attribute__((suppress(1))) l: x++;\nreturn x;\n}\n" },
        { "suppress's empty argument before a label's name", "int g(int x) {\n__attribute__((suppress(\"a\",))) l: x++;\nreturn x;\n}\n" },
        // A non-string inside the argument's parentheses (#2332): the string error, then the parser's "expected ')'" at the
        // same token, noted at the innermost `(`; in a statement, a declaration, a label's and a C23 list.
        { "parenthesized non-string in suppress", "int g(int x) {\n__attribute__((suppress((1)))) x++;\nreturn x;\n}\n" },
        { "a name in suppress's double parentheses", "int g(int x) {\n__attribute__((suppress(((x))))) x++;\nreturn x;\n}\n" },
        { "parenthesized non-string in suppress's second argument", "int g(int x) {\n__attribute__((suppress(\"a\", (1)))) x++;\nreturn x;\n}\n" },
        { "parenthesized non-string in suppress before a declaration", "int g(int x) {\n__attribute__((suppress((1)))) int y = x;\nreturn y;\n}\n" },
        { "parenthesized non-string in suppress before a label", "int g(int x) {\n__attribute__((suppress((1)))) l: x++;\nreturn x;\n}\n" },
        { "parenthesized non-string in C23's suppress", "int g(int x) {\n[[clang::suppress((1))]] x++;\nreturn x;\n}\n" },
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
        // object. But a call in a short-circuited operand, a conditional's arm, sizeof, through a pointer of no noreturn
        // type, a declaration alone, a for's increment, and a condition that doesn't fold leave the fallthrough reachable.
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
        // A call through an object whose function type is noreturn (#1844): GNU's noreturn on the pointer's own declaration,
        // a parameter's or a typedef's (through another typedef), called by name or through `*`.
        { "unreachable fallthroughs after calls through noreturn pointers",
          "typedef void (*N)(void) __attribute__((noreturn));\ntypedef N M;\nextern N np;\nextern M mp;\n"
          "extern void (*vp)(void) __attribute__((noreturn));\nint f(int x, int y, N p) {\nswitch (x) {\n"
          "case 1: np(); __attribute__((fallthrough)); x++;\ncase 2: mp(); __attribute__((fallthrough)); x++;\n"
          "case 3: vp(); __attribute__((fallthrough)); x++;\ncase 4: (*vp)(); __attribute__((fallthrough)); x++;\n"
          "case 5: p(); __attribute__((fallthrough)); x++;\ncase 6: return 1;\n}\nreturn 0;\n}\n" },
        // A noreturn function type not spelled by an attribute on the object's declaration or its typedef (#1844): a typeof
        // of a function GNU's attribute marks (a later redeclaration's too) or of `&` it, of a noreturn object, a typedef of
        // such a typeof, an element of an array of a noreturn typedef, and a cast to one.
        { "unreachable fallthroughs after calls through typeofs, elements and casts",
          "void die(void) __attribute__((noreturn));\n[[gnu::noreturn]] void stop(void);\nvoid late(void);\nvoid late(void) __attribute__((noreturn));\n"
          "extern void (*np)(void) __attribute__((noreturn));\ntypedef void (*N)(void) __attribute__((noreturn));\n"
          "__typeof__(die) *fp;\n__typeof__(stop) *sp;\n__typeof__(late) *lp;\n__typeof__(np) tp;\n__typeof__(&die) ap;\ntypedef __typeof__(die) D;\nD *dp;\nN arr[2];\n"
          "int f(int x, void *v) {\nswitch (x) {\n"
          "case 1: fp(); __attribute__((fallthrough)); x++;\ncase 2: sp(); __attribute__((fallthrough)); x++;\n"
          "case 3: lp(); __attribute__((fallthrough)); x++;\ncase 4: tp(); __attribute__((fallthrough)); x++;\n"
          "case 5: ap(); __attribute__((fallthrough)); x++;\ncase 6: dp(); __attribute__((fallthrough)); x++;\n"
          "case 7: arr[x](); __attribute__((fallthrough)); x++;\ncase 8: ((N)v)(); __attribute__((fallthrough)); x++;\n"
          "case 9: (*fp)(); __attribute__((fallthrough)); x++;\ncase 10: return 1;\n}\nreturn 0;\n}\n" },
        // But not a typeof of a function the _Noreturn keyword or C23's [[noreturn]] marks: its declaration's, not its type's
        // (each alone).
        { "a reachable fallthrough after a call through a typeof of a _Noreturn function", "_Noreturn void quit(void);\n__typeof__(quit) *qp;\n"
          "int f(int x) {\nswitch (x) {\ncase 1: qp(); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a call through a typeof of a [[noreturn]] function", "[[noreturn]] void halt(void);\n__typeof__(halt) *hp;\n"
          "int f(int x) {\nswitch (x) {\ncase 1: hp(); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        // clang's CFG builder's tautology checks (#1844): comparisons of one variable with integer literals under && and ||,
        // an operand and its negation, a bitwise operation's equality with a literal it can't give, a boolean's comparison
        // with a literal past 1, a `|` of a nonzero literal, and `& 0` and `* 0`; in an if's condition and a loop's.
        { "unreachable fallthroughs after tautologies",
          "int f(int x, int y, unsigned u, long l) {\nswitch (x) {\n"
          "case 1: if (y > 5 && y < 3) ; else return 1; __attribute__((fallthrough)); x++;\n"
          "case 2: if (y < 5 || y > 3) return 2; __attribute__((fallthrough)); x++;\n"
          "case 3: if (y || !y) return 3; __attribute__((fallthrough)); x++;\n"
          "case 4: if ((y & 4) != 3) return 4; __attribute__((fallthrough)); x++;\n"
          "case 5: if ((y | 8) == 1) ; else return 5; __attribute__((fallthrough)); x++;\n"
          "case 6: if ((y < x) != 10) return 6; __attribute__((fallthrough)); x++;\n"
          "case 7: if (10 > (y == x)) return 7; __attribute__((fallthrough)); x++;\n"
          "case 8: if (y | 2) return 8; __attribute__((fallthrough)); x++;\n"
          "case 9: if (y & 0) ; else return 9; __attribute__((fallthrough)); x++;\n"
          "case 10: if (y * 0) ; else return 10; __attribute__((fallthrough)); x++;\n"
          "case 11: if (u != 1 || u != 2) return 11; __attribute__((fallthrough)); x++;\n"
          "case 12: if (l == 'a' || l != 'a') return 12; __attribute__((fallthrough)); x++;\n"
          "case 13: while ((y > 1 || y < 3) || x) ; __attribute__((fallthrough)); x++;\n"
          "case 14: if (-3 < y || y <= -3) return 14; __attribute__((fallthrough)); x++;\n"
          "case 15: if ((y > 5 && y < 3)) ; else return 15; __attribute__((fallthrough)); x++;\n"
          "case 16: if (0 || ((y & 4) == 3)) ; else return 16; __attribute__((fallthrough)); x++;\n"
          "case 17: for (; (y > 1 || y < 3) || x;) ; __attribute__((fallthrough)); x++;\n"
          "case 18: return 0;\n}\nreturn 0;\n}\n" },
        // The tautology checks' other constants and operands (#1844): floating literals, and integer ones their implicit
        // conversion makes floating, compared in the wider type at clang's APFloat values; enumerators of one enumeration
        // (a C23 fixed underlying type's too); members (`s.m`, `p->m`, `s.in.k` through parentheses) and elements (`a[1]`
        // with `a[1u]`, `a[i]`, an enumerator's index), as Expr::isSameComparisonOperand takes them; a float compared
        // with a float literal and with a double one, whose conversions differ but whose name is the same; _Float16's
        // constants rounded to it (2049 is 2048, and their mean 2048 again).
        { "unreachable fallthroughs after tautologies of other constants and operands",
          "enum E { A = 3, B = 5 };\nstruct In { int k; };\nstruct S { int m; struct In in; int a[4]; double r; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, float g, long double ld, int i, _Float16 h) {\nswitch (x) {\n"
          "case 1: if (d > 5.0 && d < 3.0) ; else return 1; __attribute__((fallthrough)); x++;\n"
          "case 2: if (d < 5.0 || d > 3.0) return 2; __attribute__((fallthrough)); x++;\n"
          "case 3: if (g > 1.0f && g < 0.5f) ; else return 3; __attribute__((fallthrough)); x++;\n"
          "case 4: if (d > 5 && d < -3) ; else return 4; __attribute__((fallthrough)); x++;\n"
          "case 5: if (ld > 5.0L && ld < 3.0L) ; else return 5; __attribute__((fallthrough)); x++;\n"
          "case 6: if (d == 1.0 && d == 2.0) ; else return 6; __attribute__((fallthrough)); x++;\n"
          "case 7: if (y > B && y < A) ; else return 7; __attribute__((fallthrough)); x++;\n"
          "case 8: if ((y) > (B) && y < (A)) ; else return 8; __attribute__((fallthrough)); x++;\n"
          "case 9: if (y != A || y != B) return 9; __attribute__((fallthrough)); x++;\n"
          "case 10: if (s.m > 5 && s.m < 3) ; else return 10; __attribute__((fallthrough)); x++;\n"
          "case 11: if (p->m < 5 || p->m > 3) return 11; __attribute__((fallthrough)); x++;\n"
          "case 12: if (s.in.k > 5 && (s.in).k < 3) ; else return 12; __attribute__((fallthrough)); x++;\n"
          "case 13: if (s.m || !s.m) return 13; __attribute__((fallthrough)); x++;\n"
          "case 14: if (arr[1] > 5 && arr[1] < 3) ; else return 14; __attribute__((fallthrough)); x++;\n"
          "case 15: if (arr[i] > 5 && arr[i] < 3) ; else return 15; __attribute__((fallthrough)); x++;\n"
          "case 16: if (s.a[1] > 5 && s.a[1u] < 3) ; else return 16; __attribute__((fallthrough)); x++;\n"
          "case 17: if (arr[A] > 5 && arr[A] < 3) ; else return 17; __attribute__((fallthrough)); x++;\n"
          "case 18: if (y > (-5) && y < (-9)) ; else return 18; __attribute__((fallthrough)); x++;\n"
          "case 19: if (g > 0.1f && g < 0.1) ; else return 19; __attribute__((fallthrough)); x++;\n"
          "case 20: if (s.r > 1 && s.r < -1) ; else return 20; __attribute__((fallthrough)); x++;\n"
          "case 21: if (d > 1e999 || d <= 1e999) return 21; __attribute__((fallthrough)); x++;\n"
          "case 22: if (y | B) return 22; __attribute__((fallthrough)); x++;\n"
          "case 23: if (h > 1.0f16 && h < 0.5f16) ; else return 23; __attribute__((fallthrough)); x++;\n"
          "case 24: if (h > 2049 && h < 2050) ; else return 24; __attribute__((fallthrough)); x++;\n"
          "case 25: if (h != 65504 || h != 70000) return 25; __attribute__((fallthrough)); x++;\n"
          "case 26: return 0;\n}\nreturn 0;\n}\n" },
        { "unreachable fallthroughs after tautologies of fixed enumerations' enumerators",
          "enum E : long { A = 3, B = 5 };\nenum G : unsigned char { P = 1, Q = 2 };\nint f(int x, long y, enum G g) {\nswitch (x) {\n"
          "case 1: if (y > B && y < A) ; else return 1; __attribute__((fallthrough)); x++;\n"
          "case 2: if (g > Q && g < P) ; else return 2; __attribute__((fallthrough)); x++;\n"
          "case 3: if (x > B && x < A) ; else return 3; __attribute__((fallthrough)); x++;\n"
          "case 4: return 0;\n}\nreturn 0;\n}\n" },
        // But not inside the parentheses of a condition that is no logical operator, nor past `!`, which clang's
        // tryEvaluateBool doesn't look through, nor in a do loop's parenthesized operand, nor across types, nor in
        // __builtin_assume's argument (each alone).
        { "a reachable fallthrough after a parenthesized comparison", "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: y++; if (((y & 4) != 3)) return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a do loop's parenthesized operand", "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: y++; do ; while ((y > 1 || y < 3) || x); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a negated tautology", "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: y++; if (!(y > 5 && y < 3)) return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after literals of two types", "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: y++; if (y > 5 && y < 3L) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after __builtin_assume of a tautology", "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: y++; __builtin_assume(y > 5 && y < 3); __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a satisfiable comparison", "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: y++; if (y > 3 && y < 5) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        // But not a literal its comparison converts otherwise than IntegralCast or IntegralToFloating, or inside
        // parentheses; enumerators of two enumerations, or one and a literal; a member or an element under conversions of
        // different kinds; an index other than a literal or the same operand; a member of an element or a dereference;
        // `-` of an enumerator; nor values that a mean of infinities leaves unknown (each alone).
        { "a reachable fallthrough after a float literal its comparison converts", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (d > 5.0f && d < 3.0f) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a long double compared with double literals", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (ld > 5.0 && ld < 3.0) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after parenthesized literals their comparison converts", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (l > (5) && l < (3)) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after enumerators of two enumerations", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (y > B && y < C) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after an enumerator and a literal", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (y > B && y < 3) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a member's literals of two types", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (s.m > 5 && s.m < 3.0) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after an integer's literal and a floating one", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (y > 5 && y < 3.0) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after an element of a parenthesized index", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (arr[(1)] > 5 && arr[(1)] < 3) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after an element of a computed index", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (arr[i + 1] > 5 && arr[i + 1] < 3) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after an element's member", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (p[2].m > 5 && p[2].m < 3) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a dereference's member", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if ((*p).m > 5 && (*p).m < 3) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a negated enumerator", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (y > -B && y < -A) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after infinities around a value", "enum E { A = 3, B = 5 };\nenum F { C = 3, D = 5 };\nstruct S { int m; } s, *p;\nint arr[4];\n"
          "int f(int x, int y, double d, long double ld, long l, int i) {\nswitch (x) {\n"
          "case 1: if (d > -1e999 && d < 1e999) ; else return 1; __attribute__((fallthrough)); x++;\ncase 2: return 1;\n}\nreturn 0;\n}\n" },
        // A loop whose breaks can't run never ends, as clang's CFG prunes an if's branch its folded condition doesn't take
        // (#1126's review): `if (0) break;`, an else of a true condition, a break of a nested loop or switch, and a body
        // that is an if itself. But a break that can run ends it (each alone, as above).
        { "unreachable fallthroughs after loops whose breaks can't run",
          "int f(int x, int y) {\nswitch (x) {\n"
          "case 1: while (1) { if (0) break; } __attribute__((fallthrough)); x++;\n"
          "case 2: while (1) if (0) break; __attribute__((fallthrough)); x++;\n"
          "case 3: for (;;) { if (1) ; else break; } __attribute__((fallthrough)); x++;\n"
          "case 4: do { if (2 - 2) break; } while (1); __attribute__((fallthrough)); x++;\n"
          "case 5: while (1) { while (y) break; switch (y) { case 1: break; } if (0) { if (y) break; } } __attribute__((fallthrough)); x++;\n"
          "case 6: return x;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a loop's live break", "int f(int x, int y) {\nswitch (x) {\ncase 1: while (1) { if (y) break; } __attribute__((fallthrough)); x++;\ncase 2: return x;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a loop whose if body breaks", "int f(int x, int y) {\nswitch (x) {\ncase 1: while (1) if (y) break; __attribute__((fallthrough)); x++;\ncase 2: return x;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a loop's else break", "int f(int x, int y) {\nswitch (x) {\ncase 1: for (;;) if (y) ; else break; __attribute__((fallthrough)); x++;\ncase 2: return x;\n}\nreturn 0;\n}\n" },
        { "a reachable fallthrough after a false condition's else break", "int f(int x, int y) {\nswitch (x) {\ncase 1: while (1) { if (0) { } else break; } __attribute__((fallthrough)); x++;\ncase 2: return x;\n}\nreturn 0;\n}\n" },
        // One switch's fallthroughs, reachable and dead in turn, some in nested blocks: each resumes its block's scan
        // where the one before stopped (#1357), and only case 7's, reachable and not before a label, is misplaced.
        { "one block's reachable and dead fallthroughs in turn",
          "int f(int x, int y) {\nswitch (x) {\ncase 1: x++; __attribute__((fallthrough));\ncase 2: return 1; __attribute__((fallthrough)); x++;\n"
          "case 3: x++; __attribute__((fallthrough));\ncase 4: { y++; __attribute__((fallthrough)); }\ncase 5: { return 2; } __attribute__((fallthrough)); y++;\n"
          "case 6: x--; break; __attribute__((fallthrough)); x++;\ncase 7: y--; __attribute__((fallthrough)); y++;\ncase 8: y--; __attribute__((fallthrough));\n"
          "default: return x + y;\n}\n}\n" },
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
    // An unavailable member's use in an initializer is clang's error under -w too (#147, check_init_member_use): each use,
    // at its place, with its note, by both paths; and where another declaration's error sends the initializers to the
    // diagnostic walk.
    static const char* const unavailable_texts[] = {
        "struct S { int u __attribute__((unavailable(\"gone\"))); int w; };\nstruct S a = { .u = 1 };\n"
        "struct S b = { 2, 3 };\nvoid f(void) { struct S c = { .w = 1, .u = 2 }; (void)c; }\n",
        "struct S { int u __attribute__((unavailable(\"gone\"))); };\nstruct S a = { .u = 1 };\nvoid *bad = &(int){1} + nope;\n",
    };
    for (uint32_t u = 0; u < BUSTER_ARRAY_LENGTH(unavailable_texts) && parse_test_write(source, unavailable_texts[u], strlen(unavailable_texts[u])); u += 1)
    {
        char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-color-diagnostics", "-w", source, NULL };
        char* reference = NULL;
        const int oracle = check_test_driver_spawn(arguments, &reference);
        char expected[8192];
        const bool expected_valid = check_test_cleanup_errors(reference, 0, expected, sizeof(expected));
        free(reference);
        for (uint32_t scalar = 0; scalar < 2; scalar += 1)
        {
            char* checked[] = { (char*)(executable != NULL ? executable : "/proc/self/exe"), "-std=gnu23", "-fsyntax-only", "-w", source,
                                scalar != 0 ? "--scalar" : NULL, NULL };
            char* text = NULL;
            const int status = check_test_driver_spawn(checked, &text);
            char actual[8192];
            const bool actual_valid = check_test_cleanup_errors(text, 0, actual, sizeof(actual));
            free(text);
            char description[512];
            snprintf(description, sizeof(description), "an unavailable member's initializer uses under -w (%u), by the %s path: clang's (status %d)\n%sagainst (status %d)\n%s",
                     u, scalar != 0 ? "scalar" : "vector", oracle, expected, status, actual);
            check_test_expect(test, oracle == 1 && status == oracle && expected_valid && actual_valid && strcmp(expected, actual) == 0, description);
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

// The lines of a compiler's -Wunknown-attributes diagnostics in `text`, in order, into `lines` (#147), with its warnings
// at `__clang__` where an attribute's scope goes; false where they don't fit.
static bool check_test_unknown_attribute_lines(const char* text, char* lines, size_t capacity)
{
    size_t length = 0;
    bool fits = capacity != 0;
    if (fits)
    {
        lines[0] = 0;
    }
    for (const char* line = text; fits && line != NULL && *line != 0;)
    {
        const char* end = strchr(line, '\n');
        const size_t size = end != NULL ? (size_t)(end - line) : strlen(line);
        const bool kept = memmem(line, size, "unknown attribute", sizeof("unknown attribute") - 1) != NULL ||
                          memmem(line, size, "is a predefined macro name, not an attribute scope specifier",
                                 sizeof("is a predefined macro name, not an attribute scope specifier") - 1) != NULL;
        fits = !kept || size + 1 < capacity - length;
        if (kept && fits)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return fits;
}

// The diagnostic lines (errors, warnings and notes) of a compiler's output `text`, in order, into `lines`; false where they
// don't fit. Search each bounded line alone: rescanning the whole remaining output for an absent marker is quadratic
// in the number of lines (for example, #1443's 200,000-line diagnostic fixture). Find each line's end within the text
// too (memchr): strchr on the rest reads all of it per line under AddressSanitizer's strict string checks (#2030).
static bool check_test_diagnostic_lines(const char* text, char* lines, size_t capacity)
{
    size_t length = 0;
    bool fits = capacity != 0;
    if (fits)
    {
        lines[0] = 0;
    }
    const char* stop = text != NULL ? text + strlen(text) : NULL;
    for (const char* line = text; fits && line != NULL && line < stop;)
    {
        const char* end = memchr(line, '\n', (size_t)(stop - line));
        const size_t size = end != NULL ? (size_t)(end - line) : (size_t)(stop - line);
        const bool kept = memmem(line, size, ": error: ", 9) != NULL || memmem(line, size, ": warning: ", 11) != NULL || memmem(line, size, ": note: ", 8) != NULL;
        fits = !kept || size + 1 < capacity - length;
        if (kept && fits)
        {
            memcpy(lines + length, line, size);
            length += size;
            lines[length++] = '\n';
            lines[length] = 0;
        }
        line = end != NULL ? end + 1 : NULL;
    }
    return fits;
}

// The misspelled attributes of check_test_driver_unknown_attributes, from c_check_attribute_clang.inc's spellings.
static void check_test_driver_attribute_typos(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const scopes[] = { "gnu", "clang", "gnux", "clangg", "x", "msvc", "omp", "ompp", "__gnu__", "_Clang", "gs", "vk", "acc", "hlsll" };
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz_";
    enum { CAPACITY = 1u << 20, LINES = 1u << 20 };
    char* text = malloc(CAPACITY);
    char* kept = malloc(CAPACITY);
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    size_t length = 0;
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (uint32_t round = 0; text != NULL && round < 3; round += 1)
    {
        for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(check_input_clang_spellings); i += 1)
        {
            char name[96];
            uint32_t size = check_input_clang_spellings[i].length;
            memcpy(name, check_input_clang_spellings[i].name, size);
            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            const uint32_t edits = 1 + (uint32_t)((state >> 60) & 1);
            for (uint32_t e = 0; e < edits; e += 1)
            {
                state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                const uint32_t kind = (uint32_t)(state >> 61) % 3;
                const uint32_t at = (uint32_t)((state >> 32) % (size + 1u));
                const char letter = letters[(state >> 16) % (sizeof(letters) - 1)];
                if (kind == 0 && size > 1)
                {
                    const uint32_t place = at < size ? at : size - 1;
                    memmove(name + place, name + place + 1, size - place - 1);
                    size -= 1;
                }
                else if (kind == 1 && size + 1 < sizeof(name))
                {
                    memmove(name + at + 1, name + at, size - at);
                    name[at] = letter;
                    size += 1;
                }
                else if (size > 1)
                {
                    name[at < size ? at : size - 1] = letter;
                }
            }
            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
            const uint32_t form = (uint32_t)(state >> 62);
            const char* scope = scopes[(state >> 20) % BUSTER_ARRAY_LENGTH(scopes)];
            // Only identifiers: OpenMP's `declare simd` and the like are spellings with spaces.
            bool identifier = (name[0] < '0' || name[0] > '9') && length + 256 < CAPACITY;
            for (uint32_t b = 0; b < size; b += 1)
            {
                const char c = name[b];
                identifier &= (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            }
            if (identifier && form == 0)
            {
                length += (size_t)snprintf(text + length, CAPACITY - length, "[[%.*s]] int v%u_%u;\n", (int)size, name, round, i);
            }
            else if (identifier && form == 1)
            {
                length += (size_t)snprintf(text + length, CAPACITY - length, "[[%s::%.*s]] int v%u_%u;\n", scope, (int)size, name, round, i);
            }
            else if (identifier)
            {
                length += (size_t)snprintf(text + length, CAPACITY - length, "int v%u_%u __attribute__((%.*s));\n", round, i, (int)size, name);
            }
        }
    }
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/attribute-typos.c", directory);
    bool made = text != NULL && kept != NULL && expected_lines != NULL && actual_lines != NULL && parse_test_write(source, text, length);
    char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", "-ferror-limit=0", source, NULL, NULL };
    char* first = NULL;
    made = made && check_test_driver_spawn(arguments, &first) >= 0 && first != NULL;
    // The source's lines clang warns about as unknown, in order: those its warnings' line numbers name.
    size_t kept_length = 0;
    uint32_t line = 1;
    uint32_t total = 0;
    const char* cursor = text;
    const char* warning = first;
    for (; made && cursor != NULL && cursor < text + length; line += 1)
    {
        const char* end = memchr(cursor, '\n', (size_t)(text + length - cursor));
        const size_t size = end != NULL ? (size_t)(end - cursor) + 1 : (size_t)(text + length - cursor);
        char marker[64];
        snprintf(marker, sizeof(marker), "attribute-typos.c:%u:", line);
        bool unknown = false;
        for (const char* at = warning != NULL ? strstr(warning, marker) : NULL; !unknown && at != NULL; at = strstr(at + 1, marker))
        {
            const char* eol = strchr(at, '\n');
            const char* found = strstr(at, "unknown attribute");
            unknown = found != NULL && (eol == NULL || found < eol);
        }
        if (unknown)
        {
            memcpy(kept + kept_length, cursor, size);
            kept_length += size;
            total += 1;
        }
        cursor = end != NULL ? end + 1 : NULL;
    }
    made = made && total > 1000 && parse_test_write(source, kept, kept_length);
    char* expected = NULL;
    arguments[0] = (char*)clang;
    made = made && check_test_driver_spawn(arguments, &expected) >= 0 && expected != NULL && check_test_unknown_attribute_lines(expected, expected_lines, LINES);
    uint32_t suggestions = 0;
    for (const char* at = made ? strstr(expected_lines, "did you mean") : NULL; at != NULL; at = strstr(at + 1, "did you mean"))
    {
        suggestions += 1;
    }
    for (uint32_t scalar = 0; made && scalar < 2; scalar += 1)
    {
        arguments[0] = (char*)executable;
        arguments[6] = scalar != 0 ? "--scalar" : NULL;
        char* actual = NULL;
        const int status = check_test_driver_spawn(arguments, &actual);
        const bool same = actual != NULL && check_test_unknown_attribute_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
        size_t at = 0;
        while (!same && expected_lines[at] != 0 && expected_lines[at] == actual_lines[at])
        {
            at += 1;
        }
        char description[512];
        snprintf(description, sizeof(description), "attribute typos, %s: %u lines (%u suggestions), status %d, the same as clang's%s%.160s", scalar != 0 ? "scalar" : "vector",
                 total, suggestions, status, same ? "" : " but from: ", same ? "" : expected_lines + at);
        check_test_expect(test, same && status == 0, description);
        free(actual);
    }
    check_test_expect(test, made && suggestions > 300, "attribute typos: clang's lines read, hundreds of them with suggestions");
    free(first);
    free(expected);
    free(text);
    free(kept);
    free(expected_lines);
    free(actual_lines);
    unlink(source);
}

// Attributes clang doesn't know (#147, c_check_attribute_clang.inc), which it ignores with -Wunknown-attributes' warning
// at the name (a C23 one's at its scope), spelled as written in GNU's syntax and normalized in C23's: invented names,
// GCC's own (externally_visible, optimize, access), an unknown scope's and gnu::'s and clang::'s unknown names, C23's
// unsequenced, unscoped GNU names in [[...]], on declarations, members, labels and statements (an expression's and a
// labeled one's C23 attributes, which the parser reads as the statement's), beside known ones. Their
// lines must be clang's, line for line, by both paths: with no option, with -Wno-unknown-attributes (none), with
// -Werror=unknown-attributes (errors, and a failed compile) and with -w (none, and a compile clang's too).
static void check_test_driver_unknown_attributes(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char source_text[] =
        "int a __attribute__((invented_semantics(3)));\n"
        "int b __attribute__((__invented__, aligned(8)));\n"
        "[[invented::aligned(16)]] int c;\n"
        "[[aligned(4)]] int d;\n"
        "[[gnu::invented]] int e;\n"
        "[[__gnu__::__invented__]] int f;\n"
        "[[clang::invented]] int g;\n"
        "[[_Clang::invented, deprecated]] int h;\n"
        "int k __attribute__((externally_visible));\n"
        "int m __attribute__((optimize(\"O2\")));\n"
        "void n(int *p) __attribute__((access(read_only, 1)));\n"
        "[[unsequenced]] int o(void);\n"
        "struct S { int x __attribute__((invented)); [[gnu::invented2]] int y; } s;\n"
        "int t1 __attribute__((alignd(8)));\n"
        "int t2 __attribute__((no_sanitize_undefined, unsed, __visiblity__(\"default\")));\n"
        "[[gnux::__alignd__(8)]] int t3;\n"
        "[[deprecatd, maybe_unusd]] int t4;\n"
        "[[clang::nodref, clangg::noderef]] int *t5;\n"
        "[[ompp::x, omp::x, x::__y__, msvc::__y__]] int t6;\n"
        "int t7 __attribute__((____devicew___));\n"
        "[[__clang__::x]] int t8;\n"
        "[[__clang__::__aligned__(4), deprecated]] int t10;\n"
        "[[__clang__]] int t9;\n"
        "int q(int x) {\n"
        "    switch (x) { case 1: __attribute__((invented)); case 2: break; }\n"
        "l: __attribute__((invented, unused));\n"
        "    [[invented]] x += 1;\n"
        "    [[invented]] m: x -= 1;\n"
        "    if (x) goto m;\n"
        "    [[gnu::invented]] (void)x;\n"
        "    return x;\n"
        "}\n";
    static const struct
    {
        const char* option;
        bool failed;
    } configurations[] = {
        { NULL, false },
        { "-Wno-unknown-attributes", false },
        { "-Werror=unknown-attributes", true },
        { "-w", false },
    };
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/unknown-attributes.c", directory);
    const bool written = parse_test_write(source, source_text, sizeof(source_text) - 1);
    check_test_expect(test, written, "unknown attributes: fixture written");
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    for (uint32_t c = 0; written && expected_lines != NULL && actual_lines != NULL && c < BUSTER_ARRAY_LENGTH(configurations); c += 1)
    {
        char* arguments[10] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", source };
        uint32_t count = 5;
        if (configurations[c].option != NULL)
        {
            arguments[count++] = (char*)configurations[c].option;
        }
        arguments[count] = NULL;
        char* expected = NULL;
        const int oracle = check_test_driver_spawn(arguments, &expected);
        const bool lines = expected != NULL && check_test_unknown_attribute_lines(expected, expected_lines, LINES);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[count] = scalar != 0 ? "--scalar" : NULL;
            arguments[count + 1] = NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool same = actual != NULL && check_test_unknown_attribute_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            char description[2048];
            snprintf(description, sizeof(description), "unknown attributes %s, %s: status %d (clang %d), lines\n%s(clang\n%s)",
                     configurations[c].option != NULL ? configurations[c].option : "(no option)", scalar != 0 ? "scalar" : "vector", status, oracle,
                     actual_lines, expected_lines);
            // -Wno-unknown-attributes leaves clang's -Wignored-attributes warning at `__clang__`; -w leaves nothing.
            check_test_expect(test, same && (status != 0) == configurations[c].failed && (oracle != 0) == configurations[c].failed &&
                                        (expected_lines[0] != 0) == (c != 3) && (c != 1 || strstr(expected_lines, "unknown attribute") == NULL),
                              description);
            free(actual);
        }
        free(expected);
    }
    // A statement's attributes refused, named as clang names them (a C23 one normalized, at its first token; a GNU one as
    // written), and those before a label's name, which are the label's; labels redefined, with clang's note at the first
    // definition (a local label's at its declaration). The whole output, line for line, by both paths.
    unlink(source);
    static const char* const statements[] = {
        // Parser macro warnings precede semantic attribute checks; every declarator processes its shared list after
        // its parameters. Compare notes too, including the predefined macro's exact line and explicit redefinitions.
        "[[__clang__::x, __clang__::y]] int a;\n",
        "[[x, __clang__]] int a;\n",
        "[[x, __clang__::y, __clang__::z]] int a;\n",
        "[[__clang__::noinline(1)]] void f(void);\n",
        "[[__clang__::x]] int a [[__clang__::y]], b [[__clang__::z]];\n",
        "[[__clang__::x]] void f(int a [[__clang__::y]]);\n",
        "void f(int p [[__clang__::unused]]) __attribute__((stdcall));\n",
        "#undef __clang__\n#define __clang__ 7\n[[__clang__::unused]] int a;\n",
        "#define WRAP __clang__\n[[WRAP::unused]] int a;\n",
        // A failed attribute-name parse consumes the first closing bracket and diagnoses the second at file scope.
        "[[123]] int a;\n",
        "[[, 123::unused]] int a;\n",
        "#define WRAP __clang__\n[[WRAP(1, (2))::unused]] int a;\n",
        "[[gnu::annotate(\"tag\", __clang__)]] int a;\nint values[] = {1, __clang__};\n",
        "void f(int p [[gnu::cold]]) __attribute__((stdcall));\n",
        "__attribute__((stdcall)) void f(int a __attribute__((cold)));\n",
        "void (__attribute__((stdcall)) f)(int a __attribute__((cold)));\n",
        "__attribute__((stdcall)) void (*f)(int a __attribute__((cold)));\n",
        "void f(void) [[gnu::stdcall]];\n",
        "typedef void F(void) [[gnu::stdcall]];\n",
        "int (*f)(void) [[gnu::stdcall]];\n",
        "void f(void) [[gnu::stdcall, gnu::cold, x]];\n",
        "void [[gnu::stdcall]] f(void);\nvoid [[gnu::regparm(2)]] h(void);\n",
        "void [[gnu::regparm(1)]] ordered(int x __attribute__((noinline)));\n",
        "void [[gnu::ms_abi]] ordered(int x [[gnu::unknown]]);\n",
        "[[gnu::unknown]] void f(int p [[gnu::unknown]]), g(int q [[gnu::unknown]]);\n",
        "[[gnu::unknown]] int a, b; [[unknown]] int c, d; __attribute__((unknown)) int e, f;\n",
        "[[__gnu__::unknown]] int a, b [[gnu::unknown]]; [[vendor::unknown]] int c, d;\n",
        "typedef void F(void);\nF [[gnu::stdcall]] k;\n",
        "void [[gnu::ms_abi]] f(void);\nvoid f(void);\n",
        // C23's specifier attribute changes the typedef's function, before an outer declarator adds a different one.
        "typedef int (*F)(int); typedef int (__attribute__((ms_abi)) *M)(int);\n"
        "typedef M (*Outer)(void); typedef F (__attribute__((ms_abi)) *Wrong)(void);\n"
        "F [[gnu::ms_abi]] x, (*y)(void);\n"
        "_Static_assert(__builtin_types_compatible_p(__typeof__(x),M),\"base\");\n"
        "_Static_assert(__builtin_types_compatible_p(__typeof__(y),Outer),\"inner\");\n"
        "_Static_assert(!__builtin_types_compatible_p(__typeof__(y),Wrong),\"outer\");\n",
        "typedef int (*F)(int); typedef int (__attribute__((ms_abi)) *M)(int);\n"
        "typedef M (__attribute__((sysv_abi)) *Outer)(void);\n"
        "F [[gnu::ms_abi]] __attribute__((sysv_abi)) (*y)(void);\n"
        "_Static_assert(__builtin_types_compatible_p(__typeof__(y),Outer),\"mixed conventions\");\n",
        // A pointer/array typedef permits replacing its pointee's convention; distinct conventions in the same
        // specifier list, or a different one on a function typedef, remain conflicting.
        "typedef int (__attribute__((sysv_abi)) *F[2])(int); typedef int (__attribute__((ms_abi)) *M[2])(int);\n"
        "F [[gnu::ms_abi]] a; _Static_assert(__builtin_types_compatible_p(__typeof__(a),M),\"replace\");\n",
        "typedef int (*F)(int); F [[gnu::ms_abi, gnu::sysv_abi]] x;\n",
        "typedef int F(int) __attribute__((sysv_abi)); F [[gnu::ms_abi]] x;\n",
        "typedef int (*F)(int); F [[gnu::ms_abi]] __attribute__((sysv_abi)) x;\n",
        "typedef int (*F)(int); __attribute__((ms_abi)) F [[gnu::sysv_abi]] x;\n",
        "typedef int F(int); __attribute__((reqd_work_group_size(1,1,1))) F [[gnu::ms_abi]] x;\n",
        "typedef int F(int); __attribute__((amdgpu_num_sgpr(1))) F [[gnu::ms_abi]] x;\n",
        // The type at an attribute retains this declarator's groups, including redundant ones and partial chunk types.
        // Typedefs keep the groups in the sugared spelling and discard them in the canonical aka spelling.
        "int (*p1) __attribute__((stdcall));\nint (*(p2)) __attribute__((stdcall));\nint *(p3) __attribute__((stdcall));\n"
        "int ((p4)) __attribute__((stdcall));\nint (**p5) __attribute__((stdcall));\nint *(*p6) __attribute__((stdcall));\n"
        "int (*p7[3]) __attribute__((stdcall));\nint (*(p8[3])) __attribute__((stdcall));\nint (*p9)[3] __attribute__((stdcall));\n"
        "typedef int I; I (*p10) __attribute__((stdcall));\ntypedef int *P; P (p11) __attribute__((stdcall));\n"
        "int ((*p12)) __attribute__((stdcall));\nint (* const p13) __attribute__((stdcall));\n",
        "int (* __attribute__((stdcall)) p14);\nint (__attribute__((stdcall)) *p15);\n"
        "int __attribute__((stdcall)) (*p16);\nint (__attribute__((stdcall)) *p17) __attribute__((fastcall));\n",
        "void f(int (*p) __attribute__((stdcall)), int *q __attribute__((fastcall)));\n"
        "int (*a) __attribute__((stdcall)), *b __attribute__((fastcall));\n"
        "int s=sizeof(int (* __attribute__((stdcall))));\n",
        "void (__attribute__((stdcall)) *p)(int q __attribute__((cold))) [[gnu::fastcall]];\n",
        "[[__clang__::x]] void (__attribute__((stdcall)) *p)(int q [[__clang__::y]]) [[gnu::fastcall]];\n",
        "int f(int x) {\n    [[gnu::aligned(4)]] x += 1;\n    return x;\n}\n",
        "int f(int x) {\n    [[__gnu__::__fallthrough__]] x += 1;\n    return x;\n}\n",
        "int f(int x) {\n    [[__deprecated__]] x += 1;\n    return x;\n}\n",
        "int f(int x) {\n    __attribute__((__fallthrough__)) x += 1;\n    return x;\n}\n",
        "int f(int x) {\n    [[_Clang::__musttail__]] x += 1;\n    return x;\n}\n",
        "int f(int x) {\n    __attribute__((nomerge)) a: return x;\n}\n",
        "int f(int x) {\n    [[invented]] a: x++;\n    if (x < 3) goto a;\n    [[gnu::unused]] b: return x;\n}\n",
        "int f(int x) {\n    [[invented]] a: x++;\n    [[invented]] a: return x;\n}\n",
        "int f(int x) {\n    a: x++;\n    a: return x;\n}\n",
        "int f(int x) {\n    {\n        __label__ a;\n    a: x++;\n    a: x--;\n    }\n    return x;\n}\n",
        // Attributes on declarations of none of their subjects (check_input_attribute_subjects): clang's warning, which
        // keeps nothing of them, on variables, functions, parameters, members, bit-fields, typedefs, tags, enumerators and
        // labels, by GNU's spellings and C23's; and its errors, one a unit.
        "int a __attribute__((__cold__));\n[[gnu::cold]] int b;\n[[__gnu__::__cold__]] int c;\nint d __attribute__((cold, hot));\n"
        "void f(int p __attribute__((cold)));\nstruct S { int m __attribute__((cold)); int bf : 3 __attribute__((cold)); };\n"
        "typedef int T __attribute__((cold));\n"
        "void g(void) { int l __attribute__((cold)); static int s __attribute__((used)); int u __attribute__((used)); }\n"
        "void (*fp)(const char *, ...) __attribute__((format(printf, 1, 2)));\nint q __attribute__((nothrow));\n"
        "void (*r)(void) __attribute__((nothrow));\nstruct __attribute__((cold)) U { int x; };\nenum __attribute__((flag_enum)) E { E0 = 1 };\n"
        "struct __attribute__((flag_enum)) V { int x; };\nint w __attribute__((cleanup(g)));\n"
        "void h(void) { static int sv __attribute__((cleanup(g))); }\nint nn __attribute__((nonnull));\nvoid pn(int *p __attribute__((nonnull)));\n"
        "int ar __attribute__((artificial));\ninline void ia(void) __attribute__((artificial));\nvoid ni(void) __attribute__((artificial));\n",
        "void f(int a __attribute__((used)), int *b __attribute__((nonnull)), void (*c)(void) __attribute__((nothrow)), int d __attribute__((nothrow)));\n"
        "typedef void (*FP)(const char *, ...) __attribute__((format(printf, 1, 2)));\n"
        "struct W { void (*fn)(void) __attribute__((nothrow)); int z __attribute__((nothrow)); };\n"
        "int __thread tl __attribute__((tls_model(\"local-exec\")));\nvoid lab(void) { l: __attribute__((cold)); }\n"
        "void lab2(void) { [[gnu::cold]] l2: ; }\nenum F { F0 __attribute__((cold)), F1 };\nint g1 __attribute__((weak));\n"
        "void g3(void) __attribute__((weak));\nint g4 __attribute__((no_instrument_function));\nint g5 __attribute__((unused));\n"
        "void g6(void) { int l __attribute__((unused)); }\nstruct __attribute__((unused)) X { int a; };\nint g7 __attribute__((mode(SI)));\n"
        "void g8(void) __attribute__((nodebug));\nvoid g9(int p __attribute__((nodebug)));\n",
        "int e __attribute__((error(\"x\")));\n",
        "int __attribute__((tls_model(\"initial-exec\"))) tm;\n",
        "void f(void) { int l __attribute__((section(\".l\"))); }\n",
        "void f(void) __attribute__((mode(SI)));\n",
        "enum G { G0 __attribute__((mode(SI))) };\n",
        // Function types' attributes on other types, at each declarator a specifier's reaches; the handlers' own subjects
        // (sentinel's, transparent_union's) and nonstring's types, at the declaration's start.
        // Grouping around qualified pointers and arrays must be balanced in the diagnostic type spelling.
        "int * const ((x)[1])[3] __attribute__((ms_abi));\n"
        "int (* const ((y)[4]))[2] __attribute__((ms_abi));\n"
        "int * volatile ((z)[2])[5] __attribute__((ms_abi));\n"
        "int (* restrict ((w)[2]))[3] __attribute__((ms_abi));\n",
        "int a __attribute__((stdcall));\nint *b __attribute__((ms_abi));\nint c __attribute__((noreturn));\nint d __attribute__((regparm(2)));\n"
        "typedef int T; T e __attribute__((sysv_abi));\nstruct S { int m __attribute__((preserve_most)); };\nvoid f(int p __attribute__((vectorcall)));\n"
        "int __attribute__((cdecl)) i;\n[[gnu::stdcall]] int j;\n[[gnu::__stdcall__]] int j2;\nint __attribute__((stdcall)) e1, e2;\n"
        "int __attribute__((cold)) a1, a2, f1(void);\n[[gnu::cold]] int c1, c2;\n",
        "void (*sp)(int, ...) __attribute__((sentinel));\nstruct SS { void (*sf)(int, ...) __attribute__((sentinel)); };\n"
        "typedef void (*ST)(int, ...) __attribute__((sentinel));\nvoid sv(int a, ...) __attribute__((sentinel));\n"
        "void sq(void (*p)(int, ...) __attribute__((sentinel)));\nint g17 __attribute__((sentinel));\n"
        "int g __attribute__((transparent_union));\ntypedef int TI __attribute__((transparent_union));\n[[gnu::transparent_union]] int tj;\n"
        "__attribute__((transparent_union)) int t1, t2;\n",
        "struct T2 { int m __attribute__((nonstring)); [[gnu::nonstring]] int n; __attribute__((nonstring)) int o; int b : 3 __attribute__((nonstring)); };\n"
        "char g33[2] __attribute__((nonstring));\nstatic int g34 __attribute__((nonstring));\n"
        "void f(int p __attribute__((nonstring)), char q[2] __attribute__((nonstring)), int r[2] __attribute__((nonstring)), [[gnu::nonstring]] int s);\n"
        "const char *g35 __attribute__((nonstring));\nchar **g36 __attribute__((nonstring));\nsigned char g37[3][2] __attribute__((nonstring));\n"
        "typedef char TC[4]; TC g38 __attribute__((nonstring));\ntypedef int TN; TN g39 __attribute__((nonstring));\n"
        "int __attribute__((nonstring)) g40, g41;\n__extension__ int g42 __attribute__((nonstring));\n[[gnu::nonstring]] [[gnu::unused]] static int g43;\n"
        "int __attribute__((__nonstring__)) g44;\n[[__gnu__::__nonstring__]] int g45;\n_Bool g46[2] __attribute__((nonstring));\n"
        "void g(void) { register int l __attribute__((nonstring)); extern int x __attribute__((nonstring)); char c[2] __attribute__((nonstring)); }\n",
        // transparent_union's own warnings: an empty union, a floating or vector first field, a field of another size or a
        // greater alignment (a smaller one is fine, and so is a call through it), a flexible array member, an incomplete union.
        "union __attribute__((transparent_union)) E { };\nunion __attribute__((transparent_union)) F { float x; int y; };\n"
        "union __attribute__((transparent_union)) G { int x; long y; };\nunion I;\ntypedef union I TI __attribute__((transparent_union));\n"
        "union J { char c[4]; int y; } __attribute__((transparent_union));\ntypedef union { int a; double d; } TK __attribute__((transparent_union));\n"
        "typedef float FL;\nunion __attribute__((transparent_union)) L { FL x; int y; };\n"
        "union __attribute__((transparent_union)) M { int __attribute__((vector_size(16))) v; int y; };\n"
        "union __attribute__((transparent_union)) N { int x; char c[]; };\nunion P { int a; long b; };\n"
        "typedef union P TP __attribute__((transparent_union));\nunion [[gnu::transparent_union]] Q { };\n"
        "typedef union { int *a; char b[8]; } U __attribute__((transparent_union));\nvoid take(U u);\nvoid give(int *p) { take(p); }\n",
        // GNU attributes GCC doesn't take after a function definition's declarator (-Wgcc-compat); nonnull's indices and
        // subjects (its pointer warnings, a variadic function's, a parameter's), and an index of no parameter, an error.
        "void f(void) __attribute__((cold)) {}\nvoid g(void) __attribute__((noinline, used)) {}\nint h(int x) __attribute__((const)) { return x; }\n"
        "__attribute__((cold)) void i(void) {}\nvoid k(void) __attribute__((__noinline__)) { }\nvoid m(void) [[gnu::cold]] {}\n"
        "void n(void) __attribute__((invented)) {}\nstatic void q(void) __attribute__((unused, always_inline)) {}\n",
        "int a(int x) __attribute__((nonnull));\nint b(int *x, int y) __attribute__((nonnull(2)));\nint c(int *x, int y) __attribute__((nonnull(1)));\n"
        "int e(void) __attribute__((nonnull));\nint f(int x, ...) __attribute__((nonnull));\nint g(int x, ...) __attribute__((nonnull(2)));\n"
        "int h(int (*fp)(void)) __attribute__((nonnull));\nvoid i(int *p __attribute__((nonnull)), int q __attribute__((nonnull)));\n"
        "void i2(int *p __attribute__((nonnull(1))));\nint j(int x) __attribute__((nonnull(1, 1)));\nstruct S { int a; };\n"
        "int k(struct S s) __attribute__((nonnull(1)));\nint l(char x[]) __attribute__((nonnull(1)));\n",
        "int f(char *) __attribute__((nonnull(2)));\n",
        "int m(void *p) __attribute__((nonnull(0)));\n",
        // C23 attributes in a type's place (check_input_standard_place): a gnu:: declaration attribute after a declarator
        // chunk, ignored with clang's warning (a list after another where that one is), and a type attribute there, which GCC
        // doesn't take; noderef in this syntax, and vector_size after the specifiers, ignored.
        "int * [[gnu::unused]] p1;\nint a1[2] [[gnu::unused]];\nvoid m1(void) [[gnu::cold]];\nint (*fp1)(void) [[gnu::unused]];\n"
        "void f3(int * [[gnu::unused]] a);\nint * [[gnu::aligned(8)]] p2;\nint * [[gnu::invented]] p3;\nint x4 [[clang::noderef]];\n"
        "int * [[clang::noderef]] p5;\n[[clang::noderef]] int x5;\nvoid f5(void) [[gnu::cold]] [[gnu::unused]];\n"
        "int * [[gnu::unused]] [[gnu::cold]] p9;\nvoid f6(void) { int * [[gnu::unused]] q = 0; (void)q; }\nvoid m3(int) [[gnu::nonnull]];\n"
        "int [[gnu::vector_size(16)]] v1;\nint * [[gnu::regparm(2)]] p10;\nvoid m4(void) [[gnu::ms_abi]];\nint * [[clang::invented]] p11;\n",
        "int [[gnu::cold]] x2;\n",
        "int * [[deprecated]] p1;\n",
        "void f1(void) [[deprecated]];\n",
        "struct S1 { int a; } [[gnu::unused]] s1;\n",
        "void f2(int [[gnu::unused]] a);\n",
        "int [[gnu::nocf_check]] x5;\n",
        "int * const [[gnu::unused]] p6 = 0;\n",
        "int * restrict [[gnu::unused]] p8 = 0;\n",
        "int x4 __attribute__((unused)) [[gnu::unused]];\n",
        "void f7(void) __attribute__((cold)) [[gnu::cold]];\n",
        // C23 lists between a declarator's name and its suffixes, and between suffixes, which C23's direct-declarator allows.
        "int x1 [[gnu::unused]] [2];\nint x2[2] [[gnu::unused]] [3];\nvoid (*fp1 [[gnu::unused]])(void);\nvoid f1 [[gnu::cold]] (void) {}\n"
        "void f2(int a [[gnu::unused]] [2]);\nvoid f3 [[gnu::cold]] (void), f4 [[gnu::cold]] (int);\n"
        "void f6(void) { int y [[gnu::unused]] [2]; (void)y; }\nstruct S { int m [[gnu::unused]] [2]; };\nint x5 [[gnu::unused]] [[gnu::unused]] [2];\n"
        "int (*fp2 [[gnu::unused]]) [2];\nint main(void) { f1(); return x1[0] + x2[1][2] + x5[1]; }\n",
        // Calling conventions x86-64 hasn't: clang's warning where one meets a function (once for a specifier's), and none on
        // another type; two of them, or one with cdecl, are no conflict.
        "void __stdcall k1(void);\nvoid __fastcall k2(void), k3(void);\nvoid __attribute__((thiscall)) g1(void);\n"
        "void __attribute__((__stdcall__)) g4(void);\nvoid (__stdcall *fp1)(void);\nvoid (* __attribute__((stdcall)) fp2)(void);\n"
        "typedef void __attribute__((stdcall)) T1(void);\nvoid g5(void) __attribute__((stdcall, regparm(1)));\nvoid __attribute__((stdcall)) g9(void) {}\n"
        "void m4(void) [[gnu::stdcall]];\nvoid (* [[gnu::stdcall]] fp3)(void);\nint * [[gnu::stdcall]] p1;\nvoid f2(void (__stdcall *cb)(void));\n"
        "void f4(void) { void (__stdcall *q)(void) = 0; (void)q; }\nint __cdecl __vectorcall x9;\nvoid __stdcall __stdcall f7(void);\n"
        "void __stdcall __cdecl f8(void);\n[[gnu::stdcall]] void m2(void);\nvoid __pascal k4(void);\nint __attribute__((stdcall)) x1;\n"
        "struct S1 { void (__stdcall *m)(void); };\nvoid __attribute__((stdcall)) __attribute__((stdcall)) f9(void);\n",
        // Conflicting conventions on a function, by clang's names, the one x86-64 hasn't as cdecl.
        "void __cdecl __vectorcall f1(void);\n",
        "void __attribute__((stdcall)) __attribute__((ms_abi)) f2(void);\n",
        "void __attribute__((ms_abi)) __attribute__((stdcall)) f3(void);\n",
        "void __stdcall __vectorcall f6(void);\n",
        "void __attribute__((ms_abi, vectorcall)) f(void);\n",
        "void __vectorcall f3(void) __attribute__((ms_abi));\n",
        "void __attribute__((preserve_most)) __attribute__((preserve_all)) f4(void);\n",
        "typedef void __attribute__((ms_abi)) T(void);\nT __attribute__((sysv_abi)) f6;\n",
        "void (__vectorcall __cdecl *fp)(void);\n",
        // A tag declaration's attributes (check_input_tag_attributes): those of earlier declarations reach the definition's
        // layout; after a reference's name they are its declarators'; after a bare declaration's name ignored with clang's
        // warning; after the definition warned of unless it has them; an enum reference's ignored; and C23 lists on a
        // reference, an error.
        "struct A; struct __attribute__((aligned(16))) A *pa; struct A { int a; };\n_Static_assert(__alignof__(struct A) == 16, \"A\");\n"
        "struct __attribute__((packed)) B; struct B { char c; int i; };\n_Static_assert(sizeof(struct B) == 5, \"B\");\n"
        "struct __attribute__((aligned(32))) C *pc; struct __attribute__((aligned(8))) C { int a; };\n_Static_assert(__alignof__(struct C) == 32, \"C\");\n"
        "union __attribute__((aligned(16))) U; union U { int a; };\n_Static_assert(__alignof__(union U) == 16, \"U\");\n"
        "enum __attribute__((packed)) E; enum E { E0 };\n_Static_assert(sizeof(enum E) == 1, \"E\");\n"
        "struct __attribute__((aligned(16), packed)) G; struct G { char c; int i; };\n_Static_assert(sizeof(struct G) == 16, \"G\");\n"
        "struct S { int a; };\nstruct S __attribute__((aligned(16))) v2;\n_Static_assert(__alignof__(v2) == 16, \"v2\");\n"
        "struct S __attribute__((cold, aligned(32))) v6, v7[2];\n_Static_assert(__alignof__(v7) == 32, \"v7\");\n"
        "struct __attribute__((aligned(16), packed)) S v1;\nstruct __attribute__((cold)) S v4;\nstruct __attribute__((unused)) S v10;\n"
        "struct S __attribute__((__cold__, aligned(8)));\nstruct __attribute__((aligned(16))) S;\nvoid f(struct __attribute__((aligned(16))) S *p);\n"
        "union __attribute__((transparent_union)) U2;\nunion __attribute__((transparent_union)) U3 *pu;\nenum E2 { A2 };\n"
        "enum __attribute__((aligned(16))) E2 e2;\nenum __attribute__((cold)) E2 e3;\nenum __attribute__((aligned(16))) F2 : int;\n"
        "struct T2; struct __attribute__((aligned(8))) T2; struct T2 { int a; }; struct __attribute__((aligned(16))) T2 t2;\n",
        "struct S { int a; };\nstruct [[gnu::aligned(16)]] S v9;\n",
        "struct S { int a; };\nstruct S [[gnu::cold]] v;\n",
        // A GNU declaration attribute in a type name is ignored with clang's warning, mode's too (sizeof stays 4), once for
        // a cast in a constant; a function type's on another type warns as a declaration's does.
        "int s1 = sizeof(int __attribute__((aligned(16))));\nint s2 = sizeof(__attribute__((aligned(16))) int);\n"
        "int s3 = sizeof(int __attribute__((cold, __aligned__(8))));\n_Static_assert(sizeof(int __attribute__((mode(DI)))) == 4, \"m\");\n"
        "_Static_assert(sizeof((int __attribute__((mode(QI))))300) == 4, \"c\");\ntypedef typeof(int __attribute__((aligned(16)))) T1;\n"
        "int g(void) { return (int)sizeof(char __attribute__((packed))) + _Generic(1, int __attribute__((cold)): 1, default: 0); }\n"
        "int h(void) { return (int __attribute__((unused))){5} + (int)sizeof(int * __attribute__((aligned(16)))); }\n"
        "int k = sizeof(int __attribute__((stdcall)));\nint m = sizeof(int __attribute__((__vector_size__(8))));\n",
        // vector_size's checks in clang's order, each an error of its own.
        "struct S { int a; };\nstruct S v1 __attribute__((vector_size(16)));\n",
        "struct S { int a; };\ntypedef struct S TS;\nTS __attribute__((vector_size(16))) v2;\n",
        "int * __attribute__((vector_size(16))) v;\n",
        "typedef int *P;\nP __attribute__((vector_size(16))) v;\n",
        "_Bool __attribute__((vector_size(16))) v;\n",
        "_Complex float __attribute__((vector_size(16))) v;\n",
        "enum E { A };\nenum E __attribute__((vector_size(16))) v;\n",
        "union U { int a; };\nunion U __attribute__((vector_size(16))) v;\n",
        "int __attribute__((vector_size(16), vector_size(32))) v;\n",
        "_BitInt(7) __attribute__((vector_size(16))) v;\n",
        "int __attribute__((vector_size(0))) v;\n",
        "int __attribute__((vector_size(-4))) v;\n",
        "int __attribute__((vector_size(1ULL << 40))) v;\n",
        "int __attribute__((vector_size(3))) v;\n",
        "int __attribute__((vector_size(16.0))) v;\n",
        "int __attribute__((vector_size(\"x\"))) v;\n",
        "struct S { int a; };\nstruct S __attribute__((vector_size(16.0))) v;\n",
        "int * [[gnu::vector_size(16)]] v;\n",
        "short __attribute__((vector_size(6))) a;\nint __attribute__((vector_size(12))) b;\n_BitInt(8) __attribute__((vector_size(16))) c;\n",
        // _Alignof and alignof of an expression, a GNU extension (check_input_alignof_expression), once whichever evaluation
        // reaches them; __alignof__ of one, and either of a type, without a word.
        "int v;\nint a = __alignof__(v);\nint b = _Alignof(v);\nint c = alignof(v);\n_Static_assert(_Alignof(v) == 4, \"\");\n"
        "int f(void) { int x; return _Alignof(x) + alignof(int) + _Alignof(v + 1); }\nint arr[_Alignof(v)];\nenum { E = alignof(v) };\n",
        // A declaration attribute after its entity's definition that the definition lacks: clang's warning, and it's
        // ignored, an alignment's too (check_input_attribute_after_kind); one the definition has, a tentative
        // definition's, and C23's deprecated, without a word.
        "int v = 1;\nextern int v __attribute__((aligned(16)));\n_Static_assert(__alignof__(v) == 4, \"v\");\nvoid f(void) {}\n"
        "void f(void) __attribute__((cold));\nint w __attribute__((aligned(8))) = 1;\nextern int w __attribute__((aligned(16)));\nint u = 1;\n"
        "extern int u [[maybe_unused]];\nextern int u __attribute__((unused));\nvoid a(void) __attribute__((cold));\nvoid a(void) {}\n"
        "void a(void) __attribute__((cold));\nint t;\nextern int t __attribute__((aligned(16)));\nextern int u [[deprecated]];\n"
        "extern int u [[gnu::deprecated]];\nstatic int s = 1;\nstatic int s __attribute__((section(\".x\")));\n"
        "int g(int *p) { return *p; }\nint g(int *p) __attribute__((nonnull));\nvoid h(void) {}\nvoid h(void) __attribute__((weak, cold));\n"
        "int y = 2;\nextern int y __attribute__((visibility(\"hidden\")));\n",
        // Attributes the checker has no rule for, as clang takes them (check_input_attribute_generic): another language's
        // ignored with its warning, one clang ignores everywhere without a word, one its generated handler applies kept for
        // its subject's check, and those whose handlers' checks are made here (noescape's, optnone's over always_inline
        // and minsize); their argument counts, and supported ones', are clang's errors, each in a unit of its own.
        "struct __attribute__((trivial_abi)) S { int a; };\nvoid f(void) __attribute__((device));\nint x __attribute__((shared));\n"
        "void g(void) __attribute__((called_once(1)));\nint z __attribute__((bounded));\nvoid h(void) __attribute__((nouwtable));\n"
        "int w __attribute__((nouwtable));\nvoid k(void) __attribute__((disable_tail_calls, xray_never_instrument));\n"
        "void n(void) __attribute__((minsize));\nvoid p(int *q __attribute__((noescape)), int r __attribute__((noescape)));\n"
        "void a1(void) __attribute__((optnone));\nvoid a2(void) __attribute__((optnone, always_inline));\n"
        "void a3(void) __attribute__((minsize, optnone));\nint c1 __attribute__((annotate(\"x\")));\nint c2 __attribute__((annotate(\"x\", 1, 2)));\n"
        "void d1(void) { int v __attribute__((uninitialized)); (void)v; }\nint d2 __attribute__((uninitialized));\n"
        "struct __attribute__((gcc_struct)) S1 { int a; };\nvoid e1(void) __attribute__((no_sanitize_memory));\n"
        "void g1(void) __attribute__((analyzer_noreturn));\nvoid i1(void) __attribute__((optnone)) {}\nvoid j(void) [[clang::nouwtable]];\n",
        "void m(void) __attribute__((nouwtable(1)));\n",
        "int v __attribute__((minsize));\n",
        "int c3 __attribute__((annotate(1)));\n",
        "int c4 __attribute__((annotate));\n",
        "int e2 __attribute__((no_sanitize_memory));\n",
        "int j1 __attribute__((carries_dependency));\n",
        "void a(void) __attribute__((cold(1)));\n",
        "int b __attribute__((aligned(1, 2)));\n",
        "int c __attribute__((section));\n",
        "void d(const char *f, ...) __attribute__((format(printf)));\n",
        "void e(void) __attribute__((noreturn(1)));\n",
        "int g __attribute__((visibility));\n",
        "void i(void) __attribute__((constructor(1, 2)));\n",
        "int k __attribute__((deprecated(\"a\", \"b\", \"c\")));\n",
        "void l(void) __attribute__((target));\n",
        // Mutual exclusions (check_input_attribute_excluding), on one declaration, through a specifier's list, and across
        // redeclarations, but not after a definition, where the later one is dropped first.
        "void f(void) __attribute__((hot, cold));\n",
        "void f(void) __attribute__((__hot__, __cold__));\n",
        "[[gnu::hot]] void g(void) __attribute__((cold));\n",
        "__attribute__((hot)) void m(void), n(void) __attribute__((cold));\n",
        "void q(void) __attribute__((not_tail_called, always_inline));\n",
        "void h(void) __attribute__((cold));\nvoid h(void);\nvoid h(void) __attribute__((hot));\n",
        "static void s(void) __attribute__((hot));\nstatic void s(void) __attribute__((cold)) {}\n",
        "void k(void) __attribute__((hot)) {}\nvoid k(void) __attribute__((cold));\nvoid c(void) __attribute__((cold)) __attribute__((cold));\n",
        // Uses of what deprecated marks (check_input_deprecated_uses): functions, variables, typedefs, tags, enumerators (by
        // their enum too), with a message or none, after the declaration that marks them; none in a deprecated function's
        // body (deprecated by an earlier declaration too), in a deprecated declaration's specifiers, or in a bare tag
        // declaration; and an unavailable one's error, with its note at the declaration.
        "void f(void) __attribute__((deprecated));\nvoid g(void) __attribute__((deprecated(\"use h\")));\n[[deprecated]] int v;\n"
        "[[deprecated(\"old\")]] int w;\ntypedef int T __attribute__((deprecated));\nstruct __attribute__((deprecated)) S { int a; };\n"
        "enum E { A __attribute__((deprecated)), B };\nint main(void)\n{\n    f();\n    g();\n    T t = v + w + A;\n    struct S s;\n"
        "    (void)t; (void)s;\n    void (*p)(void) = f;\n    (void)p;\n    return sizeof(v);\n}\nvoid d(void) __attribute__((deprecated));\n"
        "void d(void) { f(); }\nvoid e(void);\nvoid e(void) __attribute__((deprecated));\nvoid e(void);\nT x __attribute__((deprecated));\n"
        "struct S;\nstruct S *ps;\nenum __attribute__((deprecated)) F { F0 };\nenum F ef;\nint k(void) { e(); return F0; }\n",
        // Memoized enumerator-to-enum answers (#1354): an enclosing walk crosses the nested body, but each enum's
        // diagnostics must retain its own mark when their enumerators are used out of source order.
        "enum __attribute__((deprecated(\"outer\"))) Outer { O0, O1 = sizeof(enum __attribute__((deprecated(\"inner\"))) Inner { I0, I1 }), O2 };\n"
        "enum __attribute__((deprecated(\"other\"))) Other { P0, P1, P2 };\n"
        "int f(void) { return O2 + I1 + P2 + O0 + I0 + P0 + O1; }\n",
        "enum __attribute__((unavailable(\"gone\"))) Gone { U0, U1, U2 };\n"
        "int h(void) { return U2 + U0 + U1; }\n",
        "void u(void) __attribute__((unavailable(\"gone\")));\nint h(void) { return sizeof(u); }\n",
        // __builtin___CFStringMakeConstantString's literal that isn't UTF-8 (#148, check_input_cfstring_literal), past its
        // parentheses and casts; a valid one's, and NSString's, without a word.
        "const void* f(void) { return __builtin___CFStringMakeConstantString(\"\\xff\"); }\n"
        "const void* g(void) { return __builtin___CFStringMakeConstantString(((const char*)(\"\\xc0\\x80\"))); }\n"
        "const void* h(void) { return __builtin___CFStringMakeConstantString(\"\\xc3\\xa9 \\xed\\x9f\\xbf a\\0b\"); }\n"
        "const void* k(void) { return __builtin___NSStringMakeConstantString(\"\\xff\"); }\n"
        "const void* m(void) { return __builtin___CFStringMakeConstantString(\"\\xed\\xa0\\x80\" \"\\xf4\\x90\\x80\\x80\"); }\n",
        // A start its function refuses (#148, check_diagnostic_varargs), at the builtin's name: by its ABI, and without
        // variadic parameters.
        "void f(int n, ...) { char* a; __builtin_ms_va_start(a, n); }\n",
        "void __attribute__((ms_abi)) f(int n, ...) { __builtin_va_list a; __builtin_va_start(a, n); }\n",
        "void __attribute__((ms_abi)) f(int n, ...) { char* a; __builtin_zos_va_start(&a, n); }\n",
        "void __attribute__((ms_abi)) f(int n) { char* a; __builtin_ms_va_start(a, (n)); }\n",
        "void f(int n) { __builtin_va_list a; __builtin_va_start(a, n); }\n",
        "void u(void) __attribute__((unavailable));\nvoid z(void) { u(); }\n",
        // Members' uses (check_input_member_uses): `.` and `->`, through an anonymous member and a union, of any operand
        // (a dereference, an address, a compound literal, a call), in sizeof; none in a deprecated or unavailable
        // function's body, nor in a deprecated declaration's initializer; an unavailable one's error, in a deprecated
        // function too, with its note at the member's name.
        "struct S { int x __attribute__((deprecated(\"msg\"))); int y; struct { int z __attribute__((deprecated)); }; };\n"
        "union U { int a [[deprecated]]; };\nstruct S g(void);\n"
        "int f(struct S *p, struct S s, union U v) { return p->x + s.x + s.z + v.a + s.y + (*p).x + (&s)->x + ((struct S){.y = 0}).y + g().x; }\n"
        "int h(struct S *p) { int a = sizeof(p->z); return a; }\n"
        "__attribute__((deprecated)) int m(struct S *p) { return p->x; }\n"
        "__attribute__((unavailable)) int n(struct S *p) { return p->x; }\n"
        "struct S t;\n__attribute__((deprecated)) int q = sizeof(t.x);\nint r __attribute__((deprecated)) = sizeof(t.z);\n"
        "int w(void) { __attribute__((deprecated)) int l = t.x; return 0; }\n",
        "struct S { int u __attribute__((unavailable(\"gone\"))); int w; };\nint k(struct S *p) { return p->u; }\n",
        "struct S { int u __attribute__((unavailable)); };\n__attribute__((deprecated)) int k(struct S *p) { return sizeof(p->u); }\n",
        "struct S { int u __attribute__((unavailable)); };\n__attribute__((unavailable)) int k(struct S *p) { return p->u; }\n",
        "struct S { int x __attribute__((deprecated)), y __attribute__((deprecated(\"y\"))); } s;\n"
        "struct T { struct S in; } t;\nint k(void) { return s.x + t.in.y + __builtin_offsetof(struct S, x); }\n",
        // Both deprecated and unavailable: unavailable, whatever their order, one list or two, as clang's getAvailability
        // takes the worst; a deprecated context doesn't drop it.
        "struct S { int x __attribute__((deprecated(\"old\"), unavailable(\"gone\"))); };\nint f(struct S *s) { return s->x; }\n",
        "struct S { int x __attribute__((unavailable(\"gone\"), deprecated(\"old\"))); };\nint f(struct S *s) { return s->x; }\n",
        "struct S { int x __attribute__((deprecated(\"old\"))) __attribute__((unavailable(\"gone\"))); };\n"
        "__attribute__((deprecated)) int f(struct S *s) { return s->x; }\n",
        "int g(void) __attribute__((deprecated(\"old\"), unavailable(\"gone\")));\n"
        "int h(void) __attribute__((unavailable(\"gone\"), deprecated(\"old\")));\nint f(void) { return g() + h(); }\n",
        // Redeclarations (check_input_availability_mark): a use reads the marks up to the declaration its lookup finds,
        // unavailable over deprecated and the latest of a kind's message, with the unavailable note at that declaration.
        "int g(void) __attribute__((deprecated(\"old\")));\nint f(void) { return g(); }\n"
        "int g(void) __attribute__((unavailable(\"gone\")));\nint g(void);\nint h(void) { return g(); }\n",
        "int a(void) __attribute__((deprecated(\"old\")));\nint a(void) __attribute__((deprecated(\"older\")));\n"
        "int b(void) __attribute__((unavailable(\"gone\")));\nint b(void) __attribute__((unavailable(\"really\")));\n"
        "int b(void) __attribute__((deprecated(\"dep\")));\nint f(void) { return a() + b(); }\n",
        // A block's later extern that the use doesn't see doesn't mark it; a block's earlier one marks a file-scope
        // redeclaration, and a block's own redeclaration takes the note.
        "void k(void) { extern int v __attribute__((deprecated(\"old\"))); { extern int v __attribute__((unavailable(\"gone\"))); } (void)v; }\n"
        "void m(void) { extern int w __attribute__((unavailable(\"gone\"))); }\nextern int w;\nint f(void) { return w; }\n",
        "int g(void) __attribute__((unavailable(\"gone\")));\nint f(void) { int g(void); return g(); }\n",
        // A tag's: its attributes before the use, the note at its latest declaration's name; an enumerator's by its enum,
        // the note naming the enum; a typedef's redeclaration.
        "struct __attribute__((deprecated(\"old\"))) S;\nstruct S *p;\nstruct __attribute__((unavailable(\"gone\"))) S;\nstruct S;\n"
        "struct S *q;\nstruct __attribute__((unavailable)) U { int a; };\nstruct U *r;\n",
        "enum __attribute__((unavailable(\"gone\"))) E { A };\nenum __attribute__((deprecated(\"old\"), unavailable(\"gone\"))) F { B };\n"
        "typedef int T __attribute__((deprecated(\"old\")));\ntypedef int T __attribute__((unavailable(\"gone\")));\nint x = A + B;\nT t;\n",
        // A marked declaration's specifiers by its worst mark: a deprecated one drops a deprecated typedef's or tag's use,
        // not an unavailable one's, whose error stays (at file scope, an attribute before or after the declarator, by both
        // marks or a redeclaration); an unavailable one drops both.
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((deprecated)) T x;\n",
        "struct __attribute__((unavailable(\"gone\"))) S { int a; };\n__attribute__((deprecated)) struct S s;\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\nT x __attribute__((deprecated)), y;\n",
        "typedef int T __attribute__((deprecated(\"old\"), unavailable(\"gone\")));\n__attribute__((deprecated)) T x, y;\n"
        "typedef int U __attribute__((deprecated(\"old\")));\ntypedef int U __attribute__((unavailable(\"gone\")));\n__attribute__((deprecated)) U z;\n",
        "typedef int T __attribute__((deprecated(\"old\")));\n__attribute__((deprecated)) T x;\n"
        "typedef int U __attribute__((unavailable(\"gone\")));\n__attribute__((unavailable)) U y;\n"
        "typedef int V __attribute__((deprecated(\"old\")));\n__attribute__((unavailable)) V z;\nT w __attribute__((deprecated));\n"
        "struct __attribute__((deprecated(\"old\"))) S { int a; };\n__attribute__((deprecated)) struct S s;\n"
        "struct __attribute__((unavailable(\"gone\"))) R { int a; };\n__attribute__((unavailable)) struct R r;\n",
        // Not a later declarator's: a use in an initializer (bare, in parentheses or braces) or an earlier declarator's
        // bound keeps its diagnostic; the declarator's own mark drops its initializer's, and `typeof`'s operand and an
        // attribute list's comma are in the specifiers.
        "enum { E __attribute__((unavailable(\"gone\"))) = 1 };\nint a = E, b __attribute__((unavailable));\n",
        "enum { D __attribute__((deprecated(\"old\"))) = 1 };\nint a = D, b __attribute__((deprecated));\n"
        "int c = (D), d __attribute__((deprecated));\nint k[D], m __attribute__((deprecated));\nint n = { D }, o __attribute__((deprecated));\n",
        "enum { E __attribute__((unavailable(\"gone\"))) = 1 };\nint k[E], m __attribute__((unavailable));\n",
        "enum { E __attribute__((unavailable(\"gone\"))) = 1, D __attribute__((deprecated(\"old\"))) = 2 };\n"
        "int c __attribute__((unavailable)) = E, d;\nint e __attribute__((deprecated)) = D, f;\n__attribute__((deprecated)) int g = D, h;\n"
        "typeof(E) n __attribute__((unavailable));\n__attribute__((deprecated)) typeof(D) p;\n"
        "typedef int T __attribute__((unavailable(\"gone\")));\nT __attribute__((aligned(8), unavailable)) r;\n",
        // A comma inside a group the use is in, as `typeof((E, 1))`'s, is the specifiers' too; unmarked, the use keeps its
        // diagnostic.
        "enum { E __attribute__((unavailable(\"gone\"))) = 1, D __attribute__((deprecated(\"old\"))) = 2 };\n"
        "__attribute__((unavailable)) typeof((E, 1)) x;\n__attribute__((deprecated)) typeof((D, 1)) y;\n"
        "__attribute__((unavailable)) typeof((D, 1)) z;\nint a = (E), b __attribute__((unavailable)) = sizeof(typeof((E, 1)));\n",
        "enum { E __attribute__((unavailable(\"gone\"))) = 1 };\ntypeof((E, 1)) x;\n",
        "enum { D __attribute__((deprecated(\"old\"))) = 2 };\ntypeof((D, 1)) y;\n",
        // A marked declarator's parameter lists take its mark: named and unnamed parameters, a definition's, an attribute
        // after the declarator, a function pointer's, a nested list, a block's prototype; unmarked, the uses keep theirs.
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
        "__attribute__((unavailable)) int f(T x);\n__attribute__((unavailable)) int g(T);\n"
        "__attribute__((unavailable)) int h(T x) { return x; }\n__attribute__((deprecated)) int i(D x);\n"
        "__attribute__((unavailable)) int j(D);\nint k(T x) __attribute__((unavailable));\n"
        "__attribute__((deprecated)) int (*fp)(D x);\n__attribute__((deprecated)) int m(int (*q)(D));\n"
        "void n(void) { __attribute__((deprecated)) int p(D x); __attribute__((unavailable)) int (*r)(T); (void)r; }\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\nint f(T x);\nint g(T);\n",
        "typedef int D __attribute__((deprecated(\"old\")));\nint i(D x);\nint (*fp)(D x);\nvoid n(void) { int p(D); }\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((deprecated)) int f(T x);\n",
        // A parameter's own mark is no context of its specifiers, before or after them; its marked function's is, and a
        // file-scope declarator in a group or a call in `typeof` before it is still the declaration's.
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
        "int f(T x __attribute__((unavailable)));\nint g(D x __attribute__((deprecated)));\n"
        "int k(__attribute__((unavailable)) T x);\nint m(__attribute__((deprecated)) D x);\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\nint g(int);\n"
        "__attribute__((unavailable)) int h(T x __attribute__((unavailable)));\n__attribute__((deprecated)) D (*fq)(int);\n"
        "__attribute__((unavailable)) T (*fr)(int);\ntypeof(g((T)1)) x __attribute__((unavailable));\n",
        // A compound literal's braces in the specifiers (`typeof((T){0})`) are the specifiers'; a `{` at the declaration's
        // level, an initializer's compound literal's, still ends them.
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
        "__attribute__((unavailable)) typeof((T){0}) x;\n__attribute__((deprecated)) typeof((D){0}) y;\n"
        "__attribute__((unavailable)) typeof((D){0}) w;\nint *a = (D[]){0}, b __attribute__((deprecated));\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypeof((T){0}) x;\n",
        "typedef int D __attribute__((deprecated(\"old\")));\ntypeof((D){0}) y;\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((deprecated)) typeof((T){0}) z;\n",
        // A marked declarator's array bounds take its mark, at any scope, past a group's `)` and in a later dimension; a
        // tag's body in its specifiers doesn't (an enumerator's value), and a compound literal's braces there do.
        "typedef int D __attribute__((deprecated(\"old\")));\n__attribute__((deprecated)) int x[sizeof(D)];\n"
        "int y __attribute__((deprecated)), z[sizeof(D)];\n__attribute__((deprecated)) int (*fp)[sizeof(D)];\n"
        "__attribute__((deprecated)) int w[2][sizeof(D)];\nvoid f(void) { __attribute__((deprecated)) int v[sizeof(D)]; (void)v; }\n"
        "__attribute__((deprecated)) enum { A = sizeof(D) } e;\n__attribute__((deprecated)) typeof((int){ sizeof(D) }) c;\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((unavailable)) int x[sizeof(T)];\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((unavailable)) enum { A = sizeof(T) } x;\n",
        // A tag's body in a marked declarator's bound, parameter list or initializer has its own context, the members'
        // and enumerators' uses keeping theirs; one in a marked function's body takes the function's.
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((unavailable)) int x[sizeof(struct { T m; })];\n",
        "typedef int D __attribute__((deprecated(\"old\")));\n__attribute__((deprecated)) int x[sizeof(struct { D m; })];\n"
        "__attribute__((deprecated)) int y[sizeof(enum { A = sizeof(D) })];\n__attribute__((deprecated)) int z[sizeof((D){0})];\n"
        "__attribute__((deprecated)) int i = sizeof(struct { D m; });\n__attribute__((deprecated)) int f(struct { D m; } *p);\n"
        "__attribute__((deprecated)) int j = sizeof(enum { B = sizeof(D) });\n"
        "__attribute__((deprecated)) void g(void) { struct { D m; } s; enum { C = sizeof(D) }; (void)s; }\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\n__attribute__((unavailable)) void f(void) { struct { T m; } s; (void)s; }\n",
        // A marked tag's own body is its context: a struct's, a union's, an enum's (and its base), a nested tag's, a tag
        // marked by an earlier declaration, an anonymous enum, an enum's attribute after its body; a struct's after its
        // body or a later declaration's isn't. Its definition's name is no use of it.
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
        "struct __attribute__((unavailable)) S { T m; };\nstruct __attribute__((deprecated)) R { D m; };\n"
        "enum __attribute__((deprecated)) E { A = sizeof(D) };\nunion __attribute__((deprecated)) U { D m; };\n"
        "struct __attribute__((deprecated)) N { struct { D n; } m; };\nenum __attribute__((deprecated)) { B = sizeof(D) };\n"
        "enum __attribute__((deprecated)) F : D { C };\nenum G { H = sizeof(D) } __attribute__((deprecated));\n",
        "typedef int D __attribute__((deprecated(\"old\")));\nstruct S { D m; } __attribute__((deprecated));\n"
        "struct __attribute__((deprecated)) R;\nstruct R { D m; };\nstruct Q { D m; };\nstruct __attribute__((deprecated)) Q;\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\nstruct __attribute__((deprecated)) S { T m; };\n",
        "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
        "struct S { T m; };\nenum E { A = sizeof(D) };\n",
        // A marked tag's definition with declarators is a use of it at its name (its attributes after the body counting),
        // a standalone one isn't; and a tag's use in a declaration's specifiers is diagnosed once for each declarator, in
        // that declarator's context, at any scope, where a typedef name's is once.
        "struct __attribute__((unavailable(\"gone\"))) S;\nstruct S { int m; } obj;\n",
        "struct __attribute__((unavailable(\"gone\"))) S;\nstruct S { int m; };\nstruct __attribute__((deprecated(\"old\"))) R;\n"
        "struct R { int m; };\nstruct __attribute__((deprecated(\"old\"))) Q;\nstruct Q { int m; } q1, q2;\n",
        "struct __attribute__((unavailable(\"gone\"))) S { int m; } obj;\n",
        "struct __attribute__((deprecated(\"old\"))) S { int m; } obj;\ntypedef struct __attribute__((deprecated(\"old\"))) R { int m; } RT;\n"
        "struct __attribute__((deprecated(\"old\"))) Q;\nstruct Q { int m; } obj2 __attribute__((deprecated));\n"
        "struct P { int m; } __attribute__((deprecated(\"late\"))) p;\n",
        "enum __attribute__((unavailable(\"gone\"))) E : int;\nenum E : int { A } obj;\n",
        "struct __attribute__((unavailable(\"gone\"))) S;\nstruct S { int m; };\nstruct S *p;\n",
        "typedef int D __attribute__((deprecated(\"old\")));\nD a, b;\nD c __attribute__((deprecated)), e;\nD f, g __attribute__((deprecated));\n"
        "struct __attribute__((deprecated(\"t\"))) S { int m; };\nstruct S h, i;\nstruct S j __attribute__((deprecated)), k;\n"
        "struct S l, m __attribute__((deprecated));\nvoid n(void) { struct S o, q __attribute__((deprecated)); (void)o; (void)q; }\n"
        "struct X { struct S r, u; struct S v __attribute__((deprecated)), w; };\nstruct S (*fp)(void), x, *y = 0;\n"
        "typedef struct S T1, T2;\nvoid z(struct S a, struct S b);\nint size = sizeof(struct S);\n",
        "struct __attribute__((unavailable(\"gone\"))) U;\nstruct U *u1, *u2;\n",
        // A fixed enum's underlying type (a typedef name) is no declarator: its definition's uses are each declarator's,
        // in that declarator's context. A use in the base is once, in the first declarator's context at any scope, not
        // the enum's own mark; in a declaration of the enum alone, in the enum's. An attribute group before a declarator
        // names none.
        "typedef unsigned T;\nenum __attribute__((unavailable(\"gone\"))) E : T;\nenum E : T { A } x __attribute__((unavailable));\n",
        "typedef unsigned T;\nenum __attribute__((unavailable(\"gone\"))) E : T;\nenum E : T { A } x __attribute__((unavailable)), y;\n",
        "typedef unsigned T;\nenum __attribute__((deprecated(\"old\"))) E1 : T;\nenum E1 : T { A1 } x1 __attribute__((deprecated));\n"
        "enum __attribute__((deprecated(\"old\"))) E2 : T;\nenum E2 : T { A2 } x2;\nenum __attribute__((deprecated(\"old\"))) E3 : T;\n"
        "enum E3 : T { A3 } x3 __attribute__((deprecated)), y3;\n",
        "typedef unsigned T __attribute__((deprecated(\"old\")));\ntypedef unsigned V __attribute__((unavailable(\"gone\")));\nvoid f(void)\n{\n"
        "    enum E1 : T { A1 } a1 __attribute__((deprecated)), b1;\n    enum E2 : T { A2 } a2, b2 __attribute__((deprecated));\n"
        "    enum E3 : T { A3 };\n}\nstruct S\n{\n    enum G1 : T { C1 } m1 __attribute__((deprecated)), n1;\n"
        "    enum G2 : T { C2 } m2, n2 __attribute__((deprecated));\n};\nenum __attribute__((deprecated)) K : T { K1 } k;\n"
        "enum L : T { L1 } __attribute__((deprecated)) l;\nenum : T { N } z __attribute__((deprecated)), w;\nenum M : V { M1 } v __attribute__((deprecated));\n",
        "typedef int D __attribute__((deprecated(\"old\")));\nenum __attribute__((deprecated)) F1 : D { C1 };\nenum F2 : D { C2 } __attribute__((deprecated));\n"
        "enum F3 : D { C3 };\nenum __attribute__((deprecated)) F4 : D;\nenum __attribute__((deprecated)) F5 : D;\nenum F5 : D { C5 };\n"
        "void f(void) { enum __attribute__((deprecated)) F6 : D { C6 }; enum F7 : D { C7 }; }\n"
        "struct S { enum __attribute__((deprecated)) F8 : D { C8 } m; };\n",
        "struct __attribute__((deprecated)) S { int a; } __attribute__((unused)) s __attribute__((deprecated));\n"
        "struct __attribute__((deprecated)) R { int a; } __attribute__((unused)) r;\n"
        "struct __attribute__((unavailable)) Q { int a; } __attribute__((aligned(8))) q __attribute__((unavailable)), *q2 __attribute__((unavailable));\n",
        // A context's mark is the worst too: a deprecated function's body drops a deprecated use, not an unavailable one.
        "int g(void) __attribute__((deprecated(\"old\")));\nint g(void) __attribute__((unavailable(\"gone\")));\n"
        "__attribute__((deprecated)) int f(void) { return g(); }\n__attribute__((unavailable)) int h(void) { return g(); }\n"
        "int k(void) __attribute__((deprecated(\"x\")));\nint k(void) __attribute__((unavailable(\"y\")));\nint k(void) { return g(); }\n",
        // Initializers' uses (check_init_member_use): a designator's member, a value's member by its position at each level
        // of elided braces, through an anonymous member and a union's first; none in a deprecated declaration's.
        "struct S { int x __attribute__((deprecated(\"m\"))); int y; struct { int z __attribute__((deprecated)); }; };\n"
        "struct S a = { 1, 2, 3 };\nstruct S b = { .x = 1, .z = 2 };\n"
        "struct T { struct S in; int w __attribute__((deprecated)); } c = { 1, 2, 3, 4 }, d = { { 1 }, 5 }, e = { .in.x = 1, .w = 2 };\n"
        "int f(void) { struct S l = { 0 }; return ((struct S){ .y = 1, 2 }).y + l.y; }\n"
        "__attribute__((deprecated)) struct S g = { 1 };\n"
        "union U { int a [[deprecated]]; float b; } u = { 1 }, v = { .b = 1 };\n"
        "struct S h[2] = { [1].x = 1, [0] = { 2 } };\n",
        // An unavailable member's use in an initializer is clang's error (check_init_member_use): a designator's, a value's by its
        // position at each level, a union's first, a compound literal's, an array element's; none in an unavailable declaration's.
        "struct S { int u __attribute__((unavailable(\"gone\"))); int w; };\nstruct S a = { .u = 1 };\nstruct S b = { 2, 3 };\n"
        "void f(void) { struct S c = { .w = 1, .u = 2 }; (void)c; }\n",
        "struct S { int u __attribute__((unavailable(\"gone\"))); int w; };\nstruct T { struct S s; int z; };\n"
        "struct T a = { .s.u = 1 };\nstruct T b = { { 1, 2 }, 3 };\nunion U { int v __attribute__((unavailable)); int x; } c = { 1 };\n"
        "__attribute__((unavailable)) struct S d = { .u = 1 };\n__attribute__((deprecated)) struct S e = { .u = 1 };\n"
        "int f(void) { return (struct S){ .u = 2 }.w; }\nstruct S g[2] = { [1].u = 3 };\n",
        // A marked member's access the evaluator alone reads (check_input_evaluated_members): `typeof`'s operand, a file-scope
        // array bound, an enumerator's value, a static assertion's; once where the graph reads it too (a variable length array's
        // bound); none in a marked declaration's specifiers.
        "struct S { int x __attribute__((deprecated(\"old\"))); int u __attribute__((unavailable)); } s;\ntypeof(s.x) a;\n"
        "int b[sizeof(s.x)];\nint c[sizeof(s.u)];\nvoid f(void) { typeof(s.x) d; (void)d; }\n",
        "struct S { int x __attribute__((deprecated(\"old\"))); int y; struct { int z __attribute__((deprecated)); }; } s, *p;\n"
        "typeof(p->x) a;\ntypeof(s.z) b;\nint c[sizeof(s.x) + sizeof(p->x)];\n__attribute__((deprecated)) typeof(s.x) d;\n"
        "typeof(s.x) e __attribute__((deprecated));\n"
        "int f(void) { typeof(p->x) g = 0; return sizeof(s.x) + g + (int)sizeof(typeof(s.y)); }\n"
        "_Static_assert(sizeof(s.x) == 4, \"\");\nenum { E = sizeof(s.x) };\n"
        "int h(int n) { int v[sizeof(s.x)]; int w[n + sizeof(p->x)]; return v[0] + w[0]; }\nstruct T { int m[sizeof(s.x)]; };\n",
        "struct S { int x __attribute__((deprecated(\"old\"))); } *p;\nint h(int n) { int w[n + sizeof(p->x)]; return w[0]; }\n"
        "int k(int n) { int w[n + p->x]; return w[0]; }\n",
        // A call that resolves to a deprecated overloadable overload (check_input_overload_uses), through parentheses, a
        // redeclaration and a block's; none at an address taken through the overload set, nor in a deprecated function.
        "int f(int) __attribute__((overloadable, deprecated(\"old\")));\nint f(double) __attribute__((overloadable));\n"
        "int f(int) __attribute__((overloadable));\nint g(void) { return (f)(1) + f(2.0) + f(3); }\n"
        "__attribute__((deprecated)) int h(void) { return f(1); }\nint k(char) __attribute__((overloadable));\n"
        "int k(long) __attribute__((overloadable, deprecated));\nint m(void) { int (*q)(long) = k; return k(1L) + q(0); }\n"
        "void n(void) { int f(int) __attribute__((overloadable)); f(1); }\n",
        // A member's own mark is its specifiers', parameter lists' and bounds' context (CheckInputReader.member_marks): a later
        // declarator's isn't the first's, in a struct in a function too.
        "typedef int D __attribute__((deprecated(\"old\")));\ntypedef int T __attribute__((unavailable(\"gone\")));\n"
        "struct S { int (*m)(D) __attribute__((deprecated)); D n __attribute__((deprecated)); int (*o)(T) __attribute__((unavailable)); int (*q)(D); };\n",
        "typedef int D __attribute__((deprecated(\"old\")));\n"
        "struct S { D a, b __attribute__((deprecated)); D c[sizeof(D)] __attribute__((deprecated)); int d[sizeof(D)] __attribute__((deprecated)); };\n"
        "void f(void) { struct L { int (*m)(D) __attribute__((deprecated)); } l; (void)l; }\n",
        // A deprecated anonymous struct's, union's or enum's use (its definition with declarators, a type name's, an anonymous
        // member's) at its keyword, named `(unnamed at ...)` or `(anonymous at ...)` or by its typedef; an unavailable one's error.
        "struct __attribute__((deprecated)) { int a; } x;\ntypedef struct __attribute__((deprecated)) { int a; } T;\nT y;\n"
        "union __attribute__((deprecated(\"m\"))) { int a; } z, w;\n"
        "void f(void) { struct __attribute__((deprecated)) { int a; } l; (void)l; }\n",
        "typedef struct __attribute__((deprecated)) { int a; } T, U;\nT a, b;\nU c;\nstruct { int a; } __attribute__((deprecated)) d;\n"
        "__attribute__((deprecated)) struct __attribute__((deprecated)) { int a; } e;\n"
        "struct __attribute__((unavailable)) { int a; } *f;\nenum __attribute__((deprecated)) { E0 } g;\n"
        "int h = sizeof(struct __attribute__((deprecated)) { int a; });\nstruct __attribute__((deprecated)) { int a; };\n"
        "struct O { struct __attribute__((deprecated)) { int a; } m; struct __attribute__((deprecated)) { int b; }; };\ntypedef T V;\n"
        "V v;\n",
        // A typedef that names a marked struct, union or enum takes its mark (ShouldDiagnoseAvailabilityOfDecl): through qualifiers
        // and typedefs, a definition's marks or a first declaration's own; a block's object's specifiers keep the use, a typedef's
        // and a member's drop it.
        "struct __attribute__((deprecated(\"s\"))) S { int a; };\ntypedef struct S T;\nT x, y;\nT *p;\ntypedef T W;\nW w;\n"
        "typedef struct S *P;\nP q;\nvoid f(T a);\nint g(void) { return sizeof(T); }\n",
        "struct __attribute__((unavailable(\"gone\"))) S { int a; };\ntypedef struct S T;\nT *p;\ntypedef const struct S CT;\nCT *q;\n"
        "struct R;\ntypedef struct R TR;\nTR *r0;\nstruct __attribute__((deprecated(\"late\"))) R;\nTR *r1;\n"
        "enum __attribute__((deprecated)) E { E0 };\ntypedef enum E TE;\nTE e;\n"
        "typedef struct __attribute__((deprecated(\"m\"))) { int a; } A;\n__attribute__((deprecated)) A a1;\n"
        "__attribute__((deprecated)) int f(A x) { A y; (void)y; return x.a; }\nint g(void) { return sizeof(A); }\n"
        "typedef A B __attribute__((deprecated(\"b\")));\nB b;\ntypedef union __attribute__((unavailable)) { int a; } U;\nU *u;\n"
        "struct H { struct __attribute__((unavailable)) { int z; }; union __attribute__((deprecated)) { int w; } k; };\n"
        "void h(struct __attribute__((deprecated)) { int a; } *p);\n",
        "typedef int D __attribute__((deprecated(\"d\")));\nstruct __attribute__((deprecated(\"s\"))) S { int a; };\n"
        "typedef D X __attribute__((deprecated));\ntypedef int (*F)(D) __attribute__((deprecated));\n"
        "typedef struct S Y __attribute__((deprecated));\ntypedef int Z[sizeof(D)] __attribute__((deprecated));\n"
        "typedef D W, V __attribute__((deprecated));\ntypedef struct S Q, P __attribute__((deprecated));\n"
        "void f(void) { typedef D L __attribute__((deprecated)); }\n",
        "struct R;\ntypedef struct R TR;\nstruct __attribute__((deprecated(\"def\"))) R { int a; };\nTR x;\nstruct Q;\n"
        "typedef struct Q TQ;\nstruct __attribute__((deprecated(\"fwd\"))) Q;\nstruct Q { int a; };\nTQ y;\n"
        "void f(void) { __attribute__((deprecated)) TR z; (void)z; }\n",
        "typedef int D __attribute__((deprecated(\"d\")));\nstruct __attribute__((deprecated(\"s\"))) S { int a; };\n"
        "typedef struct S TS;\nvoid f(void) {\n  __attribute__((deprecated)) D a; (void)a;\n"
        "  D b __attribute__((deprecated)); (void)b;\n  typedef D c __attribute__((deprecated));\n"
        "  __attribute__((deprecated)) struct S d; (void)d;\n  __attribute__((deprecated)) TS e; (void)e;\n"
        "  TS g __attribute__((deprecated)); (void)g;\n}\n__attribute__((deprecated)) TS h;\nTS i __attribute__((deprecated));\n",
        // An attribute that takes no arguments is checked for its subject first (check_input_attribute_subjects' count), not a
        // type's; and a declarator's type's convention warnings come before the declaration's attributes'
        // (check_input_attribute_type_warnings).
        "void f(void) __attribute__((cold(1)));\n",
        "int e __attribute__((const(1)));\nint i __attribute__((always_inline(1)));\nint m __attribute__((returns_twice(1)));\n",
        // So is one that takes arguments, which are parsed first: a string literal's position that holds none, or a name
        // that names nothing, is clang's parser's error, which drops it (check_input_attribute_parsed). A type's count and
        // sentinel's still come first.
        "int x __attribute__((format(printf)));\nint y __attribute__((format(printf, 1)));\nint v __attribute__((nonnull(1)));\n"
        "int u __attribute__((alloc_size()));\nint t __attribute__((cleanup()));\nint s __attribute__((constructor(1, 2)));\n"
        "int r __attribute__((assume_aligned(1, 2, 3)));\nint q __attribute__((format(printf, 1, 2, 3))), p;\n"
        "struct S { int m __attribute__((format(printf))); };\ntypedef int T __attribute__((format(printf)));\n"
        "int o [[gnu::format(printf)]];\nint n __attribute__((enum_extensibility(open, 1)));\n"
        "void f(int p __attribute__((format(printf))));\nint (*fp)(void) __attribute__((format(printf)));\n",
        "int z __attribute__((section()));\n",
        "int w __attribute__((aligned(1, 2)));\n",
        "void f(void) __attribute__((format(printf)));\n",
        "void f(void) __attribute__((alloc_size(1, 2, 3)));\n",
        "int g __attribute__((visibility(\"default\", \"x\")));\n",
        "int j __attribute__((mode(SI, 2)));\n",
        "int k __attribute__((sentinel(1, 2, 3)));\n",
        "int s __attribute__((noreturn(1)));\n",
        "void f(int) __attribute__((format_arg(1, 2)));\n",
        "void f(void) __attribute__((constructor(1, 2)));\n",
        "int o __attribute__((tls_model()));\n",
        "[[gnu::section()]] int x;\n",
        "void f(void) __attribute__((cold, hot(1)));\n",
        "void f(void) { int x __attribute__((section())); }\n",
        "int a __attribute__((aligned(q, 2)));\n",
        "int b __attribute__((format(printf, q)));\n",
        "int c __attribute__((visibility(hidden, r)));\n",
        "int d __attribute__((malloc(q, 1, 2)));\n",
        "int e __attribute__((cold(q)));\n",
        "int a __attribute__((aligned(sizeof(q), 2)));\n",
        "int i __attribute__((cleanup(f, g)));\n",
        "int b __attribute__((section(1, \"a\")));\n",
        "int c __attribute__((deprecated(\"a\", 1, 2)));\n",
        "int e __attribute__((visibility(1, 2)));\n",
        "int __attribute__((cold)) __stdcall x;\n__attribute__((cold)) int __stdcall y;\nint __stdcall __attribute__((cold)) z;\n"
        "int __attribute__((unused, cold)) __fastcall w;\n",
        // A function type's attribute anywhere in a declarator (#147, check_input_attribute_type_warnings): clang warns of it
        // as it builds the declarator's type, before the declaration's attributes: a convention x86-64 hasn't for the
        // specifiers' and the trailing ones, then a group's or a pointer's; off a function, a group's or a pointer's with the
        // type built so far, then the specifiers' and the trailing ones with the whole type. Each declarator in turn, in
        // parameters, members, typedefs, type names and bodies, and after unknown attributes.
        "__attribute__((noinline)) void (__stdcall *p)(void);\n__attribute__((noinline)) void (__attribute__((stdcall)) *q)(void);\n"
        "__attribute__((noinline)) int (__attribute__((regparm(2))) *r);\n__attribute__((noinline)) __stdcall void (*s)(void);\n"
        "__attribute__((noinline)) void (__fastcall *a1)(void);\n__attribute__((noinline)) void (__thiscall *a2)(void);\n"
        "__attribute__((noinline)) void (__pascal *a3)(void);\n__attribute__((noinline)) void (__attribute__((ms_abi)) *a4)(void);\n"
        "__attribute__((noinline)) int (__attribute__((ms_abi)) *a5);\n__attribute__((noinline)) int (__attribute__((sysv_abi)) *a6);\n"
        "__attribute__((noinline)) int (__attribute__((noreturn)) *a9);\n__attribute__((noinline)) void (__attribute__((noreturn)) *a10)(void);\n"
        "__attribute__((noinline)) int (__stdcall *a11);\n__attribute__((noinline)) int __stdcall *a12;\n"
        "__attribute__((noinline)) int *a13 __attribute__((regparm(1)));\n__attribute__((noinline)) void (__stdcall *b1)(void), (__fastcall *b2)(void);\n"
        "__attribute__((cold, noinline)) void (__stdcall *b3)(void);\n__attribute__((noinline)) __attribute__((fastcall)) void (__stdcall *b4)(void);\n"
        "void (__stdcall *b5)(void) __attribute__((noinline));\n__attribute__((noinline)) void (* __stdcall b6)(void);\n"
        "__attribute__((noinline)) void (*(__stdcall *b7))(void);\n__attribute__((noinline)) int (__attribute__((regparm(2))) r2);\n"
        "struct S { __attribute__((noinline)) void (__stdcall *m)(void); __attribute__((noinline)) int (__attribute__((regparm(2))) *n); };\n"
        "void f(__attribute__((noinline)) void (__stdcall *p)(void), __attribute__((noinline)) int (__attribute__((regparm(2))) *r));\n"
        "typedef __attribute__((noinline)) void (__stdcall *T)(void);\n__attribute__((noinline)) __attribute__((regparm(2))) int c1;\n"
        "__attribute__((noinline)) int (__attribute__((noinline)) *c2);\n",
        "int * __attribute__((regparm(1))) x1;\nint *(__attribute__((regparm(1))) x2);\nint (__attribute__((regparm(1))) *(*x3));\n"
        "int (__attribute__((regparm(2))) a1[3]);\nint (__attribute__((regparm(2))) (*f1)(void));\nint (__attribute__((regparm(2))) *f2)(void);\n"
        "int *(* __attribute__((regparm(2))) f3)[2];\nint __attribute__((regparm(2))) (*f4)[2];\nint (* __stdcall x5)[2];\n"
        "int (__stdcall (*f5)(void));\n__attribute__((noinline)) int (__stdcall (*f6)(void));\n"
        "int x6 __attribute__((noinline)), (__attribute__((regparm(2))) *x7);\n"
        "__attribute__((noinline)) int (__attribute__((noreturn)) *x8), (__attribute__((regparm(2))) *x9);\n"
        "int __attribute__((noinline)) (__attribute__((noreturn)) *x10);\nint (__attribute__((noinline)) __attribute__((noreturn)) *x11);\n"
        "int (__attribute__((noreturn)) __attribute__((noinline)) *x12);\nint (__attribute__((noreturn, noinline)) *x13);\n"
        "int (__attribute__((noinline, noreturn)) *x14);\n__attribute__((noinline)) __attribute__((regparm(1))) int (__stdcall *x15)(void);\n"
        "__attribute__((noinline)) int (__attribute__((stdcall)) *x16)(void) __attribute__((cold));\n"
        "int (__stdcall *x17)(void) __attribute__((cold)) __attribute__((fastcall));\n__attribute__((noinline)) int (__stdcall *x18)(void), x19;\n"
        "__attribute__((noinline)) int (__attribute__((regparm(1))) **(*x20)[2])[3];\n"
        "void g(void) { __attribute__((noinline)) int (__attribute__((regparm(1))) *l); (void)l; }\n"
        "int y1 = sizeof(__attribute__((noinline)) int (__attribute__((regparm(1))) *));\n"
        "typedef int D __attribute__((deprecated));\n__attribute__((foo)) void (__stdcall *h1)(void);\n"
        "__attribute__((noinline)) D (__stdcall *h2)(void);\n__attribute__((noinline)) D (__attribute__((regparm(1))) *h3);\n"
        "__attribute__((noinline)) void (__stdcall *h6)(D);\n[[gnu::noinline]] void (__stdcall *h8)(void);\n"
        "__attribute__((noinline)) void (*h10)(void) __attribute__((stdcall));\n"
        "__attribute__((noinline)) int x, y __attribute__((regparm(1)));\n"
        "__attribute__((noinline)) int z3 __attribute__((unused)), w3 __attribute__((regparm(1)));\n",
        // A declaration's specifiers' attributes before a declarator that starts with a group, which aren't a typeof's
        // (check_input_free_standing); and a C23 list at a group's start, clang's parser's error.
        "__attribute__((noinline)) int (e1);\n__attribute__((cold)) int (*e2);\n__attribute__((cold)) static int (*e3);\n"
        "static __attribute__((cold)) int (*e4);\nvoid f(void) { __attribute__((cold)) int (*e9); }\n",
        "void ([[gnu::stdcall]] *a7)(void);\n",
        "int ([[gnu::regparm(2)]] *r3);\n",
        "__attribute__((noinline)) void ([[gnu::stdcall]] *p)(void);\n",
        "void __attribute__((stdcall)) ([[gnu::noinline]] *p)(void);\n",
        "void ([[gnu::x]] *p)(void) __attribute__((noinline));\n",
        "void f(int ([[gnu::x]] *p) __attribute__((cold))) __attribute__((stdcall));\n",
        "void f([[gnu::x]] *p __attribute__((cold))) __attribute__((stdcall));\n",
        "void f([[gnu::x]] const p __attribute__((cold))) __attribute__((stdcall));\n",
        "void f([[gnu::x]] Unknown *p __attribute__((cold))) __attribute__((stdcall));\n",
        "__attribute__((noinline)) void ([[gnu::x]] ([[gnu::y]] *p))(void);\n",
        // A handler's preliminary warning precedes its own error, after the completed type's warnings and any
        // earlier declaration attributes. Kernel-only checks follow the entire attribute list.
        "void __attribute__((stdcall, unknown, amdgpu_num_sgpr(0))) f(void);\n",
        "void __attribute__((stdcall, unknown, amdgpu_num_vgpr(4294967296))) f(void);\n",
        // A kernel attribute in the specifiers is rechecked after a function-type chunk, but one in the trailing
        // declaration list is first applied after that chunk. Without a type attribute there is only one visit.
        "void __attribute__((amdgpu_num_sgpr(0))) f(void);\n",
        "void __attribute__((amdgpu_num_sgpr(0))) f(void) __attribute__((stdcall));\n",
        "void __attribute__((stdcall)) f(void) __attribute__((amdgpu_num_sgpr(0)));\n",
        // Availability diagnostics share the complete enclosing declarator's boundary, including every marked
        // parameter type, and then advance to the next declarator after a comma.
        "typedef int D __attribute__((deprecated));\n__attribute__((cold)) void (*f)(D, D), (*g)(D);\n",
    };
    for (uint32_t s = 0; written && expected_lines != NULL && actual_lines != NULL && s < BUSTER_ARRAY_LENGTH(statements); s += 1)
    {
        snprintf(source, sizeof(source), "%s/attributed-statement-%u.c", directory, s);
        const bool made = parse_test_write(source, statements[s], strlen(statements[s]));
        char* arguments[] = { (char*)clang, "-std=gnu23", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL, NULL };
        char* expected = NULL;
        const int oracle = made ? check_test_driver_spawn(arguments, &expected) : -1;
        const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
        for (uint32_t scalar = 0; lines && scalar < 2; scalar += 1)
        {
            arguments[0] = (char*)executable;
            arguments[5] = scalar != 0 ? "--scalar" : NULL;
            char* actual = NULL;
            const int status = check_test_driver_spawn(arguments, &actual);
            const bool compared = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES);
            const bool same = compared && strcmp(expected_lines, actual_lines) == 0 && (status != 0) == (oracle != 0);
            char description[160];
            snprintf(description, sizeof(description), "attributed statement %u, %s: status %d (clang %d), diagnostic lines agree", s,
                     scalar != 0 ? "scalar" : "vector", status, oracle);
            check_test_expect(test, same, description);
            if (!same)
            {
                fprintf(stderr, "%s:\n%s(clang\n%s)\n", description, compared ? actual_lines : actual != NULL ? actual : "<no output>\n", expected_lines);
            }
            free(actual);
        }
        check_test_expect(test, made && lines, "attributed statements: fixture written and clang's lines read");
        free(expected);
        unlink(source);
    }
    free(expected_lines);
    free(actual_lines);
    // clang's "did you mean" suggestions (check_warning_attribute_suggestion): misspellings of clang's spellings, made by a
    // fixed generator (a deletion, an insertion or a replacement, once or twice), as GNU attributes, C23 ones and C23 ones of
    // a scope, itself misspelled at times. A first run of clang keeps the lines it warns about as unknown, as the misspelling
    // can make another attribute clang knows; then both compilers' unknown-attribute lines of those, by both paths, must be
    // clang's.
    check_test_driver_attribute_typos(test, clang, executable, directory);
    // The vocabulary itself (c_check_attribute_clang.inc), against this clang: each GNU name's __has_attribute and each C23
    // spelling's __has_c_attribute (under -std=gnu23) is nonzero, one answer per line.
    for (uint32_t standard = 0; written && standard < 2; standard += 1)
    {
        const CheckInputClangAttribute* list = standard != 0 ? check_input_clang_c23_attributes : check_input_clang_gnu_attributes;
        const uint32_t count = standard != 0 ? BUSTER_ARRAY_LENGTH(check_input_clang_c23_attributes) : BUSTER_ARRAY_LENGTH(check_input_clang_gnu_attributes);
        const char* query = standard != 0 ? "__has_c_attribute" : "__has_attribute";
        size_t capacity = 1;
        for (uint32_t i = 0; i < count; i += 1)
        {
            capacity += strlen(query) + list[i].length + 3;
        }
        char* text = malloc(capacity);
        size_t length = 0;
        for (uint32_t i = 0; text != NULL && i < count; i += 1)
        {
            length += (size_t)snprintf(text + length, capacity - length, "%s(%.*s)\n", query, (int)list[i].length, list[i].name);
        }
        snprintf(source, sizeof(source), "%s/attribute-vocabulary.c", directory);
        const bool made = text != NULL && parse_test_write(source, text, length);
        char* arguments[] = { (char*)clang, standard != 0 ? "-std=gnu23" : "-std=gnu17", "-E", "-P", "-ferror-limit=0", "-x", "c", source, NULL };
        char* output = NULL;
        const int status = made ? check_test_driver_spawn_stream(arguments, &output, true) : -1;
        uint32_t answers = 0;
        uint32_t zero = UINT32_MAX;
        for (const char* line = output; line != NULL && *line != 0;)
        {
            const char* end = strchr(line, '\n');
            const bool blank = line[0] == '\n';
            if (!blank && zero == UINT32_MAX && strtol(line, NULL, 10) == 0)
            {
                zero = answers;
            }
            answers += blank ? 0u : 1u;
            line = end != NULL ? end + 1 : NULL;
        }
        char description[256];
        snprintf(description, sizeof(description), "attribute vocabulary, %s: clang status %d, %u answers of %u, the first zero %s", query, status, answers,
                 count, zero < count ? list[zero].name : "(none)");
        check_test_expect(test, status == 0 && answers == count && zero == UINT32_MAX, description);
        free(output);
        free(text);
        unlink(source);
    }
}
