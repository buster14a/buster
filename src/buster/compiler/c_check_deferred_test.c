// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// test-checker-deferred: #113's deferred bodies (c_check_deferred.c) on fixtures of the issue's cases. Each fixture is
// written to a file and checked as test-checker-corpus checks one (check_corpus_measure): on the scalar path, the
// vector path, and the vector path with every body read apart, which must give the vector path's results exactly,
// column by column, its diagnostics among them. It runs on 1, 2, 3 and 8 lanes through the stacks, the batch and the
// direct binding resolution, on every lane the bodies can take (CheckDeferred.every_lane), and each fixture says
// whether the deferred reader must merge it or leave it to source order. A merged unit's bodies must run on that many
// lanes, and its reader be copied by one of them at least and by no more (#432's review). Each fixture is read again on
// 8 lanes by the reader's rule of how many (one lane, and one more per BUSTER_CHECK_DEFERRED_LANE_TOKENS of the bodies'
// tokens beyond twice the largest body's), and six of bodies of known tokens are read by the rule alone: on each side
// of a second lane and of an eighth, and beside a largest body, which leaves the others fewer tokens. A unit that
// merges, but the rule's sides, is checked again with its first lane taking every part, when no other lane may copy the
// reader; with its merge failing after the replay, which must give source order's result; and with the allocation of
// the deferred reader's state failing, which must count as a resource failure, not as a result that differs. The many
// bodies are read again with the reader timing what its lanes cost (CheckDeferred.timing), on one lane, which must
// wait for no other, and on 8, and lane_run's dispatch and the wake-up must fall within the lanes' span. Then
// benchmark-checker-deferred's --cpus parser, and the difference report on a deferred path that made no input, which
// must say so rather than read its columns.
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
};

static const CheckDeferredFixture check_deferred_fixtures[] =
{
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
    // A tag first named in a file-scope declarator (a prototype's parameter, a sizeof in an array's bound, a
    // definition's parameter) ends with the declarator in source order's reader, which the history doesn't say: a later
    // body that names the tag makes one of its own. The last declares the tag first, which every body sees.
    { "tag-in-a-prototype", "void g(struct S *p);\nint f(void) { struct S *q = 0; return q != 0; }\n", true },
    { "tag-in-a-prototype-then-passed", "void g(struct S *p);\nvoid f(void) { struct S *q = 0; g(q); }\n", true },
    { "tag-in-a-prototype-two-bodies", "void g(struct S *p);\nint f1(void) { struct S *q = 0; return q != 0; }\nint f2(void) { struct S *r = 0; return r != 0; }\n", true },
    { "tag-in-an-array-bound", "int a[sizeof(struct X *)];\nint f(void) { struct X *q = 0; return q != 0; }\n", true },
    { "tag-in-a-definitions-parameters", "int h(struct T *p) { struct T *q = p; return q != 0; }\n", true },
    { "tag-declared-before-a-prototype", "struct S;\nvoid g(struct S *p);\nint f(void) { struct S *q = 0; return q != 0; }\n", true },
    // Block-scope declarations with linkage name the file-scope entity, which later declarations see: source order's.
    { "block-scope-extern", "int g = 3;\nint f(void) { extern int g; return g; }\n", false },
    { "block-scope-function", "int f(void) { int h(int); return h(1); }\nint h(int x) { return x; }\n", false },
    // What a body appends to the reader's tables: strings and __func__, statement expressions, VLAs, varargs, asm
    // statements, cached constants, compound literals and statics.
    { "strings-and-function-names", "const char* top = \"top\";\nconst char* f(void) { return __func__; }\nconst char* g(void) { const char* s = \"body\"; return s + sizeof __func__; }\n", true },
    { "statement-expressions", "int f(int a) { int r = ({ int t = a + 1; t * 2; }); return r + ({ a; }); }\nint g(int b) { return ({ int u = b; u; }); }\n", true },
    { "variable-length-arrays", "int f(int n, int a[n]) { int b[n]; b[0] = a[0]; return (int)sizeof b + b[0]; }\nint g(int m) { int c[m][2]; return (int)sizeof c; }\n", true },
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
    // A union made transparent in a body applies to what follows it: source order's.
    { "transparent-union-in-a-body", "union U { int* a; long* b; };\nint f(void) { typedef union U T __attribute__((transparent_union)); return 0; }\n", false },
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
    // top level made: source order's, whose completion everything after the body sees. And one that only uses it.
    { "parameter-list-tag-completed-in-its-body", "int f(struct T *p) { struct T { int x; }; return p->x; }\nint g(void) { return 2; }\n", false },
    { "parameter-list-tag-used-in-its-body", "int f(struct T *p) { struct T *q = p; return q != 0; }\nint g(void) { return 2; }\n", true },
};

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

// The lanes a unit's bodies run on, written again from the rule (docs/c-deferred-bodies.md, "How many lanes"): one, and
// one more per BUSTER_CHECK_DEFERRED_LANE_TOKENS of their tokens beyond twice the largest body's, or with `every` as
// many as there are lanes; and no more than `lanes` or the bodies, none without bodies.
static uint64_t check_deferred_test_lanes(uint64_t bodies, uint64_t tokens, uint64_t largest, uint64_t lanes, bool every)
{
    const uint64_t beyond = tokens > largest * 2 ? tokens - largest * 2 : 0;
    const uint64_t wanted = every ? lanes : 1 + beyond / BUSTER_CHECK_DEFERRED_LANE_TOKENS;
    const uint64_t most = bodies < lanes ? bodies : lanes;
    return wanted < most ? wanted : most;
}

// The rule's sides, with BUSTER_CHECK_DEFERRED_LANE_TOKENS (N): `bodies` functions of empty statements, whose bodies
// have N tokens each from their opening brace to their closing one, but the first `first` times as many and the last
// `fewer` fewer; and the lanes the rule gives them on 8. A second lane needs N tokens beyond twice the largest body's,
// and an eighth seven times as many, each with a token fewer too; and a largest body of four times N beside four of N, a
// token fewer, which leave one lane, and beside five, which take two.
typedef struct CheckDeferredSize CheckDeferredSize;
struct CheckDeferredSize
{
    const char* name;
    uint32_t bodies;
    uint32_t first;
    uint32_t fewer;
    uint32_t lanes;
};

static const CheckDeferredSize check_deferred_sizes[] =
{
    { "lane-tokens-below-two-lanes", 3, 1, 1, 1 },
    { "lane-tokens-two-lanes", 3, 1, 0, 2 },
    { "lane-tokens-below-eight-lanes", 9, 1, 1, 7 },
    { "lane-tokens-eight-lanes", 9, 1, 0, 8 },
    { "lane-tokens-large-body-and-four", 5, 4, 1, 1 },
    { "lane-tokens-large-body-and-five", 6, 4, 0, 2 },
};

// A CheckDeferredSize's unit: its bodies and the tokens they hold in all, and the largest one's.
static void check_deferred_sized(char* text, uint64_t capacity, const CheckDeferredSize* size, uint64_t* tokens, uint64_t* largest)
{
    uint64_t length = 0;
    *tokens = 0;
    *largest = 0;
    for (uint32_t b = 0; b < size->bodies && length < capacity; b += 1)
    {
        const uint64_t body = (uint64_t)BUSTER_CHECK_DEFERRED_LANE_TOKENS * (b == 0 ? size->first : 1) - (b + 1 == size->bodies ? size->fewer : 0);
        length += (uint64_t)snprintf(text + length, capacity - length, "void f%u(void) {", b);
        for (uint64_t i = 2; i < body && length < capacity; i += 1)
        {
            length += (uint64_t)snprintf(text + length, capacity - length, i % 64 == 0 ? ";\n" : ";");
        }
        length += length < capacity ? (uint64_t)snprintf(text + length, capacity - length, "}\n") : 0;
        *tokens += body;
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
        if (size != 0)
        {
            check_deferred_sized(sized, sizeof(sized), size, &size_tokens, &size_largest);
        }
        const char* name = f < listed ? check_deferred_fixtures[f].name : size == 0 ? "many-bodies" : size->name;
        const char* source = f < listed ? check_deferred_fixtures[f].source : size == 0 ? many : sized;
        const bool expected = f < listed ? check_deferred_fixtures[f].merged : true;
        const bool every = size == 0;
        char path[256];
        snprintf(path, sizeof(path), "%s/%s.c", directory, name);
        valid = parse_test_write(path, source, strlen(source));
        char* options_text[2] = { "-std=gnu17", path };
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
                const uint64_t most = check_deferred_test_lanes(outcome.deferred_bodies, outcome.deferred_body_tokens, outcome.deferred_largest_body,
                                                                lane_counts[l], every);
                const bool copied = !outcome.deferred || (outcome.deferred_active == most && outcome.deferred_copies >= (most != 0) &&
                                                          outcome.deferred_copies <= most);
                const bool sized_right = size == 0 || (outcome.deferred_bodies == size->bodies && outcome.deferred_body_tokens == size_tokens &&
                                                       outcome.deferred_largest_body == size_largest &&
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
                            copied ? "" : " (not the rule's lanes, or its reader copied by too many of them or by none)",
                            sized_right ? "" : " (not the bodies' tokens it was made with, or not the lanes it was made for)");
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
            const uint64_t most = check_deferred_test_lanes(outcome.deferred_bodies, outcome.deferred_body_tokens, outcome.deferred_largest_body, 8, false);
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
        // The many bodies with the reader timing what its lanes cost (CheckDeferred.timing), on one lane and on every one
        // of 8: no wake-up on one lane, which has no other to wait for, and lane_run's dispatch and the wake-up within
        // the lanes' span.
        for (uint32_t l = 0; valid && f == listed && l < 2; l += 1)
        {
            check_corpus_deferred_lanes = l == 0 ? 1 : 8;
            check_corpus_deferred_bindings = CHECK_INPUT_BINDINGS_STACKS;
            check_corpus_deferred_timing = true;
            CheckCorpusOutcome outcome;
            check_corpus_measure(path, &options, false, true, &outcome);
            check_corpus_deferred_timing = false;
            const bool timed = outcome.deferred_active == check_corpus_deferred_lanes && outcome.deferred_lanes_ns != 0 &&
                               (l != 0 || outcome.deferred_wake_ns == 0) &&
                               outcome.deferred_dispatch_ns + outcome.deferred_wake_ns <= outcome.deferred_lanes_ns;
            const bool passed = outcome.prepared && outcome.equal && outcome.deferred_equal && outcome.deferred && timed;
            checks += 1;
            failures += !passed;
            if (!passed)
            {
                fprintf(stderr, "test-checker-deferred: %s on %u lanes, timed: %s (span %" PRIu64 " ns, dispatch %" PRIu64 " ns, wake-up %" PRIu64 " ns)\n", name,
                        check_corpus_deferred_lanes, !outcome.prepared ? "not preprocessed" : !outcome.deferred_equal ? "the deferred result differs" :
                        !outcome.equal ? "the scalar and vector paths differ" : !outcome.deferred ? "not merged" :
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
    if (!keep && valid)
    {
        rmdir(directory);
    }
    arena_release(&option_arena);
    printf("test-checker-deferred: %u fixtures, %" PRIu64 " checks, %" PRIu64 " merged, %" PRIu64 " bodies read apart, %" PRIu64 " failures%s\n", fixtures, checks,
           merged, bodies, failures, valid ? "" : " (a fixture couldn't be written or preprocessed)");
    return valid && failures == 0 ? 0 : 1;
}
