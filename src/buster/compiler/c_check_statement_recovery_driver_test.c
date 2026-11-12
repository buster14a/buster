// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Translation-unit-wide semantic error recovery, phase 3 (#1447): statements after an error.
// - A `break` or `continue` outside its loop is clang's invalid statement, after which the body and the unit are checked
//   on, so a second one is reported too (CheckInput's recovered reports).
// - A body clang ends after the unit's first error gets no CFG analysis (no -Wuninitialized, no -Wreturn-type), and every
//   runtime-behavior warning it queued is given, reachable or not (check_common_flush).
// - An expression graph's errors no longer cut the asm and control diagnostics of the statements that don't read them.
// On original fixtures, the whole output, line for line, and the status, by the vector path, the scalar one and deferred
// bodies on two lanes, under gnu17 and gnu23 and the warning and error-limit options; and -c of each, which must be
// refused with no object written. The asm and control errors are compared whole too: every one, in clang's words, a
// duplicate case's with its "previous case defined here" note, after a graph's error and in a unit without one (#1447).
// A rejected unit's other bodies get their common warnings (check_driver_rejected_common, #1447), and a left-out
// statement's complete operands theirs (#2120).
// Include after c_check_semantic_recovery_driver_test.c, whose reader this takes.

static void check_test_driver_statement_recovery(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    static const char* const fixtures[] = {
        // Two control errors in two bodies, and a later body's runtime warning.
        "void f(void) { break; }\nvoid g(void) { continue; }\nint h(int x) { return x << 40; }\n",
        // Two in one body, after a switch whose own `break` is valid.
        "void f(int x) { switch (x) { case 1: break; } continue; break; }\n",
        // A control error, then a later body's -Wparentheses and runtime warning.
        "void g(void) { break; }\nint h(int x) { if (x = 2) return x << 40; return 0; }\n",
        // -Wuninitialized before the error, none after it, and no -Wreturn-type after it either.
        "int u0(void) { int y; return y; }\nvoid e(void) { break; }\nint u(void) { int y; return y; }\nint r(int x) { if (x) return 1; }\n",
        // After a preprocessing error, the unreachable statements' and the unselected arms' runtime warnings.
        "#error boom\nint h(int x) { if (0) return x << 40; return 0; }\nint k(int x) { return 1 ? 0 : x / 0; }\n"
          "int m(int x) { return 0; return x / 0; }\n",
        // A body before the error keeps its reachability, as does a file-scope initializer after it; after it, the short
        // circuits, a static selection's other arms, a `goto`'s and a `for (;;)`'s unreachable statements, the bounds, a
        // null dereference, a sign comparison and a constant conversion are given, but nothing in `sizeof`.
        "int before(int x) { if (0) return x << 40; return 0; }\nint g1 = 1 ? 0 : 1 / 0;\n#error boom\nint g2 = 1 ? 0 : 1 / 0;\n"
          "int a(int x) { return 0 && x / 0; }\nint b(int x) { return sizeof(x / 0); }\n"
          "int c(int x) { return _Generic(x, int: 0, default: x / 0); }\nint d(int x) { return __builtin_choose_expr(1, 0, x / 0); }\n"
          "int e(int x) { return x || (x / 0); }\nint f(int x) { int a[2]; (void)x; return 0 ? a[4] : 0; }\n"
          "int g(int x) { for (;;) {} return x / 0; }\nint h(int x) { goto out; x = x / 0; out: return 0; }\n"
          "unsigned i(int x, unsigned u) { if (0) return x < u; return 0; }\nchar j(void) { if (0) return 300; return 0; }\n"
          "int k(void) { if (0) return *(int *)0; return 0; }\n",
        // An error inside a body: the body's runtime warnings before it follow it, at the body's end.
        "int a(int x) { if (0) return x << 40; return 0; }\nint b(int x) { if (0) return x << 40; break; return 0; }\n"
          "int c(int x) { if (0) return x << 40; return 0; }\n",
        // A recovered attribute error (phase 1's), then a body's unreachable runtime warning.
        "__attribute__((nonnull(5))) void g(int *p);\nint s(int x) { if (0) return x << 40; return x; }\nint u(void) { int y; return y; }\n",
        // A graph's error, then a later statement's asm and control errors.
        "struct S { int a; } s;\nint f(void) { return s.b; }\nvoid a(int x) { __asm__(\"\" : \"=r\"(1)); }\n",
        "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(double x) { switch (x) { case 1: return 1; } return 0; }\n",
        // A graph's error in one of an asm statement's operands leaves its other operands checked.
        "struct S { int a; } s;\nvoid b(void) { __asm__(\"\" : : \"r\"(s.b), \"m\"(1)); }\n",
        // After a graph's error, every switch's conflicts, each noted at the case it repeats: the singletons in value
        // order, the ranges by their low values, a nested switch's at its own end, and the values in the condition's type.
        "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(int i)\n{\n"
        "    switch (i) { case 7: case 7: case 7: break; case 1 ... 3: case 2 ... 4: break; }\n"
        "    switch (i) { case 4: case 2: case 4: case 2: break; }\n"
        "    switch (i) { case 1: { switch (i) { case 2: case 2: break; } } case 1: break; }\n"
        "    switch (i) { case 1 ... 3: case 2 ... 4: case 3: case 0 ... 9: break; }\n"
        "    switch (i) { case 5: case 1 ... 9: case 7: break; }\n"
        "    switch (i) { case -1: case 0xffffffffu: break; }\n"
        "    switch ((unsigned char)i) { case 1: case 1: break; }\n    return 0;\n}\n",
        // The defaults, by clang's walk of the order it makes them in: a default labelling another is made first.
        "struct S { int a; } s;\nint f(void) { return s.b; }\nint g(int i, int y)\n{\n"
        "    switch (i) { default: break; default: break; default: default: break; }\n"
        "    switch (i) { default: { y = 1; default: y = 2; } }\n"
        "    switch (i) { default: case 1: default: y = 3; }\n    return y;\n}\n",
        // A condition of no integer type, by its typedef's name, a pointer's and a record's; and a case that reads the
        // error is still a case of its switch.
        "typedef double D;\nstruct S { int a; } s;\nint f(void) { return s.b; }\n"
        "int g(double x, D y, int *p) { switch (x) { case 1: return 1; } switch (y) { default: return 2; } switch (p) { default: return 3; }"
        " switch (s) { default: return 4; } return 0; }\n"
        "int h(int i) { switch (i) { case 1: return s.b; case 1: return 2; } return 0; }\n",
        // Without a graph's error too.
        "int g(int i, double x)\n{\n    switch (i) { case 2: case 2: break; }\n    switch (x) { case 1: break; }\n"
        "    switch (i) { default: default: break; }\n    return 0;\n}\n",
        // After a graph's error, each asm statement's first error in clang's words (an output's naming an input, and the
        // reverse, as clang's do), an incomplete type's with its forward declaration's note, a tie's after the
        // statement's other operands, and a cast's, which doesn't end its statement.
        "typedef float F;\nstruct S { int a : 3; int b; } s;\nstruct I;\nextern struct I inc;\nconst int ci = 0;\n_BitInt(8) bi;\n"
        "void v(void);\nint x;\nint f(void) { return s.c; }\nvoid a(void)\n{\n"
        "    __asm__(\"\" : \"=r\"(1));\n    __asm__(\"\" : \"=r\"(ci));\n    __asm__(\"\" : \"=m\"(s.a));\n"
        "    __asm__(\"\" : : \"m\"(s.a));\n    __asm__(\"\" : : \"m\"(1));\n    __asm__(\"\" : : \"r\"(v()));\n"
        "    __asm__(\"\" : : \"r\"(bi));\n    __asm__(\"\" : \"=r\"(bi));\n    __asm__(\"\" : \"=r\"(inc));\n"
        "    __asm__(\"\" : : \"m\"(inc));\n    __asm__(\"\" : \"=r\"(x) : \"0\"((F)1.0f));\n"
        "    __asm__(\"\" : \"=r\"(x), \"=r\"(2) : \"m\"(3));\n    __asm__(\"\" : \"=r\"((int)x), \"=r\"(1), \"=r\"((long)x));\n"
        "    __asm__(\"\" : \"=r\"(x) : \"0\"(1.0), \"m\"(2));\n    __asm__(\"\" : \"=r\"(((unsigned)(x))));\n}\n",
        // Without a graph's error too; a cast to void is no cast clang names, so its operand is an output's invalid
        // lvalue (review on #1795).
        "int x;\nvoid a(void)\n{\n    __asm__(\"\" : \"=r\"(1));\n    __asm__(\"\" : : \"m\"(1));\n"
        "    __asm__(\"\" : \"=r\"((long)x), \"=r\"(x) : \"0\"(1.0));\n"
        "    __asm__(\"\" : \"=r\"((void)x));\n    __asm__(\"\" : \"=r\"((void)(int)x), \"=r\"((int)x));\n"
        "    __asm__(\"\" : \"=r\"(x) : \"r\"((void)x));\n}\n",
        // A graph's error in one body: the others' common warnings, -Wuninitialized before it and none after it, and their
        // -Wreturn-type, runtime, -Wswitch, -Wformat and -Wempty-body warnings (#1447: a second reading without that body).
        "struct S { int a; } s;\n"
        "enum E { A, B, C };\n"
        "int printf(const char *, ...);\n"
        "int un0(void) { int y; return y; }\n"
        "int bad(void) { return s.b; }\n"
        "int after(int x) { if (x = 2) return x << 40; return 0; }\n"
        "void sw(enum E e) { switch (e) { case A: break; } }\n"
        "void fmt(int x) { printf(\"%s\\n\", x); }\n"
        "int ret(int x) { if (x) return 1; }\n"
        "int un(void) { int y; return y; }\n"
        "void empty(int x) { if (x); }\n",
        // A control error on the valid path: its scope's unused variable and the unit-end ones are left out, the other
        // bodies' common warnings given.
        "int printf(const char *, ...);\n"
        "int f(int x)\n"
        "{\n"
        "    int u;\n"
        "    switch (x) { case 1: case 1: break; }\n"
        "    return 0;\n"
        "}\n"
        "int g(int y) { if (y = 1) return printf(\"%d\\n\", 1.5); return 0; }\n"
        "static int z;\n"
        "void h(void) { int w; }\n",
        // An asm error's body left out, the next one's warnings given.
        "void a(void) { __asm__(\"\" : \"=r\"(1)); }\n"
        "int g(int y) { if (y = 1) return 1; return y << 40; }\n",
        // -Wreturn-type before a graph's error, and the next body's unused variable and -Wparentheses.
        "int r(int x) { if (x) return 1; }\n"
        "struct S { int a; } s;\n"
        "int bad(void) { return s.b; }\n"
        "int g(int y) { int w; if (y = 1) return 1; return 0; }\n",
        // A reader's error (an undeclared name), after which the other bodies' unused variables and common warnings are given.
        "int f(void){ return zz; }\n"
        "int g(int y){ if (y=1) return 1; int w; return 0; }\n"
        "int h(void) { return 1 << 40; }\n",
        // The same with bodies before it.
        "int g(int y){ int w; return y; }\n"
        "int f(void){ return zz; }\n"
        "int g2(int y){ int w2; return y; }\n",
        // A reader's error that ends its walk (a failed static assertion): the other bodies' reader warnings (deprecated uses,
        // unused variables), before the error and after it, and their common warnings, without the unit-end ones.
        "int printf(const char *, ...);\n"
        "int g __attribute__((deprecated));\n"
        "int pre(int x) { int q; return printf(\"%d\", 1.5) + x + g; }\n"
        "int f(void) { _Static_assert(0, \"x\"); return 0; }\n"
        "int post(int x) { int q; int a[2]; if (x = 1) return g; return a[3] + x + printf(\"%d\", 1.5); }\n"
        "static int unused_static;\n",
        // The same error in a body with a deprecated use, the next body's given.
        "int g __attribute__((deprecated));\n"
        "int f(void) { _Static_assert(0, \"x\"); return g; }\n"
        "int h(int x) { int w; if (x = 1) return g; return 0; }\n",
        // A graph's error mid-way in a body: the second reading leaves out its statement alone (`{` `}` in its place), so the
        // body's warnings before and after it are given, the body flushed; and a file-scope initializer's error, left out
        // of its declaration, before a later declaration's and body's warnings.
        "struct S { int a; } s;\n"
        "enum E { A, B };\n"
        "int printf(const char *, ...);\n"
        "int f(int x, unsigned u, enum E e)\n"
        "{\n"
        "    if (x = 1) x = 0;\n"
        "    if (x);\n"
        "    printf(\"%s\\n\", x);\n"
        "    switch (e) { case A: break; }\n"
        "    x = s.b;\n"
        "    if (x = 2) x = 0;\n"
        "    if (x);\n"
        "    printf(\"%d\\n\", 1.5);\n"
        "    int z = x << 40;\n"
        "    switch (e) { case A: break; }\n"
        "    if (x < u) return z;\n"
        "    return 0;\n"
        "}\n"
        "int g = s.b, h = 1 << 40;\n"
        "int after(int y) { if (y = 3) return 1; return 0; }\n",
        // Errors whose statement can't be left out alone: a condition's, a for header's, a statement expression's and a
        // const declaration's initializer's leave out their bodies, the others' warnings given.
        "struct S { int a; } s;\n"
        "int printf(const char *, ...);\n"
        "int cond(void) { if (s.b) return 1; return 0; }\n"
        "int loop(void) { int i; for (i = s.b; i < 2; i++) {} return i; }\n"
        "int expr(void) { return ({ int t = s.b; t; }); }\n"
        "int konst(void) { const int c = s.b; return c; }\n"
        "int other(int x) { if (x = 1) return printf(\"%d\\n\", 1.5); return x << 40; }\n",
        // A statement expression's last statement left out makes its value void, which the function's return can't take:
        // the view's reading isn't valid, so the error's body is left out instead, and the next body's warnings given.
        "struct S { int a; } s;\n"
        "int f(int x) { return ({ x = 1; x = s.b; }); }\n"
        "int ok(int y) { if (y = 5) return 1; return y << 40; }\n",
        // A block declaration's initializer left out, the variable kept.
        "struct S { int a; } s;\n"
        "int f(int x)\n"
        "{\n"
        "    int y = s.b;\n"
        "    if (x = 2) return y;\n"
        "    return 0;\n"
        "}\n",
        // A function holding an error keeps its unused parameters, as clang's: but one that its left-out part names, an
        // erroneous expression's operand or sizeof's, is used; a member's name of the same spelling isn't. The scope
        // holding an error loses its unused variables, the enclosing body's with it.
        "struct T { int a; };\n"
        "int f1(int a, int b) { int u; return a + undeclared; }\n"
        "int f3(int a, struct T t) { return t.a + nope; }\n"
        "int f8(int a) { return sizeof(a) + zz; }\n"
        "int f9(int a, int c) { int w; { int q = c + zz; } return 0; }\n"
        "int g(int y) { return 0; }\n"
        "int h(void) { int v; return 0; }\n",
        // A reader's error that ends the first reading's walk, which skips the reader's control scan too: a duplicate case
        // before it, which the second reading finds and gives, then leaves its body out of a third, which gives the warnings.
        "void k(int a) { switch (a) { case 1: case 1: break; } }\n"
        "int m(void) { _Static_assert(0, \"x\"); return 0; }\n"
        "int g(int y) { if (y = 1) return 1; int w; return 0; }\n",
        // The same with the control errors after it, in two bodies.
        "int m(void) { _Static_assert(0, \"x\"); return 0; }\n"
        "void k(int a) { switch (a) { case 1: case 1: break; } }\n"
        "void k2(int a) { switch (a) { default: break; case 2: case 2: break; default: break; } }\n"
        "int g(int y) { if (y = 1) return 1; int w; return 0; }\n",
        // Rounds: the second reading's walk ends at the second assertion, so the third reading finds the duplicate case,
        // and a fourth gives the warnings.
        "int m(void) { _Static_assert(0, \"x\"); return 0; }\n"
        "int n(void) { _Static_assert(0, \"y\"); return 0; }\n"
        "void k(int a) { switch (a) { case 1: case 1: break; } }\n"
        "int g(int y) { if (y = 1) return 1; int w; return 0; }\n",
        // A label's redefinition, which clang drops, keeping its statement, is recovered: the body's switch is checked on, its
        // duplicate case given. Clang defines a label as its statement completes, so of a label and one within its statement
        // (`L: L: ;`, `M: { M: ; }`, `P: Q: P: ;`) the outer is the redefinition; of two in sequence, the second.
        "void k(int a) { L: L: ; switch (a) { case 1: case 1; break; } }\n"
        "void m(void) { M: { M: ; } N: ; N: ; goto N; }\n"
        "void n(void) { P: Q: P: ; goto Q; }\n"
        "int g(int y) { if (y = 1) return 1; int w; return 0; }\n",
        // An _Alignof whose operand holds an undeclared name gives -Wgnu-alignof-expression after the name's error (#1946), an
        // expression's in parentheses and an inner sizeof's or _Alignof's too, but a type name's none.
        "int f(void) { return _Alignof(undecl); }\n"
        "int g(void) { return _Alignof((undecl2 + 1)); }\n"
        "int h(void) { return _Alignof(int[undecl3]); }\n",
        "int k(void) { return _Alignof(sizeof(undecl4)) + _Alignof(_Alignof(undecl5)); }\n",
        // A goto or a case that enters a statement expression, and one that bypasses a variable length array's, a cleanup
        // variable's or a VLA typedef's initialization: clang's jump scope errors, each with its note (#1942).
        "void f(int x) { goto in; x = ({ in: 1; }); }\n",
        "void c(int *p);\n"
        "void f(int n) { goto in; int a[n]; in: ; }\n"
        "void g(void) { goto in; int b __attribute__((cleanup(c))) = 0; (void)b; in: ; }\n"
        "void h(int n) { switch (n) { int a[n]; case 1: ; } }\n"
        "void k(int n) { goto in; typedef int T[n]; in: ; }\n",
        // A case label a switch may not jump to is still its case, and its report cuts nothing: the assignment of the
        // statement expression's void value keeps its error (#1942).
        "int f(int x) { int y = 0; switch (x) { case 0: y = ({ int z; }); y = ({ case 1: 2; }); } return y; }\n"
        "int h(int x) { switch (x) { case 0: x = ({ case 5: 1; }) + \"s\"; } return x; }\n",
        // A computed goto and an asm goto into a variable length array's, a VLA typedef's or a cleanup variable's scope:
        // clang's notes at a possible target's label and the declaration it bypasses (#1942).
        "void use(int);\nvoid cl(int *);\n"
        "void f(int n) { void *p = &&l1; goto *p; { int a[n]; l1: use(a[0]); } }\n"
        "void g(int n) { asm goto(\"\" :::: l2); { int b[n]; l2: use(b[0]); } }\n"
        "void h(int n) { void *p = &&l3; goto *p; { typedef int T[n]; l3: use(sizeof(T)); } }\n"
        "void k(void) { void *p = &&l4; goto *p; { int c __attribute__((cleanup(cl))) = 0; l4: use(c); } }\n"
        "void m(int n) { void *q = &&l5; { int a[n]; goto *q; } l5: ; }\n",
        // An undeclared name in a type name's bound ends the reader's walk; the rounds find the later body's error, under -w
        // too (#2162).
        "int f(void) { return _Alignof(int[undecl3]); }\n"
        "int k(void) { return d; }\n",
        // A declaration constant's error inside a body (#2120): the view leaves out the declaration and its uses, and its
        // reading's later errors, in statements it keeps whole, are the unit's, in each round.
        "int k(int b, ...);\n"
        "int h(int a);\n"
        "void f(void)\n"
        "{\n"
        "    typeof(k()) u;\n"
        "    typeof(k()) w;\n"
        "    (void)u; (void)w;\n"
        "    (void)sizeof(h(1, 2));\n"
        "    int unused;\n"
        "}\n",
        // A statement expression's left-out last statement makes its value void in the view, whose error isn't the unit's;
        // a later statement's is.
        "int k(int b, ...);\n"
        "int h(int a);\n"
        "int a[sizeof(k())];\n"
        "void f(void)\n"
        "{\n"
        "    int x = ({ typeof(k()) v; 1; });\n"
        "    int y = ({ int z = k(); });\n"
        "    h(1, 2);\n"
        "    (void)(x + y);\n"
        "}\n",
        // A file-scope array initialized by a compound literal whose body holds a value that is no constant (#1812): the
        // literal's error once at its place, and no mismatch of the array's type with the literal's, which clang's invalid
        // literal leaves out; a valid body's mismatch is still given.
        "int a[] = (int[2]){\"x\", 1};\n"
        "int g;\n"
        "int b[] = (int[2]){&g, 1};\n"
        "const int q[] = (int[2]){\"x\", 1};\n"
        "int h;\n"
        "long u[] = (int[2]){h, 1};\n"
        "long s[] = (int[2]){\"x\", 1};\n"
        "const int t[] = (int[2]){2, 1};\n",
        // Warnings that default to errors (#1969): a conversion between an integer and a pointer or between pointers, and a
        // return's missing or extra value. Clang's scopes don't count them (DiagnosticIDs::isUnrecoverable), so their unused
        // variables are given; the view casts a conversion's source, so its statement's warnings are given too.
        "int f(short q) { char *p = &q; return 0; }\n",
        "int a(int *p) { int x; x = p; return x / 0; }\n"
        "int b(int *p) { int x = p + (1 / 0); return x; }\n"
        "int c(short q) { int u; char *p = &q; return *p << 40; }\n"
        "int z3(void) { int u; return; }\n"
        "void v(void) { int u; return 1; }\n"
        "void t(int *); int d(void) { int u; t(5); return 0; }\n"
        "int *e(int n) { int w; return n << 40; }\n"
        "void (*fp)(int); void g0(void); int g(void) { int u; fp = g0; return 0; }\n",
        // The same in a unit with an undeclared name, whose bodies' unused variables come from the view's reading.
        "int f(short q) { char *p = &q; return 0; }\n"
        "int z3(void) { int u; return; }\n"
        "int k(int *p) { int q; q = p; return q + (1 << 40); }\n"
        "int m(void) { int u; return undeclared; }\n"
        "int n(int *p) { int u; int x = p; return x / 0; }\n",
        // An array compound literal's conversion: the view's cast starts at its type name's `(`, not its body's `}`, so the
        // function's later warnings stay (#2146 with #2132).
        "int printf(const char *, ...);\n"
        "void f(int n) { char *b = (int[2]){ 1, 2 }; printf(\"%s\\n\", n); }\n"
        "void g(int n) { int u; char *e = (long[]){ 1, 2, 3 }; long *l = (int[1]){ 0 }; printf(\"%s\\n\", n); }\n"
        "void h(int n) { int w = (struct { int x; }){ 1 }; char *a = (int[2]){ 1, 2 }; if (n < (unsigned){ 4 }) {} }\n",
        // A left-out statement's complete operands, kept as `(void)(A);` (#2120): the warnings clang gives as it builds them,
        // before its error or after it, a call's -Wformat, a comma's -Wunused-value and -Wself-assign, and an erroneous
        // call's arguments'.
        "int printf(const char *, ...);\nstruct S { int a; } s;\nint k(int, int);\nvoid f(int x, int *p)\n{\n"
        "    x = printf(\"%s\", x) + s.b;\n    x = s.b + printf(\"%s\", x);\n    k(1, (2, 3), 4);\n    p + (2, 3.0);\n"
        "    x = (x = x) + s.b;\n}\n",
        // Through a cast, a call, a subscript and a conditional; not in sizeof; a statement-level comma, a for's body and a
        // return; and no flow analysis's warning (`u`).
        "typedef int T;\nint g(int);\nstruct S { int a; } s;\nint f(int x, int *p)\n{\n    int u;\n    x = (T)s.b + (x = x);\n"
        "    x = g(s.b) + (1, 2);\n    x = p[s.b] + p[(1, 2)];\n    x = x ? (3, 4) : s.b;\n    x = sizeof(s.b + (1, 2));\n"
        "    x = u + s.b;\n    x = (1, 2), s.b;\n    for (;;) x = s.b + (1, 2);\n    return (1, 2) + s.b;\n}\n"
        "int h(int x) { x = undeclared + (1, 2); return x << 40; }\n",
        // A static initializer that is only no constant keeps its warnings (#1447): the view keeps it, its error first and
        // then its division's at the declarator's end, in a unit of no body too, a later declarator's, a record's and an
        // array's.
        "const int d = 1 / 0;\nconst int a = 2, b = 1 / 0, c = 3;\nstruct S { int a, b; } s = { 1 / 0, 2 };\nint arr[2] = { 1, 1 / 0 };\n",
        // Its uses stay bound, a block's static's too, and an undeclared name's body is another error.
        "const int d = 1 / 0;\nint use(void) { int u; return d; }\nint f(void) { return undeclared; }\nint e = 1 % 0;\n"
        "void g(void) { static int r = (1, 2) + 1 / 0; int w; (void)r; }\n",
    };
    static const char* const standards[] = { "-std=gnu17", "-std=gnu23" };
    // Each run's options: none, one or two.
    static const char* const options[][2] = { { NULL, NULL }, { "-Wall", NULL }, { "-Wall", "-Wextra" }, { "-ferror-limit=2", NULL }, { "-w", NULL } };
    enum { LINES = 16384 };
    char* expected_lines = malloc(LINES);
    char* actual_lines = malloc(LINES);
    char source[PATH_MAX];
    char object[PATH_MAX];
    snprintf(object, sizeof(object), "%s/statement-recovery.o", directory);
    for (uint32_t f = 0; expected_lines != NULL && actual_lines != NULL && f < BUSTER_ARRAY_LENGTH(fixtures); f += 1)
    {
        snprintf(source, sizeof(source), "%s/statement-recovery-%u.c", directory, f);
        const bool made = parse_test_write(source, fixtures[f], strlen(fixtures[f]));
        for (uint32_t s = 0; made && s < BUSTER_ARRAY_LENGTH(standards); s += 1)
        {
            for (uint32_t o = 0; o < BUSTER_ARRAY_LENGTH(options); o += 1)
            {
                char* arguments[12] = { (char*)clang, (char*)standards[s], "-fsyntax-only", "-fno-caret-diagnostics", source };
                uint32_t count = 5;
                for (uint32_t a = 0; a < 2 && options[o][a] != NULL; a += 1)
                {
                    arguments[count++] = (char*)options[o][a];
                }
                arguments[count] = NULL;
                char* expected = NULL;
                const int oracle = check_test_driver_spawn(arguments, &expected);
                const bool lines = expected != NULL && check_test_diagnostic_lines(expected, expected_lines, LINES);
                check_test_expect(test, lines && oracle != 0, "statement recovery: clang rejects the fixture and its lines are read");
                for (uint32_t mode = 0; lines && mode < 3; mode += 1)
                {
                    arguments[0] = (char*)executable;
                    uint32_t at = count;
                    if (mode == 1)
                    {
                        arguments[at++] = "--scalar";
                    }
                    else if (mode == 2)
                    {
                        arguments[at++] = "--deferred-bodies";
                        arguments[at++] = "--lanes";
                        arguments[at++] = "2";
                    }
                    arguments[at] = NULL;
                    char* actual = NULL;
                    const int status = check_test_driver_spawn(arguments, &actual);
                    const bool same = actual != NULL && check_test_diagnostic_lines(actual, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
                    char description[8192];
                    snprintf(description, sizeof(description), "statement recovery %u, %s %s %s, mode %u: status %d (clang %d), lines\n%s(clang\n%s)", f, standards[s],
                             options[o][0] != NULL ? options[o][0] : "", options[o][1] != NULL ? options[o][1] : "", mode, status, oracle, actual_lines, expected_lines);
                    check_test_expect(test, same && (status != 0) == (oracle != 0), description);
                    free(actual);
                }
                free(expected);
            }
        }
        // -c is refused, and writes no object, by both paths, on the unit's own reading and on real lanes (deferred bodies on
        // one, two and three lanes, which -c starts), whose diagnostics are clang's, whole, as -fsyntax-only's are.
        char* reference_arguments[] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", source, NULL };
        char* reference = NULL;
        const int reference_status = made ? check_test_driver_spawn(reference_arguments, &reference) : 0;
        const bool referenced = made && reference_status != 0 && reference != NULL && check_test_diagnostic_lines(reference, expected_lines, LINES);
        check_test_expect(test, !made || referenced, "statement recovery: clang's lines for -c's comparison are read");
        static const char* const lane_counts[] = { NULL, "1", "2", "3" };
        for (uint32_t run = 0; referenced && run < 2 * BUSTER_ARRAY_LENGTH(lane_counts); run += 1)
        {
            const uint32_t scalar = run & 1;
            const char* lane_count = lane_counts[run / 2];
            unlink(object);
            char* arguments[12] = { (char*)executable, "-std=gnu17", "-fno-caret-diagnostics", "-c", source, "-o", object };
            uint32_t at = 7;
            if (scalar != 0)
            {
                arguments[at++] = "--scalar";
            }
            if (lane_count != NULL)
            {
                arguments[at++] = "--deferred-bodies";
                arguments[at++] = "--lanes";
                arguments[at++] = (char*)lane_count;
            }
            arguments[at] = NULL;
            char* text = NULL;
            const int status = check_test_driver_spawn(arguments, &text);
            const bool written = access(object, F_OK) == 0;
            const bool same = text != NULL && check_test_diagnostic_lines(text, actual_lines, LINES) && strcmp(expected_lines, actual_lines) == 0;
            char description[8192];
            snprintf(description, sizeof(description), "statement recovery %u, -c by the %s path on %s lanes: refused (status %d), no object, clang's lines\n%s(clang\n%s)",
                     f, scalar != 0 ? "scalar" : "vector", lane_count != NULL ? lane_count : "no", status, same ? "" : actual_lines, same ? "" : expected_lines);
            check_test_expect(test, status != 0 && !written && same, description);
            free(text);
        }
        free(reference);
        check_test_expect(test, made, "statement recovery: fixture written");
        unlink(source);
    }
    // A reader's error that ends the first reading's walk (a negative array size), before the unit's pragma diagnostics:
    // the second reading publishes the walk's reports and warnings from there on, but not the pragmas' records and
    // warnings, which the first reading gave whole. The error's own words are not yet clang's, so the other lines are
    // compared, which must each be given once, as clang's.
    // Two sources: the pragmas' records (visibility, floating point) and their warnings (#pragma weak's), apart, as
    // the order of the two families' diagnostics at one place isn't yet clang's.
    static const char* const walk_ended[] = {
        "int f0(void) { int a[-1]; return 0; }\n"
        "#pragma GCC visibility push(bogus)\n"
        "#pragma float_control(pop)\n"
        "int g(int x) { int w; if (x = 1) return x; return 0; }\n",
        "int f0(void) { int a[-1]; return 0; }\n"
        "#pragma weak\n"
        "int g(int x) { int w; if (x = 1) return x; return 0; }\n",
    };
    for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(walk_ended); w += 1)
    {
        snprintf(source, sizeof(source), "%s/statement-recovery-pragmas.c", directory);
        const bool pragmas_made = expected_lines != NULL && actual_lines != NULL && parse_test_write(source, walk_ended[w], strlen(walk_ended[w]));
        check_test_expect(test, pragmas_made, "statement recovery: the pragma fixture is written");
        for (uint32_t mode = 0; pragmas_made && mode < 3; mode += 1)
        {
            char* arguments[12] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-Wall", source, NULL };
            char* texts[2] = { NULL, NULL };
            char* lines[2] = { expected_lines, actual_lines };
            bool read = true;
            for (uint32_t side = 0; side < 2; side += 1)
            {
                uint32_t at = 6;
                arguments[0] = side == 0 ? (char*)clang : (char*)executable;
                if (side == 1 && mode == 1)
                {
                    arguments[at++] = "--scalar";
                }
                else if (side == 1 && mode == 2)
                {
                    arguments[at++] = "--deferred-bodies";
                    arguments[at++] = "--lanes";
                    arguments[at++] = "2";
                }
                arguments[at] = NULL;
                const int status = check_test_driver_spawn(arguments, &texts[side]);
                read = read && status != 0 && texts[side] != NULL && check_test_diagnostic_lines(texts[side], lines[side], LINES);
                // The lines without the errors'.
                size_t kept = 0;
                for (char* line = lines[side]; read && *line != 0;)
                {
                    char* end = strchr(line, '\n');
                    const size_t size = end != NULL ? (size_t)(end - line) + 1 : strlen(line);
                    if (memmem(line, size, ": error: ", 9) == NULL)
                    {
                        memmove(lines[side] + kept, line, size);
                        kept += size;
                    }
                    line += size;
                }
                if (read)
                {
                    lines[side][kept] = 0;
                }
            }
            const bool same = read && strcmp(expected_lines, actual_lines) == 0;
            char description[8192];
            snprintf(description, sizeof(description), "statement recovery: pragma diagnostics past a walk-ending error %u, mode %u, once each:\n%s(clang\n%s)", w, mode,
                     read ? actual_lines : "", read ? expected_lines : "");
            check_test_expect(test, same, description);
            free(texts[0]);
            free(texts[1]);
        }
        unlink(source);
    }
    // Rounds past the error limit's cut (BUSTER_CHECK_DRIVER_REJECTED_ROUNDS, #1447): each body's initialized incomplete object ends its
    // reading's walk (an uninitialized one is an invalid declaration that recovers), so the first reading gives the first error and each round one more, until clang's error limit cuts
    // the output; the bodies after the cut are left out, and the warnings before it are given. The whole output, by
    // default, at a limit inside the errors, with -Wfatal-errors and without a limit, by the vector path, the scalar
    // one and deferred bodies on two lanes.
    static const char capped[] =
        "void m1(void) { struct S1 s = {0}; }\nvoid m2(void) { struct S2 s = {0}; }\nvoid m3(void) { struct S3 s = {0}; }\n"
        "void m4(void) { struct S4 s = {0}; }\nvoid m5(void) { struct S5 s = {0}; }\nvoid m6(void) { struct S6 s = {0}; }\n"
        "int g(int y) { if (y = 1) return 1; int w; return 0; }\n"
        "int h(int x) { x = x; struct S7 s = {0}; return x = x; }\n";
    static const char* const limits[] = { NULL, "-ferror-limit=3", "-Wfatal-errors", "-ferror-limit=0" };
    snprintf(source, sizeof(source), "%s/statement-recovery-capped.c", directory);
    const bool capped_made = expected_lines != NULL && actual_lines != NULL && parse_test_write(source, capped, sizeof(capped) - 1);
    check_test_expect(test, capped_made, "statement recovery: the capped fixture is written");
    for (uint32_t l = 0; capped_made && l < BUSTER_ARRAY_LENGTH(limits); l += 1)
    {
        for (uint32_t mode = 0; mode < 3; mode += 1)
        {
            char* arguments[12] = { (char*)clang, "-std=gnu17", "-fsyntax-only", "-fno-caret-diagnostics", "-Wall", source, NULL };
            char* texts[2] = { NULL, NULL };
            char* lines[2] = { expected_lines, actual_lines };
            int statuses[2] = { 0, 0 };
            bool read = true;
            for (uint32_t side = 0; side < 2; side += 1)
            {
                uint32_t at = 6;
                arguments[0] = side == 0 ? (char*)clang : (char*)executable;
                if (limits[l] != NULL)
                {
                    arguments[at++] = (char*)limits[l];
                }
                if (side == 1 && mode == 1)
                {
                    arguments[at++] = "--scalar";
                }
                else if (side == 1 && mode == 2)
                {
                    arguments[at++] = "--deferred-bodies";
                    arguments[at++] = "--lanes";
                    arguments[at++] = "2";
                }
                arguments[at] = NULL;
                statuses[side] = check_test_driver_spawn(arguments, &texts[side]);
                read = read && texts[side] != NULL && check_test_diagnostic_lines(texts[side], lines[side], LINES);
            }
            const bool same = read && statuses[0] != 0 && statuses[1] != 0 && strcmp(expected_lines, actual_lines) == 0;
            char description[8192];
            snprintf(description, sizeof(description), "statement recovery: a unit of many rounds, %s, mode %u:\n%s(clang\n%s)",
                     limits[l] != NULL ? limits[l] : "the default limit", mode, read ? actual_lines : "", read ? expected_lines : "");
            check_test_expect(test, same, description);
            free(texts[0]);
            free(texts[1]);
        }
    }
    unlink(source);
    unlink(object);
    free(expected_lines);
    free(actual_lines);
}
