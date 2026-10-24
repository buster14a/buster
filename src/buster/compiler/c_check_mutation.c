// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Semantic mutations for test-checker-corpus (#133's last "Done when" item: acceptance and the first error's line
// against clang -fsyntax-only on inputs mutated semantically). Each unit that the corpus check compared without a
// difference, and that clang accepts, is printed as -E text with line markers: its base. The base is compared as a
// file first, so that a later difference belongs to a mutation, then read back and parsed by the scalar parser, whose
// tree (c_parser.c, "The tree") gives the places to mutate:
//   - a declaration's type: a pointer added or removed, its signedness or width changed, `const` added or removed, or
//     its type replaced by a struct (or a struct by `int`);
//   - a declaration removed: a local, a parameter, a file-scope declaration or a struct member;
//   - a binary operator's operands swapped, or two arguments of a call.
// A mutant is one edit of the base text, checked as the corpus check checks a file (check_corpus_file): acceptance and
// the first error's file and line against clang's, and the scalar and vector paths' every column against each other.
// Three mutants in four mutate the unit's own file, the fourth a file it includes. A difference is minimized
// (check_mutation_minimize: top-level declarations, statements, then tokens, blanked while clang and the checker still
// give the same results; then the line markers and blank lines dropped) and saved as checker_mutation_N.c in the
// current directory, for a test-checker fixture (c_check_mutation_test.c). This is test tooling outside the compile
// path, and has no vector path: the time goes into the checks and clang's processes.
//
// Layout: CheckMutationKind and the sites (check_mutation_discover), the edits (check_mutation_edit), the probes and
// the minimizer (check_mutation_probe, check_mutation_minimize, check_mutation_save), and the campaign
// (check_mutation_campaign), which run_checker_corpus runs for --semantic-mutations. Include from c_check_corpus.c,
// after check_corpus_file.
#ifndef BUSTER_C_CHECK_MUTATION_INCLUDED
#define BUSTER_C_CHECK_MUTATION_INCLUDED

typedef enum CheckMutationKind
{
    CHECK_MUTATION_POINTER_ADD,
    CHECK_MUTATION_POINTER_REMOVE,
    CHECK_MUTATION_SIGNEDNESS,
    CHECK_MUTATION_WIDTH,
    CHECK_MUTATION_CONST,
    CHECK_MUTATION_STRUCT,
    CHECK_MUTATION_REMOVE_LOCAL,
    CHECK_MUTATION_REMOVE_PARAMETER,
    CHECK_MUTATION_REMOVE_FILE_SCOPE,
    CHECK_MUTATION_REMOVE_MEMBER,
    CHECK_MUTATION_SWAP_OPERANDS,
    CHECK_MUTATION_SWAP_ARGUMENTS,
    CHECK_MUTATION_KIND_COUNT,
} CheckMutationKind;

static const char* const check_mutation_names[CHECK_MUTATION_KIND_COUNT] =
{
    "type-pointer-add",
    "type-pointer-remove",
    "type-signedness",
    "type-width",
    "type-const",
    "type-struct",
    "remove-local",
    "remove-parameter",
    "remove-file-scope",
    "remove-member",
    "swap-operands",
    "swap-arguments",
};

// The families mutants take in turn, each the kinds from its entry to the next one's: a type changed, a declaration
// removed, operands swapped.
#define BUSTER_CHECK_MUTATION_FAMILIES 3
static const uint8_t check_mutation_families[BUSTER_CHECK_MUTATION_FAMILIES + 1] =
{
    CHECK_MUTATION_POINTER_ADD,
    CHECK_MUTATION_REMOVE_LOCAL,
    CHECK_MUTATION_SWAP_OPERANDS,
    CHECK_MUTATION_KIND_COUNT,
};

// Tries at another site when an edit can't be made there (two operands spelled alike, say).
#define BUSTER_CHECK_MUTATION_ATTEMPTS 16
// The most tokens an edit of a declaration's specifiers rewrites one by one.
#define BUSTER_CHECK_MUTATION_RUN 64
// The tag a struct replacement names when the unit has no struct before the site, or by chance: incomplete everywhere.
#define BUSTER_CHECK_MUTATION_TAG "buster_mutation"

// A place to mutate, in parser tokens:
//   - a type change: the declaration's specifiers are [first, second), and its declarator starts at second;
//   - a removal: [first, second] are blanked (a parameter's with one of its commas), or replaced by `void` if third;
//   - an operand swap: the operands [first, second] and [third, fourth];
//   - an argument swap: the call's parentheses, first and second.
typedef struct CheckMutationSite CheckMutationSite;
struct CheckMutationSite
{
    uint8_t kind;
    // In the unit's own file, rather than one it includes.
    bool main;
    uint32_t first;
    uint32_t second;
    uint32_t third;
    uint32_t fourth;
};

// One unit's base and what its scalar parse says about it. The parser and the preprocessor are the campaign's own
// (check_mutation_campaign), alive while the unit's mutants are made.
typedef struct CheckMutationUnit CheckMutationUnit;
struct CheckMutationUnit
{
    const char* file;
    char* text;
    size_t length;
    Preprocessor* pp;
    Parser* parser;
    uint32_t count;
    // Each parser token's bytes in the text, and its subtree's first and last tokens.
    uint32_t* starts;
    uint32_t* ends;
    uint32_t* firsts;
    uint32_t* lasts;
    // Nonzero for a token inside an attribute or an asm statement's operands, which no swap touches.
    uint8_t* grouped;
    // Each struct or union definition's tag, in order.
    uint32_t* tags;
    uint32_t tag_count;
    CheckMutationSite* sites;
    uint32_t site_count;
    uint32_t site_capacity;
    // The sites by kind and file (main last): site_order's run [site_offsets[k * 2 + main], site_offsets[k * 2 + main + 1]).
    uint32_t* site_order;
    uint32_t site_offsets[CHECK_MUTATION_KIND_COUNT * 2 + 1];
};

// A mutation as an edit of the base text: bytes [begin, end) become `replacement`.
typedef struct CheckMutationEdit CheckMutationEdit;
struct CheckMutationEdit
{
    uint32_t begin;
    uint32_t end;
    char* replacement;
    size_t replacement_length;
    size_t replacement_capacity;
    bool valid;
    char detail[160];
};

// What clang and the checker gave on one text, for the minimizer's comparisons.
typedef struct CheckMutationOutcome CheckMutationOutcome;
struct CheckMutationOutcome
{
    bool valid;
    // The checker's scalar parser found no syntax error: the minimizer keeps a mutant that parses parseable.
    bool parsed;
    bool clang_rejected;
    bool accepted;
    bool equal;
    bool resource;
    uint32_t clang_line;
    uint32_t line;
    const char* phase;
    char clang_file[512];
    char clang_message[256];
    char file[512];
    char message[256];
};

typedef struct CheckMutationContext CheckMutationContext;
struct CheckMutationContext
{
    // The options the bases are read back with, and clang's arguments (the configuration's, then -x cpp-output).
    PPOptions plain;
    const char* clang;
    const char** arguments;
    uint32_t argument_count;
    const char* directory;
    // The minimizer's candidate, rewritten for every probe.
    char probe[4200];
    uint64_t probes;
};

typedef struct CheckMutationTally CheckMutationTally;
struct CheckMutationTally
{
    // Units whose bases were compared, and how many of those differed.
    uint64_t units;
    uint64_t base_differences;
    // Units passed over: no base could be made, or no site found.
    uint64_t unusable;
    uint64_t mutants;
    uint64_t differences;
    uint64_t intended;
    uint64_t minimized;
    uint64_t saved;
    uint64_t sites[CHECK_MUTATION_KIND_COUNT];
    uint64_t kinds[CHECK_MUTATION_KIND_COUNT];
    uint64_t kind_rejected[CHECK_MUTATION_KIND_COUNT];
    uint64_t kind_differences[CHECK_MUTATION_KIND_COUNT];
};

// Differences that are intended: the checker's result stands, for the reason given. A difference matches an entry when
// clang's and the checker's verdicts are the entry's and both messages contain the entry's (an empty one matches any).
typedef struct CheckMutationIntended CheckMutationIntended;
struct CheckMutationIntended
{
    bool clang_rejected;
    bool checker_accepted;
    const char* clang_message;
    const char* checker_message;
    const char* reason;
};

static const CheckMutationIntended check_mutation_intended[] =
{
    {
        .clang_rejected = false,
        .checker_accepted = false,
        .clang_message = "\x01",
        .checker_message = "\x01",
        .reason = "a placeholder no message contains, so that the table always has an entry",
    },
};

// ---------------------------------------------------------------------------------------------------------------------
// Sites

static bool check_mutation_spelled(const CheckMutationUnit* unit, uint32_t token, const char* word)
{
    const size_t length = strlen(word);
    return token < unit->count && unit->ends[token] - unit->starts[token] == length && memcmp(unit->text + unit->starts[token], word, length) == 0;
}

static uint8_t check_mutation_role(const CheckMutationUnit* unit, uint32_t token)
{
    return token < unit->count ? unit->parser->tree.roles[token] : PARSE_ROLE_NONE;
}

static uint8_t check_mutation_kind(const CheckMutationUnit* unit, uint32_t token)
{
    return token < unit->count ? unit->parser->tokens.kinds[token] : PARSE_KIND_END;
}

static uint32_t check_mutation_parent(const CheckMutationUnit* unit, uint32_t token)
{
    return token < unit->count ? unit->parser->tree.parents[token] : BUSTER_PARSE_ROOT;
}

// The roles that make up a declaration's specifiers in the tree: each specifier's parent is the one before it, and the
// first one's is the declaration's node (c_parser.c: "specifiers as right-associative prefix operators").
static bool check_mutation_specifier_role(uint8_t role)
{
    return role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || role == PARSE_ROLE_CLOSE_STRUCT || role == PARSE_ROLE_CLOSE_ENUM ||
           role == PARSE_ROLE_CLOSE_SPECIFIER || role == PARSE_ROLE_CLOSE_ATTRIBUTE_SPECIFIER;
}

static bool check_mutation_binary_role(uint8_t role)
{
    return role == PARSE_ROLE_MULTIPLICATIVE || role == PARSE_ROLE_ADDITIVE || role == PARSE_ROLE_SHIFT || role == PARSE_ROLE_RELATIONAL ||
           role == PARSE_ROLE_EQUALITY || role == PARSE_ROLE_BITWISE_AND || role == PARSE_ROLE_BITWISE_XOR || role == PARSE_ROLE_BITWISE_OR ||
           role == PARSE_ROLE_LOGICAL_AND || role == PARSE_ROLE_LOGICAL_OR || role == PARSE_ROLE_ASSIGN;
}

// The groups whose contents aren't C expressions to swap in: attributes and asm statements' operands.
static bool check_mutation_grouping_role(uint8_t role)
{
    return role == PARSE_ROLE_OPEN_ATTRIBUTE || role == PARSE_ROLE_OPEN_ATTRIBUTE_SPECIFIER || role == PARSE_ROLE_OPEN_ATTRIBUTE_LEFT ||
           role == PARSE_ROLE_OPEN_ATTRIBUTE_RIGHT || role == PARSE_ROLE_OPEN_ATTRIBUTE_LIST || role == PARSE_ROLE_OPEN_ASM;
}

static bool check_mutation_opener_role(uint8_t role)
{
    return role >= PARSE_ROLE_OPEN_GROUP && role <= PARSE_ROLE_OPEN_CASE;
}

static bool check_mutation_add_site(CheckMutationUnit* unit, uint8_t kind, uint32_t first, uint32_t second, uint32_t third, uint32_t fourth)
{
    bool valid = true;
    if (unit->site_count == unit->site_capacity)
    {
        const uint32_t capacity = unit->site_capacity != 0 ? unit->site_capacity * 2 : 1024;
        CheckMutationSite* sites = capacity > unit->site_capacity ? realloc(unit->sites, (size_t)capacity * sizeof(CheckMutationSite)) : NULL;
        valid = sites != NULL;
        if (valid)
        {
            unit->sites = sites;
            unit->site_capacity = capacity;
        }
    }
    if (valid)
    {
        // The site's file is its first token's presumed one: the unit's own if line markers give it the unit's name.
        const uint32_t location = unit->pp->output.locations[unit->parser->tokens.sources[first]];
        const char* name = NULL;
        uint32_t length = 0;
        pp_presumed(unit->pp, location, &name, &length);
        const bool main = name != NULL && length == strlen(unit->file) && memcmp(name, unit->file, length) == 0;
        unit->sites[unit->site_count++] = (CheckMutationSite)
        {
            .kind = kind,
            .main = main,
            .first = first,
            .second = second,
            .third = third,
            .fourth = fourth,
        };
    }
    return valid;
}

// The first token after a declaration's specifiers, from its first specifier: specifier tokens, struct and enum keywords
// and their tags and bodies, attributes, and the groups of `_Atomic(...)` and `typeof(...)`.
static uint32_t check_mutation_declarator(const CheckMutationUnit* unit, uint32_t first, uint32_t last)
{
    uint32_t token = first;
    bool more = true;
    while (more && token <= last)
    {
        const uint8_t role = check_mutation_role(unit, token);
        const uint8_t kind = check_mutation_kind(unit, token);
        const bool keyword = role == PARSE_ROLE_LEAF_RIGHT && (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_ATOMIC ||
                                                               kind == PARSE_KIND_TYPEOF || kind == PARSE_KIND_ATTRIBUTE || kind == PARSE_KIND_ALIGNAS);
        const bool tag = role == PARSE_ROLE_LEAF_RIGHT && kind == PARSE_KIND_IDENTIFIER && token > first &&
                         (check_mutation_kind(unit, token - 1) == PARSE_KIND_STRUCT || check_mutation_kind(unit, token - 1) == PARSE_KIND_ENUM);
        if (check_mutation_opener_role(role))
        {
            // An opener's parent is its closer.
            const uint32_t closer = check_mutation_parent(unit, token);
            more = closer != BUSTER_PARSE_ROOT && closer > token && closer <= last;
            token = more ? closer + 1 : token;
        }
        else if (role == PARSE_ROLE_SPECIFIER || role == PARSE_ROLE_TAG || keyword || tag)
        {
            token += 1;
        }
        else
        {
            more = false;
        }
    }
    return token;
}

// The declarations' sites: each declaration's first specifier is a specifier whose parent isn't one, and that parent
// says where the declaration is.
static bool check_mutation_declarations(CheckMutationUnit* unit)
{
    bool valid = true;
    for (uint32_t token = 0; valid && token < unit->count; token += 1)
    {
        const uint32_t parent = check_mutation_parent(unit, token);
        const bool top = check_mutation_specifier_role(check_mutation_role(unit, token)) &&
                         (parent == BUSTER_PARSE_ROOT || !check_mutation_specifier_role(check_mutation_role(unit, parent)));
        const uint8_t parent_role = check_mutation_role(unit, parent);
        const uint32_t above = check_mutation_parent(unit, parent);
        const uint8_t above_role = check_mutation_role(unit, above);
        const bool terminated = top && parent_role == PARSE_ROLE_TERMINATOR;
        const bool file_scope = (terminated && above == BUSTER_PARSE_ROOT) || (top && parent_role == PARSE_ROLE_CLOSE_BODY && above == BUSTER_PARSE_ROOT);
        const bool local = terminated && (above_role == PARSE_ROLE_CLOSE_BODY || above_role == PARSE_ROLE_CLOSE_BLOCK);
        const bool for_init = terminated && above_role == PARSE_ROLE_CLOSE_FOR;
        const bool member = terminated && above_role == PARSE_ROLE_CLOSE_STRUCT;
        // A parameter's first specifier hangs from the commas between the parameters, or from the declarator's `)`.
        uint32_t group = parent;
        while (top && check_mutation_role(unit, group) == PARSE_ROLE_SEPARATOR)
        {
            group = check_mutation_parent(unit, group);
        }
        const bool parameter = top && check_mutation_role(unit, group) == PARSE_ROLE_CLOSE_FUNCTION;
        if (file_scope || local || for_init || member || parameter)
        {
            const uint32_t first = unit->firsts[token];
            const uint32_t last = unit->lasts[token];
            const uint32_t declarator = check_mutation_declarator(unit, first, last);
            const uint8_t next = check_mutation_kind(unit, declarator);
            const bool declares = declarator <= last + 1 && declarator < unit->count && next != PARSE_KIND_SEMI && next != PARSE_KIND_COLON;
            for (uint8_t kind = CHECK_MUTATION_POINTER_ADD; valid && declares && kind <= CHECK_MUTATION_STRUCT; kind += 1)
            {
                const bool star = next == PARSE_KIND_STAR && check_mutation_role(unit, declarator) == PARSE_ROLE_PREFIX;
                // Whether the edit applies is the edit's to say (check_mutation_type); a site per kind keeps the kinds
                // equally likely.
                if (kind != CHECK_MUTATION_POINTER_REMOVE || star)
                {
                    valid = check_mutation_add_site(unit, kind, first, declarator, 0, 0);
                }
            }
            if (valid && (file_scope || local || for_init || member))
            {
                // A declaration in a for loop's first clause keeps its `;`.
                const uint8_t kind = file_scope ? CHECK_MUTATION_REMOVE_FILE_SCOPE : member ? CHECK_MUTATION_REMOVE_MEMBER : CHECK_MUTATION_REMOVE_LOCAL;
                valid = check_mutation_add_site(unit, kind, first, for_init ? parent - 1 : parent, 0, 0);
            }
        }
    }
    return valid;
}

// The parameters' sites, from each function declarator's parentheses: its parameters are the runs between the commas
// one level inside them. A parameter goes with the comma after it, or the one before it if it is the last; a lone
// parameter becomes `void`. `...` and `void` themselves stay.
static bool check_mutation_parameters(CheckMutationUnit* unit)
{
    bool valid = true;
    for (uint32_t opener = 0; valid && opener < unit->count; opener += 1)
    {
        const uint32_t closer = check_mutation_parent(unit, opener);
        if (check_mutation_role(unit, opener) == PARSE_ROLE_OPEN_FUNCTION && closer != BUSTER_PARSE_ROOT && closer > opener + 1)
        {
            const uint32_t depth = unit->parser->tree.depths[opener] + 1;
            uint32_t begin = opener + 1;
            uint32_t previous = BUSTER_PARSE_NONE;
            for (uint32_t token = opener + 1; valid && token <= closer; token += 1)
            {
                const bool separator = token < closer && check_mutation_role(unit, token) == PARSE_ROLE_SEPARATOR && unit->parser->tree.depths[token] == depth;
                if (separator || token == closer)
                {
                    const uint32_t end = token - 1;
                    const bool kept = begin == end && (check_mutation_kind(unit, begin) == PARSE_KIND_ELLIPSIS || check_mutation_spelled(unit, begin, "void"));
                    const bool lone = previous == BUSTER_PARSE_NONE && token == closer;
                    if (begin <= end && !kept)
                    {
                        const uint32_t first = separator || lone ? begin : previous;
                        const uint32_t last = separator ? token : end;
                        valid = check_mutation_add_site(unit, CHECK_MUTATION_REMOVE_PARAMETER, first, last, lone, 0);
                    }
                    previous = token;
                    begin = token + 1;
                }
            }
        }
    }
    return valid;
}

// The swaps' sites: binary operators whose operands are expressions (not a declarator and its initializer, a designator
// or an enumerator), and calls of two arguments or more, outside attributes and asm operands.
static bool check_mutation_swaps(CheckMutationUnit* unit)
{
    bool valid = true;
    for (uint32_t token = 0; valid && token < unit->count; token += 1)
    {
        const uint8_t role = check_mutation_role(unit, token);
        if (check_mutation_binary_role(role) && unit->grouped[token] == 0 && unit->firsts[token] < token && unit->lasts[token] > token)
        {
            uint32_t above = check_mutation_parent(unit, token);
            while (above != BUSTER_PARSE_ROOT && (check_mutation_role(unit, above) == PARSE_ROLE_COMMA || check_mutation_role(unit, above) == PARSE_ROLE_SEPARATOR))
            {
                above = check_mutation_parent(unit, above);
            }
            const uint8_t above_role = check_mutation_role(unit, above);
            const uint8_t start = check_mutation_role(unit, unit->firsts[token]);
            const bool initializer = role == PARSE_ROLE_ASSIGN && (check_mutation_specifier_role(above_role) || above_role == PARSE_ROLE_CLOSE_ENUM ||
                                                                   above_role == PARSE_ROLE_CLOSE_INITIALIZER || above_role == PARSE_ROLE_CLOSE_COMPOUND_BODY ||
                                                                   start == PARSE_ROLE_MEMBER_OPERATOR || start == PARSE_ROLE_OPEN_SUBSCRIPT);
            // Both operands must be the operator's own subtrees, the runs on either side of it.
            uint32_t left = token - 1;
            while (check_mutation_parent(unit, left) != token && check_mutation_parent(unit, left) != BUSTER_PARSE_ROOT && check_mutation_parent(unit, left) < token)
            {
                left = check_mutation_parent(unit, left);
            }
            uint32_t right = token + 1;
            while (check_mutation_parent(unit, right) != token && check_mutation_parent(unit, right) != BUSTER_PARSE_ROOT && check_mutation_parent(unit, right) > token &&
                   check_mutation_parent(unit, right) <= unit->lasts[token])
            {
                right = check_mutation_parent(unit, right);
            }
            const bool operands = check_mutation_parent(unit, left) == token && unit->firsts[left] == unit->firsts[token] && unit->lasts[left] == token - 1 &&
                                  check_mutation_parent(unit, right) == token && unit->firsts[right] == token + 1 && unit->lasts[right] == unit->lasts[token];
            if (!initializer && operands)
            {
                valid = check_mutation_add_site(unit, CHECK_MUTATION_SWAP_OPERANDS, unit->firsts[token], token - 1, token + 1, unit->lasts[token]);
            }
        }
        else if (role == PARSE_ROLE_OPEN_CALL && unit->grouped[token] == 0)
        {
            const uint32_t closer = check_mutation_parent(unit, token);
            const uint32_t depth = unit->parser->tree.depths[token] + 1;
            uint32_t separators = 0;
            for (uint32_t at = token + 1; closer != BUSTER_PARSE_ROOT && at < closer; at += 1)
            {
                separators += check_mutation_role(unit, at) == PARSE_ROLE_SEPARATOR && unit->parser->tree.depths[at] == depth;
            }
            if (separators != 0 && check_mutation_role(unit, closer) == PARSE_ROLE_CLOSE_CALL)
            {
                valid = check_mutation_add_site(unit, CHECK_MUTATION_SWAP_ARGUMENTS, token, closer, 0, 0);
            }
        }
    }
    return valid;
}

// Every site of the unit, grouped by kind and file, and the struct and union tags its definitions give.
static bool check_mutation_discover(CheckMutationUnit* unit)
{
    const uint32_t count = unit->count;
    unit->grouped = calloc((size_t)count + 1, 1);
    unit->tags = malloc(((size_t)count + 1) * sizeof(uint32_t));
    // The groups no swap enters, with a stack of their closers.
    uint32_t* closers = malloc(((size_t)count + 1) * sizeof(uint32_t));
    bool valid = unit->grouped != NULL && unit->tags != NULL && closers != NULL;
    uint32_t depth = 0;
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        while (depth != 0 && closers[depth - 1] < token)
        {
            depth -= 1;
        }
        unit->grouped[token] = depth != 0;
        const uint32_t closer = check_mutation_parent(unit, token);
        if (check_mutation_grouping_role(check_mutation_role(unit, token)) && closer != BUSTER_PARSE_ROOT && closer > token)
        {
            closers[depth++] = closer;
        }
    }
    free(closers);
    for (uint32_t token = 0; valid && token < count; token += 1)
    {
        const bool body = check_mutation_role(unit, token) == PARSE_ROLE_OPEN_STRUCT && token >= 2;
        if (body && check_mutation_kind(unit, token - 1) == PARSE_KIND_IDENTIFIER && check_mutation_spelled(unit, token - 2, "struct"))
        {
            unit->tags[unit->tag_count++] = token - 1;
        }
    }
    valid = valid && check_mutation_declarations(unit) && check_mutation_parameters(unit) && check_mutation_swaps(unit);
    // A counting sort of the sites by kind and file.
    unit->site_order = valid ? malloc(((size_t)unit->site_count + 1) * sizeof(uint32_t)) : NULL;
    valid = valid && unit->site_order != NULL;
    uint32_t next[CHECK_MUTATION_KIND_COUNT * 2] = { 0 };
    for (uint32_t site = 0; valid && site < unit->site_count; site += 1)
    {
        next[unit->sites[site].kind * 2 + unit->sites[site].main] += 1;
    }
    unit->site_offsets[0] = 0;
    for (uint32_t group = 0; group < CHECK_MUTATION_KIND_COUNT * 2; group += 1)
    {
        unit->site_offsets[group + 1] = unit->site_offsets[group] + next[group];
        next[group] = unit->site_offsets[group];
    }
    for (uint32_t site = 0; valid && site < unit->site_count; site += 1)
    {
        unit->site_order[next[unit->sites[site].kind * 2 + unit->sites[site].main]++] = site;
    }
    return valid;
}

static void check_mutation_release(CheckMutationUnit* unit)
{
    free(unit->text);
    free(unit->starts);
    free(unit->ends);
    free(unit->firsts);
    free(unit->lasts);
    free(unit->grouped);
    free(unit->tags);
    free(unit->sites);
    free(unit->site_order);
    *unit = (CheckMutationUnit){ 0 };
}

// Reads the unit's text, as `name`, with `plain`, and parses it with the scalar parser: each parser token's bytes in the
// text, and its subtree's first and last tokens. False if the text doesn't parse. `pp` and `parser` are the caller's,
// which releases them before the text.
static bool check_mutation_read(CheckMutationUnit* unit, const char* name, const PPOptions* plain, InternTable* identifiers, Preprocessor* pp,
                                Parser* parser)
{
    unit->pp = pp;
    unit->parser = parser;
    bool valid = unit->length < UINT32_MAX / 2 && intern_initialize(identifiers) && pp_initialize(pp, plain, identifiers) &&
                 pp_run_source(pp, name, (const uint8_t*)unit->text, (uint32_t)unit->length) && pp->errors == 0;
    const ParseOptions scalar =
    {
        .scalar = true,
    };
    valid = valid && parse_initialize(parser, &scalar, plain->language) && parse_translation_unit(parser, pp) && parser->errors == 0 &&
            parser->fatal == PARSE_MESSAGE_NONE && parser->postorder != NULL;
    unit->count = valid ? parser->tokens.count : 0;
    // The text's own tokens, whose starts the parser tokens' locations are.
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(unit->length);
    uint8_t* kinds = valid ? malloc(capacity) : NULL;
    uint32_t* starts = valid ? malloc(capacity * sizeof(uint32_t)) : NULL;
    uint32_t* ends = valid ? malloc(capacity * sizeof(uint32_t)) : NULL;
    unit->starts = valid ? malloc(((size_t)unit->count + 1) * sizeof(uint32_t)) : NULL;
    unit->ends = valid ? malloc(((size_t)unit->count + 1) * sizeof(uint32_t)) : NULL;
    unit->firsts = valid ? malloc(((size_t)unit->count + 1) * sizeof(uint32_t)) : NULL;
    unit->lasts = valid ? malloc(((size_t)unit->count + 1) * sizeof(uint32_t)) : NULL;
    valid = valid && kinds != NULL && starts != NULL && ends != NULL && unit->starts != NULL && unit->ends != NULL && unit->firsts != NULL &&
            unit->lasts != NULL;
    const int64_t lexed = valid ? c_tokenize((const uint8_t*)unit->text, unit->length, kinds, starts, ends, 0) : -1;
    valid = valid && lexed >= 0;
    uint32_t at = 0;
    for (uint32_t token = 0; valid && token < unit->count; token += 1)
    {
        const uint32_t source = parser->tokens.sources[token];
        const uint32_t location = pp->output.locations[source];
        const uint32_t range = pp_find_range(pp, location);
        valid = range != BUSTER_PP_NONE && pp->ranges[range].file != BUSTER_PP_NONE;
        const uint32_t offset = valid ? location - pp->ranges[range].base : 0;
        while (valid && at < (uint64_t)lexed && starts[at] < offset)
        {
            at += 1;
        }
        // The two halves of a split `::` share their source.
        const bool split = token != 0 && parser->tokens.sources[token - 1] == source;
        valid = valid && (split || (at < (uint64_t)lexed && starts[at] == offset));
        if (valid && split)
        {
            unit->ends[token - 1] = unit->starts[token - 1] + 1;
        }
        unit->starts[token] = split ? offset + 1 : offset;
        unit->ends[token] = valid ? ends[at] : offset;
        unit->firsts[token] = token;
        unit->lasts[token] = token;
    }
    free(kinds);
    free(starts);
    free(ends);
    // Children come before their parents in the postorder, so each subtree's run is whole when its node is reached.
    for (uint32_t position = 0; valid && position < unit->count; position += 1)
    {
        const uint32_t token = parser->postorder[position];
        const uint32_t parent = token < unit->count ? check_mutation_parent(unit, token) : BUSTER_PARSE_ROOT;
        valid = token < unit->count && (parent == BUSTER_PARSE_ROOT || parent < unit->count);
        if (valid && parent != BUSTER_PARSE_ROOT)
        {
            unit->firsts[parent] = unit->firsts[token] < unit->firsts[parent] ? unit->firsts[token] : unit->firsts[parent];
            unit->lasts[parent] = unit->lasts[token] > unit->lasts[parent] ? unit->lasts[token] : unit->lasts[parent];
        }
    }
    return valid;
}

// The unit's base: `file` preprocessed with the corpus's options and printed with line markers into `base`, then read
// back (check_mutation_read) and its sites found.
static bool check_mutation_prepare(CheckMutationUnit* unit, const char* file, const PPOptions* options, const PPOptions* plain, const char* base,
                                   InternTable* identifiers, Preprocessor* pp, Parser* parser)
{
    *unit = (CheckMutationUnit)
    {
        .file = file,
    };
    static InternTable printing_identifiers;
    static Preprocessor printing;
    // The printer places a macro expansion's tokens at its start only from the starts the run kept, as -E keeps them.
    PPOptions printing_options = *options;
    printing_options.expansion_starts = true;
    bool valid = intern_initialize(&printing_identifiers) && pp_initialize(&printing, &printing_options, &printing_identifiers) &&
                 pp_run(&printing, file);
    FILE* stream = valid ? open_memstream(&unit->text, &unit->length) : NULL;
    valid = stream != NULL && pp_print(&printing, stream, true);
    if (stream != NULL)
    {
        valid = fclose(stream) == 0 && valid;
    }
    pp_release(&printing);
    intern_release(&printing_identifiers);
    valid = valid && check_corpus_write(base, unit->text, unit->length) && check_mutation_read(unit, base, plain, identifiers, pp, parser) &&
            check_mutation_discover(unit);
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Edits

static bool check_mutation_append(CheckMutationEdit* edit, const char* bytes, size_t length)
{
    if (edit->valid && edit->replacement_length + length + 1 > edit->replacement_capacity)
    {
        const size_t capacity = (edit->replacement_length + length + 1) * 2 + 64;
        char* replacement = realloc(edit->replacement, capacity);
        edit->valid = replacement != NULL;
        edit->replacement = edit->valid ? replacement : edit->replacement;
        edit->replacement_capacity = edit->valid ? capacity : edit->replacement_capacity;
    }
    if (edit->valid && length != 0)
    {
        memcpy(edit->replacement + edit->replacement_length, bytes, length);
        edit->replacement_length += length;
    }
    return edit->valid;
}

static bool check_mutation_append_text(CheckMutationEdit* edit, const char* text)
{
    return check_mutation_append(edit, text, strlen(text));
}

// The newlines of the base's bytes [begin, end), so that the lines after an edit keep their numbers.
static void check_mutation_append_lines(CheckMutationEdit* edit, const CheckMutationUnit* unit, uint32_t begin, uint32_t end)
{
    for (uint32_t at = begin; at < end; at += 1)
    {
        if (unit->text[at] == '\n')
        {
            check_mutation_append(edit, "\n", 1);
        }
    }
}

// Tokens [first, last] rewritten, each by its entry of `substitutions` (NULL keeps it), with the spaces between them
// kept: the edit of a declaration's specifiers.
static void check_mutation_substitute(CheckMutationEdit* edit, const CheckMutationUnit* unit, uint32_t first, uint32_t last,
                                      const char* const* substitutions)
{
    edit->begin = unit->starts[first];
    edit->end = unit->ends[last];
    for (uint32_t token = first; token <= last; token += 1)
    {
        const char* substitution = substitutions[token - first];
        check_mutation_append(edit, " ", 1);
        if (substitution != NULL)
        {
            check_mutation_append_text(edit, substitution);
        }
        else
        {
            check_mutation_append(edit, unit->text + unit->starts[token], unit->ends[token] - unit->starts[token]);
        }
        if (token < last)
        {
            check_mutation_append(edit, unit->text + unit->ends[token], unit->starts[token + 1] - unit->ends[token]);
        }
    }
}

// What a declaration's specifiers [first, declarator) hold, for the type edits.
typedef struct CheckMutationRun CheckMutationRun;
struct CheckMutationRun
{
    // The type specifiers (keywords, a typedef name, a struct or enum keyword and its tag), and of those the base
    // keywords (char, short, int, long, __int128, float, double).
    uint32_t types[BUSTER_CHECK_MUTATION_RUN];
    uint32_t type_count;
    uint32_t bases[BUSTER_CHECK_MUTATION_RUN];
    uint32_t base_count;
    uint32_t sign;
    uint32_t constant;
    bool floating;
    bool named;
    bool tagged;
    // A body, a group (typeof, _Atomic(...), _BitInt(...)) or a keyword the edits don't rewrite (void, _Bool, _Float16...).
    bool other;
};

static void check_mutation_run(const CheckMutationUnit* unit, uint32_t first, uint32_t declarator, CheckMutationRun* run)
{
    *run = (CheckMutationRun)
    {
        .sign = BUSTER_PARSE_NONE,
        .constant = BUSTER_PARSE_NONE,
    };
    uint32_t token = first;
    while (token < declarator)
    {
        const uint8_t role = check_mutation_role(unit, token);
        const uint8_t kind = check_mutation_kind(unit, token);
        uint32_t next = token + 1;
        if (check_mutation_opener_role(role))
        {
            const uint32_t closer = check_mutation_parent(unit, token);
            run->other |= !check_mutation_grouping_role(role);
            next = closer != BUSTER_PARSE_ROOT && closer > token ? closer + 1 : declarator;
        }
        else if (kind == PARSE_KIND_TYPE)
        {
            const bool sign = check_mutation_spelled(unit, token, "unsigned") || check_mutation_spelled(unit, token, "signed") ||
                              check_mutation_spelled(unit, token, "__signed") || check_mutation_spelled(unit, token, "__signed__");
            const bool integer = check_mutation_spelled(unit, token, "char") || check_mutation_spelled(unit, token, "short") ||
                                 check_mutation_spelled(unit, token, "int") || check_mutation_spelled(unit, token, "long") ||
                                 check_mutation_spelled(unit, token, "__int128");
            const bool floating = check_mutation_spelled(unit, token, "float") || check_mutation_spelled(unit, token, "double");
            const bool complex = check_mutation_spelled(unit, token, "_Complex") || check_mutation_spelled(unit, token, "__complex") ||
                                 check_mutation_spelled(unit, token, "__complex__");
            run->sign = sign && run->sign == BUSTER_PARSE_NONE ? token : run->sign;
            run->other |= !sign && !integer && !floating && !complex;
            run->floating |= floating;
            if ((integer || floating) && run->base_count < BUSTER_CHECK_MUTATION_RUN)
            {
                run->bases[run->base_count++] = token;
            }
            if (run->type_count < BUSTER_CHECK_MUTATION_RUN)
            {
                run->types[run->type_count++] = token;
            }
        }
        else if (kind == PARSE_KIND_QUALIFIER)
        {
            const bool constant = check_mutation_spelled(unit, token, "const") || check_mutation_spelled(unit, token, "__const") ||
                                  check_mutation_spelled(unit, token, "__const__");
            run->constant = constant && run->constant == BUSTER_PARSE_NONE ? token : run->constant;
        }
        else if (kind == PARSE_KIND_IDENTIFIER && role == PARSE_ROLE_SPECIFIER)
        {
            run->named = true;
            if (run->type_count < BUSTER_CHECK_MUTATION_RUN)
            {
                run->types[run->type_count++] = token;
            }
        }
        else if ((kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM) && check_mutation_role(unit, token + 1) == PARSE_ROLE_TAG && token + 1 < declarator)
        {
            run->tagged = true;
            if (run->type_count + 1 < BUSTER_CHECK_MUTATION_RUN)
            {
                run->types[run->type_count++] = token;
                run->types[run->type_count++] = token + 1;
            }
            next = token + 2;
        }
        else if (kind == PARSE_KIND_STRUCT || kind == PARSE_KIND_ENUM || kind == PARSE_KIND_ATOMIC || kind == PARSE_KIND_TYPEOF)
        {
            run->other = true;
        }
        token = next;
    }
}

static uint32_t check_mutation_choose(uint64_t* state, uint32_t count)
{
    return count != 0 ? (uint32_t)(parse_test_random(state) % count) : 0;
}

// A type edit at `site`, or an invalid edit where its kind doesn't apply to the declaration.
static void check_mutation_type(CheckMutationEdit* edit, const CheckMutationUnit* unit, const CheckMutationSite* site, uint64_t* state)
{
    CheckMutationRun run;
    check_mutation_run(unit, site->first, site->second, &run);
    const bool basic = run.type_count != 0 && !run.named && !run.tagged && !run.other;
    const bool integer = basic && !run.floating && (run.base_count != 0 || run.sign != BUSTER_PARSE_NONE);
    const bool floating = basic && run.floating && run.sign == BUSTER_PARSE_NONE;
    // The specifiers the edit rewrites, one by one: from the first of them to the last.
    const char* substitutions[BUSTER_CHECK_MUTATION_RUN] = { 0 };
    const uint32_t first_type = run.type_count != 0 ? run.types[0] : site->first;
    const uint32_t last_type = run.type_count != 0 ? run.types[run.type_count - 1] : site->first;
    const bool short_run = last_type - first_type < BUSTER_CHECK_MUTATION_RUN;
    if (site->kind == CHECK_MUTATION_POINTER_ADD)
    {
        edit->begin = unit->starts[site->second];
        edit->end = edit->begin;
        check_mutation_append_text(edit, " * ");
        snprintf(edit->detail, sizeof(edit->detail), "added a pointer");
    }
    else if (site->kind == CHECK_MUTATION_POINTER_REMOVE)
    {
        edit->begin = unit->starts[site->second];
        edit->end = unit->ends[site->second];
        check_mutation_append_text(edit, " ");
        snprintf(edit->detail, sizeof(edit->detail), "removed a pointer");
    }
    else if (site->kind == CHECK_MUTATION_SIGNEDNESS && integer && short_run)
    {
        if (run.sign != BUSTER_PARSE_NONE)
        {
            const char* replacement = check_mutation_spelled(unit, run.sign, "unsigned") ? "signed" : "unsigned";
            substitutions[run.sign - first_type] = replacement;
            check_mutation_substitute(edit, unit, first_type, last_type, substitutions);
            snprintf(edit->detail, sizeof(edit->detail), "made it %s", replacement);
        }
        else
        {
            edit->begin = unit->starts[run.bases[0]];
            edit->end = edit->begin;
            check_mutation_append_text(edit, " unsigned ");
            snprintf(edit->detail, sizeof(edit->detail), "made it unsigned");
        }
    }
    else if (site->kind == CHECK_MUTATION_WIDTH && (integer || floating) && short_run)
    {
        static const char* const integers[] =
        {
            "char",
            "short",
            "int",
            "long",
            "long long",
        };
        static const char* const floats[] =
        {
            "float",
            "double",
            "long double",
        };
        const char* const* choices = floating ? floats : integers;
        const uint32_t choice_count = floating ? BUSTER_ARRAY_LENGTH(floats) : BUSTER_ARRAY_LENGTH(integers);
        // The current base spelled as the choices are, to choose another one.
        char current[64] = { 0 };
        for (uint32_t i = 0; i < run.base_count; i += 1)
        {
            const size_t used = strlen(current);
            snprintf(current + used, sizeof(current) - used, "%s%.*s", i != 0 ? " " : "", (int)(unit->ends[run.bases[i]] - unit->starts[run.bases[i]]),
                     unit->text + unit->starts[run.bases[i]]);
        }
        uint32_t choice = check_mutation_choose(state, choice_count);
        choice = strcmp(choices[choice], current) == 0 ? (choice + 1) % choice_count : choice;
        if (run.base_count != 0)
        {
            substitutions[run.bases[0] - first_type] = choices[choice];
            for (uint32_t i = 1; i < run.base_count; i += 1)
            {
                substitutions[run.bases[i] - first_type] = "";
            }
            check_mutation_substitute(edit, unit, first_type, last_type, substitutions);
        }
        else
        {
            edit->begin = unit->ends[run.sign];
            edit->end = edit->begin;
            check_mutation_append_text(edit, " ");
            check_mutation_append_text(edit, choices[choice]);
            check_mutation_append_text(edit, " ");
        }
        snprintf(edit->detail, sizeof(edit->detail), "made '%s' '%s'", current[0] != 0 ? current : "int", choices[choice]);
    }
    else if (site->kind == CHECK_MUTATION_CONST)
    {
        if (run.constant != BUSTER_PARSE_NONE)
        {
            edit->begin = unit->starts[run.constant];
            edit->end = unit->ends[run.constant];
            check_mutation_append_text(edit, " ");
            snprintf(edit->detail, sizeof(edit->detail), "removed const");
        }
        else
        {
            edit->begin = unit->starts[first_type];
            edit->end = edit->begin;
            check_mutation_append_text(edit, " const ");
            snprintf(edit->detail, sizeof(edit->detail), "added const");
        }
    }
    else if (site->kind == CHECK_MUTATION_STRUCT && (basic || run.named || run.tagged) && !run.other && short_run)
    {
        // A struct defined before the declaration, or the incomplete one; a struct reference becomes `int` as often.
        char replacement[160];
        uint32_t tags = 0;
        while (tags < unit->tag_count && unit->tags[tags] < site->first)
        {
            tags += 1;
        }
        const uint32_t choice = check_mutation_choose(state, tags + 1);
        const bool integer_type = run.tagged && parse_test_random(state) % 2 == 0;
        if (integer_type)
        {
            snprintf(replacement, sizeof(replacement), "int");
        }
        else if (choice < tags)
        {
            const uint32_t tag = unit->tags[choice];
            snprintf(replacement, sizeof(replacement), "struct %.*s", (int)(unit->ends[tag] - unit->starts[tag]), unit->text + unit->starts[tag]);
        }
        else
        {
            snprintf(replacement, sizeof(replacement), "struct %s", BUSTER_CHECK_MUTATION_TAG);
        }
        substitutions[0] = replacement;
        for (uint32_t i = 1; i < run.type_count; i += 1)
        {
            substitutions[run.types[i] - first_type] = "";
        }
        check_mutation_substitute(edit, unit, first_type, last_type, substitutions);
        snprintf(edit->detail, sizeof(edit->detail), "made the type '%s'", replacement);
    }
    else
    {
        edit->valid = false;
    }
}

// The arguments of the call whose parentheses are `opener` and `closer`: their first tokens, and a last token after the
// last argument's last one (the closer), so that argument i is [firsts[i], firsts[i + 1] - 2].
static uint32_t check_mutation_arguments(const CheckMutationUnit* unit, uint32_t opener, uint32_t closer, uint32_t* firsts, uint32_t capacity)
{
    const uint32_t depth = unit->parser->tree.depths[opener] + 1;
    uint32_t count = 0;
    firsts[count++] = opener + 1;
    for (uint32_t token = opener + 1; token < closer && count < capacity; token += 1)
    {
        if (check_mutation_role(unit, token) == PARSE_ROLE_SEPARATOR && unit->parser->tree.depths[token] == depth)
        {
            firsts[count++] = token + 1;
        }
    }
    const uint32_t arguments = count;
    firsts[count < capacity ? count : capacity - 1] = closer + 1;
    return arguments;
}

// Two runs of tokens, [a, b] before [c, d], exchanged with the bytes between them kept.
static void check_mutation_exchange(CheckMutationEdit* edit, const CheckMutationUnit* unit, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    edit->begin = unit->starts[a];
    edit->end = unit->ends[d];
    const uint32_t left = unit->ends[b] - unit->starts[a];
    const uint32_t right = unit->ends[d] - unit->starts[c];
    const bool same = left == right && memcmp(unit->text + unit->starts[a], unit->text + unit->starts[c], left) == 0;
    check_mutation_append(edit, " ", 1);
    check_mutation_append(edit, unit->text + unit->starts[c], right);
    check_mutation_append(edit, unit->text + unit->ends[b], unit->starts[c] - unit->ends[b]);
    check_mutation_append(edit, unit->text + unit->starts[a], left);
    check_mutation_append(edit, " ", 1);
    // Exchanging two runs spelled alike changes nothing.
    edit->valid &= !same;
}

static void check_mutation_edit(CheckMutationEdit* edit, const CheckMutationUnit* unit, const CheckMutationSite* site, uint64_t* state)
{
    edit->valid = true;
    edit->replacement_length = 0;
    edit->detail[0] = 0;
    if (site->kind <= CHECK_MUTATION_STRUCT)
    {
        check_mutation_type(edit, unit, site, state);
    }
    else if (site->kind <= CHECK_MUTATION_REMOVE_MEMBER)
    {
        edit->begin = unit->starts[site->first];
        edit->end = unit->ends[site->second];
        check_mutation_append(edit, " ", 1);
        if (site->third != 0)
        {
            check_mutation_append_text(edit, "void ");
        }
        check_mutation_append_lines(edit, unit, edit->begin, edit->end);
        snprintf(edit->detail, sizeof(edit->detail), "removed %u tokens", site->second - site->first + 1);
    }
    else if (site->kind == CHECK_MUTATION_SWAP_OPERANDS)
    {
        check_mutation_exchange(edit, unit, site->first, site->second, site->third, site->fourth);
        snprintf(edit->detail, sizeof(edit->detail), "swapped the operands of '%.*s'", (int)(unit->ends[site->second + 1] - unit->starts[site->second + 1]),
                 unit->text + unit->starts[site->second + 1]);
    }
    else
    {
        uint32_t firsts[256];
        const uint32_t arguments = check_mutation_arguments(unit, site->first, site->second, firsts, BUSTER_ARRAY_LENGTH(firsts));
        const uint32_t i = check_mutation_choose(state, arguments - 1);
        const uint32_t j = i + 1 + check_mutation_choose(state, arguments - 1 - i);
        edit->valid = arguments >= 2 && arguments < BUSTER_ARRAY_LENGTH(firsts) && firsts[i + 1] >= firsts[i] + 2 && firsts[j + 1] >= firsts[j] + 2;
        if (edit->valid)
        {
            check_mutation_exchange(edit, unit, firsts[i], firsts[i + 1] - 2, firsts[j], firsts[j + 1] - 2);
            snprintf(edit->detail, sizeof(edit->detail), "swapped arguments %u and %u of %u", i + 1, j + 1, arguments);
        }
    }
    edit->valid &= edit->begin <= edit->end && edit->end <= unit->length;
}

// The base text with the edit made, malloc'd.
static char* check_mutation_text(const CheckMutationUnit* unit, const CheckMutationEdit* edit, size_t* length)
{
    *length = unit->length - (edit->end - edit->begin) + edit->replacement_length;
    char* text = malloc(*length + 1);
    if (text != NULL)
    {
        memcpy(text, unit->text, edit->begin);
        memcpy(text + edit->begin, edit->replacement, edit->replacement_length);
        memcpy(text + edit->begin + edit->replacement_length, unit->text + edit->end, unit->length - edit->end);
        text[*length] = 0;
    }
    return text;
}

// A site for mutant `index`: its family in turn, a kind of that family at random among those with a site in the file
// the mutant mutates (the unit's own, but every fourth mutant an included one, where either has none the other), and a
// site of that kind at random.
static const CheckMutationSite* check_mutation_pick(const CheckMutationUnit* unit, uint64_t index, uint64_t* state)
{
    const CheckMutationSite* result = NULL;
    for (uint32_t step = 0; result == NULL && step < BUSTER_CHECK_MUTATION_FAMILIES * 2; step += 1)
    {
        const uint32_t family = (uint32_t)((index + step / 2) % BUSTER_CHECK_MUTATION_FAMILIES);
        const uint32_t main = (index % 4 != 3) != (step % 2 == 1);
        uint32_t available = 0;
        for (uint32_t kind = check_mutation_families[family]; kind < check_mutation_families[family + 1]; kind += 1)
        {
            available += unit->site_offsets[kind * 2 + main + 1] != unit->site_offsets[kind * 2 + main];
        }
        uint32_t chosen = check_mutation_choose(state, available);
        for (uint32_t kind = check_mutation_families[family]; result == NULL && kind < check_mutation_families[family + 1]; kind += 1)
        {
            const uint32_t begin = unit->site_offsets[kind * 2 + main];
            const uint32_t end = unit->site_offsets[kind * 2 + main + 1];
            if (begin != end && chosen == 0)
            {
                result = unit->sites + unit->site_order[begin + check_mutation_choose(state, end - begin)];
            }
            chosen -= begin != end && chosen != 0;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Probes and the minimizer

// What clang (`oracle`) and the checker (`measured`) gave on a text.
static void check_mutation_outcome(const CheckCorpusOracle* oracle, const CheckCorpusOutcome* measured, CheckMutationOutcome* outcome)
{
    *outcome = (CheckMutationOutcome)
    {
        .valid = measured->prepared,
        .parsed = measured->parsed,
        .clang_rejected = oracle->rejected,
        .accepted = measured->accepted[0],
        .equal = measured->equal,
        .resource = measured->resource,
        .clang_line = oracle->line,
        .line = measured->lines[0],
        .phase = measured->phase,
    };
    memcpy(outcome->clang_file, oracle->file, sizeof(outcome->clang_file));
    memcpy(outcome->clang_message, oracle->message, sizeof(outcome->clang_message));
    memcpy(outcome->file, measured->file, sizeof(outcome->file));
    memcpy(outcome->message, measured->message, sizeof(outcome->message));
}

// Clang and the checker on `text`, written to the context's probe file: the scalar path alone, or both when `both`.
static bool check_mutation_probe(CheckMutationContext* context, const char* text, size_t length, bool both, CheckMutationOutcome* outcome)
{
    *outcome = (CheckMutationOutcome){ 0 };
    context->probes += 1;
    const int removed = unlink(context->probe);
    bool valid = (removed == 0 || errno == ENOENT) && check_corpus_write(context->probe, text, length);
    CheckCorpusOracle oracle;
    valid = valid && check_corpus_clang_file(context->clang, context->arguments, context->argument_count, context->probe, &oracle);
    if (valid)
    {
        CheckCorpusOutcome measured;
        check_corpus_measure(context->probe, &context->plain, oracle.suppress_warnings, both, &measured);
        check_mutation_outcome(&oracle, &measured, outcome);
        valid = outcome->valid;
    }
    return valid;
}

// Whether a candidate gives the difference the minimizer keeps: the same verdicts, messages and agreement of the paths,
// at the same lines and in the same files while `exact` (the blanking phases keep every line where it was), and a
// candidate of a mutant that parses parses too (so that what follows the first errors stays C).
static bool check_mutation_same(const CheckMutationOutcome* wanted, const CheckMutationOutcome* candidate, bool exact)
{
    bool same = candidate->valid && candidate->parsed == wanted->parsed && candidate->clang_rejected == wanted->clang_rejected &&
                candidate->accepted == wanted->accepted && candidate->equal == wanted->equal && candidate->resource == wanted->resource;
    same = same && (!wanted->clang_rejected || strcmp(candidate->clang_message, wanted->clang_message) == 0);
    same = same && (wanted->accepted || (strcmp(candidate->phase, wanted->phase) == 0 && strcmp(candidate->message, wanted->message) == 0));
    if (exact)
    {
        same = same && (!wanted->clang_rejected || (candidate->clang_line == wanted->clang_line && strcmp(candidate->clang_file, wanted->clang_file) == 0));
        same = same && (wanted->accepted || (candidate->line == wanted->line && strcmp(candidate->file, wanted->file) == 0));
    }
    else if (wanted->clang_rejected && !wanted->accepted)
    {
        // A difference of lines or files must stay one, the same way round.
        const bool lines = wanted->line != wanted->clang_line;
        same = same && (lines ? candidate->line != candidate->clang_line && (candidate->line < candidate->clang_line) == (wanted->line < wanted->clang_line) :
                                candidate->line == candidate->clang_line && strcmp(candidate->file, candidate->clang_file) != 0);
    }
    return same;
}

// Blanks the bytes [begin, end) of `text` but its newlines; returns whether anything but whitespace was there.
static bool check_mutation_blank(char* text, uint32_t begin, uint32_t end)
{
    bool changed = false;
    for (uint32_t at = begin; at < end; at += 1)
    {
        const char byte = text[at];
        changed |= byte != ' ' && byte != '\n' && byte != '\t' && byte != '\r';
        text[at] = byte == '\n' ? '\n' : ' ';
    }
    return changed;
}

// Removes the ranges of `segments` (pairs of bytes [begin, end) in `current`), a chunk at a time, while the difference
// stays (check_mutation_same, `exact` or not): a pass over chunks of half the ranges, then of half that, down to single
// ranges, whose passes repeat while one removes something (removing a range can make another removable). `current`
// keeps its length: a removed range's bytes become spaces, its newlines kept.
static void check_mutation_reduce(CheckMutationContext* context, const CheckMutationOutcome* wanted, char* current, size_t length,
                                  const uint32_t* segments, uint32_t count, bool both, bool exact, char* candidate)
{
    uint32_t chunk = count > 1 ? (count + 1) / 2 : 1;
    bool more = count != 0;
    while (more)
    {
        bool progress = false;
        for (uint32_t begin = 0; begin < count; begin += chunk)
        {
            const uint32_t end = count - begin < chunk ? count : begin + chunk;
            memcpy(candidate, current, length);
            bool changed = false;
            for (uint32_t segment = begin; segment < end; segment += 1)
            {
                changed |= check_mutation_blank(candidate, segments[segment * 2], segments[segment * 2 + 1]);
            }
            CheckMutationOutcome outcome = { 0 };
            const bool same = changed && check_mutation_probe(context, candidate, length, both, &outcome) && check_mutation_same(wanted, &outcome, exact);
            if (same)
            {
                memcpy(current, candidate, length);
                progress = true;
            }
        }
        more = chunk > 1 || progress;
        chunk = chunk > 1 ? (chunk + 1) / 2 : 1;
    }
}

// The ranges the minimizer removes, as pairs of bytes of the parsed text: phase 0 its top-level declarations, phase 1
// the statements of every block and the members of every struct.
static uint32_t check_mutation_segments(const CheckMutationUnit* unit, uint32_t phase, uint32_t* segments)
{
    uint32_t count = 0;
    for (uint32_t token = 0; token < unit->count; token += 1)
    {
        const uint32_t parent = check_mutation_parent(unit, token);
        const uint8_t parent_role = check_mutation_role(unit, parent);
        const uint8_t role = check_mutation_role(unit, token);
        const bool top = parent == BUSTER_PARSE_ROOT;
        const bool statement = (parent_role == PARSE_ROLE_CLOSE_BODY || parent_role == PARSE_ROLE_CLOSE_BLOCK) && role != PARSE_ROLE_OPEN_BODY &&
                               role != PARSE_ROLE_OPEN_BLOCK;
        const bool member = parent_role == PARSE_ROLE_CLOSE_STRUCT && role == PARSE_ROLE_TERMINATOR;
        if (phase == 0 ? top : statement || member)
        {
            segments[count * 2] = unit->starts[unit->firsts[token]];
            segments[count * 2 + 1] = unit->ends[token];
            count += 1;
        }
    }
    return count;
}

// The text's lines, as pairs of bytes, for a text that doesn't parse.
static uint32_t check_mutation_line_segments(const char* text, size_t length, uint32_t* segments)
{
    uint32_t count = 0;
    uint32_t begin = 0;
    for (uint32_t at = 0; at < length; at += 1)
    {
        if (text[at] == '\n' || at + 1 == length)
        {
            segments[count * 2] = begin;
            segments[count * 2 + 1] = at + 1;
            count += 1;
            begin = at + 1;
        }
    }
    return count;
}

// The candidate's tokens, as pairs of bytes, but those of directive lines (line markers and pragmas); or with
// `directives`, those lines alone, whole.
static uint32_t check_mutation_token_segments(const char* text, size_t length, bool directives, uint32_t* segments)
{
    const uint64_t capacity = BUSTER_C_TOKENIZER_CAPACITY(length);
    uint8_t* kinds = malloc(capacity);
    uint32_t* starts = malloc(capacity * sizeof(uint32_t));
    uint32_t* ends = malloc(capacity * sizeof(uint32_t));
    const int64_t lexed = kinds != NULL && starts != NULL && ends != NULL ? c_tokenize((const uint8_t*)text, length, kinds, starts, ends, 0) : 0;
    uint32_t count = 0;
    // Bytes before `cursor` have been classified: `directive` says whether the line holding the byte before it is one,
    // a line whose first byte after spaces is '#'.
    uint32_t cursor = 0;
    bool directive = false;
    for (int64_t token = 0; token < lexed; token += 1)
    {
        while (cursor <= starts[token])
        {
            if (cursor == 0 || text[cursor - 1] == '\n')
            {
                uint32_t at = cursor;
                while (at < length && (text[at] == ' ' || text[at] == '\t'))
                {
                    at += 1;
                }
                directive = at < length && text[at] == '#';
            }
            cursor += 1;
        }
        if (!directives && !directive && ends[token] > starts[token])
        {
            segments[count * 2] = starts[token];
            segments[count * 2 + 1] = ends[token];
            count += 1;
        }
        else if (directives && directive && (count == 0 || segments[count * 2 - 1] <= starts[token]))
        {
            // The directive's whole line, to its newline.
            uint32_t end = ends[token];
            while (end < length && text[end] != '\n')
            {
                end += 1;
            }
            segments[count * 2] = starts[token];
            segments[count * 2 + 1] = end;
            count += 1;
        }
    }
    free(kinds);
    free(starts);
    free(ends);
    return count;
}

// `text` without the lines that `drop` says: 2 for lines of spaces alone, 3 for runs of spaces collapsed to one and
// trailing spaces dropped. The result is malloc'd.
static char* check_mutation_compact(const char* text, size_t length, uint32_t drop, size_t* result_length)
{
    char* result = malloc(length + 1);
    size_t used = 0;
    size_t begin = 0;
    while (result != NULL && begin < length)
    {
        size_t end = begin;
        while (end < length && text[end] != '\n')
        {
            end += 1;
        }
        size_t first = begin;
        while (first < end && (text[first] == ' ' || text[first] == '\t'))
        {
            first += 1;
        }
        const bool dropped = drop == 2 && first == end;
        if (!dropped && drop == 3)
        {
            const size_t line = used;
            bool space = false;
            for (size_t at = begin; at < end; at += 1)
            {
                const bool blank = text[at] == ' ' || text[at] == '\t';
                if (!blank || !space)
                {
                    result[used++] = blank ? ' ' : text[at];
                }
                space = blank;
            }
            while (used > line && result[used - 1] == ' ')
            {
                used -= 1;
            }
        }
        else if (!dropped)
        {
            memcpy(result + used, text + begin, end - begin);
            used += end - begin;
        }
        if (!dropped && end < length)
        {
            result[used++] = '\n';
        }
        begin = end + 1;
    }
    if (result != NULL)
    {
        result[used] = 0;
    }
    *result_length = used;
    return result;
}

// Minimizes `text` while it gives the difference `wanted` gave: removes top-level declarations, then statements and
// members (from the text's own parse, or its lines if it doesn't parse), then tokens, each phase keeping every line
// where it was; then drops the directives, the empty lines and extra spaces where the difference stays (lines may move
// then). The result is malloc'd, and `final` what both paths give on it.
static char* check_mutation_minimize(CheckMutationContext* context, const char* text, size_t length, const CheckMutationOutcome* wanted,
                                     size_t* result_length, CheckMutationOutcome* final)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    const bool both = !wanted->equal;
    char* current = malloc(length + 1);
    char* candidate = malloc(length + 1);
    const uint64_t capacity = (BUSTER_C_TOKENIZER_CAPACITY(length) + 1) * 2;
    uint32_t* segments[2] =
    {
        malloc(capacity * sizeof(uint32_t)),
        malloc(capacity * sizeof(uint32_t)),
    };
    CheckMutationUnit parsed =
    {
        .text = malloc(length + 1),
        .length = length,
    };
    const bool valid = current != NULL && candidate != NULL && segments[0] != NULL && segments[1] != NULL && parsed.text != NULL;
    uint32_t counts[2] = { 0 };
    if (valid)
    {
        memcpy(current, text, length);
        current[length] = 0;
        memcpy(parsed.text, text, length);
        parsed.text[length] = 0;
        const bool tree = check_mutation_read(&parsed, context->probe, &context->plain, &identifiers, &pp, &parser);
        counts[0] = tree ? check_mutation_segments(&parsed, 0, segments[0]) : check_mutation_line_segments(text, length, segments[0]);
        counts[1] = tree ? check_mutation_segments(&parsed, 1, segments[1]) : 0;
    }
    parse_release(&parser);
    pp_release(&pp);
    intern_release(&identifiers);
    check_mutation_release(&parsed);
    for (uint32_t phase = 0; valid && phase < 2; phase += 1)
    {
        check_mutation_reduce(context, wanted, current, length, segments[phase], counts[phase], both, true, candidate);
    }
    if (valid)
    {
        const uint32_t count = check_mutation_token_segments(current, length, false, segments[0]);
        check_mutation_reduce(context, wanted, current, length, segments[0], count, both, true, candidate);
    }
    // The line markers a system header's place needs stay; the others go, and the lines move.
    if (valid)
    {
        const uint32_t count = check_mutation_token_segments(current, length, true, segments[0]);
        check_mutation_reduce(context, wanted, current, length, segments[0], count, both, false, candidate);
    }
    size_t current_length = length;
    for (uint32_t drop = 2; valid && drop <= 3; drop += 1)
    {
        size_t compact_length = 0;
        char* compact = check_mutation_compact(current, current_length, drop, &compact_length);
        CheckMutationOutcome outcome;
        const bool kept = compact != NULL && check_mutation_probe(context, compact, compact_length, both, &outcome) && check_mutation_same(wanted, &outcome, false);
        if (kept)
        {
            memcpy(current, compact, compact_length + 1);
            current_length = compact_length;
        }
        free(compact);
    }
    // The result on both paths, as test-checker will check it.
    const bool final_valid = valid && check_mutation_probe(context, current, current_length, true, final);
    free(candidate);
    free(segments[0]);
    free(segments[1]);
    if (!final_valid)
    {
        free(current);
        current = NULL;
    }
    *result_length = final_valid ? current_length : 0;
    return current;
}

// Writes a minimized difference as checker_mutation_N.c in the current directory, at the first N not taken, never over
// an earlier one, with what it is in a comment after its last line (so that its lines stay where they are).
static bool check_mutation_save(const char* text, size_t length, const char* label, const CheckMutationOutcome* outcome, char* name, size_t name_size)
{
    int descriptor = -1;
    for (uint32_t number = 0; descriptor < 0 && number < 100000; number += 1)
    {
        snprintf(name, name_size, "checker_mutation_%u.c", number);
        descriptor = open(name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        number = descriptor < 0 && errno != EEXIST ? 100000 : number;
    }
    FILE* stream = descriptor >= 0 ? fdopen(descriptor, "w") : NULL;
    bool valid = stream != NULL && fwrite(text, 1, length, stream) == length;
    if (valid)
    {
        // The label and messages can't end the comment early: "*/" is written "* /".
        char description[2048];
        char clang[400] = "accepted";
        char checker[400] = "accepted";
        if (outcome->clang_rejected)
        {
            snprintf(clang, sizeof(clang), "rejected at line %u: %s", outcome->clang_line, outcome->clang_message);
        }
        if (!outcome->accepted)
        {
            snprintf(checker, sizeof(checker), "rejected at line %u: %s (%s)", outcome->line, outcome->message, outcome->phase);
        }
        snprintf(description, sizeof(description), "test-checker-corpus --semantic-mutations: %s\nclang: %s\nchecker: %s\npaths: %s", label, clang,
                 checker, outcome->equal ? "equal" : "DIFFERENT");
        for (char* at = strstr(description, "*/"); at != NULL; at = strstr(at, "*/"))
        {
            at[1] = ' ';
        }
        valid = fprintf(stream, "%s/*\n%s\n*/\n", length != 0 && text[length - 1] != '\n' ? "\n" : "", description) > 0;
    }
    if (stream != NULL)
    {
        valid = fclose(stream) == 0 && valid;
    }
    else if (descriptor >= 0)
    {
        close(descriptor);
    }
    return valid;
}

// The intended difference `outcome` is, or NULL.
static const CheckMutationIntended* check_mutation_intended_match(const CheckMutationOutcome* outcome)
{
    const CheckMutationIntended* result = NULL;
    for (uint32_t i = 0; result == NULL && i < BUSTER_ARRAY_LENGTH(check_mutation_intended); i += 1)
    {
        const CheckMutationIntended* entry = check_mutation_intended + i;
        const bool matches = outcome->equal && !outcome->resource && entry->clang_rejected == outcome->clang_rejected &&
                             entry->checker_accepted == outcome->accepted && strstr(outcome->clang_message, entry->clang_message) != NULL &&
                             strstr(outcome->message, entry->checker_message) != NULL;
        result = matches ? entry : NULL;
    }
    return result;
}

// Whether clang and the checker differ on a text: acceptance, the first error's line or file, or the two paths.
static bool check_mutation_different(const CheckMutationOutcome* outcome)
{
    return !outcome->equal || outcome->resource || outcome->accepted == outcome->clang_rejected ||
           (outcome->clang_rejected && (outcome->line != outcome->clang_line || strcmp(outcome->file, outcome->clang_file) != 0));
}

// The context the campaign and --minimize-mutant check texts in: preprocessed text, read with `options` but no
// predefined macros, includes or -D, and clang's arguments with -x cpp-output after them.
static bool check_mutation_context(CheckMutationContext* context, const PPOptions* options, const char* clang, const char* const* arguments,
                                   uint32_t argument_count, const char* directory)
{
    *context = (CheckMutationContext)
    {
        .plain = *options,
        .clang = clang,
        .directory = directory,
    };
    context->plain.no_standard_includes = true;
    context->plain.include_count = 0;
    context->plain.macro_count = 0;
    context->plain.predefines_path = 0;
    context->plain.undefine = true;
    context->arguments = malloc(((size_t)argument_count + 2) * sizeof(char*));
    context->argument_count = argument_count + 2;
    snprintf(context->probe, sizeof(context->probe), "%s/minimize.c", directory);
    const bool valid = context->arguments != NULL;
    if (valid)
    {
        memcpy(context->arguments, arguments, (size_t)argument_count * sizeof(char*));
        context->arguments[argument_count] = "-x";
        context->arguments[argument_count + 1] = "cpp-output";
    }
    return valid;
}

static bool check_mutation_context_release(CheckMutationContext* context)
{
    const int removed = unlink(context->probe);
    free(context->arguments);
    context->arguments = NULL;
    return removed == 0 || errno == ENOENT;
}

// --minimize-mutant: each of `paths` (a mutant --keep retained, or any preprocessed text) probed on both paths and, where
// clang and the checker differ, minimized and saved as the campaign saves its differences. True if every file could be
// probed and every difference was saved.
static bool check_mutation_minimize_files(char** paths, uint32_t count, const PPOptions* options, const char* clang, const char* const* arguments,
                                          uint32_t argument_count, const char* directory)
{
    static CheckMutationContext context;
    bool valid = check_mutation_context(&context, options, clang, arguments, argument_count, directory);
    for (uint32_t i = 0; valid && i < count; i += 1)
    {
        FILE* stream = fopen(paths[i], "rb");
        char* text = NULL;
        size_t length = 0;
        valid = stream != NULL && fseek(stream, 0, SEEK_END) == 0;
        const long size = valid ? ftell(stream) : -1;
        valid = valid && size >= 0 && (uint64_t)size < UINT32_MAX / 2 && fseek(stream, 0, SEEK_SET) == 0;
        text = valid ? malloc((size_t)size + 1) : NULL;
        valid = text != NULL && fread(text, 1, (size_t)size, stream) == (size_t)size;
        length = valid ? (size_t)size : 0;
        if (stream != NULL)
        {
            fclose(stream);
        }
        CheckMutationOutcome outcome;
        valid = valid && check_mutation_probe(&context, text, length, true, &outcome);
        if (valid && check_mutation_different(&outcome))
        {
            size_t minimized_length = 0;
            CheckMutationOutcome final;
            const uint64_t probes = context.probes;
            char* minimized = check_mutation_minimize(&context, text, length, &outcome, &minimized_length, &final);
            char saved_name[64];
            valid = minimized != NULL && check_mutation_save(minimized, minimized_length, paths[i], &final, saved_name, sizeof(saved_name));
            printf("checker-corpus: %s: %s (%zu bytes, from %zu, in %" PRIu64 " probes)\n", paths[i], valid ? saved_name : "not minimized",
                   minimized_length, length, context.probes - probes);
            free(minimized);
        }
        else if (valid)
        {
            printf("checker-corpus: %s: no difference\n", paths[i]);
        }
        free(text);
    }
    valid = check_mutation_context_release(&context) && valid;
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The campaign

// `count` mutants of the units `bases` marks (check_corpus_files), spread over them in an order the seed shuffles, each
// unit taking an equal share (one that can't be mutated passes its share on). Each mutant is checked by
// check_corpus_file_outcome (kind "semantic", its base's "semantic-base"); the first `minimize` differences that aren't
// intended are minimized and saved.
static bool check_mutation_campaign(CheckCorpusTally* tally, CheckMutationTally* mutations, char** files, const bool* bases, uint32_t file_count,
                                    uint64_t count, uint64_t seed, uint64_t minimize, const char* directory, bool keep, const PPOptions* options,
                                    const char* clang, const char* const* arguments, uint32_t argument_count, FILE* report, uint64_t report_limit)
{
    static InternTable identifiers;
    static Preprocessor pp;
    static Parser parser;
    static CheckMutationContext context;
    uint32_t* order = malloc(((size_t)file_count + 1) * sizeof(uint32_t));
    bool valid = check_mutation_context(&context, options, clang, arguments, argument_count, directory) && order != NULL;
    uint32_t eligible = 0;
    for (uint32_t file = 0; valid && file < file_count; file += 1)
    {
        if (bases[file])
        {
            order[eligible++] = file;
        }
    }
    uint64_t state = seed ^ 0x5be7c1e5d0f3a9b1ull;
    for (uint32_t i = eligible; valid && i > 1; i -= 1)
    {
        const uint32_t j = (uint32_t)(parse_test_random(&state) % i);
        const uint32_t swapped = order[i - 1];
        order[i - 1] = order[j];
        order[j] = swapped;
    }
    CheckMutationEdit edit = { 0 };
    uint64_t done = 0;
    uint64_t carried = 0;
    for (uint32_t position = 0; valid && position < eligible && done < count; position += 1)
    {
        const uint64_t share = count / eligible + (position < count % eligible) + carried;
        const char* file = files[order[position]];
        char base[4300];
        snprintf(base, sizeof(base), "%s/semantic_base_%u.c", directory, position);
        CheckMutationUnit unit = { 0 };
        bool usable = share != 0 && check_mutation_prepare(&unit, file, options, &context.plain, base, &identifiers, &pp, &parser);
        usable = usable && unit.site_count != 0;
        bool base_different = false;
        if (usable)
        {
            CheckCorpusOracle oracle;
            char label[4400];
            snprintf(label, sizeof(label), "%s (semantic base, seed=%" PRIu64 ")", file, seed);
            valid = check_corpus_clang_file(clang, context.arguments, context.argument_count, base, &oracle);
            base_different = valid && check_corpus_file(tally, base, label, "semantic-base", &context.plain, &oracle, report, report_limit);
            mutations->units += valid;
            mutations->base_differences += base_different;
            for (uint32_t site = 0; site < unit.site_count; site += 1)
            {
                mutations->sites[unit.sites[site].kind] += 1;
            }
        }
        mutations->unusable += share != 0 && !usable;
        // A unit that can't be mutated, or whose base differs, passes its share on, as does a mutant no site gives.
        uint64_t made = 0;
        uint64_t unmade = 0;
        while (valid && usable && !base_different && made < share && unmade < share)
        {
            const uint64_t index = done;
            uint64_t choice = seed * 0x9e3779b97f4a7c15ull ^ (index + unmade * 0x10000000000ull);
            const CheckMutationSite* site = NULL;
            edit.valid = false;
            for (uint32_t attempt = 0; !edit.valid && attempt < BUSTER_CHECK_MUTATION_ATTEMPTS; attempt += 1)
            {
                site = check_mutation_pick(&unit, index, &choice);
                if (site != NULL)
                {
                    check_mutation_edit(&edit, &unit, site, &choice);
                }
            }
            size_t length = 0;
            char* text = edit.valid ? check_mutation_text(&unit, &edit, &length) : NULL;
            unmade += !edit.valid;
            char path[4300];
            snprintf(path, sizeof(path), "%s/semantic_%" PRIu64 ".c", directory, index);
            valid = !edit.valid || (text != NULL && check_corpus_write(path, text, length));
            CheckCorpusOracle oracle;
            valid = valid && (!edit.valid || check_corpus_clang_file(clang, context.arguments, context.argument_count, path, &oracle));
            if (valid && edit.valid)
            {
                const uint32_t location = pp.output.locations[parser.tokens.sources[site->first]];
                const char* name = NULL;
                uint32_t name_length = 0;
                const uint32_t line = pp_presumed(&pp, location, &name, &name_length);
                char label[4400];
                snprintf(label, sizeof(label), "%s (seed=%" PRIu64 " semantic=%" PRIu64 " %s at %.*s:%u '%.*s': %s)", file, seed, index,
                         check_mutation_names[site->kind], (int)name_length, name, line, (int)(unit.ends[site->first] - unit.starts[site->first]),
                         unit.text + unit.starts[site->first], edit.detail);
                // The mutant's own tally, added to the run's without its differences if they are intended.
                CheckCorpusTally single =
                {
                    .reports = tally->reports,
                };
                CheckCorpusOutcome measured;
                const bool different = check_corpus_file_outcome(&single, path, label, "semantic", &context.plain, &oracle, report, report_limit, &measured);
                single.reports -= tally->reports;
                CheckMutationOutcome outcome;
                check_mutation_outcome(&oracle, &measured, &outcome);
                const CheckMutationIntended* intended = different ? check_mutation_intended_match(&outcome) : NULL;
                check_corpus_tally_add(tally, &single, intended == NULL);
                mutations->mutants += 1;
                mutations->kinds[site->kind] += 1;
                mutations->kind_rejected[site->kind] += oracle.rejected;
                mutations->intended += intended != NULL;
                mutations->differences += different && intended == NULL;
                mutations->kind_differences[site->kind] += different && intended == NULL;
                if (intended != NULL)
                {
                    fprintf(stderr, "%s: intended difference: %s\n", label, intended->reason);
                }
                if (different && intended == NULL && mutations->minimized < minimize && outcome.valid)
                {
                    mutations->minimized += 1;
                    size_t minimized_length = 0;
                    CheckMutationOutcome final;
                    const uint64_t probes = context.probes;
                    char* minimized = check_mutation_minimize(&context, text, length, &outcome, &minimized_length, &final);
                    char saved_name[64];
                    const bool saved = minimized != NULL && check_mutation_save(minimized, minimized_length, label, &final, saved_name, sizeof(saved_name));
                    mutations->saved += saved;
                    fprintf(stderr, "%s: %s (%zu bytes, from %zu, in %" PRIu64 " probes)\n", label, saved ? saved_name : "not minimized",
                            minimized_length, length, context.probes - probes);
                    free(minimized);
                }
                if (!(keep && different))
                {
                    valid = unlink(path) == 0;
                }
                made += 1;
                done += 1;
            }
            free(text);
        }
        carried = share - made;
        if (usable && !(keep && base_different))
        {
            valid = unlink(base) == 0 && valid;
        }
        else if (!usable && share != 0)
        {
            const int removed = unlink(base);
            valid = (removed == 0 || errno == ENOENT) && valid;
        }
        check_mutation_release(&unit);
        parse_release(&parser);
        pp_release(&pp);
        intern_release(&identifiers);
    }
    valid = check_mutation_context_release(&context) && valid;
    if (valid && done < count)
    {
        fprintf(stderr, "checker-corpus: %" PRIu64 " of %" PRIu64 " semantic mutants made: no base left to mutate\n", done, count);
    }
    free(edit.replacement);
    free(order);
    return valid && done == count;
}

#endif
