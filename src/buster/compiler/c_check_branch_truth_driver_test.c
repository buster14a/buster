// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The bit before a branch condition's fold (#1340, check_cfg_decidable in c_check_program_cfg.c): a node whose bit is
// clear is one that check_program_branch_truth's fold leaves undecided and at which clang's CFG builder's rules fire no
// event. The fold is the only verdict, so the check is the fold's own answer at every node of a unit, asked every way a
// condition is (--branch-truth-census, check_driver_branch_truth_census), over:
//
// - a unit with a condition for each rule the bit is derived from and for each shape a rule's test rejects, whose census
//   is frozen: a rule that kept too little is unsound there, and one that kept everything changes the line;
// - tests/checker/cfg-pruning's units (#2990's conditions, the tautologies and the shapes), frozen too;
// - every unit of tests/driver/warnings that compiles alone;
// - generated units: functions of random conditions over the operators, the literals, the casts, the parentheses and the
//   statements the rules read, with the rules' own shapes among them (`v == 1 && v == 2`, `v || !v`, `(v & 8) == 4`).

#include <glob.h>

// A condition for each rule and each rejection, one a line: the comment says what the bit must do with it.
static const char check_driver_truth_rules[] =
    "enum E { A, B, C = 5 };\n"
    "struct S { int m; int a[4]; _Bool b; struct S* p; };\n"
    "int g(void);\n"
    "extern int table[4];\n"
    "int f(int x, int y, unsigned u, _Bool b, double d, struct S s, struct S* p, int* q)\n"
    "{\n"
    "    int r = 0;\n"
    "    if (x) r += 1;\n"                           // rejected: a value
    "    if (p->m == 0) r += 1;\n"                   // rejected: a literal beside no boolean
    "    if (x != 3 && y) r += 1;\n"                 // rejected
    "    if ((x & 8) == 0) r += 1;\n"                // rejected: a literal 0 beside `&`
    "    if (x & 8) r += 1;\n"                       // rejected once asked: no constant 0
    "    if (!(x & 8)) r += 1;\n"                    // rejected once asked
    "    if ((x & 8) && y) r += 1;\n"                // rejected once asked, the `&` as a first operand too
    "    r += (x & 16) ? 1 : 2;\n"                   // rejected once asked by the conditional
    "    if (x == 1 && y == 2) r += 1;\n"            // rejected: two variables
    "    if (x == 1 || p->m == 2) r += 1;\n"         // rejected: a value and a member
    "    if (!x && y) r += 1;\n"                     // rejected: a `!` beside another value's
    "    if (!x || p->m) r += 1;\n"                  // rejected: a `!` of a value beside a member
    "    if (q) r += 1;\n"                           // rejected: an lvalue's address constant
    "    if (s.m) r += 1;\n"                         // rejected: a member's
    "    if (x == y) r += 1;\n"                      // rejected: no literal
    "    if (1) r += 1;\n"                           // a constant
    "    if (0 && x) r += 1;\n"                      // through a logical operator
    "    if (x || 1) r += 1;\n"
    "    if (!0) r += 1;\n"                          // through `!`
    "    if ((_Bool)1) r += 1;\n"                    // through a cast
    "    if (__builtin_constant_p(x)) r += 1;\n"     // a query
    "    if (table) r += 1;\n"                       // an array's address
    "    if (g) r += 1;\n"                           // a function's
    "    if (\"s\") r += 1;\n"                       // a string literal's
    "    if (&x) r += 1;\n"                          // an object's
    "    if (x * 0) r += 1;\n"                       // `x * 0`
    "    if (0 * x) r += 1;\n"
    "    if (x & 0) r += 1;\n"
    "    if ((x & 0) && y) r += 1;\n"
    "    r += (x & 0) ? 1 : 2;\n"
    "    if (x | 1) r += 1;\n"                       // `x | 1`
    "    if (1 | x) r += 1;\n"
    "    if ((x & 8) == 4) r += 1;\n"                // the bitwise equality
    "    if (4 == (x & 8)) r += 1;\n"
    "    if ((x | 8) == 0) r += 1;\n"
    "    if ((x | 8) != -1) r += 1;\n"               // a literal `-` takes
    "    if ((x & 8) == ~0) r += 1;\n"
    "    if ((x < y) == 10) r += 1;\n"               // a boolean value and a literal above 1
    "    if (b == 2) r += 1;\n"                      // a _Bool's
    "    if (10 > b) r += 1;\n"                      // the relational check
    "    if (!x >= 2) r += 1;\n"
    "    if (x == 1 && x == 2) r += 1;\n"            // two comparisons of one variable
    "    if (1 < x || 5 > x) r += 1;\n"              // their constants on the left
    "    if (p->m == A && p->m == B) r += 1;\n"      // a member and enumerators
    "    if (s.a[1] < 2 && s.a[1] > 3) r += 1;\n"    // an element
    "    if (d < 1.5 && d > 2.5) r += 1;\n"          // floating constants
    "    if (x || !x) r += 1;\n"                     // a value and its negation
    "    if (!x && x) r += 1;\n"
    "    if (p->m && !p->m) r += 1;\n"               // a member's
    "    if (s.a[1] || !s.a[1]) r += 1;\n"           // an element's
    "    if ((x == 1 && x == 2) || y) r += 1;\n"     // a logical operator's operand
    "    do { r += 1; } while (x * 0);\n"            // a do statement's condition
    "    while (y && (x | 1)) { r += 1; break; }\n"
    "    r += (x & 8) == 4 ? 1 : 2;\n"               // a conditional's
    "    r += x ?: 2;\n"                             // GNU's, rejected
    "    if (*(int (*)[4])4096) r += 1;\n"           // an lvalue array's address, with no object
    "    if (0 * (x + y)) r += 1;\n"                 // operators whose operands aren't the nodes before them
    "    if (x * (char)0) r += 1;\n"
    "    r += x * (char)0;\n"                        // a product nobody asks
    "    if (1 | (x + y)) r += 1;\n"
    "    if (x && (char)0) r += 1;\n"
    "    if (!(x && (char)0)) r += 1;\n"
    "    if ((_Bool)(x && (char)0)) r += 1;\n"
    "    if ((x && (char)0) || y) r += 1;\n"
    "    if (y && (x && (char)0)) r += 1;\n"
    "    if (x == -1 && x == -2) r += 1;\n"
    "    if (x != -2 || x != -1) r += 1;\n"
    "    if (y && x * (char)0) r += 1;\n"
    "    return r;\n"
    "}\n"
    "_Bool h(int x)\n"                               // through a return's conversion
    "{\n"
    "    return x && (char)0;\n"
    "}\n";

// A unit with a wrapper of _Bool (an aligned one), where a value's type alone doesn't say it is boolean.
static const char check_driver_truth_wrapped[] =
    "typedef _Bool __attribute__((aligned(4))) Wide;\n"
    "int f(Wide w, _Bool b, int x, int y)\n"
    "{\n"
    "    int r = 0;\n"
    "    if (w == 2) r += 1;\n"                     // an aligned _Bool's
    "    if (3 < w) r += 1;\n"
    "    if (b == 2) r += 1;\n"
    "    if (x == 2) r += 1;\n"                     // rejected
    "    if (x != 3 && y) r += 1;\n"                // rejected
    "    return r;\n"
    "}\n";

// xorshift64*.
static uint32_t check_driver_truth_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return (uint32_t)((value * UINT64_C(2685821657736338717)) >> 32);
}

// A generated unit's expressions come from a stack of pieces: text, written as it is, or an expression to expand `depth`
// levels deep, of one of three kinds. An integer operand of an arithmetic operator or a comparison
// (BUSTER_CHECK_TRUTH_INTEGER, `I` in a production) is an atom or a parenthesized production, so that no operator
// captures another's operand and no comparison chains. An operand of a logical operator, of `!` or of a cast, and a
// condition itself, is written bare or parenthesized at random, which is where the rules tell the two apart: an integer
// one (BUSTER_CHECK_TRUTH_LOGICAL, `L`) or any scalar (BUSTER_CHECK_TRUTH_SCALAR, `S`: a pointer, a function, an
// array, a floating value). A production's operands are pushed in reverse.
#define BUSTER_CHECK_TRUTH_TEXT 0u
#define BUSTER_CHECK_TRUTH_INTEGER 1u
#define BUSTER_CHECK_TRUTH_LOGICAL 2u
#define BUSTER_CHECK_TRUTH_SCALAR 3u
#define BUSTER_CHECK_TRUTH_STACK 512u

typedef struct CheckDriverTruthPiece CheckDriverTruthPiece;
struct CheckDriverTruthPiece
{
    const char* text;
    uint32_t length;
    uint8_t kind;
    uint8_t depth;
};

typedef struct CheckDriverTruthText CheckDriverTruthText;
struct CheckDriverTruthText
{
    char* bytes;
    size_t length;
    size_t capacity;
    bool full;
};

static void check_driver_truth_put(CheckDriverTruthText* text, const char* bytes, size_t length)
{
    text->full |= text->length + length + 1 > text->capacity;
    if (!text->full)
    {
        memcpy(text->bytes + text->length, bytes, length);
        text->length += length;
        text->bytes[text->length] = 0;
    }
}

// One expression of `kind`, `depth` levels deep at most.
static void check_driver_truth_expression(CheckDriverTruthText* text, uint64_t* state, uint8_t kind, uint8_t depth)
{
    static const char* const integers[] = {
        "x", "y", "u", "b", "e", "s.m", "p->m", "s.a[1]", "s.a[x]", "arr[2]", "*q", "s.b", "p->p->m",
        "0", "1", "2", "4", "8", "-1", "3", "'c'", "0u", "1l", "A", "B", "C", "sizeof(int)", "__builtin_constant_p(x)", "g()", "(0)", "(x)", "(2)",
    };
    static const char* const scalars[] = { "q", "&x", "\"s\"", "arr", "g", "s.a", "(int*)0", "d", "1.5", "p", "s.p" };
    // The operators, and the rules' own shapes over one variable.
    static const char* const productions[] = {
        "- I", "~ I", "! S", "+ I", "(int)I", "(_Bool)S", "(char)I", "(unsigned)I", "(long)I", "(int)(unsigned char)I",
        "I * I", "I & I", "I | I", "I ^ I", "I + I", "I == I", "I != I", "I < I", "I <= I", "I > I", "I >= I",
        "S && S", "S || S", "L && L", "L || L", "L && L && L", "L || L || L", "L && L || L", "(S ? I : I)", "(y = I)", "(I, I)",
        "I * 0", "0 * I", "0 & I", "I & (0)", "I | 1", "2 | I", "I | (char)3", "I | -1", "I | A", "I | C",
        "(I & 8) == 4", "4 != (I & 8)", "(I | 8) == 0", "(I & 8) == 0", "(I & 8) == -1", "(I | 2) != ~0", "(I < I) == 10", "b == 2", "10 > b", "s.b < 3", "! I >= 2",
        "x == 1 && x == 2", "x < 3 || x > 1", "1 < x || 5 > x", "p->m == A && p->m == B", "s.a[1] < 2 && s.a[1] > 3", "d < 1.5 && d > 2.5", "x == -1 || x != (char)2",
        "u > 4 && u < 3", "e == A && e == C", "x == 1 && y == 2", "x || !x", "!x && x", "p->m && !p->m", "s.a[1] || !s.a[1]", "!x || y", "x && !p->m", "b && !b",
        "d < I", "I == 0", "I != 1", "__builtin_expect(I, 1)",
    };
    CheckDriverTruthPiece stack[BUSTER_CHECK_TRUTH_STACK];
    uint32_t top = 1;
    stack[0] = (CheckDriverTruthPiece){ .kind = kind, .depth = depth };
    while (top != 0)
    {
        top -= 1;
        const CheckDriverTruthPiece piece = stack[top];
        const uint32_t roll = check_driver_truth_random(state);
        if (piece.kind == BUSTER_CHECK_TRUTH_TEXT)
        {
            check_driver_truth_put(text, piece.text, piece.length);
        }
        else if (piece.depth == 0 || roll % 4 == 0 || top + 16 > BUSTER_CHECK_TRUTH_STACK)
        {
            // An atom: a scalar expression's is an integer one's three times in four.
            const bool scalar = piece.kind == BUSTER_CHECK_TRUTH_SCALAR && (roll >> 8) % 4 == 0;
            const char* atom = scalar ? scalars[(roll >> 16) % BUSTER_ARRAY_LENGTH(scalars)] : integers[(roll >> 16) % BUSTER_ARRAY_LENGTH(integers)];
            check_driver_truth_put(text, atom, strlen(atom));
        }
        else
        {
            // A production, parenthesized or not: its pieces in reverse, the last one first.
            const char* production = productions[(roll >> 8) % BUSTER_ARRAY_LENGTH(productions)];
            const bool enclosed = piece.kind == BUSTER_CHECK_TRUTH_INTEGER || (roll >> 3) % 3 == 0;
            const size_t length = strlen(production);
            if (enclosed)
            {
                stack[top++] = (CheckDriverTruthPiece){ .text = ")", .length = 1 };
            }
            size_t end = length;
            for (size_t at = length; at-- > 0;)
            {
                const char letter = production[at];
                if (letter == 'I' || letter == 'L' || letter == 'S')
                {
                    stack[top++] = (CheckDriverTruthPiece){ .text = production + at + 1, .length = (uint32_t)(end - at - 1) };
                    stack[top++] = (CheckDriverTruthPiece){ .kind = letter == 'I' ? BUSTER_CHECK_TRUTH_INTEGER : letter == 'L' ? BUSTER_CHECK_TRUTH_LOGICAL :
                                                                                                                              BUSTER_CHECK_TRUTH_SCALAR,
                                                            .depth = (uint8_t)(piece.depth - 1) };
                    end = at;
                }
            }
            stack[top++] = (CheckDriverTruthPiece){ .text = production, .length = (uint32_t)end };
            if (enclosed)
            {
                stack[top++] = (CheckDriverTruthPiece){ .text = "(", .length = 1 };
            }
        }
    }
}

// A generated unit: `functions` functions of six statements each, a condition apiece.
static void check_driver_truth_unit(CheckDriverTruthText* text, uint64_t* state, uint32_t functions)
{
    static const char prelude[] =
        "enum E { A, B, C = 5 };\n"
        "struct S { int m; int a[4]; _Bool b; struct S* p; };\n"
        "int g(void);\n";
    static const char* const statements[][2] = {
        { "    if (", ") r += 1;\n" },
        { "    while (", ") { r += 2; break; }\n" },
        { "    do { r += 3; } while (", ");\n" },
        { "    for (; ", "; ) { r += 4; break; }\n" },
        { "    r += (", ") ? 5 : 6;\n" },
        { "    if (", ") r += 7; else r -= 7;\n" },
    };
    check_driver_truth_put(text, prelude, sizeof(prelude) - 1);
    for (uint32_t f = 0; f < functions; f += 1)
    {
        char header[192];
        const int size = snprintf(header, sizeof(header), "int f%u(int x, int y, unsigned u, _Bool b, double d, struct S s, struct S* p, int* q, int arr[4], enum E e)\n{\n    int r = 0;\n", f);
        check_driver_truth_put(text, header, (size_t)size);
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(statements); s += 1)
        {
            const uint32_t roll = check_driver_truth_random(state);
            check_driver_truth_put(text, statements[s][0], strlen(statements[s][0]));
            check_driver_truth_expression(text, state, s == 4 ? BUSTER_CHECK_TRUTH_LOGICAL : BUSTER_CHECK_TRUTH_SCALAR, (uint8_t)(1 + roll % 4));
            check_driver_truth_put(text, statements[s][1], strlen(statements[s][1]));
        }
        static const char gnu[] = "    r += (";
        static const char tail[] = ") ?: 8;\n    return r;\n}\n";
        check_driver_truth_put(text, gnu, sizeof(gnu) - 1);
        check_driver_truth_expression(text, state, BUSTER_CHECK_TRUTH_LOGICAL, 2);
        check_driver_truth_put(text, tail, sizeof(tail) - 1);
    }
}

// A unit's census: whether the compile printed one, and its counts.
typedef struct CheckDriverTruthCensus CheckDriverTruthCensus;
struct CheckDriverTruthCensus
{
    unsigned nodes;
    unsigned kept;
    unsigned decided;
    unsigned events;
    unsigned conditions;
    unsigned condition_kept;
    unsigned condition_decided;
    unsigned bits;
    unsigned unsound;
    bool printed;
};

static CheckDriverTruthCensus check_driver_truth_census(const char* executable, const char* source)
{
    char* const arguments[] = { (char*)executable, "--branch-truth-census", "-w", "-fsyntax-only", (char*)source, NULL };
    char* text = NULL;
    CheckDriverTruthCensus census = { 0 };
    (void)check_test_driver_spawn(arguments, &text);
    const char* line = text != NULL ? strstr(text, "branch-truth-census\t") : NULL;
    census.printed = line != NULL &&
                     sscanf(line, "branch-truth-census\tnodes\t%u\tkept\t%u\tdecided\t%u\tevents\t%u\tconditions\t%u\tkept\t%u\tdecided\t%u\tbits\t%u\tunsound\t%u\n", &census.nodes,
                            &census.kept, &census.decided, &census.events, &census.conditions, &census.condition_kept, &census.condition_decided, &census.bits,
                            &census.unsound) == 9;
    free(text);
    return census;
}

static void check_test_driver_branch_truth(CheckTest* test, const char* clang, const char* executable, const char* directory)
{
    (void)clang;
    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/branch_truth.c", directory);
    // The rules' unit and #2990's fixtures, frozen: conditions, those the bit keeps and those the fold decides.
    static const struct
    {
        const char* file;
        unsigned conditions;
        unsigned kept;
        unsigned decided;
        unsigned events;
    } frozen[] = {
        { NULL, 99, 54, 47, 23 },
        { "", 6, 3, 3, 0 },
        { "checker/cfg-pruning/issue.c", 24, 20, 17, 10 },
        { "checker/cfg-pruning/runtime.c", 2, 2, 1, 0 },
        { "checker/cfg-pruning/shapes.c", 78, 46, 31, 23 },
        { "checker/cfg-pruning/tautologies.c", 57, 34, 22, 34 },
    };
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(frozen); i += 1)
    {
        char path[PATH_MAX];
        bool ready = true;
        const bool rules = frozen[i].file == NULL;
        if (rules || frozen[i].file[0] == 0)
        {
            ready = parse_test_write(source, rules ? check_driver_truth_rules : check_driver_truth_wrapped,
                                     rules ? sizeof(check_driver_truth_rules) - 1 : sizeof(check_driver_truth_wrapped) - 1);
            snprintf(path, sizeof(path), "%s", source);
        }
        else
        {
            snprintf(path, sizeof(path), "%s/%s", check_test_fixture_root, frozen[i].file);
        }
        const CheckDriverTruthCensus census = ready ? check_driver_truth_census(executable, path) : (CheckDriverTruthCensus){ 0 };
        const bool same = census.printed && census.bits == 1 && census.unsound == 0 && census.conditions == frozen[i].conditions && census.condition_kept == frozen[i].kept &&
                          census.condition_decided == frozen[i].decided && census.events == frozen[i].events;
        if (!same)
        {
            fprintf(stderr, "branch truth: %s: printed %d bits %u unsound %u conditions %u kept %u decided %u events %u\n", rules ? "the rules' unit" : frozen[i].file[0] == 0 ? "the wrapped _Bool's unit" : frozen[i].file,
                    census.printed, census.bits, census.unsound, census.conditions, census.condition_kept, census.condition_decided, census.events);
        }
        check_test_expect(test, same, "a branch condition's bit is sound on the frozen unit, and keeps and decides the conditions it did");
    }
    // The warnings' fixtures: every one that compiles alone.
    char pattern[PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s/driver/warnings/*/*.c", check_test_fixture_root);
    glob_t found = { 0 };
    const bool listed = glob(pattern, 0, NULL, &found) == 0;
    uint32_t censused = 0;
    uint32_t unsound = 0;
    for (size_t i = 0; listed && i < found.gl_pathc; i += 1)
    {
        const CheckDriverTruthCensus census = check_driver_truth_census(executable, found.gl_pathv[i]);
        // (A unit with no node has no bits.)
        censused += census.printed && census.bits == 1 ? 1 : 0;
        if (census.printed && census.unsound != 0)
        {
            unsound += 1;
            fprintf(stderr, "branch truth: %s: unsound %u\n", found.gl_pathv[i], census.unsound);
        }
    }
    if (listed)
    {
        globfree(&found);
    }
    check_test_expect(test, listed && censused >= 400 && unsound == 0, "a branch condition's bit is sound on the warnings' fixtures");
    // Generated units.
    CheckDriverTruthText text = { .capacity = 1u << 20 };
    text.bytes = malloc(text.capacity);
    uint64_t state = UINT64_C(0x1340134013401340);
    uint64_t conditions = 0;
    uint64_t kept = 0;
    uint64_t decided = 0;
    uint64_t events = 0;
    bool sound = text.bytes != NULL;
    for (uint32_t unit = 0; sound && unit < 12; unit += 1)
    {
        text.length = 0;
        check_driver_truth_unit(&text, &state, 48);
        const bool written = !text.full && parse_test_write(source, text.bytes, text.length);
        const CheckDriverTruthCensus census = written ? check_driver_truth_census(executable, source) : (CheckDriverTruthCensus){ 0 };
        sound = census.printed && census.bits == 1 && census.unsound == 0;
        conditions += census.conditions;
        kept += census.condition_kept;
        decided += census.condition_decided;
        events += census.events;
        if (!sound)
        {
            char kept_source[PATH_MAX];
            snprintf(kept_source, sizeof(kept_source), "%s/branch_truth_unsound.c", directory);
            (void)parse_test_write(kept_source, text.bytes, text.length);
            fprintf(stderr, "branch truth: generated unit %u (kept as %s): printed %d bits %u unsound %u\n", unit, kept_source, census.printed, census.bits, census.unsound);
        }
    }
    free(text.bytes);
    // (The generator's conditions are mostly the rules' own shapes: most decide, and some of each kind must.)
    check_test_expect(test, sound && conditions >= 4000 && decided >= 500 && decided <= kept && kept < conditions && events >= 100,
                      "a branch condition's bit is sound on generated conditions, some decided and some rejected");
    unlink(source);
}
