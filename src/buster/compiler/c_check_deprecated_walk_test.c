// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The deprecated-use pass's vector walks (#1489, check_input_availability_events and check_input_deprecated_block) against
// its scalar walk, which visits every token: the same reports, warning events and availability contexts, with each source
// shifted by 0 to 66 tokens of empty declarations, so that every token, scope brace and use falls at each place in its
// 64-token block. The scalar walk is the oracle; test-checker-driver compares both with clang.

static const char* const check_test_deprecated_walk_sources[] = {
    "void f(void) __attribute__((deprecated));\nvoid g(void) __attribute__((deprecated(\"use h\")));\n[[deprecated]] int v;\n"
    "[[deprecated(\"old\")]] int w;\ntypedef int T __attribute__((deprecated));\nstruct __attribute__((deprecated)) S { int a; };\n"
    "enum E { A __attribute__((deprecated)), B };\nint main(void)\n{\n    f();\n    g();\n    T t = v + w + A;\n    struct S s;\n"
    "    (void)t; (void)s;\n    void (*p)(void) = f;\n    (void)p;\n    return sizeof(v);\n}\nvoid d(void) __attribute__((deprecated));\n"
    "void d(void) { f(); }\nvoid e(void);\nvoid e(void) __attribute__((deprecated));\nvoid e(void);\nT x __attribute__((deprecated));\n"
    "struct S;\nstruct S *ps;\nenum __attribute__((deprecated)) F { F0 };\nenum F ef;\nint k(void) { e(); return F0; }\n",
    "void u(void) __attribute__((unavailable(\"gone\")));\nint h(void) { return sizeof(u); }\nvoid z(void) { u(); }\n",
    "struct S { int x __attribute__((deprecated(\"msg\"))); int y; struct { int z __attribute__((deprecated)); }; };\n"
    "union U { int a [[deprecated]]; };\nstruct S g(void);\nint use(struct S *p, union U *q) { return p->x + p->z + q->a + g().y; }\n"
    "__attribute__((deprecated)) int m(struct S *p) { return p->x; }\n__attribute__((unavailable)) int n(struct S *p) { return p->x; }\n"
    "struct S t;\n__attribute__((deprecated)) int q = sizeof(t.x);\nint r __attribute__((deprecated)) = sizeof(t.z);\n"
    "int w(void) { __attribute__((deprecated)) int l = t.x; return 0; }\nint s2[sizeof(t.x)];\ntypeof(t.z) s3;\n",
    "void k(void) { extern int v __attribute__((deprecated(\"old\"))); { extern int v __attribute__((unavailable(\"gone\"))); } (void)v; }\n"
    "void m(void) { extern int w __attribute__((unavailable(\"gone\"))); }\nextern int w;\nint f(void) { return w; }\n",
    "struct __attribute__((deprecated(\"old\"))) S;\nstruct S *p;\nstruct __attribute__((unavailable(\"gone\"))) S;\nstruct S;\n"
    "struct S *q;\nstruct __attribute__((unavailable)) U { int a; };\nstruct U *r;\n",
    "enum __attribute__((unavailable(\"gone\"))) E { A };\nenum __attribute__((deprecated(\"old\"), unavailable(\"gone\"))) F { B };\n"
    "typedef int T __attribute__((deprecated(\"old\")));\ntypedef int T __attribute__((unavailable(\"gone\")));\nint x = A + B;\nT t;\n",
    "typedef int T __attribute__((unavailable(\"gone\")));\nT x __attribute__((deprecated)), y;\n"
    "enum { E __attribute__((unavailable(\"gone\"))) = 1, D __attribute__((deprecated(\"old\"))) = 2 };\n"
    "int c __attribute__((unavailable)) = E, d;\nint e __attribute__((deprecated)) = D, f;\n__attribute__((deprecated)) int g = D, h;\n"
    "typeof(E) n __attribute__((unavailable));\n__attribute__((deprecated)) typeof(D) p;\n",
    "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
    "__attribute__((unavailable)) int f(T x);\n__attribute__((unavailable)) int g(T);\n"
    "__attribute__((unavailable)) int h(T x) { return x; }\n__attribute__((deprecated)) int i(D x);\n"
    "__attribute__((unavailable)) int j(D);\nint k(T x) __attribute__((unavailable));\n"
    "__attribute__((deprecated)) int (*fp)(D x);\n__attribute__((deprecated)) int m(int (*q)(D));\n"
    "void n(void) { __attribute__((deprecated)) int p(D x); __attribute__((unavailable)) int (*r)(T); (void)r; }\n",
    "typedef int D __attribute__((deprecated(\"old\")));\n__attribute__((deprecated)) int x[sizeof(D)];\n"
    "int y __attribute__((deprecated)), z[sizeof(D)];\n__attribute__((deprecated)) int (*fp)[sizeof(D)];\n"
    "__attribute__((deprecated)) int w[2][sizeof(D)];\nvoid f(void) { __attribute__((deprecated)) int v[sizeof(D)]; (void)v; }\n"
    "__attribute__((deprecated)) enum { A = sizeof(D) } e;\n__attribute__((deprecated)) typeof((int){ sizeof(D) }) c;\n"
    "__attribute__((deprecated)) int i = sizeof(struct { D m; });\n__attribute__((deprecated)) int f2(struct { D m; } *p);\n"
    "__attribute__((deprecated)) void g(void) { struct { D m; } s; enum { C = sizeof(D) }; (void)s; }\n",
    "typedef int T __attribute__((unavailable(\"gone\")));\ntypedef int D __attribute__((deprecated(\"old\")));\n"
    "struct __attribute__((unavailable)) S { T m; };\nstruct __attribute__((deprecated)) R { D m; };\n"
    "enum __attribute__((deprecated)) E { A = sizeof(D) };\nunion __attribute__((deprecated)) U { D m; };\n"
    "struct __attribute__((deprecated)) N { struct { D n; } m; };\nenum __attribute__((deprecated)) { B = sizeof(D) };\n"
    "enum __attribute__((deprecated)) F : D { C };\nenum G { H = sizeof(D) } __attribute__((deprecated));\n",
    "struct __attribute__((unavailable(\"gone\"))) S;\nstruct S { int m; };\nstruct __attribute__((deprecated(\"old\"))) R;\n"
    "struct R { int m; };\nstruct __attribute__((deprecated(\"old\"))) Q;\nstruct Q { int m; } q1, q2;\n"
    "typedef struct __attribute__((deprecated(\"old\"))) { int m; } AT;\nAT a1, a2;\nvoid fa(void) { AT a3; (void)a3; }\n",
    "typedef int D __attribute__((deprecated(\"old\")));\nD a, b;\nD c __attribute__((deprecated)), e;\nD f, g __attribute__((deprecated));\n"
    "struct __attribute__((deprecated(\"t\"))) S { int m; };\nstruct S h, i;\nstruct S j __attribute__((deprecated)), k;\n"
    "struct S l, m __attribute__((deprecated));\nvoid n(void) { struct S o, q __attribute__((deprecated)); (void)o; (void)q; }\n"
    "struct X { struct S r, u; struct S v __attribute__((deprecated)), w; };\nstruct S (*fp)(void), x, *y = 0;\n"
    "typedef struct S T1, T2;\nvoid z(struct S a, struct S b);\nint size = sizeof(struct S);\n",
    "typedef unsigned T __attribute__((deprecated(\"old\")));\ntypedef unsigned V __attribute__((unavailable(\"gone\")));\nvoid f(void)\n{\n"
    "    enum E1 : T { A1 } a1 __attribute__((deprecated)), b1;\n    enum E2 : T { A2 } a2, b2 __attribute__((deprecated));\n"
    "    enum E3 : T { A3 };\n}\nstruct S\n{\n    enum G1 : T { C1 } m1 __attribute__((deprecated)), n1;\n"
    "    enum G2 : T { C2 } m2, n2 __attribute__((deprecated));\n};\nenum __attribute__((deprecated)) K : T { K1 } k;\n"
    "enum L : T { L1 } __attribute__((deprecated)) l;\nenum : T { N } z __attribute__((deprecated)), w;\nenum M : V { M1 } v __attribute__((deprecated));\n",
    // Nested scopes, levels and parameter lists around the uses, in blocks, records and calls, and an unmarked unit's
    // declarations around a marked one's.
    "int plain(int a, int b) { return a + b; }\nint old(int a) __attribute__((deprecated(\"new\")));\n"
    "struct R { int a; struct { int b; int c[sizeof(int)]; } in; };\n"
    "int f(int x)\n{\n    int s = 0;\n    for (int i = 0; i < x; i += 1)\n    {\n        { s += plain(old(i), (old)(i)); }\n"
    "        struct L { int (*fp)(int); int m[2]; } l = { old, { 1, 2 } };\n        s += l.fp(i) + plain((int){ old(1) }, ((old(2))));\n"
    "    }\n    if (s) { while (s > 1) { s = old(s) / 2; } }\n    return s + sizeof(struct { int q[sizeof(old(1))]; });\n}\n"
    "__attribute__((deprecated)) int g(int x) { return old(x) + plain(old(x), 1); }\nint h(int (*p)(int) __attribute__((deprecated)));\n"
    "int k[3] = { [0] = sizeof(old(1)), [2] = 2 };\n",
};

// A source's reading's diagnostics and contexts, by the scalar walk (path 0) or the vector one (path 1).
typedef struct CheckTestDeprecatedWalk CheckTestDeprecatedWalk;
struct CheckTestDeprecatedWalk
{
    InternTable identifiers;
    Preprocessor pp;
    Parser parser;
    CheckTestTypes types;
    Arena arena;
    Arena scratch;
    CheckInput input;
    bool read;
};

static void check_test_deprecated_walk_read(CheckTestDeprecatedWalk* walk, const char* source, uint32_t length, bool scalar)
{
    const PPOptions options = { .language = { .standard = 2023, .gnu = true }, .no_standard_includes = true };
    const ParseOptions parse_options = { .tree_only = true, .scalar = scalar };
    walk->arena = arena_reserve(UINT64_C(1) << 28);
    walk->scratch = arena_reserve(UINT64_C(1) << 28);
    const bool ready = walk->arena.base != 0 && walk->scratch.base != 0 && intern_initialize(&walk->identifiers) &&
                       pp_initialize(&walk->pp, &options, &walk->identifiers) && parse_initialize(&walk->parser, &parse_options, options.language) &&
                       check_test_types_open(&walk->types) && pp_run_source(&walk->pp, "deprecated-walk.c", (const uint8_t*)source, length) &&
                       parse_translation_unit(&walk->parser, &walk->pp);
    walk->read = ready && check_input_compile(&walk->input, &walk->parser, &walk->pp, &walk->types.table, &walk->arena, &walk->scratch, true,
                                              walk->parser.tokens.count, NULL);
}

static void check_test_deprecated_walk_release(CheckTestDeprecatedWalk* walk)
{
    check_test_types_close(&walk->types);
    parse_release(&walk->parser);
    pp_release(&walk->pp);
    intern_release(&walk->identifiers);
    arena_release(&walk->scratch);
    arena_release(&walk->arena);
}

// Whether two readings of a source gave the same reports, warning events and notes, and availability contexts, and how
// many of the deprecated pass's diagnostics they gave (`*uses`).
static bool check_test_deprecated_walk_equal(const CheckInput* a, const CheckInput* b, uint32_t* uses)
{
    bool equal = a->token_count == b->token_count && a->report_count == b->report_count && a->warning_events.count == b->warning_events.count &&
                 a->warning_events.note_count == b->warning_events.note_count && (a->availability_contexts == NULL) == (b->availability_contexts == NULL) &&
                 (a->availability_contexts == NULL || memcmp(a->availability_contexts, b->availability_contexts, (uint64_t)a->token_count + 1) == 0);
    for (uint32_t i = 0; equal && i < a->report_count; i += 1)
    {
        const CheckInputReport* x = a->reports + i;
        const CheckInputReport* y = b->reports + i;
        equal = x->token == y->token && x->message == y->message && x->note_token == y->note_token && x->note_kind == y->note_kind && x->detail == y->detail &&
                x->value == y->value;
        *uses += x->message == CHECK_INPUT_UNAVAILABLE_USE;
    }
    for (uint32_t i = 0; equal && i < a->warning_events.count; i += 1)
    {
        const CheckWarningEvent* x = a->warning_events.events + i;
        const CheckWarningEvent* y = b->warning_events.events + i;
        equal = x->id == y->id && x->kind == y->kind && x->token == y->token && x->completion == y->completion && x->payload.name == y->payload.name &&
                x->payload.token == y->payload.token && x->payload.data == y->payload.data && x->payload.first_type == y->payload.first_type &&
                x->note_first == y->note_first && x->note_count == y->note_count;
        *uses += x->kind == CHECK_WARNING_EVENT_DEPRECATED_USE;
    }
    for (uint32_t i = 0; equal && i < a->warning_events.note_count; i += 1)
    {
        const CheckWarningNote* x = a->warning_events.notes + i;
        const CheckWarningNote* y = b->warning_events.notes + i;
        equal = x->kind == y->kind && x->token == y->token && x->payload.name == y->payload.name && x->payload.data == y->payload.data;
    }
    return equal;
}

static __attribute__((noinline)) void check_test_deprecated_walks(CheckTest* test)
{
    // Up to 66 empty declarations before the source, then the source.
    char source[8192];
    uint32_t compared = 0;
    uint32_t uses = 0;
    for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(check_test_deprecated_walk_sources); s += 1)
    {
        const uint32_t length = (uint32_t)strlen(check_test_deprecated_walk_sources[s]);
        for (uint32_t shift = 0; shift <= 66 && shift + length < sizeof(source); shift += 1)
        {
            memset(source, ';', shift);
            memcpy(source + shift, check_test_deprecated_walk_sources[s], length);
            CheckTestDeprecatedWalk walks[2] = { 0 };
            for (uint32_t path = 0; path < 2; path += 1)
            {
                check_test_deprecated_walk_read(&walks[path], source, shift + length, path == 0);
            }
            char description[160];
            snprintf(description, sizeof(description), "deprecated walks: source %u after %u tokens, both paths read it", s, shift);
            check_test_expect(test, walks[0].read && walks[1].read, description);
            if (walks[0].read && walks[1].read)
            {
                snprintf(description, sizeof(description), "deprecated walks: source %u after %u tokens, the vector walk gives the scalar walk's diagnostics", s, shift);
                check_test_expect(test, check_test_deprecated_walk_equal(&walks[0].input, &walks[1].input, &uses), description);
                compared += 1;
            }
            for (uint32_t path = 0; path < 2; path += 1)
            {
                check_test_deprecated_walk_release(&walks[path]);
            }
        }
    }
    // The fixtures must reach the pass's diagnostics, not compare empty outputs.
    check_test_expect(test, compared != 0 && uses >= compared, "deprecated walks: the sources' uses are diagnosed");
}
