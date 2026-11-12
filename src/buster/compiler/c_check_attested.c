// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2531's step 0, the census (docs/attested-declarations.md, "The steps and their gates"): what attested declarations
// would skip, counted on the pipeline as it is. measure-attested runs a command of several units as `--lanes 1` runs
// it, and beside it, at two places of each unit that check_driver_unit gives it (one test of check_driver_attested a
// place, which only this command sets):
//
//   - check_attested_input, the reader's call: before the reader, the design's passes over the parser's tree, timed by
//     this thread's user-mode cycles: the lists (each file-scope declaration's range, and each identifier's class by its
//     role and the brackets around it), the declarations' own hashes, the chain (each one's semantic hash from its own
//     and its names' current hashes, the memo's probe, its declared names' new hashes), the reach (from the roots, by
//     two cursors a name) and the marks (the skipped ranges' bits). Then the reader, check_input_run as -c calls it but
//     with its environment published, which copies the binding events past the reader and changes nothing it reads or
//     writes.
//   - check_attested_after, the unit's end: what each declaration's reading did, from what the reader and the later
//     stages left. Silent: no diagnostic of the driver's collected ones (PPOptions.collect_diagnostics, every stage's)
//     is placed at one of its tokens by its output position. Pure: no token of it has a definition, a tentative
//     definition, internal linkage, a global register or an alias (CheckInput.declaration_bits and its attributes),
//     and it is no file-scope asm statement. Listed: each of its file-scope DECLARE events, and each tag it completes,
//     is for a name its list declares; each of its USE events for one its list holds. Then the memo learns, at the end
//     of a unit without an error and with every diagnostic placed, the semantic hashes of the declarations that read
//     silently, purely and listed.
//
// No compile changes: every declaration is read, and the counts say what step 2 would skip. A declaration the memo
// attests is also checked against its own reading here (a contradiction: an input of a reading the hash doesn't cover).
//
//   build/compiler measure-attested [--list N] [--passes N] [compile options] file...
//
// The names no token spells (the design's "Names reached without a token") count as spelled by a root: the library
// function behind a `__builtin_` name a reached declaration spells, the identifiers in the strings of a reached
// declaration's asm and attribute groups (asm labels, alias and weakref targets, asm statements), and in a unit with a
// function definition every runtime function the lowering looks up by name in every such unit
// (check_lower_runtime_names before CHECK_LOWER_RUNTIME_LIBRARY), whose first declaration gives a runtime call's symbol.
// The last is counted apart: the declarations it alone reaches.
//
// Layout: CheckAttested (the command's state and counts), check_attested_ids (each new name's facts), the passes
// (check_attested_lists, check_attested_hashes, check_attested_chain, check_attested_reach, check_attested_marks),
// check_attested_input, the record (check_attested_after), check_attested_report and run_attested_census. Include after
// c_checker_units.c: c_checker_driver.c declares the two calls and check_driver_attested.
//
// This is a measurement beside the compile: the passes are the design's in scalar code, one declaration's chain at a
// time, and their figures are what a vector path must beat.
#ifndef BUSTER_C_CHECK_ATTESTED_INCLUDED
#define BUSTER_C_CHECK_ATTESTED_INCLUDED

#include <linux/perf_event.h>
#include <sys/syscall.h>

// An identifier's class in its declaration's list: it declares the name at file scope (a superset: every name a reading
// can declare), its reading looks the name up, and its namespace (the tags', or the ordinary names').
#define BUSTER_ATTESTED_DECLARES 0x01u
#define BUSTER_ATTESTED_LOOKS_UP 0x02u
#define BUSTER_ATTESTED_TAG 0x04u

// A declaration's flags: a function definition (a root, never attested), a pragma inside it, a string in an asm or an
// attribute group (symbols the compiler reads by name), and a file-scope asm statement.
#define BUSTER_ATTESTED_DEFINITION 0x01u
#define BUSTER_ATTESTED_PRAGMA 0x02u
#define BUSTER_ATTESTED_STRINGS 0x04u
#define BUSTER_ATTESTED_ASM 0x08u

// A name's facts (CheckAttested.id_facts): known, a `__builtin_` name, a runtime function's name.
#define BUSTER_ATTESTED_ID_KNOWN 0x01u
#define BUSTER_ATTESTED_ID_BUILTIN 0x02u
#define BUSTER_ATTESTED_ID_RUNTIME 0x04u

// The most names the census follows (a key is a name's IdentId * 2 + its namespace), and the memo's first and most
// slots: it doubles at three quarters full, so that its few thousand hashes (musl's command's 3,218) stay in the
// second-level cache, where a table of the most slots put each probe in memory.
#define BUSTER_ATTESTED_MAX_IDS (1u << 24)
#define BUSTER_ATTESTED_MEMO_FIRST_SLOTS (1u << 12)
#define BUSTER_ATTESTED_MEMO_SLOTS (1u << 22)
#define BUSTER_ATTESTED_NONE UINT32_MAX

// The passes, as they are timed.
typedef enum CheckAttestedPass
{
    CHECK_ATTESTED_PASS_LISTS,
    CHECK_ATTESTED_PASS_HASHES,
    CHECK_ATTESTED_PASS_LITERALS,
    CHECK_ATTESTED_PASS_CHAIN,
    CHECK_ATTESTED_PASS_REACH,
    CHECK_ATTESTED_PASS_MARKS,
    CHECK_ATTESTED_PASSES,
} CheckAttestedPass;

static const char* const check_attested_pass_names[CHECK_ATTESTED_PASSES] =
{
    "lists",
    "own-hashes",
    "literals",
    "chain",
    "reach",
    "marks",
};

// What became of a declaration, the first that holds in this order.
typedef enum CheckAttestedOutcome
{
    // Its unit's parse failed or the unit reported an error: read again with nothing skipped, nothing learned.
    CHECK_ATTESTED_ERROR_UNIT,
    // A function definition: a root, never attested.
    CHECK_ATTESTED_DEFINITION_ROOT,
    // A pragma inside it: never attested.
    CHECK_ATTESTED_PRAGMA_INSIDE,
    // Attested and not reached: the walk would pass it.
    CHECK_ATTESTED_SKIPPED,
    // Attested, and reached by the unit's code: read.
    CHECK_ATTESTED_REACHED,
    // Attested, and reached only by the runtime functions' names: read.
    CHECK_ATTESTED_REACHED_RUNTIME,
    // A miss, read silently, purely and listed: the memo learns it at the unit's end.
    CHECK_ATTESTED_LEARNED,
    // A miss that would be learned, in a unit with a diagnostic no declaration holds: the memo learns nothing from it.
    CHECK_ATTESTED_UNPLACED,
    // A miss whose reading wasn't silent: a diagnostic at one of its tokens.
    CHECK_ATTESTED_DIAGNOSTIC,
    // A miss whose reading declared a name its list doesn't hold, or completed a tag it doesn't, or looked one up.
    CHECK_ATTESTED_UNLISTED,
    // A miss whose reading had a consequence at the unit's end: a definition (an initializer), a tentative definition,
    // internal linkage, a global register, an alias, a file-scope asm statement.
    CHECK_ATTESTED_DEFINES,
    CHECK_ATTESTED_TENTATIVE,
    CHECK_ATTESTED_INTERNAL,
    CHECK_ATTESTED_REGISTER,
    CHECK_ATTESTED_ALIAS,
    CHECK_ATTESTED_ASM_STATEMENT,
    CHECK_ATTESTED_OUTCOMES,
} CheckAttestedOutcome;

static const char* const check_attested_outcome_names[CHECK_ATTESTED_OUTCOMES] =
{
    "error-unit",
    "definition",
    "pragma-inside",
    "skipped",
    "reached",
    "reached-by-runtime-names",
    "learned",
    "unplaced-diagnostic",
    "diagnostic",
    "unlisted",
    "defines",
    "tentative",
    "internal",
    "register",
    "alias",
    "asm-statement",
};

// The passes' counters.
typedef enum CheckAttestedCounter
{
    CHECK_ATTESTED_CYCLES,
    CHECK_ATTESTED_BRANCH_MISSES,
    CHECK_ATTESTED_DATA_MISSES,
    CHECK_ATTESTED_COUNTERS,
} CheckAttestedCounter;

typedef struct CheckAttestedHash CheckAttestedHash;
struct CheckAttestedHash
{
    uint64_t low;
    uint64_t high;
};

// A declaration's outcome, for the tests (CheckAttested.records): its unit's place in the command's order (0 first), its
// line and file (the last part of its name, cut to fit), whether that is the unit's own file, and whether its reading
// contradicted a hit.
typedef struct CheckAttestedRecord CheckAttestedRecord;
struct CheckAttestedRecord
{
    uint32_t unit;
    uint32_t line;
    char file[24];
    uint8_t outcome;
    bool main_file;
    bool contradiction;
};

// A name's state across the lane's units, by key (IdentId * 2 + namespace), 16 bytes: in the unit whose stamp it has,
// its current hash, and its declarers' list (`local`, an index of the unit's dense lists, or BUSTER_ATTESTED_NONE). A
// header's names take consecutive ids where the lane first interns them, so that four of them share a cache line, which
// the compile between two units evicts.
typedef struct CheckAttestedKey CheckAttestedKey;
struct CheckAttestedKey
{
    uint64_t hash;
    uint32_t stamp;
    uint32_t local;
};
_Static_assert(sizeof(CheckAttestedKey) == 16, "four names' states a cache line");

// A name's declarers in the unit (CheckAttested.lists, by a key's `local`): a list of the unit's entries in source
// order (entry_declarations, entry_next, entry_previous), its first and last entry and their count, and the reach's two
// cursors and what is still between them.
typedef struct CheckAttestedList CheckAttestedList;
struct CheckAttestedList
{
    uint32_t first;
    uint32_t last;
    uint32_t count;
    uint32_t low;
    uint32_t high;
    uint32_t remaining;
};

// A name entry of a declaration's list (CheckAttested.name_entries): its key, and its class above it.
#define BUSTER_ATTESTED_CLASS_SHIFT 25u
#define BUSTER_ATTESTED_KEY_MASK ((1u << BUSTER_ATTESTED_CLASS_SHIFT) - 1)
_Static_assert(2 * (uint64_t)BUSTER_ATTESTED_MAX_IDS <= (UINT64_C(1) << BUSTER_ATTESTED_CLASS_SHIFT), "a key fits below its class");

// The command's census: its options, the lane's state across its units (the memo, the names' current hashes and the
// reach's lists, validated by a stamp, and the names' facts), the unit's arrays and the counts.
typedef struct CheckAttested CheckAttested;
struct CheckAttested
{
    // --list N: the first N declarations of each outcome but skipped, and every contradiction, by file and line.
    uint32_t list;
    // --passes N: the passes run N times a unit (1 by default), the least cycles of each kept; the first run's results.
    uint32_t passes;
    // The memo: a set of 128-bit hashes, open addressing in `memo_mask + 1` slots, a zero slot empty (a hash's low word is
    // never zero).
    Arena memo_arena;
    CheckAttestedHash* memo;
    uint64_t memo_mask;
    uint64_t memo_count;
    // By key: the names' states.
    Arena key_arena;
    CheckAttestedKey* keys;
    // By IdentId: the name's facts (BUSTER_ATTESTED_ID_*), a `__builtin_X` name's X (0 until it is interned), and how
    // many of the table's names have them.
    uint8_t* id_facts;
    uint32_t* builtin_targets;
    uint32_t ids_known;
    // The lists' parts of each parser kind and role (check_attested_actions).
    uint8_t kind_actions[256];
    uint8_t role_actions[256];
    // The keys of the runtime functions' names the lane's table holds.
    uint32_t* runtime_keys;
    uint32_t runtime_count;
    uint32_t* runtime_lengths;
    uint32_t stamp;
    bool failed;
    // The unit's arrays, in `unit_arena`, made by check_attested_input and read by check_attested_after.
    Arena unit_arena;
    bool unit_ready;
    const char* path;
    uint32_t token_count;
    uint32_t declaration_count;
    uint32_t* firsts;
    uint32_t* lasts;
    uint8_t* flags;
    uint32_t* locations;
    uint32_t* name_starts;
    uint32_t* name_entries;
    uint32_t name_count;
    CheckAttestedHash* owns;
    CheckAttestedHash* semantics;
    uint8_t* hits;
    uint8_t* reached;
    uint8_t* reached_without_runtime;
    uint32_t* worklist;
    uint32_t* entry_declarations;
    uint32_t* entry_next;
    uint32_t* entry_previous;
    // The unit's declarers' lists, one a name it declares (`distinct_count` of them).
    CheckAttestedList* lists;
    uint32_t distinct_count;
    uint64_t* marks;
    // Each pragma's place (the declaration it comes before, or its own), its hash, and the keys it spells.
    uint32_t pragma_count;
    uint32_t* pragma_declarations;
    uint8_t* pragma_inside;
    CheckAttestedHash* pragma_hashes;
    uint32_t* pragma_name_starts;
    uint32_t* pragma_keys;
    // The strings in asm and attribute groups: their tokens, by declaration.
    uint32_t string_count;
    uint32_t* string_tokens;
    uint32_t* string_declarations;
    // The pp output's system levels, and the parser tokens' (all a user file's where there are no system files), as the
    // reader's check_input_system_tokens maps them before its walk; and a spelling's buffer.
    uint8_t* levels;
    uint8_t* token_levels;
    uint8_t* spelling;
    uint64_t spelling_capacity;
    bool any_definition;
    // The thread's user-mode counters (cycles, branch misses, first-level data cache misses: CHECK_ATTESTED_COUNTERS),
    // each one's page (read with rdpmc where the kernel allows, so that no system call comes between two passes), and
    // the thread they count.
    int counters[CHECK_ATTESTED_COUNTERS];
    volatile struct perf_event_mmap_page* pages[CHECK_ATTESTED_COUNTERS];
    pid_t cycles_thread;
    bool counted;
    // The counts.
    uint64_t units_observed;
    uint64_t units_failed;
    uint64_t units_unplaced;
    uint64_t units_without_passes;
    uint64_t tokens;
    uint64_t failed_tokens;
    uint64_t declarations;
    uint64_t declaration_tokens;
    uint64_t outcome_declarations[CHECK_ATTESTED_OUTCOMES];
    uint64_t outcome_tokens[CHECK_ATTESTED_OUTCOMES];
    uint64_t hits_total;
    uint64_t hit_tokens;
    uint64_t contradictions;
    uint64_t contradiction_tokens;
    uint64_t unlisted_declares;
    uint64_t unlisted_completions;
    uint64_t unlisted_uses;
    uint64_t diagnostics;
    uint64_t unplaced_diagnostics;
    uint64_t declare_events;
    uint64_t use_events;
    uint64_t pass_tokens;
    // Each pass's branch misses and first-level data cache misses: the first run's, and the run of least cycles'.
    uint64_t first_events[CHECK_ATTESTED_PASSES][CHECK_ATTESTED_COUNTERS];
    uint64_t least_events[CHECK_ATTESTED_PASSES][CHECK_ATTESTED_COUNTERS];
    // The own hashes' literals (counted on every run of the passes): all, those spelled (not an integer's value), and
    // the searches of the ranges their spellings took.
    uint64_t literal_count;
    uint64_t spelled_literals;
    uint64_t range_searches;
    uint64_t pass_cycles[CHECK_ATTESTED_PASSES];
    uint64_t first_cycles[CHECK_ATTESTED_PASSES];
    uint64_t pass_ticks[CHECK_ATTESTED_PASSES];
    uint64_t read_overhead;
    uint64_t listed[CHECK_ATTESTED_OUTCOMES];
    uint64_t contradictions_listed;
    // Each declaration's record, where a test gives room for them (null otherwise), and how many there are.
    CheckAttestedRecord* records;
    uint32_t record_capacity;
    uint32_t record_count;
};

// The census a measure-attested command runs beside its units (c_checker_driver.c declares it): null in every other.
static CheckAttested* check_driver_attested;

// ---------------------------------------------------------------------------------------------------------------------
// Hashing: a token's word into two lanes of a sum, each lane a multiply folded (wyhash's mum), so that a declaration's
// tokens hash in any order with their positions, and the chain's steps one after another.

#define BUSTER_ATTESTED_K0 0xa0761d6478bd642full
#define BUSTER_ATTESTED_K1 0xe7037ed1a0b428dbull
#define BUSTER_ATTESTED_K2 0x8ebc6af09c88c6e3ull
#define BUSTER_ATTESTED_K3 0x589965cc75374cc3ull

static inline uint64_t check_attested_mum(uint64_t a, uint64_t b)
{
    const unsigned __int128 product = (unsigned __int128)a * b;
    return (uint64_t)product ^ (uint64_t)(product >> 64);
}

// One step of a chain: `state` with `word` mixed in.
static inline CheckAttestedHash check_attested_step(CheckAttestedHash state, uint64_t word)
{
    const uint64_t low = check_attested_mum(state.low ^ word ^ BUSTER_ATTESTED_K0, state.high ^ BUSTER_ATTESTED_K1);
    const uint64_t high = check_attested_mum(state.high ^ word ^ BUSTER_ATTESTED_K2, low ^ BUSTER_ATTESTED_K3);
    return (CheckAttestedHash){ .low = low | 1, .high = high };
}

// A string's hash, eight bytes at a time.
static uint64_t check_attested_bytes(const uint8_t* bytes, uint32_t length)
{
    uint64_t hash = BUSTER_ATTESTED_K3 ^ length;
    uint32_t i = 0;
    while (i + 8 <= length)
    {
        uint64_t word;
        memcpy(&word, bytes + i, 8);
        hash = check_attested_mum(hash ^ word, BUSTER_ATTESTED_K1);
        i += 8;
    }
    uint64_t tail = 0;
    memcpy(&tail, bytes + i, length - i);
    return check_attested_mum(hash ^ tail, BUSTER_ATTESTED_K2);
}

static bool check_attested_equal(CheckAttestedHash a, CheckAttestedHash b)
{
    return a.low == b.low && a.high == b.high;
}

// Whether the memo holds `hash`.
static bool check_attested_memo_has(const CheckAttested* census, CheckAttestedHash hash)
{
    const uint64_t mask = census->memo_mask;
    uint64_t slot = hash.low & mask;
    bool found = false;
    bool probing = true;
    while (probing)
    {
        const CheckAttestedHash entry = census->memo[slot];
        found = check_attested_equal(entry, hash);
        probing = !found && entry.low != 0;
        slot = (slot + 1) & mask;
    }
    return found;
}

// `hash` into a table of `mask + 1` slots that has room for it.
static void check_attested_memo_put(CheckAttestedHash* table, uint64_t mask, CheckAttestedHash hash, uint64_t* count)
{
    uint64_t slot = hash.low & mask;
    bool done = false;
    while (!done)
    {
        const CheckAttestedHash entry = table[slot];
        done = entry.low == 0 || check_attested_equal(entry, hash);
        if (entry.low == 0)
        {
            table[slot] = hash;
            *count += 1;
        }
        slot = (slot + 1) & mask;
    }
}

// The memo with `hash`, doubled first where it is three quarters full: whether it holds it (false where it would grow
// past its most slots, or the arena has no room).
static bool check_attested_memo_add(CheckAttested* census, CheckAttestedHash hash)
{
    bool room = census->memo_count < (census->memo_mask + 1) / 4 * 3;
    if (!room && census->memo_mask + 1 < BUSTER_ATTESTED_MEMO_SLOTS)
    {
        const uint64_t mask = census->memo_mask * 2 + 1;
        CheckAttestedHash* table = arena_push_array(&census->memo_arena, mask + 1, sizeof(CheckAttestedHash), 64);
        uint64_t count = 0;
        for (uint64_t slot = 0; table != NULL && slot <= census->memo_mask; slot += 1)
        {
            if (census->memo[slot].low != 0)
            {
                check_attested_memo_put(table, mask, census->memo[slot], &count);
            }
        }
        room = table != NULL;
        census->memo = table != NULL ? table : census->memo;
        census->memo_mask = table != NULL ? mask : census->memo_mask;
    }
    if (room)
    {
        check_attested_memo_put(census->memo, census->memo_mask, hash, &census->memo_count);
    }
    return room;
}

// ---------------------------------------------------------------------------------------------------------------------
// The names' facts, made once a name for the lane's table: whether it is a `__builtin_` name or a runtime function's.

// The runtime functions the lowering looks up in every unit with a body (check_lower_prepare's first segment, its own:
// memcpy, memset, the libgcc helpers, the libm functions of the elementwise builtins). The library's functions after them
// it looks up only for a builtin a body calls, which spells its name.
static uint32_t check_attested_runtime_count(void)
{
    return CHECK_LOWER_RUNTIME_LIBRARY;
}

// Facts for the names the table gained since the last unit, and the runtime functions' keys among them.
static void check_attested_ids(CheckAttested* census, const InternTable* table)
{
    const uint32_t count = table->count < BUSTER_ATTESTED_MAX_IDS ? table->count : BUSTER_ATTESTED_MAX_IDS;
    const uint32_t runtimes = check_attested_runtime_count();
    for (uint32_t id = census->ids_known > 1 ? census->ids_known : 1; id < count; id += 1)
    {
        const uint8_t* bytes = intern_bytes(table, id);
        const uint32_t length = intern_length(table, id);
        uint8_t facts = BUSTER_ATTESTED_ID_KNOWN;
        facts |= length > 10 && memcmp(bytes, "__builtin_", 10) == 0 ? BUSTER_ATTESTED_ID_BUILTIN : 0;
        bool runtime = false;
        for (uint32_t r = 0; !runtime && r < runtimes; r += 1)
        {
            runtime = census->runtime_lengths[r] == length && memcmp(check_lower_runtime_names[r], bytes, length) == 0;
        }
        if (runtime && census->runtime_count < runtimes)
        {
            census->runtime_keys[census->runtime_count] = id * 2;
            census->runtime_count += 1;
            facts |= BUSTER_ATTESTED_ID_RUNTIME;
        }
        census->id_facts[id] = facts;
    }
    census->ids_known = count > census->ids_known ? count : census->ids_known;
}

// The key of the library function behind `__builtin_X` name `id` (X's, ordinary), or BUSTER_ATTESTED_NONE where the
// table holds no X.
static uint32_t check_attested_builtin_target(CheckAttested* census, const InternTable* table, uint32_t id)
{
    uint32_t target = census->builtin_targets[id];
    if (target == 0)
    {
        target = intern_find(table, intern_bytes(table, id) + 10, intern_length(table, id) - 10);
        census->builtin_targets[id] = target;
    }
    return target != 0 ? target * 2 : BUSTER_ATTESTED_NONE;
}

// ---------------------------------------------------------------------------------------------------------------------
// The lists: the declarations' ranges and each identifier's class.

// Whether `role` opens a group, or closes one.
static bool check_attested_opens(uint8_t role)
{
    return role >= PARSE_ROLE_OPEN_GROUP && role <= PARSE_ROLE_OPEN_CASE;
}

static bool check_attested_closes(uint8_t role)
{
    return role >= PARSE_ROLE_CLOSE_GROUP && role <= PARSE_ROLE_CLOSE_CASE;
}

// Whether opener `role` is an asm group's or an attribute's, whose strings name symbols (asm labels, alias targets).
static bool check_attested_symbol_group(uint8_t role)
{
    return role == PARSE_ROLE_OPEN_ASM || (role >= PARSE_ROLE_OPEN_ATTRIBUTE && role <= PARSE_ROLE_OPEN_ATTRIBUTE_LIST);
}

// A level's flags in the lists' stack: an `=` or a bit-field's `:` read since its last comma (what follows is an
// expression), and an enumerator's name next (an enum's body, after its `{` or a comma).
#define BUSTER_ATTESTED_LEVEL_EXPRESSION 0x01u
#define BUSTER_ATTESTED_LEVEL_ENUMERATOR 0x02u

// A level's base class, the class of a name (no tag's, specifier's or member's) at it outside an expression: a
// declarator's at the declaration's own level; a parenthesized declarator's group's, its parent's at its opening; a
// parameter list's and a struct body's none (a name of their own scope's); an enum body's, its enumerators' (the first
// name of each declares it, the rest look up); and any other group's (an array's bound, an initializer, an attribute's
// operands, typeof's, a static assertion's), a lookup.
#define BUSTER_ATTESTED_BASE_ENUM 0x80u

// The tokens' parts in the lists' pass, from their kinds and roles (CheckAttested.kind_actions and role_actions): an
// identifier, a string, an asm keyword, a comma, a `;`; an opener, a closer, an `=` or a bit-field's `:`, an asm or
// attribute group's opener. A token with none takes no step but the root's test.
#define BUSTER_ATTESTED_ACT_IDENTIFIER 0x01u
#define BUSTER_ATTESTED_ACT_STRING 0x02u
#define BUSTER_ATTESTED_ACT_ASM 0x04u
#define BUSTER_ATTESTED_ACT_COMMA 0x08u
#define BUSTER_ATTESTED_ACT_SEMI 0x10u
#define BUSTER_ATTESTED_ACT_OPEN 0x20u
#define BUSTER_ATTESTED_ACT_CLOSE 0x40u
#define BUSTER_ATTESTED_ACT_EXPRESSION 0x80u

// The kinds' and roles' parts, made once a census.
static void check_attested_actions(uint8_t* kind_actions, uint8_t* role_actions)
{
    memset(kind_actions, 0, 256);
    memset(role_actions, 0, 256);
    kind_actions[PARSE_KIND_IDENTIFIER] = BUSTER_ATTESTED_ACT_IDENTIFIER;
    kind_actions[PARSE_KIND_STRING] = BUSTER_ATTESTED_ACT_STRING;
    kind_actions[PARSE_KIND_ASM] = BUSTER_ATTESTED_ACT_ASM;
    kind_actions[PARSE_KIND_COMMA] = BUSTER_ATTESTED_ACT_COMMA;
    kind_actions[PARSE_KIND_SEMI] = BUSTER_ATTESTED_ACT_SEMI;
    for (uint32_t role = 0; role < PARSE_ROLE_COUNT; role += 1)
    {
        role_actions[role] = check_attested_opens((uint8_t)role) ? BUSTER_ATTESTED_ACT_OPEN :
                             check_attested_closes((uint8_t)role) ? BUSTER_ATTESTED_ACT_CLOSE :
                             role == PARSE_ROLE_ASSIGN || role == PARSE_ROLE_BITFIELD ? BUSTER_ATTESTED_ACT_EXPRESSION : 0;
    }
}

// The class of a name at a level of base `base` and flags `flags`.
static uint8_t check_attested_level_class(uint8_t base, uint8_t flags)
{
    const uint8_t enumerator = (flags & BUSTER_ATTESTED_LEVEL_ENUMERATOR) != 0 ? BUSTER_ATTESTED_DECLARES : BUSTER_ATTESTED_LOOKS_UP;
    return (flags & BUSTER_ATTESTED_LEVEL_EXPRESSION) != 0 ? BUSTER_ATTESTED_LOOKS_UP : base == BUSTER_ATTESTED_BASE_ENUM ? enumerator : base;
}

// The base class of a level opened by `role` inside a level whose names have class `outer`.
static uint8_t check_attested_opened_base(uint8_t role, uint8_t outer)
{
    return role == PARSE_ROLE_OPEN_GROUP ? outer :
           role == PARSE_ROLE_OPEN_FUNCTION || role == PARSE_ROLE_OPEN_STRUCT ? 0 :
           role == PARSE_ROLE_OPEN_ENUM ? BUSTER_ATTESTED_BASE_ENUM : BUSTER_ATTESTED_LOOKS_UP;
}

// The lists' state as one pass in source order goes: the open groups' stack (each level's opener, flags and base
// class), and where the unit's arrays are.
typedef struct CheckAttestedListing CheckAttestedListing;
struct CheckAttestedListing
{
    const uint8_t* kinds;
    const uint8_t* roles;
    const uint32_t* names;
    const uint32_t* parents;
    // A function definition's body's closing, set at its opening: the lists' callers hand its inside to
    // check_attested_list_body and go on at it; BUSTER_ATTESTED_NONE outside one.
    uint32_t body_end;
    uint32_t* firsts;
    uint32_t* lasts;
    uint8_t* flags;
    uint32_t* name_starts;
    uint32_t* name_entries;
    uint32_t* string_tokens;
    uint32_t* string_declarations;
    uint8_t* stack_roles;
    uint8_t* stack_flags;
    uint8_t* stack_bases;
    uint32_t declaration;
    uint32_t names_count;
    uint32_t strings;
    uint32_t depth;
    // The asm and attribute groups open around the token, whose strings name symbols.
    uint32_t symbol_groups;
    uint32_t first;
    uint8_t declaration_flags;
    bool any_definition;
};

// Token `t`, of role `role` and parts `action` (BUSTER_ATTESTED_ACT_*), into the lists, and the declaration it ends where
// it is a root (`root`). Both paths call it for every token with a part or a root, the scalar one for every token.
static inline __attribute__((always_inline)) void check_attested_list_token(CheckAttestedListing* listing, uint32_t t, uint8_t role, uint8_t action, bool root)
{
    uint8_t* const stack_flags = listing->stack_flags;
    uint8_t* const stack_bases = listing->stack_bases;
    uint32_t depth = listing->depth;
    if (action != 0)
    {
        const uint8_t level_flags = stack_flags[depth];
        if ((action & BUSTER_ATTESTED_ACT_IDENTIFIER) != 0)
        {
            const uint32_t id = listing->names[t] < BUSTER_ATTESTED_MAX_IDS ? listing->names[t] : 0;
            const bool tag = role == PARSE_ROLE_TAG || role == PARSE_ROLE_LEAF_RIGHT;
            const uint8_t operand = check_attested_level_class(stack_bases[depth], level_flags);
            const uint8_t name_class = tag ? BUSTER_ATTESTED_DECLARES | BUSTER_ATTESTED_LOOKS_UP | BUSTER_ATTESTED_TAG :
                                       role == PARSE_ROLE_SPECIFIER ? BUSTER_ATTESTED_LOOKS_UP :
                                       role == PARSE_ROLE_MEMBER ? 0 : operand;
            stack_flags[depth] = level_flags & ~BUSTER_ATTESTED_LEVEL_ENUMERATOR;
            listing->name_entries[listing->names_count] = (id * 2 + (tag ? 1u : 0u)) | (uint32_t)name_class << BUSTER_ATTESTED_CLASS_SHIFT;
            listing->names_count += 1;
        }
        else if ((action & BUSTER_ATTESTED_ACT_STRING) != 0 && listing->symbol_groups != 0)
        {
            listing->string_tokens[listing->strings] = t;
            listing->string_declarations[listing->strings] = listing->declaration;
            listing->strings += 1;
            listing->declaration_flags |= BUSTER_ATTESTED_STRINGS;
        }
        else if ((action & BUSTER_ATTESTED_ACT_ASM) != 0 && depth == 0 &&
                 (t == listing->first || (t == listing->first + 1 && listing->kinds[listing->first] == PARSE_KIND_EXTENSION)))
        {
            listing->declaration_flags |= BUSTER_ATTESTED_ASM;
        }
        else if ((action & BUSTER_ATTESTED_ACT_COMMA) != 0)
        {
            stack_flags[depth] = stack_bases[depth] == BUSTER_ATTESTED_BASE_ENUM ? BUSTER_ATTESTED_LEVEL_ENUMERATOR : 0;
        }
        else if ((action & BUSTER_ATTESTED_ACT_SEMI) != 0)
        {
            // A struct body's member declaration ends: its bit-field's width with it.
            stack_flags[depth] = 0;
        }
        else if ((action & BUSTER_ATTESTED_ACT_EXPRESSION) != 0)
        {
            stack_flags[depth] = level_flags | BUSTER_ATTESTED_LEVEL_EXPRESSION;
        }
        if ((action & BUSTER_ATTESTED_ACT_OPEN) != 0)
        {
            const uint8_t base = check_attested_opened_base(role, check_attested_level_class(stack_bases[depth], stack_flags[depth]));
            depth += 1;
            listing->stack_roles[depth] = role;
            stack_bases[depth] = base;
            stack_flags[depth] = base == BUSTER_ATTESTED_BASE_ENUM ? BUSTER_ATTESTED_LEVEL_ENUMERATOR : 0;
            listing->symbol_groups += check_attested_symbol_group(role) ? 1u : 0u;
            listing->declaration_flags |= role == PARSE_ROLE_OPEN_BODY ? BUSTER_ATTESTED_DEFINITION : 0;
            // A function definition's body: its inside is check_attested_list_body's, up to its closing.
            listing->body_end = role == PARSE_ROLE_OPEN_BODY && depth == 1 ? listing->parents[t] : BUSTER_ATTESTED_NONE;
        }
        else if ((action & BUSTER_ATTESTED_ACT_CLOSE) != 0 && depth != 0)
        {
            listing->symbol_groups -= check_attested_symbol_group(listing->stack_roles[depth]) && listing->symbol_groups != 0 ? 1u : 0u;
            depth -= 1;
        }
    }
    if (root)
    {
        const uint32_t declaration = listing->declaration;
        const bool definition = (listing->declaration_flags & BUSTER_ATTESTED_DEFINITION) != 0 || role == PARSE_ROLE_CLOSE_BODY;
        listing->firsts[declaration] = listing->first;
        listing->lasts[declaration] = t;
        listing->flags[declaration] = listing->declaration_flags | (definition ? BUSTER_ATTESTED_DEFINITION : 0);
        listing->any_definition |= definition;
        listing->declaration = declaration + 1;
        listing->name_starts[declaration + 1] = listing->names_count;
        listing->declaration_flags = 0;
        listing->first = t + 1;
        depth = 0;
        listing->symbol_groups = 0;
        stack_flags[0] = 0;
    }
    listing->depth = depth;
}

// A function definition's body's inside, tokens [begin, end) of declaration `listing->declaration`: each identifier
// looks its name up, and declares it at file scope where the body can: a callee (an implicit function's declaration) and,
// in a body with an `extern` declaration, every name (a block's extern declaration's, without telling which). Any other
// name a body names has an earlier declaration, which the reach's earlier edge reaches and whose later edge reaches the
// later ones; the binding events check the lists of definitions as of every declaration. A body with an asm statement
// gives its every string as one that may name symbols. No stack: a body's inside makes no file-scope structure. The
// vector path 64 tokens at a time (the identifiers' entries compressed into the list, a callee by its next token's
// role); the scalar path, its twin, a token at a time.
static void check_attested_list_body(CheckAttestedListing* listing, uint32_t begin, uint32_t end, bool scalar)
{
    const uint8_t* const kinds = listing->kinds;
    const uint8_t* const roles = listing->roles;
    const uint32_t* const names = listing->names;
    bool externs = false;
    bool asms = false;
    uint32_t u = begin;
    for (; !BUSTER_SCALAR_PATH(scalar) && u < end; u += 64)
    {
        const __mmask64 lanes = end - u < 64 ? _bzhi_u64(UINT64_MAX, end - u) : UINT64_MAX;
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + u);
        asms |= _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_ASM)) != 0;
        uint64_t storage = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_STORAGE));
        while (storage != 0)
        {
            externs |= names[u + _tzcnt_u64(storage)] == C_NAME_EXTERN;
            storage &= storage - 1;
        }
    }
    for (; u < end; u += 1)
    {
        asms |= kinds[u] == PARSE_KIND_ASM;
        externs |= kinds[u] == PARSE_KIND_STORAGE && names[u] == C_NAME_EXTERN;
    }
    const uint32_t declares = externs ? BUSTER_ATTESTED_DECLARES << BUSTER_ATTESTED_CLASS_SHIFT : 0;
    uint32_t count = listing->names_count;
    uint32_t strings = listing->strings;
    u = begin;
    for (; !BUSTER_SCALAR_PATH(scalar) && u < end; u += 64)
    {
        const __mmask64 lanes = end - u < 64 ? _bzhi_u64(UINT64_MAX, end - u) : UINT64_MAX;
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + u);
        const __m512i role = _mm512_maskz_loadu_epi8(lanes, roles + u);
        // The next tokens' roles (the body's closing is the last one read).
        const __m512i next = _mm512_maskz_loadu_epi8(lanes, roles + u + 1);
        const uint64_t identifiers = _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_IDENTIFIER));
        const uint64_t tags = identifiers & (_mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_TAG)) | _mm512_cmpeq_epi8_mask(role, _mm512_set1_epi8(PARSE_ROLE_LEAF_RIGHT)));
        const uint64_t callees = identifiers & (_mm512_cmpeq_epi8_mask(next, _mm512_set1_epi8(PARSE_ROLE_OPEN_CALL)) | _mm512_cmpeq_epi8_mask(next, _mm512_set1_epi8(PARSE_ROLE_OPEN_FUNCTION)));
        for (uint32_t g = 0; g < 4; g += 1)
        {
            const __mmask16 group = (__mmask16)(identifiers >> (16 * g));
            const __mmask16 tagged = (__mmask16)(tags >> (16 * g));
            const __mmask16 called = (__mmask16)(callees >> (16 * g));
            const __m512i ids = _mm512_maskz_loadu_epi32(group, names + u + 16 * g);
            __m512i entries = _mm512_add_epi32(_mm512_slli_epi32(ids, 1), _mm512_maskz_set1_epi32(tagged, 1));
            entries = _mm512_or_si512(entries, _mm512_set1_epi32((int32_t)(BUSTER_ATTESTED_LOOKS_UP << BUSTER_ATTESTED_CLASS_SHIFT | declares)));
            entries = _mm512_or_si512(entries, _mm512_maskz_set1_epi32(tagged, (int32_t)(BUSTER_ATTESTED_TAG << BUSTER_ATTESTED_CLASS_SHIFT)));
            entries = _mm512_or_si512(entries, _mm512_maskz_set1_epi32(called, (int32_t)(BUSTER_ATTESTED_DECLARES << BUSTER_ATTESTED_CLASS_SHIFT)));
            _mm512_mask_compressstoreu_epi32(listing->name_entries + count, group, entries);
            count += (uint32_t)__builtin_popcount(group);
        }
        uint64_t texts = asms ? _mm512_mask_cmpeq_epi8_mask(lanes, kind, _mm512_set1_epi8(PARSE_KIND_STRING)) : 0;
        while (texts != 0)
        {
            listing->string_tokens[strings] = u + (uint32_t)_tzcnt_u64(texts);
            listing->string_declarations[strings] = listing->declaration;
            strings += 1;
            texts &= texts - 1;
        }
    }
    for (; u < end; u += 1)
    {
        const bool identifier = kinds[u] == PARSE_KIND_IDENTIFIER;
        const bool tag = roles[u] == PARSE_ROLE_TAG || roles[u] == PARSE_ROLE_LEAF_RIGHT;
        const bool callee = roles[u + 1] == PARSE_ROLE_OPEN_CALL || roles[u + 1] == PARSE_ROLE_OPEN_FUNCTION;
        listing->name_entries[count] = (names[u] * 2 + (tag ? 1u : 0u)) | BUSTER_ATTESTED_LOOKS_UP << BUSTER_ATTESTED_CLASS_SHIFT | declares |
                                       (tag ? BUSTER_ATTESTED_TAG << BUSTER_ATTESTED_CLASS_SHIFT : 0) |
                                       (callee ? BUSTER_ATTESTED_DECLARES << BUSTER_ATTESTED_CLASS_SHIFT : 0);
        count += identifier ? 1u : 0u;
        if (asms && kinds[u] == PARSE_KIND_STRING)
        {
            listing->string_tokens[strings] = u;
            listing->string_declarations[strings] = listing->declaration;
            strings += 1;
        }
    }
    listing->declaration_flags |= strings != listing->strings ? BUSTER_ATTESTED_STRINGS : 0;
    listing->names_count = count;
    listing->strings = strings;
}

// The lists of `parser`'s unit into the census's unit arrays; whether there was room. One pass in source order, with a
// stack of the open groups: on the vector path 64 tokens at a time, their parts from the tables by vpermb and the
// parser's roots by compares, stepping through the tokens that have either; on the scalar path, its twin, every token.
static __attribute__((noinline)) bool check_attested_lists(CheckAttested* census, const Parser* parser)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint8_t* const roles = parser->tree.roles;
    const uint32_t* const parents = parser->tree.parents;
    const uint8_t* const kind_actions = census->kind_actions;
    const uint8_t* const role_actions = census->role_actions;
    const uint64_t mark = census->unit_arena.length;
    CheckAttestedListing listing = {
        .kinds = kinds,
        .roles = roles,
        .names = parser->tokens.names,
        .parents = parents,
        .body_end = BUSTER_ATTESTED_NONE,
        .firsts = census->firsts,
        .lasts = census->lasts,
        .flags = census->flags,
        .name_starts = census->name_starts,
        .name_entries = census->name_entries,
        .string_tokens = census->string_tokens,
        .string_declarations = census->string_declarations,
        .stack_roles = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .stack_flags = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .stack_bases = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
    };
    const bool valid = listing.stack_roles != NULL && listing.stack_flags != NULL && listing.stack_bases != NULL;
    if (valid)
    {
        listing.stack_roles[0] = PARSE_ROLE_NONE;
        listing.stack_flags[0] = 0;
        listing.stack_bases[0] = BUSTER_ATTESTED_DECLARES;
        census->name_starts[0] = 0;
    }
    const __m512i kind_low = _mm512_loadu_si512(kind_actions);
    const __m512i kind_high = _mm512_loadu_si512(kind_actions + 64);
    const __m512i role_low = _mm512_loadu_si512(role_actions);
    const __m512i role_high = _mm512_loadu_si512(role_actions + 64);
    const __m512i none = _mm512_set1_epi32(-1);
    const bool scalar = parser->options.scalar;
    uint32_t t = 0;
    while (valid && !BUSTER_SCALAR_PATH(scalar) && t < count)
    {
        const uint32_t length = count - t < 64 ? count - t : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        // Kinds and roles are below 128 (_Static_assert in c_parser.c): a 128-byte table each.
        const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + t);
        const __m512i role = _mm512_maskz_loadu_epi8(lanes, roles + t);
        const __m512i actions = _mm512_or_si512(_mm512_permutex2var_epi8(kind_low, kind, kind_high), _mm512_permutex2var_epi8(role_low, role, role_high));
        uint64_t root_bits = 0;
        for (uint32_t g = 0; g < 4; g += 1)
        {
            const __mmask16 group = (__mmask16)(lanes >> (16 * g));
            root_bits |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(group, _mm512_maskz_loadu_epi32(group, parents + t + 16 * g), none) << (16 * g);
        }
        uint8_t part[64];
        uint8_t role_bytes[64];
        _mm512_storeu_si512(part, actions);
        _mm512_storeu_si512(role_bytes, role);
        uint64_t steps = (_mm512_test_epi8_mask(actions, actions) & lanes) | root_bits;
        uint32_t next = t + length;
        while (steps != 0)
        {
            const uint32_t k = (uint32_t)_tzcnt_u64(steps);
            check_attested_list_token(&listing, t + k, role_bytes[k], part[k], (root_bits >> k & 1) != 0);
            steps &= steps - 1;
            if (listing.body_end != BUSTER_ATTESTED_NONE)
            {
                // A body: its inside, then the lists go on at its closing.
                check_attested_list_body(&listing, t + k + 1, listing.body_end, scalar);
                next = listing.body_end;
                listing.body_end = BUSTER_ATTESTED_NONE;
                steps = 0;
            }
        }
        t = next;
    }
    while (valid && t < count)
    {
        check_attested_list_token(&listing, t, roles[t], kind_actions[kinds[t]] | role_actions[roles[t]], parents[t] == BUSTER_PARSE_ROOT);
        t += 1;
        if (listing.body_end != BUSTER_ATTESTED_NONE)
        {
            check_attested_list_body(&listing, t, listing.body_end, scalar);
            t = listing.body_end;
            listing.body_end = BUSTER_ATTESTED_NONE;
        }
    }
    uint32_t declaration = listing.declaration;
    if (valid && listing.first < count)
    {
        // Tokens after the last declaration's root, which a valid parse doesn't leave: one declaration more.
        census->firsts[declaration] = listing.first;
        census->lasts[declaration] = count - 1;
        census->flags[declaration] = listing.declaration_flags;
        declaration += 1;
        census->name_starts[declaration] = listing.names_count;
    }
    // The own hashes' sentinel: no declaration ends past the last.
    census->lasts[declaration] = UINT32_MAX;
    census->declaration_count = declaration;
    census->name_count = listing.names_count;
    census->string_count = listing.strings;
    census->any_definition = listing.any_definition;
    arena_pop_to(&census->unit_arena, mark);
    return valid;
}

// The pragmas' places, hashes and spelled names. A pragma whose parser position is a declaration's first token (or the
// unit's end) is between declarations; one inside a declaration marks it.
static __attribute__((noinline)) bool check_attested_pragmas(CheckAttested* census, const Parser* parser, Preprocessor* pp)
{
    const uint32_t pragmas = parser->tokens.pragma_count;
    const PPTokens* output = &pp->output;
    uint32_t declaration = 0;
    uint32_t keys = 0;
    bool valid = true;
    census->pragma_name_starts[0] = 0;
    for (uint32_t p = 0; valid && p < pragmas; p += 1)
    {
        const uint32_t position = parser->tokens.pragma_positions[p];
        while (declaration < census->declaration_count && census->lasts[declaration] < position)
        {
            declaration += 1;
        }
        const bool inside = declaration < census->declaration_count && census->firsts[declaration] < position;
        census->pragma_declarations[p] = declaration;
        census->pragma_inside[p] = inside;
        if (inside)
        {
            census->flags[declaration] |= BUSTER_ATTESTED_PRAGMA;
        }
        uint32_t end = 0;
        const uint32_t hash_token = parser->tokens.pragma_sources[p];
        const bool complete = parse_pragma_at(output, hash_token, &end);
        CheckAttestedHash hash = { .low = BUSTER_ATTESTED_K2, .high = BUSTER_ATTESTED_K3 };
        for (uint32_t i = hash_token + 1; complete && i < end; i += 1)
        {
            const uint8_t kind = output->kinds[i];
            const bool named = kind == C_TOKEN_RAW_IDENTIFIER || (kind >= BUSTER_PP_KIND_KEYWORD && kind < BUSTER_PP_KIND_PRAGMA_END);
            uint64_t word = (uint64_t)kind << 32;
            if (named || kind == BUSTER_PP_KIND_INTEGER)
            {
                word |= output->values[i];
            }
            else
            {
                const uint64_t capacity = (uint64_t)output->values[i] - output->locations[i] + 1;
                const uint8_t* spelling = census->spelling;
                const uint32_t length = capacity <= census->spelling_capacity ? pp_spelling(pp, kind, output->values[i], output->locations[i], census->spelling, &spelling) : 0;
                word ^= check_attested_bytes(spelling, length);
            }
            hash = check_attested_step(hash, word);
            if (kind == C_TOKEN_RAW_IDENTIFIER && output->values[i] < BUSTER_ATTESTED_MAX_IDS && keys < parser->tokens.count)
            {
                census->pragma_keys[keys] = output->values[i] * 2;
                keys += 1;
            }
        }
        census->pragma_hashes[p] = hash;
        census->pragma_name_starts[p + 1] = keys;
        valid = complete;
    }
    census->pragma_count = valid ? pragmas : 0;
    return valid;
}

// ---------------------------------------------------------------------------------------------------------------------
// The declarations' own hashes: each token's kind, role, system level and name (or literal: an integer's value, any
// other's spelling), at its place in the declaration.

// The ranges the last literals' spellings were in (a unit's literals are mostly in a few: the file's own text and the
// macros' definitions it expands), each searched for again only where none holds a literal's location.
#define BUSTER_ATTESTED_RANGES 4u
typedef struct CheckAttestedRanges CheckAttestedRanges;
struct CheckAttestedRanges
{
    uint32_t bases[BUSTER_ATTESTED_RANGES];
    uint32_t lengths[BUSTER_ATTESTED_RANGES];
    const uint8_t* bytes[BUSTER_ATTESTED_RANGES];
    uint32_t next;
};

// The spelling of the literal at pp output token `source`, as pp_spelling gives it, into `*spelling`: its bytes found
// through the ranges that held the last literals (`ranges`), since a unit's literals are mostly in a few. Its length,
// or UINT32_MAX where it doesn't fit the census's buffer.
static uint32_t check_attested_literal(CheckAttested* census, Preprocessor* pp, uint32_t source, CheckAttestedRanges* ranges, const uint8_t** spelling)
{
    const PPTokens* output = &pp->output;
    const uint8_t kind = output->kinds[source];
    const uint32_t location = output->locations[source];
    const uint32_t end = output->values[source];
    const bool fits = end >= location && (uint64_t)end - location + 1 <= census->spelling_capacity;
    uint32_t result = UINT32_MAX;
    if (fits && (kind == C_TOKEN_UNKNOWN || kind == BUSTER_PP_KIND_EMBED))
    {
        result = pp_spelling(pp, kind, end, location, census->spelling, spelling);
    }
    else if (fits)
    {
        uint32_t held = BUSTER_ATTESTED_RANGES;
        for (uint32_t r = 0; r < BUSTER_ATTESTED_RANGES; r += 1)
        {
            held = location - ranges->bases[r] < ranges->lengths[r] ? r : held;
        }
        if (held == BUSTER_ATTESTED_RANGES)
        {
            census->range_searches += 1;
            const uint32_t found = pp_find_range(pp, location);
            if (found != BUSTER_PP_NONE)
            {
                held = ranges->next;
                ranges->bases[held] = pp->ranges[found].base;
                ranges->lengths[held] = pp->ranges[found].length;
                ranges->bytes[held] = pp->ranges[found].bytes;
                ranges->next = (held + 1) % BUSTER_ATTESTED_RANGES;
            }
        }
        result = held != BUSTER_ATTESTED_RANGES ?
                 pp_without_splices(pp, kind, ranges->bytes[held] + (location - ranges->bases[held]), end - location, census->spelling, spelling) :
                 pp_spelling(pp, kind, end, location, census->spelling, spelling);
    }
    return result;
}

// A literal's word at its place `place` in its declaration (its own constants, apart from the tokens' words), into
// `*hash`: an integer's value, or any other's spelling's hash.
static void check_attested_literal_word(CheckAttested* census, Preprocessor* pp, uint32_t source, uint32_t place, CheckAttestedRanges* ranges, CheckAttestedHash* hash)
{
    const PPTokens* output = &pp->output;
    uint64_t word = (uint64_t)1 << 63;
    if (output->kinds[source] == BUSTER_PP_KIND_INTEGER)
    {
        word |= output->values[source];
    }
    else
    {
        const uint8_t* spelling = census->spelling;
        const uint32_t length = check_attested_literal(census, pp, source, ranges, &spelling);
        word ^= length != UINT32_MAX ? check_attested_bytes(spelling, length) : (uint64_t)output->locations[source] << 24;
    }
    census->spelled_literals += output->kinds[source] != BUSTER_PP_KIND_INTEGER ? 1 : 0;
    const uint64_t placed = word ^ (uint64_t)place * BUSTER_ATTESTED_K1;
    const unsigned __int128 product = (unsigned __int128)(placed ^ BUSTER_ATTESTED_K2) * BUSTER_ATTESTED_K3;
    hash->low += (uint64_t)product ^ (uint64_t)(product >> 64);
    hash->high += (uint64_t)(product >> 64) + placed;
}

// The declarations' own hashes: a sum over each one's tokens of a product of the token's word at its place, then its
// literals' spellings' (the sum's order is none, so they come after), then its length mixed in. One pass over every
// token, its declaration's sum kept where it ends (no branch a token, none a declaration); the literals stepped
// through by their kinds' mask 64 tokens at a time, or every token on the scalar path, its twin.
static __attribute__((noinline)) void check_attested_hashes(CheckAttested* census, const Parser* parser)
{
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint8_t* const roles = parser->tree.roles;
    const uint32_t* const names = parser->tokens.names;
    const uint8_t* const levels = census->token_levels;
    const uint32_t* const parents = parser->tree.parents;
    const uint32_t* const firsts = census->firsts;
    const uint32_t* const lasts = census->lasts;
    CheckAttestedHash* const owns = census->owns;
    const uint32_t declarations = census->declaration_count;
    uint32_t d = 0;
    // The token's place in its declaration times BUSTER_ATTESTED_K3, kept as a running sum.
    uint64_t place = 0;
    uint64_t low = BUSTER_ATTESTED_K0;
    uint64_t high = BUSTER_ATTESTED_K1;
    const uint32_t end = declarations != 0 ? lasts[declarations - 1] + 1 : 0;
    for (uint32_t t = declarations != 0 ? firsts[0] : 0; t < end; t += 1)
    {
        const uint64_t word = (uint64_t)names[t] << 32 | (uint64_t)levels[t] << 16 | (uint64_t)roles[t] << 8 | kinds[t];
        const uint64_t placed = word ^ place;
        const unsigned __int128 product = (unsigned __int128)(placed ^ BUSTER_ATTESTED_K0) * BUSTER_ATTESTED_K1;
        low += (uint64_t)product ^ (uint64_t)(product >> 64);
        high += (uint64_t)(product >> 64) + placed;
        // The sum so far, kept as the declaration's own until its last token's is: then the next declaration's. A
        // declaration's last token is a root of the parser's tree (the lists' ranges), which no step here waits for.
        owns[d] = (CheckAttestedHash){ .low = low, .high = high };
        const bool ends = parents[t] == BUSTER_PARSE_ROOT;
        d += ends ? 1u : 0u;
        place = ends ? 0 : place + BUSTER_ATTESTED_K3;
        low = ends ? BUSTER_ATTESTED_K0 : low;
        high = ends ? BUSTER_ATTESTED_K1 : high;
    }
}

// The own hashes' literals: each one's word added to its declaration's sum (a cursor that only rises, as the literals
// do), then every declaration's sum finished with its length.
static __attribute__((noinline)) void check_attested_literals(CheckAttested* census, const Parser* parser, Preprocessor* pp)
{
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint32_t* const sources = parser->tokens.sources;
    const uint32_t* const firsts = census->firsts;
    const uint32_t* const lasts = census->lasts;
    CheckAttestedHash* const owns = census->owns;
    const uint32_t declarations = census->declaration_count;
    const uint32_t end = declarations != 0 ? lasts[declarations - 1] + 1 : 0;
    const __m512i number = _mm512_set1_epi8(PARSE_KIND_NUMBER);
    const __m512i character = _mm512_set1_epi8(PARSE_KIND_CHARACTER);
    const __m512i string = _mm512_set1_epi8(PARSE_KIND_STRING);
    // No range held yet: each length 0, which no location's offset is below.
    CheckAttestedRanges ranges = { 0 };
    uint32_t at = 0;
    for (uint32_t t = 0; t < end; t += 64)
    {
        const uint32_t length = end - t < 64 ? end - t : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        uint64_t literals = 0;
        if (!BUSTER_SCALAR_PATH(parser->options.scalar))
        {
            const __m512i kind = _mm512_maskz_loadu_epi8(lanes, kinds + t);
            literals = (_mm512_cmpeq_epi8_mask(kind, number) | _mm512_cmpeq_epi8_mask(kind, character) | _mm512_cmpeq_epi8_mask(kind, string)) & lanes;
        }
        else
        {
            for (uint32_t k = 0; k < length; k += 1)
            {
                const uint8_t kind = kinds[t + k];
                literals |= (uint64_t)(kind == PARSE_KIND_NUMBER || kind == PARSE_KIND_CHARACTER || kind == PARSE_KIND_STRING) << k;
            }
        }
        while (literals != 0)
        {
            const uint32_t token = t + (uint32_t)_tzcnt_u64(literals);
            while (lasts[at] < token)
            {
                at += 1;
            }
            check_attested_literal_word(census, pp, sources[token], token - firsts[at], &ranges, owns + at);
            census->literal_count += 1;
            literals &= literals - 1;
        }
    }
    for (uint32_t k = 0; k < declarations; k += 1)
    {
        owns[k] = check_attested_step(owns[k], lasts[k] - firsts[k]);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The chain: in source order, each declaration's semantic hash from its own, the ambient state (the pragmas before it)
// and the current hash of each name its list declares or looks up; the memo's probe; then its declared names' hashes.

// Name `key`'s current hash in the unit: 0 before a declaration of it.
static uint64_t check_attested_name_hash(const CheckAttested* census, uint32_t key)
{
    const CheckAttestedKey* state = census->keys + key;
    return state->stamp == census->stamp ? state->hash : 0;
}

// Name `key` declared by a declaration (or a pragma) of semantic hash `by`: its current hash made from the last one and
// `by`; its state the unit's from here, with no declarers' list yet where it had none.
static void check_attested_declare(CheckAttested* census, uint32_t key, CheckAttestedHash by)
{
    CheckAttestedKey* state = census->keys + key;
    const bool current = state->stamp == census->stamp;
    const uint64_t before = current ? state->hash : 0;
    state->hash = check_attested_mum(before ^ by.low ^ BUSTER_ATTESTED_K0, by.high ^ key ^ BUSTER_ATTESTED_K1) | 1;
    state->local = current ? state->local : BUSTER_ATTESTED_NONE;
    state->stamp = census->stamp;
}

// Declaration `d` among the declarers of `key`, the last of them yet: the reach's list, in source order.
static void check_attested_declarer(CheckAttested* census, uint32_t key, uint32_t d, uint32_t* entries, uint32_t* distinct)
{
    // (check_attested_declare has made the name's state the unit's.)
    const uint32_t entry = *entries;
    CheckAttestedKey* state = census->keys + key;
    census->entry_declarations[entry] = d;
    census->entry_next[entry] = BUSTER_ATTESTED_NONE;
    if (state->local == BUSTER_ATTESTED_NONE)
    {
        state->local = *distinct;
        census->lists[*distinct] = (CheckAttestedList){ .first = entry, .last = entry, .count = 0 };
        census->entry_previous[entry] = BUSTER_ATTESTED_NONE;
        *distinct += 1;
    }
    else
    {
        CheckAttestedList* list = census->lists + state->local;
        census->entry_previous[entry] = list->last;
        census->entry_next[list->last] = entry;
    }
    CheckAttestedList* list = census->lists + state->local;
    list->last = entry;
    list->count += 1;
    *entries = entry + 1;
}

static __attribute__((noinline)) void check_attested_chain(CheckAttested* census)
{
    const uint32_t declarations = census->declaration_count;
    const uint32_t* const name_starts = census->name_starts;
    const uint32_t* const name_entries = census->name_entries;
    CheckAttestedHash ambient = { .low = BUSTER_ATTESTED_K1, .high = BUSTER_ATTESTED_K0 };
    uint32_t pragma = 0;
    uint32_t entries = 0;
    uint32_t distinct = 0;
    for (uint32_t d = 0; d < declarations; d += 1)
    {
        // The pragmas before it (between declarations, or inside the one before): the ambient state and the names
        // they spell, which they count as declaring.
        while (pragma < census->pragma_count && (census->pragma_declarations[pragma] < d || (census->pragma_declarations[pragma] == d && !census->pragma_inside[pragma])))
        {
            const CheckAttestedHash hash = census->pragma_hashes[pragma];
            ambient = check_attested_step((CheckAttestedHash){ .low = ambient.low ^ hash.low, .high = ambient.high ^ hash.high }, pragma);
            for (uint32_t k = census->pragma_name_starts[pragma]; k < census->pragma_name_starts[pragma + 1]; k += 1)
            {
                check_attested_declare(census, census->pragma_keys[k], hash);
            }
            pragma += 1;
        }
        // Its own hash and the ambient state, then a sum over its listed names of each one's entry and current hash at its
        // place in the list (no chain from one name to the next), then the sum mixed in.
        const CheckAttestedHash own = census->owns[d];
        const uint32_t start = name_starts[d];
        const uint32_t end = name_starts[d + 1];
        uint64_t low = 0;
        uint64_t high = 0;
        for (uint32_t n = start; n < end; n += 1)
        {
            const uint32_t entry = name_entries[n];
            const bool listed = (entry >> BUSTER_ATTESTED_CLASS_SHIFT & (BUSTER_ATTESTED_DECLARES | BUSTER_ATTESTED_LOOKS_UP)) != 0;
            const uint64_t name = check_attested_name_hash(census, entry & BUSTER_ATTESTED_KEY_MASK);
            const uint64_t placed = (uint64_t)entry ^ (uint64_t)(n - start) << 32;
            low += listed ? check_attested_mum(name ^ placed ^ BUSTER_ATTESTED_K0, BUSTER_ATTESTED_K1) : 0;
            high += listed ? check_attested_mum(name ^ placed ^ BUSTER_ATTESTED_K2, BUSTER_ATTESTED_K3) : 0;
        }
        const CheckAttestedHash hash = check_attested_step((CheckAttestedHash){ .low = own.low ^ ambient.low ^ low, .high = own.high ^ ambient.high ^ high }, end - start);
        census->semantics[d] = hash;
        const bool eligible = (census->flags[d] & (BUSTER_ATTESTED_DEFINITION | BUSTER_ATTESTED_PRAGMA)) == 0;
        census->hits[d] = eligible && check_attested_memo_has(census, hash);
        for (uint32_t n = name_starts[d]; n < end; n += 1)
        {
            const uint32_t entry = name_entries[n];
            if ((entry >> BUSTER_ATTESTED_CLASS_SHIFT & BUSTER_ATTESTED_DECLARES) != 0)
            {
                check_attested_declare(census, entry & BUSTER_ATTESTED_KEY_MASK, hash);
                check_attested_declarer(census, entry & BUSTER_ATTESTED_KEY_MASK, d, &entries, &distinct);
            }
        }
    }
    census->distinct_count = distinct;
}

// ---------------------------------------------------------------------------------------------------------------------
// The reach: the declarations a root reaches, a root being a definition, a pragma, a declaration the memo doesn't attest
// and, with `runtime`, the unit's end spelling every runtime function's name. A reached declaration reaches every earlier
// declaration of a name it spells (a key's rising cursor) and every later declaration of a name it declares (its falling
// cursor): each cursor passes each of the key's declarers once.

static void check_attested_reach_one(CheckAttested* census, uint8_t* reached, uint32_t* pending, uint32_t declaration)
{
    if (reached[declaration] == 0)
    {
        reached[declaration] = 1;
        census->worklist[*pending] = declaration;
        *pending += 1;
    }
}

// Every declarer of `key` before declaration `position`.
static void check_attested_rise(CheckAttested* census, uint8_t* reached, uint32_t* pending, uint32_t key, uint32_t position)
{
    const CheckAttestedKey* state = key < 2 * BUSTER_ATTESTED_MAX_IDS ? census->keys + key : NULL;
    if (state != NULL && state->stamp == census->stamp && state->local != BUSTER_ATTESTED_NONE)
    {
        CheckAttestedList* list = census->lists + state->local;
        uint32_t entry = list->low;
        uint32_t remaining = list->remaining;
        while (remaining != 0 && census->entry_declarations[entry] < position)
        {
            check_attested_reach_one(census, reached, pending, census->entry_declarations[entry]);
            entry = census->entry_next[entry];
            remaining -= 1;
        }
        list->low = entry;
        list->remaining = remaining;
    }
}

// Every declarer of `key` after declaration `position`.
static void check_attested_fall(CheckAttested* census, uint8_t* reached, uint32_t* pending, uint32_t key, uint32_t position)
{
    const CheckAttestedKey* state = key < 2 * BUSTER_ATTESTED_MAX_IDS ? census->keys + key : NULL;
    if (state != NULL && state->stamp == census->stamp && state->local != BUSTER_ATTESTED_NONE)
    {
        CheckAttestedList* list = census->lists + state->local;
        uint32_t entry = list->high;
        uint32_t remaining = list->remaining;
        while (remaining != 0 && census->entry_declarations[entry] > position)
        {
            check_attested_reach_one(census, reached, pending, census->entry_declarations[entry]);
            entry = census->entry_previous[entry];
            remaining -= 1;
        }
        list->high = entry;
        list->remaining = remaining;
    }
}

// The cursors at their lists' ends.
static void check_attested_cursors(CheckAttested* census)
{
    for (uint32_t k = 0; k < census->distinct_count; k += 1)
    {
        CheckAttestedList* list = census->lists + k;
        list->low = list->first;
        list->high = list->last;
        list->remaining = list->count;
    }
}

// The identifiers in the strings of declaration `d`'s asm and attribute groups: each word's key, risen to the unit's end.
static void check_attested_string_names(CheckAttested* census, const Parser* parser, Preprocessor* pp, uint8_t* reached, uint32_t* pending, uint32_t d)
{
    const PPTokens* output = &pp->output;
    uint32_t low = 0;
    uint32_t high = census->string_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = census->string_declarations[middle] < d ? middle + 1 : low;
        high = census->string_declarations[middle] < d ? high : middle;
    }
    for (uint32_t s = low; s < census->string_count && census->string_declarations[s] == d; s += 1)
    {
        const uint32_t source = parser->tokens.sources[census->string_tokens[s]];
        const uint64_t capacity = (uint64_t)output->values[source] - output->locations[source] + 1;
        const uint8_t* spelling = census->spelling;
        const uint32_t length = capacity <= census->spelling_capacity ? pp_spelling(pp, output->kinds[source], output->values[source], output->locations[source], census->spelling, &spelling) : 0;
        uint32_t i = 0;
        while (i < length)
        {
            const uint8_t c = spelling[i];
            const bool starts = c == '_' || c == '$' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            uint32_t end = i + 1;
            while (starts && end < length && (spelling[end] == '_' || spelling[end] == '$' || spelling[end] == '.' || (spelling[end] >= 'a' && spelling[end] <= 'z') ||
                                              (spelling[end] >= 'A' && spelling[end] <= 'Z') || (spelling[end] >= '0' && spelling[end] <= '9')))
            {
                end += 1;
            }
            const uint32_t id = starts ? intern_find(pp->table, spelling + i, end - i) : 0;
            if (id != 0 && id < BUSTER_ATTESTED_MAX_IDS)
            {
                check_attested_rise(census, reached, pending, id * 2, census->declaration_count);
            }
            i = end;
        }
    }
}

static __attribute__((noinline)) void check_attested_reach(CheckAttested* census, const Parser* parser, Preprocessor* pp, uint8_t* reached, bool runtime)
{
    const uint32_t declarations = census->declaration_count;
    uint32_t pending = 0;
    memset(reached, 0, declarations);
    check_attested_cursors(census);
    for (uint32_t d = 0; d < declarations; d += 1)
    {
        if ((census->flags[d] & BUSTER_ATTESTED_DEFINITION) != 0 || census->hits[d] == 0)
        {
            check_attested_reach_one(census, reached, &pending, d);
        }
    }
    for (uint32_t p = 0; p < census->pragma_count; p += 1)
    {
        // A pragma spells its names (every earlier declaration of them) and counts as declaring them (every later one).
        const uint32_t position = census->pragma_declarations[p];
        for (uint32_t k = census->pragma_name_starts[p]; k < census->pragma_name_starts[p + 1]; k += 1)
        {
            check_attested_rise(census, reached, &pending, census->pragma_keys[k], position != 0 ? position : declarations);
            if (position != 0)
            {
                check_attested_fall(census, reached, &pending, census->pragma_keys[k], position - 1);
            }
        }
    }
    for (uint32_t r = 0; runtime && census->any_definition && r < census->runtime_count; r += 1)
    {
        check_attested_rise(census, reached, &pending, census->runtime_keys[r], declarations);
    }
    while (pending != 0)
    {
        pending -= 1;
        const uint32_t d = census->worklist[pending];
        const uint8_t flags = census->flags[d];
        for (uint32_t n = census->name_starts[d]; n < census->name_starts[d + 1]; n += 1)
        {
            const uint32_t entry = census->name_entries[n];
            const uint32_t key = entry & BUSTER_ATTESTED_KEY_MASK;
            check_attested_rise(census, reached, &pending, key, d);
            if ((entry >> BUSTER_ATTESTED_CLASS_SHIFT & BUSTER_ATTESTED_DECLARES) != 0)
            {
                check_attested_fall(census, reached, &pending, key, d);
            }
            // A `__builtin_X` name: X's declarations, whose symbol a call of the builtin may take.
            const uint32_t id = key / 2;
            if ((key & 1) == 0 && (census->id_facts[id] & BUSTER_ATTESTED_ID_BUILTIN) != 0)
            {
                check_attested_rise(census, reached, &pending, check_attested_builtin_target(census, pp->table, id), declarations);
            }
        }
        if ((flags & BUSTER_ATTESTED_STRINGS) != 0)
        {
            check_attested_string_names(census, parser, pp, reached, &pending, d);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The marks: a bit for each token of a skipped declaration (attested and not reached), 64 tokens a word.

static __attribute__((noinline)) void check_attested_marks(CheckAttested* census)
{
    const uint32_t words = census->token_count / 64 + 1;
    memset(census->marks, 0, (uint64_t)words * sizeof(uint64_t));
    for (uint32_t d = 0; d < census->declaration_count; d += 1)
    {
        if (census->hits[d] != 0 && census->reached[d] == 0)
        {
            const uint32_t first = census->firsts[d];
            const uint32_t end = census->lasts[d] + 1;
            uint32_t t = first;
            while (t < end)
            {
                const uint32_t word = t / 64;
                const uint32_t bit = t % 64;
                const uint32_t span = end - t < 64 - bit ? end - t : 64 - bit;
                census->marks[word] |= (span == 64 ? ~0ull : ((1ull << span) - 1)) << bit;
                t += span;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The unit before its reader: its arrays, the passes timed, then the reader with its environment published.

// The counter's value: with rdpmc from its page under the page's sequence lock, as linux/perf_event.h describes (the
// parser benchmark's reader), or with read() where the kernel doesn't allow rdpmc.
static uint64_t check_attested_count(const CheckAttested* census, uint32_t c)
{
    uint64_t value = 0;
    volatile struct perf_event_mmap_page* page = census->pages[c];
    if (page != NULL && page->cap_user_rdpmc)
    {
        bool stable = false;
        while (!stable)
        {
            const uint32_t sequence = page->lock;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            const uint32_t index = page->index;
            int64_t count = page->offset;
            if (index != 0)
            {
                const uint32_t shift = 64 - page->pmc_width;
                count += (int64_t)(__rdpmc((int)index - 1) << shift) >> shift;
            }
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            stable = page->lock == sequence;
            value = (uint64_t)count;
        }
    }
    else if (census->counters[c] >= 0 && read(census->counters[c], &value, sizeof(value)) != (ssize_t)sizeof(value))
    {
        value = 0;
    }
    return value;
}

// The counters' values into `values`.
static void check_attested_counts(const CheckAttested* census, uint64_t* values)
{
    for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
    {
        values[c] = check_attested_count(census, c);
    }
}

// Closes the counters.
static void check_attested_counters_close(CheckAttested* census)
{
    for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
    {
        if (census->pages[c] != NULL)
        {
            munmap((void*)census->pages[c], (size_t)sysconf(_SC_PAGESIZE));
            census->pages[c] = NULL;
        }
        if (census->counters[c] >= 0)
        {
            close(census->counters[c]);
            census->counters[c] = -1;
        }
    }
}

// This thread's user-mode counters, opened again where the unit runs on another thread than the last.
static void check_attested_counter(CheckAttested* census)
{
    const pid_t thread = (pid_t)syscall(SYS_gettid);
    if (census->counters[CHECK_ATTESTED_CYCLES] < 0 || census->cycles_thread != thread)
    {
        check_attested_counters_close(census);
        const uint32_t types[CHECK_ATTESTED_COUNTERS] = { PERF_TYPE_HARDWARE, PERF_TYPE_HARDWARE, PERF_TYPE_HW_CACHE };
        const uint64_t configs[CHECK_ATTESTED_COUNTERS] = {
            PERF_COUNT_HW_CPU_CYCLES,
            PERF_COUNT_HW_BRANCH_MISSES,
            PERF_COUNT_HW_CACHE_L1D | (uint64_t)PERF_COUNT_HW_CACHE_OP_READ << 8 | (uint64_t)PERF_COUNT_HW_CACHE_RESULT_MISS << 16,
        };
        for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
        {
            struct perf_event_attr attribute;
            memset(&attribute, 0, sizeof(attribute));
            attribute.type = types[c];
            attribute.size = sizeof(attribute);
            attribute.config = configs[c];
            attribute.exclude_kernel = 1;
            attribute.exclude_hv = 1;
            census->counters[c] = (int)syscall(SYS_perf_event_open, &attribute, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
            void* page = census->counters[c] >= 0 ? mmap(0, (size_t)sysconf(_SC_PAGESIZE), PROT_READ, MAP_SHARED, census->counters[c], 0) : MAP_FAILED;
            census->pages[c] = page != MAP_FAILED ? (volatile struct perf_event_mmap_page*)page : NULL;
        }
        census->cycles_thread = thread;
        census->counted = census->counters[CHECK_ATTESTED_CYCLES] >= 0;
        // A read's own cycles, from a pair of reads with nothing between them, the least of a few.
        uint64_t least = UINT64_MAX;
        for (uint32_t i = 0; census->counted && i < 16; i += 1)
        {
            const uint64_t before = check_attested_count(census, CHECK_ATTESTED_CYCLES);
            const uint64_t after = check_attested_count(census, CHECK_ATTESTED_CYCLES);
            least = after - before < least ? after - before : least;
        }
        census->read_overhead = census->counted ? least : 0;
    }
}

// The unit's arrays, for `count` tokens and `pragmas` pragmas; whether there was room.
static bool check_attested_unit_arrays(CheckAttested* census, uint32_t count, uint32_t pragmas, uint32_t output_count)
{
    Arena* arena = &census->unit_arena;
    arena_pop_to(arena, 0);
    const uint64_t slots = (uint64_t)count + 2;
    census->firsts = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->lasts = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->flags = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->locations = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->name_starts = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->name_entries = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->owns = arena_push_array(arena, slots, sizeof(CheckAttestedHash), 64);
    census->semantics = arena_push_array(arena, slots, sizeof(CheckAttestedHash), 64);
    census->hits = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->reached = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->reached_without_runtime = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->worklist = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->entry_declarations = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->entry_next = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->entry_previous = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->lists = arena_push_array(arena, slots, sizeof(CheckAttestedList), 64);
    census->marks = arena_push_array(arena, slots / 64 + 2, sizeof(uint64_t), 64);
    census->string_tokens = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->string_declarations = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->pragma_declarations = arena_push_array(arena, (uint64_t)pragmas + 1, sizeof(uint32_t), 64);
    census->pragma_inside = arena_push_array(arena, (uint64_t)pragmas + 1, sizeof(uint8_t), 64);
    census->pragma_hashes = arena_push_array(arena, (uint64_t)pragmas + 1, sizeof(CheckAttestedHash), 64);
    census->pragma_name_starts = arena_push_array(arena, (uint64_t)pragmas + 2, sizeof(uint32_t), 64);
    census->pragma_keys = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->levels = arena_push_array(arena, (uint64_t)output_count + 4, sizeof(uint8_t), 64);
    census->token_levels = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->spelling_capacity = UINT64_C(1) << 20;
    census->spelling = arena_push_array(arena, census->spelling_capacity, sizeof(uint8_t), 64);
    return census->spelling != NULL;
}

// The pp output's system levels, as the reader's check_input_system_tokens makes them, or none where no file is a
// system one.
static void check_attested_levels(CheckAttested* census, const Parser* parser, Preprocessor* pp)
{
    bool system = false;
    for (uint32_t i = 0; !system && i < pp->range_count; i += 1)
    {
        system = pp->ranges[i].system != BUSTER_PP_USER_FILE;
    }
    for (uint32_t i = 0; !system && i < pp->line_directive_count; i += 1)
    {
        system = pp->line_directives[i].system != BUSTER_PP_USER_FILE;
    }
    if (system && pp->output.locations != NULL)
    {
        if (BUSTER_SCALAR_PATH(parser->options.scalar) || pp->system_runs_inexact)
        {
            pp_history_ensure(pp);
            pp_system_levels_history(pp, census->levels);
        }
        else
        {
            pp_system_levels(pp, census->levels);
        }
        census->levels[pp->output.count] = BUSTER_PP_USER_FILE;
        const uint32_t count = parser->tokens.count;
        for (uint32_t t = 0; t < count; t += 1)
        {
            const uint32_t source = parser->tokens.sources[t];
            census->token_levels[t] = source < pp->output.count ? census->levels[source] : BUSTER_PP_USER_FILE;
        }
    }
    else
    {
        // Every token a user file's: the column all BUSTER_PP_USER_FILE, which the own hashes read as any other.
        _Static_assert(BUSTER_PP_USER_FILE == 0, "a user file's level is zero");
        memset(census->token_levels, 0, (uint64_t)parser->tokens.count + 1);
    }
}

// One run of the passes, each one's user-mode cycles into `cycles` and TSC ticks into `ticks`: whether the lists could
// be made (room, and every pragma's end).
static bool check_attested_passes(CheckAttested* census, const Parser* parser, Preprocessor* pp, uint64_t* cycles, uint64_t* ticks,
                                  uint64_t events[CHECK_ATTESTED_PASSES][CHECK_ATTESTED_COUNTERS])
{
    const uint64_t overhead = census->read_overhead;
    census->stamp += 1;
    uint64_t laps[CHECK_ATTESTED_PASSES + 1][CHECK_ATTESTED_COUNTERS];
    uint64_t times[CHECK_ATTESTED_PASSES + 1];
    check_attested_counts(census, laps[0]);
    times[0] = __rdtsc();
    const bool listed = check_attested_lists(census, parser) && check_attested_pragmas(census, parser, pp);
    times[1] = __rdtsc();
    check_attested_counts(census, laps[1]);
    if (listed)
    {
        check_attested_hashes(census, parser);
    }
    times[2] = __rdtsc();
    check_attested_counts(census, laps[2]);
    if (listed)
    {
        check_attested_literals(census, parser, pp);
    }
    times[3] = __rdtsc();
    check_attested_counts(census, laps[3]);
    if (listed)
    {
        check_attested_chain(census);
    }
    times[4] = __rdtsc();
    check_attested_counts(census, laps[4]);
    if (listed)
    {
        check_attested_reach(census, parser, pp, census->reached, true);
    }
    times[5] = __rdtsc();
    check_attested_counts(census, laps[5]);
    if (listed)
    {
        check_attested_marks(census);
    }
    times[6] = __rdtsc();
    check_attested_counts(census, laps[6]);
    for (uint32_t p = 0; p < CHECK_ATTESTED_PASSES; p += 1)
    {
        const uint64_t lap = laps[p + 1][CHECK_ATTESTED_CYCLES] - laps[p][CHECK_ATTESTED_CYCLES];
        cycles[p] = lap > overhead ? lap - overhead : 0;
        ticks[p] = times[p + 1] - times[p];
        for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
        {
            events[p][c] = laps[p + 1][c] - laps[p][c];
        }
    }
    return listed;
}

// check_driver_unit's reader for a census's unit: the passes, then check_input_run as check_input_compile calls it, with
// the reader's environment published (its binding events kept past it). A view of a failed parse runs no passes.
static bool check_attested_input(CheckAttested* census, CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch,
                                 bool complete_translation_unit, uint32_t original_end, const uint8_t* deferred_tokens, const char* path)
{
    const uint32_t count = parser->tokens.count;
    census->unit_ready = false;
    census->path = path;
    census->token_count = count;
    const bool whole = complete_translation_unit && parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE && parser->tree.roles != NULL &&
                       parser->tree.parents != NULL && pp->table->count < BUSTER_ATTESTED_MAX_IDS;
    if (whole && check_attested_unit_arrays(census, count, parser->tokens.pragma_count, pp->output.count))
    {
        check_attested_ids(census, pp->table);
        check_attested_levels(census, parser, pp);
        check_attested_counter(census);
        uint64_t least[CHECK_ATTESTED_PASSES];
        uint64_t least_ticks[CHECK_ATTESTED_PASSES];
        uint64_t events[CHECK_ATTESTED_PASSES][CHECK_ATTESTED_COUNTERS];
        uint64_t least_events[CHECK_ATTESTED_PASSES][CHECK_ATTESTED_COUNTERS];
        bool listed = check_attested_passes(census, parser, pp, least, least_ticks, events);
        memcpy(least_events, events, sizeof(events));
        for (uint32_t p = 0; p < CHECK_ATTESTED_PASSES; p += 1)
        {
            census->first_cycles[p] += least[p];
            for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
            {
                census->first_events[p][c] += events[p][c];
            }
        }
        // The same passes again, their results the same, each one's least cycles (with that run's events) and ticks kept.
        for (uint32_t run = 1; run < census->passes; run += 1)
        {
            uint64_t again[CHECK_ATTESTED_PASSES];
            uint64_t again_ticks[CHECK_ATTESTED_PASSES];
            listed &= check_attested_passes(census, parser, pp, again, again_ticks, events);
            for (uint32_t p = 0; p < CHECK_ATTESTED_PASSES; p += 1)
            {
                if (again[p] < least[p])
                {
                    least[p] = again[p];
                    memcpy(least_events[p], events[p], sizeof(events[p]));
                }
                least_ticks[p] = again_ticks[p] < least_ticks[p] ? again_ticks[p] : least_ticks[p];
            }
        }
        for (uint32_t p = 0; p < CHECK_ATTESTED_PASSES; p += 1)
        {
            census->pass_cycles[p] += least[p];
            census->pass_ticks[p] += least_ticks[p];
            for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
            {
                census->least_events[p][c] += least_events[p][c];
            }
        }
        census->pass_tokens += count;
        // Untimed: the reach without the runtime functions' names, for what that rule alone reaches.
        check_attested_reach(census, parser, pp, census->reached_without_runtime, false);
        // Each declaration's place, for --list and the tests' records alone.
        for (uint32_t d = 0; (census->list != 0 || census->records != NULL) && d < census->declaration_count; d += 1)
        {
            const uint32_t source = parser->tokens.sources[census->firsts[d]];
            census->locations[d] = source < pp->output.count && pp->output.locations != NULL ? pp->output.locations[source] : 0;
        }
        census->unit_ready = listed;
    }
    return check_input_run(input, parser, pp, types, arena, scratch, complete_translation_unit, original_end, deferred_tokens, CHECK_INPUT_BINDINGS_DEFAULT, 0, true, true,
                           true);
}

// ---------------------------------------------------------------------------------------------------------------------
// The record, at the unit's end.

// The declaration that holds token `token`, or the count where none does.
static uint32_t check_attested_declaration_of(const CheckAttested* census, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = census->declaration_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = census->lasts[middle] < token ? middle + 1 : low;
        high = census->lasts[middle] < token ? high : middle;
    }
    return low < census->declaration_count && census->firsts[low] <= token ? low : census->declaration_count;
}

// The token at output position `position`: the last whose source is at or before it, or the count past the last's.
static uint32_t check_attested_token_at(const Parser* parser, uint32_t position)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t* const sources = parser->tokens.sources;
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = sources[middle] <= position ? middle + 1 : low;
        high = sources[middle] <= position ? high : middle;
    }
    const bool past = low == count && (count == 0 || position > sources[count - 1]);
    return low == 0 || past ? count : low - 1;
}

// Whether declaration `d`'s list holds `key` with a class among `classes`.
static bool check_attested_lists_hold(const CheckAttested* census, uint32_t d, uint32_t key, uint8_t classes)
{
    bool found = false;
    for (uint32_t n = census->name_starts[d]; !found && n < census->name_starts[d + 1]; n += 1)
    {
        const uint32_t entry = census->name_entries[n];
        found = (entry & BUSTER_ATTESTED_KEY_MASK) == key && (entry >> BUSTER_ATTESTED_CLASS_SHIFT & classes) != 0;
    }
    return found;
}

// Declaration `d`'s line and file, as a diagnostic at its first token would name them, with its unit's.
static void check_attested_list_declaration(CheckAttested* census, Preprocessor* pp, const char* what, uint32_t d)
{
    const char* name = NULL;
    uint32_t length = 0;
    const uint32_t location = census->locations[d];
    const uint32_t line = location != 0 ? pp_presumed(pp, location, &name, &length) : 0;
    printf("attested: list %s unit=%s file=%.*s line=%u tokens=%u\n", what, census->path != NULL ? census->path : "-", name != NULL ? (int)length : 1,
           name != NULL ? name : "-", line, census->lasts[d] - census->firsts[d] + 1);
}

// check_driver_unit's call at the unit's end (its checked view `parser`, its reader's `input`, `types`, and whether it
// failed): each declaration's record and outcome, and the memo's new hashes.
static void check_attested_after(CheckAttested* census, const Parser* parser, Preprocessor* pp, const CheckInput* input, const TypeTable* types, bool valid,
                                 bool complete)
{
    const uint32_t count = parser->tokens.count;
    census->units_observed += 1;
    census->tokens += count;
    const bool ready = census->unit_ready && complete && count == census->token_count;
    census->unit_ready = false;
    const uint32_t declarations = ready ? census->declaration_count : 0;
    census->units_without_passes += ready ? 0 : 1;
    // The unit's diagnostics, each at the declaration that holds its output position's token.
    const uint64_t mark = census->unit_arena.length;
    uint32_t* diagnostics = ready ? arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64) : NULL;
    uint8_t* unlisted = ready ? arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(uint8_t), 64) : NULL;
    const bool arrays = diagnostics != NULL && unlisted != NULL;
    if (arrays)
    {
        memset(diagnostics, 0, ((uint64_t)declarations + 1) * sizeof(uint32_t));
        memset(unlisted, 0, (uint64_t)declarations + 1);
    }
    bool error = !valid || input->report_count != 0 || !input->environment.valid || pp->diagnostic_collection_failed;
    uint32_t unplaced = 0;
    for (const PPDiagnosticRecord* record = pp->diagnostic_first; record != NULL; record = record->next)
    {
        error |= record->severity == PP_SEVERITY_ERROR || record->severity == PP_SEVERITY_FATAL;
        const uint64_t position = record->position / PP_DIAGNOSTIC_PHASE_COUNT;
        const uint32_t token = position < UINT32_MAX ? check_attested_token_at(parser, (uint32_t)position) : count;
        const uint32_t d = arrays && token < count ? check_attested_declaration_of(census, token) : declarations;
        if (d < declarations)
        {
            diagnostics[d] += 1;
        }
        else
        {
            unplaced += 1;
        }
        census->diagnostics += 1;
    }
    census->unplaced_diagnostics += unplaced;
    census->units_unplaced += unplaced != 0 ? 1 : 0;
    // The binding events of file scope against the lists.
    const CBindings* bindings = arrays && input->environment.valid ? input->environment.bindings : NULL;
    const uint32_t name_capacity = bindings != NULL ? bindings->name_capacity : 0;
    for (uint32_t e = 0; bindings != NULL && e < bindings->count; e += 1)
    {
        const uint8_t operation = bindings->operation[e];
        const uint32_t space = bindings->key[e] / name_capacity;
        const uint32_t id = bindings->key[e] % name_capacity;
        const bool file_declare = operation == C_BINDING_DECLARE && bindings->depth[e] == 0 && space <= C_BINDING_TAG;
        const bool use = operation == C_BINDING_USE && space <= C_BINDING_TAG;
        const uint32_t d = file_declare || use ? check_attested_declaration_of(census, bindings->point[e]) : declarations;
        if (d < declarations)
        {
            const uint32_t key = id * 2 + (space == C_BINDING_TAG ? 1u : 0u);
            const bool held = check_attested_lists_hold(census, d, key, file_declare ? BUSTER_ATTESTED_DECLARES : BUSTER_ATTESTED_DECLARES | BUSTER_ATTESTED_LOOKS_UP);
            census->declare_events += file_declare ? 1 : 0;
            census->use_events += use ? 1 : 0;
            census->unlisted_declares += file_declare && !held ? 1 : 0;
            census->unlisted_uses += use && !held ? 1 : 0;
            unlisted[d] |= !held ? 1 : 0;
            if (!held && census->list != 0)
            {
                printf("attested: unlisted %s name=%.*s\n", file_declare ? "declare" : "use", (int)intern_length(pp->table, id), (const char*)intern_bytes(pp->table, id));
                check_attested_list_declaration(census, pp, "unlisted", d);
            }
        }
    }
    // The tags' completions against the lists. A completion is the token after a body's `}`, or a fixed enum's `{` or `;`
    // (the token before either is the declaration's), and 0 for a predefined type's tag (`__va_list_tag`), no
    // declaration's.
    const uint32_t tags = bindings != NULL && input->environment.tag_completions != NULL ? input->environment.tag_count : 0;
    for (uint32_t tag = 0; tag < tags && tag < types->tags.count; tag += 1)
    {
        const uint32_t completion = input->environment.tag_completions[tag];
        const uint32_t name = types->tags.name[tag];
        const uint32_t d = completion != 0 && completion <= count && name != 0 ? check_attested_declaration_of(census, completion - 1) : declarations;
        if (d < declarations && (census->flags[d] & BUSTER_ATTESTED_DEFINITION) == 0 && !check_attested_lists_hold(census, d, name * 2 + 1, BUSTER_ATTESTED_DECLARES))
        {
            census->unlisted_completions += 1;
            unlisted[d] = 1;
            if (census->list != 0)
            {
                printf("attested: unlisted completion name=%.*s\n", (int)intern_length(pp->table, name), (const char*)intern_bytes(pp->table, name));
                check_attested_list_declaration(census, pp, "unlisted", d);
            }
        }
    }
    error |= ready && !arrays;
    const bool learning = !error && unplaced == 0;
    census->units_failed += error ? 1 : 0;
    census->failed_tokens += error || !ready ? count : 0;
    const uint8_t* const bits = input->declaration_bits;
    for (uint32_t d = 0; ready && arrays && d < declarations; d += 1)
    {
        const uint32_t first = census->firsts[d];
        const uint32_t last = census->lasts[d];
        const uint32_t tokens = last - first + 1;
        const uint8_t flags = census->flags[d];
        // Its reading's consequences, by the reader's facts at its tokens.
        CheckAttestedOutcome impure = CHECK_ATTESTED_OUTCOMES;
        for (uint32_t t = first; bits != NULL && t <= last && impure == CHECK_ATTESTED_OUTCOMES; t += 1)
        {
            const uint8_t b = bits[t];
            const CheckInputAttributes* attributes = (b & (BUSTER_CHECK_BITS_STATIC | BUSTER_CHECK_BITS_DEFINED)) != 0 || check_input_linkage(bits, t) != 0 ?
                                                     check_input_attributes_at(input, t) : NULL;
            impure = (b & BUSTER_CHECK_BITS_DEFINED) != 0 ? CHECK_ATTESTED_DEFINES :
                     (b & BUSTER_CHECK_BITS_TENTATIVE) != 0 ? CHECK_ATTESTED_TENTATIVE :
                     check_input_linkage(bits, t) == 1 ? CHECK_ATTESTED_INTERNAL :
                     (b & BUSTER_CHECK_BITS_REGISTER) != 0 ? CHECK_ATTESTED_REGISTER :
                     attributes != NULL && check_input_attribute_alias_definition(attributes) ? CHECK_ATTESTED_ALIAS : CHECK_ATTESTED_OUTCOMES;
        }
        impure = impure == CHECK_ATTESTED_OUTCOMES && (flags & BUSTER_ATTESTED_ASM) != 0 ? CHECK_ATTESTED_ASM_STATEMENT : impure;
        const bool eligible = diagnostics[d] == 0 && unlisted[d] == 0 && impure == CHECK_ATTESTED_OUTCOMES;
        const bool hit = census->hits[d] != 0;
        CheckAttestedOutcome outcome = error ? CHECK_ATTESTED_ERROR_UNIT :
                                       (flags & BUSTER_ATTESTED_DEFINITION) != 0 ? CHECK_ATTESTED_DEFINITION_ROOT :
                                       (flags & BUSTER_ATTESTED_PRAGMA) != 0 ? CHECK_ATTESTED_PRAGMA_INSIDE :
                                       hit && census->reached[d] == 0 ? CHECK_ATTESTED_SKIPPED :
                                       hit && census->reached_without_runtime[d] != 0 ? CHECK_ATTESTED_REACHED :
                                       hit ? CHECK_ATTESTED_REACHED_RUNTIME :
                                       diagnostics[d] != 0 ? CHECK_ATTESTED_DIAGNOSTIC :
                                       unlisted[d] != 0 ? CHECK_ATTESTED_UNLISTED :
                                       impure != CHECK_ATTESTED_OUTCOMES ? impure :
                                       learning ? CHECK_ATTESTED_LEARNED : CHECK_ATTESTED_UNPLACED;
        census->declarations += 1;
        census->declaration_tokens += tokens;
        census->outcome_declarations[outcome] += 1;
        census->outcome_tokens[outcome] += tokens;
        census->hits_total += hit && !error ? 1 : 0;
        census->hit_tokens += hit && !error ? tokens : 0;
        // A hit whose reading here wasn't what the memo attests: an input of the reading the hash doesn't cover.
        if (hit && !error && !eligible)
        {
            census->contradictions += 1;
            census->contradiction_tokens += tokens;
            if (census->contradictions_listed < census->list)
            {
                census->contradictions_listed += 1;
                printf("attested: contradiction diagnostics=%u unlisted=%u impure=%s\n", diagnostics[d], unlisted[d],
                       impure != CHECK_ATTESTED_OUTCOMES ? check_attested_outcome_names[impure] : "-");
                check_attested_list_declaration(census, pp, "contradiction", d);
            }
        }
        if (outcome != CHECK_ATTESTED_SKIPPED && census->listed[outcome] < census->list)
        {
            census->listed[outcome] += 1;
            check_attested_list_declaration(census, pp, check_attested_outcome_names[outcome], d);
        }
        if (outcome == CHECK_ATTESTED_LEARNED)
        {
            census->failed |= !check_attested_memo_add(census, census->semantics[d]);
        }
        if (census->records != NULL && census->record_count < census->record_capacity)
        {
            const char* name = NULL;
            uint32_t length = 0;
            const uint32_t location = census->locations[d];
            CheckAttestedRecord* record = census->records + census->record_count;
            *record = (CheckAttestedRecord){
                .unit = (uint32_t)(census->units_observed - 1),
                .line = location != 0 ? pp_presumed(pp, location, &name, &length) : 0,
                .outcome = (uint8_t)outcome,
                .main_file = location != 0 && pp_location_in_main_file(pp, location),
                .contradiction = hit && !error && !eligible,
            };
            uint32_t base = length;
            while (name != NULL && base != 0 && name[base - 1] != '/')
            {
                base -= 1;
            }
            const uint32_t kept = name != NULL ? (length - base < sizeof(record->file) - 1 ? length - base : (uint32_t)sizeof(record->file) - 1) : 0;
            if (kept != 0)
            {
                memcpy(record->file, name + base, kept);
            }
            census->record_count += 1;
        }
    }
    arena_pop_to(&census->unit_arena, mark);
}

// ---------------------------------------------------------------------------------------------------------------------
// The report and the command.

static double check_attested_share(uint64_t part, uint64_t whole)
{
    return whole != 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

static void check_attested_report(const CheckAttested* census)
{
    const uint64_t definitions = census->outcome_tokens[CHECK_ATTESTED_DEFINITION_ROOT];
    const uint64_t outside = census->declaration_tokens - definitions;
    const uint64_t skipped = census->outcome_tokens[CHECK_ATTESTED_SKIPPED];
    printf("attested: units=%" PRIu64 " failed=%" PRIu64 " without-passes=%" PRIu64 " with-unplaced-diagnostics=%" PRIu64 " tokens=%" PRIu64 " failed-tokens=%" PRIu64 "\n",
           census->units_observed, census->units_failed, census->units_without_passes, census->units_unplaced, census->tokens, census->failed_tokens);
    printf("attested: declarations=%" PRIu64 " tokens=%" PRIu64 " outside-definitions=%" PRIu64 " memo=%" PRIu64 "\n", census->declarations, census->declaration_tokens, outside,
           census->memo_count);
    printf("attested: skippable declarations=%" PRIu64 " tokens=%" PRIu64 " (%.2f%% of the tokens outside definitions, %.2f%% of all)\n",
           census->outcome_declarations[CHECK_ATTESTED_SKIPPED], skipped, check_attested_share(skipped, outside), check_attested_share(skipped, census->tokens));
    for (uint32_t o = 0; o < CHECK_ATTESTED_OUTCOMES; o += 1)
    {
        printf("attested: outcome=%s declarations=%" PRIu64 " tokens=%" PRIu64 " (%.2f%% of the tokens outside definitions)\n", check_attested_outcome_names[o],
               census->outcome_declarations[o], census->outcome_tokens[o], o == CHECK_ATTESTED_DEFINITION_ROOT ? 0.0 : check_attested_share(census->outcome_tokens[o], outside));
    }
    printf("attested: hits=%" PRIu64 " tokens=%" PRIu64 " contradictions=%" PRIu64 " tokens=%" PRIu64 "\n", census->hits_total, census->hit_tokens, census->contradictions,
           census->contradiction_tokens);
    printf("attested: events declares=%" PRIu64 " unlisted=%" PRIu64 " completions-unlisted=%" PRIu64 " uses=%" PRIu64 " unlisted-uses=%" PRIu64 "\n", census->declare_events,
           census->unlisted_declares, census->unlisted_completions, census->use_events, census->unlisted_uses);
    printf("attested: diagnostics=%" PRIu64 " unplaced=%" PRIu64 "\n", census->diagnostics, census->unplaced_diagnostics);
    printf("attested: literals=%" PRIu64 " spelled=%" PRIu64 " range-searches=%" PRIu64 " (of every run of the passes)\n", census->literal_count, census->spelled_literals,
           census->range_searches);
    uint64_t all = 0;
    uint64_t first_all = 0;
    uint64_t ticks = 0;
    const double tokens = census->pass_tokens != 0 ? (double)census->pass_tokens : 1.0;
    for (uint32_t p = 0; p < CHECK_ATTESTED_PASSES; p += 1)
    {
        printf("attested: pass=%s cycles=%" PRIu64 " per-token=%.2f first-run=%.2f ticks-per-token=%.2f branch-misses-per-token=%.3f first-run=%.3f "
               "l1d-misses-per-token=%.3f first-run=%.3f\n",
               check_attested_pass_names[p], census->pass_cycles[p], (double)census->pass_cycles[p] / tokens, (double)census->first_cycles[p] / tokens,
               (double)census->pass_ticks[p] / tokens, (double)census->least_events[p][CHECK_ATTESTED_BRANCH_MISSES] / tokens,
               (double)census->first_events[p][CHECK_ATTESTED_BRANCH_MISSES] / tokens, (double)census->least_events[p][CHECK_ATTESTED_DATA_MISSES] / tokens,
               (double)census->first_events[p][CHECK_ATTESTED_DATA_MISSES] / tokens);
        all += census->pass_cycles[p];
        first_all += census->first_cycles[p];
        ticks += census->pass_ticks[p];
    }
    printf("attested: passes cycles=%" PRIu64 " tokens=%" PRIu64 " per-token=%.2f first-run=%.2f ticks-per-token=%.2f counted=%s runs=%u\n", all, census->pass_tokens,
           (double)all / tokens, (double)first_all / tokens, (double)ticks / tokens, census->counted ? "yes" : "no", census->passes);
}

static bool check_attested_number(const char* text, uint32_t* result)
{
    uint64_t value = 0;
    bool valid = text != NULL && *text != 0;
    for (const char* c = text; valid && *c != 0; c += 1)
    {
        valid = *c >= '0' && *c <= '9' && value <= 1000000;
        value = value * 10 + (uint64_t)(*c - '0');
    }
    *result = (uint32_t)value;
    return valid && value <= 1000000;
}

static bool check_attested_open(CheckAttested* census)
{
    for (uint32_t c = 0; c < CHECK_ATTESTED_COUNTERS; c += 1)
    {
        census->counters[c] = -1;
    }
    const uint64_t keys = 2 * (uint64_t)BUSTER_ATTESTED_MAX_IDS;
    const uint32_t runtimes = check_attested_runtime_count();
    // Room for every size the memo takes, each table after the last: twice its most slots.
    census->memo_arena = arena_reserve(2 * (uint64_t)BUSTER_ATTESTED_MEMO_SLOTS * sizeof(CheckAttestedHash) + 4096);
    census->memo = arena_push_array(&census->memo_arena, BUSTER_ATTESTED_MEMO_FIRST_SLOTS, sizeof(CheckAttestedHash), 64);
    census->memo_mask = BUSTER_ATTESTED_MEMO_FIRST_SLOTS - 1;
    census->key_arena = arena_reserve(keys * sizeof(CheckAttestedKey) + (uint64_t)BUSTER_ATTESTED_MAX_IDS * (sizeof(uint8_t) + sizeof(uint32_t)) +
                                      2 * ((uint64_t)runtimes + 1) * sizeof(uint32_t) + 16 * 4096);
    Arena* arena = &census->key_arena;
    census->keys = arena_push_array(arena, keys, sizeof(CheckAttestedKey), 4096);
    census->id_facts = arena_push_array(arena, BUSTER_ATTESTED_MAX_IDS, sizeof(uint8_t), 4096);
    census->builtin_targets = arena_push_array(arena, BUSTER_ATTESTED_MAX_IDS, sizeof(uint32_t), 4096);
    census->runtime_keys = arena_push_array(arena, runtimes + 1, sizeof(uint32_t), 4096);
    census->runtime_lengths = arena_push_array(arena, runtimes + 1, sizeof(uint32_t), 4096);
    for (uint32_t r = 0; census->runtime_lengths != NULL && r < runtimes; r += 1)
    {
        census->runtime_lengths[r] = check_lower_runtime_names[r] != NULL ? (uint32_t)strlen(check_lower_runtime_names[r]) : UINT32_MAX;
    }
    census->unit_arena = arena_reserve(UINT64_C(1) << 36);
    check_attested_actions(census->kind_actions, census->role_actions);
    return census->memo != NULL && census->keys != NULL && census->runtime_lengths != NULL && census->unit_arena.base != NULL;
}

static void check_attested_close(CheckAttested* census)
{
    check_attested_counters_close(census);
    arena_release(&census->unit_arena);
    arena_release(&census->key_arena);
    arena_release(&census->memo_arena);
}

// The command of `count` arguments (compile options and files) run as `--lanes 1` runs it, with `census` beside it, and
// its report on stdout after the units' diagnostics, but where `quiet`: the command's status, 1 where it failed, the
// census ran out of room or observed no unit. The arguments may not set the lanes, which are the census's: one, whose
// units share the memo, as the design's lane does.
static int check_attested_run(CheckAttested* census, int count, char** arguments, bool quiet)
{
    char** forwarded = calloc((size_t)count + 3, sizeof(char*));
    bool valid = forwarded != NULL && count != 0;
    for (int i = 0; valid && i < count; i += 1)
    {
        const char* argument = arguments[i];
        valid = strcmp(argument, "--lanes") != 0 && strncmp(argument, "--lanes=", 8) != 0 && strcmp(argument, "--lane-cpus") != 0 &&
                strncmp(argument, "--lane-cpus=", 12) != 0 && strcmp(argument, "--worker-budget") != 0 && strncmp(argument, "--worker-budget=", 16) != 0 &&
                strcmp(argument, "--deferred-bodies") != 0;
        forwarded[i] = arguments[i];
    }
    int result = 1;
    // (Closed only where it was opened: a census's counters are -1 from its opening on, and 0 before, stdin's.)
    const bool opened = valid && check_attested_open(census);
    if (opened)
    {
        forwarded[count] = "--lanes";
        forwarded[count + 1] = "1";
        check_driver_attested = census;
        result = run_check_command_mode(count + 2, forwarded, true, true);
        check_driver_attested = NULL;
        fflush(stderr);
        if (!quiet)
        {
            check_attested_report(census);
        }
        fflush(stdout);
        result = result == 0 && !census->failed && census->units_observed != 0 ? 0 : 1;
    }
    if (valid)
    {
        check_attested_close(census);
    }
    free(forwarded);
    return valid ? result : 2;
}

// measure-attested [--list N] [--passes N] [compile options] file...: the census's options, then check_attested_run.
static __attribute__((unused)) int run_attested_census(int count, char** arguments)
{
    CheckAttested census = { .passes = 1 };
    int first = 0;
    bool valid = true;
    bool options = true;
    while (valid && options && first < count)
    {
        const char* argument = arguments[first];
        options = strcmp(argument, "--list") == 0 || strcmp(argument, "--passes") == 0;
        if (strcmp(argument, "--list") == 0)
        {
            valid = first + 1 < count && check_attested_number(arguments[first + 1], &census.list);
        }
        else if (strcmp(argument, "--passes") == 0)
        {
            valid = first + 1 < count && check_attested_number(arguments[first + 1], &census.passes) && census.passes != 0 && census.passes <= 100;
        }
        first += options ? 2 : 0;
    }
    const int status = valid ? check_attested_run(&census, count - first, arguments + first, false) : 2;
    if (status == 2)
    {
        fprintf(stderr, "usage: compiler measure-attested [--list N] [--passes N] [compile options] file...\n");
    }
    return status == 0 ? 0 : 1;
}

#endif
