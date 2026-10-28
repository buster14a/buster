// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-checker-deferred: #113's deferred bodies (c_check_deferred.c) on fixtures of the issue's cases. Each fixture is
// written to a file and checked as test-checker-corpus checks one (check_corpus_measure): on the scalar path, the
// vector path, and the vector path with every body read apart, which must give the vector path's results exactly,
// column by column, its diagnostics among them. It runs on 1, 2, 3 and 8 lanes through the stacks, the batch and the
// direct binding resolution, on every lane the bodies can take (CheckDeferred.every_lane), and each fixture says
// whether the deferred reader must merge it or leave it to source order. A merged unit's bodies must run on that many
// lanes, its reader be copied by one of them at least and by no more (#432's review), and its parts be as many as a
// group of them cuts. Each fixture is read again on 8 lanes by the reader's rule of how many (one lane, and one more per
// BUSTER_CHECK_DEFERRED_LANE_TOKENS, and one per BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS of the top level's tokens, of
// the bodies' tokens beyond the larger of the largest body's and twice the second largest's), and eight of bodies of
// known tokens are read by the rule alone: on each side of a second lane and of an
// eighth, beside a largest body, which the first lane reads itself, and beside a second largest, which a woken lane
// reads in about twice its tokens' time; their largest body must be the first lane's, also on every lane with the first
// lane starting once the others have taken from the counter (CheckDeferred.late_first). A unit that
// merges, but the rule's sides, is checked again with its first lane taking every part, when no other lane may copy the
// reader; with its merge failing after the replay, which must give source order's result; on 8 lanes by a state that
// read it before, on one lane and on eight, whose memory the reading takes over, poisoned or as it was left; and with the
// allocation of the deferred reader's state failing, which must count as a resource failure, not as a result that
// differs. The many bodies are read again with the reader timing what its lanes cost (CheckDeferred.timing), on one
// lane, which must wait for no other, also inside a running pinned team with an unfinished take and impossible pin
// target, and on 8. The caller's context and running state, its installed team and worker identities, and its affinity
// must survive; the entry's dispatch and the wake-up must fall within the lanes' span. Then
// benchmark-checker-deferred's --cpus parser, the difference report on a deferred path that made no input, which must
// say so rather than read its columns, and the state's memory: a lane's arenas in one mapping, kept across readings and
// unmapped by check_deferred_release.
//
//   ./build/compiler test-checker-deferred [--keep]
//
// Include after c_check_corpus.c.

typedef struct CheckDeferredFixture CheckDeferredFixture;
struct CheckDeferredFixture
{
    const char* name;
    const char* source;
    // Whether the unit is merged (false: it does what only source order can merge).
    bool merged;
    // The fixture's -std= option, -std=gnu17 when 0.
    char* standard;
};

static const CheckDeferredFixture check_deferred_fixtures[] =
{
    { "nested-selection-warnings",
      "#define C(X) __builtin_choose_expr(1,0,(X))\n"
      "#define N C(C(C(C(C(C(C(C(C(C(C(C((2147483647+1)))))))))))))\n"
      "int f(void){enum {A=N}; return A;} int g(void){enum {B=N}; return B;}\n"
      "int h(void){return _Generic(N,int:0,default:1);}\n", true },
    { "naked-valid-bodies", "__attribute__((naked)) void f(int n) { ; __asm__(\"ret\"); }\nint g(int n) { return n; }\n", true },
    { "naked-invalid-statement", "__attribute__((naked)) void f(void) { int x=1; __asm__(\"ret\"); }\nint g(int n) { return n; }\n", true },
    { "naked-parameter-reference", "__attribute__((naked)) void f(int n) { __asm__(\"\" : : \"r\"(n)); }\nint g(int n) { return n; }\n", true },
    { "naked-inherited-attribute", "void f(void) __attribute__((naked));\nvoid f(void) { return; }\nint g(int n) { return n; }\n", true },
    { "naked-vla-type-operand", "__attribute__((naked)) void f(int n) { __asm__(\"\" : : \"r\"(sizeof(int[n]))); }\nint g(int n) { return n; }\n", true },
    { "naked-vla-type-only-operand", "__attribute__((naked)) void f(int n) { __asm__(\"\" : : \"i\"(sizeof(int[sizeof n]))); }\nint g(int n) { return n; }\n", true },
    { "naked-inline-register-types", "__attribute__((naked)) void f(void) { register struct { int a; } x asm(\"rax\"); __asm__(\"ret\"); }\n"
      "__attribute__((naked)) void g(void) { register enum { A=1 } x asm(\"rax\"); __asm__(\"ret\"); }\n", true },
    { "naked-interleaved-bodies", "__asm__(\".text\");\n__attribute__((naked)) void f(int n) { ; __asm__(\"ret\"); }\n"
      "void g(int n) { __asm__(\"\" : : \"r\"(n)); }\n__attribute__((naked)) void h(int n) { __asm__(\"\" : : \"i\"(3)); }\n", true },
    { "loop-hints", "int f(int n) {\n#pragma clang loop unroll_count(4)\n while (n--) {} return n; }\nint g(int n) {\n#pragma nounroll\n do n--; while(n); return n; }\n", true },
    { "loop-conflict", "void f(int n) {\n#pragma clang loop unroll_count(4) unroll(full)\n while(n--) {} }\n", false },
    { "loop-conflict-type-error", "void f(int n) {\n#pragma clang loop unroll_count(4) unroll(full)\n while(n--) { *n = 1; } }\n", false },
    { "loop-placement", "void f(int n) {\n#pragma unroll\n n = 1; }\n", false },
    { "loop-conflict-control-error", "void f(int n) {\n#pragma clang loop unroll(full) unroll_count(4)\n while(n--) { L:; L:; } }\n", false },
    // #619: a unit with #pragma clang section is source order's (its settings and conflicts read the walk's declarations in
    // order), as is one whose body has a section attribute (a lane refuses its section event); #pragma clang optimize
    // changes nothing a body's reading does, at the top level or in a body.
    { "section-pragma", "#pragma clang section text = \".x\"\nint f(void) { return 1; }\n#pragma clang section text = \"\"\nint g(void) { return 2; }\n", false },
    { "section-attribute-in-a-body", "int f(void) { static int s __attribute__((section(\".x\"))) = 1; return s; }\nint g(void) { return 2; }\n", false },
    { "optimize-pragma", "#pragma clang optimize off\nint f(void) { return 1; }\n#pragma clang optimize on\nint g(void) { return 2; }\n", true },
    { "optimize-pragma-in-a-body", "int f(void) {\n#pragma clang optimize off\n return 1; }\nint g(void) { return 2; }\n", true },
    // The issue's three: a later declaration and a later completion aren't seen by an earlier body, and an
    // initializer's enumerator is.
    { "later-declaration", "int f(void) { return later; }\nint later = 7;\n", true },
    { "later-completion", "struct S;\nint f(void) { return sizeof(struct S); }\nstruct S { int x; };\n", true },
    { "enumerator-from-initializer", "int x = sizeof(enum { A = 1 });\nint y = A;\nint f(void) { return A + x + y; }\n", true },
    // A typedef used as a type in a body and shadowed by an object in a later one, and one redeclared later.
    { "typedef-shadowed-later", "typedef int T;\nint f(void) { T x = 2; return x; }\nint g(void) { int T = 3; return T; }\nint h(void) { T y = 4; return y; }\n", true },
    // The top level reports the redeclaration, and its walk stops there, as source order's: source order's.
    { "typedef-redeclared-later", "typedef int T;\nint f(void) { T x = 2; return x; }\nint T;\n", false },
    // Tags completed, redeclared and shadowed around bodies.
    { "tags-around-bodies",
      "struct A;\nint f(struct A* a) { return a != 0; }\nstruct A { int x; };\n"
      "int g(struct A* a) { struct A { long y; } b = { 3 }; return a->x + (int)b.y + (int)sizeof(struct A); }\n"
      "int h(void) { return (int)sizeof(struct A); }\nenum E { E0, E1 };\nint k(void) { enum E { E2 = 5 } e = E2; return e + E1; }\n", true },
    { "tag-completed-after-use", "struct L;\nstruct L* make(void);\nint f(void) { struct L* l = make(); return l == 0; }\nstruct L { struct L* next; };\n"
      "int g(struct L* l) { return l->next == 0; }\n", true },
    // A tag completed between bodies, which the walk itself needs complete or not: a body's table has the top level's
    // completions after its opening withdrawn, and those before it given back, whichever body its lane read before.
    { "incomplete-local-object", "struct S;\nint f(void) { struct S s; return 0; }\nstruct S { int x; };\nint g(void) { struct S s = { 1 }; return s.x; }\n", false },
    { "records-completed-between-bodies",
      "struct S;\nint f(void) { struct L { int n; struct S* s; } l = { 0, 0 }; return l.n + (int)sizeof(struct L); }\nstruct S { int x; };\n"
      "int g(void) { struct M { struct S s; } m = { { 3 } }; return m.s.x; }\n", true },
    { "enum-completed-between-bodies", "enum E;\nint f(void) { enum E* e = 0; return e != 0; }\nenum E { A, B };\nint g(void) { enum E e = B; return e; }\n", true },
    { "typedef-of-a-later-completion",
      "struct S;\nint f(void) { typedef struct S T; T* t = 0; return t != 0; }\nstruct S { int x; };\nint g(void) { typedef struct S T; T t = { 2 }; return t.x; }\n", true },
    // A tag first named in a file-scope declarator's parameter list (a prototype's, a definition's) ends with the list in
    // source order's reader, whose scope is deeper than the file scope the history holds: a later body that names the
    // tag makes one of its own. One that an array's bound names is the file scope's, which later bodies see, as is the
    // last fixture's, which the file scope declares first.
    { "tag-in-a-prototype", "void g(struct S *p);\nint f(void) { struct S *q = 0; return q != 0; }\n", true },
    { "tag-in-a-prototype-then-passed", "void g(struct S *p);\nvoid f(void) { struct S *q = 0; g(q); }\n", true },
    { "tag-in-a-prototype-two-bodies", "void g(struct S *p);\nint f1(void) { struct S *q = 0; return q != 0; }\nint f2(void) { struct S *r = 0; return r != 0; }\n", true },
    { "tag-in-an-array-bound", "int a[sizeof(struct X *)];\nint f(void) { struct X *q = 0; return q != 0; }\n", true },
    { "tag-in-a-definitions-parameters", "int h(struct T *p) { struct T *q = p; return q != 0; }\n", true },
    { "tag-declared-before-a-prototype", "struct S;\nvoid g(struct S *p);\nint f(void) { struct S *q = 0; return q != 0; }\n", true },
    // Block-scope declarations with linkage name the file-scope entity, which later declarations see. A lane takes one
    // that leaves the entity as it was: declared before the body, of a type the body's declaration doesn't change, its
    // linked declaration replaced only by one that later ones inherit the same from. The state the lane checks against is
    // source order's at the opening, whatever the top level declares later (a definition after the body, of an
    // array with a size). The first declaration of an entity, and one that adds to its type, are source order's.
    { "block-scope-extern", "int g = 3;\nint f(void) { extern int g; return g; }\n", true },
    { "block-scope-extern-before-its-definition", "extern int g;\nint f(void) { extern int g; return g; }\nint g = 3;\n", true },
    { "block-scope-prototype-before-its-definition", "int h(int);\nint f(void) { int h(int); return h(1); }\nint g(void) { int h(int); return h(2); }\n"
      "int h(int x) { return x; }\n", true },
    { "block-scope-function-after-its-definition", "int f1(char *p) { return *p + 1; }\nint main(void) { char s = 1; int f1(char *); return f1(&s) != 2; }\n", true },
    { "block-scope-array-before-its-size", "extern int a[];\nint f(void) { extern int a[]; return a[0]; }\nint a[4] = { 7 };\n", true },
    { "block-scope-function", "int f(void) { int h(int); return h(1); }\nint h(int x) { return x; }\n", false },
    { "visibility-extern-source-order", "int shared;\n#pragma GCC visibility push(hidden)\nint hidden;\n"
      "int f(void){extern int shared;return shared;}\nint g(void){extern int shared;return shared;}\n#pragma GCC visibility pop\n", false },
    { "visibility-attribute-extern-source-order", "int shared;\n"
      "int f(void){extern int shared __attribute__((visibility(\"hidden\")));return shared;}\nint g(void){return shared;}\n", false },
    { "visibility-locals-still-merge", "#pragma GCC visibility push(hidden)\n"
      "int f(void){int local=3;return local;}\nint g(void){int local=7;return local;}\n#pragma GCC visibility pop\n", true },
    { "implicit-functions-source-order", "typedef __typeof__(top_missing()) T;\nint f(void){return first_missing();}\n"
      "int g(void){return second_missing();}\nint h(void){return third_missing();}\n", false },
    { "block-scope-extern-first", "int f(void) { extern int e; return e; }\nint g(void) { extern int e; return e + 1; }\n", false },
    { "block-scope-extern-adds-a-size", "extern int a[];\nint f(void) { extern int a[3]; return (int)sizeof a; }\nint g(void) { return 2; }\n", false },
    { "block-scope-extern-conflicts", "int g = 3;\nint f(void) { extern long g; return (int)g; }\n", false },
    { "block-scope-prototype-adds-parameters", "int h();\nint f(void) { int h(int); return h(1); }\nint h(int x) { return x; }\n", false },
    // A naked prototype's later declarations inherit the attribute: a block's declaration that inherits it merges, and
    // one that adds it, which the later definition inherits in source order, is source order's.
    { "block-scope-declaration-inherits-naked", "__attribute__((naked)) void n(void);\nint f(void) { void n(void); n(); return 0; }\n"
      "void n(void) { __asm__(\"ret\"); }\n", true },
    { "block-scope-declaration-adds-naked", "void n(void);\nint f(void) { __attribute__((naked)) void n(void); return 0; }\n"
      "void n(void) { __asm__(\"ret\"); }\n", false },
    // What a body appends to the reader's tables: strings and __func__, statement expressions, VLAs, varargs, asm
    // statements, cached constants, compound literals and statics.
    { "strings-and-function-names", "const char* top = \"top\";\nconst char* f(void) { return __func__; }\nconst char* g(void) { const char* s = \"body\"; return s + sizeof __func__; }\n", true },
    {
        "string-initializer-indices",
        "const char* before[]={\"first\",\"second\"};\n"
        "const char* f(int i){static const char* t[]={\"body\" \" one\",\"body two\",__func__};"
        "_Static_assert(sizeof \"condition\"==10,\"message\" \" tail\");return t[i];}\n"
        "char between[][4]={\"ab\",(\"cd\")};\n"
        "const char* g(int i){struct Row{char text[4];const char* p;};"
        "static struct Row t[]={{\"xy\",\"body three\"},{\"za\",\"body four\"}};return t[i].p;}\n"
        "const char* after[]={\"last\",\"final\"};\n",
        true,
    },
    { "statement-expressions", "int f(int a) { int r = ({ int t = a + 1; t * 2; }); return r + ({ a; }); }\nint g(int b) { return ({ int u = b; u; }); }\n", true },
    { "variable-length-arrays", "int f(int n, int a[n]) { int b[n]; b[0] = a[0]; return (int)sizeof b + b[0]; }\nint g(int m) { int c[m][2]; return (int)sizeof c; }\n", true },
    { "vla-header-spans",
      "void proto(int n, int a[n]);\n"
      "int f(int n, int a[n][n+1], int (*callback)(int m, int b[m]), int z[n+2]) { typedef int A[n]; A a0; { A a1; int b[n+2]; } return sizeof a0; }\n"
      "int g(int m, int (*p)[m+2]) { __typeof__(int[m+3]) a, b; return sizeof a + sizeof b + sizeof *p; }\n"
      "int h(int k, int a[k]) { typedef int A[k+1]; A b; return sizeof b; }\n", true },
    { "varargs",
      "int sum(int count, ...) { __builtin_va_list list; __builtin_va_start(list, count); int total = 0;\n"
      "  for (int i = 0; i < count; i += 1) total += __builtin_va_arg(list, int); __builtin_va_end(list); return total; }\n"
      "int g(void) { return sum(2, 1, 2); }\n", true },
    { "predefined-type-first-in-a-body", "int f(void) { __builtin_va_list a; (void)a; return 0; }\n__builtin_va_list top;\nint g(void) { __builtin_va_list b; (void)b; return 1; }\n", true },
    { "asm-statements", "int f(int x) { __asm__ volatile(\"\" : \"+r\"(x)); return x; }\nint g(int y) { __asm__(\"\" :: \"r\"(y)); return y; }\n", true },
    { "constants-and-generic",
      "enum { N = 4 };\nint table[N * 2];\nint f(int x) { switch (x) { case N: return 1; case N + 1: return 2; default: return _Generic(x, int: 3, default: 4); } }\n"
      "int g(void) { char c[sizeof(int) + N]; return (int)sizeof c; }\n", true },
    { "compound-literals-and-statics", "int* f(void) { static int s = 3; int* p = (int[]){ 1, 2, 3 }; return p[0] ? &s : 0; }\nint* g = (int[]){ 4, 5 };\n", true },
    // A body's own name, a function declared before and defined after, and empty bodies.
    { "recursion-and-forward-calls", "static int later(int);\nint fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }\nint f(void) { return later(2); }\n"
      "static int later(int x) { return x + 1; }\nvoid empty(void) {}\nvoid empty2(void) { }\n", true },
    { "labels-and-gotos", "int f(int x) { if (x) goto out; x += 1; out: return x; }\nint g(int y) { again: if (y > 0) { y -= 1; goto again; } return y; }\n", true },
    // A file-scope union made transparent in a body applies to what follows it: source order's. A union the body made,
    // which nothing after it can name, the merge takes, with its activation, which a call through a pointer reads.
    { "transparent-union-in-a-body", "union U { int* a; long* b; };\nint f(void) { typedef union U T __attribute__((transparent_union)); return 0; }\n", false },
    { "transparent-local-union-in-a-body",
      "int f(int* p) { union L { int* a; long* b; }; typedef union L T __attribute__((transparent_union)); int (*fp)(T) = 0; return fp ? fp(p) : 0; }\n"
      "int g(void) { union M { char* c; }; return (int)sizeof(union M); }\n", true },
    // Intrinsics: a body's declarations, each made once at its first use in source order, before or after the top level's
    // (a declaration's sizeof reads the call), in one body and in several, and the calls with them.
    { "intrinsics-in-a-body", "unsigned f(unsigned x, unsigned y) { __builtin_ia32_pause(); return __builtin_ia32_crc32si(x, y); }\nint g(void) { return 2; }\n", true },
    { "intrinsics-in-two-bodies",
      "unsigned f(unsigned x) { return __builtin_ia32_crc32si(x, 1) ^ __builtin_ia32_crc32si(1, x); }\n"
      "unsigned g(unsigned y) { __builtin_ia32_pause(); return __builtin_ia32_crc32si(y, 2); }\n", true },
    { "intrinsic-before-the-top-levels", "unsigned f(unsigned x) { return __builtin_ia32_crc32si(x, 1); }\nint n = sizeof(__builtin_ia32_crc32si(1, 2));\n"
      "unsigned g(unsigned y) { return __builtin_ia32_crc32si(y, (unsigned)n); }\n", true },
    // The body decodes the declaration the top level decodes only after it, and so makes its types first: the types the
    // top level makes between them come after those, as in source order.
    { "intrinsic-before-the-top-levels-types", "unsigned f(unsigned x) { return __builtin_ia32_crc32si(x, 1); }\nstruct S { int a; } *s;\n"
      "int n = sizeof(__builtin_ia32_crc32si(1, 2));\nunsigned g(unsigned y) { return __builtin_ia32_crc32si(y, (unsigned)n); }\n", true },
    { "intrinsic-after-the-top-levels", "int n = sizeof(__builtin_ia32_crc32si(1, 2));\nunsigned f(unsigned x) { return __builtin_ia32_crc32si(x, (unsigned)n); }\n", true },
    { "intrinsic-vectors-in-bodies",
      "typedef int V __attribute__((vector_size(16)));\nV f(V x) { return __builtin_ia32_pshufd(x, 3); }\nV g(V y) { return __builtin_ia32_pshufd(y, 1); }\n"
      "unsigned h(unsigned z) { return __builtin_ia32_crc32si(z, 3); }\n", true },
    // A union completed after one body and made transparent at file scope after another: each body sees it as source
    // order's reader does, incomplete, complete and then transparent. Before the typedef, a pointer for it is clang's
    // error, which the expressions' phase gives after the merge, as after source order's walk.
    { "transparent-union-later", "union V;\nint first(void) { return 2; }\nunion V { int* p; long* q; };\nint e(union V v);\n"
      "int middle(union V v) { return e(v); }\ntypedef union V W __attribute__((transparent_union));\nint last(void) { int x = 0; return e(&x); }\n", true },
    { "transparent-union-too-early", "union U;\nint first(void) { return 1; }\nunion U { int* p; long* q; };\nint d(union U u);\n"
      "int early(void) { int x = 0; return d(&x); }\ntypedef union U T __attribute__((transparent_union));\nint late(void) { int x = 0; return d(&x); }\n", true },
    // A body whose walk reports an error is source order's, as is a unit whose top level does; a syntax error stops
    // before any reading, and an error the later phases find is merged like any other.
    { "error-in-a-body", "int f(void) { int a[-1]; return 0; }\nint g(void) { return 2; }\n", false },
    { "error-at-the-top-level", "int f(void) { return 1; }\nint x[-1];\nint g(void) { return 2; }\n", false },
    { "syntax-error", "int f(void) { return 1 +; }\nint g(void) { return 2; }\n", false },
    { "error-in-a-later-phase", "int f(int* p) { return p + 1.0; }\nint g(void) { return 2; }\n", true },
    // A body that completes its definition's parameter-list tag, which it declares again (#436), completes a tag the
    // top level made, which nothing after the body can name: merged, the tag complete from the body's definition on.
    // A union and an enum too, a tag completed after the body uses it, one in each of two bodies (on one lane, the
    // second body's after the first's tag is given back), and one that only uses it.
    { "parameter-list-tag-completed-in-its-body", "int f(struct T *p) { struct T { int x; }; return p->x; }\nint g(void) { return 2; }\n", true },
    { "parameter-list-union-completed-in-its-body", "int f(union U *p) { union U { int a; float b; }; return p->a; }\nint g(void) { return 2; }\n", true },
    { "parameter-list-enum-completed-in-its-body", "int f(enum E *p) { enum E { A, B }; return *p == B; }\nint g(void) { return 2; }\n", true },
    { "parameter-list-tag-completed-after-a-use",
      "int f(struct T *p) { struct T *q = p; struct T { int x; }; return q->x + (int)sizeof *p; }\nint g(void) { return 2; }\n", true },
    { "parameter-list-tags-completed-in-two-bodies",
      "int f(struct T *p) { struct T { int x; }; return p->x; }\nlong g(struct T *q) { struct T { long y; int z; }; return q->y + (long)sizeof *q; }\n"
      "int h(void) { return 3; }\n", true },
    { "parameter-list-tag-used-in-its-body", "int f(struct T *p) { struct T *q = p; return q != 0; }\nint g(void) { return 2; }\n", true },
    // Structs, unions and enums defined in a parameter list (#438): a definition's tags and enumeration constants, which
    // its body declares again at its opening, whether it names them or not; and a prototype's, which end with it.
    { "parameter-list-definitions-in-its-body",
      "int f(struct S { int x; } s, enum E { K = 7 } e) { struct S t = s; enum E k = K; return t.x + k + e; }\nint g(void) { return 2; }\n", true },
    { "parameter-list-constant-shadows-the-file-scope", "enum { K = 5 };\nint f(enum E { K = 1 } e) { return K + e; }\nint g(void) { return K; }\n", true },
    { "parameter-list-constant-its-body-doesnt-name", "enum { K = 5 };\nint f(enum E { K = 1 } e) { return e; }\nint g(void) { return K; }\n", true },
    { "prototype-list-constant-ends-with-it", "void g(enum E { K = 7 } e);\nint f(void) { int K = 3; return K; }\n", true },
    { "definition-in-an-array-bound", "int a[sizeof(struct V { int x; })];\nint f(void) { struct V v = { 2 }; return v.x; }\n", true },
    // A definition in a prototype that a parameter's bound holds ends at that prototype's `)`, and one in a body's
    // bound after a statement expression is read before the walk reaches the expression (#460's review).
    { "bound-prototype-constant-isnt-the-bodys", "int K = 3;\nint f(int a[sizeof(void (*)(enum E { K = 7 }))]) { return K; }\nint g(void) { return K; }\n", true },
    { "body-bound-definition-after-a-statement-expression",
      "int f(int m) { int a[({ m; }) + sizeof(struct S { int x; })]; struct S s = { 0 }; return s.x + (int)sizeof a; }\nint g(void) { return 2; }\n", true },
    // C23's enums with a fixed underlying type in bodies, beside plain enums, structs and unions. A body's tags take the
    // indices after the top level's, as the tags of the bodies its lane read before it did, and none may keep another's
    // fixed base (lanes once shared the top level's enum_fixed). Both orders, many bodies, and a file-scope one shadowed.
    { "fixed-enum-then-plain-enum", "int f(void) { enum E : int { A = 1 }; return A; }\nint g(void) { enum F { B = 2 }; return (int)sizeof(enum F) + B; }\n", true,
      "-std=gnu23" },
    { "plain-enum-then-fixed-enum", "int f(void) { enum F { B = 2 }; return B; }\nint g(void) { enum E : long { A = 1 }; return (int)sizeof(enum E) + A; }\n", true,
      "-std=gnu23" },
    { "fixed-enums-in-many-bodies",
      "int f0(void) { enum E0 : char { A0 = 1 }; return A0; }\nint f1(void) { enum E1 { B1 = 2 }; return (int)sizeof(enum E1) + B1; }\n"
      "int f2(void) { enum E2 : long long { A2 = 3 }; return (int)sizeof(enum E2) + A2; }\n"
      "int f3(void) { struct S3 { int x; } s = { 4 }; return s.x + (int)sizeof(struct S3); }\n"
      "int f4(void) { enum E4 : short; enum E4 : short { A4 = 5 }; return (int)sizeof(enum E4) + A4; }\n"
      "int f5(void) { enum E5 { B5 = 6 } e = B5; return e; }\nint f6(void) { union U6 { int i; char c; } u = { 7 }; return u.i + (int)sizeof(union U6); }\n"
      "int f7(void) { enum E7 : unsigned { A7 = 8 }; enum F7 { B7 = 9 }; return A7 + B7 + (int)sizeof(enum F7); }\n", true, "-std=gnu23" },
    { "fixed-enum-at-file-scope-and-in-a-body",
      "enum T : unsigned char { X = 1 };\nint f(void) { enum T t = X; return t + (int)sizeof t; }\n"
      "int g(void) { enum T : short { Y = 2 }; return Y + (int)sizeof(enum T); }\nint h(void) { enum T u = X; return u; }\n", true, "-std=gnu23" },
};

// Every cut of empty, singleton and repeated-position histories against a simple forward oracle. Each history ends
// exactly at a protected page, so an upper-bound search must not read its end or an empty history. Nonzero starts
// keep earlier names out; declaration/type payloads distinguish the last equal-position update from the first.
static bool check_deferred_test_linked_history(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    for (uint32_t length = 0; valid && length <= 128; length += 1)
    {
        const uint32_t prefix = 3;
        CheckDeferredLinked* records = (CheckDeferredLinked*)(mapping + page) - length - prefix;
        for (uint32_t i = 0; i < length + prefix; i += 1)
        {
            records[i] = (CheckDeferredLinked){
                .position = i < prefix ? UINT32_MAX : 3 + (i - prefix) / 3 * 2,
                .declaration = i + 1,
                .type = i + 7,
            };
        }
        for (uint32_t point = 0; valid && point <= 90; point += 1)
        {
            uint32_t expected = prefix;
            for (uint32_t i = prefix; i < prefix + length; i += 1)
            {
                expected += records[i].position <= point;
            }
            const uint32_t actual = check_deferred_linked_end(records, prefix, prefix + length, point);
            valid = actual == expected;
            if (actual > prefix && valid)
            {
                valid = records[actual - 1].declaration == expected && records[actual - 1].type == expected + 6;
            }
        }
        valid = valid && check_deferred_linked_end(records, prefix, prefix + length, UINT32_MAX) == prefix + length;
    }
    valid = valid && check_deferred_linked_end(NULL, 0, 0, 0) == 0;
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    return valid;
}

static uint32_t check_deferred_benchmark_cpus(const char* text, uint32_t* cpus, uint32_t capacity);

// benchmark-checker-deferred's --cpus (#432's review), into room for BUSTER_CHECK_DEFERRED_MAX_LANES CPUs that ends at a
// PROT_NONE page: each list of 1 to that many is read whole, one of one more is refused, and so is every other invalid
// list. A write past the room faults.
static bool check_deferred_test_cpus(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* pages = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = pages != MAP_FAILED && mprotect(pages + page, page, PROT_NONE) == 0;
    uint32_t* cpus = valid ? (uint32_t*)(pages + page) - BUSTER_CHECK_DEFERRED_MAX_LANES : 0;
    char text[1024];
    uint64_t length = 0;
    for (uint32_t i = 0; valid && i <= BUSTER_CHECK_DEFERRED_MAX_LANES; i += 1)
    {
        length += (uint64_t)snprintf(text + length, sizeof(text) - length, "%s%u", i == 0 ? "" : ",", i);
        const uint32_t count = check_deferred_benchmark_cpus(text, cpus, BUSTER_CHECK_DEFERRED_MAX_LANES);
        valid = i < BUSTER_CHECK_DEFERRED_MAX_LANES ? count == i + 1 && cpus[0] == 0 && cpus[i] == i : count == 0;
    }
    char limit[2][16];
    snprintf(limit[0], sizeof(limit[0]), "%u", (uint32_t)(BUSTER_LANE_CPU_MASK_WORDS * 64 - 1));
    snprintf(limit[1], sizeof(limit[1]), "%u", (uint32_t)(BUSTER_LANE_CPU_MASK_WORDS * 64));
    valid = valid && check_deferred_benchmark_cpus(limit[0], cpus, BUSTER_CHECK_DEFERRED_MAX_LANES) == 1;
    const char* const invalid[] = { "", ",", "1,", ",1", "1,,2", "a", "1a", "-1", "+1", "1 2", limit[1] };
    for (uint32_t i = 0; valid && i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        valid = check_deferred_benchmark_cpus(invalid[i], cpus, BUSTER_CHECK_DEFERRED_MAX_LANES) == 0;
    }
    if (pages != MAP_FAILED)
    {
        munmap(pages, page * 2);
    }
    return valid;
}

// The difference report on a deferred path that made no input (#432's second review): the vector path's tokens have
// columns and the deferred path's none, which the report must say rather than read. Its stages go to standard error,
// silenced here.
static bool check_deferred_test_no_input(void)
{
    static uint32_t declarations[4];
    static CheckCorpusPath made;
    static CheckCorpusPath none;
    made = (CheckCorpusPath){ 0 };
    none = (CheckCorpusPath){ 0 };
    made.input.token_count = BUSTER_ARRAY_LENGTH(declarations);
    made.input.declarations = declarations;
    char text[256] = { 0 };
    fflush(stderr);
    const int saved = dup(STDERR_FILENO);
    const int null = open("/dev/null", O_WRONLY);
    const bool quiet = saved >= 0 && null >= 0 && dup2(null, STDERR_FILENO) >= 0;
    check_corpus_deferred_difference(&made, &none, text, sizeof(text));
    fflush(stderr);
    if (saved >= 0)
    {
        dup2(saved, STDERR_FILENO);
        close(saved);
    }
    if (null >= 0)
    {
        close(null);
    }
    return quiet && strstr(text, "made no input") != NULL;
}

// The state's memory (c_check_deferred.c, check_deferred_lane_arenas): a lane's six arenas in one mapping, in order and
// end to end with their capacities, writable to their last byte, kept with their pages when the state takes them back
// for its next reading, and its own arena likewise; then every mapping unmapped by check_deferred_release, which leaves
// the state as a zeroed one.
static bool check_deferred_test_memory(void)
{
    static CheckDeferred state;
    state = (CheckDeferred){ 0 };
    const uint32_t lanes[] = { 0, 5, BUSTER_CHECK_DEFERRED_MAX_LANES - 1 };
    const uint64_t capacities[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { UINT64_C(1) << 36, UINT64_C(1) << 36, UINT64_C(1) << 36, UINT64_C(1) << 34, UINT64_C(1) << 32, UINT64_C(1) << 32 };
    uint8_t* mappings[BUSTER_ARRAY_LENGTH(lanes)] = { 0 };
    bool valid = true;
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(lanes); l += 1)
    {
        CheckDeferredArenas* arenas = state.memory.lanes + lanes[l];
        Arena* list[BUSTER_CHECK_DEFERRED_LANE_ARENAS] = { &arenas->output, &arenas->scratch, &arenas->results, &arenas->types, &arenas->type_index, &arenas->names };
        valid &= check_deferred_lane_arenas(arenas);
        mappings[l] = arenas->mapping;
        uint64_t offset = 0;
        for (uint32_t i = 0; valid && i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
        {
            valid = list[i]->base == arenas->mapping + offset && list[i]->capacity == capacities[i] && list[i]->length == 0 && list[i]->mapping_offset == 0;
            uint8_t* first = valid ? arena_push(list[i], 64, 64) : 0;
            valid = first == list[i]->base;
            if (valid)
            {
                first[0] = (uint8_t)(i + 1);
                list[i]->base[list[i]->capacity - 1] = (uint8_t)(i + 1);
            }
            offset += capacities[i];
        }
        valid = valid && offset == BUSTER_CHECK_DEFERRED_LANE_BYTES;
        // Taken back for the next reading: the same mapping, empty, its pages as the last reading left them.
        valid = valid && check_deferred_lane_arenas(arenas) && arenas->mapping == mappings[l];
        for (uint32_t i = 0; valid && i < BUSTER_CHECK_DEFERRED_LANE_ARENAS; i += 1)
        {
            valid = list[i]->length == 0 && list[i]->base[0] == i + 1 && list[i]->base[list[i]->capacity - 1] == i + 1;
        }
    }
    check_deferred_arena(&state.memory.arena, UINT64_C(1) << 36);
    uint8_t* own = state.memory.arena.base;
    valid = valid && own != 0 && arena_push(&state.memory.arena, 4096, 64) == own;
    check_deferred_arena(&state.memory.arena, UINT64_C(1) << 36);
    valid = valid && state.memory.arena.base == own && state.memory.arena.length == 0;
    check_deferred_release(&state);
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    unsigned char resident = 0;
    for (uint32_t l = 0; l < BUSTER_ARRAY_LENGTH(lanes); l += 1)
    {
        // An address of no mapping: mincore refuses it with ENOMEM.
        valid = valid && mappings[l] != 0 && mincore(mappings[l], page, &resident) != 0 && errno == ENOMEM;
        valid = valid && mincore(mappings[l] + BUSTER_CHECK_DEFERRED_LANE_BYTES - page, page, &resident) != 0 && errno == ENOMEM;
    }
    valid = valid && own != 0 && mincore(own, page, &resident) != 0 && errno == ENOMEM;
    static const CheckDeferredMemory zero;
    valid = valid && memcmp(&state.memory, &zero, sizeof(zero)) == 0;
    return valid;
}

// A lane's per-name columns (c_check_deferred.c, check_deferred_lane_names): from room for 4,096 names, in its names
// arena in order, each on a 64-byte line and clear of the next, the columns that start as none zero; at the same places
// for a reading of fewer names and for the next reading, with what the last left in them, but for the columns that start
// as none where a reading was poisoned (check_deferred_poison), which are cleared with the stamp; the intrinsics' cache
// only where the top level has one; laid out again on zero pages for more names than the room; and refused for more
// than the arena holds, after which a reading of fewer names takes the places of the room it had.
static bool check_deferred_test_lane_names(void)
{
    static CheckDeferred state;
    static CheckInputReader top;
    static CheckDeferredLane lane;
    static uint32_t intrinsics[1];
    state = (CheckDeferred){ .top_reader = &top };
    top = (CheckInputReader){ .intrinsic_names = intrinsics };
    lane = (CheckDeferredLane){ .arenas = state.memory.lanes };
    CheckDeferredArenas* arenas = lane.arenas;
    CheckInputReader* reader = &lane.reader;
    bool valid = check_deferred_lane_arenas(arenas);
    uint8_t* first[8] = { 0 };
    for (uint32_t layout = 0; valid && layout < 2; layout += 1)
    {
        const uint32_t room = layout == 0 ? 4096 : 8192;
        valid = check_deferred_lane_names(&state, &lane, layout == 0 ? 100 : 4097) && arenas->name_room == room && arenas->stamp == 0;
        uint8_t* columns[8] = { (uint8_t*)reader->bindings.heads, (uint8_t*)reader->parameter_seen, (uint8_t*)reader->member_seen, (uint8_t*)lane.stamps,
                                (uint8_t*)reader->linked_declarations, (uint8_t*)reader->linked_types, (uint8_t*)reader->intrinsic_names, (uint8_t*)reader->builtin_names };
        const uint64_t bytes[8] = { (uint64_t)room * C_BINDING_NAMESPACES * 4, (uint64_t)room * 4, (uint64_t)room * 4, (uint64_t)room * 4,
                                    (uint64_t)room * 4, (uint64_t)room * sizeof(TypeId), (uint64_t)room * 4, (uint64_t)room * 2 };
        valid = valid && columns[0] == arenas->names.base && arenas->name_bytes == arenas->names.length;
        for (uint32_t c = 0; valid && c < 8; c += 1)
        {
            valid = columns[c] != 0 && ((uintptr_t)columns[c] & 63) == 0 && (c == 7 || columns[c] + bytes[c] <= columns[c + 1]) &&
                    columns[c] + bytes[c] <= arenas->names.base + arenas->names.length;
            // The columns that start as none: zero, on the first layout's fresh pages and on the second's cleared ones.
            for (uint64_t b = 0; valid && c < 4 && b < bytes[c]; b += 1)
            {
                valid = columns[c][b] == 0;
            }
            first[c] = layout == 0 ? columns[c] : first[c];
        }
        valid = valid && (layout == 0 || reader->bindings.heads == (uint32_t*)first[0]);
        if (valid && layout == 0)
        {
            // What a reading leaves: kept for the next, at the same places, for fewer names.
            reader->bindings.heads[5] = 7;
            reader->parameter_seen[5] = 9;
            lane.stamps[5] = 3;
            reader->linked_declarations[5] = 11;
            reader->builtin_names[5] = 13;
            arenas->stamp = 3;
            top.intrinsic_names = 0;
            valid = check_deferred_lane_names(&state, &lane, 50) && reader->bindings.heads == (uint32_t*)first[0] && lane.stamps == (uint32_t*)first[3] &&
                    reader->intrinsic_names == 0 && reader->bindings.heads[5] == 7 && reader->parameter_seen[5] == 9 && lane.stamps[5] == 3 &&
                    reader->linked_declarations[5] == 11 && reader->builtin_names[5] == 13 && arenas->stamp == 3;
            // A poisoned reading: the columns that start as none cleared, with the stamp, and the others left to the
            // bodies, which set them before they read them.
            check_deferred_poison(&state, 0xa5);
            top.intrinsic_names = intrinsics;
            valid = valid && arenas->names_dirty && check_deferred_lane_names(&state, &lane, 4096) && !arenas->names_dirty && reader->bindings.heads == (uint32_t*)first[0] &&
                    reader->bindings.heads[5] == 0 && reader->parameter_seen[5] == 0 && lane.stamps[5] == 0 && arenas->stamp == 0 &&
                    reader->linked_declarations[5] == 0xa5a5a5a5u && reader->builtin_names[5] == 0xa5a5;
            reader->member_seen[room - 1] = 1;
            lane.stamps[room - 1] = 1;
            arenas->stamp = 1;
        }
    }
    // More names than the arena holds: refused, and fewer taken at the room's places again, its columns as none.
    valid = valid && !check_deferred_lane_names(&state, &lane, UINT32_MAX) && arenas->name_room == 8192;
    valid = valid && check_deferred_lane_names(&state, &lane, 100) && arenas->name_room == 8192 && reader->bindings.heads == (uint32_t*)arenas->names.base &&
            lane.stamps[100] == 0 && arenas->stamp == 0;
    check_deferred_release(&state);
    return valid;
}

// The merge's scans of a column's tokens (check_deferred_entries, check_deferred_entries64 and check_deferred_count_bytes)
// against their scalar twins, on ranges of every length to 300 from five starts, of columns that end at a PROT_NONE page,
// with entries at random tokens at four densities: of 32-bit values whose none is 0 or BUSTER_CHECK_NONE, of 64-bit ones
// and of bytes. The same positions in order, each in the range, the same counts, and no read past a column, which
// faults.
static bool check_deferred_test_entries(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(0, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    uint32_t vector_positions[301];
    uint32_t scalar_positions[301];
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    // Entries per 64 tokens, and the ranges' starts.
    const uint32_t densities[] = { 0, 3, 32, 64 };
    const uint32_t begins[] = { 0, 1, 13, 64, 65 };
    for (uint32_t end = 0; valid && end <= 300; end += 1)
    {
        for (uint32_t s = 0; valid && s < BUSTER_ARRAY_LENGTH(begins) && begins[s] <= end; s += 1)
        {
            const uint32_t begin = begins[s];
            for (uint32_t d = 0; valid && d < BUSTER_ARRAY_LENGTH(densities); d += 1)
            {
                for (uint32_t kind = 0; valid && kind < 4; kind += 1)
                {
                    uint32_t vector_count = 0;
                    uint32_t scalar_count = 0;
                    if (kind < 2)
                    {
                        uint32_t* column = (uint32_t*)(mapping + page) - end;
                        const uint32_t none = kind == 0 ? 0 : BUSTER_CHECK_NONE;
                        for (uint32_t i = 0; i < end; i += 1)
                        {
                            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                            column[i] = (uint32_t)(state >> 58) < densities[d] ? (uint32_t)(state >> 16) % 1000 + 1 : none;
                        }
                        vector_count = check_deferred_entries(column, begin, end, none, true, vector_positions);
                        scalar_count = check_deferred_entries(column, begin, end, none, false, scalar_positions);
                    }
                    else if (kind == 2)
                    {
                        uint64_t* column = (uint64_t*)(mapping + page) - end;
                        for (uint32_t i = 0; i < end; i += 1)
                        {
                            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                            column[i] = (uint32_t)(state >> 58) < densities[d] ? state | 1 : 0;
                        }
                        vector_count = check_deferred_entries64(column, begin, end, true, vector_positions);
                        scalar_count = check_deferred_entries64(column, begin, end, false, scalar_positions);
                    }
                    else
                    {
                        uint8_t* column = mapping + page - end;
                        for (uint32_t i = 0; i < end; i += 1)
                        {
                            state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
                            column[i] = (uint32_t)(state >> 58) < densities[d] ? (uint8_t)(state >> 24 | 1) : 0;
                        }
                        vector_count = check_deferred_count_bytes(column, begin, end, true);
                        scalar_count = check_deferred_count_bytes(column, begin, end, false);
                    }
                    const uint32_t listed = kind < 3 ? vector_count : 0;
                    valid = vector_count == scalar_count && memcmp(vector_positions, scalar_positions, (uint64_t)listed * sizeof(uint32_t)) == 0;
                    for (uint32_t e = 0; valid && e < listed; e += 1)
                    {
                        valid = begin <= vector_positions[e] && vector_positions[e] < end && (e == 0 || vector_positions[e - 1] < vector_positions[e]);
                    }
                }
            }
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    return valid;
}

// A dominant trailing top-level item leaves the last lane an empty range past every item. It must not inspect the
// unused cut after the real bodies, or form a position from it; the real cuts end at a protected page here.
static bool check_deferred_test_empty_pass(void)
{
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool valid = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    if (valid)
    {
        enum { BODIES = 8 };
        CheckDeferredCut* cuts = (CheckDeferredCut*)(mapping + page) - BODIES;
        uint64_t costs[BODIES + 1];
        for (uint32_t b = 0; b < BODIES; b += 1)
        {
            cuts[b] = (CheckDeferredCut){ .opening = b * 2, .closing = b * 2 + 1 };
            costs[b] = cuts[b].closing + 1;
        }
        costs[BODIES] = 4097;
        const Range64 range = range_part_by_cost(costs, BODIES + 1, 7, 8);
        valid = range.begin == BODIES + 1 && range.end == range.begin;
        Parser parser = { .tokens.count = 4096 };
        CheckInput input = { 0 };
        CheckInputReader reader = { .parser = &parser, .output = &input };
        CheckDeferred deferred = { .cuts = cuts, .body_count = BODIES };
        uint32_t position = UINT32_MAX;
        CheckDeferredPass pass = { .deferred = &deferred, .reader = &reader, .positions = &position };
        for (uint32_t vector = 0; valid && vector < 2; vector += 1)
        {
            pass.vector = vector != 0;
            valid = check_deferred_pass_items(&pass, (uint32_t)range.begin, (uint32_t)range.end) == 0 && position == UINT32_MAX;
        }
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, page * 2);
    }
    return valid;
}

// The merge's explicit scalar twin survives the state's reset, including alternation and poisoned reused storage.
static bool check_deferred_test_merge_modes(void)
{
    static InternTable names;
    static Preprocessor pp;
    static Parser parser;
    static CheckDeferred deferred;
    static const char source[] = "int f(int n){return n+1;}int g(int n){return f(n)+2;}";
    const PPOptions options = { .language = { .standard = 2017, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parsing = { 0 };
    bool valid = intern_initialize(&names) && pp_initialize(&pp, &options, &names) &&
                 pp_run_source(&pp, "merge-modes.c", (const uint8_t*)source, sizeof(source) - 1) &&
                 parse_initialize(&parser, &parsing, options.language) && parse_translation_unit(&parser, &pp) && parser.errors == 0;
    const bool modes[] = { true, false, true };
    for (uint32_t m = 0; valid && m < BUSTER_ARRAY_LENGTH(modes); m += 1)
    {
        CheckTestTypes types = { 0 };
        CheckInput input = { 0 };
        Arena output = arena_reserve(UINT64_C(1) << 27);
        Arena scratch = arena_reserve(UINT64_C(1) << 27);
        deferred.scalar_merge = modes[m];
        valid = output.base != NULL && scratch.base != NULL && check_test_types_open(&types) &&
                check_input_deferred(&deferred, &input, &parser, &pp, &types.table, &output, &scratch, 1, CHECK_INPUT_BINDINGS_DEFAULT) &&
                !deferred.unsupported && deferred.scalar_merge == modes[m];
        check_test_types_close(&types);
        arena_release(&scratch);
        arena_release(&output);
        check_deferred_poison(&deferred, 0xa5);
    }
    check_deferred_release(&deferred);
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&names);
    return valid;
}

// The bodies' tokens that pay for a lane in a unit whose top level has `top` tokens (outside its bodies), written again
// from the rule (docs/c-deferred-bodies.md, "How many lanes").
static uint64_t check_deferred_test_lane_tokens(uint64_t top)
{
    return BUSTER_CHECK_DEFERRED_LANE_TOKENS + top / BUSTER_CHECK_DEFERRED_LANE_TOP_TOKENS;
}

// The lanes a unit's bodies run on, written again from the rule: one, and one more per check_deferred_test_lane_tokens
// of their tokens beyond the larger of the largest body's and twice the second largest's, or with `every` as many as
// there are lanes; and no more than `lanes` or the bodies, none without bodies.
static uint64_t check_deferred_test_lanes(uint64_t bodies, uint64_t tokens, uint64_t top, uint64_t largest, uint64_t second, uint64_t lanes, bool every)
{
    const uint64_t path = largest > second * 2 ? largest : second * 2;
    const uint64_t beyond = tokens > path ? tokens - path : 0;
    const uint64_t wanted = every ? lanes : 1 + beyond / check_deferred_test_lane_tokens(top);
    const uint64_t most = bodies < lanes ? bodies : lanes;
    return wanted < most ? wanted : most;
}

// The rule's sides, with N the tokens that pay for a lane in their unit, whose top level is five tokens a function
// (check_deferred_test_lane_tokens): `bodies` functions of empty statements, whose bodies
// have N tokens each from their opening brace to their closing one, but the first `first` times as many, the second
// `second` times as many, and the last `fewer` fewer; and the lanes the rule gives them on 8. Bodies of N: a second lane
// needs N tokens beyond twice the second largest body's, and an eighth seven times as many, each with a token fewer too.
// A largest body of four times N, which the first lane reads itself, beside one of N, and of a token fewer: the second
// lane its tokens beyond the largest body's pay for (#448's rule, which counted the largest body twice, took one). A
// largest body of three times N beside one of twice N, whose woken lane reads it in about four times N's time, and one
// of N and of a token fewer: the third lane its tokens beyond four times N pay for.
typedef struct CheckDeferredSize CheckDeferredSize;
struct CheckDeferredSize
{
    const char* name;
    uint32_t bodies;
    uint32_t first;
    uint32_t second;
    uint32_t fewer;
    uint32_t lanes;
    // When nonzero, this many tokens per body, two more in the first: grouped-part boundary cases.
    uint32_t base_tokens;
};

static const CheckDeferredSize check_deferred_sizes[] =
{
    { "lane-tokens-below-two-lanes", 3, 1, 1, 1, 1, 0 },
    { "lane-tokens-two-lanes", 3, 1, 1, 0, 2, 0 },
    { "lane-tokens-below-eight-lanes", 9, 1, 1, 1, 7, 0 },
    { "lane-tokens-eight-lanes", 9, 1, 1, 0, 8, 0 },
    { "lane-tokens-large-body-below-two-lanes", 2, 4, 1, 1, 1, 0 },
    { "lane-tokens-large-body-two-lanes", 2, 4, 1, 0, 2, 0 },
    { "lane-tokens-second-body-below-three-lanes", 3, 3, 2, 1, 2, 0 },
    { "lane-tokens-second-body-three-lanes", 3, 3, 2, 0, 3, 0 },
    { .name = "grouped-parts-below-two-lanes-capacity", .bodies = 127, .lanes = 2, .base_tokens = 18 },
    { .name = "grouped-parts-two-lanes-capacity", .bodies = 128, .lanes = 2, .base_tokens = 18 },
    { .name = "grouped-parts-above-two-lanes-capacity", .bodies = 129, .lanes = 2, .base_tokens = 18 },
    { .name = "grouped-parts-below-eight-lanes-capacity", .bodies = 511, .lanes = 6, .base_tokens = 18 },
    { .name = "grouped-parts-eight-lanes-capacity", .bodies = 512, .lanes = 6, .base_tokens = 18 },
    { .name = "grouped-parts-above-eight-lanes-capacity", .bodies = 513, .lanes = 6, .base_tokens = 18 },
};

// A CheckDeferredSize's unit: its bodies and the tokens they hold in all, the largest one's (the first of them) and the
// largest of the others'.
static void check_deferred_sized(char* text, uint64_t capacity, const CheckDeferredSize* size, uint64_t* tokens, uint64_t* largest, uint64_t* second)
{
    uint64_t length = 0;
    *tokens = 0;
    *largest = 0;
    *second = 0;
    for (uint32_t b = 0; b < size->bodies && length < capacity; b += 1)
    {
        // `void fB(void)`: five tokens of the top level a function.
        const uint64_t lane_tokens = check_deferred_test_lane_tokens((uint64_t)size->bodies * 5);
        const uint64_t body = size->base_tokens != 0 ? size->base_tokens + (b == 0 ? 2u : 0u) :
                              lane_tokens * (b == 0 ? size->first : b == 1 ? size->second : 1) - (b + 1 == size->bodies ? size->fewer : 0);
        length += (uint64_t)snprintf(text + length, capacity - length, "void f%u(void) {", b);
        for (uint64_t i = 2; i < body && length < capacity; i += 1)
        {
            length += (uint64_t)snprintf(text + length, capacity - length, i % 64 == 0 ? ";\n" : ";");
        }
        length += length < capacity ? (uint64_t)snprintf(text + length, capacity - length, "}\n") : 0;
        *tokens += body;
        *second = body > *largest ? *largest : body > *second ? body : *second;
        *largest = body > *largest ? body : *largest;
    }
}

// Many bodies, for the lanes' parts: a unit of `count` small functions that call one another.
static void check_deferred_many(char* text, uint64_t capacity, uint32_t count)
{
    uint64_t length = (uint64_t)snprintf(text, capacity, "struct P { int a; long b; };\nstatic int f0(int x) { return x; }\n");
    for (uint32_t i = 1; i < count && length < capacity; i += 1)
    {
        length += (uint64_t)snprintf(text + length, capacity - length,
                                     "static int f%u(int x) { struct P p = { x, %u }; const char* s = \"f%u\"; return f%u(x + (int)p.b) + s[0]; }\n", i, i, i, i - 1);
    }
}

typedef struct CheckDeferredTimedContext CheckDeferredTimedContext;
struct CheckDeferredTimedContext
{
    const char* path;
    const PPOptions* options;
    CheckCorpusOutcome outcome;
    LaneTeam* team;
    pid_t threads[2];
    bool continued[2];
    bool restored;
};

// Read in a running team's first lane, with an unfinished take and an impossible inner pin target. The other lane
// waits on the original group, while the reading must restore that group, its counter and its running state.
static void check_deferred_timed_context(void* argument)
{
    CheckDeferredTimedContext* test = argument;
    const uint32_t index = lane_index();
    test->threads[index] = (pid_t)syscall(SYS_gettid);
    if (index == 0)
    {
        const Lane previous = thread_context.lane;
        const uint64_t taken = thread_context.taken;
        const bool running = thread_context.running;
        const uint32_t pinned_cpu = test->team->cpus[0];
        const uint32_t cpu = UINT32_MAX;
        thread_context.taken = 3;
        check_corpus_deferred_cpus = &cpu;
        check_corpus_measure(test->path, test->options, false, true, &test->outcome);
        check_corpus_deferred_cpus = 0;
        uint64_t mask[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
        bool affinity = syscall(SYS_sched_getaffinity, 0, sizeof(mask), mask) > 0;
        for (uint32_t word = 0; word < BUSTER_LANE_CPU_MASK_WORDS; word += 1)
        {
            const uint64_t expected = word == pinned_cpu / 64 ? 1ull << (pinned_cpu % 64) : 0;
            affinity = mask[word] == expected && affinity;
        }
        test->restored = thread_context.lane.index == previous.index && thread_context.lane.count == previous.count &&
                         thread_context.lane.group == previous.group && thread_context.taken == 3 &&
                         running && thread_context.running == running && thread_context.team == test->team && affinity;
        thread_context.taken = taken;
        thread_context.running = running;
        lane_restore(previous);
    }
    lane_sync();
}

static void check_deferred_timed_continue(void* argument)
{
    CheckDeferredTimedContext* test = argument;
    const uint32_t index = lane_index();
    test->continued[index] = lane_count() == 2 && (pid_t)syscall(SYS_gettid) == test->threads[index];
}

static bool check_deferred_timed_team(const char* path, const PPOptions* options, CheckCorpusOutcome* outcome)
{
    uint64_t before[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    uint64_t after[BUSTER_LANE_CPU_MASK_WORDS] = { 0 };
    const bool saved = syscall(SYS_sched_getaffinity, 0, sizeof(before), before) > 0;
    uint32_t cpu = 0;
    while (saved && cpu < BUSTER_LANE_CPU_MASK_WORDS * 64 && (before[cpu / 64] & (1ull << (cpu % 64))) == 0)
    {
        cpu += 1;
    }
    uint32_t other = cpu + 1;
    while (saved && other < BUSTER_LANE_CPU_MASK_WORDS * 64 && (before[other / 64] & (1ull << (other % 64))) == 0)
    {
        other += 1;
    }
    bool result = saved && other < BUSTER_LANE_CPU_MASK_WORDS * 64;
    if (!result)
    {
        fprintf(stderr, "test-checker-deferred: the live-team context check requires two distinct allowed CPUs\n");
    }
    if (result)
    {
        LaneTeam team = { 0 };
        const uint32_t cpus[] = { cpu, other };
        CheckDeferredTimedContext test = { .path = path, .options = options, .team = &team };
        const bool started = lane_team_start(&team, 2, cpus);
        result = started && lane_run(2, check_deferred_timed_context, &test, cpus) && test.restored;
        *outcome = test.outcome;
        const bool continued = started && lane_run(2, check_deferred_timed_continue, &test, cpus);
        result = continued && test.continued[0] && test.continued[1] && result;
        lane_team_stop(&team);
        result = syscall(SYS_sched_getaffinity, 0, sizeof(after), after) > 0 && memcmp(before, after, sizeof(before)) == 0 && result;
    }
    return result;
}

static int run_checker_deferred_tests(int count, char** arguments)
{
    bool keep = false;
    bool valid = true;
    for (int i = 0; i < count; i += 1)
    {
        keep |= strcmp(arguments[i], "--keep") == 0;
        valid &= strcmp(arguments[i], "--keep") == 0;
    }
    char directory[] = "/tmp/buster-check-deferred-XXXXXX";
    valid = valid && mkdtemp(directory) != NULL;
    Arena option_arena = arena_reserve(UINT64_C(1) << 24);
    static char many[1 << 20];
    static char sized[1 << 20];
    check_deferred_many(many, sizeof(many), 400);
    const uint32_t listed = (uint32_t)BUSTER_ARRAY_LENGTH(check_deferred_fixtures);
    const uint32_t fixtures = listed + 1 + (uint32_t)BUSTER_ARRAY_LENGTH(check_deferred_sizes);
    static const uint32_t lane_counts[] = { 1, 2, 3, 8 };
    static const CheckInputBindingMode modes[] = { CHECK_INPUT_BINDINGS_STACKS, CHECK_INPUT_BINDINGS_BATCH, CHECK_INPUT_BINDINGS_DIRECT };
    static const char* const mode_names[] = { "stacks", "batch", "direct" };
    uint64_t checks = 0;
    uint64_t failures = 0;
    uint64_t merged = 0;
    uint64_t bodies = 0;
    for (uint32_t f = 0; valid && f < fixtures; f += 1)
    {
        // The listed fixtures, the many bodies, and the rule's sides, which the reader reads by its own rule alone.
        const CheckDeferredSize* size = f > listed ? check_deferred_sizes + (f - listed - 1) : 0;
        uint64_t size_tokens = 0;
        uint64_t size_largest = 0;
        uint64_t size_second = 0;
        if (size != 0)
        {
            check_deferred_sized(sized, sizeof(sized), size, &size_tokens, &size_largest, &size_second);
        }
        const char* name = f < listed ? check_deferred_fixtures[f].name : size == 0 ? "many-bodies" : size->name;
        const char* source = f < listed ? check_deferred_fixtures[f].source : size == 0 ? many : sized;
        const bool expected = f < listed ? check_deferred_fixtures[f].merged : true;
        const bool every = size == 0;
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.c", directory, name);
        valid = parse_test_write(path, source, strlen(source));
        char* options_text[2] = { f < listed && check_deferred_fixtures[f].standard != 0 ? check_deferred_fixtures[f].standard : "-std=gnu17", path };
        PPOptions options;
        const char* input = 0;
        const char* output = 0;
        bool markers = false;
        valid = valid && pp_parse_options(2, options_text, &options, &option_arena, &input, &output, &markers);
        options.semantic_pragmas = true;
        for (uint32_t l = 0; valid && l < BUSTER_ARRAY_LENGTH(lane_counts); l += 1)
        {
            for (uint32_t m = 0; m < BUSTER_ARRAY_LENGTH(modes); m += 1)
            {
                check_corpus_deferred_lanes = lane_counts[l];
                check_corpus_deferred_bindings = modes[m];
                check_corpus_deferred_every_lane = every;
                CheckCorpusOutcome outcome;
                check_corpus_measure(path, &options, false, true, &outcome);
                check_corpus_deferred_every_lane = true;
                // The rule's lanes, and the reader copied by one of them at least and by no more; and a side of the
                // rule's bodies of the tokens it was made with, on the lanes it was made for.
                const uint64_t top = outcome.tokens - outcome.deferred_body_tokens;
                const uint64_t most = check_deferred_test_lanes(outcome.deferred_bodies, outcome.deferred_body_tokens, top, outcome.deferred_largest_body,
                                                                outcome.deferred_second_body, lane_counts[l], every);
                // The parts a group of the bodies' lanes cuts, made before the lanes started (parts_by_cost).
                const uint64_t group = most == 1 ? 1 : BUSTER_LANE_PARTS_PER_LANE * most;
                const uint64_t parts = outcome.deferred_bodies < group ? outcome.deferred_bodies : group;
                const bool copied = !outcome.deferred || (outcome.deferred_active == most && outcome.deferred_copies >= (most != 0) &&
                                                          outcome.deferred_copies <= most && outcome.deferred_parts == parts &&
                                                          outcome.deferred_lane_tokens == check_deferred_test_lane_tokens(top));
                const bool sized_right = size == 0 || (outcome.deferred_bodies == size->bodies && outcome.deferred_body_tokens == size_tokens &&
                                                       outcome.deferred_largest_body == size_largest && outcome.deferred_second_body == size_second &&
                                                       outcome.deferred_largest_lane == 0 &&
                                                       outcome.deferred_active == (size->lanes < lane_counts[l] ? size->lanes : lane_counts[l]));
                const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred == expected && copied && sized_right;
                checks += 1;
                failures += !passed;
                merged += outcome.deferred;
                bodies += outcome.deferred_bodies;
                if (!passed)
                {
                    fprintf(stderr, "test-checker-deferred: %s on %u lanes through %s: %s%s%s%s\n", name, lane_counts[l], mode_names[m],
                            !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                            !outcome.equal ? "the scalar and vector paths differ" : "", outcome.deferred != expected ? (expected ? "not merged" : "merged") : "",
                            copied ? "" : " (not the rule's lanes, their parts or tokens, or its reader copied by too many of them or by none)",
                            sized_right ? "" : " (not the bodies' tokens it was made with, not the lanes it was made for, or its largest body not the first lane's)");
                }
            }
        }
        // The reader's own rule on 8 lanes, which reads most of the listed fixtures on one.
        if (valid && size == 0)
        {
            check_corpus_deferred_lanes = 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_every_lane = false;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_every_lane = true;
            const uint64_t most = check_deferred_test_lanes(outcome.deferred_bodies, outcome.deferred_body_tokens, outcome.tokens - outcome.deferred_body_tokens,
                                                            outcome.deferred_largest_body, outcome.deferred_second_body, 8, false);
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred == expected &&
                                (!outcome.deferred || (outcome.deferred_active == most && outcome.deferred_copies >= (most != 0) && outcome.deferred_copies <= most));
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on 8 lanes by the reader's rule: %s\n", name,
                        !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : outcome.deferred != expected ? (expected ? "not merged" : "merged") :
                        "not the rule's lanes, or its reader copied by too many of them or by none");
            }
        }
        // A side of the rule of more than one body, on every lane its bodies can take of 8 with the first lane starting once
        // every other has taken from the counter, as when the others arrive first: the first lane must still read the
        // largest body, the first, which the others' takes must leave to it. Grouped parts also run by the reader's
        // rule: 129 bodies cut into 128 parts can put two smaller bodies in the costliest part instead.
        for (uint32_t policy = 0; valid && size != 0 && size->bodies > 1 && policy < (size->base_tokens != 0 ? 2u : 1u); policy += 1)
        {
            check_corpus_deferred_lanes = 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_late_first = true;
            check_corpus_deferred_every_lane = policy == 0;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_late_first = false;
            check_corpus_deferred_every_lane = true;
            const uint64_t most = policy == 0 ? (size->bodies < 8 ? size->bodies : 8) : size->lanes;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred && outcome.deferred_active == most &&
                                outcome.deferred_largest_body == size_largest && outcome.deferred_largest_lane == 0;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on %" PRIu64 " lanes with the first lane starting last: %s\n", name, most,
                        !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : !outcome.deferred ? "not merged" :
                        outcome.deferred_active != most ? "not every lane" : "the largest body not the first lane's");
            }
        }
        // A unit that merges, with its first lane taking every part: no other lane copies the reader. (The rule's sides,
        // whose bodies are only empty statements, take none of these.)
        if (valid && expected && size == 0)
        {
            check_corpus_deferred_lanes = 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_hold_lanes = true;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_hold_lanes = false;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred &&
                                outcome.deferred_copies == (outcome.deferred_bodies != 0);
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on 8 lanes with the first taking every part: %s\n", name,
                        !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : !outcome.deferred ? "not merged" : "a lane without a part copied the reader");
            }
        }
        // A unit that merges, with its merge finding the columns' entries by the scalar twin: the same result.
        for (uint32_t l = 0; valid && expected && size == 0 && l < 2; l += 1)
        {
            check_corpus_deferred_lanes = l == 0 ? 1 : 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_scalar_merge = true;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_scalar_merge = false;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on %u lanes with the merge's scalar twin: %s\n", name, check_corpus_deferred_lanes,
                        !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : "not merged");
            }
        }
        // A unit that merges, with its merge failing after the replay into the caller's table: source order's result,
        // from the table taken back to how it came.
        for (uint32_t l = 0; valid && expected && size == 0 && l < 2; l += 1)
        {
            check_corpus_deferred_lanes = l == 0 ? 1 : 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_fail_merge = true;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_fail_merge = false;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && !outcome.deferred;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on %u lanes with its merge failing after the replay: %s\n", name, check_corpus_deferred_lanes,
                        !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the result differs from source order's" :
                        !outcome.equal ? "the scalar and vector paths differ" : "merged");
            }
        }
        // A unit that merges, read on 8 lanes by a state that read it before: on one lane and on eight with every byte
        // that reading pushed in the state's memory poisoned, on eight with what it wrote there, and on one with every
        // lane's stamp then set to its last (check_deferred_stamp), which each lane's next body wraps: the one lane's
        // first body's names were stamped as the wrapped stamp's first. A reading takes over the memory of the state's
        // earlier ones, the lanes' among it, and must read as it does fresh.
        for (uint32_t l = 0; valid && expected && size == 0 && l < 4; l += 1)
        {
            check_corpus_deferred_lanes = 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_reread = l == 0 || l == 3 ? 1 : 8;
            check_corpus_deferred_poison = l < 2;
            check_corpus_deferred_stamp = l == 3 ? UINT32_MAX : 0;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            const uint32_t first = check_corpus_deferred_reread;
            check_corpus_deferred_reread = 0;
            check_corpus_deferred_poison = false;
            check_corpus_deferred_stamp = 0;
            const uint64_t most = check_deferred_test_lanes(outcome.deferred_bodies, outcome.deferred_body_tokens, outcome.tokens - outcome.deferred_body_tokens,
                                                            outcome.deferred_largest_body, outcome.deferred_second_body, 8, true);
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred && outcome.deferred_active == most &&
                                outcome.deferred_copies >= (most != 0) && outcome.deferred_copies <= most && check_corpus_deferred_kept;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on 8 lanes after a reading on %u%s: %s\n", name, first,
                        l < 2 ? " whose memory was poisoned" : l == 3 ? " whose lanes' stamps were then at their last" : "",
                        !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : !outcome.deferred ? "not merged" :
                        !check_corpus_deferred_kept ? "the state's memory wasn't kept" : "not every lane, or its reader copied by too many of them or by none");
            }
        }
        // A unit that merges, with the allocation of the deferred reader's state failing: a resource failure, which
        // check_corpus_check_policy keeps through the input's result, and not a result that differs.
        if (valid && expected && size == 0)
        {
            check_corpus_deferred_lanes = 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_fail_state = true;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_fail_state = false;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.resource && !outcome.deferred &&
                                outcome.deferred_reason != NULL && strcmp(outcome.deferred_reason, "the deferred path's state or tables couldn't be allocated") == 0;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s with the deferred reader's state not allocated: %s\n", name,
                        !outcome.prepared ? "not preprocessed" : !outcome.resource ? "not a resource failure" : !outcome.deferred_equal ? "reported as a difference" :
                        !outcome.equal ? "the scalar and vector paths differ" : "merged, or another reason");
            }
        }
        // Time one lane normally and inside a live pinned team, then eight. Preserve context, counter and running state,
        // the installed team's workers, and the caller's affinity, with dispatch and wake-up inside the lanes' span.
        for (uint32_t l = 0; valid && f == listed && l < 3; l += 1)
        {
            check_corpus_deferred_lanes = l < 2 ? 1 : 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_timing = true;
            CheckCorpusOutcome outcome = { 0 };
            bool restored = true;
            if (l == 1)
            {
                restored = check_deferred_timed_team(path, &options, &outcome);
            }
            else
            {
                check_corpus_measure(path, &options, false, true, &outcome);
            }
            check_corpus_deferred_timing = false;
            const bool timed = outcome.deferred_active == check_corpus_deferred_lanes && outcome.deferred_lanes_ns != 0 &&
                               (l == 2 || outcome.deferred_wake_ns == 0) &&
                               outcome.deferred_dispatch_ns + outcome.deferred_wake_ns <= outcome.deferred_lanes_ns;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred && timed && restored;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on %u lanes, timed: %s (span %" PRIu64 " ns, dispatch %" PRIu64 " ns, wake-up %" PRIu64 " ns)\n", name,
                        check_corpus_deferred_lanes, !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : !outcome.deferred ? "not merged" :
                        !restored ? "the caller's context, team continuity or affinity changed" :
                        "not on every lane, untimed, a wake-up on one lane, or the dispatch and the wake-up past the lanes' span",
                        outcome.deferred_lanes_ns, outcome.deferred_dispatch_ns, outcome.deferred_wake_ns);
            }
        }
        check_corpus_deferred_lanes = 0;
        if (!keep)
        {
            unlink(path);
        }
    }
    const bool linked_history = check_deferred_test_linked_history();
    checks += 1;
    failures += !linked_history;
    if (!linked_history)
    {
        fprintf(stderr, "test-checker-deferred: a linked history lookup read outside its name or selected the wrong update\n");
    }
    const bool cpus = check_deferred_test_cpus();
    checks += 1;
    failures += !cpus;
    if (!cpus)
    {
        fprintf(stderr, "test-checker-deferred: benchmark-checker-deferred's --cpus parser read a list wrong\n");
    }
    const bool no_input = check_deferred_test_no_input();
    checks += 1;
    failures += !no_input;
    if (!no_input)
    {
        fprintf(stderr, "test-checker-deferred: the difference report on a deferred path without an input didn't say so\n");
    }
    const bool entries = check_deferred_test_entries();
    checks += 1;
    failures += !entries;
    if (!entries)
    {
        fprintf(stderr, "test-checker-deferred: the merge's scans of a column's tokens and their scalar twins differ\n");
    }
    const bool empty_pass = check_deferred_test_empty_pass();
    checks += 1;
    failures += !empty_pass;
    if (!empty_pass)
    {
        fprintf(stderr, "test-checker-deferred: an empty merge share reads past the real cuts or writes positions\n");
    }
    const bool merge_modes = check_deferred_test_merge_modes();
    checks += 1;
    failures += !merge_modes;
    if (!merge_modes)
    {
        fprintf(stderr, "test-checker-deferred: the requested scalar merge mode is lost across state reuse\n");
    }
    const bool memory = check_deferred_test_memory();
    checks += 1;
    failures += !memory;
    if (!memory)
    {
        fprintf(stderr, "test-checker-deferred: the state's memory wasn't laid out, kept or released as it should be\n");
    }
    const bool names = check_deferred_test_lane_names();
    checks += 1;
    failures += !names;
    if (!names)
    {
        fprintf(stderr, "test-checker-deferred: a lane's per-name columns weren't laid out, kept, cleared or refused as they should be\n");
    }
    if (!keep && valid)
    {
        rmdir(directory);
    }
    arena_release(&option_arena);
    printf("test-checker-deferred: %u fixtures, %" PRIu64 " checks, %" PRIu64 " merged, %" PRIu64 " bodies read apart, %" PRIu64 " failures%s\n", fixtures, checks,
           merged, bodies, failures, valid ? "" : " (a fixture couldn't be written or preprocessed)");
    return valid && failures == 0 ? 0 : 1;
}
