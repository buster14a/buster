// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// #2531's attested declarations (docs/attested-declarations.md): the passes, a lane's memo, step 0's census and steps 1
// and 2's compile.
//
// The compile (--attested, c_checker_driver.c's CheckAttestedMode): each units lane of a command of several source files
// has a CheckAttested of its own (check_attested_lane_open, CheckUnitsLane.attested), whose memo and names' states its
// units share. Before each unit's reader the passes run untimed (check_attested_decide); with --attested=on the reader's
// walk passes the declarations attested and not reached (check_attested_skips, check_input_skip), and with verify a hit
// whose reading contradicts its memo fails the unit (check_attested_contradiction). The reader tests each reading's
// binding events against its list as it ends (check_attested_events); the unit's end records each reading and teaches the
// memo (check_attested_after), and says whether a unit that skipped is read again with nothing skipped
// (check_attested_again, check_units_compile). The compile's reach takes no runtime functions' names (check_attested_decide
// says why).
//
// The census, step 0: what attested declarations would skip, counted on the pipeline as it is. measure-attested runs a
// command of several units as `--lanes 1` runs it, and beside it, at two places of each unit that check_driver_unit gives
// it (one test of check_driver_attested a place, which only this command sets):
//
//   - check_attested_input, the reader's call: before the reader, the design's passes over the parser's tree, timed by
//     this thread's user-mode cycles: the lists (each file-scope declaration's range, and each identifier's class by its
//     role and the brackets around it), the declarations' own hashes, the chain (each one's semantic hash from its own
//     and its names' current hashes, the memo's probe, its declared names' new hashes), the reach (from the roots, by
//     two cursors a name) and the marks (the skipped ranges' bits). Then the reader, check_input_run as -c calls it, which
//     tests each reading's binding events against its declaration's list as it ends (check_attested_events).
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
// (check_attested_lists: the walk, the scalar path's, and on the vector path the bounds, the own hashes and the lane's
// cache of lists, check_attested_lists_cached; check_attested_hashes, check_attested_chain, check_attested_reach,
// check_attested_marks), check_attested_input, the compile's lane state, the reader's tree columns from the cache
// (check_attested_tree_columns), the record (check_attested_after), check_attested_report and run_attested_census.
// Include after c_checker_units.c: c_checker_driver.c declares the two calls and check_driver_attested.
//
// The census is a measurement beside the compile; the passes are the compile's.
#ifndef BUSTER_C_CHECK_ATTESTED_INCLUDED
#define BUSTER_C_CHECK_ATTESTED_INCLUDED

#include <elf.h>
#include <linux/perf_event.h>
#include <sys/auxv.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
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
// slots: it doubles at a quarter full, so that its few thousand hashes (musl's command's 3,218, 256 KB) stay in the
// second-level cache, where a table of the most slots put each probe in memory.
#define BUSTER_ATTESTED_MAX_IDS (1u << 24)
#define BUSTER_ATTESTED_MEMO_FIRST_SLOTS (1u << 12)
#define BUSTER_ATTESTED_MEMO_SLOTS (1u << 22)
// The most hashes a command of one source file's memo holds (CheckAttested.alone): a full file's, and as many learned.
#define BUSTER_ATTESTED_ALONE_HASHES (1u << 21)
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

// The lists' cache (check_attested_lists_cached): a lane's lists of the declarations its units read, by each one's own
// hash before its literals (its tokens' kinds, roles, names and system levels at their places, which are everything its
// list is made from) and its length. A slot's list is in the cache's pool at `offset`: the block of its declaration's tree
// columns in the tree pool (BUSTER_ATTESTED_NONE until a reader keeps them, check_attested_tree_columns), then its `names`
// entries, its `declared` keys and its `strings` tokens' places in it; `flags` are its BUSTER_ATTESTED_* (no definition is
// kept). A
// zero `low` is an empty slot (a key's is never zero). The table doubles at a quarter full, as the memo does, up to
// its most slots; the pool takes the lane's lists up to its most entries, then keeps no more.
typedef struct CheckAttestedListSlot CheckAttestedListSlot;
struct CheckAttestedListSlot
{
    uint64_t low;
    uint64_t high;
    uint32_t length;
    uint32_t offset;
    uint16_t names;
    uint16_t declared;
    uint8_t strings;
    uint8_t flags;
    uint8_t padding[2];
};
_Static_assert(sizeof(CheckAttestedListSlot) == 32, "two slots a cache line");

#define BUSTER_ATTESTED_LIST_FIRST_SLOTS (1u << 12)
#define BUSTER_ATTESTED_LIST_SLOTS (1u << 20)
#define BUSTER_ATTESTED_LIST_POOL (1u << 24)
// The tree pool's most halfwords: a kept declaration's tree is seven columns of its length, a halfword a token
// (check_attested_tree_columns), and a declaration of BUSTER_ATTESTED_TREE_MOST tokens or more is never kept.
#define BUSTER_ATTESTED_TREE_POOL (1u << 26)
#define BUSTER_ATTESTED_TREE_MOST UINT16_MAX

// A name entry of a declaration's list (CheckAttested.name_entries): its key, and its class above it.
#define BUSTER_ATTESTED_CLASS_SHIFT 25u
#define BUSTER_ATTESTED_KEY_MASK ((1u << BUSTER_ATTESTED_CLASS_SHIFT) - 1)
_Static_assert(2 * (uint64_t)BUSTER_ATTESTED_MAX_IDS <= (UINT64_C(1) << BUSTER_ATTESTED_CLASS_SHIFT), "a key fits below its class");

// The command's census: its options, the lane's state across its units (the memo, the names' current hashes and the
// reach's lists, validated by a stamp, and the names' facts), the unit's arrays and the counts.
typedef struct CheckAttested CheckAttested;
struct CheckAttested
{
    // A lane's of a compile (steps 1 and 2, check_attested_lane_open), not measure-attested's: its mode (CheckAttestedMode),
    // whether the unit's reader was given marks to skip (the unit then has `skipped` declarations, `skipped_tokens` tokens),
    // whether the unit must be read again with nothing skipped (check_attested_after: an error, an unlisted name or the typo
    // corrector), and whether the reading under way is that one, which runs no passes and teaches nothing. The unit's
    // contradictions (hits whose reading here isn't what the memo attests), and the first one's declaration.
    bool production;
    uint8_t mode;
    bool skipped;
    bool again;
    bool reading_again;
    uint32_t skipped_declarations;
    uint64_t skipped_tokens;
    uint32_t unit_contradictions;
    uint32_t contradicted;
    // The unit's tree columns that differed from the whole unit's (check_attested_tree_columns' twin), which its record
    // counts among its contradictions.
    uint32_t unit_tree_mismatches;
    // Step 3's view (check_attested_view): with --attested=on, the unit's parse without the declarations it skips, which
    // the reader and every stage after it take in the parse's place (`viewing`), in `unit_arena`. Each declaration's
    // place there: its first token's index and past its last (a skipped one's empty, at the next read token's index),
    // and the read tokens' system levels and the pragmas' positions there. Every other array of the unit's is the
    // parse's, by the parse's token indices.
    bool viewing;
    Parser view;
    uint32_t* view_begins;
    uint32_t* view_ends;
    // The declarations a view reads, in order (`view_read_count` of them), which the reader's tree columns take one after
    // another without a skipped one's state (check_attested_tree_columns).
    uint32_t* view_reads;
    uint32_t view_read_count;
    uint8_t* view_levels;
    uint32_t* view_pragmas;
    // Step 4 (--attested-memo, check_attested_memo_load and check_attested_memo_save): the memo kept across processes, in
    // the file `memo_path` (the directory the option names, the file by `memo_key`: the compiler's build and the command's
    // options). Its hashes are then portable (`portable`): a name's word is its spelling's hash (`id_words`, by IdentId,
    // check_attested_ids) where a lane's own memo takes its id in the lane's table, and a function definition's own hash
    // takes the process's `nonce` beside the lane's stamp. The hashes loaded, and those the memo learned since (`fresh`),
    // which the lane's close appends to the file.
    bool portable;
    // A command of one source file's state (step 4's memo, check_driver_one_unit), whose one unit would only fill the
    // lane's caches of lists and trees (check_attested_lists_cached, check_attested_tree_columns), and whose arenas, a
    // process's for one unit, are base pages throughout: each page it touches costs it a fault, where a huge page would
    // cost it a 2 MB clear. A units' run's lanes and the census keep the caches.
    bool alone;
    uint64_t nonce;
    uint64_t* id_words;
    char* memo_path;
    CheckAttestedHash memo_key;
    uint64_t memo_loaded;
    Arena fresh_arena;
    CheckAttestedHash* fresh;
    uint64_t fresh_count;
    // A command of one source file's memo is its hashes in the order loaded and learned (`memo`, `memo_count` of them; the
    // fresh ones those past `memo_loaded`) and a table of their indexes plus one (`memo_index`, `memo_mask + 1` slots, zero
    // empty) at most half full: about 24 bytes a hash for a process to fault in, where a table of the hashes themselves
    // takes 32 to 64 (check_attested_memo_has_alone).
    uint32_t* memo_index;
    // The lane's units that skipped, those read as their view, and those read again.
    uint64_t units_skipping;
    uint64_t units_viewing;
    uint64_t units_again;
    // --list N: the first N declarations of each outcome but skipped, and every contradiction, by file and line.
    uint32_t list;
    // --passes N: the passes run N times a unit (1 by default), the least cycles of each kept; the first run's results.
    uint32_t passes;
    // --twins: each unit's lists, own hashes and literals made again by the other path and compared (check_attested_twins).
    bool twins;
    uint64_t twin_differences;
    // The memo: a set of 128-bit hashes, open addressing in `memo_mask + 1` slots, a zero slot empty (a hash's low word is
    // never zero).
    Arena memo_arena;
    CheckAttestedHash* memo;
    uint64_t memo_mask;
    uint64_t memo_count;
    // By key: the names' states, and the events' test's marks (check_attested_events): the classes a declaration's list
    // gives the key, with the declaration and the unit's stamp.
    Arena key_arena;
    CheckAttestedKey* keys;
    uint64_t* list_marks;
    // By key: the bodies' marks of their lists' entries (check_attested_list_body), and the lane's last body's number.
    uint64_t* body_marks;
    uint64_t body_stamp;
    // By IdentId: the name's facts (BUSTER_ATTESTED_ID_*), a `__builtin_X` name's X (0 until it is interned), and how
    // many of the table's names have them.
    uint8_t* id_facts;
    uint32_t* builtin_targets;
    uint32_t ids_known;
    // The lists' parts of each parser kind and role (check_attested_actions).
    uint8_t kind_actions[256];
    uint8_t role_actions[256];
    // The lists' cache (CheckAttestedListSlot): its table, in `list_arena`, each size after the last, and its pool; and how
    // many of the lane's declarations it gave a list and made one for.
    Arena list_arena;
    CheckAttestedListSlot* list_slots;
    uint64_t list_mask;
    uint64_t list_count;
    Arena list_pool_arena;
    uint32_t* list_pool;
    uint64_t list_pool_length;
    uint64_t list_hits;
    uint64_t list_misses;
    // The cached declarations' tree columns (check_attested_tree_columns), and how many declarations took theirs from it, had
    // them made, kept them, and had a tree other than the one kept under their key (none: a key's tokens give its tree).
    Arena tree_pool_arena;
    uint16_t* tree_pool;
    uint64_t tree_pool_length;
    uint64_t tree_hits;
    uint64_t tree_made;
    uint64_t tree_kept;
    uint64_t tree_mismatches;
    // By declaration of the unit: its list's block in the pool, or BUSTER_ATTESTED_NONE where the cache holds none.
    uint32_t* list_blocks;
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
    // Each declaration's declared names' keys apart (declared_starts, declared_keys), for the chain to declare without
    // telling them from the names it looks up.
    uint32_t* declared_starts;
    uint32_t* declared_keys;
    CheckAttestedHash* owns;
    // The own hashes' running sums at each declaration's root (the vector path's), with room for a register past the last.
    uint64_t* sums_low;
    uint64_t* sums_high;
    CheckAttestedHash* semantics;
    uint8_t* hits;
    // Each declaration whose reading declared, completed or looked up a name its list doesn't hold, the unit's events of
    // that kind, and whether the reader got to test them (check_attested_events): it doesn't where it failed.
    uint8_t* unlisted;
    uint64_t unit_unlisted_events;
    bool events_checked;
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
    // Whether the unit's tokens' levels are the preprocessor's (check_attested_levels found a system file), for the reader's
    // system tokens (check_attested_token_levels).
    bool levels_ready;
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

// An arena of the census's state: base pages throughout for a command of one source file's (CheckAttested.alone), and
// as arena_reserve gives one otherwise.
static Arena check_attested_reserve(const CheckAttested* census, uint64_t capacity)
{
    return census->alone ? arena_reserve_small(capacity, capacity) : arena_reserve(capacity);
}

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

// Whether a command of one source file's memo holds `hash` (CheckAttested.memo_index): its probe of the index table.
static bool check_attested_memo_has_alone(const CheckAttested* census, CheckAttestedHash hash)
{
    const uint64_t mask = census->memo_mask;
    uint64_t slot = hash.low & mask;
    uint32_t index = census->memo_index[slot];
    bool found = false;
    while (!found && index != 0)
    {
        found = check_attested_equal(census->memo[index - 1], hash);
        slot = (slot + 1) & mask;
        index = census->memo_index[slot];
    }
    return found;
}

// Whether the memo holds `hash`.
static bool check_attested_memo_has(const CheckAttested* census, CheckAttestedHash hash)
{
    bool found = false;
    if (census->alone)
    {
        found = check_attested_memo_has_alone(census, hash);
    }
    else
    {
        const uint64_t mask = census->memo_mask;
        uint64_t slot = hash.low & mask;
        bool probing = true;
        while (probing)
        {
            const CheckAttestedHash entry = census->memo[slot];
            found = check_attested_equal(entry, hash);
            probing = !found && entry.low != 0;
            slot = (slot + 1) & mask;
        }
    }
    return found;
}

// Index `index` (a hash's in a command of one source file's memo, plus one) into an index table of `mask + 1` slots that
// has room for it, by the hash's low word `low`.
static void check_attested_memo_index_put(uint32_t* table, uint64_t mask, uint64_t low, uint32_t index)
{
    uint64_t slot = low & mask;
    while (table[slot] != 0)
    {
        slot = (slot + 1) & mask;
    }
    table[slot] = index;
}

// A command of one source file's memo's index table, of `slots` slots, its hashes' indexes in it: whether there was room.
static bool check_attested_memo_index(CheckAttested* census, uint64_t slots)
{
    uint32_t* const table = arena_push_array(&census->memo_arena, slots, sizeof(uint32_t), 64);
    for (uint64_t h = 0; table != NULL && h < census->memo_count; h += 1)
    {
        check_attested_memo_index_put(table, slots - 1, census->memo[h].low, (uint32_t)h + 1);
    }
    census->memo_index = table != NULL ? table : census->memo_index;
    census->memo_mask = table != NULL ? slots - 1 : census->memo_mask;
    return table != NULL;
}

// A command of one source file's memo with `hash`, its index table doubled first where it is half full: whether it holds
// it (false where it would hold more than BUSTER_ATTESTED_ALONE_HASHES, or the arena has no room).
static bool check_attested_memo_add_alone(CheckAttested* census, CheckAttestedHash hash)
{
    const bool below = census->memo_count < BUSTER_ATTESTED_ALONE_HASHES;
    bool room = below && census->memo_count < (census->memo_mask + 1) / 2;
    if (below && !room && census->memo_mask + 1 < BUSTER_ATTESTED_MEMO_SLOTS)
    {
        room = check_attested_memo_index(census, 2 * (census->memo_mask + 1));
    }
    if (room && !check_attested_memo_has_alone(census, hash))
    {
        census->memo[census->memo_count] = hash;
        census->memo_count += 1;
        check_attested_memo_index_put(census->memo_index, census->memo_mask, hash.low, (uint32_t)census->memo_count);
    }
    return room;
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

// The memo with `hash`, doubled first where it is a quarter full (so that a probe nearly always ends at its first slot,
// one branch the predictor gets right): whether it holds it (false where it would grow past its most slots, or the arena
// has no room).
static bool check_attested_memo_add(CheckAttested* census, CheckAttestedHash hash)
{
    bool room = !census->alone && census->memo_count < (census->memo_mask + 1) / 4;
    if (!census->alone && !room && census->memo_mask + 1 < BUSTER_ATTESTED_MEMO_SLOTS)
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
    const uint64_t before = census->memo_count;
    if (room)
    {
        check_attested_memo_put(census->memo, census->memo_mask, hash, &census->memo_count);
    }
    room = census->alone ? check_attested_memo_add_alone(census, hash) : room;
    // (A hash new to a memo kept across processes is the file's at the lane's close.)
    if (census->memo_count != before && census->fresh != NULL && census->fresh_count < BUSTER_ATTESTED_MEMO_SLOTS)
    {
        census->fresh[census->fresh_count] = hash;
        census->fresh_count += 1;
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
    // (The compile's reach takes no runtime functions' names: only the census looks them up.)
    const uint32_t runtimes = census->production ? 0 : check_attested_runtime_count();
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
        if (census->portable)
        {
            // (Never zero: id 0, no name's, has the word zero.)
            census->id_words[id] = check_attested_bytes(bytes, length) | 1;
        }
    }
    census->ids_known = count > census->ids_known ? count : census->ids_known;
}

// Key `key`'s word in the semantic hashes: the key itself (its IdentId and namespace) for a lane's own memo, and for a memo
// kept across processes its name's spelling's word, its namespace mixed in.
static inline uint64_t check_attested_key_word(const CheckAttested* census, uint32_t key)
{
    return census->portable ? census->id_words[key / 2] ^ ((key & 1) != 0 ? BUSTER_ATTESTED_K3 : 0) : key;
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
    uint32_t* declared_starts;
    uint32_t* declared_keys;
    uint32_t declared_count;
    uint32_t* string_tokens;
    uint32_t* string_declarations;
    uint8_t* stack_roles;
    uint8_t* stack_flags;
    uint8_t* stack_bases;
    // The bodies' marks by key (CheckAttested.body_marks) and the last body's number in the lane.
    uint64_t* body_marks;
    uint64_t body_stamp;
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
            listing->declared_keys[listing->declared_count] = id * 2 + (tag ? 1u : 0u);
            listing->declared_count += (name_class & BUSTER_ATTESTED_DECLARES) != 0 ? 1u : 0u;
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
        listing->declared_starts[declaration + 1] = listing->declared_count;
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
    uint32_t declared = listing->declared_count;
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
            // The declared ones' keys: the callees, or every one in a body with an extern declaration.
            const __mmask16 declaring = externs ? group : called;
            _mm512_mask_compressstoreu_epi32(listing->declared_keys + declared, declaring, _mm512_add_epi32(_mm512_slli_epi32(ids, 1), _mm512_maskz_set1_epi32(tagged, 1)));
            declared += (uint32_t)__builtin_popcount(declaring);
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
        listing->declared_keys[declared] = names[u] * 2 + (tag ? 1u : 0u);
        declared += identifier && (callee || externs) ? 1u : 0u;
        if (asms && kinds[u] == PARSE_KIND_STRING)
        {
            listing->string_tokens[strings] = u;
            listing->string_declarations[strings] = listing->declaration;
            strings += 1;
        }
    }
    listing->declaration_flags |= strings != listing->strings ? BUSTER_ATTESTED_STRINGS : 0;
    // The body's entries each once by key and class (the first, in order): a name a body spells again is the same lookup
    // and the same declaration, and the reach and the events' test then take each once. A key's mark is the body's number
    // and the classes it was seen with (a bit of each of the eight).
    listing->body_stamp += 1;
    const uint64_t owner = listing->body_stamp << 8;
    uint64_t* const marks = listing->body_marks;
    uint32_t kept = listing->names_count;
    for (uint32_t n = listing->names_count; n < count; n += 1)
    {
        const uint32_t entry = listing->name_entries[n];
        const uint32_t key = entry & BUSTER_ATTESTED_KEY_MASK;
        const uint64_t mark = marks[key];
        const uint64_t seen = (mark & ~UINT64_C(0xff)) == owner ? mark & 0xff : 0;
        const uint64_t bit = UINT64_C(1) << (entry >> BUSTER_ATTESTED_CLASS_SHIFT);
        listing->name_entries[kept] = entry;
        kept += (seen & bit) == 0 ? 1u : 0u;
        marks[key] = owner | seen | bit;
    }
    listing->names_count = kept;
    listing->declared_count = declared;
    listing->strings = strings;
}

// The lists of `parser`'s unit into the census's unit arrays, a token at a time: whether there was room. One pass in
// source order, with a stack of the open groups: the scalar path's, and the twin of check_attested_lists_cached, the
// vector path's.
static bool check_attested_lists_walk(CheckAttested* census, const Parser* parser)
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
        .declared_starts = census->declared_starts,
        .declared_keys = census->declared_keys,
        .string_tokens = census->string_tokens,
        .string_declarations = census->string_declarations,
        .stack_roles = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .stack_flags = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .stack_bases = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .body_marks = census->body_marks,
        .body_stamp = census->body_stamp,
    };
    const bool valid = listing.stack_roles != NULL && listing.stack_flags != NULL && listing.stack_bases != NULL;
    if (valid)
    {
        listing.stack_roles[0] = PARSE_ROLE_NONE;
        listing.stack_flags[0] = 0;
        listing.stack_bases[0] = BUSTER_ATTESTED_DECLARES;
        census->name_starts[0] = 0;
        census->declared_starts[0] = 0;
    }
    uint32_t t = 0;
    while (valid && t < count)
    {
        check_attested_list_token(&listing, t, roles[t], kind_actions[kinds[t]] | role_actions[roles[t]], parents[t] == BUSTER_PARSE_ROOT);
        t += 1;
        if (listing.body_end != BUSTER_ATTESTED_NONE)
        {
            check_attested_list_body(&listing, t, listing.body_end, true);
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
        census->declared_starts[declaration] = listing.declared_count;
    }
    // The own hashes' sentinel: no declaration ends past the last.
    census->lasts[declaration] = UINT32_MAX;
    census->declaration_count = declaration;
    census->name_count = listing.names_count;
    census->string_count = listing.strings;
    census->any_definition = listing.any_definition;
    census->body_stamp = listing.body_stamp;
    // (The walk keeps nothing in the lists' cache.)
    memset(census->list_blocks, 0xff, (uint64_t)declaration * sizeof(uint32_t));
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
                // (A name's word: its id, or with a memo kept across processes its spelling's, check_attested_ids.)
                word ^= named && census->portable && output->values[i] < BUSTER_ATTESTED_MAX_IDS ? census->id_words[output->values[i]] : output->values[i];
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

// A token's word's contribution to its declaration's own hash, in one of its two halves (`k0`, `k1`, `k2`): a 64-bit
// mix, two multiplies, the same in a vector lane and in scalar code.
#define BUSTER_ATTESTED_M0 0x9e3779b97f4a7c15ull
#define BUSTER_ATTESTED_M1 0xbf58476d1ce4e5b9ull
#define BUSTER_ATTESTED_M2 0x94d049bb133111ebull
#define BUSTER_ATTESTED_M3 0xd6e8feb86659fd93ull

static inline uint64_t check_attested_mix(uint64_t value, uint64_t first, uint64_t second)
{
    uint64_t x = value * first;
    x ^= x >> 29;
    x *= second;
    x ^= x >> 32;
    return x;
}

// The same in eight lanes.
static inline __m512i check_attested_mix_lanes(__m512i value, uint64_t first, uint64_t second)
{
    __m512i x = _mm512_mullo_epi64(value, _mm512_set1_epi64((int64_t)first));
    x = _mm512_xor_si512(x, _mm512_srli_epi64(x, 29));
    x = _mm512_mullo_epi64(x, _mm512_set1_epi64((int64_t)second));
    return _mm512_xor_si512(x, _mm512_srli_epi64(x, 32));
}

// The declarations' own hashes: a sum over each one's tokens of its token's word (name, system level, role and kind) at
// its place in the declaration (the place times BUSTER_ATTESTED_K3, in the word), mixed into two halves; then its
// literals' spellings' (the sum has no order, so they come after), then its length mixed in. The vector path takes
// eight tokens a register, each one's place from the declaration's first token found by a prefix maximum of the lanes
// that start one, and each declaration's sum as the difference of a running prefix sum at its root and at the last
// one (compressed into `sums` at the roots); the scalar path, its twin, a token at a time. No branch a token. A function
// definition's own hash is zero on both paths, and neither reads its tokens past the block it starts in: its semantic
// hash is its own (check_attested_chain), and its body is a third of musl's tokens.
static __attribute__((noinline)) void check_attested_hashes(CheckAttested* census, const Parser* parser, bool scalar)
{
    const uint8_t* const kinds = parser->tokens.kinds;
    const uint8_t* const roles = parser->tree.roles;
    const uint32_t* const names = parser->tokens.names;
    const uint8_t* const levels = census->token_levels;
    const uint32_t* const parents = parser->tree.parents;
    const uint32_t* const lasts = census->lasts;
    // (A name's spelling's word with a memo kept across processes, check_attested_ids.)
    const uint64_t* const words = census->portable ? census->id_words : NULL;
    CheckAttestedHash* const owns = census->owns;
    const uint8_t* const flags = census->flags;
    const uint32_t declarations = census->declaration_count;
    const uint32_t end = declarations != 0 ? lasts[declarations - 1] + 1 : 0;
    uint32_t d = 0;
    uint32_t t = 0;
    if (!BUSTER_SCALAR_PATH(scalar))
    {
        uint64_t* const sums_low = census->sums_low;
        uint64_t* const sums_high = census->sums_high;
        const __m512i lane = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
        const __m512i zero = _mm512_setzero_si512();
        const __m512i last = _mm512_set1_epi64(7);
        __m512i carry_low = zero;
        __m512i carry_high = zero;
        __m512i carry_first = zero;
        uint32_t previous_root = 1;
        while (t < end)
        {
            // Inside a function definition (`d` is the declaration `t` is in): its sum at its root is the running one, and the
            // next declaration starts a block.
            while (d < declarations && t < end && (flags[d] & BUSTER_ATTESTED_DEFINITION) != 0)
            {
                _mm512_storeu_si512(sums_low + d, carry_low);
                _mm512_storeu_si512(sums_high + d, carry_high);
                t = lasts[d] + 1;
                d += 1;
                previous_root = 1;
            }
            if (t >= end)
            {
                break;
            }
            const uint32_t length = end - t < 8 ? end - t : 8;
            const __mmask8 lanes = (__mmask8)_bzhi_u32(0xffu, length);
            // A name's word: its id above the token's parts, or with a memo kept across processes its spelling's (id 0, no
            // name's, has the word zero: the parts alone).
            const __m256i ids = _mm256_maskz_loadu_epi32(lanes, names + t);
            const __m512i name = words != NULL ? _mm512_mask_i32gather_epi64(zero, lanes, ids, words, 8) : _mm512_slli_epi64(_mm512_cvtepu32_epi64(ids), 32);
            const __m512i kind = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, kinds + t));
            const __m512i role = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, roles + t));
            const __m512i level = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(lanes, levels + t));
            __m512i word = _mm512_xor_si512(name, _mm512_slli_epi64(level, 16));
            word = _mm512_or_si512(word, _mm512_or_si512(_mm512_slli_epi64(role, 8), kind));
            const __mmask8 roots = (__mmask8)(_mm256_cmpeq_epi32_mask(_mm256_maskz_loadu_epi32(lanes, parents + t), _mm256_set1_epi32(-1)) & lanes);
            const __mmask8 starts = (__mmask8)(((uint32_t)roots << 1 | previous_root) & lanes);
            const __m512i token = _mm512_add_epi64(_mm512_set1_epi64(t), lane);
            // Each lane's declaration's first token: the greatest start at or below it in the block, or the last blocks'
            // (the block's own maximum doesn't wait for theirs: one max carries from block to block).
            __m512i local_first = _mm512_maskz_mov_epi64(starts, token);
            local_first = _mm512_max_epu64(local_first, _mm512_alignr_epi64(local_first, zero, 7));
            local_first = _mm512_max_epu64(local_first, _mm512_alignr_epi64(local_first, zero, 6));
            local_first = _mm512_max_epu64(local_first, _mm512_alignr_epi64(local_first, zero, 4));
            const __m512i first = _mm512_max_epu64(local_first, carry_first);
            const __m512i placed = _mm512_xor_si512(word, _mm512_mullo_epi64(_mm512_sub_epi64(token, first), _mm512_set1_epi64((int64_t)BUSTER_ATTESTED_K3)));
            __m512i low = _mm512_maskz_mov_epi64(lanes, check_attested_mix_lanes(_mm512_xor_si512(placed, _mm512_set1_epi64((int64_t)BUSTER_ATTESTED_K0)),
                                                                                 BUSTER_ATTESTED_M0, BUSTER_ATTESTED_M1));
            __m512i high = _mm512_maskz_mov_epi64(lanes, check_attested_mix_lanes(_mm512_xor_si512(placed, _mm512_set1_epi64((int64_t)BUSTER_ATTESTED_K2)),
                                                                                  BUSTER_ATTESTED_M2, BUSTER_ATTESTED_M3));
            // The block's own running sums at each lane, which don't wait for the last blocks': the running sums are them
            // and the carry, one add from block to block.
            low = _mm512_add_epi64(low, _mm512_alignr_epi64(low, zero, 7));
            high = _mm512_add_epi64(high, _mm512_alignr_epi64(high, zero, 7));
            low = _mm512_add_epi64(low, _mm512_alignr_epi64(low, zero, 6));
            high = _mm512_add_epi64(high, _mm512_alignr_epi64(high, zero, 6));
            low = _mm512_add_epi64(low, _mm512_alignr_epi64(low, zero, 4));
            high = _mm512_add_epi64(high, _mm512_alignr_epi64(high, zero, 4));
            // At the roots, the running sums, packed (`sums` has room for a register past the last declaration).
            _mm512_storeu_si512(sums_low + d, _mm512_add_epi64(_mm512_maskz_compress_epi64(roots, low), carry_low));
            _mm512_storeu_si512(sums_high + d, _mm512_add_epi64(_mm512_maskz_compress_epi64(roots, high), carry_high));
            d += (uint32_t)__builtin_popcount(roots);
            carry_low = _mm512_add_epi64(carry_low, _mm512_permutexvar_epi64(last, low));
            carry_high = _mm512_add_epi64(carry_high, _mm512_permutexvar_epi64(last, high));
            carry_first = _mm512_max_epu64(carry_first, _mm512_permutexvar_epi64(last, local_first));
            previous_root = (uint32_t)roots >> (length - 1) & 1;
            t += length;
        }
        // A last declaration that no root ends (a failed parse's view): the sums at the end.
        if (d < declarations)
        {
            _mm512_storeu_si512(sums_low + d, carry_low);
            _mm512_storeu_si512(sums_high + d, carry_high);
            d += 1;
        }
        uint64_t before_low = 0;
        uint64_t before_high = 0;
        for (uint32_t k = 0; k < declarations; k += 1)
        {
            const bool definition = (flags[k] & BUSTER_ATTESTED_DEFINITION) != 0;
            owns[k] = definition ? (CheckAttestedHash){ 0 } : (CheckAttestedHash){ .low = sums_low[k] - before_low, .high = sums_high[k] - before_high };
            before_low = sums_low[k];
            before_high = sums_high[k];
        }
    }
    else
    {
        // The token's place in its declaration times BUSTER_ATTESTED_K3, kept as a running sum.
        uint64_t place = 0;
        uint64_t low = 0;
        uint64_t high = 0;
        for (; t < end; t += 1)
        {
            // A function definition's own hash zero, its tokens unread.
            while (d < declarations && t < end && (flags[d] & BUSTER_ATTESTED_DEFINITION) != 0)
            {
                owns[d] = (CheckAttestedHash){ 0 };
                t = lasts[d] + 1;
                d += 1;
            }
            if (t >= end)
            {
                break;
            }
            const uint64_t name = words != NULL ? words[names[t]] : (uint64_t)names[t] << 32;
            const uint64_t word = name ^ ((uint64_t)levels[t] << 16 | (uint64_t)roles[t] << 8 | kinds[t]);
            const uint64_t placed = word ^ place;
            low += check_attested_mix(placed ^ BUSTER_ATTESTED_K0, BUSTER_ATTESTED_M0, BUSTER_ATTESTED_M1);
            high += check_attested_mix(placed ^ BUSTER_ATTESTED_K2, BUSTER_ATTESTED_M2, BUSTER_ATTESTED_M3);
            // The sum so far, kept as the declaration's own until its last token's is: then the next declaration's.
            owns[d] = (CheckAttestedHash){ .low = low, .high = high };
            const bool ends = parents[t] == BUSTER_PARSE_ROOT;
            d += ends ? 1u : 0u;
            place = ends ? 0 : place + BUSTER_ATTESTED_K3;
            low = ends ? 0 : low;
            high = ends ? 0 : high;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The lists on the vector path: the declarations' bounds, their own hashes, then each one's list from the lane's cache
// (CheckAttestedListSlot), or, where it holds none, made a token at a time as check_attested_lists_walk makes it, and kept.
// A unit's header declarations are the last units' (musl's command finds 95% of its declarations' lists there), and a
// declaration's list is made from its tokens' kinds, roles and names alone, which its own hash covers at their places:
// a hit copies a few entries where the walk stepped through every token with a part.

// The declarations' bounds (each root ends one) and which are function definitions (a body's `{` in it, or a root that
// closes a body, as the walk tells them), 64 tokens at a time: the own hashes pass the definitions by, and the lists
// make every other flag.
static void check_attested_bounds(CheckAttested* census, const Parser* parser)
{
    const uint32_t count = parser->tokens.count;
    const uint8_t* const roles = parser->tree.roles;
    const uint32_t* const parents = parser->tree.parents;
    const __m512i none = _mm512_set1_epi32(-1);
    const __m512i open_body = _mm512_set1_epi8(PARSE_ROLE_OPEN_BODY);
    const __m512i close_body = _mm512_set1_epi8(PARSE_ROLE_CLOSE_BODY);
    uint32_t declaration = 0;
    uint32_t first = 0;
    bool body = false;
    for (uint32_t t = 0; t < count; t += 64)
    {
        const uint32_t length = count - t < 64 ? count - t : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        const __m512i role = _mm512_maskz_loadu_epi8(lanes, roles + t);
        uint64_t roots = 0;
        for (uint32_t g = 0; g < 4; g += 1)
        {
            const __mmask16 group = (__mmask16)(lanes >> (16 * g));
            roots |= (uint64_t)_mm512_mask_cmpeq_epi32_mask(group, _mm512_maskz_loadu_epi32(group, parents + t + 16 * g), none) << (16 * g);
        }
        const uint64_t bodies = _mm512_mask_cmpeq_epi8_mask(lanes, role, open_body);
        const uint64_t closes = _mm512_mask_cmpeq_epi8_mask(lanes, role, close_body) & roots;
        // The block's lanes of the declaration the next root ends.
        uint64_t from = UINT64_MAX;
        while (roots != 0)
        {
            const uint32_t k = (uint32_t)_tzcnt_u64(roots);
            const uint64_t through = k == 63 ? UINT64_MAX : (UINT64_C(2) << k) - 1;
            const bool definition = body || (bodies & from & through) != 0 || (closes >> k & 1) != 0;
            census->firsts[declaration] = first;
            census->lasts[declaration] = t + k;
            census->flags[declaration] = definition ? BUSTER_ATTESTED_DEFINITION : 0;
            declaration += 1;
            first = t + k + 1;
            body = false;
            from = k == 63 ? 0 : UINT64_MAX << (k + 1);
            roots &= roots - 1;
        }
        body |= (bodies & from) != 0;
    }
    if (first < count)
    {
        // Tokens after the last root, which a valid parse doesn't leave: one declaration more, as the walk ends.
        census->firsts[declaration] = first;
        census->lasts[declaration] = count - 1;
        census->flags[declaration] = body ? BUSTER_ATTESTED_DEFINITION : 0;
        declaration += 1;
    }
    census->lasts[declaration] = UINT32_MAX;
    census->declaration_count = declaration;
}

// `count` words from `from` to `to`, 16 at a time by masked moves, which touch nothing past `count` (a list has a few
// words: no call for them).
static inline void check_attested_copy_words(uint32_t* to, const uint32_t* from, uint32_t count)
{
    for (uint32_t k = 0; k < count; k += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, count - k < 16 ? count - k : 16);
        _mm512_mask_storeu_epi32(to + k, lanes, _mm512_maskz_loadu_epi32(lanes, from + k));
    }
}

// How many declarations ahead the cached lists fetch a key's slot.
#define BUSTER_ATTESTED_LIST_AHEAD 4u

// The cache's slot of the list keyed (`low`, `high`, `length`): its own, or the empty slot its probe ends at.
static CheckAttestedListSlot* check_attested_list_slot(const CheckAttested* census, uint64_t low, uint64_t high, uint32_t length)
{
    const uint64_t mask = census->list_mask;
    uint64_t slot = low & mask;
    CheckAttestedListSlot* entry = census->list_slots + slot;
    while (entry->low != 0 && (entry->low != low || entry->high != high || entry->length != length))
    {
        slot = (slot + 1) & mask;
        entry = census->list_slots + slot;
    }
    return entry;
}

// Declaration `d`'s list, just made from its tokens (`first` the first, its strings `strings_from` to `strings_to` of the
// unit's), into the cache under key (`low`, `high`, `length`), which it doesn't hold: where it fits a slot and the pool
// has room, and the table, doubled first where it is a quarter full, isn't at its most slots.
static void check_attested_list_keep(CheckAttested* census, uint64_t low, uint64_t high, uint32_t length, uint32_t d, uint32_t first, uint32_t strings_from,
                                     uint32_t strings_to)
{
    const uint32_t names = census->name_starts[d + 1] - census->name_starts[d];
    const uint32_t declared = census->declared_starts[d + 1] - census->declared_starts[d];
    const uint32_t strings = strings_to - strings_from;
    const uint64_t words = 1 + (uint64_t)names + declared + strings;
    bool room = census->list_slots != NULL && census->list_pool != NULL && names <= UINT16_MAX && declared <= UINT16_MAX && strings <= UINT8_MAX &&
                census->list_pool_length + words <= BUSTER_ATTESTED_LIST_POOL;
    if (room && census->list_count >= (census->list_mask + 1) / 4)
    {
        const uint64_t mask = census->list_mask * 2 + 1;
        CheckAttestedListSlot* table = mask < BUSTER_ATTESTED_LIST_SLOTS ? arena_push_array(&census->list_arena, mask + 1, sizeof(CheckAttestedListSlot), 64) : NULL;
        for (uint64_t slot = 0; table != NULL && slot <= census->list_mask; slot += 1)
        {
            const CheckAttestedListSlot entry = census->list_slots[slot];
            uint64_t at = entry.low & mask;
            while (entry.low != 0 && table[at].low != 0)
            {
                at = (at + 1) & mask;
            }
            table[at] = entry.low != 0 ? entry : table[at];
        }
        room = table != NULL;
        census->list_slots = table != NULL ? table : census->list_slots;
        census->list_mask = table != NULL ? mask : census->list_mask;
    }
    if (room)
    {
        CheckAttestedListSlot* slot = check_attested_list_slot(census, low, high, length);
        census->list_pool[census->list_pool_length] = BUSTER_ATTESTED_NONE;
        census->list_blocks[d] = (uint32_t)census->list_pool_length;
        uint32_t* const pool = census->list_pool + census->list_pool_length + 1;
        memcpy(pool, census->name_entries + census->name_starts[d], (uint64_t)names * sizeof(uint32_t));
        memcpy(pool + names, census->declared_keys + census->declared_starts[d], (uint64_t)declared * sizeof(uint32_t));
        for (uint32_t k = 0; k < strings; k += 1)
        {
            pool[names + declared + k] = census->string_tokens[strings_from + k] - first;
        }
        *slot = (CheckAttestedListSlot){
            .low = low,
            .high = high,
            .length = length,
            .offset = (uint32_t)census->list_pool_length,
            .names = (uint16_t)names,
            .declared = (uint16_t)declared,
            .strings = (uint8_t)strings,
            .flags = census->flags[d],
        };
        census->list_pool_length += words;
        census->list_count += 1;
    }
}

// The lists of `parser`'s unit on the vector path, with the own hashes (check_attested_hashes, which they are keyed by and
// which the scalar path makes after its lists): whether there was room. check_attested_lists_walk is its twin.
static bool check_attested_lists_cached(CheckAttested* census, const Parser* parser)
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
        .declared_starts = census->declared_starts,
        .declared_keys = census->declared_keys,
        .string_tokens = census->string_tokens,
        .string_declarations = census->string_declarations,
        .stack_roles = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .stack_flags = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .stack_bases = arena_push_array(&census->unit_arena, (uint64_t)count + 2, sizeof(uint8_t), 64),
        .body_marks = census->body_marks,
        .body_stamp = census->body_stamp,
    };
    const bool valid = listing.stack_roles != NULL && listing.stack_flags != NULL && listing.stack_bases != NULL;
    if (valid)
    {
        listing.stack_roles[0] = PARSE_ROLE_NONE;
        listing.stack_bases[0] = BUSTER_ATTESTED_DECLARES;
        census->name_starts[0] = 0;
        census->declared_starts[0] = 0;
        check_attested_bounds(census, parser);
        check_attested_hashes(census, parser, false);
    }
    const uint32_t declarations = valid ? census->declaration_count : 0;
    for (uint32_t d = 0; d < declarations; d += 1)
    {
        const uint32_t first = census->firsts[d];
        const uint32_t last = census->lasts[d];
        const bool rooted = parents[last] == BUSTER_PARSE_ROOT;
        // (A definition's own hash is zero: its list is made, never kept. A key's low word is never zero.)
        const bool kept = rooted && (census->flags[d] & BUSTER_ATTESTED_DEFINITION) == 0;
        const uint64_t low = census->owns[d].low | 1;
        const uint64_t high = census->owns[d].high;
        const uint32_t length = last - first + 1;
        // The table is the lane's, out of the first two caches by the next unit: a slot a few declarations on is fetched
        // while this one's list is copied.
        const uint32_t ahead = d + BUSTER_ATTESTED_LIST_AHEAD < declarations ? d + BUSTER_ATTESTED_LIST_AHEAD : d;
        // (A command of one source file's state keeps no cache: each list made, CheckAttested.alone.)
        if (!census->alone)
        {
            __builtin_prefetch(census->list_slots + ((census->owns[ahead].low | 1) & census->list_mask));
        }
        CheckAttestedListSlot* slot = kept && !census->alone ? check_attested_list_slot(census, low, high, length) : NULL;
        census->list_blocks[d] = BUSTER_ATTESTED_NONE;
        if (slot != NULL && slot->low != 0)
        {
            census->list_blocks[d] = slot->offset;
            const uint32_t* const pool = census->list_pool + slot->offset + 1;
            check_attested_copy_words(listing.name_entries + listing.names_count, pool, slot->names);
            listing.names_count += slot->names;
            check_attested_copy_words(listing.declared_keys + listing.declared_count, pool + slot->names, slot->declared);
            listing.declared_count += slot->declared;
            for (uint32_t k = 0; k < slot->strings; k += 1)
            {
                listing.string_tokens[listing.strings] = first + pool[slot->names + slot->declared + k];
                listing.string_declarations[listing.strings] = d;
                listing.strings += 1;
            }
            census->flags[d] = slot->flags;
            census->name_starts[d + 1] = listing.names_count;
            census->declared_starts[d + 1] = listing.declared_count;
            census->list_hits += 1;
        }
        else
        {
            const uint32_t strings_from = listing.strings;
            listing.declaration = d;
            listing.first = first;
            listing.depth = 0;
            listing.symbol_groups = 0;
            listing.declaration_flags = 0;
            listing.stack_flags[0] = 0;
            uint32_t t = first;
            while (t <= last)
            {
                check_attested_list_token(&listing, t, roles[t], kind_actions[kinds[t]] | role_actions[roles[t]], parents[t] == BUSTER_PARSE_ROOT);
                t += 1;
                if (listing.body_end != BUSTER_ATTESTED_NONE)
                {
                    check_attested_list_body(&listing, t, listing.body_end, false);
                    t = listing.body_end;
                    listing.body_end = BUSTER_ATTESTED_NONE;
                }
            }
            if (!rooted)
            {
                // The tokens after the last root (check_attested_bounds), as the walk ends.
                census->flags[d] = listing.declaration_flags;
                census->name_starts[d + 1] = listing.names_count;
                census->declared_starts[d + 1] = listing.declared_count;
            }
            census->list_misses += 1;
            if (slot != NULL)
            {
                check_attested_list_keep(census, low, high, length, d, first, strings_from, listing.strings);
            }
        }
    }
    census->name_count = listing.names_count;
    census->string_count = listing.strings;
    census->any_definition = listing.any_definition;
    census->body_stamp = listing.body_stamp;
    arena_pop_to(&census->unit_arena, mark);
    return valid;
}

// The lists (and on the vector path the own hashes before them): check_attested_lists_cached, or on the scalar path its
// twin, check_attested_lists_walk.
static __attribute__((noinline)) bool check_attested_lists(CheckAttested* census, const Parser* parser, bool scalar)
{
    return BUSTER_SCALAR_PATH(scalar) ? check_attested_lists_walk(census, parser) : check_attested_lists_cached(census, parser);
}

// The own hashes' literals: each one's word added to its declaration's sum (a cursor that only rises, as the literals
// do), then every declaration's sum finished with its length. A function definition's literals are spelled by neither
// path (its own hash is zero, check_attested_hashes): a block starting in one goes on at its end.
static __attribute__((noinline)) void check_attested_literals(CheckAttested* census, const Parser* parser, Preprocessor* pp, bool scalar)
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
    const uint8_t* const flags = census->flags;
    uint32_t at = 0;
    uint32_t t = 0;
    while (t < end)
    {
        while (lasts[at] < t)
        {
            at += 1;
        }
        if ((flags[at] & BUSTER_ATTESTED_DEFINITION) != 0)
        {
            t = lasts[at] + 1;
            continue;
        }
        const uint32_t length = end - t < 64 ? end - t : 64;
        const __mmask64 lanes = length == 64 ? UINT64_MAX : _bzhi_u64(UINT64_MAX, length);
        uint64_t literals = 0;
        if (!BUSTER_SCALAR_PATH(scalar))
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
            if ((flags[at] & BUSTER_ATTESTED_DEFINITION) == 0)
            {
                check_attested_literal_word(census, pp, sources[token], token - firsts[at], &ranges, owns + at);
                census->literal_count += 1;
            }
            literals &= literals - 1;
        }
        t += length;
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
    state->hash = check_attested_mum(before ^ by.low ^ BUSTER_ATTESTED_K0, by.high ^ check_attested_key_word(census, key) ^ BUSTER_ATTESTED_K1) | 1;
    state->local = current ? state->local : BUSTER_ATTESTED_NONE;
    state->stamp = census->stamp;
}

// Name `key` declared by declaration `d` of semantic hash `by`, as check_attested_declare declares it, and `d` among its
// declarers, the last of them yet: the reach's list, in source order. No branch: a name's first declarer in the unit
// takes a new list (the next of `*distinct`), chosen as any other.
static void check_attested_declarer(CheckAttested* census, uint32_t key, CheckAttestedHash by, uint32_t d, uint32_t* entries, uint32_t* distinct)
{
    CheckAttestedKey* state = census->keys + key;
    const bool current = state->stamp == census->stamp;
    const uint64_t before = current ? state->hash : 0;
    const uint32_t had = current ? state->local : BUSTER_ATTESTED_NONE;
    const bool fresh = had == BUSTER_ATTESTED_NONE;
    const uint32_t local = fresh ? *distinct : had;
    CheckAttestedList* list = census->lists + local;
    const uint32_t entry = *entries;
    const uint32_t last = fresh ? entry : list->last;
    census->entry_declarations[entry] = d;
    census->entry_next[entry] = BUSTER_ATTESTED_NONE;
    census->entry_previous[entry] = fresh ? BUSTER_ATTESTED_NONE : last;
    // The last declarer's next is this one (a fresh list's own entry's stays none).
    census->entry_next[last] = fresh ? BUSTER_ATTESTED_NONE : entry;
    list->first = fresh ? entry : list->first;
    list->count = fresh ? 1 : list->count + 1;
    list->last = entry;
    state->hash = check_attested_mum(before ^ by.low ^ BUSTER_ATTESTED_K0, by.high ^ check_attested_key_word(census, key) ^ BUSTER_ATTESTED_K1) | 1;
    state->local = local;
    state->stamp = census->stamp;
    *distinct += fresh ? 1u : 0u;
    *entries = entry + 1;
}

// How many entries ahead the chain fetches a name's state.
#define BUSTER_ATTESTED_CHAIN_AHEAD 8u

static __attribute__((noinline)) void check_attested_chain(CheckAttested* census)
{
    const uint32_t declarations = census->declaration_count;
    const uint32_t* const name_starts = census->name_starts;
    const uint32_t* const name_entries = census->name_entries;
    const uint32_t name_count = census->name_count;
    CheckAttestedHash ambient = { .low = BUSTER_ATTESTED_K1, .high = BUSTER_ATTESTED_K0 };
    uint32_t pragma = 0;
    uint32_t entries = 0;
    // List 0 is the reach's empty one, every name's without a declarer.
    uint32_t distinct = 1;
    census->lists[0] = (CheckAttestedList){ .first = BUSTER_ATTESTED_NONE, .last = BUSTER_ATTESTED_NONE };
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
        // place in the list (no chain from one name to the next), then the sum mixed in. A function definition's is its own
        // in the lane, from the unit's stamp and its place: it is never attested, so its hash is only what the names it
        // declares take, and any later declaration that lists one of them then misses, which reads it. That takes from
        // the memo nothing it attests, and from the passes its body's literals and names (a third of musl's tokens).
        const CheckAttestedHash own = census->owns[d];
        const bool definition = (census->flags[d] & BUSTER_ATTESTED_DEFINITION) != 0;
        const uint32_t start = name_starts[d];
        const uint32_t end = definition ? start : name_starts[d + 1];
        uint64_t low = 0;
        uint64_t high = 0;
        for (uint32_t n = start; n < end; n += 1)
        {
            // The names' states are the lane's, mostly out of the first two caches by the time the next unit lists them: a
            // state a few entries on is fetched while this one is mixed.
            const uint32_t ahead = n + BUSTER_ATTESTED_CHAIN_AHEAD < name_count ? name_entries[n + BUSTER_ATTESTED_CHAIN_AHEAD] : name_entries[n];
            __builtin_prefetch(census->keys + (ahead & BUSTER_ATTESTED_KEY_MASK));
            const uint32_t entry = name_entries[n];
            const bool listed = (entry >> BUSTER_ATTESTED_CLASS_SHIFT & (BUSTER_ATTESTED_DECLARES | BUSTER_ATTESTED_LOOKS_UP)) != 0;
            const uint64_t name = check_attested_name_hash(census, entry & BUSTER_ATTESTED_KEY_MASK);
            const uint64_t placed = check_attested_key_word(census, entry & BUSTER_ATTESTED_KEY_MASK) ^ (entry & ~BUSTER_ATTESTED_KEY_MASK) ^ (uint64_t)(n - start) << 32;
            low += listed ? check_attested_mum(name ^ placed ^ BUSTER_ATTESTED_K0, BUSTER_ATTESTED_K1) : 0;
            high += listed ? check_attested_mum(name ^ placed ^ BUSTER_ATTESTED_K2, BUSTER_ATTESTED_K3) : 0;
        }
        const CheckAttestedHash mixed = check_attested_step((CheckAttestedHash){ .low = own.low ^ ambient.low ^ low, .high = own.high ^ ambient.high ^ high }, end - start);
        const CheckAttestedHash unique = check_attested_step((CheckAttestedHash){ .low = BUSTER_ATTESTED_K2 ^ ((uint64_t)census->stamp << 32 | d), .high = BUSTER_ATTESTED_K1 ^ census->nonce },
                                                             BUSTER_ATTESTED_K0);
        const CheckAttestedHash hash = definition ? unique : mixed;
        census->semantics[d] = hash;
        const bool eligible = (census->flags[d] & (BUSTER_ATTESTED_DEFINITION | BUSTER_ATTESTED_PRAGMA)) == 0;
        census->hits[d] = eligible && check_attested_memo_has(census, hash);
        for (uint32_t k = census->declared_starts[d]; k < census->declared_starts[d + 1]; k += 1)
        {
            check_attested_declarer(census, census->declared_keys[k], hash, d, &entries, &distinct);
        }
    }
    census->distinct_count = distinct;
}

// ---------------------------------------------------------------------------------------------------------------------
// The reach: the declarations a root reaches, a root being a definition, a pragma, a declaration the memo doesn't attest
// and, with `runtime`, the unit's end spelling every runtime function's name. A reached declaration reaches every earlier
// declaration of a name it spells (a key's rising cursor) and every later declaration of a name it declares (its falling
// cursor): each cursor passes each of the key's declarers once.

// Declaration `declaration` reached: on the worklist where it wasn't (written at every call, kept where it is new).
static void check_attested_reach_one(CheckAttested* census, uint8_t* reached, uint32_t* pending, uint32_t declaration)
{
    census->worklist[*pending] = declaration;
    *pending += reached[declaration] == 0 ? 1u : 0u;
    reached[declaration] = 1;
}

// Name `key`'s declarers' list in the unit, or list 0, the empty one, where it has none.
static CheckAttestedList* check_attested_list_of(CheckAttested* census, uint32_t key)
{
    const CheckAttestedKey* state = census->keys + (key < 2 * BUSTER_ATTESTED_MAX_IDS ? key : 0);
    const bool listed = key < 2 * BUSTER_ATTESTED_MAX_IDS && state->stamp == census->stamp && state->local != BUSTER_ATTESTED_NONE;
    return census->lists + (listed ? state->local : 0);
}

// Every declarer of `key` before declaration `position`.
static void check_attested_rise(CheckAttested* census, uint8_t* reached, uint32_t* pending, uint32_t key, uint32_t position)
{
    CheckAttestedList* list = check_attested_list_of(census, key);
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

// Every declarer of `key` after declaration `position`.
static void check_attested_fall(CheckAttested* census, uint8_t* reached, uint32_t* pending, uint32_t key, uint32_t position)
{
    CheckAttestedList* list = check_attested_list_of(census, key);
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

// The cursors at their lists' ends.
static void check_attested_cursors(CheckAttested* census)
{
    // (List 0, the empty one, keeps nothing between its cursors.)
    for (uint32_t k = 1; k < census->distinct_count; k += 1)
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
// The marks: the declarations attested and not reached (skipped), and their tokens, counted; and where the reader takes the
// parse rather than its view (check_attested_view had no room), a bit for each of their tokens, 64 tokens a word
// (check_attested_mark_bits).

static __attribute__((noinline)) void check_attested_marks(CheckAttested* census)
{
    uint32_t skipped = 0;
    uint64_t skipped_tokens = 0;
    for (uint32_t d = 0; d < census->declaration_count; d += 1)
    {
        const bool skip = census->hits[d] != 0 && census->reached[d] == 0;
        skipped += skip ? 1u : 0u;
        skipped_tokens += skip ? census->lasts[d] - census->firsts[d] + 1 : 0;
    }
    census->skipped_declarations = skipped;
    census->skipped_tokens = skipped_tokens;
}

static void check_attested_mark_bits(CheckAttested* census)
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
// The unit before its reader: its arrays, the passes timed, then the reader.

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
    census->declared_starts = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    // (Room for a register past the last: the body's vector path stores whole registers.)
    census->declared_keys = arena_push_array(arena, slots + 16, sizeof(uint32_t), 64);
    census->owns = arena_push_array(arena, slots, sizeof(CheckAttestedHash), 64);
    census->semantics = arena_push_array(arena, slots, sizeof(CheckAttestedHash), 64);
    census->sums_low = arena_push_array(arena, slots + 8, sizeof(uint64_t), 64);
    census->sums_high = arena_push_array(arena, slots + 8, sizeof(uint64_t), 64);
    census->hits = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->list_blocks = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->unlisted = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->reached = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->reached_without_runtime = arena_push_array(arena, slots, sizeof(uint8_t), 64);
    census->worklist = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->entry_declarations = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->entry_next = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->entry_previous = arena_push_array(arena, slots, sizeof(uint32_t), 64);
    census->lists = arena_push_array(arena, slots + 1, sizeof(CheckAttestedList), 64);
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
    census->levels_ready = false;
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
        const uint32_t limit = pp->output.count;
        census->levels[limit] = BUSTER_PP_USER_FILE;
        memset(census->levels + limit + 1, 0, 3);
        const uint32_t count = parser->tokens.count;
        const uint32_t* const sources = parser->tokens.sources;
        uint32_t t = 0;
        // The vector path 16 tokens at a time, as the reader's system tokens map them (check_input_system_token_map): their
        // sources, clamped to the user file's entry past the last, gather four bytes each, of which the low one is the
        // level (`levels` has three bytes past that entry); the loop after it, the scalar path, is its twin and takes the
        // last tokens.
        for (; !BUSTER_SCALAR_PATH(parser->options.scalar) && count - t >= 16; t += 16)
        {
            const __m512i source = _mm512_min_epu32(_mm512_loadu_si512(sources + t), _mm512_set1_epi32((int32_t)limit));
            const __m512i level = _mm512_i32gather_epi32(source, census->levels, 1);
            _mm_storeu_si128((__m128i*)(census->token_levels + t), _mm512_cvtepi32_epi8(level));
        }
        for (; t < count; t += 1)
        {
            const uint32_t source = sources[t];
            census->token_levels[t] = census->levels[source < limit ? source : limit];
        }
        census->levels_ready = true;
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
    const bool scalar = parser->options.scalar;
    const bool listed = check_attested_lists(census, parser, scalar) && check_attested_pragmas(census, parser, pp);
    times[1] = __rdtsc();
    check_attested_counts(census, laps[1]);
    // (The vector path's lists made the own hashes first, their key: its own-hashes pass is in its lists'.)
    if (listed && BUSTER_SCALAR_PATH(scalar))
    {
        check_attested_hashes(census, parser, scalar);
    }
    times[2] = __rdtsc();
    check_attested_counts(census, laps[2]);
    if (listed)
    {
        check_attested_literals(census, parser, pp, scalar);
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

// --twins: the lists, the own hashes and their literals made again by the other path, the vector one's or its scalar
// twin's, compared with the passes' (CheckAttested.twin_differences counts the declarations whose range, flags, names
// or own hash differ, and a unit whose counts differ). Their arrays are then the other path's, the same where nothing
// differs. Untimed.
static void check_attested_twins(CheckAttested* census, const Parser* parser, Preprocessor* pp)
{
    const uint32_t declarations = census->declaration_count;
    const uint32_t names = census->name_count;
    const uint32_t strings = census->string_count;
    const uint64_t mark = census->unit_arena.length;
    uint32_t* firsts = arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64);
    uint32_t* lasts = arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64);
    uint8_t* flags = arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(uint8_t), 64);
    uint32_t* starts = arena_push_array(&census->unit_arena, (uint64_t)declarations + 2, sizeof(uint32_t), 64);
    uint32_t* entries = arena_push_array(&census->unit_arena, (uint64_t)names + 1, sizeof(uint32_t), 64);
    uint32_t* texts = arena_push_array(&census->unit_arena, (uint64_t)strings + 1, sizeof(uint32_t), 64);
    CheckAttestedHash* owns = arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(CheckAttestedHash), 64);
    // (The reader's tree columns take the lists' blocks of the path that runs first: the twin's are its own.)
    uint32_t* blocks = arena_push_array(&census->unit_arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64);
    const bool room = firsts != NULL && lasts != NULL && flags != NULL && starts != NULL && entries != NULL && texts != NULL && owns != NULL && blocks != NULL;
    if (room)
    {
        memcpy(firsts, census->firsts, (uint64_t)declarations * sizeof(uint32_t));
        memcpy(lasts, census->lasts, (uint64_t)declarations * sizeof(uint32_t));
        memcpy(flags, census->flags, declarations);
        memcpy(starts, census->name_starts, ((uint64_t)declarations + 1) * sizeof(uint32_t));
        memcpy(entries, census->name_entries, (uint64_t)names * sizeof(uint32_t));
        memcpy(texts, census->string_tokens, (uint64_t)strings * sizeof(uint32_t));
        memcpy(owns, census->owns, (uint64_t)declarations * sizeof(CheckAttestedHash));
        memcpy(blocks, census->list_blocks, (uint64_t)declarations * sizeof(uint32_t));
        const bool other = !parser->options.scalar;
        const bool listed = check_attested_lists(census, parser, other) && check_attested_pragmas(census, parser, pp);
        if (listed)
        {
            if (BUSTER_SCALAR_PATH(other))
            {
                check_attested_hashes(census, parser, other);
            }
            check_attested_literals(census, parser, pp, other);
        }
        const bool same = listed && census->declaration_count == declarations && census->name_count == names && census->string_count == strings;
        census->twin_differences += same ? 0 : 1;
        for (uint32_t d = 0; same && d < declarations; d += 1)
        {
            bool equal = census->firsts[d] == firsts[d] && census->lasts[d] == lasts[d] && census->flags[d] == flags[d] && census->name_starts[d] == starts[d] &&
                         check_attested_equal(census->owns[d], owns[d]);
            for (uint32_t n = starts[d]; equal && n < starts[d + 1]; n += 1)
            {
                equal = census->name_entries[n] == entries[n];
            }
            census->twin_differences += equal ? 0 : 1;
        }
        for (uint32_t k = 0; same && k < strings; k += 1)
        {
            census->twin_differences += census->string_tokens[k] == texts[k] ? 0 : 1;
        }
        memcpy(census->list_blocks, blocks, (uint64_t)declarations * sizeof(uint32_t));
    }
    census->twin_differences += room ? 0 : 1;
    arena_pop_to(&census->unit_arena, mark);
}

// check_driver_unit's reader for a census's unit: the passes, then check_input_run as check_input_compile calls it, which
// tests each reading's binding events as it ends (check_attested_events). A view of a failed parse runs no passes.
static bool check_attested_input(CheckAttested* census, CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch,
                                 bool complete_translation_unit, uint32_t original_end, const uint8_t* deferred_tokens, const char* path)
{
    const uint32_t count = parser->tokens.count;
    census->unit_ready = false;
    census->levels_ready = false;
    census->unit_tree_mismatches = 0;
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
        if (census->twins && listed)
        {
            check_attested_twins(census, parser, pp);
        }
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
    census->events_checked = false;
    return check_input_run(input, parser, pp, types, arena, scratch, complete_translation_unit, original_end, deferred_tokens, CHECK_INPUT_BINDINGS_DEFAULT, 0, false, true,
                           true, census);
}

// ---------------------------------------------------------------------------------------------------------------------
// Step 4: a memo kept across processes (--attested-memo=DIRECTORY). A file in the directory a build names for each key:
// the compiler's build (its GNU build ID, which the baked build makes from the executable's bytes) and the command's
// options but for its inputs, outputs, lanes and reports (check_attested_memo_key), so that every hash a file holds was
// made by this compiler reading as this command reads. The file is a header (BUSTER_ATTESTED_MEMO_MAGIC and the key) and
// 16-byte records, each a hash a reading attested: a process loads them into each lane's memo as the lane opens, and the
// lane appends what it learned as it closes, under the file's lock (flock), so that the processes of a build share it. A
// record two processes wrote is one hash in a memo. The hashes are portable (check_attested_ids): a name's by its
// spelling, and a function definition's own by the process's nonce, which no other process's hash has. A file whose
// header isn't this key's, or that can't be read, is a memo of nothing; one past BUSTER_ATTESTED_MEMO_RECORDS records is
// written again with its writer's memo alone.

#define BUSTER_ATTESTED_MEMO_MAGIC "BUSTATT1"
#define BUSTER_ATTESTED_MEMO_HEADER 32u
#define BUSTER_ATTESTED_MEMO_RECORDS (UINT64_C(1) << 20)

typedef struct CheckAttestedBuild CheckAttestedBuild;
struct CheckAttestedBuild
{
    CheckAttestedHash hash;
    bool found;
};

// The program's own GNU build ID, by its program headers in memory (the auxiliary vector's AT_PHDR, its load base from its
// PT_PHDR), into `build`.
static void check_attested_build_note(CheckAttestedBuild* build)
{
    const Elf64_Phdr* const headers = (const Elf64_Phdr*)getauxval(AT_PHDR);
    const uint64_t count = getauxval(AT_PHNUM);
    uintptr_t base = 0;
    bool based = false;
    for (uint64_t h = 0; headers != NULL && h < count; h += 1)
    {
        based |= headers[h].p_type == PT_PHDR;
        base = headers[h].p_type == PT_PHDR ? (uintptr_t)headers - headers[h].p_vaddr : base;
    }
    for (uint64_t h = 0; based && h < count; h += 1)
    {
        const uint8_t* at = headers[h].p_type == PT_NOTE ? (const uint8_t*)(base + headers[h].p_vaddr) : NULL;
        const uint8_t* const end = at != NULL ? at + headers[h].p_memsz : NULL;
        while (at != NULL && at + sizeof(Elf64_Nhdr) <= end)
        {
            const Elf64_Nhdr* note = (const Elf64_Nhdr*)at;
            const uint8_t* const name = at + sizeof(Elf64_Nhdr);
            const uint8_t* const descriptor = name + ((note->n_namesz + 3) & ~3u);
            const uint8_t* const next = descriptor + ((note->n_descsz + 3) & ~3u);
            if (next <= end && note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && memcmp(name, "GNU", 4) == 0 && note->n_descsz != 0)
            {
                const uint64_t word = check_attested_bytes(descriptor, note->n_descsz);
                build->hash = check_attested_step((CheckAttestedHash){ .low = word, .high = word ^ BUSTER_ATTESTED_K1 }, note->n_descsz);
                build->found = true;
            }
            at = next <= end ? next : NULL;
        }
    }
}

// Whether `argument` is one of a command's that changes no unit's reading: its inputs (`inputs`), outputs and
// dependency files, its lanes, the stages' report, and the memo's own directory; `*valued` where its value is the next
// argument.
static bool check_attested_memo_ignored(const char* argument, char* const* inputs, uint32_t input_count, bool* valued)
{
    static const char* const spaced[] = { "-o", "-MF", "-MT", "-MQ", "--lanes", "--lane-cpus" };
    static const char* const alone[] = { "-MD", "-MMD", "-MP", "--time-stages" };
    static const char* const joined[] = { "--lanes=", "--lane-cpus=", "--attested-memo=", "--units-memory=", "-MF", "-MT", "-MQ" };
    bool ignored = false;
    *valued = false;
    for (uint32_t i = 0; !ignored && i < input_count; i += 1)
    {
        ignored = argument == inputs[i];
    }
    for (uint32_t i = 0; !ignored && i < BUSTER_ARRAY_LENGTH(spaced); i += 1)
    {
        *valued = strcmp(argument, spaced[i]) == 0;
        ignored = *valued;
    }
    for (uint32_t i = 0; !ignored && i < BUSTER_ARRAY_LENGTH(alone); i += 1)
    {
        ignored = strcmp(argument, alone[i]) == 0;
    }
    for (uint32_t i = 0; !ignored && i < BUSTER_ARRAY_LENGTH(joined); i += 1)
    {
        ignored = strncmp(argument, joined[i], strlen(joined[i])) == 0;
    }
    return ignored;
}

// The program's executable file's identity, where its link has no build ID (the CI toolchain's lld adds none): its
// device, inode, size and modification time, which a build or a copy changes, into `build`.
static void check_attested_build_file(CheckAttestedBuild* build)
{
    struct stat status;
    if (stat("/proc/self/exe", &status) == 0)
    {
        const uint64_t words[5] = { (uint64_t)status.st_dev, (uint64_t)status.st_ino, (uint64_t)status.st_size, (uint64_t)status.st_mtim.tv_sec,
                                    (uint64_t)status.st_mtim.tv_nsec };
        CheckAttestedHash hash = { .low = BUSTER_ATTESTED_K0, .high = BUSTER_ATTESTED_K2 };
        for (uint32_t w = 0; w < BUSTER_ARRAY_LENGTH(words); w += 1)
        {
            hash = check_attested_step(hash, words[w]);
        }
        build->hash = hash;
        build->found = true;
    }
}

// A memo's key (`key`, two words) for the command of `count` arguments whose inputs are `inputs`: the compiler's build
// (its GNU build ID, or without one its executable file's identity) and every argument but those
// check_attested_memo_ignored leaves out, in order. Whether the build has an identity: without one there is no key, and
// no memo kept across processes.
static bool check_attested_memo_key(int count, char** arguments, char* const* inputs, uint32_t input_count, uint64_t* key)
{
    CheckAttestedBuild build = { 0 };
    check_attested_build_note(&build);
    if (!build.found)
    {
        check_attested_build_file(&build);
    }
    CheckAttestedHash hash = build.hash;
    for (int i = 0; build.found && i < count; i += 1)
    {
        bool valued = false;
        const bool ignored = check_attested_memo_ignored(arguments[i], inputs, input_count, &valued);
        hash = ignored ? hash : check_attested_step(hash, check_attested_bytes((const uint8_t*)arguments[i], (uint32_t)strlen(arguments[i])) ^ (uint64_t)i << 48);
        i += valued ? 1 : 0;
    }
    key[0] = hash.low;
    key[1] = hash.high;
    return build.found;
}

// A command of one source file's memo from the open file `file`'s `records` records, after its header: read whole into
// the memo's array by one call (its pages the array's own), then each valid record that isn't there yet kept in turn,
// its index in a table sized once for them and a unit's more, half full. Whether they could be read.
static bool check_attested_memo_load_alone(CheckAttested* census, int file, uint64_t records)
{
    uint64_t slots = BUSTER_ATTESTED_MEMO_FIRST_SLOTS;
    while (slots < BUSTER_ATTESTED_MEMO_SLOTS && slots / 2 < records + records / 8 + 64)
    {
        slots *= 2;
    }
    const ssize_t length = (ssize_t)(records * sizeof(CheckAttestedHash));
    bool valid = census->memo_count == 0 && records <= BUSTER_ATTESTED_ALONE_HASHES &&
                 pread(file, census->memo, (size_t)length, BUSTER_ATTESTED_MEMO_HEADER) == length;
    valid = valid && (slots == census->memo_mask + 1 || check_attested_memo_index(census, slots));
    uint64_t kept = 0;
    for (uint64_t r = 0; valid && r < records; r += 1)
    {
        const CheckAttestedHash hash = census->memo[r];
        if ((hash.low & 1) != 0 && !check_attested_memo_has_alone(census, hash))
        {
            census->memo[kept] = hash;
            kept += 1;
            census->memo_count = kept;
            check_attested_memo_index_put(census->memo_index, census->memo_mask, hash.low, (uint32_t)kept);
        }
    }
    census->memo_loaded = census->memo_count;
    return valid;
}

// The memo's file into `census->memo` (counted in memo_loaded): its header checked against the key, then each whole
// record. Whether the file was there with this key's header.
static bool check_attested_memo_load(CheckAttested* census)
{
    const int file = open(census->memo_path, O_RDONLY | O_CLOEXEC);
    const bool locked = file >= 0 && flock(file, LOCK_SH) == 0;
    struct stat status;
    const bool sized = locked && fstat(file, &status) == 0 && status.st_size >= BUSTER_ATTESTED_MEMO_HEADER;
    const uint64_t records = sized ? ((uint64_t)status.st_size - BUSTER_ATTESTED_MEMO_HEADER) / sizeof(CheckAttestedHash) : 0;
    uint8_t header[BUSTER_ATTESTED_MEMO_HEADER];
    uint64_t key[2] = { 0 };
    bool valid = sized && records <= BUSTER_ATTESTED_MEMO_RECORDS && pread(file, header, sizeof(header), 0) == (ssize_t)sizeof(header);
    if (valid)
    {
        memcpy(key, header + 8, sizeof(key));
    }
    valid = valid && memcmp(header, BUSTER_ATTESTED_MEMO_MAGIC, 8) == 0 && key[0] == census->memo_key.low && key[1] == census->memo_key.high;
    if (valid && census->alone)
    {
        valid = check_attested_memo_load_alone(census, file, records);
    }
    else if (valid)
    {
        // The table at the size its records and a unit's more take, at once, past the sizes doubling would leave in its
        // arena: a quarter full.
        uint64_t slots = BUSTER_ATTESTED_MEMO_FIRST_SLOTS;
        while (slots < BUSTER_ATTESTED_MEMO_SLOTS && slots / 4 < records + records / 8 + 256)
        {
            slots *= 2;
        }
        CheckAttestedHash* const table = slots != census->memo_mask + 1 ? arena_push_array(&census->memo_arena, slots, sizeof(CheckAttestedHash), 64) : NULL;
        census->memo = table != NULL ? table : census->memo;
        census->memo_mask = table != NULL ? slots - 1 : census->memo_mask;
        // The records a block at a time through a buffer on the stack (no copy of the file to fault in), each into the
        // memo: loaded, not learned, so that none is fresh.
        CheckAttestedHash* const fresh = census->fresh;
        census->fresh = NULL;
        CheckAttestedHash block[256];
        for (uint64_t r = 0; valid && r < records; r += BUSTER_ARRAY_LENGTH(block))
        {
            const uint64_t count = records - r < BUSTER_ARRAY_LENGTH(block) ? records - r : BUSTER_ARRAY_LENGTH(block);
            const ssize_t length = (ssize_t)(count * sizeof(CheckAttestedHash));
            valid = pread(file, block, (size_t)length, (off_t)(BUSTER_ATTESTED_MEMO_HEADER + r * sizeof(CheckAttestedHash))) == length;
            for (uint64_t k = 0; valid && k < count; k += 1)
            {
                const bool room = (block[k].low & 1) != 0 && check_attested_memo_add(census, block[k]);
                census->memo_loaded += room ? 1 : 0;
            }
        }
        census->fresh = fresh;
    }
    if (locked)
    {
        flock(file, LOCK_UN);
    }
    if (file >= 0)
    {
        close(file);
    }
    return valid;
}

// The hashes the lane's memo learned since it was loaded, appended to the memo's file under its lock; or, where the file
// is new, empty, torn or not this key's (its name is), or would grow past BUSTER_ATTESTED_MEMO_RECORDS, the file written
// again with the header and the memo whole.
static void check_attested_memo_save(CheckAttested* census)
{
    // (A command of one source file's fresh hashes are its memo's past those loaded.)
    const uint64_t fresh_count = census->alone ? census->memo_count - census->memo_loaded : census->fresh_count;
    const char* const slash = census->memo_path != NULL && fresh_count != 0 ? strrchr(census->memo_path, '/') : NULL;
    if (slash != NULL)
    {
        // The directory, made where it isn't there yet.
        char directory[PATH_MAX];
        const size_t length = (size_t)(slash - census->memo_path);
        if (length < sizeof(directory))
        {
            memcpy(directory, census->memo_path, length);
            directory[length] = 0;
            mkdir(directory, 0777);
        }
    }
    const int file = slash != NULL ? open(census->memo_path, O_RDWR | O_CREAT | O_CLOEXEC, 0666) : -1;
    const bool locked = file >= 0 && flock(file, LOCK_EX) == 0;
    struct stat status;
    const bool sized = locked && fstat(file, &status) == 0;
    uint8_t header[BUSTER_ATTESTED_MEMO_HEADER] = { 0 };
    memcpy(header, BUSTER_ATTESTED_MEMO_MAGIC, 8);
    memcpy(header + 8, &census->memo_key.low, 8);
    memcpy(header + 16, &census->memo_key.high, 8);
    uint8_t found[BUSTER_ATTESTED_MEMO_HEADER] = { 0 };
    const bool ours = sized && status.st_size >= BUSTER_ATTESTED_MEMO_HEADER && pread(file, found, sizeof(found), 0) == (ssize_t)sizeof(found) &&
                      memcmp(found, header, sizeof(header)) == 0;
    const uint64_t records = ours ? ((uint64_t)status.st_size - BUSTER_ATTESTED_MEMO_HEADER) / sizeof(CheckAttestedHash) : 0;
    // A file that isn't this key's (new, empty, torn in its header: its name is the key's) or is past its most records, or
    // a torn record, is written again from its start with this lane's memo whole; any other takes the fresh hashes.
    const bool again = !ours || records + fresh_count > BUSTER_ATTESTED_MEMO_RECORDS ||
                       ((uint64_t)status.st_size - BUSTER_ATTESTED_MEMO_HEADER) % sizeof(CheckAttestedHash) != 0;
    bool written = sized && ftruncate(file, again ? 0 : status.st_size) == 0;
    off_t at = again ? 0 : status.st_size;
    if (written && again)
    {
        written = pwrite(file, header, sizeof(header), 0) == (ssize_t)sizeof(header);
        at = BUSTER_ATTESTED_MEMO_HEADER;
    }
    // The records: the fresh ones, or with `again` every slot of the memo (a command of one source file's every hash, its
    // last BUSTER_ATTESTED_MEMO_RECORDS at most).
    const uint64_t first = again && census->alone && census->memo_count > BUSTER_ATTESTED_MEMO_RECORDS ? census->memo_count - BUSTER_ATTESTED_MEMO_RECORDS : 0;
    const CheckAttestedHash* const fresh = census->alone ? census->memo + census->memo_loaded : census->fresh;
    const CheckAttestedHash* const from = again ? census->memo + first : fresh;
    const uint64_t slots = again ? (census->alone ? census->memo_count - first : census->memo_mask + 1) : fresh_count;
    CheckAttestedHash block[256];
    uint32_t held = 0;
    for (uint64_t k = 0; written && k <= slots; k += 1)
    {
        if (k < slots && from[k].low != 0)
        {
            block[held] = from[k];
            held += 1;
        }
        if (held == BUSTER_ARRAY_LENGTH(block) || (k == slots && held != 0))
        {
            const ssize_t length = (ssize_t)(held * sizeof(CheckAttestedHash));
            written = pwrite(file, block, (size_t)length, at) == length;
            at += length;
            held = 0;
        }
    }
    if (locked)
    {
        flock(file, LOCK_UN);
    }
    if (file >= 0)
    {
        close(file);
    }
    census->fresh_count = 0;
    census->memo_loaded = census->alone ? census->memo_count : census->memo_loaded;
}

// A lane's memo kept across processes: the file for `key` in `directory`, loaded, and the hashes portable from here (the
// lane's first unit's ids are the first it sees). Whether there was room for its state.
static bool check_attested_memo_open(CheckAttested* census, const char* directory, const uint64_t* key)
{
    census->portable = true;
    census->memo_key = (CheckAttestedHash){ .low = key[0], .high = key[1] };
    uint64_t nonce = 0;
    if (getrandom(&nonce, sizeof(nonce), GRND_NONBLOCK) != (ssize_t)sizeof(nonce))
    {
        nonce = __rdtsc() ^ ((uint64_t)getpid() << 32);
    }
    census->nonce = nonce | 1;
    const size_t length = strlen(directory) + 64;
    census->memo_path = malloc(length);
    if (census->memo_path != NULL)
    {
        snprintf(census->memo_path, length, "%s/%016" PRIx64 "%016" PRIx64 ".attested", directory, key[0], key[1]);
    }
    // (A command of one source file's fresh hashes are its memo's own, CheckAttested.memo_index.)
    if (!census->alone)
    {
        census->fresh_arena = check_attested_reserve(census, (uint64_t)BUSTER_ATTESTED_MEMO_SLOTS * sizeof(CheckAttestedHash) + 4096);
        census->fresh = arena_push_array(&census->fresh_arena, BUSTER_ATTESTED_MEMO_SLOTS, sizeof(CheckAttestedHash), 64);
    }
    const bool room = census->memo_path != NULL && (census->alone || census->fresh != NULL);
    if (room)
    {
        check_attested_memo_load(census);
    }
    return room;
}

// ---------------------------------------------------------------------------------------------------------------------
// The compile's own (#2531's steps 1 and 2): a lane's state, the passes untimed before its unit's reader, and the reader
// given the marks of the declarations it passes.

static bool check_attested_open(CheckAttested* census);
static void check_attested_close(CheckAttested* census);

// A lane's state for a command of several source files with --attested=on or verify (`mode`), or null where it can't be
// reserved (its units are then read as without it). The memo starts empty, and its names' states are the lane's intern
// table's, which its units share.
static CheckAttested* check_attested_lane_open(uint8_t mode, const char* memo, const uint64_t* key, bool alone)
{
    CheckAttested* attested = calloc(1, sizeof(CheckAttested));
    if (attested != NULL)
    {
        attested->alone = alone;
    }
    const bool opened = attested != NULL && check_attested_open(attested) && (memo == NULL || check_attested_memo_open(attested, memo, key));
    if (attested != NULL && !opened)
    {
        check_attested_close(attested);
        free(attested);
        attested = NULL;
    }
    if (attested != NULL)
    {
        attested->production = true;
        attested->mode = mode;
        attested->passes = 1;
    }
    return attested;
}

static void check_attested_lane_close(CheckAttested* attested)
{
    if (attested != NULL)
    {
        check_attested_memo_save(attested);
        check_attested_close(attested);
        free(attested);
    }
}

// A lane's state in a command that leaves its lanes' memory to the exit: its arenas stay mapped until then, unreleased,
// as the lane's own are, and the state itself, the heap's, is freed.
static void check_attested_lane_leave(CheckAttested* attested)
{
    if (attested != NULL)
    {
        check_attested_memo_save(attested);
        free(attested->memo_path);
    }
    free(attested);
}

// A unit's start on the lane (null where it has no state): nothing skipped yet, and whether it is a reading again, which
// skips nothing.
static void check_attested_begin(CheckAttested* attested, bool again)
{
    if (attested != NULL)
    {
        attested->units_again += again ? 1 : 0;
        attested->reading_again = again;
        attested->skipped = false;
        attested->viewing = false;
        attested->again = false;
    }
}

// Whether the unit just compiled is read again with nothing skipped: it skipped, and its record says so or it `failed`.
static bool check_attested_again(const CheckAttested* attested, bool failed)
{
    return attested != NULL && attested->skipped && (attested->again || failed);
}

// The lane's counts, which --time-stages reports for the command (check_driver_units), into `counts`
// (CHECK_ATTESTED_SUMMARY): added to what is there.
static void check_attested_summary(const CheckAttested* attested, uint64_t* counts)
{
    if (attested != NULL)
    {
        counts[CHECK_ATTESTED_SUMMARY_UNITS] += attested->units_observed - attested->units_again;
        counts[CHECK_ATTESTED_SUMMARY_SKIPPING] += attested->units_skipping;
        counts[CHECK_ATTESTED_SUMMARY_VIEWS] += attested->units_viewing;
        counts[CHECK_ATTESTED_SUMMARY_AGAIN] += attested->units_again;
        counts[CHECK_ATTESTED_SUMMARY_DECLARATIONS] += attested->declarations;
        counts[CHECK_ATTESTED_SUMMARY_SKIPPED] += attested->outcome_declarations[CHECK_ATTESTED_SKIPPED];
        counts[CHECK_ATTESTED_SUMMARY_OUTSIDE_TOKENS] += attested->declaration_tokens - attested->outcome_tokens[CHECK_ATTESTED_DEFINITION_ROOT];
        counts[CHECK_ATTESTED_SUMMARY_SKIPPED_TOKENS] += attested->outcome_tokens[CHECK_ATTESTED_SKIPPED];
        counts[CHECK_ATTESTED_SUMMARY_HASHES] += attested->memo_count;
        counts[CHECK_ATTESTED_SUMMARY_CONTRADICTIONS] += attested->contradictions;
        counts[CHECK_ATTESTED_SUMMARY_UNLISTED] += attested->unlisted_declares + attested->unlisted_uses + attested->unlisted_completions;
    }
}

// The passes, untimed: whether the lists could be made (room, and every pragma's end). The marks then hold the
// declarations attested and not reached (skipped_declarations of them, skipped_tokens tokens).
static bool check_attested_decide(CheckAttested* attested, const Parser* parser, Preprocessor* pp)
{
    attested->stamp += 1;
    const bool scalar = parser->options.scalar;
    const bool listed = check_attested_lists(attested, parser, scalar) && check_attested_pragmas(attested, parser, pp);
    if (listed)
    {
        // (The vector path's lists made the own hashes, their key.)
        if (BUSTER_SCALAR_PATH(scalar))
        {
            check_attested_hashes(attested, parser, scalar);
        }
        check_attested_literals(attested, parser, pp, scalar);
        check_attested_chain(attested);
        // Without the runtime functions' names: a runtime call takes a declaration's symbol only where the selection
        // numbered the declaration's object (check_lower_declared_runtime), which only a reference of the code it selects
        // makes, a reading's that spells the name or its builtin's, which reach the declaration. The census counts what that
        // rule would reach (reached-by-runtime-names).
        check_attested_reach(attested, parser, pp, attested->reached, false);
        check_attested_marks(attested);
        // (The record's outcomes take the reach as its own: the census's reach without the runtime names.)
        memcpy(attested->reached_without_runtime, attested->reached, attested->declaration_count);
    }
    return listed;
}

// ---------------------------------------------------------------------------------------------------------------------
// Step 3's view: the unit's parse without the declarations its reader skips. A skipped declaration is attested and not
// reached: no read declaration names what it declares, its reading would report nothing and leave nothing behind, and
// the walk passed it with no visit. Every pass of the reader and of the stages after it that takes the whole unit took
// its tokens too (some 20 files read the reader's columns by token): in the view they aren't there, so that a skipped
// token costs none of them anything. A file-scope declaration is a tree of its own in a valid parse (its root its last
// token, no sibling), so that leaving whole declarations out leaves the rest a valid forest: a read token's parent, its
// own declaration's or none, moves down by the tokens left out before it, as the token does. The view's sources are the
// parse's, so that every location, spelling and diagnostic is the same; the census's arrays stay the parse's, by its token
// indices, and a declaration's place in the view is view_begins and view_ends.

// A column of the view: `read` entries of `size` bytes and the padding the parser keeps around each column
// (BUSTER_PARSE_TOKEN_PADDING entries before the first and after the last), the parse's own padding copied, which holds
// what a kernel reading past a token expects there (PARSE_KIND_END, zeros). Null where the arena has no room.
static void* check_attested_view_column(Arena* arena, const void* column, uint32_t count, uint32_t read, uint32_t size)
{
    const uint64_t padding = (uint64_t)BUSTER_PARSE_TOKEN_PADDING * size;
    uint8_t* bytes = arena_push_array(arena, (uint64_t)read + 2 * BUSTER_PARSE_TOKEN_PADDING, size, 64);
    const uint8_t* const from = column;
    if (bytes != NULL)
    {
        memcpy(bytes, from - padding, padding);
        memcpy(bytes + padding + (uint64_t)read * size, from + (uint64_t)count * size, padding);
    }
    return bytes != NULL ? bytes + padding : NULL;
}

// A run of `length` read tokens of the parse, from its token `from`, into the view at `at`: every column, a read token's
// parent (but a root's, BUSTER_PARSE_ROOT) less the skipped tokens before the run, its origin (its index in the parse),
// and its system level where the passes mapped them (`levels`, or null). The vector path 16 tokens at a time, each
// column a masked load and store, so that a short run costs no call; the scalar path's a token at a time is its twin.
static void check_attested_view_run(Parser* view, const Parser* parser, uint8_t* view_levels, const uint8_t* levels, uint32_t* origins, uint32_t from,
                                    uint32_t at, uint32_t length, bool scalar)
{
    const uint32_t shift = from - at;
    const __m512i root = _mm512_set1_epi32((int32_t)BUSTER_PARSE_ROOT);
    const __m512i down = _mm512_set1_epi32((int32_t)shift);
    const __m512i lane = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t k = 0;
    for (; !BUSTER_SCALAR_PATH(scalar) && k < length; k += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, length - k < 16 ? length - k : 16);
        const uint32_t t = from + k;
        const uint32_t v = at + k;
        _mm_mask_storeu_epi8(view->tokens.kinds + v, lanes, _mm_maskz_loadu_epi8(lanes, parser->tokens.kinds + t));
        _mm_mask_storeu_epi8(view->tokens.flags + v, lanes, _mm_maskz_loadu_epi8(lanes, parser->tokens.flags + t));
        _mm_mask_storeu_epi8(view->tree.roles + v, lanes, _mm_maskz_loadu_epi8(lanes, parser->tree.roles + t));
        _mm512_mask_storeu_epi32(view->tokens.names + v, lanes, _mm512_maskz_loadu_epi32(lanes, parser->tokens.names + t));
        _mm512_mask_storeu_epi32(view->tokens.sources + v, lanes, _mm512_maskz_loadu_epi32(lanes, parser->tokens.sources + t));
        _mm512_mask_storeu_epi32(view->tree.depths + v, lanes, _mm512_maskz_loadu_epi32(lanes, parser->tree.depths + t));
        const __m512i parent = _mm512_maskz_loadu_epi32(lanes, parser->tree.parents + t);
        const __mmask16 inner = _mm512_mask_cmpneq_epi32_mask(lanes, parent, root);
        _mm512_mask_storeu_epi32(view->tree.parents + v, lanes, _mm512_mask_sub_epi32(parent, inner, parent, down));
        _mm512_mask_storeu_epi32(origins + v, lanes, _mm512_add_epi32(_mm512_set1_epi32((int32_t)t), lane));
        if (levels != NULL)
        {
            _mm_mask_storeu_epi8(view_levels + v, lanes, _mm_maskz_loadu_epi8(lanes, levels + t));
        }
    }
    for (; k < length; k += 1)
    {
        const uint32_t t = from + k;
        const uint32_t v = at + k;
        view->tokens.kinds[v] = parser->tokens.kinds[t];
        view->tokens.flags[v] = parser->tokens.flags[t];
        view->tree.roles[v] = parser->tree.roles[t];
        view->tokens.names[v] = parser->tokens.names[t];
        view->tokens.sources[v] = parser->tokens.sources[t];
        view->tree.depths[v] = parser->tree.depths[t];
        view->tree.parents[v] = parser->tree.parents[t] != BUSTER_PARSE_ROOT ? parser->tree.parents[t] - shift : BUSTER_PARSE_ROOT;
        origins[v] = t;
        if (levels != NULL)
        {
            view_levels[v] = levels[t];
        }
    }
}

// The view of `parser`'s unit, whose skipped declarations the marks hold (skipped_tokens of its tokens), into
// CheckAttested.view: its columns copied a run of read declarations at a time (runs between skipped ones,
// check_attested_view_run), its parents moved down; each declaration's place in it; the read tokens' system levels (where the passes
// mapped them) and the pragmas' positions moved down by the skipped tokens before them (a pragma inside a declaration
// makes it ineligible, so that none is inside a skipped one). Whether there was room: without it the reader takes the
// parse with the marks, as step 2's did.
static bool check_attested_view(CheckAttested* census, const Parser* parser)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t declarations = census->declaration_count;
    const uint32_t read = count - (uint32_t)census->skipped_tokens;
    const bool scalar = parser->options.scalar;
    Arena* const arena = &census->unit_arena;
    const uint64_t mark = arena->length;
    Parser* const view = &census->view;
    *view = (Parser){
        .options = parser->options,
        .language = parser->language,
        .respelled = NULL,
        .preprocessor = parser->preprocessor,
        .meanings = parser->meanings,
        .statistics = parser->statistics,
    };
    memcpy(view->predeclared, parser->predeclared, sizeof(view->predeclared));
    view->tokens.count = read;
    view->tokens.capacity = read;
    view->tokens.kinds = check_attested_view_column(arena, parser->tokens.kinds, count, read, sizeof(uint8_t));
    view->tokens.names = check_attested_view_column(arena, parser->tokens.names, count, read, sizeof(uint32_t));
    view->tokens.sources = check_attested_view_column(arena, parser->tokens.sources, count, read, sizeof(uint32_t));
    view->tokens.flags = check_attested_view_column(arena, parser->tokens.flags, count, read, sizeof(uint8_t));
    view->tree.roles = check_attested_view_column(arena, parser->tree.roles, count, read, sizeof(uint8_t));
    view->tree.depths = check_attested_view_column(arena, parser->tree.depths, count, read, sizeof(uint32_t));
    view->tree.parents = check_attested_view_column(arena, parser->tree.parents, count, read, sizeof(uint32_t));
    uint32_t* const origins = arena_push_array(arena, (uint64_t)read + 1, sizeof(uint32_t), 64);
    view->origins = origins;
    census->view_begins = arena_push_array(arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64);
    census->view_ends = arena_push_array(arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64);
    census->view_reads = arena_push_array(arena, (uint64_t)declarations + 1, sizeof(uint32_t), 64);
    census->view_read_count = 0;
    census->view_levels = census->levels_ready ? arena_push_array(arena, (uint64_t)read + 1, sizeof(uint8_t), 64) : NULL;
    census->view_pragmas = arena_push_array(arena, (uint64_t)parser->tokens.pragma_count + 1, sizeof(uint32_t), 64);
    const bool room = view->tokens.kinds != NULL && view->tokens.names != NULL && view->tokens.sources != NULL && view->tokens.flags != NULL &&
                      view->tree.roles != NULL && view->tree.depths != NULL && view->tree.parents != NULL && origins != NULL && census->view_begins != NULL &&
                      census->view_ends != NULL && census->view_reads != NULL && (census->view_levels != NULL || !census->levels_ready) && census->view_pragmas != NULL &&
                      (declarations == 0 || census->lasts[declarations - 1] + 1 == count);
    // A run: declarations [`d`, `e`), read, from token `from` (the first's) to `to` (past the last's), its tokens at `at`
    // in the view.
    uint32_t at = 0;
    uint32_t d = 0;
    uint32_t pragma = 0;
    const uint32_t* const positions = parser->tokens.pragma_positions;
    while (room && d < declarations)
    {
        const bool skipped = census->hits[d] != 0 && census->reached[d] == 0;
        uint32_t e = d;
        while (e < declarations && (census->hits[e] != 0 && census->reached[e] == 0) == skipped)
        {
            census->view_begins[e] = skipped ? at : at + (census->firsts[e] - census->firsts[d]);
            census->view_ends[e] = skipped ? at : at + (census->lasts[e] + 1 - census->firsts[d]);
            census->view_reads[census->view_read_count] = e;
            census->view_read_count += skipped ? 0u : 1u;
            e += 1;
        }
        const uint32_t from = census->firsts[d];
        const uint32_t to = census->lasts[e - 1] + 1;
        // The pragmas before the run's end: each at its place less the tokens skipped before it (one before a skipped
        // run is at the next read token).
        while (pragma < parser->tokens.pragma_count && positions[pragma] < to)
        {
            census->view_pragmas[pragma] = skipped ? at : at + (positions[pragma] - from);
            pragma += 1;
        }
        if (!skipped)
        {
            check_attested_view_run(view, parser, census->view_levels, census->view_levels != NULL ? census->token_levels : NULL, origins, from, at, to - from, scalar);
            at += to - from;
        }
        d = e;
    }
    // The pragmas at the unit's end.
    while (room && pragma < parser->tokens.pragma_count)
    {
        census->view_pragmas[pragma] = at + (positions[pragma] - count);
        pragma += 1;
    }
    const bool valid = room && at == read;
    if (valid)
    {
        // (A label numbered past the last token, at the unit's end, keeps the parse's number.)
        origins[read] = count;
        if (census->view_levels != NULL)
        {
            // (check_attested_levels' entry past the last token.)
            census->view_levels[read] = census->token_levels[count];
        }
        view->tokens.pragma_sources = parser->tokens.pragma_sources;
        view->tokens.pragma_positions = census->view_pragmas;
        view->tokens.pragma_count = parser->tokens.pragma_count;
    }
    else
    {
        arena_pop_to(arena, mark);
        *view = (Parser){ 0 };
    }
    return valid;
}

// The parse a unit's reader and the stages after it take: the view where the unit skips (check_attested_view), the parse
// itself otherwise.
static Parser* check_attested_parse(CheckAttested* attested, Parser* parser)
{
    return attested != NULL && attested->viewing ? &attested->view : parser;
}

// check_driver_unit's reader for a unit of a lane with attested declarations: the passes (none for a unit read again,
// whose first reading skipped and must not, nor for a view of a failed parse), then check_input_run as check_input_compile
// calls it, which tests each reading's binding events against its list as it ends (check_attested_events). With
// --attested=on, a unit that skips is read as its view (check_attested_view), which the stages after the reader take too
// (check_attested_parse); or, where there was no room for the view, as the parse with the marks of the declarations
// attested and not reached, which its walk passes.
static bool check_attested_read(CheckAttested* attested, CheckInput* input, const Parser* parser, Preprocessor* pp, TypeTable* types, Arena* arena, Arena* scratch,
                                bool complete_translation_unit, uint32_t original_end, const uint8_t* deferred_tokens)
{
    const uint32_t count = parser->tokens.count;
    attested->unit_ready = false;
    attested->levels_ready = false;
    attested->unit_tree_mismatches = 0;
    attested->skipped = false;
    attested->again = false;
    attested->path = NULL;
    attested->token_count = count;
    attested->skipped_declarations = 0;
    attested->skipped_tokens = 0;
    attested->viewing = false;
    const bool whole = !attested->reading_again && complete_translation_unit && parser->errors == 0 && parser->fatal == PARSE_MESSAGE_NONE &&
                       parser->tree.roles != NULL && parser->tree.parents != NULL && pp->table->count < BUSTER_ATTESTED_MAX_IDS;
    if (whole && check_attested_unit_arrays(attested, count, parser->tokens.pragma_count, pp->output.count))
    {
        check_attested_ids(attested, pp->table);
        check_attested_levels(attested, parser, pp);
        attested->unit_ready = check_attested_decide(attested, parser, pp);
        attested->skipped = attested->unit_ready && attested->mode == CHECK_ATTESTED_MODE_ON && attested->skipped_declarations != 0;
        // The view, where the unit skips the whole of its parse (not a lazily deferred selection's, whose marks are the
        // parse's): its token indices are the reader's from here, and the unit's. A command of one source file's
        // (CheckAttested.alone) only where it skips a third of the unit's tokens or more: each page of its view is a
        // fault for the process, some 11 cycles a token it copies, where a token read as a view saves the reader about
        // 26 over one its marks pass (sqlite3.c, which skips 6% of its tokens, took 4,000 more faults with its view).
        const bool worth = !attested->alone || 3 * (uint64_t)attested->skipped_tokens >= count;
        attested->viewing = attested->skipped && worth && deferred_tokens == NULL && original_end == count && check_attested_view(attested, parser);
        if (attested->skipped && !attested->viewing)
        {
            // (The parse with the skipped declarations' marks, check_input_skip, where there was no room for the view.)
            check_attested_mark_bits(attested);
        }
        attested->token_count = attested->viewing ? attested->view.tokens.count : count;
    }
    attested->events_checked = false;
    const Parser* const read = attested->viewing ? &attested->view : parser;
    return check_input_run(input, read, pp, types, arena, scratch, complete_translation_unit, attested->viewing ? read->tokens.count : original_end, deferred_tokens,
                           CHECK_INPUT_BINDINGS_DEFAULT, 0, false, true, true, attested);
}

// ---------------------------------------------------------------------------------------------------------------------
// The tree's columns from the lists' cache (the reader's, check_input_run's parse_tree_columns): in a valid parse each
// file-scope declaration's tokens are one tree whose root is its last token, with no sibling (a root has none), so its
// columns are its own parents' alone, offset by its first token: its tokens' positions in the postorder are its own
// run (every earlier token's subtree ends before it), and every column holds tokens of its own run or none. A cached
// declaration's columns are kept relative to its first token, with its parents (seven columns of its length, in the
// tree pool, check_attested_tree_keep); a later declaration under its key whose parents are the same takes them, 16
// tokens at a time, and any other has them made over its run as parse_tree_columns makes the unit's, the twin.

// The declaration of tokens [`a`, `b`]'s tree columns as parse_tree_columns makes the unit's (with the sizes), its three
// passes over the run alone: the positions' prefix sum starts at `a`, the tokens whose subtree ends before it.
static void check_attested_tree_range(const uint32_t* parents, uint32_t count, uint32_t a, uint32_t b, uint32_t* last_children, uint32_t* before,
                                      uint32_t* postorder, uint32_t* positions, uint32_t* ends, uint32_t* first_children, uint32_t* next_siblings,
                                      uint32_t* starts, uint32_t* sizes)
{
    for (uint32_t v = a; v <= b; v += 1)
    {
        last_children[v] = 0;
        first_children[v] = BUSTER_PARSE_NONE;
        before[v] = 0;
    }
    first_children[count] = BUSTER_PARSE_NONE;
    for (uint32_t i = a; i <= b; i += 1)
    {
        last_children[parents[i] == BUSTER_PARSE_NONE ? count : parents[i]] = i;
    }
    for (uint32_t v = b + 1; v-- > a;)
    {
        const uint32_t parent = parents[v];
        const uint32_t slot = parent != BUSTER_PARSE_NONE ? parent : count;
        const uint32_t sibling = first_children[slot];
        next_siblings[v] = parent != BUSTER_PARSE_NONE ? sibling : BUSTER_PARSE_NONE;
        first_children[slot] = v;
        const uint32_t last = last_children[v];
        const uint32_t end = ends[last];
        const uint32_t own = __builtin_unpredictable(last > v) ? end : v;
        ends[v] = own;
        before[own] += 1;
    }
    uint32_t sum = a;
    for (uint32_t v = a; v <= b; v += 1)
    {
        const uint32_t position = sum + ends[v] - v;
        positions[v] = position;
        postorder[position] = v;
        sum += before[v];
        const uint32_t first = first_children[v];
        const bool before_v = first < v;
        const uint32_t start = starts[before_v ? first : v];
        starts[v] = __builtin_unpredictable(before_v) ? start : v;
        sizes[position] = ends[v] - starts[v] + 1;
    }
}

// Whether every parent of tokens [`a`, `b`] is one of them or none: the declaration is its own tree.
static bool check_attested_tree_own(const uint32_t* parents, uint32_t a, uint32_t b)
{
    const __m512i none = _mm512_set1_epi32(-1);
    bool own = true;
    for (uint32_t t = a; own && t <= b; t += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, b - t + 1 < 16 ? b - t + 1 : 16);
        const __m512i parent = _mm512_maskz_loadu_epi32(lanes, parents + t);
        const __mmask16 inside = _mm512_mask_cmpge_epu32_mask(lanes, parent, _mm512_set1_epi32((int32_t)a)) &
                                 _mm512_mask_cmple_epu32_mask(lanes, parent, _mm512_set1_epi32((int32_t)b));
        own = (__mmask16)(inside | _mm512_mask_cmpeq_epi32_mask(lanes, parent, none)) == lanes;
    }
    return own;
}

// `column`'s tokens' values [`a`, `a` + `length`) less `a` into `kept` as halfwords (none as UINT16_MAX), or with
// `restore` `kept`'s plus `a` back into `column` (UINT16_MAX as none): 16 at a time, masked at the run's end. A kept
// declaration is shorter than BUSTER_ATTESTED_TREE_MOST tokens, so that every value of its own run is below it.
static void check_attested_tree_shift(uint32_t* column, uint16_t* kept, uint32_t a, uint32_t length, bool restore)
{
    const __m512i none = _mm512_set1_epi32(-1);
    const __m512i offset = _mm512_set1_epi32((int32_t)a);
    for (uint32_t k = 0; k < length; k += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, length - k < 16 ? length - k : 16);
        if (restore)
        {
            const __m512i value = _mm512_cvtepu16_epi32(_mm512_castsi512_si256(_mm512_maskz_loadu_epi16((__mmask32)lanes, kept + k)));
            const __mmask16 some = _mm512_cmpneq_epi32_mask(value, _mm512_set1_epi32(UINT16_MAX));
            _mm512_mask_storeu_epi32(column + a + k, lanes, _mm512_mask_add_epi32(none, some, value, offset));
        }
        else
        {
            const __m512i value = _mm512_maskz_loadu_epi32(lanes, column + a + k);
            const __m512i shifted = _mm512_mask_sub_epi32(value, _mm512_mask_cmpneq_epi32_mask(lanes, value, none), value, offset);
            _mm512_mask_storeu_epi16(kept + k, (__mmask32)lanes, _mm512_castsi256_si512(_mm512_cvtepi32_epi16(shifted)));
        }
    }
}

// Whether the kept tree at `kept`, of `length` tokens, has the parents of tokens from `a`: each one none, or one of the
// run's own at its place less `a`.
static bool check_attested_tree_same(const uint32_t* parents, const uint16_t* kept, uint32_t a, uint32_t length)
{
    const __m512i none = _mm512_set1_epi32(-1);
    const __m512i offset = _mm512_set1_epi32((int32_t)a);
    bool same = true;
    for (uint32_t k = 0; same && k < length; k += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, length - k < 16 ? length - k : 16);
        const __m512i parent = _mm512_maskz_loadu_epi32(lanes, parents + a + k);
        const __mmask16 rooted = _mm512_mask_cmpeq_epi32_mask(lanes, parent, none);
        const __m512i relative = _mm512_sub_epi32(parent, offset);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, relative, _mm512_set1_epi32((int32_t)length));
        const __m512i expected = _mm512_cvtepu16_epi32(_mm512_castsi512_si256(_mm512_maskz_loadu_epi16((__mmask32)lanes, kept + k)));
        const __mmask16 equal = _mm512_mask_cmpeq_epi32_mask(rooted, expected, _mm512_set1_epi32(UINT16_MAX)) |
                                _mm512_mask_cmpeq_epi32_mask(inside, expected, relative);
        same = equal == lanes;
    }
    return same;
}

// Declaration `d`'s kept tree, or null where it has none.
static const uint16_t* check_attested_tree_of(const CheckAttested* census, uint32_t d)
{
    const uint32_t block = census->list_blocks[d];
    const uint32_t tree = block != BUSTER_ATTESTED_NONE ? census->list_pool[block] : BUSTER_ATTESTED_NONE;
    return tree != BUSTER_ATTESTED_NONE ? census->tree_pool + tree : NULL;
}

// How many declarations ahead the tree columns fetch a declaration's list's block, and half as many its kept tree.
#define BUSTER_ATTESTED_TREE_AHEAD 4u

// The unit's tree columns as parse_tree_columns makes them (its arguments, with the sizes), a declaration at a time from
// the lists' cache (the section's comment): false where the unit's passes didn't run, it has no sizes or a declaration
// isn't its own tree, and the caller then makes them whole. With --attested=verify or --twins the unit's columns are
// made whole too, in `scratch`, and a difference is a contradiction.
static bool check_attested_tree_columns(CheckAttested* census, const uint32_t* parents, uint32_t count, uint32_t* last_children, uint32_t* before,
                                        uint32_t* postorder, uint32_t* positions, uint32_t* ends, uint32_t* first_children, uint32_t* next_siblings,
                                        uint32_t* starts, uint32_t* sizes, Arena* scratch)
{
    const bool ready = census->unit_ready && !census->alone && count == census->token_count && sizes != NULL && census->declaration_count != 0;
    // In a view, its read declarations alone, in order (a skipped one has no tokens, nor columns, and its cached tree's
    // block isn't touched); otherwise every declaration.
    const uint32_t declarations = !ready ? 0 : census->viewing ? census->view_read_count : census->declaration_count;
    bool valid = ready;
    for (uint32_t r = 0; valid && r < declarations; r += 1)
    {
        const uint32_t d = census->viewing ? census->view_reads[r] : r;
        const uint32_t a = census->viewing ? census->view_begins[d] : census->firsts[d];
        const uint32_t length = census->viewing ? census->view_ends[d] - a : census->lasts[d] - a + 1;
        const uint32_t b = a + length - 1;
        // The pools are the lane's, mostly out of the first two caches by the next unit: a list's block a few declarations
        // on, and the kept tree of one half as far, are fetched while this one's columns are written.
        const uint32_t ahead = r + BUSTER_ATTESTED_TREE_AHEAD < declarations ? (census->viewing ? census->view_reads[r + BUSTER_ATTESTED_TREE_AHEAD] : r + BUSTER_ATTESTED_TREE_AHEAD) :
                                                                                BUSTER_ATTESTED_NONE;
        const uint32_t half = r + BUSTER_ATTESTED_TREE_AHEAD / 2 < declarations ?
                              (census->viewing ? census->view_reads[r + BUSTER_ATTESTED_TREE_AHEAD / 2] : r + BUSTER_ATTESTED_TREE_AHEAD / 2) : BUSTER_ATTESTED_NONE;
        if (ahead != BUSTER_ATTESTED_NONE && census->list_blocks[ahead] != BUSTER_ATTESTED_NONE)
        {
            __builtin_prefetch(census->list_pool + census->list_blocks[ahead]);
        }
        const uint16_t* const later = half != BUSTER_ATTESTED_NONE ? check_attested_tree_of(census, half) : NULL;
        if (later != NULL)
        {
            __builtin_prefetch(later);
            __builtin_prefetch(later + 32);
            __builtin_prefetch(later + 64);
        }
        const uint32_t block = census->list_blocks[d];
        const uint16_t* const kept = check_attested_tree_of(census, d);
        if (length != 0 && kept != NULL && check_attested_tree_same(parents, kept, a, length))
        {
            check_attested_tree_shift(positions, (uint16_t*)kept + length, a, length, true);
            check_attested_tree_shift(postorder, (uint16_t*)kept + 2 * (uint64_t)length, a, length, true);
            check_attested_tree_shift(ends, (uint16_t*)kept + 3 * (uint64_t)length, a, length, true);
            check_attested_tree_shift(first_children, (uint16_t*)kept + 4 * (uint64_t)length, a, length, true);
            check_attested_tree_shift(next_siblings, (uint16_t*)kept + 5 * (uint64_t)length, a, length, true);
            for (uint32_t k = 0; k < length; k += 16)
            {
                const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, length - k < 16 ? length - k : 16);
                _mm512_mask_storeu_epi32(sizes + a + k, lanes, _mm512_cvtepu16_epi32(_mm512_castsi512_si256(_mm512_maskz_loadu_epi16((__mmask32)lanes, kept + 6 * (uint64_t)length + k))));
            }
            census->tree_hits += 1;
        }
        else if (length != 0)
        {
            valid = check_attested_tree_own(parents, a, b);
            if (valid)
            {
                check_attested_tree_range(parents, count, a, b, last_children, before, postorder, positions, ends, first_children, next_siblings, starts, sizes);
                census->tree_made += 1;
                census->tree_mismatches += kept != NULL ? 1 : 0;
            }
            if (valid && kept == NULL && block != BUSTER_ATTESTED_NONE && length < BUSTER_ATTESTED_TREE_MOST &&
                census->tree_pool_length + 7 * (uint64_t)length <= BUSTER_ATTESTED_TREE_POOL)
            {
                uint16_t* const into = census->tree_pool + census->tree_pool_length;
                check_attested_tree_shift((uint32_t*)parents, into, a, length, false);
                check_attested_tree_shift(positions, into + length, a, length, false);
                check_attested_tree_shift(postorder, into + 2 * (uint64_t)length, a, length, false);
                check_attested_tree_shift(ends, into + 3 * (uint64_t)length, a, length, false);
                check_attested_tree_shift(first_children, into + 4 * (uint64_t)length, a, length, false);
                check_attested_tree_shift(next_siblings, into + 5 * (uint64_t)length, a, length, false);
                for (uint32_t k = 0; k < length; k += 16)
                {
                    const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffffu, length - k < 16 ? length - k : 16);
                    _mm512_mask_storeu_epi16(into + 6 * (uint64_t)length + k, (__mmask32)lanes, _mm512_castsi256_si512(_mm512_cvtepi32_epi16(_mm512_maskz_loadu_epi32(lanes, sizes + a + k))));
                }
                census->list_pool[block] = (uint32_t)census->tree_pool_length;
                census->tree_pool_length += 7 * (uint64_t)length;
                census->tree_kept += 1;
            }
        }
    }
    if (valid)
    {
        // parse_tree_columns' sentinels.
        first_children[count] = BUSTER_PARSE_NONE;
        next_siblings[count] = BUSTER_PARSE_NONE;
        ends[count] = count;
        positions[count] = count;
    }
    if (valid && (census->mode == CHECK_ATTESTED_MODE_VERIFY || census->twins))
    {
        // The twin: the whole unit's columns, which these must be to the word.
        const uint64_t mark = scratch->length;
        const uint64_t slots = (uint64_t)count + 1;
        uint32_t* const whole = arena_push_array(scratch, 9 * slots, sizeof(uint32_t), 64);
        if (whole != NULL)
        {
            parse_tree_columns(parents, count, whole, whole + slots, whole + 2 * slots, whole + 3 * slots, whole + 4 * slots, whole + 5 * slots, whole + 6 * slots, whole + 7 * slots,
                               whole + 8 * slots);
            const bool same = memcmp(whole + 2 * slots, postorder, (uint64_t)count * sizeof(uint32_t)) == 0 &&
                              memcmp(whole + 3 * slots, positions, slots * sizeof(uint32_t)) == 0 && memcmp(whole + 4 * slots, ends, slots * sizeof(uint32_t)) == 0 &&
                              memcmp(whole + 5 * slots, first_children, slots * sizeof(uint32_t)) == 0 &&
                              memcmp(whole + 6 * slots, next_siblings, slots * sizeof(uint32_t)) == 0 && memcmp(whole + 8 * slots, sizes, (uint64_t)count * sizeof(uint32_t)) == 0;
            census->unit_tree_mismatches += same ? 0 : 1;
            census->contradictions += same ? 0 : 1;
            census->tree_mismatches += same ? 0 : 1;
        }
        arena_pop_to(scratch, mark);
    }
    return valid;
}

// Whether --attested=verify fails the unit: a contradiction in it.
static bool check_attested_verifying(const CheckAttested* attested)
{
    return attested->mode == CHECK_ATTESTED_MODE_VERIFY && attested->unit_contradictions != 0;
}

// --attested=verify's failure: the unit's first contradiction, a hit whose reading here wasn't silent, pure and listed,
// by its file and line, on the unit's stream.
static void check_attested_contradiction(const CheckAttested* attested, Preprocessor* pp, FILE* errors)
{
    const char* name = NULL;
    uint32_t length = 0;
    const uint32_t line = attested->contradicted != 0 ? pp_presumed(pp, attested->contradicted, &name, &length) : 0;
    fprintf(errors, "fatal error: %.*s:%u: an attested declaration's reading here is not what the memo attests (%u in the unit; #2531)\n",
            name != NULL ? (int)length : 1, name != NULL ? name : "-", line, attested->unit_contradictions);
}

// ---------------------------------------------------------------------------------------------------------------------
// The readings' binding events against the lists: check_input_run's call as it ends, with its bindings stream, its tags'
// completions and its table, before its scratch goes.

static uint32_t check_attested_declaration_of(const CheckAttested* census, uint32_t token);
static bool check_attested_holds(const CheckAttested* census, uint32_t d, uint32_t token);
static bool check_attested_lists_hold(const CheckAttested* census, uint32_t d, uint32_t key, uint8_t classes);
static void check_attested_list_declaration(CheckAttested* census, Preprocessor* pp, const char* what, uint32_t d);

// The marks of the declarations that the unit's walk passes (check_input_skip), or null where it passes none.
static const uint64_t* check_attested_skips(const CheckAttested* attested)
{
    // (The view has none of their tokens.)
    return attested->skipped && !attested->viewing ? attested->marks : NULL;
}

// Each of the unit's tokens' system level (BUSTER_PP_*_FILE) as check_attested_levels made it before the passes, for the
// reader's system tokens (check_input_system_tokens), or null where it made none (no system file, or a reading again).
static const uint8_t* check_attested_token_levels(const CheckAttested* attested)
{
    return !attested->levels_ready ? NULL : attested->viewing ? attested->view_levels : attested->token_levels;
}

// Declaration `d`'s list marked by key (CheckAttested.list_marks): each key's mark the unit's stamp, `d` and the classes
// its entries give the key. A mark of another unit or declaration is replaced, and so is the one an earlier marking left.
static void check_attested_mark_list(CheckAttested* census, uint32_t d)
{
    const uint64_t owner = ((uint64_t)(census->stamp & 0xffffffu) << 40) | ((uint64_t)d << 8);
    uint64_t* const marks = census->list_marks;
    for (uint32_t n = census->name_starts[d]; n < census->name_starts[d + 1]; n += 1)
    {
        const uint32_t entry = census->name_entries[n];
        const uint32_t key = entry & BUSTER_ATTESTED_KEY_MASK;
        const uint64_t mark = marks[key];
        const uint64_t kept = (mark & ~UINT64_C(0xff)) == owner ? mark : owner;
        marks[key] = kept | (entry >> BUSTER_ATTESTED_CLASS_SHIFT);
    }
}

// Each file-scope DECLARE event, and each USE event, of a declaration's reading against its list, and each tag it
// completes: a declaration whose reading declared, completed or looked up a name its list doesn't hold is unlisted
// (never learned, and a unit that skipped is read again). The events come in nearly source order, so each one's
// declaration is the last one's or a search's; a declaration's list is marked by key as its events start (a function
// body's has every name it spells, so that a scan of the list for each event would take the body's square).
static void check_attested_events(CheckAttested* census, Preprocessor* pp, const CBindings* bindings, const uint32_t* completions, const TypeTable* types, uint32_t count)
{
    const bool ready = census->unit_ready && count == census->token_count;
    const uint32_t declarations = ready ? census->declaration_count : 0;
    census->events_checked = true;
    census->unit_unlisted_events = 0;
    if (ready)
    {
        memset(census->unlisted, 0, declarations);
    }
    const uint32_t name_capacity = bindings->name_capacity;
    const uint64_t stamp = (uint64_t)(census->stamp & 0xffffffu) << 40;
    uint32_t d = declarations;
    for (uint32_t e = 0; ready && name_capacity != 0 && e < bindings->count; e += 1)
    {
        const uint8_t operation = bindings->operation[e];
        const uint32_t space = bindings->key[e] / name_capacity;
        const uint32_t id = bindings->key[e] % name_capacity;
        const bool file_declare = operation == C_BINDING_DECLARE && bindings->depth[e] == 0 && space <= C_BINDING_TAG;
        const bool use = operation == C_BINDING_USE && space <= C_BINDING_TAG;
        const uint32_t point = bindings->point[e];
        const bool here = d < declarations && check_attested_holds(census, d, point);
        const uint32_t at = (file_declare || use) && !here ? check_attested_declaration_of(census, point) : d;
        if (at != d && at < declarations)
        {
            check_attested_mark_list(census, at);
        }
        d = at;
        if ((file_declare || use) && d < declarations)
        {
            const uint32_t key = id * 2 + (space == C_BINDING_TAG ? 1u : 0u);
            const uint64_t mark = key < 2 * BUSTER_ATTESTED_MAX_IDS ? census->list_marks[key] : 0;
            const uint8_t classes = file_declare ? BUSTER_ATTESTED_DECLARES : BUSTER_ATTESTED_DECLARES | BUSTER_ATTESTED_LOOKS_UP;
            const bool held = (mark & ~UINT64_C(0xff)) == (stamp | (uint64_t)d << 8) && (mark & classes) != 0;
            census->declare_events += file_declare ? 1 : 0;
            census->use_events += use ? 1 : 0;
            census->unlisted_declares += file_declare && !held ? 1 : 0;
            census->unlisted_uses += use && !held ? 1 : 0;
            census->unit_unlisted_events += !held ? 1 : 0;
            census->unlisted[d] |= !held ? 1 : 0;
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
    const uint32_t tags = ready && completions != NULL ? types->tags.count : 0;
    for (uint32_t tag = 0; tag < tags; tag += 1)
    {
        const uint32_t completion = completions[tag];
        const uint32_t name = types->tags.name[tag];
        const uint32_t at = completion != 0 && completion <= count && name != 0 ? check_attested_declaration_of(census, completion - 1) : declarations;
        if (at < declarations && (census->flags[at] & BUSTER_ATTESTED_DEFINITION) == 0 && !check_attested_lists_hold(census, at, name * 2 + 1, BUSTER_ATTESTED_DECLARES))
        {
            census->unlisted_completions += 1;
            census->unit_unlisted_events += 1;
            census->unlisted[at] = 1;
            if (census->list != 0)
            {
                printf("attested: unlisted completion name=%.*s\n", (int)intern_length(pp->table, name), (const char*)intern_bytes(pp->table, name));
                check_attested_list_declaration(census, pp, "unlisted", at);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The record, at the unit's end.

// Whether declaration `d` holds token `token` of the unit's reader: of its view, where it reads one (a skipped
// declaration's place there is empty), or of its parse.
static bool check_attested_holds(const CheckAttested* census, uint32_t d, uint32_t token)
{
    return census->viewing ? census->view_begins[d] <= token && token < census->view_ends[d] : census->firsts[d] <= token && token <= census->lasts[d];
}

// The declaration that holds token `token` of the unit's reader, or the count where none does: the first whose end is
// past it (in a view, a skipped declaration's empty place ends where the next read one starts).
static uint32_t check_attested_declaration_of(const CheckAttested* census, uint32_t token)
{
    uint32_t low = 0;
    uint32_t high = census->declaration_count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        const bool before = census->viewing ? census->view_ends[middle] <= token : census->lasts[middle] < token;
        low = before ? middle + 1 : low;
        high = before ? high : middle;
    }
    return low < census->declaration_count && check_attested_holds(census, low, token) ? low : census->declaration_count;
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

// The declaration that holds the first token whose location is `location`, or the count where none does: a token at a time,
// for a diagnostic that its position doesn't place (few, and only in a unit that has one).
static uint32_t check_attested_located(const CheckAttested* census, const Parser* parser, const Preprocessor* pp, uint32_t location)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t* const sources = parser->tokens.sources;
    const uint32_t* const locations = pp->output.locations;
    uint32_t token = count;
    for (uint32_t t = 0; locations != NULL && token == count && t < count; t += 1)
    {
        token = sources[t] < pp->output.count && locations[sources[t]] == location ? t : count;
    }
    return token < count ? check_attested_declaration_of(census, token) : census->declaration_count;
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
    // (The parse's tokens: a view's and those it left out.)
    const uint64_t parsed = count + (census->viewing ? census->skipped_tokens : 0);
    census->units_observed += 1;
    census->tokens += parsed;
    census->unit_contradictions = census->unit_tree_mismatches;
    census->unit_tree_mismatches = 0;
    census->contradicted = 0;
    uint64_t unlisted_events = 0;
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
    bool error = !valid || input->report_count != 0 || !census->events_checked || pp->diagnostic_collection_failed;
    uint32_t unplaced = 0;
    for (const PPDiagnosticRecord* record = pp->diagnostic_first; record != NULL; record = record->next)
    {
        error |= record->severity == PP_SEVERITY_ERROR || record->severity == PP_SEVERITY_FATAL;
        const uint64_t position = record->position / PP_DIAGNOSTIC_PHASE_COUNT;
        const uint32_t token = position < UINT32_MAX ? check_attested_token_at(parser, (uint32_t)position) : count;
        uint32_t d = arrays && token < count ? check_attested_declaration_of(census, token) : declarations;
        // A diagnostic of the unit's end (-Wunused-function's, an unused internal declaration's) is published past its
        // last token: placed by its location instead, at the token that has it, where one does.
        d = arrays && d == declarations && record->location != 0 ? check_attested_located(census, parser, pp, record->location) : d;
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
    // The binding events against the lists, which the reader tested as it ended (check_attested_events).
    unlisted_events += ready ? census->unit_unlisted_events : 0;
    for (uint32_t d = 0; ready && arrays && d < declarations; d += 1)
    {
        unlisted[d] = census->unlisted[d];
    }
    error |= ready && !arrays;
    const bool learning = !error && unplaced == 0;
    census->units_failed += error ? 1 : 0;
    census->failed_tokens += error || !ready ? parsed : 0;
    const uint8_t* const bits = input->declaration_bits;
    for (uint32_t d = 0; ready && arrays && d < declarations; d += 1)
    {
        const uint32_t tokens = census->lasts[d] - census->firsts[d] + 1;
        // Its tokens in the reader's parse (in a view, its place there: a skipped one's empty).
        const uint32_t first = census->viewing ? census->view_begins[d] : census->firsts[d];
        const uint32_t end = census->viewing ? census->view_ends[d] : census->lasts[d] + 1;
        const uint8_t flags = census->flags[d];
        // Its reading's consequences, by the reader's facts at its tokens (none where the walk passed it: no reading).
        const bool passed = census->skipped && census->hits[d] != 0 && census->reached[d] == 0;
        CheckAttestedOutcome impure = CHECK_ATTESTED_OUTCOMES;
        for (uint32_t t = first; bits != NULL && !passed && t < end && impure == CHECK_ATTESTED_OUTCOMES; t += 1)
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
            const uint32_t source = first < count ? parser->tokens.sources[first] : UINT32_MAX;
            census->contradicted = census->unit_contradictions == 0 && source < pp->output.count && pp->output.locations != NULL ? pp->output.locations[source] :
                                   census->contradicted;
            census->unit_contradictions += 1;
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
    // A unit that skipped is read again with nothing skipped where its reading may have depended on what it skipped: an
    // error (clang's typo correction and recovery look at every declared name), the typo corrector for a warning, a name
    // declared or looked up outside its declaration's list (the chain's hashes didn't cover it), or a contradiction (an
    // input of a reading that the hash doesn't cover, which a skipped declaration may have too).
    census->again = census->skipped && (error || !ready || !arrays || unlisted_events != 0 || input->typo_searched || census->unit_contradictions != 0);
    census->units_skipping += census->skipped ? 1 : 0;
    census->units_viewing += census->viewing ? 1 : 0;
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
    if (census->twins)
    {
        printf("attested: twins differences=%" PRIu64 "\n", census->twin_differences);
    }
    printf("attested: literals=%" PRIu64 " spelled=%" PRIu64 " range-searches=%" PRIu64 " (of every run of the passes)\n", census->literal_count, census->spelled_literals,
           census->range_searches);
    printf("attested: lists cached=%" PRIu64 " made=%" PRIu64 " kept=%" PRIu64 " pool-entries=%" PRIu64 " (of every run of the passes)\n", census->list_hits, census->list_misses,
           census->list_count, census->list_pool_length);
    printf("attested: trees cached=%" PRIu64 " made=%" PRIu64 " kept=%" PRIu64 " pool-words=%" PRIu64 " mismatches=%" PRIu64 "\n", census->tree_hits, census->tree_made,
           census->tree_kept, census->tree_pool_length, census->tree_mismatches);
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
    // Room for every size the memo takes, each table after the last: twice its most slots. A command of one source file's:
    // its hashes' array, then each size of their indexes' table after the last (CheckAttested.memo_index).
    if (census->alone)
    {
        census->memo_arena = check_attested_reserve(census, (uint64_t)BUSTER_ATTESTED_ALONE_HASHES * sizeof(CheckAttestedHash) +
                                                                2 * (uint64_t)BUSTER_ATTESTED_MEMO_SLOTS * sizeof(uint32_t) + 8192);
        census->memo = arena_push_array(&census->memo_arena, BUSTER_ATTESTED_ALONE_HASHES, sizeof(CheckAttestedHash), 64);
        census->memo_index = arena_push_array(&census->memo_arena, BUSTER_ATTESTED_MEMO_FIRST_SLOTS, sizeof(uint32_t), 64);
    }
    else
    {
        census->memo_arena = check_attested_reserve(census, 2 * (uint64_t)BUSTER_ATTESTED_MEMO_SLOTS * sizeof(CheckAttestedHash) + 4096);
        census->memo = arena_push_array(&census->memo_arena, BUSTER_ATTESTED_MEMO_FIRST_SLOTS, sizeof(CheckAttestedHash), 64);
    }
    census->memo_mask = BUSTER_ATTESTED_MEMO_FIRST_SLOTS - 1;
    census->key_arena = check_attested_reserve(census, keys * (sizeof(CheckAttestedKey) + 2 * sizeof(uint64_t)) +
                                      (uint64_t)BUSTER_ATTESTED_MAX_IDS * (sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint64_t)) +
                                      2 * ((uint64_t)runtimes + 1) * sizeof(uint32_t) + 16 * 4096);
    Arena* arena = &census->key_arena;
    census->keys = arena_push_array(arena, keys, sizeof(CheckAttestedKey), 4096);
    census->list_marks = arena_push_array(arena, keys, sizeof(uint64_t), 4096);
    census->body_marks = arena_push_array(arena, keys, sizeof(uint64_t), 4096);
    census->id_facts = arena_push_array(arena, BUSTER_ATTESTED_MAX_IDS, sizeof(uint8_t), 4096);
    census->builtin_targets = arena_push_array(arena, BUSTER_ATTESTED_MAX_IDS, sizeof(uint32_t), 4096);
    // (Written only with a memo kept across processes, check_attested_ids: its pages untouched otherwise.)
    census->id_words = arena_push_array(arena, BUSTER_ATTESTED_MAX_IDS, sizeof(uint64_t), 4096);
    census->runtime_keys = arena_push_array(arena, runtimes + 1, sizeof(uint32_t), 4096);
    census->runtime_lengths = arena_push_array(arena, runtimes + 1, sizeof(uint32_t), 4096);
    for (uint32_t r = 0; census->runtime_lengths != NULL && r < runtimes; r += 1)
    {
        census->runtime_lengths[r] = check_lower_runtime_names[r] != NULL ? (uint32_t)strlen(check_lower_runtime_names[r]) : UINT32_MAX;
    }
    census->unit_arena = check_attested_reserve(census, UINT64_C(1) << 36);
    // The lists' cache: room for every size its table takes, each after the last, and its pool. A command of one source
    // file's state keeps none (CheckAttested.alone), so it maps none: three mappings fewer to make, commit and unmap.
    if (!census->alone)
    {
        census->list_arena = check_attested_reserve(census, 2 * (uint64_t)BUSTER_ATTESTED_LIST_SLOTS * sizeof(CheckAttestedListSlot) + 4096);
        census->list_slots = arena_push_array(&census->list_arena, BUSTER_ATTESTED_LIST_FIRST_SLOTS, sizeof(CheckAttestedListSlot), 64);
        census->list_mask = BUSTER_ATTESTED_LIST_FIRST_SLOTS - 1;
        census->list_pool_arena = check_attested_reserve(census, (uint64_t)BUSTER_ATTESTED_LIST_POOL * sizeof(uint32_t) + 4096);
        census->list_pool = arena_push_array(&census->list_pool_arena, BUSTER_ATTESTED_LIST_POOL, sizeof(uint32_t), 64);
        census->tree_pool_arena = check_attested_reserve(census, (uint64_t)BUSTER_ATTESTED_TREE_POOL * sizeof(uint16_t) + 4096);
        census->tree_pool = arena_push_array(&census->tree_pool_arena, BUSTER_ATTESTED_TREE_POOL, sizeof(uint16_t), 64);
    }
    check_attested_actions(census->kind_actions, census->role_actions);
    return census->memo != NULL && (!census->alone || census->memo_index != NULL) && census->keys != NULL && census->list_marks != NULL &&
           census->body_marks != NULL && census->runtime_lengths != NULL &&
           census->id_words != NULL &&
           census->unit_arena.base != NULL &&
           (census->alone || (census->list_slots != NULL && census->list_pool != NULL && census->tree_pool != NULL));
}

static void check_attested_close(CheckAttested* census)
{
    check_attested_counters_close(census);
    arena_release(&census->fresh_arena);
    free(census->memo_path);
    census->memo_path = NULL;
    arena_release(&census->unit_arena);
    arena_release(&census->key_arena);
    arena_release(&census->memo_arena);
    arena_release(&census->list_arena);
    arena_release(&census->list_pool_arena);
    arena_release(&census->tree_pool_arena);
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
        result = result == 0 && !census->failed && census->units_observed != 0 && census->twin_differences == 0 ? 0 : 1;
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
        options = strcmp(argument, "--list") == 0 || strcmp(argument, "--passes") == 0 || strcmp(argument, "--twins") == 0;
        census.twins |= strcmp(argument, "--twins") == 0;
        if (strcmp(argument, "--list") == 0)
        {
            valid = first + 1 < count && check_attested_number(arguments[first + 1], &census.list);
        }
        else if (strcmp(argument, "--passes") == 0)
        {
            valid = first + 1 < count && check_attested_number(arguments[first + 1], &census.passes) && census.passes != 0 && census.passes <= 100;
        }
        first += !options ? 0 : strcmp(argument, "--twins") == 0 ? 1 : 2;
    }
    const int status = valid ? check_attested_run(&census, count - first, arguments + first, false) : 2;
    if (status == 2)
    {
        fprintf(stderr, "usage: compiler measure-attested [--list N] [--passes N] [--twins] [compile options] file...\n");
    }
    return status == 0 ? 0 : 1;
}

#endif
