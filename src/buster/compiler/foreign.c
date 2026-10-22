// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Linking other compilers' objects, steps 1 to 5 (docs/object-writer-and-linker.md, "Linking other compilers'
// objects"): a command line of ELF relocatable objects, archives (thin ones too), shared libraries and linker scripts
// of libc.so's subset (import.c's readers) opened, parsed and resolved; the archive members the link needs loaded; and
// COMDAT groups picked. What comes after (garbage collection, the relocation scan, merged strings, .eh_frame and the
// write) reads what this leaves: each loaded object's headers and global symbols, each global name's winner, and each
// group's fate.
//
// Every phase is one function that every lane of a group runs (lane.c; docs/object-writer-and-linker.md, "Lanes"),
// meeting at syncs, each lane writing only what it owns:
//   1. Open (foreign_open): the inputs found (-l's search, a script's inputs found as lld finds them) and mapped
//      privately, split across the lanes by count; lane 0 reads the scripts and puts their inputs in their place.
//   2. Indices and the parse (foreign_read_indices, foreign_parse): each archive's symbol index alone
//      (import_archive_lazy; under --whole-archive every member, import_archive) with its names hashed and indexed, each
//      library's exports (import_library); then round by round, the round's objects' headers, by object, and their
//      global symbols into columns of the round's own, by symbol (count, place, fill: rule 3).
//   3. Resolve (foreign_exchange, foreign_resolve): the round's records, a symbol's or a group's each, through the
//      exchange over 2^partition_bits partitions by the hash's top bits (rule 4, without compare-and-swap), into each
//      partition's tables, which its lane keeps from round to round.
//   4. Load archive members (foreign_resolve's decisions, foreign_load): each name a round touched decides whether its
//      first archive member loads; lane 0 marks the members the lanes need, and the next round parses them: a frontier
//      walk in rounds, until a round loads nothing.
//   5. COMDAT groups and the winners (foreign_groups, foreign_winners): each signature's group earliest by position
//      kept, the others' sections discarded; then each name's winner, its id, and each symbol's name id.
//
// Positions: an input's place in link order (scripts' inputs where the script is), and an archive member's at its
// archive's place, in member order (foreign_position). Every comparison of definitions goes by position, so ties go to
// the earlier input, whichever lane or round met them first.
//
// The results are GNU ld's, except where GNU ld's depend on the archives' order (a name two archives define, an archive
// that an input after it needs); there they are lld's and mold's. foreign_decide says how a member loads; the
// precedence of definitions is the ELF specification's and GNU ld's: strong, then common (the largest; ELF section 5.2
// ranks a common symbol above a weak definition, which mold doesn't), then weak, then a shared library's export, then
// none. Where the three linkers differ, and so this one from one of them:
//   - GNU ld's common rule (a common symbol loads the member that defines its name as global data): GNU ld's, which lld
//     lacks;
//   - an archive before the input that needs it, and a shared library's export of a name an archive defines too: lld's;
//   - ties between members of one archive (two weak definitions, commons of one size, COMDAT groups of one signature)
//     go by member order, as mold's do; GNU ld and lld order a member loaded for a member after it after that one.
//   - loading only adds: a member a round loads stays, where lld, which loads in input order, might find a definition
//     first that a later round of this walk finds.
//
// Each phase has a vector path and an exact scalar twin (ForeignPath), which give the same result (foreign_test.c
// compares them on every input, with groups of 1, 2, 3 and 8 lanes): the readers' two paths; the headers eight section
// headers at a time; the symbols eight at a time with their names hashed eight at a time; the records eight at a time;
// and the resolution, which on the vector path sorts each partition's round of records by hash first, so that each name
// is probed once. Opening is system calls, and the probes of the archives' and libraries' indices, the groups and the
// winners are a few records per name, scalar on both paths ("SIMD: what pays" in the design has the measurements).
//
// main.c includes this file after import.c and runtime.c, whose readers and linker names it uses. Layout, in this
// order:
//   - API: ForeignError, ForeignPath, ForeignKind, ForeignResolution, ForeignClass, ForeignArgument, ForeignCommand,
//     ForeignInput, ForeignArchive, ForeignLibrary, ForeignObject, ForeignSymbols, ForeignGroups, ForeignRecord,
//     ForeignEntry, ForeignGroupEntry, ForeignTable, ForeignNames, ForeignRound, ForeignPhase, ForeignFailure,
//     ForeignLane, Foreign and the entry points' prototypes.
//   - Positions and failures: foreign_position, foreign_failure, foreign_fail and foreign_failed.
//   - The command line: foreign_arguments.
//   - Opening: foreign_kind, foreign_find, foreign_map, foreign_open_input, foreign_expand_scripts and foreign_open.
//   - Archives' and libraries' indices: foreign_list_inputs, foreign_read_archive, foreign_read_library and
//     foreign_read_indices.
//   - The first round: foreign_first_round.
//   - The parse: foreign_member_bytes, foreign_check_tables, foreign_headers_exact, foreign_headers_vector,
//     foreign_object_headers, foreign_class, foreign_symbol_exact, foreign_symbols_vector, foreign_fill_symbols,
//     foreign_object_groups, foreign_round_columns and foreign_parse.
//   - The exchange: foreign_partition, foreign_records_vector, foreign_exchange_items and foreign_exchange.
//   - Resolution and archive members: the tables (foreign_entry, foreign_group_entry), foreign_touch,
//     foreign_resolve_exact, foreign_sort_records, foreign_resolve_vector, foreign_probe, foreign_member_defines_data,
//     foreign_decide, foreign_resolve and foreign_load.
//   - COMDAT groups and the winners: foreign_groups, foreign_win and foreign_winners.
//   - Running: foreign_lane_main, foreign_run, foreign_release and the accessors.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <elf.h>

// OS-dependent includes
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

// CPU architecture dependent includes
#include <immintrin.h>

// ---------------------------------------------------------------------------------------------------------------------
// API

typedef enum ForeignError
{
    FOREIGN_ERROR_NONE,
    // An option this linker doesn't take, or one without its value (ForeignFailure.argument says which).
    FOREIGN_ERROR_ARGUMENTS,
    // A file couldn't be opened, read or mapped: ForeignFailure.system_error says why.
    FOREIGN_ERROR_FILE,
    // A -l name, or a script's input, that no directory has.
    FOREIGN_ERROR_NOT_FOUND,
    // A file that is none of the inputs: not an ELF object or shared library, not an archive, not a script.
    FOREIGN_ERROR_FORMAT,
    // An input that doesn't read: ForeignFailure.import says what is wrong and where.
    FOREIGN_ERROR_MALFORMED,
    // Something valid this linker doesn't link: an archive without a symbol index, scripts nested too deep, a member
    // that isn't an object, more objects than a record holds.
    FOREIGN_ERROR_UNSUPPORTED,
    // An arena couldn't hold what a phase needs.
    FOREIGN_ERROR_MEMORY,
    // The lanes couldn't be started.
    FOREIGN_ERROR_LANES,
} ForeignError;

// Which way each phase runs (docs/object-writer-and-linker.md, "Step 5's phases, data-parallel"): its vector path, or
// its exact scalar twin, which is the vector path's oracle in the tests. Both give the same result.
typedef enum ForeignPath
{
    FOREIGN_PATH_VECTOR,
    FOREIGN_PATH_EXACT,
} ForeignPath;

// What an input is.
typedef enum ForeignKind
{
    FOREIGN_KIND_NONE,
    FOREIGN_KIND_OBJECT,
    FOREIGN_KIND_ARCHIVE,
    FOREIGN_KIND_LIBRARY,
    FOREIGN_KIND_SCRIPT,
} ForeignKind;

// An input's flags, from the options before it on the command line or from its script.
#define BUSTER_FOREIGN_INPUT_LIBRARY 0x01
#define BUSTER_FOREIGN_INPUT_WHOLE_ARCHIVE 0x02
#define BUSTER_FOREIGN_INPUT_AS_NEEDED 0x04
#define BUSTER_FOREIGN_INPUT_STATIC 0x08

// How a global name is resolved: to a strong, common or weak definition in a loaded object, to a shared library's
// export, to nothing for a name only weak references refer to (0 in the link), or to nothing for one a strong reference
// refers to, which the link must report unless garbage collection drops every reference.
typedef enum ForeignResolution
{
    FOREIGN_RESOLUTION_UNDEFINED,
    FOREIGN_RESOLUTION_STRONG,
    FOREIGN_RESOLUTION_COMMON,
    FOREIGN_RESOLUTION_WEAK,
    FOREIGN_RESOLUTION_SHARED,
    FOREIGN_RESOLUTION_WEAK_UNDEFINED,
    FOREIGN_RESOLUTIONS,
} ForeignResolution;

// What a global symbol of an object is to the resolution, in a record's tag: a strong definition (in a section, or
// absolute), a common symbol, a weak definition (GNU_UNIQUE's too, which g++ writes in COMDAT groups and which lld ranks
// with weak ones), a strong or a weak reference; and a COMDAT group's signature, whose records share the exchange.
typedef enum ForeignClass
{
    FOREIGN_CLASS_STRONG,
    FOREIGN_CLASS_COMMON,
    FOREIGN_CLASS_WEAK,
    FOREIGN_CLASS_REFERENCE,
    FOREIGN_CLASS_WEAK_REFERENCE,
    FOREIGN_CLASS_GROUP,
    FOREIGN_CLASSES,
} ForeignClass;

// One argument of the command line that names a file: a path, or with BUSTER_FOREIGN_INPUT_LIBRARY a -l name, and the
// flags the options before it set. A caller may give a file's bytes instead (the tests place inputs at pages' ends):
// they are used as they are, and never unmapped.
typedef struct ForeignArgument ForeignArgument;
struct ForeignArgument
{
    String8 name;
    uint8_t flags;
    String8 bytes;
};

// What foreign_arguments makes of a command line.
typedef struct ForeignCommand ForeignCommand;
struct ForeignCommand
{
    ForeignArgument* arguments;
    uint32_t argument_count;
    // -L's directories, in order.
    String8* directories;
    uint32_t directory_count;
    // -u's names: strong references from the command line, before every input.
    String8* undefined;
    uint32_t undefined_count;
};

// An input in link order, a script's inputs in place of the script: its path (NUL-terminated), its bytes (the file,
// mapped privately: an executable patches sections where they are), what it is, and its flags. `item` is its
// archive's, library's or object's index among them; `position` is its place in the order (see "Positions").
typedef struct ForeignInput ForeignInput;
struct ForeignInput
{
    // The name as the command line or the script gives it (a path, or a -l name), and the path found for it.
    String8 name;
    String8 path;
    String8 bytes;
    // Whether `bytes` are the caller's, which foreign_release leaves alone.
    bool given;
    ForeignKind kind;
    uint8_t flags;
    uint32_t item;
    // The script that named it plus 1, or 0 for the command line's; and how deep among scripts it is.
    uint32_t parent;
    uint32_t depth;
};

// An archive. Under --whole-archive every member loads: import_archive reads them all into `members`, numbered in
// archive order, and nothing probes the archive, since every name its members define is a loaded object's. Otherwise
// import_archive_lazy reads its symbol index alone, the members it names numbered by rank (in archive order), and
// members[m] is read when member m loads. Then each index entry's name's link_hash, an index of them by hash (lookup),
// and the members the link loads, a bit each, with each one's object plus 1.
typedef struct ForeignArchive ForeignArchive;
struct ForeignArchive
{
    uint32_t input;
    bool whole;
    bool thin;
    ImportArchiveIndex index;
    ImportMember* members;
    uint32_t member_count;
    uint64_t* hashes;
    ImportNameIndex lookup;
    uint64_t* loaded;
    uint32_t* member_objects;
};

// A shared library: its exports, versions and references (import_library), indexed by hash.
typedef struct ForeignLibrary ForeignLibrary;
struct ForeignLibrary
{
    uint32_t input;
    ImportLibrary library;
};

// A loaded object: a command-line object, or an archive's member the link loads. Its position (see "Positions"), its
// input, and its member's number plus 1 (0 for a command-line object); its bytes; what the parse found: the section
// table, the symbol table with its strings and extended section indices, the COMDAT groups and the relocation sections;
// where its global symbols and groups are among all objects' (global ids); and after the groups are picked, a byte per
// section, 1 for a section of a discarded group (only for an object with one).
typedef struct ForeignObject ForeignObject;
struct ForeignObject
{
    uint64_t position;
    uint32_t input;
    uint32_t member;
    String8 bytes;
    // Whether `bytes` is a mapping of its own (a thin archive's member), which foreign_release unmaps.
    bool mapped;
    // The header and section table, and the symbol table (import_elf and import_symbols), and the index of the first
    // global symbol (the symbol table's sh_info).
    ImportElf elf;
    ImportSymbols symbols;
    uint32_t first_global;
    // Its COMDAT groups (import_object_groups), in section order, with their sections.
    ImportGroup* group_list;
    uint32_t* group_sections;
    uint32_t global_begin;
    uint32_t global_count;
    uint32_t group_begin;
    uint32_t group_count;
    // Its SHT_RELA sections, in section order, each for a section of its own.
    uint32_t relocation_count;
    uint32_t* relocations;
    uint8_t* discarded;
};

// A round's global symbols, as columns indexed by a symbol's place in the round (its global id less the round's
// first): the round's objects' symbols one after another in object order. Each one's name (a slice of its object's
// string table) with its length and link_hash, its value, size and section index (st_shndx through SHN_XINDEX:
// SHN_UNDEF, SHN_ABS and SHN_COMMON as they are), st_info and st_other; the resolution's entry of its name in its
// partition's table; and in the end its name's id.
typedef struct ForeignSymbols ForeignSymbols;
struct ForeignSymbols
{
    const uint8_t** name;
    uint32_t* length;
    uint64_t* hash;
    uint64_t* value;
    uint64_t* size;
    uint32_t* section;
    uint8_t* info;
    uint8_t* other;
    uint32_t* entry;
    uint32_t* name_id;
};

// A round's COMDAT groups, as columns indexed by a group's place in the round, the objects' groups in object order:
// each one's signature with its length and link_hash, its object (in whose group_list it is its global id less the
// object's group_begin), its entry in its partition's group table, and whether it is kept: the first group of its
// signature by position.
typedef struct ForeignGroups ForeignGroups;
struct ForeignGroups
{
    const uint8_t** signature;
    uint32_t* length;
    uint64_t* hash;
    uint32_t* object;
    uint32_t* entry;
    uint8_t* kept;
};

// A record of the exchange (docs/object-writer-and-linker.md, "Lanes", rule 4): a name's hash, the symbol (or group) it
// is about, by its place in its round, and a tag: the class in bits 0 to 3, the symbol's visibility in bits 4 and 5,
// and its object in bits 8 up. 16 bytes, each crossing between lanes once.
typedef struct ForeignRecord ForeignRecord;
struct ForeignRecord
{
    uint64_t hash;
    uint32_t item;
    uint32_t tag;
};

// A name in a partition's table, 64 bytes: its hash and bytes (the first record's); what the rounds need to decide what
// to load: its flags (BUSTER_FOREIGN_ENTRY_*), the most constraining visibility of every record of it (a rank: default,
// protected, hidden, internal), the last round that touched it, and the positions of its earliest strong reference,
// definition (strong, common or weak) and common symbol; the archive member that defines it first (its archive plus 1,
// and the member), and the shared library that exports it first (its library plus 1, and the export).
typedef struct ForeignEntry ForeignEntry;
struct ForeignEntry
{
    uint64_t hash;
    const uint8_t* name;
    uint32_t length;
    uint8_t flags;
    uint8_t visibility;
    uint16_t round;
    uint64_t reference;
    uint64_t defined;
    uint64_t common;
    uint32_t owner_archive;
    uint32_t owner_member;
    uint32_t shared_library;
    uint32_t shared_export;
};
_Static_assert(sizeof(ForeignEntry) == 64, "an entry is a cache line");

// A COMDAT signature in a partition's group table: its hash and bytes, and the group of it with the earliest position.
typedef struct ForeignGroupEntry ForeignGroupEntry;
struct ForeignGroupEntry
{
    uint64_t hash;
    const uint8_t* signature;
    uint32_t length;
    uint32_t group;
    uint64_t position;
};

// A partition's tables, which its lane owns: names and group signatures, each open addressing on the hash's low bits
// (slot i holds an entry's index plus 1, or 0 when empty) over entries in the order their first records came; the
// entries this round touched; and in the end the id of its first name.
typedef struct ForeignTable ForeignTable;
struct ForeignTable
{
    uint32_t* slots;
    uint32_t slot_mask;
    ForeignEntry* entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t* group_slots;
    uint32_t group_slot_mask;
    ForeignGroupEntry* groups;
    uint32_t group_count;
    uint32_t group_capacity;
    uint32_t* touched;
    uint32_t touched_count;
    uint32_t touched_capacity;
    uint32_t name_base;
};

// The names, the resolution's output, as columns by name id (from 0, partition by partition, each partition's in the
// order their first records came): each one's bytes, length and hash; how it is resolved (ForeignResolution); the
// definition: a global symbol for one in an object, or for a shared library's its export's index in `symbol` and its
// library in `library`; how many strong definitions it has (two or more are a duplicate, and `second` is the second
// one's symbol); a common symbol's size and alignment (log2), the largest of its commons'; the most constraining
// visibility (STV_*) among its symbols; and whether a strong or a weak reference refers to it
// (BUSTER_FOREIGN_NAME_*).
typedef struct ForeignNames ForeignNames;
struct ForeignNames
{
    const uint8_t** bytes;
    uint32_t* length;
    uint64_t* hash;
    uint8_t* resolution;
    uint32_t* symbol;
    uint32_t* library;
    uint32_t* definitions;
    uint32_t* second;
    uint64_t* common_size;
    uint8_t* common_alignment;
    uint8_t* visibility;
    uint8_t* flags;
    uint32_t count;
};

#define BUSTER_FOREIGN_NAME_REFERENCED 0x01
#define BUSTER_FOREIGN_NAME_WEAK_REFERENCED 0x02

// A round: its objects (ids from first_object to end_object), its global symbols (global ids from first_symbol) and
// groups, each in columns of the round's own, pushed once their counts are known, so that the link touches no more
// memory than it uses; and its records, partition p's from partition_begin[p] to partition_begin[p + 1].
typedef struct ForeignRound ForeignRound;
struct ForeignRound
{
    uint32_t first_object;
    uint32_t end_object;
    uint32_t first_symbol;
    uint32_t symbol_count;
    uint32_t first_group;
    uint32_t group_count;
    ForeignSymbols symbols;
    ForeignGroups groups;
    ForeignRecord* records;
    uint64_t* partition_begin;
};

// The phases each lane times (ForeignLane.nanoseconds): opening, the archives' and libraries' indices, the objects'
// headers and their symbols (the parse), the exchange, the rounds' resolution, the groups and the winners.
typedef enum ForeignPhase
{
    FOREIGN_PHASE_OPEN,
    FOREIGN_PHASE_INDICES,
    FOREIGN_PHASE_HEADERS,
    FOREIGN_PHASE_SYMBOLS,
    FOREIGN_PHASE_EXCHANGE,
    FOREIGN_PHASE_RESOLVE,
    FOREIGN_PHASE_LOAD,
    FOREIGN_PHASE_GROUPS,
    FOREIGN_PHASE_WINNERS,
    FOREIGN_PHASES,
} ForeignPhase;

// A failure: what failed, the input (and member plus 1) or object it is about, its position, the reader's result (what
// is wrong in the file, and where), errno for a file, and the argument for an option.
typedef struct ForeignFailure ForeignFailure;
struct ForeignFailure
{
    ForeignError error;
    uint64_t position;
    uint32_t input;
    uint32_t member;
    ImportResult import;
    int system_error;
    uint32_t argument;
};

// What each lane owns: its arena (the partitions' tables, its lists), its first failure, the members its partitions
// need this round, and how long each phase took it, until it arrived at the phase's last sync.
typedef struct ForeignLane ForeignLane;
struct ForeignLane
{
    _Alignas(64) Arena arena;
    ForeignFailure failure;
    uint64_t* needs;
    uint32_t need_count;
    uint32_t need_capacity;
    uint64_t nanoseconds[FOREIGN_PHASES];
};

// The state every lane shares. foreign_run fills it from a command, on a group of `lanes` lanes.
typedef struct Foreign Foreign;
struct Foreign
{
    // The options.
    ForeignPath path;
    uint32_t lanes;
    const ForeignCommand* command;
    // Where the shared columns are pushed from, by lane 0 between syncs.
    Arena arena;
    // The inputs in link order, and their archives and libraries.
    ForeignInput* inputs;
    uint32_t input_count;
    ForeignArchive* archives;
    uint32_t archive_count;
    ForeignLibrary* libraries;
    uint32_t library_count;
    // The loaded objects, by id: object 0 holds the command line's references (-u), then each round's in position
    // order; and each one's position, which the resolution reads per record.
    ForeignObject* objects;
    uint32_t object_count;
    uint32_t object_capacity;
    uint64_t* positions;
    // The rounds: round_count of them once the last is done.
    ForeignRound* rounds;
    uint32_t round_count;
    // The exchange: 2^partition_bits partitions by the hash's top bits, each lane's counts per partition, and each
    // partition's tables.
    uint32_t partition_bits;
    uint32_t partition_count;
    uint32_t* counts;
    ForeignTable* tables;
    // The output.
    ForeignNames names;
    // The members the next round loads, which lane 0 lists, and how many.
    uint32_t next_objects;
    // Each lane's own.
    ForeignLane* lane_states;
    // The first failure by position, once the lanes are done.
    ForeignFailure failure;
};

// Reads a command line's inputs and the options this linker takes: paths; -l NAME and -lNAME (-l:FILE for a file by
// name); -L DIR and -LDIR; -u NAME, --undefined NAME and --undefined=NAME; --whole-archive and --no-whole-archive;
// --as-needed and --no-as-needed; -Bstatic, -Bdynamic, -static, -dn and -dy; and --start-group, --end-group, -( and -),
// which the frontier walk makes moot (see "Archive members"). Arrays from `arena`. Returns the failure, with the index
// of the argument it is about.
static __attribute__((unused)) ForeignFailure foreign_arguments(ForeignCommand* command, int argc, char** argv, Arena* arena);
// Resolves `command` on a group of `lanes` lanes (pinned to `cpus`, if given), by `path`: fills `foreign`, whose arena
// it reserves. Returns the first failure by position (also foreign->failure); release `foreign` either way.
static __attribute__((unused)) ForeignFailure foreign_run(Foreign* foreign, const ForeignCommand* command, ForeignPath path, uint32_t lanes, const uint32_t* cpus);
// Unmaps the inputs and releases the arena.
static __attribute__((unused)) void foreign_release(Foreign* foreign);
// The id of the loaded object a global symbol belongs to.
static __attribute__((unused)) uint32_t foreign_symbol_object(const Foreign* foreign, uint32_t symbol);
// The round a global symbol belongs to; its columns are that round's, at the symbol's id less the round's first.
static __attribute__((unused)) const ForeignRound* foreign_symbol_round(const Foreign* foreign, uint32_t symbol);
// The index in its object's symbol table of a global symbol.
static __attribute__((unused)) uint32_t foreign_symbol_index(const Foreign* foreign, uint32_t symbol);
// The name id of `name` (link_hash `hash`), plus 1, or 0 if no loaded object has a global symbol of it.
static __attribute__((unused)) uint32_t foreign_find_name(const Foreign* foreign, const uint8_t* name, uint32_t length, uint64_t hash);

// ---------------------------------------------------------------------------------------------------------------------
// Positions and failures
//
// A position orders everything the resolution compares: inputs in link order, and an archive's members at the
// archive's place, in member order. Ties between definitions go to the earlier position (see "Resolution").

// The command line's own references (-u) come before every input.
#define BUSTER_FOREIGN_COMMAND_POSITION 0
#define BUSTER_FOREIGN_NO_POSITION UINT64_MAX
// Scripts may name scripts this deep.
#define BUSTER_FOREIGN_MAX_DEPTH 8
// A record's tag holds an object's id in its bits 8 up.
#define BUSTER_FOREIGN_TAG_OBJECT_SHIFT 8
#define BUSTER_FOREIGN_TAG_CLASS 0x0f
#define BUSTER_FOREIGN_TAG_VISIBILITY_SHIFT 4
#define BUSTER_FOREIGN_MAX_OBJECTS (1u << (32 - BUSTER_FOREIGN_TAG_OBJECT_SHIFT))
// The shared arena, from which lane 0 pushes the columns every lane fills; and each lane's own.
#define BUSTER_FOREIGN_ARENA_BYTES (1ull << 40)
#define BUSTER_FOREIGN_LANE_ARENA_BYTES (1ull << 36)
#define BUSTER_FOREIGN_PATH_BYTES 4096

// An input's position: its place in link order, from 1 (0 is the command line's), in the high 32 bits, and in the low
// ones a member's index plus 1, or 0 for anything but an archive's member.
static uint64_t foreign_position(uint32_t input, uint32_t member)
{
    return (uint64_t)(input + 1) << 32 | member;
}

static ForeignFailure foreign_failure(ForeignError error, uint64_t position, uint32_t input, uint32_t member)
{
    ForeignFailure result = {
        .error = error,
        .position = position,
        .input = input,
        .member = member,
    };
    return result;
}

// Whether `failure` comes before `kept`: by position, then by its offset in the file, so that the failure reported is
// the first in link order whichever lane met it.
static bool foreign_earlier(const ForeignFailure* failure, const ForeignFailure* kept)
{
    return kept->error == FOREIGN_ERROR_NONE || failure->position < kept->position || (failure->position == kept->position && failure->import.offset < kept->import.offset);
}

// Records a failure of this lane's, keeping its first.
static void foreign_fail(ForeignLane* lane, ForeignFailure failure)
{
    if (failure.error != FOREIGN_ERROR_NONE && foreign_earlier(&failure, &lane->failure))
    {
        lane->failure = failure;
    }
}

// After a sync: whether any lane has failed. Every lane reads every lane's failure, so every lane decides alike.
static bool foreign_failed(const Foreign* foreign)
{
    bool result = false;
    for (uint32_t i = 0; i < foreign->lanes; i += 1)
    {
        result = result || foreign->lane_states[i].failure.error != FOREIGN_ERROR_NONE;
    }
    return result;
}

static uint64_t foreign_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Adds this lane's time since `start` to the phase, and returns the time now: a phase ends at its last sync, which the
// caller makes after this.
static uint64_t foreign_time(ForeignLane* lane, ForeignPhase phase, uint64_t start)
{
    const uint64_t now = foreign_now();
    lane->nanoseconds[phase] += now - start;
    return now;
}

// ---------------------------------------------------------------------------------------------------------------------
// The command line

static bool foreign_starts_with(const char* text, const char* prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static String8 foreign_string(const char* text)
{
    String8 result = {
        .pointer = (char*)text,
        .length = strlen(text),
    };
    return result;
}

static ForeignFailure foreign_arguments(ForeignCommand* command, int argc, char** argv, Arena* arena)
{
    ForeignFailure result = { 0 };
    const uint64_t count = argc > 0 ? (uint64_t)argc : 0;
    *command = (ForeignCommand){
        .arguments = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignArgument, count + 1),
        .directories = BUSTER_ARENA_PUSH_ARRAY(arena, String8, count + 1),
        .undefined = BUSTER_ARENA_PUSH_ARRAY(arena, String8, count + 1),
    };
    if (command->arguments == 0 || command->directories == 0 || command->undefined == 0)
    {
        result = foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0);
    }
    uint8_t flags = 0;
    for (uint64_t i = 0; result.error == FOREIGN_ERROR_NONE && i < count; i += 1)
    {
        const char* argument = argv[i];
        // An option whose value is the next argument, or in this one after the option's letters.
        const bool separate = strcmp(argument, "-l") == 0 || strcmp(argument, "-L") == 0 || strcmp(argument, "-u") == 0 || strcmp(argument, "--undefined") == 0;
        const char* value = separate && i + 1 < count ? argv[i + 1] : 0;
        const bool library = strcmp(argument, "-l") == 0 || (!separate && foreign_starts_with(argument, "-l"));
        const bool directory = strcmp(argument, "-L") == 0 || (!separate && foreign_starts_with(argument, "-L"));
        const bool undefined = strcmp(argument, "-u") == 0 || strcmp(argument, "--undefined") == 0 || (!separate && foreign_starts_with(argument, "--undefined=")) ||
                               (!separate && foreign_starts_with(argument, "-u"));
        if (!separate)
        {
            value = library || directory ? argument + 2 : foreign_starts_with(argument, "--undefined=") ? argument + strlen("--undefined=") : undefined ? argument + 2 : 0;
        }
        i += separate ? 1 : 0;
        if ((library || directory || undefined) && (value == 0 || value[0] == 0))
        {
            result = foreign_failure(FOREIGN_ERROR_ARGUMENTS, 0, 0, 0);
            result.argument = (uint32_t)(separate ? i - 1 : i);
        }
        else if (library)
        {
            command->arguments[command->argument_count] = (ForeignArgument){
                .name = foreign_string(value),
                .flags = (uint8_t)(flags | BUSTER_FOREIGN_INPUT_LIBRARY),
            };
            command->argument_count += 1;
        }
        else if (directory)
        {
            command->directories[command->directory_count] = foreign_string(value);
            command->directory_count += 1;
        }
        else if (undefined)
        {
            command->undefined[command->undefined_count] = foreign_string(value);
            command->undefined_count += 1;
        }
        else if (strcmp(argument, "--whole-archive") == 0)
        {
            flags |= BUSTER_FOREIGN_INPUT_WHOLE_ARCHIVE;
        }
        else if (strcmp(argument, "--no-whole-archive") == 0)
        {
            flags &= (uint8_t)~BUSTER_FOREIGN_INPUT_WHOLE_ARCHIVE;
        }
        else if (strcmp(argument, "--as-needed") == 0)
        {
            flags |= BUSTER_FOREIGN_INPUT_AS_NEEDED;
        }
        else if (strcmp(argument, "--no-as-needed") == 0)
        {
            flags &= (uint8_t)~BUSTER_FOREIGN_INPUT_AS_NEEDED;
        }
        else if (strcmp(argument, "-Bstatic") == 0 || strcmp(argument, "-static") == 0 || strcmp(argument, "-dn") == 0 || strcmp(argument, "-non_shared") == 0)
        {
            flags |= BUSTER_FOREIGN_INPUT_STATIC;
        }
        else if (strcmp(argument, "-Bdynamic") == 0 || strcmp(argument, "-dy") == 0 || strcmp(argument, "-call_shared") == 0)
        {
            flags &= (uint8_t)~BUSTER_FOREIGN_INPUT_STATIC;
        }
        else if (strcmp(argument, "--start-group") == 0 || strcmp(argument, "--end-group") == 0 || strcmp(argument, "-(") == 0 || strcmp(argument, "-)") == 0)
        {
            // Every archive's definitions are in reach of every reference (see "Archive members").
        }
        else if (argument[0] == '-' && argument[1] != 0)
        {
            result = foreign_failure(FOREIGN_ERROR_ARGUMENTS, 0, 0, 0);
            result.argument = (uint32_t)i;
        }
        else
        {
            command->arguments[command->argument_count] = (ForeignArgument){
                .name = foreign_string(argument),
                .flags = flags,
            };
            command->argument_count += 1;
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Opening (step 1)
//
// The lanes split the inputs by count (opening a file is system calls, whatever its size: mmap reads nothing) and each
// maps its own. A -l name is searched for first: in each -L directory in order, lib<name>.so then lib<name>.a, or only
// the latter under -Bstatic, or the file itself for -l:<file>. Then lane 0 reads each script (import_script: libc.so's
// subset) and puts its inputs where it was, after it, each found as lld finds one: an absolute path as it is, a -l name
// by the search, and a relative path in the script's directory, then the current one, then the -L directories; and the
// lanes open those, until no input is a script. Scripts stay in the list, which keeps the positions in link order.

static ForeignKind foreign_kind(String8 bytes)
{
    const uint64_t magic = sizeof(BUSTER_IMPORT_ARCHIVE_MAGIC) - 1;
    ForeignKind result = FOREIGN_KIND_SCRIPT;
    if (bytes.length >= sizeof(Elf64_Ehdr) && memcmp(bytes.pointer, ELFMAG, SELFMAG) == 0)
    {
        uint16_t type = 0;
        memcpy(&type, bytes.pointer + offsetof(Elf64_Ehdr, e_type), sizeof(type));
        result = type == ET_REL ? FOREIGN_KIND_OBJECT : type == ET_DYN ? FOREIGN_KIND_LIBRARY : FOREIGN_KIND_NONE;
    }
    else if (bytes.length >= magic && (memcmp(bytes.pointer, BUSTER_IMPORT_ARCHIVE_MAGIC, magic) == 0 || memcmp(bytes.pointer, BUSTER_IMPORT_THIN_ARCHIVE_MAGIC, magic) == 0))
    {
        result = FOREIGN_KIND_ARCHIVE;
    }
    else if (bytes.length >= SELFMAG && memcmp(bytes.pointer, ELFMAG, SELFMAG) == 0)
    {
        result = FOREIGN_KIND_NONE;
    }
    return result;
}

// Whether `path` is a regular file.
static bool foreign_exists(const char* path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

// `directory`/`name` (or `name` alone without a directory) into `path`, NUL-terminated: false if it doesn't fit.
static bool foreign_join(char* path, String8 directory, const char* prefix, String8 name, const char* suffix)
{
    const uint64_t prefix_length = strlen(prefix);
    const uint64_t suffix_length = strlen(suffix);
    const uint64_t length = directory.length + (directory.length != 0 ? 1 : 0) + prefix_length + name.length + suffix_length;
    const bool result = length < BUSTER_FOREIGN_PATH_BYTES;
    if (result)
    {
        uint64_t at = 0;
        memcpy(path, directory.pointer, directory.length);
        at += directory.length;
        if (directory.length != 0)
        {
            path[at] = '/';
            at += 1;
        }
        memcpy(path + at, prefix, prefix_length);
        at += prefix_length;
        memcpy(path + at, name.pointer, name.length);
        at += name.length;
        memcpy(path + at, suffix, suffix_length);
        path[length] = 0;
    }
    return result;
}

// The directory part of a path, without its last '/' (empty for a path without one).
static String8 foreign_directory(String8 path)
{
    String8 result = {
        .pointer = path.pointer,
        .length = 0,
    };
    for (uint64_t i = 0; i < path.length; i += 1)
    {
        result.length = path.pointer[i] == '/' ? i : result.length;
    }
    return result;
}

// Finds input `index`'s file into `path`: false if no place has it.
static bool foreign_find(const Foreign* foreign, const ForeignInput* input, char* path)
{
    const ForeignCommand* command = foreign->command;
    const String8 none = { 0 };
    bool result = false;
    if ((input->flags & BUSTER_FOREIGN_INPUT_LIBRARY) != 0)
    {
        const bool exact = input->name.length > 1 && input->name.pointer[0] == ':';
        const String8 file = {
            .pointer = input->name.pointer + (exact ? 1 : 0),
            .length = input->name.length - (exact ? 1 : 0),
        };
        const bool dynamic = (input->flags & BUSTER_FOREIGN_INPUT_STATIC) == 0;
        for (uint32_t d = 0; !result && d < command->directory_count; d += 1)
        {
            const String8 directory = command->directories[d];
            if (exact)
            {
                result = foreign_join(path, directory, "", file, "") && foreign_exists(path);
            }
            else
            {
                result = dynamic && foreign_join(path, directory, "lib", file, ".so") && foreign_exists(path);
                result = result || (foreign_join(path, directory, "lib", file, ".a") && foreign_exists(path));
            }
        }
    }
    else if (input->parent == 0 || (input->name.length != 0 && input->name.pointer[0] == '/'))
    {
        result = foreign_join(path, none, "", input->name, "");
    }
    else
    {
        const String8 script = foreign_directory(foreign->inputs[input->parent - 1].path);
        result = script.length != 0 && foreign_join(path, script, "", input->name, "") && foreign_exists(path);
        result = result || (foreign_join(path, none, "", input->name, "") && foreign_exists(path));
        for (uint32_t d = 0; !result && d < command->directory_count; d += 1)
        {
            result = foreign_join(path, command->directories[d], "", input->name, "") && foreign_exists(path);
        }
    }
    return result;
}

// Maps a file privately and writably (a later phase patches sections in place): its bytes, or empty with *error set.
static String8 foreign_map(const char* path, int* error)
{
    String8 result = { 0 };
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    struct stat status;
    *error = 0;
    if (file < 0 || fstat(file, &status) != 0)
    {
        *error = errno;
    }
    else if (!S_ISREG(status.st_mode))
    {
        *error = S_ISDIR(status.st_mode) ? EISDIR : EINVAL;
    }
    else if (status.st_size != 0)
    {
        void* mapping = mmap(0, (uint64_t)status.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, file, 0);
        if (mapping == MAP_FAILED)
        {
            *error = errno;
        }
        else
        {
            result.pointer = mapping;
            result.length = (uint64_t)status.st_size;
        }
    }
    if (file >= 0)
    {
        close(file);
    }
    return result;
}

// Finds and maps input `index`, and tells what it is. Its path is a copy in the lane's arena.
static void foreign_open_input(Foreign* foreign, uint32_t index, ForeignLane* lane)
{
    ForeignInput* input = &foreign->inputs[index];
    const uint64_t position = foreign_position(index, 0);
    char path[BUSTER_FOREIGN_PATH_BYTES];
    if (input->given)
    {
        input->path = input->name;
        input->kind = foreign_kind(input->bytes);
        if (input->kind == FOREIGN_KIND_NONE)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_FORMAT, position, index, 0));
        }
    }
    else if (!foreign_find(foreign, input, path))
    {
        foreign_fail(lane, foreign_failure((input->flags & BUSTER_FOREIGN_INPUT_LIBRARY) != 0 || input->parent != 0 ? FOREIGN_ERROR_NOT_FOUND : FOREIGN_ERROR_FILE, position, index, 0));
        lane->failure.system_error = lane->failure.error == FOREIGN_ERROR_FILE ? ENAMETOOLONG : 0;
    }
    else
    {
        const uint64_t length = strlen(path);
        char* copy = arena_push(&lane->arena, length + 1, 1);
        int error = 0;
        if (copy == 0)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, position, index, 0));
        }
        else
        {
            memcpy(copy, path, length + 1);
            input->path = (String8){
                .pointer = copy,
                .length = length,
            };
            input->bytes = foreign_map(copy, &error);
            input->kind = error == 0 ? foreign_kind(input->bytes) : FOREIGN_KIND_NONE;
        }
        if (copy != 0 && error != 0)
        {
            ForeignFailure failure = foreign_failure(FOREIGN_ERROR_FILE, position, index, 0);
            failure.system_error = error;
            foreign_fail(lane, failure);
        }
        else if (copy != 0 && input->kind == FOREIGN_KIND_NONE)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_FORMAT, position, index, 0));
        }
    }
}

// Lane 0's part: each script not yet read gets its inputs after it, unopened, in a new list. Returns how many inputs
// it added, or UINT32_MAX after a failure.
static uint32_t foreign_expand_scripts(Foreign* foreign, ForeignLane* lane)
{
    uint32_t added = 0;
    uint32_t total = foreign->input_count;
    bool failed = false;
    Scratch scratch = scratch_begin(&lane->arena);
    ImportScript* scripts = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ImportScript, foreign->input_count);
    failed = scripts == 0 && foreign->input_count != 0;
    for (uint32_t i = 0; !failed && i < foreign->input_count; i += 1)
    {
        ForeignInput* input = &foreign->inputs[i];
        scripts[i] = (ImportScript){ 0 };
        // A script's item is 1 once it has been read.
        if (input->kind == FOREIGN_KIND_SCRIPT && input->item == 0)
        {
            const ImportResult read = import_script(&scripts[i], input->bytes, scratch.arena);
            if (read.error != IMPORT_ERROR_NONE)
            {
                ForeignFailure failure = foreign_failure(FOREIGN_ERROR_MALFORMED, foreign_position(i, 0), i, 0);
                failure.import = read;
                foreign_fail(lane, failure);
                failed = true;
            }
            else if (scripts[i].input_count != 0 && input->depth + 1 >= BUSTER_FOREIGN_MAX_DEPTH)
            {
                foreign_fail(lane, foreign_failure(FOREIGN_ERROR_UNSUPPORTED, foreign_position(i, 0), i, 0));
                failed = true;
            }
            added += scripts[i].input_count;
            total += scripts[i].input_count;
        }
    }
    ForeignInput* inputs = !failed && added != 0 ? BUSTER_ARENA_PUSH_ARRAY(&foreign->arena, ForeignInput, total) : 0;
    if (!failed && added != 0 && inputs == 0)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
        failed = true;
    }
    if (!failed && added != 0)
    {
        // Old index i is new index place[i]; a script's inputs follow it.
        uint32_t* place = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, foreign->input_count);
        uint32_t at = 0;
        for (uint32_t i = 0; place != 0 && i < foreign->input_count; i += 1)
        {
            place[i] = at;
            at += 1 + (foreign->inputs[i].kind == FOREIGN_KIND_SCRIPT && foreign->inputs[i].item == 0 ? scripts[i].input_count : 0);
        }
        failed = place == 0;
        at = 0;
        for (uint32_t i = 0; !failed && i < foreign->input_count; i += 1)
        {
            ForeignInput input = foreign->inputs[i];
            const bool expand = input.kind == FOREIGN_KIND_SCRIPT && input.item == 0;
            input.parent = input.parent != 0 ? place[input.parent - 1] + 1 : 0;
            input.item = expand ? 1 : input.item;
            inputs[at] = input;
            at += 1;
            for (uint32_t k = 0; expand && k < scripts[i].input_count; k += 1)
            {
                const ImportScriptInput* named = &scripts[i].inputs[k];
                const uint8_t inherited = input.flags & (BUSTER_FOREIGN_INPUT_WHOLE_ARCHIVE | BUSTER_FOREIGN_INPUT_AS_NEEDED | BUSTER_FOREIGN_INPUT_STATIC);
                inputs[at] = (ForeignInput){
                    .name = named->name,
                    .flags = (uint8_t)(inherited | (named->library ? BUSTER_FOREIGN_INPUT_LIBRARY : 0) | (named->as_needed ? BUSTER_FOREIGN_INPUT_AS_NEEDED : 0)),
                    .parent = place[i] + 1,
                    .depth = input.depth + 1,
                };
                at += 1;
            }
        }
        foreign->inputs = failed ? foreign->inputs : inputs;
        foreign->input_count = failed ? foreign->input_count : total;
    }
    if (!failed)
    {
        // A script without inputs, or whose inputs are all in place: read.
        for (uint32_t i = 0; i < foreign->input_count; i += 1)
        {
            ForeignInput* input = &foreign->inputs[i];
            input->item = input->kind == FOREIGN_KIND_SCRIPT ? 1 : input->item;
        }
    }
    scratch_end(scratch);
    return failed ? UINT32_MAX : added;
}

// Step 1: the inputs found, mapped and told apart, with every script's inputs in its place.
static void foreign_open(Foreign* foreign, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    bool more = true;
    while (more)
    {
        const Range64 mine = lane_range(foreign->input_count);
        for (uint64_t i = mine.begin; i < mine.end; i += 1)
        {
            if (foreign->inputs[i].kind == FOREIGN_KIND_NONE && foreign->inputs[i].path.length == 0)
            {
                foreign_open_input(foreign, (uint32_t)i, lane);
            }
        }
        lane_sync();
        uint32_t added = 0;
        if (lane_index() == 0)
        {
            added = foreign_failed(foreign) ? 0 : foreign_expand_scripts(foreign, lane);
        }
        added = (uint32_t)lane_broadcast(added, 0);
        more = added != 0 && added != UINT32_MAX;
    }
    start = foreign_time(lane, FOREIGN_PHASE_OPEN, start);
    (void)start;
}

// ---------------------------------------------------------------------------------------------------------------------
// Archives' and libraries' indices
//
// Each archive's symbol index is read alone (import_archive_lazy, or on the exact path import_archive_lazy_scalar):
// none of its members, whose headers a later round reads when it loads them, as GNU ld seeks to the members its scan
// loads. An archive under --whole-archive is read whole (import_archive, or import_archive_scalar). Each index entry's
// name gets its link_hash (link_hash_names eight at a time, or one link_hash at a time), and the entries an
// ImportNameIndex by hash, in which a name's owner is found. Each library's exports (import_library, or
// import_library_scalar) come indexed. The lanes split the archives and libraries by bytes.

// Lane 0, after opening: the archives and libraries listed in link order, and each input's item.
static bool foreign_list_inputs(Foreign* foreign, ForeignLane* lane)
{
    uint32_t archives = 0;
    uint32_t libraries = 0;
    for (uint32_t i = 0; i < foreign->input_count; i += 1)
    {
        archives += foreign->inputs[i].kind == FOREIGN_KIND_ARCHIVE ? 1 : 0;
        libraries += foreign->inputs[i].kind == FOREIGN_KIND_LIBRARY ? 1 : 0;
    }
    foreign->archives = BUSTER_ARENA_PUSH_ARRAY(&foreign->arena, ForeignArchive, archives + 1);
    foreign->libraries = BUSTER_ARENA_PUSH_ARRAY(&foreign->arena, ForeignLibrary, libraries + 1);
    const bool result = foreign->archives != 0 && foreign->libraries != 0;
    for (uint32_t i = 0; result && i < foreign->input_count; i += 1)
    {
        ForeignInput* input = &foreign->inputs[i];
        if (input->kind == FOREIGN_KIND_ARCHIVE)
        {
            input->item = foreign->archive_count;
            foreign->archives[foreign->archive_count] = (ForeignArchive){
                .input = i,
                .whole = (input->flags & BUSTER_FOREIGN_INPUT_WHOLE_ARCHIVE) != 0,
            };
            foreign->archive_count += 1;
        }
        else if (input->kind == FOREIGN_KIND_LIBRARY)
        {
            input->item = foreign->library_count;
            foreign->libraries[foreign->library_count] = (ForeignLibrary){
                .input = i,
            };
            foreign->library_count += 1;
        }
    }
    if (!result)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
    }
    return result;
}

static ForeignFailure foreign_import_failure(ImportResult read, uint64_t position, uint32_t input, uint32_t member)
{
    ForeignFailure result = foreign_failure(read.error == IMPORT_ERROR_MEMORY ? FOREIGN_ERROR_MEMORY : FOREIGN_ERROR_MALFORMED, position, input, member);
    result.import = read;
    return result;
}

// Reads archive `a`: its index alone, or under --whole-archive every member; then its index's names hashed and
// indexed.
static void foreign_read_archive(Foreign* foreign, uint32_t a, ForeignLane* lane)
{
    ForeignArchive* archive = &foreign->archives[a];
    const ForeignInput* input = &foreign->inputs[archive->input];
    const uint64_t position = foreign_position(archive->input, 0);
    const bool vector = foreign->path == FOREIGN_PATH_VECTOR;
    ImportResult read = { 0 };
    if (archive->whole)
    {
        ImportArchive whole;
        read = vector ? import_archive(&whole, input->bytes, &lane->arena) : import_archive_scalar(&whole, input->bytes, &lane->arena);
        archive->thin = whole.thin;
        archive->members = whole.members;
        archive->member_count = read.error == IMPORT_ERROR_NONE ? whole.member_count : 0;
    }
    else
    {
        read = vector ? import_archive_lazy(&archive->index, input->bytes, &lane->arena) : import_archive_lazy_scalar(&archive->index, input->bytes, &lane->arena);
        archive->thin = archive->index.thin;
        archive->member_count = read.error == IMPORT_ERROR_NONE ? archive->index.member_count : 0;
        archive->members = BUSTER_ARENA_PUSH_ARRAY(&lane->arena, ImportMember, (uint64_t)archive->member_count + 1);
    }
    const uint32_t count = archive->index.symbol_count;
    const uint32_t members = archive->member_count;
    // An archive with members but no index: GNU ld doesn't link it ("archive has no index; run ranlib to add one").
    const bool unindexed = !archive->whole && read.error == IMPORT_ERROR_NONE && !archive->index.indexed && archive->index.members;
    archive->hashes = BUSTER_ARENA_PUSH_ARRAY(&lane->arena, uint64_t, (uint64_t)count + 1);
    archive->loaded = BUSTER_ARENA_PUSH_ARRAY(&lane->arena, uint64_t, members / 64 + 1);
    archive->member_objects = BUSTER_ARENA_PUSH_ARRAY(&lane->arena, uint32_t, (uint64_t)members + 1);
    if (read.error != IMPORT_ERROR_NONE)
    {
        foreign_fail(lane, foreign_import_failure(read, position, archive->input, 0));
    }
    else if (unindexed)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_UNSUPPORTED, position, archive->input, 0));
    }
    else if (archive->members == 0 || archive->hashes == 0 || archive->loaded == 0 || archive->member_objects == 0)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, position, archive->input, 0));
    }
    else
    {
        memset(archive->loaded, 0, (members / 64 + 1) * sizeof(uint64_t));
        memset(archive->member_objects, 0, ((uint64_t)members + 1) * sizeof(uint32_t));
        if (!archive->whole)
        {
            memset(archive->members, 0, ((uint64_t)members + 1) * sizeof(ImportMember));
        }
        Scratch scratch = scratch_begin(&lane->arena);
        const uint8_t** names = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, const uint8_t*, (uint64_t)count + 1);
        uint32_t* lengths = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + 1);
        bool fits = names != 0 && lengths != 0;
        for (uint32_t i = 0; fits && i < count; i += 1)
        {
            const String8 name = archive->index.symbols[i].name;
            fits = name.length <= UINT32_MAX;
            names[i] = (const uint8_t*)name.pointer;
            lengths[i] = (uint32_t)name.length;
        }
        if (fits && vector)
        {
            link_hash_names(names, lengths, count, archive->hashes);
        }
        for (uint32_t i = 0; fits && !vector && i < count; i += 1)
        {
            archive->hashes[i] = link_hash(names[i], lengths[i]);
        }
        fits = fits && import_index_build(&archive->lookup, archive->hashes, count, &lane->arena);
        if (!fits)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, position, archive->input, 0));
        }
        scratch_end(scratch);
    }
}

static void foreign_read_library(Foreign* foreign, uint32_t l, ForeignLane* lane)
{
    ForeignLibrary* library = &foreign->libraries[l];
    const ForeignInput* input = &foreign->inputs[library->input];
    const ImportResult read = foreign->path == FOREIGN_PATH_VECTOR ? import_library(&library->library, input->bytes, &lane->arena) : import_library_scalar(&library->library, input->bytes, &lane->arena);
    if (read.error != IMPORT_ERROR_NONE)
    {
        foreign_fail(lane, foreign_import_failure(read, foreign_position(library->input, 0), library->input, 0));
    }
}

// The archives and libraries read, split across the lanes by their bytes: archive a is item a, library l item
// archive_count + l.
static void foreign_read_indices(Foreign* foreign, ForeignLane* lane)
{
    const uint32_t items = foreign->archive_count + foreign->library_count;
    Scratch scratch = scratch_begin(&lane->arena);
    uint64_t* cost = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, items + 1);
    uint64_t total = 0;
    for (uint32_t i = 0; cost != 0 && i < items; i += 1)
    {
        const uint32_t input = i < foreign->archive_count ? foreign->archives[i].input : foreign->libraries[i - foreign->archive_count].input;
        total += foreign->inputs[input].bytes.length;
        cost[i] = total;
    }
    const Range64 mine = cost != 0 ? lane_range_by_cost(cost, items) : lane_range(items);
    for (uint64_t i = mine.begin; i < mine.end; i += 1)
    {
        if (i < foreign->archive_count)
        {
            foreign_read_archive(foreign, (uint32_t)i, lane);
        }
        else
        {
            foreign_read_library(foreign, (uint32_t)(i - foreign->archive_count), lane);
        }
    }
    scratch_end(scratch);
}

// ---------------------------------------------------------------------------------------------------------------------
// The first round
//
// Round 0 loads object 0 (the command line's references, -u), every command-line object and every member of an archive
// under --whole-archive, in position order. The objects get room for every object the link could load; each round's
// symbols, groups and records get columns of their own once its headers have counted them (ForeignRound).

// The partitions, by how many records round 0's objects could have (a symbol per 24 bytes of each): about
// BUSTER_FOREIGN_PARTITION_RECORDS each, from 1 to 2^BUSTER_FOREIGN_MAX_PARTITION_BITS. They depend on the input's size,
// never on the lanes (rule 5).
#define BUSTER_FOREIGN_PARTITION_RECORDS 2048
#define BUSTER_FOREIGN_MAX_PARTITION_BITS 12

// Lane 0: round 0's objects listed, and the objects, rounds, counts and tables pushed.
static bool foreign_first_round(Foreign* foreign, ForeignLane* lane)
{
    const ForeignCommand* command = foreign->command;
    uint64_t objects = 1;
    uint64_t estimate = command->undefined_count;
    for (uint32_t i = 0; i < foreign->input_count; i += 1)
    {
        const ForeignInput* input = &foreign->inputs[i];
        objects += input->kind == FOREIGN_KIND_OBJECT ? 1 : 0;
        estimate += input->kind == FOREIGN_KIND_OBJECT || (input->kind == FOREIGN_KIND_ARCHIVE && foreign->archives[input->item].whole) ? input->bytes.length / sizeof(Elf64_Sym) : 0;
    }
    for (uint32_t a = 0; a < foreign->archive_count; a += 1)
    {
        objects += foreign->archives[a].member_count;
    }
    uint32_t bits = 0;
    while (bits < BUSTER_FOREIGN_MAX_PARTITION_BITS && ((uint64_t)BUSTER_FOREIGN_PARTITION_RECORDS << bits) < estimate)
    {
        bits += 1;
    }
    foreign->partition_bits = bits;
    foreign->partition_count = 1u << bits;
    const uint32_t partitions = foreign->partition_count;
    bool result = objects < BUSTER_FOREIGN_MAX_OBJECTS;
    if (!result)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_UNSUPPORTED, 0, 0, 0));
    }
    Arena* arena = &foreign->arena;
    if (result)
    {
        foreign->object_capacity = (uint32_t)objects;
        foreign->objects = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignObject, objects);
        foreign->positions = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, objects);
        // A round loads one member or more, but the last, which loads none.
        foreign->rounds = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignRound, objects + 1);
        foreign->counts = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)foreign->lanes * partitions);
        foreign->tables = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignTable, partitions);
        result = foreign->objects != 0 && foreign->positions != 0 && foreign->rounds != 0 && foreign->counts != 0 && foreign->tables != 0;
        if (!result)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
        }
    }
    if (result)
    {
        memset(foreign->tables, 0, partitions * sizeof(ForeignTable));
        // Object 0: the command line's references, one strong undefined symbol each, which round 0's placement fills.
        foreign->objects[0] = (ForeignObject){
            .position = BUSTER_FOREIGN_COMMAND_POSITION,
            .input = UINT32_MAX,
            .global_count = command->undefined_count,
        };
        foreign->object_count = 1;
        for (uint32_t i = 0; i < foreign->input_count; i += 1)
        {
            const ForeignInput* input = &foreign->inputs[i];
            if (input->kind == FOREIGN_KIND_OBJECT)
            {
                foreign->objects[foreign->object_count] = (ForeignObject){
                    .position = foreign_position(i, 0),
                    .input = i,
                    .bytes = input->bytes,
                };
                foreign->object_count += 1;
            }
            const ForeignArchive* archive = input->kind == FOREIGN_KIND_ARCHIVE ? &foreign->archives[input->item] : 0;
            const uint32_t members = archive != 0 && archive->whole ? archive->member_count : 0;
            for (uint32_t m = 0; m < members; m += 1)
            {
                archive->loaded[m / 64] |= 1ull << (m % 64);
                archive->member_objects[m] = foreign->object_count + 1;
                foreign->objects[foreign->object_count] = (ForeignObject){
                    .position = foreign_position(i, m + 1),
                    .input = i,
                    .member = m + 1,
                    .bytes = archive->members[m].bytes,
                };
                foreign->object_count += 1;
            }
        }
        for (uint32_t id = 0; id < foreign->object_count; id += 1)
        {
            foreign->positions[id] = foreign->objects[id].position;
        }
        foreign->rounds[0] = (ForeignRound){
            .first_object = 0,
            .end_object = foreign->object_count,
        };
        foreign->round_count = 0;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// The parse (step 2)
//
// A round's objects are parsed in two phases, which count, place and fill (rule 3):
//   - Headers, split across the lanes by object: an archive member's header first (import_archive_member; a thin
//     archive's member is then mapped from its file), then the ELF header and section table (import_elf: every
//     section within the file, the names' table), the symbol table (import_symbols) and its first global symbol
//     (sh_info), each relocation section's header (linked to the symbol table, for a section of the object, at most one
//     per section) and how many COMDAT groups there are. The vector path checks eight section headers at a time,
//     transposed (link_transpose_words), and lists the symbol tables, extended indices and relocation sections by
//     compression; anything out of the ordinary sends the object to the exact path, which says what is wrong and
//     where.
//   - Then every lane places the round's objects' global symbols and groups by prefix sums of their counts, lane 0
//     pushes the round's columns, and the symbols are filled split across the lanes by symbol, across objects'
//     boundaries: each one's name, its length and link_hash, value, size, section index, st_info and st_other. The
//     vector path reads eight symbols at a time (import_load_records3), checks them in masks, finds each name's NUL with
//     a masked 64-byte load and hashes eight names at a time (link_hash_rows); a batch with anything out of the
//     ordinary is the exact path's, a symbol at a time. Each object's groups are read by import_object_groups, by the
//     lane that read its headers: an object has few, so both paths read them a group at a time.

// GNU's binding for a symbol unique in the process (STB_GNU_UNIQUE), which g++ gives inline functions' static
// variables, in COMDAT groups.
#define BUSTER_FOREIGN_UNIQUE STB_GNU_UNIQUE

// Maps a thin archive member's file: the archive's directory and the member's name (or the name alone if absolute).
static String8 foreign_map_member(const ForeignInput* input, String8 name, int* error)
{
    const String8 none = { 0 };
    char path[BUSTER_FOREIGN_PATH_BYTES];
    const bool absolute = name.length != 0 && name.pointer[0] == '/';
    String8 result = { 0 };
    *error = ENAMETOOLONG;
    if (foreign_join(path, absolute ? none : foreign_directory(input->path), "", name, ""))
    {
        result = foreign_map(path, error);
        *error = *error == 0 && result.length == 0 ? EINVAL : *error;
    }
    return result;
}

// Archive `archive`'s member `member`: its header read (import_archive_member) into the archive's members, for a
// lazy archive, and its bytes, a thin archive's mapped (*mapped then says so). False, with *failure, if either fails.
static bool foreign_member_bytes(const ForeignArchive* archive, const ForeignInput* input, uint32_t member, String8* bytes, bool* mapped, ForeignFailure* failure)
{
    const uint64_t position = foreign_position(archive->input, member + 1);
    ImportResult read = { 0 };
    bool result = true;
    *mapped = false;
    if (!archive->whole)
    {
        read = import_archive_member(&archive->members[member], input->bytes, &archive->index, member);
        result = read.error == IMPORT_ERROR_NONE;
        *failure = foreign_import_failure(read, position, archive->input, member + 1);
    }
    *bytes = archive->members[member].bytes;
    if (result && archive->thin)
    {
        int error = 0;
        *bytes = foreign_map_member(input, archive->members[member].name, &error);
        *mapped = error == 0;
        result = error == 0;
        *failure = foreign_failure(FOREIGN_ERROR_FILE, position, archive->input, member + 1);
        failure->system_error = error;
    }
    return result;
}

static ImportResult foreign_section_failure(ImportError error, const ImportElf* elf, uint32_t section)
{
    ImportResult result = import_fail(error, elf->section_offset + (uint64_t)section * sizeof(Elf64_Shdr));
    result.section = section;
    return result;
}

// What both paths check after the section table: the symbol table's sh_info (its first global symbol: from 1, the null
// symbol being local, to its count) and each relocation section's header, from the list of them in section order: an
// Elf64_Rela table linked to the symbol table, for a section of the object, at most one per section. An SHT_REL
// section, which no x86-64 compiler writes, isn't linked here (import.c doesn't read one either): the first of it and a
// bad relocation section in section order is reported.
static ImportResult foreign_check_tables(ForeignObject* object, uint32_t rel_section, Arena* scratch)
{
    const ImportElf* elf = &object->elf;
    const ImportSymbols* symbols = &object->symbols;
    ImportResult result = { 0 };
    object->first_global = 0;
    if (symbols->section != 0)
    {
        const Elf64_Shdr header = import_section_header(elf, symbols->section);
        object->first_global = header.sh_info;
        if (header.sh_info == 0 || header.sh_info > symbols->count)
        {
            result = foreign_section_failure(IMPORT_ERROR_BOUNDS, elf, symbols->section);
        }
    }
    uint64_t* targets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, elf->section_count / 64 + 1);
    if (targets == 0)
    {
        result = result.error == IMPORT_ERROR_NONE ? import_fail(IMPORT_ERROR_MEMORY, 0) : result;
    }
    else
    {
        memset(targets, 0, (elf->section_count / 64 + 1) * sizeof(uint64_t));
    }
    for (uint32_t r = 0; result.error == IMPORT_ERROR_NONE && r < object->relocation_count; r += 1)
    {
        const uint32_t index = object->relocations[r];
        const Elf64_Shdr header = import_section_header(elf, index);
        uint64_t count = 0;
        const bool table = import_table(&header, sizeof(Elf64_Rela), &count) && count <= UINT32_MAX;
        const uint32_t target = header.sh_info;
        if (rel_section != 0 && rel_section < index)
        {
            result = foreign_section_failure(IMPORT_ERROR_UNSUPPORTED, elf, rel_section);
            result.type = SHT_REL;
        }
        else if (!table || symbols->section == 0 || header.sh_link != symbols->section || target == SHN_UNDEF || target >= elf->section_count)
        {
            result = foreign_section_failure(IMPORT_ERROR_BOUNDS, elf, index);
        }
        else if ((targets[target / 64] >> (target % 64) & 1) != 0)
        {
            result = foreign_section_failure(IMPORT_ERROR_UNSUPPORTED, elf, index);
            result.type = SHT_RELA;
        }
        else
        {
            targets[target / 64] |= 1ull << (target % 64);
        }
    }
    if (result.error == IMPORT_ERROR_NONE && rel_section != 0)
    {
        result = foreign_section_failure(IMPORT_ERROR_UNSUPPORTED, elf, rel_section);
        result.type = SHT_REL;
    }
    object->global_count = result.error == IMPORT_ERROR_NONE && symbols->section != 0 ? symbols->count - object->first_global : 0;
    return result;
}

// The exact path: import_elf, import_symbols, a scan for the relocation and group sections, and the checks.
static ImportResult foreign_headers_exact(ForeignObject* object, Arena* arena)
{
    object->elf = (ImportElf){ 0 };
    object->symbols = (ImportSymbols){ 0 };
    object->group_count = 0;
    object->relocation_count = 0;
    ImportResult result = import_elf(&object->elf, object->bytes, ET_REL);
    if (result.error == IMPORT_ERROR_NONE)
    {
        result = import_symbols(&object->elf, &object->symbols);
    }
    const uint32_t count = object->elf.section_count;
    object->relocations = result.error == IMPORT_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count) : 0;
    if (result.error == IMPORT_ERROR_NONE && object->relocations == 0)
    {
        result = import_fail(IMPORT_ERROR_MEMORY, 0);
    }
    uint32_t rel_section = 0;
    for (uint32_t index = 1; result.error == IMPORT_ERROR_NONE && index < count; index += 1)
    {
        const uint32_t type = import_section_header(&object->elf, index).sh_type;
        if (type == SHT_RELA)
        {
            object->relocations[object->relocation_count] = index;
            object->relocation_count += 1;
        }
        rel_section = type == SHT_REL && rel_section == 0 ? index : rel_section;
        object->group_count += type == SHT_GROUP ? 1 : 0;
    }
    if (result.error == IMPORT_ERROR_NONE)
    {
        Scratch scratch = scratch_begin(arena);
        result = foreign_check_tables(object, rel_section, scratch.arena);
        scratch_end(scratch);
    }
    return result;
}

// The vector path: the header (import_elf_header) and names' table, then the sections eight at a time: each one's
// contents within the file (but a NOBITS section's), and the symbol tables, extended index tables and relocation
// sections listed by compressing their indices, the groups counted. True if it read the object as the exact path
// would; otherwise the exact path reads it again and reports.
static bool foreign_headers_vector(ForeignObject* object, Arena* arena)
{
    uint32_t names = 0;
    object->elf = (ImportElf){ 0 };
    object->symbols = (ImportSymbols){ 0 };
    object->group_count = 0;
    object->relocation_count = 0;
    bool result = import_elf_header(&object->elf, object->bytes, ET_REL, &names).error == IMPORT_ERROR_NONE && import_elf_names(&object->elf, names);
    const ImportElf* elf = &object->elf;
    const uint32_t count = elf->section_count;
    Scratch scratch = scratch_begin(arena);
    // Each list has room for a batch's stores past its count.
    uint32_t* symbol_tables = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + BUSTER_IMPORT_BATCH) : 0;
    uint32_t* extended = result ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + BUSTER_IMPORT_BATCH) : 0;
    object->relocations = result ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count + BUSTER_IMPORT_BATCH) : 0;
    result = result && symbol_tables != 0 && extended != 0 && object->relocations != 0;
    ImportVectorSections found = {
        .symbol_tables = symbol_tables,
        .extended = extended,
        .relocations = object->relocations,
    };
    const char* table = elf->bytes.pointer + elf->section_offset;
    const __m512i file_bytes = _mm512_set1_epi64((long long)elf->bytes.length);
    const __m512i lane_index = _mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0);
    uint32_t rel_section = 0;
    for (uint32_t first = 0; result && first < count; first += BUSTER_IMPORT_BATCH)
    {
        const uint32_t n = count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH;
        // Section 0 is the null section.
        const uint32_t active = import_lane_mask(n) & (first == 0 ? 0xfe : 0xff);
        __m512i rows[BUSTER_IMPORT_BATCH];
        __m512i words[BUSTER_IMPORT_BATCH];
        for (uint32_t r = 0; r < BUSTER_IMPORT_BATCH; r += 1)
        {
            rows[r] = _mm512_maskz_loadu_epi64(r < n ? 0xff : 0, table + (uint64_t)(first + r) * sizeof(Elf64_Shdr));
        }
        link_transpose_words(rows, words);
        // Elf64_Shdr's words: sh_name and sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link and sh_info,
        // sh_addralign, sh_entsize.
        const __m512i type = _mm512_srli_epi64(words[0], 32);
        const __m512i offset = words[3];
        const __m512i size = words[4];
        const uint32_t nobits = import_lanes_equal(type, SHT_NOBITS);
        const uint32_t within = _mm512_cmple_epu64_mask(size, file_bytes) & _mm512_cmple_epu64_mask(offset, _mm512_sub_epi64(file_bytes, size));
        result = (active & ~nobits & ~within) == 0;
        const __m512i index = _mm512_add_epi64(lane_index, _mm512_set1_epi64(first));
        const uint32_t symtab = active & import_lanes_equal(type, SHT_SYMTAB);
        const uint32_t shndx = active & import_lanes_equal(type, SHT_SYMTAB_SHNDX);
        const uint32_t rela = active & import_lanes_equal(type, SHT_RELA);
        const uint32_t rel = active & import_lanes_equal(type, SHT_REL);
        import_store32(symbol_tables + found.symbol_table_count, _mm512_maskz_compress_epi64((__mmask8)symtab, index), (uint32_t)__builtin_popcount(symtab));
        import_store32(extended + found.extended_count, _mm512_maskz_compress_epi64((__mmask8)shndx, index), (uint32_t)__builtin_popcount(shndx));
        import_store32(object->relocations + found.relocation_count, _mm512_maskz_compress_epi64((__mmask8)rela, index), (uint32_t)__builtin_popcount(rela));
        found.symbol_table_count += (uint32_t)__builtin_popcount(symtab);
        found.extended_count += (uint32_t)__builtin_popcount(shndx);
        found.relocation_count += (uint32_t)__builtin_popcount(rela);
        object->group_count += (uint32_t)__builtin_popcount(active & import_lanes_equal(type, SHT_GROUP));
        rel_section = rel_section == 0 && rel != 0 ? first + (uint32_t)__builtin_ctz(rel) : rel_section;
    }
    // import_symbols' checks, on the lists; then foreign_check_tables', whose failures the exact path reports.
    result = result && import_vector_symbol_table(elf, &found, &object->symbols);
    object->relocation_count = found.relocation_count;
    result = result && foreign_check_tables(object, rel_section, scratch.arena).error == IMPORT_ERROR_NONE;
    scratch_end(scratch);
    return result;
}

// An object's headers, by the path's way; an archive member's header and bytes first.
static void foreign_object_headers(Foreign* foreign, uint32_t id, ForeignLane* lane)
{
    ForeignObject* object = &foreign->objects[id];
    const ForeignInput* input = &foreign->inputs[object->input];
    ForeignFailure failure = { 0 };
    bool readable = true;
    if (object->member != 0)
    {
        readable = foreign_member_bytes(&foreign->archives[input->item], input, object->member - 1, &object->bytes, &object->mapped, &failure);
    }
    if (!readable)
    {
        foreign_fail(lane, failure);
    }
    else if (foreign_kind(object->bytes) != FOREIGN_KIND_OBJECT)
    {
        // A member that isn't a relocatable object: another archive, a shared library, LLVM bitcode.
        foreign_fail(lane, foreign_failure(object->member != 0 ? FOREIGN_ERROR_UNSUPPORTED : FOREIGN_ERROR_FORMAT, object->position, object->input, object->member));
    }
    else
    {
        const uint64_t mark = lane->arena.length;
        const bool vector = foreign->path == FOREIGN_PATH_VECTOR && foreign_headers_vector(object, &lane->arena);
        if (!vector)
        {
            arena_pop_to(&lane->arena, mark);
            const ImportResult read = foreign_headers_exact(object, &lane->arena);
            if (read.error != IMPORT_ERROR_NONE)
            {
                foreign_fail(lane, foreign_import_failure(read, object->position, object->input, object->member));
            }
        }
    }
}

// What a global symbol is to the resolution, from its binding and section index (which the checks allow).
static ForeignClass foreign_class(uint8_t info, uint32_t section)
{
    const uint32_t binding = ELF64_ST_BIND(info);
    ForeignClass result = FOREIGN_CLASS_STRONG;
    if (section == SHN_UNDEF)
    {
        result = binding == STB_WEAK ? FOREIGN_CLASS_WEAK_REFERENCE : FOREIGN_CLASS_REFERENCE;
    }
    else if (section == SHN_COMMON)
    {
        result = FOREIGN_CLASS_COMMON;
    }
    else
    {
        result = binding == STB_GLOBAL ? FOREIGN_CLASS_STRONG : FOREIGN_CLASS_WEAK;
    }
    return result;
}

// Reads global symbol `index` of `object` into the round's columns at `at`, a symbol at a time: false, with *failure,
// if it is malformed or not linked here. The checks, each with the place it reports:
//   - its name within the string table, NUL-terminated, and not empty (IMPORT_ERROR_BOUNDS);
//   - a binding of a global symbol: STB_GLOBAL, STB_WEAK or STB_GNU_UNIQUE; a local one past sh_info is malformed
//     (IMPORT_ERROR_BOUNDS), another is unsupported;
//   - a type of NOTYPE, OBJECT, FUNC, TLS, GNU_IFUNC or COMMON (others, a SECTION or FILE symbol among them, are
//     unsupported);
//   - a section index of the object's, or SHN_UNDEF, SHN_ABS or SHN_COMMON, through SHN_XINDEX when the object has
//     extended indices (IMPORT_ERROR_BOUNDS; another reserved index is unsupported);
//   - a common symbol global, with an alignment (its st_value) that is a power of two (IMPORT_ERROR_BOUNDS); an
//     undefined one not GNU_UNIQUE (unsupported).
static bool foreign_symbol_exact(const ForeignObject* object, uint32_t index, uint32_t at, ForeignSymbols* symbols, ImportResult* failure)
{
    const ImportSymbols* table = &object->symbols;
    const Elf64_Sym symbol = import_symbol(table, index);
    const uint64_t offset = (uint64_t)(table->entries.pointer - object->elf.bytes.pointer) + (uint64_t)index * sizeof(Elf64_Sym);
    const uint32_t type = ELF64_ST_TYPE(symbol.st_info);
    const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
    uint32_t section = SHN_UNDEF;
    String8 name = { 0 };
    const bool named = import_string(table->names, symbol.st_name, &name) && name.length != 0 && name.length <= UINT32_MAX;
    const bool indexed = import_symbol_section(table, index, &symbol, &section);
    const bool reserved = section >= SHN_LORESERVE && symbol.st_shndx != SHN_XINDEX;
    const bool type_known = type == STT_NOTYPE || type == STT_OBJECT || type == STT_FUNC || type == STT_TLS || type == STT_GNU_IFUNC || type == STT_COMMON;
    const bool binding_known = binding == STB_GLOBAL || binding == STB_WEAK || binding == BUSTER_FOREIGN_UNIQUE;
    const bool common = reserved && section == SHN_COMMON;
    const bool alignment = !common || (symbol.st_value != 0 && (symbol.st_value & (symbol.st_value - 1)) == 0);
    bool result = false;
    *failure = (ImportResult){ 0 };
    if (!named || !indexed || binding == STB_LOCAL || (!reserved && section != SHN_UNDEF && section >= object->elf.section_count) || !alignment)
    {
        *failure = import_fail(IMPORT_ERROR_BOUNDS, offset);
    }
    else if (!binding_known || !type_known || (reserved && section != SHN_ABS && section != SHN_COMMON) || (common && binding != STB_GLOBAL) ||
             (section == SHN_UNDEF && binding == BUSTER_FOREIGN_UNIQUE))
    {
        *failure = import_fail(IMPORT_ERROR_UNSUPPORTED, offset);
        failure->type = binding_known ? type : binding;
    }
    else
    {
        symbols->name[at] = (const uint8_t*)name.pointer;
        symbols->length[at] = (uint32_t)name.length;
        symbols->hash[at] = link_hash((const uint8_t*)name.pointer, name.length);
        symbols->value[at] = symbol.st_value;
        symbols->size[at] = symbol.st_size;
        symbols->section[at] = section;
        symbols->info[at] = symbol.st_info;
        symbols->other[at] = symbol.st_other;
        result = true;
    }
    failure->symbol = result ? 0 : index;
    return result;
}

// The vector path over symbols [first, first + count) of `object` (indices in its symbol table), stored in the round's
// columns from `at`: eight at a time, the checks as masks, each name's length from a masked 64-byte load and the names
// hashed eight at a time. Returns how many it stored before the first batch it leaves to the exact path, which the
// caller runs from there.
static uint32_t foreign_symbols_vector(const ForeignObject* object, uint32_t first, uint32_t count, uint32_t at, ForeignSymbols* symbols)
{
    const ImportSymbols* table = &object->symbols;
    const String8 names = table->names;
    const uint32_t sections = object->elf.section_count;
    const __m512i low32 = _mm512_set1_epi64(BUSTER_IMPORT_LOW32);
    // An offset within the names' table starts a name when the table ends with a NUL.
    bool result = names.length != 0 && names.pointer[names.length - 1] == 0;
    uint32_t stored = 0;
    while (result && stored < count)
    {
        const uint32_t index = first + stored;
        const uint32_t n = count - stored < BUSTER_IMPORT_BATCH ? count - stored : BUSTER_IMPORT_BATCH;
        const uint32_t active = import_lane_mask(n);
        __m512i words[3];
        uint32_t indirect = 0;
        import_load_records3(table->entries.pointer + (uint64_t)index * sizeof(Elf64_Sym), n, words);
        // Elf64_Sym's words: st_name, st_info, st_other and st_shndx; st_value; st_size.
        const __m512i section = import_vector_symbol_sections(words[0], table, index, n, &indirect);
        const __m512i name = _mm512_and_si512(words[0], low32);
        const __m512i info = _mm512_and_si512(_mm512_srli_epi64(words[0], 32), _mm512_set1_epi64(0xff));
        const __m512i other = _mm512_and_si512(_mm512_srli_epi64(words[0], 40), _mm512_set1_epi64(0xff));
        const __m512i type = _mm512_and_si512(info, _mm512_set1_epi64(0x0f));
        const __m512i binding = _mm512_srli_epi64(info, 4);
        const __m512i value = words[1];
        const uint32_t reserved = _mm512_cmpge_epu64_mask(section, _mm512_set1_epi64(SHN_LORESERVE)) & ~indirect;
        const uint32_t undefined = _mm512_testn_epi64_mask(section, section);
        const uint32_t common = reserved & import_lanes_equal(section, SHN_COMMON);
        const uint32_t absolute = reserved & import_lanes_equal(section, SHN_ABS);
        const uint32_t global_binding = import_lanes_equal(binding, STB_GLOBAL);
        const uint32_t unique = import_lanes_equal(binding, BUSTER_FOREIGN_UNIQUE);
        const uint32_t binding_known = global_binding | import_lanes_equal(binding, STB_WEAK) | unique;
        const uint32_t type_known = import_lanes_equal(type, STT_NOTYPE) | import_lanes_equal(type, STT_OBJECT) | import_lanes_equal(type, STT_FUNC) |
                                    import_lanes_equal(type, STT_TLS) | import_lanes_equal(type, STT_GNU_IFUNC) | import_lanes_equal(type, STT_COMMON);
        const uint32_t outside = ~reserved & ~undefined & _mm512_cmpge_epu64_mask(section, _mm512_set1_epi64(sections));
        // A power of two: nonzero, and value & (value - 1) zero.
        const uint32_t aligned = _mm512_test_epi64_mask(value, value) & _mm512_testn_epi64_mask(value, _mm512_sub_epi64(value, _mm512_set1_epi64(1)));
        const uint32_t named = _mm512_test_epi64_mask(name, name) & _mm512_cmplt_epu64_mask(name, _mm512_set1_epi64((long long)names.length));
        const uint32_t bad = (indirect & (table->extended.length == 0 ? 0xff : 0)) | ~named | ~binding_known | ~type_known | outside | (reserved & ~common & ~absolute) |
                             (common & ~aligned) | (common & ~global_binding) | (undefined & unique);
        // The names' lengths: a name past 63 bytes takes memchr, and an empty one is the exact path's to report.
        uint32_t offsets[BUSTER_IMPORT_BATCH];
        uint32_t lengths[BUSTER_IMPORT_BATCH];
        import_store32(offsets, name, n);
        uint32_t empty = 0;
        for (uint32_t lane = 0; (bad & active) == 0 && lane < n; lane += 1)
        {
            const uint64_t length = import_name_length(names, offsets[lane]);
            lengths[lane] = (uint32_t)(length <= UINT32_MAX ? length : 0);
            empty |= (lengths[lane] == 0 ? 1u : 0u) << lane;
        }
        result = ((bad | empty) & active) == 0;
        if (result)
        {
            __m512i rows[BUSTER_IMPORT_BATCH];
            uint64_t hashes[BUSTER_IMPORT_BATCH];
            const uint32_t long_names = import_name_rows(names.pointer, offsets, lengths, n, rows);
            const __m512i length = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32((__mmask8)active, lengths));
            _mm512_storeu_si512(hashes, link_hash_rows(rows, length));
            for (uint32_t lane = 0; long_names != 0 && lane < n; lane += 1)
            {
                if ((long_names >> lane & 1) != 0)
                {
                    hashes[lane] = link_hash((const uint8_t*)names.pointer + offsets[lane], lengths[lane]);
                }
            }
            const __m512i pointers = _mm512_add_epi64(_mm512_set1_epi64((long long)(uintptr_t)names.pointer), name);
            const uint32_t place = at + stored;
            import_store64((void*)(symbols->name + place), pointers, n);
            memcpy(symbols->length + place, lengths, n * sizeof(uint32_t));
            memcpy(symbols->hash + place, hashes, n * sizeof(uint64_t));
            import_store64(symbols->value + place, value, n);
            import_store64(symbols->size + place, words[2], n);
            import_store32(symbols->section + place, section, n);
            import_store8(symbols->info + place, info, n);
            import_store8(symbols->other + place, other, n);
            stored += n;
        }
    }
    return stored;
}

// The object of round `round` that holds global symbol `symbol`: the last whose global_begin is at most it (an object
// without globals begins where the next does, so the last such holds it).
static uint32_t foreign_round_object(const Foreign* foreign, const ForeignRound* round, uint64_t symbol)
{
    uint32_t low = round->first_object;
    uint32_t high = round->end_object;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (foreign->objects[middle].global_begin <= symbol)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

// A lane's part of a round's global symbols, [begin, end) by place in the round, across objects' boundaries: each
// object's stretch by the vector path, then what it leaves by the exact path, which reports a failure.
static void foreign_fill_symbols(Foreign* foreign, ForeignRound* round, uint64_t begin, uint64_t end, ForeignLane* lane)
{
    uint64_t place = begin;
    uint32_t id = place < end ? foreign_round_object(foreign, round, round->first_symbol + place) : round->end_object;
    bool failed = false;
    for (; !failed && place < end && id < round->end_object; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        const uint64_t object_end = (uint64_t)object->global_begin + object->global_count - round->first_symbol;
        const uint64_t stretch_end = object_end < end ? object_end : end;
        if (id != 0 && place < stretch_end)
        {
            const uint32_t index = object->first_global + (uint32_t)(round->first_symbol + place - object->global_begin);
            const uint32_t count = (uint32_t)(stretch_end - place);
            const uint32_t done = foreign->path == FOREIGN_PATH_VECTOR ? foreign_symbols_vector(object, index, count, (uint32_t)place, &round->symbols) : 0;
            for (uint32_t k = done; !failed && k < count; k += 1)
            {
                ImportResult read;
                failed = !foreign_symbol_exact(object, index + k, (uint32_t)place + k, &round->symbols, &read);
                if (failed)
                {
                    foreign_fail(lane, foreign_import_failure(read, object->position, object->input, object->member));
                }
            }
        }
        place = stretch_end > place ? stretch_end : place;
    }
}

// An object's COMDAT groups (import_object_groups), into the round's group columns from its place: each one's signature,
// with its link_hash, and its object.
static void foreign_object_groups(Foreign* foreign, ForeignRound* round, uint32_t id, ForeignLane* lane)
{
    ForeignObject* object = &foreign->objects[id];
    ImportObject imported = { 0 };
    const ImportResult read = import_object_groups(&object->elf, &object->symbols, &imported, &lane->arena);
    if (read.error != IMPORT_ERROR_NONE || imported.group_count != object->group_count)
    {
        foreign_fail(lane, foreign_import_failure(read, object->position, object->input, object->member));
    }
    else
    {
        object->group_list = imported.groups;
        object->group_sections = imported.group_sections;
        ForeignGroups* groups = &round->groups;
        for (uint32_t g = 0; g < object->group_count; g += 1)
        {
            const uint32_t at = object->group_begin - round->first_group + g;
            const String8 signature = imported.groups[g].signature;
            groups->signature[at] = (const uint8_t*)signature.pointer;
            groups->length[at] = (uint32_t)signature.length;
            groups->hash[at] = link_hash((const uint8_t*)signature.pointer, signature.length);
            groups->object[at] = id;
            groups->kept[at] = 0;
        }
    }
}

// Lane 0: a round's columns and records, for its counted symbols and groups, and object 0's symbols in round 0.
static bool foreign_round_columns(Foreign* foreign, ForeignRound* round)
{
    Arena* arena = &foreign->arena;
    const uint64_t symbols = round->symbol_count;
    const uint64_t groups = round->group_count;
    ForeignSymbols* columns = &round->symbols;
    columns->name = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, symbols + 1);
    columns->length = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols + 1);
    columns->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, symbols + 1);
    columns->value = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, symbols + 1);
    columns->size = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, symbols + 1);
    columns->section = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols + 1);
    columns->info = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, symbols + 1);
    columns->other = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, symbols + 1);
    columns->entry = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols + 1);
    columns->name_id = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, symbols + 1);
    ForeignGroups* group_columns = &round->groups;
    group_columns->signature = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, groups + 1);
    group_columns->length = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, groups + 1);
    group_columns->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, groups + 1);
    group_columns->object = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, groups + 1);
    group_columns->entry = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, groups + 1);
    group_columns->kept = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, groups + 1);
    round->records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignRecord, symbols + groups + 1);
    round->partition_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, (uint64_t)foreign->partition_count + 1);
    const bool result = columns->name != 0 && columns->length != 0 && columns->hash != 0 && columns->value != 0 && columns->size != 0 && columns->section != 0 &&
                        columns->info != 0 && columns->other != 0 && columns->entry != 0 && columns->name_id != 0 && group_columns->signature != 0 &&
                        group_columns->length != 0 && group_columns->hash != 0 && group_columns->object != 0 && group_columns->entry != 0 &&
                        group_columns->kept != 0 && round->records != 0 && round->partition_begin != 0;
    const ForeignCommand* command = foreign->command;
    for (uint32_t u = 0; result && round->first_object == 0 && u < command->undefined_count; u += 1)
    {
        const String8 name = command->undefined[u];
        columns->name[u] = (const uint8_t*)name.pointer;
        columns->length[u] = (uint32_t)name.length;
        columns->hash[u] = link_hash((const uint8_t*)name.pointer, name.length);
        columns->value[u] = 0;
        columns->size[u] = 0;
        columns->section[u] = SHN_UNDEF;
        columns->info[u] = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        columns->other[u] = STV_DEFAULT;
    }
    return result;
}

// Step 2 for round `round`'s objects: their headers, then (after a sync) their places, the round's columns (lane 0),
// and after another their symbols and groups. Every lane places every object of the round and writes its own objects'
// places: the symbols after the rounds' before, and the groups likewise.
static void foreign_parse(Foreign* foreign, uint32_t index, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    ForeignRound* round = &foreign->rounds[index];
    const uint32_t first_object = round->first_object;
    const uint32_t end_object = round->end_object;
    const Range64 objects = lane_range(end_object - first_object);
    for (uint64_t i = objects.begin; i < objects.end; i += 1)
    {
        const uint32_t id = first_object + (uint32_t)i;
        if (id != 0)
        {
            foreign_object_headers(foreign, id, lane);
        }
    }
    start = foreign_time(lane, FOREIGN_PHASE_HEADERS, start);
    lane_sync();
    const bool failed = foreign_failed(foreign);
    const ForeignRound* previous = index != 0 ? &foreign->rounds[index - 1] : 0;
    const uint64_t first_symbol = previous != 0 ? (uint64_t)previous->first_symbol + previous->symbol_count : 0;
    const uint64_t first_group = previous != 0 ? (uint64_t)previous->first_group + previous->group_count : 0;
    uint64_t global = first_symbol;
    uint64_t group = first_group;
    for (uint32_t id = first_object; !failed && id < end_object; id += 1)
    {
        ForeignObject* object = &foreign->objects[id];
        const bool mine = id - first_object >= objects.begin && id - first_object < objects.end;
        if (mine)
        {
            object->global_begin = (uint32_t)global;
            object->group_begin = (uint32_t)group;
        }
        global += object->global_count;
        group += object->group_count;
    }
    if (!failed && (global >= UINT32_MAX || group >= UINT32_MAX))
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_UNSUPPORTED, 0, 0, 0));
    }
    else if (!failed && lane_index() == 0)
    {
        round->first_symbol = (uint32_t)first_symbol;
        round->symbol_count = (uint32_t)(global - first_symbol);
        round->first_group = (uint32_t)first_group;
        round->group_count = (uint32_t)(group - first_group);
        if (!foreign_round_columns(foreign, round))
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
        }
    }
    lane_sync();
    if (!foreign_failed(foreign))
    {
        // Object 0's symbols, the command line's, are lane 0's.
        const uint64_t begin = index == 0 ? foreign->objects[0].global_count : 0;
        const Range64 symbols = lane_range(round->symbol_count - begin);
        foreign_fill_symbols(foreign, round, begin + symbols.begin, begin + symbols.end, lane);
        for (uint64_t i = objects.begin; i < objects.end; i += 1)
        {
            const uint32_t id = first_object + (uint32_t)i;
            if (id != 0 && foreign->objects[id].group_count != 0)
            {
                foreign_object_groups(foreign, round, id, lane);
            }
        }
    }
    start = foreign_time(lane, FOREIGN_PHASE_SYMBOLS, start);
    (void)start;
    lane_sync();
}

// ---------------------------------------------------------------------------------------------------------------------
// The exchange (rule 4)
//
// A round's records are its global symbols' and its groups', one each, in item order: the symbols, then the groups,
// each by its place in the round. Every lane takes a range of them (lane_range, so that a partition's records come from
// the lanes in input order), counts them per partition, and after a sync places its segment of each partition, after
// the earlier partitions' and the earlier lanes' segments of the same partition, and fills it. The partition is the
// hash's top bits; a group's hash is its signature's mixed with BUSTER_FOREIGN_GROUP_SALT, so that a signature and a
// symbol of one name are different keys. The vector path makes eight symbols' records at a time (their partitions,
// classes and tags in vectors, each record's two words put together by permutes), then stores each where its
// partition's segment is; the exact path makes one at a time.

#define BUSTER_FOREIGN_GROUP_SALT 0x5bd1e9955bd1e995ull

static uint32_t foreign_partition(const Foreign* foreign, uint64_t hash)
{
    return foreign->partition_bits == 0 ? 0 : (uint32_t)(hash >> (64 - foreign->partition_bits));
}

static uint32_t foreign_tag(ForeignClass class, uint8_t other, uint32_t object)
{
    return (uint32_t)class | (uint32_t)(other & 0x03) << BUSTER_FOREIGN_TAG_VISIBILITY_SHIFT | object << BUSTER_FOREIGN_TAG_OBJECT_SHIFT;
}

// The record of the round's symbol at place `at`, of `object`: the exact path's.
static ForeignRecord foreign_symbol_record(const ForeignRound* round, uint32_t at, uint32_t object)
{
    const ForeignSymbols* symbols = &round->symbols;
    const ForeignRecord result = {
        .hash = symbols->hash[at],
        .item = at,
        .tag = foreign_tag(foreign_class(symbols->info[at], symbols->section[at]), symbols->other[at], object),
    };
    return result;
}

static ForeignRecord foreign_group_record(const ForeignRound* round, uint32_t at)
{
    const ForeignRecord result = {
        .hash = round->groups.hash[at] ^ BUSTER_FOREIGN_GROUP_SALT,
        .item = at,
        .tag = foreign_tag(FOREIGN_CLASS_GROUP, 0, round->groups.object[at]),
    };
    return result;
}

// Up to eight records of `object`'s symbols from place `at`, `count` of them, in order into `records`, and their
// partitions: foreign_class's classes as masks, the tags in vectors, each record's two words put together by permutes.
static void foreign_records_vector(const Foreign* foreign, const ForeignRound* round, uint32_t at, uint32_t count, uint32_t object, ForeignRecord* records, uint32_t* partitions)
{
    const ForeignSymbols* symbols = &round->symbols;
    const __mmask8 loaded = (__mmask8)import_lane_mask(count);
    const __m512i hash = _mm512_maskz_loadu_epi64(loaded, symbols->hash + at);
    const __m512i section = _mm512_cvtepu32_epi64(_mm256_maskz_loadu_epi32(loaded, symbols->section + at));
    const __m512i info = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->info + at));
    const __m512i other = _mm512_cvtepu8_epi64(_mm_maskz_loadu_epi8(loaded, symbols->other + at));
    const __m512i binding = _mm512_srli_epi64(info, 4);
    const uint32_t undefined = _mm512_testn_epi64_mask(section, section);
    const uint32_t common = import_lanes_equal(section, SHN_COMMON);
    const uint32_t weak = import_lanes_equal(binding, STB_WEAK);
    const uint32_t global = import_lanes_equal(binding, STB_GLOBAL);
    // FOREIGN_CLASS_STRONG is 0: a definition with a global binding.
    __m512i class = _mm512_maskz_mov_epi64((__mmask8)(~undefined & ~common & ~global), _mm512_set1_epi64(FOREIGN_CLASS_WEAK));
    class = _mm512_mask_mov_epi64(class, (__mmask8)common, _mm512_set1_epi64(FOREIGN_CLASS_COMMON));
    class = _mm512_mask_mov_epi64(class, (__mmask8)(undefined & ~weak), _mm512_set1_epi64(FOREIGN_CLASS_REFERENCE));
    class = _mm512_mask_mov_epi64(class, (__mmask8)(undefined & weak), _mm512_set1_epi64(FOREIGN_CLASS_WEAK_REFERENCE));
    const __m512i visibility = _mm512_slli_epi64(_mm512_and_si512(other, _mm512_set1_epi64(0x03)), BUSTER_FOREIGN_TAG_VISIBILITY_SHIFT);
    const __m512i tag = _mm512_or_si512(_mm512_or_si512(class, visibility), _mm512_set1_epi64((long long)object << BUSTER_FOREIGN_TAG_OBJECT_SHIFT));
    const __m512i item = _mm512_add_epi64(_mm512_set_epi64(7, 6, 5, 4, 3, 2, 1, 0), _mm512_set1_epi64(at));
    // Each record's second word: the item, then the tag above it.
    const __m512i second = _mm512_or_si512(item, _mm512_slli_epi64(tag, 32));
    _mm512_storeu_si512(records, _mm512_permutex2var_epi64(hash, _mm512_set_epi64(11, 3, 10, 2, 9, 1, 8, 0), second));
    _mm512_storeu_si512(records + 4, _mm512_permutex2var_epi64(hash, _mm512_set_epi64(15, 7, 14, 6, 13, 5, 12, 4), second));
    const __m512i partition = foreign->partition_bits == 0 ? _mm512_setzero_si512() : _mm512_srli_epi64(hash, 64 - foreign->partition_bits);
    import_store32(partitions, partition, count);
}

// One record into the exchange: with `next`, stored at its partition's next place, which moves on; without, counted.
static void foreign_place_record(ForeignRecord* records, uint64_t* next, uint32_t* counts, ForeignRecord record, uint32_t partition)
{
    if (next != 0)
    {
        records[next[partition]] = record;
        next[partition] += 1;
    }
    else
    {
        counts[partition] += 1;
    }
}

// The round's items [begin, end), its symbols then its groups, into the exchange: counted, or stored. The symbols go
// object by object, eight at a time on the vector path.
static void foreign_exchange_items(const Foreign* foreign, ForeignRound* round, uint64_t begin, uint64_t end, uint64_t* next, uint32_t* counts)
{
    const uint64_t symbols = round->symbol_count;
    const uint64_t symbol_end = end < symbols ? end : symbols;
    uint64_t item = begin;
    uint32_t object = item < symbol_end ? foreign_round_object(foreign, round, round->first_symbol + item) : round->end_object;
    while (item < symbol_end)
    {
        const ForeignObject* holder = &foreign->objects[object];
        const uint64_t object_end = (uint64_t)holder->global_begin + holder->global_count - round->first_symbol;
        const uint64_t stretch = object_end < symbol_end ? object_end : symbol_end;
        while (foreign->path == FOREIGN_PATH_VECTOR && item < stretch)
        {
            const uint32_t n = stretch - item < BUSTER_IMPORT_BATCH ? (uint32_t)(stretch - item) : BUSTER_IMPORT_BATCH;
            ForeignRecord batch[BUSTER_IMPORT_BATCH];
            uint32_t partitions[BUSTER_IMPORT_BATCH];
            foreign_records_vector(foreign, round, (uint32_t)item, n, object, batch, partitions);
            for (uint32_t k = 0; k < n; k += 1)
            {
                foreign_place_record(round->records, next, counts, batch[k], partitions[k]);
            }
            item += n;
        }
        for (; item < stretch; item += 1)
        {
            const ForeignRecord record = foreign_symbol_record(round, (uint32_t)item, object);
            foreign_place_record(round->records, next, counts, record, foreign_partition(foreign, record.hash));
        }
        object += 1;
    }
    for (item = item > symbols ? item : symbols; item < end; item += 1)
    {
        const ForeignRecord record = foreign_group_record(round, (uint32_t)(item - symbols));
        foreign_place_record(round->records, next, counts, record, foreign_partition(foreign, record.hash));
    }
}

// The round's records into their partitions: count, place, fill.
static void foreign_exchange(Foreign* foreign, uint32_t index, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    ForeignRound* round = &foreign->rounds[index];
    const uint32_t partitions = foreign->partition_count;
    const uint32_t lanes = lane_count();
    const uint32_t me = lane_index();
    const Range64 mine = lane_range((uint64_t)round->symbol_count + round->group_count);
    uint32_t* counts = foreign->counts + (uint64_t)me * partitions;
    memset(counts, 0, partitions * sizeof(uint32_t));
    foreign_exchange_items(foreign, round, mine.begin, mine.end, 0, counts);
    lane_sync();
    Scratch scratch = scratch_begin(&lane->arena);
    uint64_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, partitions + 1);
    const Range64 written = lane_range(partitions + 1);
    uint64_t offset = 0;
    for (uint32_t p = 0; next != 0 && p < partitions; p += 1)
    {
        if (p >= written.begin && p < written.end)
        {
            round->partition_begin[p] = offset;
        }
        for (uint32_t l = 0; l < lanes; l += 1)
        {
            next[p] = l == me ? offset : next[p];
            offset += foreign->counts[(uint64_t)l * partitions + p];
        }
    }
    if (next == 0)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
    }
    else
    {
        if (written.end == partitions + 1)
        {
            round->partition_begin[partitions] = offset;
        }
        foreign_exchange_items(foreign, round, mine.begin, mine.end, next, 0);
    }
    scratch_end(scratch);
    start = foreign_time(lane, FOREIGN_PHASE_EXCHANGE, start);
    (void)start;
    lane_sync();
}

// ---------------------------------------------------------------------------------------------------------------------
// Resolution (step 3) and archive members (step 4)
//
// Each lane owns a range of the partitions (lane_range: the same in every round, so that a partition's tables stay with
// its lane) and takes each round's records of them, which come from every lane in input order. A name's entry keeps
// what loading needs (ForeignEntry): its earliest strong reference, definition and common symbol by position, the most
// constraining visibility, and once a round touched it without a definition, the archive member that defines it first
// (by position: the first archive in link order whose index has it, and its lowest member among the entries there) and
// the first shared library that exports it. A group signature's entry keeps its earliest group by position. Both paths
// fill the same tables, adding names in the order of their first records; each record's entry goes to its symbol's (or
// group's) `entry` column.
//
// Then each entry the round touched decides whether its first member must be loaded: as lld and mold decide where GNU
// ld's result depends on the archives' order, and so as GNU ld does where it doesn't (an archive after the inputs that
// need it). The member loads if a strong reference refers to the name and both the member and the earliest strong
// reference come before the name's earliest definition, which a shared library's export is too when every symbol of
// the name has default visibility (GNU ld and lld bind no other to a shared library). A weak reference loads nothing.
// As GNU ld does, a common symbol with no strong definition loads the first member after it, in index order, that
// defines the name as global data (STB_GLOBAL or GNU_UNIQUE, not a function, not common); lld loads nothing for a
// common symbol, and mold any member that defines it. Each lane lists the members it needs; lane 0 marks them in their
// archives' bitsets after a sync, and the next round loads those not loaded before, in position order. Loading only
// ever adds: a member a round loaded stays loaded whatever a later round finds.

#define BUSTER_FOREIGN_ENTRY_PROBED 0x01
#define BUSTER_FOREIGN_ENTRY_STRONG_DEFINED 0x02
#define BUSTER_FOREIGN_ENTRY_WEAK_DEFINED 0x04
#define BUSTER_FOREIGN_ENTRY_COMMON 0x08
#define BUSTER_FOREIGN_ENTRY_REFERENCED 0x10
#define BUSTER_FOREIGN_ENTRY_WEAK_REFERENCED 0x20
#define BUSTER_FOREIGN_ENTRY_DATA_PROBED 0x40
// A table starts with room for this many entries, then doubles; its slots are twice its entries.
#define BUSTER_FOREIGN_TABLE_ENTRIES 16

// A visibility's rank by constraint: default, protected, hidden, internal; and back.
static const uint8_t foreign_visibility_ranks[4] = {
    [STV_DEFAULT] = 0,
    [STV_INTERNAL] = 3,
    [STV_HIDDEN] = 2,
    [STV_PROTECTED] = 1,
};

static const uint8_t foreign_rank_visibilities[4] = {
    STV_DEFAULT,
    STV_PROTECTED,
    STV_HIDDEN,
    STV_INTERNAL,
};

static uint64_t foreign_min(uint64_t a, uint64_t b)
{
    return a < b ? a : b;
}

static uint64_t foreign_max(uint64_t a, uint64_t b)
{
    return a > b ? a : b;
}

// Doubles a table's entries and slots, reinserting every entry: false if the arena can't hold them.
static bool foreign_grow(ForeignTable* table, Arena* arena)
{
    const uint32_t capacity = table->capacity == 0 ? BUSTER_FOREIGN_TABLE_ENTRIES : table->capacity * 2;
    const uint32_t slot_count = capacity * 2;
    ForeignEntry* entries = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignEntry, capacity);
    uint32_t* slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slot_count);
    const bool result = entries != 0 && slots != 0 && capacity < UINT32_MAX / 2;
    if (result)
    {
        memcpy(entries, table->entries, (uint64_t)table->count * sizeof(ForeignEntry));
        memset(slots, 0, (uint64_t)slot_count * sizeof(uint32_t));
        for (uint32_t e = 0; e < table->count; e += 1)
        {
            uint32_t slot = (uint32_t)entries[e].hash & (slot_count - 1);
            while (slots[slot] != 0)
            {
                slot = (slot + 1) & (slot_count - 1);
            }
            slots[slot] = e + 1;
        }
        table->entries = entries;
        table->slots = slots;
        table->slot_mask = slot_count - 1;
        table->capacity = capacity;
    }
    return result;
}

static bool foreign_grow_groups(ForeignTable* table, Arena* arena)
{
    const uint32_t capacity = table->group_capacity == 0 ? BUSTER_FOREIGN_TABLE_ENTRIES : table->group_capacity * 2;
    const uint32_t slot_count = capacity * 2;
    ForeignGroupEntry* groups = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignGroupEntry, capacity);
    uint32_t* slots = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slot_count);
    const bool result = groups != 0 && slots != 0 && capacity < UINT32_MAX / 2;
    if (result)
    {
        memcpy(groups, table->groups, (uint64_t)table->group_count * sizeof(ForeignGroupEntry));
        memset(slots, 0, (uint64_t)slot_count * sizeof(uint32_t));
        for (uint32_t e = 0; e < table->group_count; e += 1)
        {
            uint32_t slot = (uint32_t)groups[e].hash & (slot_count - 1);
            while (slots[slot] != 0)
            {
                slot = (slot + 1) & (slot_count - 1);
            }
            slots[slot] = e + 1;
        }
        table->groups = groups;
        table->group_slots = slots;
        table->group_slot_mask = slot_count - 1;
        table->group_capacity = capacity;
    }
    return result;
}

// The entry of a name in a partition's table, added if it has none: its index, or UINT32_MAX if the table can't grow.
// A hash match is checked against the bytes.
static uint32_t foreign_entry(ForeignTable* table, uint64_t hash, const uint8_t* name, uint32_t length, Arena* arena)
{
    uint32_t result = UINT32_MAX;
    if (table->count < table->capacity || foreign_grow(table, arena))
    {
        uint32_t slot = (uint32_t)hash & table->slot_mask;
        bool searching = true;
        while (searching && table->slots[slot] != 0)
        {
            const uint32_t e = table->slots[slot] - 1;
            const ForeignEntry* entry = &table->entries[e];
            const bool same = entry->hash == hash && entry->length == length && import_same_bytes(entry->name, name, length);
            result = same ? e : result;
            searching = !same;
            slot = same ? slot : (slot + 1) & table->slot_mask;
        }
        if (searching)
        {
            result = table->count;
            table->entries[result] = (ForeignEntry){
                .hash = hash,
                .name = name,
                .length = length,
                .reference = BUSTER_FOREIGN_NO_POSITION,
                .defined = BUSTER_FOREIGN_NO_POSITION,
                .common = BUSTER_FOREIGN_NO_POSITION,
            };
            table->slots[slot] = result + 1;
            table->count += 1;
        }
    }
    return result;
}

// The same for a group signature in the partition's group table.
static uint32_t foreign_group_entry(ForeignTable* table, uint64_t hash, const uint8_t* signature, uint32_t length, Arena* arena)
{
    uint32_t result = UINT32_MAX;
    if (table->group_count < table->group_capacity || foreign_grow_groups(table, arena))
    {
        uint32_t slot = (uint32_t)hash & table->group_slot_mask;
        bool searching = true;
        while (searching && table->group_slots[slot] != 0)
        {
            const uint32_t e = table->group_slots[slot] - 1;
            const ForeignGroupEntry* entry = &table->groups[e];
            const bool same = entry->hash == hash && entry->length == length && import_same_bytes(entry->signature, signature, length);
            result = same ? e : result;
            searching = !same;
            slot = same ? slot : (slot + 1) & table->group_slot_mask;
        }
        if (searching)
        {
            result = table->group_count;
            table->groups[result] = (ForeignGroupEntry){
                .hash = hash,
                .signature = signature,
                .length = length,
                .group = UINT32_MAX,
                .position = BUSTER_FOREIGN_NO_POSITION,
            };
            table->group_slots[slot] = result + 1;
            table->group_count += 1;
        }
    }
    return result;
}

// Adds a record to its entry: the positions, flags and visibility, and the round's list of touched entries.
static void foreign_touch(ForeignTable* table, uint32_t e, uint32_t tag, uint64_t position, uint32_t round)
{
    ForeignEntry* entry = &table->entries[e];
    const ForeignClass class = (ForeignClass)(tag & BUSTER_FOREIGN_TAG_CLASS);
    const uint8_t rank = foreign_visibility_ranks[tag >> BUSTER_FOREIGN_TAG_VISIBILITY_SHIFT & 0x03];
    const bool definition = class == FOREIGN_CLASS_STRONG || class == FOREIGN_CLASS_COMMON || class == FOREIGN_CLASS_WEAK;
    static const uint8_t flags[FOREIGN_CLASSES] = {
        [FOREIGN_CLASS_STRONG] = BUSTER_FOREIGN_ENTRY_STRONG_DEFINED,
        [FOREIGN_CLASS_COMMON] = BUSTER_FOREIGN_ENTRY_COMMON,
        [FOREIGN_CLASS_WEAK] = BUSTER_FOREIGN_ENTRY_WEAK_DEFINED,
        [FOREIGN_CLASS_REFERENCE] = BUSTER_FOREIGN_ENTRY_REFERENCED,
        [FOREIGN_CLASS_WEAK_REFERENCE] = BUSTER_FOREIGN_ENTRY_WEAK_REFERENCED,
        [FOREIGN_CLASS_GROUP] = 0,
    };
    entry->visibility = rank > entry->visibility ? rank : entry->visibility;
    entry->flags |= flags[class];
    entry->defined = definition ? foreign_min(entry->defined, position) : entry->defined;
    entry->common = class == FOREIGN_CLASS_COMMON ? foreign_min(entry->common, position) : entry->common;
    entry->reference = class == FOREIGN_CLASS_REFERENCE ? foreign_min(entry->reference, position) : entry->reference;
    if (entry->round != round + 1)
    {
        entry->round = (uint16_t)(round + 1);
        table->touched[table->touched_count] = e;
        table->touched_count += 1;
    }
}

// A group record: the signature's earliest group by position, then by id.
static void foreign_touch_group(ForeignTable* table, uint32_t e, uint32_t group, uint64_t position)
{
    ForeignGroupEntry* entry = &table->groups[e];
    const bool earlier = position < entry->position || (position == entry->position && group < entry->group);
    entry->position = earlier ? position : entry->position;
    entry->group = earlier ? group : entry->group;
}

// Room for this round's touched entries: one per record at most.
static bool foreign_touched_room(ForeignTable* table, uint64_t records, Arena* arena)
{
    bool result = true;
    if (records > table->touched_capacity)
    {
        const uint64_t capacity = records > 2ull * table->touched_capacity ? records : 2ull * table->touched_capacity;
        table->touched = capacity <= UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, capacity) : 0;
        table->touched_capacity = table->touched != 0 ? (uint32_t)capacity : 0;
        result = table->touched != 0;
    }
    table->touched_count = 0;
    return result;
}

// The bytes a record's name or signature is.
static void foreign_record_name(const ForeignRound* round, const ForeignRecord* record, const uint8_t** name, uint32_t* length)
{
    const bool group = (record->tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP;
    *name = group ? round->groups.signature[record->item] : round->symbols.name[record->item];
    *length = group ? round->groups.length[record->item] : round->symbols.length[record->item];
}

// A record folded into its entry, which its symbol's or group's column gets.
static void foreign_fold(Foreign* foreign, ForeignRound* round, ForeignTable* table, uint32_t e, const ForeignRecord* record, uint32_t index)
{
    const uint64_t position = foreign->positions[record->tag >> BUSTER_FOREIGN_TAG_OBJECT_SHIFT];
    if ((record->tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP)
    {
        foreign_touch_group(table, e, round->first_group + record->item, position);
        round->groups.entry[record->item] = e;
    }
    else
    {
        foreign_touch(table, e, record->tag, position, index);
        round->symbols.entry[record->item] = e;
    }
}

// The exact path: each record of the partition's round in turn, its entry found or added and updated.
static bool foreign_resolve_exact(Foreign* foreign, ForeignRound* round, uint32_t index, ForeignTable* table, const ForeignRecord* records, uint64_t count, Arena* arena)
{
    bool result = true;
    for (uint64_t i = 0; result && i < count; i += 1)
    {
        const ForeignRecord* record = &records[i];
        const uint8_t* name = 0;
        uint32_t length = 0;
        foreign_record_name(round, record, &name, &length);
        const bool group = (record->tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP;
        const uint32_t e = group ? foreign_group_entry(table, record->hash, name, length, arena) : foreign_entry(table, record->hash, name, length, arena);
        result = e != UINT32_MAX;
        if (result)
        {
            foreign_fold(foreign, round, table, e, record, index);
        }
    }
    return result;
}

// The vector path's sort: below this many records, a stable insertion sort by the whole hash; from it, a stable LSD
// radix sort by the hash's low 16 bits in two passes of 8, then an insertion sort by the whole hash within each run of
// equal low bits, which holds about one name.
#define BUSTER_FOREIGN_SORT_RADIX 64
#define BUSTER_FOREIGN_SORT_BUCKETS 256

// Sorts `count` record places in `order` (0 to count - 1 on entry) stably by their records' hashes, using `spare`.
static void foreign_sort_records(const ForeignRecord* records, uint32_t* order, uint32_t* spare, uint32_t count)
{
    if (count >= BUSTER_FOREIGN_SORT_RADIX)
    {
        uint32_t buckets[BUSTER_FOREIGN_SORT_BUCKETS];
        uint32_t* from = order;
        uint32_t* to = spare;
        for (uint32_t shift = 0; shift < 16; shift += 8)
        {
            memset(buckets, 0, sizeof(buckets));
            for (uint32_t i = 0; i < count; i += 1)
            {
                buckets[(records[from[i]].hash >> shift) & 0xff] += 1;
            }
            import_exclusive_scan(buckets, BUSTER_FOREIGN_SORT_BUCKETS);
            for (uint32_t i = 0; i < count; i += 1)
            {
                const uint32_t bucket = (uint32_t)(records[from[i]].hash >> shift) & 0xff;
                to[buckets[bucket]] = from[i];
                buckets[bucket] += 1;
            }
            uint32_t* swap = from;
            from = to;
            to = swap;
        }
        // Two passes: the sorted places are back in `order`.
    }
    // Insertion sort by the whole hash: of everything below the radix sort's threshold, and within each run of equal
    // low 16 bits after it. A place moves left only past a strictly larger hash, which keeps the sort stable.
    uint32_t run = 0;
    while (run < count)
    {
        uint32_t end = run + 1;
        const uint64_t low = records[order[run]].hash & 0xffff;
        while (end < count && (count < BUSTER_FOREIGN_SORT_RADIX || (records[order[end]].hash & 0xffff) == low))
        {
            end += 1;
        }
        for (uint32_t i = run + 1; i < end; i += 1)
        {
            const uint32_t place = order[i];
            const uint64_t hash = records[place].hash;
            uint32_t j = i;
            while (j > run && records[order[j - 1]].hash > hash)
            {
                order[j] = order[j - 1];
                j -= 1;
            }
            order[j] = place;
        }
        run = end;
    }
}

// The vector path: the partition's round of records grouped by name before the tables see them, so that each name is
// probed once, and in the order of its slots (the sort's low bits are the slots'). The records' places are sorted by
// hash (foreign_sort_records), and each run of equal hashes, a name's records, is checked against its first's bytes
// and kind (a group's signature or a symbol's name): a 64-bit collision between different names leaves the round to
// the exact path, before any change to the tables. Then the names are taken in the order of their first records, the
// order the exact path adds them in, each probed once, and its records folded in as the exact path folds them.
static bool foreign_resolve_vector(Foreign* foreign, ForeignRound* round, uint32_t index, ForeignTable* table, const ForeignRecord* records, uint64_t count, Arena* arena)
{
    Scratch scratch = scratch_begin(arena);
    const uint32_t n = count <= UINT32_MAX ? (uint32_t)count : 0;
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)n + 1);
    uint32_t* spare = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)n + 1);
    // By a name's first record's place: where its run starts in `order`; and the places that start a name, a bit each.
    uint32_t* run_of = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)n + 1);
    uint64_t* heads = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)n / 64 + 1);
    bool result = count <= UINT32_MAX && order != 0 && spare != 0 && run_of != 0 && heads != 0;
    bool same = result;
    if (result)
    {
        for (uint32_t i = 0; i < n; i += 1)
        {
            order[i] = i;
        }
        memset(heads, 0, ((uint64_t)n / 64 + 1) * sizeof(uint64_t));
        foreign_sort_records(records, order, spare, n);
        uint32_t run = 0;
        while (same && run < n)
        {
            const ForeignRecord* first = &records[order[run]];
            const bool group = (first->tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP;
            const uint8_t* name = 0;
            uint32_t length = 0;
            foreign_record_name(round, first, &name, &length);
            uint32_t end = run + 1;
            while (same && end < n && records[order[end]].hash == first->hash)
            {
                const ForeignRecord* other = &records[order[end]];
                const uint8_t* other_name = 0;
                uint32_t other_length = 0;
                foreign_record_name(round, other, &other_name, &other_length);
                same = ((other->tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP) == group && other_length == length && import_same_bytes(other_name, name, length);
                end += 1;
            }
            // A stable sort leaves a run's first record its earliest.
            const uint32_t head = order[run];
            heads[head / 64] |= 1ull << (head % 64);
            run_of[head] = run;
            run = end;
        }
    }
    if (result && same)
    {
        for (uint32_t w = 0; result && w < n / 64 + 1; w += 1)
        {
            for (uint64_t bits = heads[w]; result && bits != 0; bits &= bits - 1)
            {
                const uint32_t head = w * 64 + (uint32_t)__builtin_ctzll(bits);
                const ForeignRecord* first = &records[head];
                const bool group = (first->tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP;
                const uint8_t* name = 0;
                uint32_t length = 0;
                foreign_record_name(round, first, &name, &length);
                const uint32_t e = group ? foreign_group_entry(table, first->hash, name, length, arena) : foreign_entry(table, first->hash, name, length, arena);
                result = e != UINT32_MAX;
                for (uint32_t k = run_of[head]; result && k < n && records[order[k]].hash == first->hash; k += 1)
                {
                    foreign_fold(foreign, round, table, e, &records[order[k]], index);
                }
            }
        }
    }
    scratch_end(scratch);
    if (result && !same)
    {
        result = foreign_resolve_exact(foreign, round, index, table, records, count, arena);
    }
    return result;
}

// Looks a name up in the archives' indices and the libraries' exports: its first member by position and its first
// shared library in link order. An archive under --whole-archive has no index to look in: its members are all loaded.
static void foreign_probe(const Foreign* foreign, ForeignEntry* entry)
{
    for (uint32_t a = 0; entry->owner_archive == 0 && a < foreign->archive_count; a += 1)
    {
        const ForeignArchive* archive = &foreign->archives[a];
        uint32_t begin = 0;
        uint32_t end = 0;
        uint32_t member = UINT32_MAX;
        import_index_bucket(&archive->lookup, entry->hash, &begin, &end);
        for (uint32_t place = begin; place < end; place += 1)
        {
            const uint32_t i = archive->lookup.order[place];
            const String8 name = archive->index.symbols[i].name;
            const bool same = archive->lookup.hash[place] == entry->hash && name.length == entry->length && import_same_bytes((const uint8_t*)name.pointer, entry->name, name.length);
            const uint32_t candidate = archive->index.symbols[i].member;
            member = same && candidate < member ? candidate : member;
        }
        entry->owner_archive = member != UINT32_MAX ? a + 1 : 0;
        entry->owner_member = member != UINT32_MAX ? member : 0;
    }
    for (uint32_t l = 0; entry->shared_library == 0 && l < foreign->library_count; l += 1)
    {
        const uint32_t export = import_library_find(&foreign->libraries[l].library, entry->name, entry->length, entry->hash);
        entry->shared_library = export != 0 ? l + 1 : 0;
        entry->shared_export = export != 0 ? export - 1 : 0;
    }
    entry->flags |= BUSTER_FOREIGN_ENTRY_PROBED;
}

// Whether archive member `member` defines the name as GNU ld's common rule asks (is_global_data_symbol_definition): the
// first symbol of the name among its global ones, if defined: with STB_GLOBAL or an OS binding (GNU_UNIQUE), not a
// function or an IFUNC, not common, and not in a reserved section but SHN_ABS. The member isn't loaded: its header and
// symbol table are read here into locals, a symbol at a time, on both paths, since the rule is rare. A member that
// doesn't read doesn't define it.
static bool foreign_member_defines_data(const Foreign* foreign, const ForeignArchive* archive, uint32_t member, const ForeignEntry* entry)
{
    const ForeignInput* input = &foreign->inputs[archive->input];
    ImportMember header = archive->members[member];
    int error = archive->whole ? 0 : import_archive_member(&header, input->bytes, &archive->index, member).error == IMPORT_ERROR_NONE ? 0 : EINVAL;
    String8 bytes = header.bytes;
    if (error == 0 && archive->thin)
    {
        bytes = foreign_map_member(input, header.name, &error);
    }
    ImportElf elf = { 0 };
    ImportSymbols symbols = { 0 };
    bool readable = error == 0 && import_elf(&elf, bytes, ET_REL).error == IMPORT_ERROR_NONE && import_symbols(&elf, &symbols).error == IMPORT_ERROR_NONE && symbols.section != 0;
    const uint32_t first = readable ? import_section_header(&elf, symbols.section).sh_info : 0;
    readable = readable && first >= 1 && first <= symbols.count;
    bool result = false;
    bool found = false;
    for (uint32_t i = first; readable && !found && i < symbols.count; i += 1)
    {
        const Elf64_Sym symbol = import_symbol(&symbols, i);
        String8 name = { 0 };
        found = import_string(symbols.names, symbol.st_name, &name) && name.length == entry->length && import_same_bytes((const uint8_t*)name.pointer, entry->name, name.length) &&
                symbol.st_shndx != SHN_UNDEF;
        const uint32_t binding = ELF64_ST_BIND(symbol.st_info);
        const uint32_t type = ELF64_ST_TYPE(symbol.st_info);
        const bool reserved = symbol.st_shndx >= SHN_LORESERVE && symbol.st_shndx < SHN_ABS;
        result = found && (binding == STB_GLOBAL || binding >= STB_LOOS) && type != STT_FUNC && type != STT_GNU_IFUNC && symbol.st_shndx != SHN_COMMON && !reserved;
    }
    if (archive->thin && error == 0 && bytes.length != 0)
    {
        munmap(bytes.pointer, bytes.length);
    }
    return result;
}

// Adds archive `a`'s member `member` to this lane's needs.
static void foreign_need(ForeignLane* lane, uint32_t a, uint32_t member)
{
    if (lane->need_count == lane->need_capacity)
    {
        const uint32_t capacity = lane->need_capacity == 0 ? 64 : lane->need_capacity * 2;
        uint64_t* needs = BUSTER_ARENA_PUSH_ARRAY(&lane->arena, uint64_t, capacity);
        if (needs != 0)
        {
            memcpy(needs, lane->needs, lane->need_count * sizeof(uint64_t));
            lane->needs = needs;
            lane->need_capacity = capacity;
        }
    }
    if (lane->need_count < lane->need_capacity)
    {
        lane->needs[lane->need_count] = (uint64_t)a << 32 | member;
        lane->need_count += 1;
    }
    else
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
    }
}

static bool foreign_member_loaded(const ForeignArchive* archive, uint32_t member)
{
    return (archive->loaded[member / 64] >> (member % 64) & 1) != 0;
}

// The position of the shared library an entry's name binds to: the first that exports it, if every symbol of the name
// has default visibility; none otherwise.
static uint64_t foreign_shared_position(const Foreign* foreign, const ForeignEntry* entry)
{
    const bool bindable = entry->shared_library != 0 && entry->visibility == 0;
    return bindable ? foreign_position(foreign->libraries[entry->shared_library - 1].input, 0) : BUSTER_FOREIGN_NO_POSITION;
}

// What an entry the round touched loads (see the section's comment): its first member, and by GNU ld's common rule the
// first member after its earliest common symbol that defines it as global data.
static void foreign_decide(const Foreign* foreign, ForeignEntry* entry, ForeignLane* lane)
{
    if ((entry->flags & BUSTER_FOREIGN_ENTRY_PROBED) == 0 && (entry->defined == BUSTER_FOREIGN_NO_POSITION || (entry->flags & BUSTER_FOREIGN_ENTRY_COMMON) != 0))
    {
        foreign_probe(foreign, entry);
    }
    if (entry->owner_archive != 0 && entry->reference != BUSTER_FOREIGN_NO_POSITION)
    {
        const ForeignArchive* archive = &foreign->archives[entry->owner_archive - 1];
        const uint64_t owner = foreign_position(archive->input, entry->owner_member + 1);
        const uint64_t shadow = foreign_min(entry->defined, foreign_shared_position(foreign, entry));
        if (!foreign_member_loaded(archive, entry->owner_member) && foreign_max(owner, entry->reference) < shadow)
        {
            foreign_need(lane, entry->owner_archive - 1, entry->owner_member);
        }
    }
    const bool common_rule = (entry->flags & BUSTER_FOREIGN_ENTRY_COMMON) != 0 && (entry->flags & (BUSTER_FOREIGN_ENTRY_STRONG_DEFINED | BUSTER_FOREIGN_ENTRY_DATA_PROBED)) == 0;
    if (common_rule)
    {
        entry->flags |= BUSTER_FOREIGN_ENTRY_DATA_PROBED;
        bool found = false;
        for (uint32_t a = 0; !found && a < foreign->archive_count; a += 1)
        {
            const ForeignArchive* archive = &foreign->archives[a];
            uint32_t begin = 0;
            uint32_t end = 0;
            import_index_bucket(&archive->lookup, entry->hash, &begin, &end);
            // The bucket's entries are in index order.
            for (uint32_t place = begin; !found && place < end; place += 1)
            {
                const uint32_t i = archive->lookup.order[place];
                const String8 name = archive->index.symbols[i].name;
                const uint32_t member = archive->index.symbols[i].member;
                const bool same = archive->lookup.hash[place] == entry->hash && name.length == entry->length && import_same_bytes((const uint8_t*)name.pointer, entry->name, name.length);
                const bool after = foreign_position(archive->input, member + 1) > entry->common;
                found = same && after && !foreign_member_loaded(archive, member) && foreign_member_defines_data(foreign, archive, member, entry);
                if (found)
                {
                    foreign_need(lane, a, member);
                }
            }
        }
    }
}

// The partition's records of the round into its tables, then each touched entry's decision.
static void foreign_resolve_partition(Foreign* foreign, uint32_t partition, uint32_t index, ForeignLane* lane)
{
    ForeignRound* round = &foreign->rounds[index];
    ForeignTable* table = &foreign->tables[partition];
    const uint64_t first = round->partition_begin[partition];
    const uint64_t count = round->partition_begin[partition + 1] - first;
    bool result = foreign_touched_room(table, count, &lane->arena);
    if (result && foreign->path == FOREIGN_PATH_VECTOR)
    {
        result = foreign_resolve_vector(foreign, round, index, table, round->records + first, count, &lane->arena);
    }
    else if (result)
    {
        result = foreign_resolve_exact(foreign, round, index, table, round->records + first, count, &lane->arena);
    }
    for (uint32_t t = 0; result && t < table->touched_count; t += 1)
    {
        foreign_decide(foreign, &table->entries[table->touched[t]], lane);
    }
    if (!result)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
    }
}

static void foreign_resolve(Foreign* foreign, uint32_t index, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    lane->need_count = 0;
    const Range64 partitions = lane_range(foreign->partition_count);
    for (uint64_t p = partitions.begin; p < partitions.end; p += 1)
    {
        foreign_resolve_partition(foreign, (uint32_t)p, index, lane);
    }
    start = foreign_time(lane, FOREIGN_PHASE_RESOLVE, start);
    (void)start;
    lane_sync();
}

// Lane 0, after a round's resolution: every lane's needs marked in the archives' bitsets; those not loaded before become
// the next round's objects, in position order (archive by archive, member by member). Returns how many. When none, the
// rounds are over, and the names' columns are pushed: every partition's entries are final.
static uint32_t foreign_load(Foreign* foreign, uint32_t index, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    Scratch scratch = scratch_begin(&lane->arena);
    // Each archive's pending members, a bit each, from its first word in `pending`.
    uint64_t* bases = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)foreign->archive_count + 1);
    uint64_t words = 0;
    for (uint32_t a = 0; bases != 0 && a < foreign->archive_count; a += 1)
    {
        bases[a] = words;
        words += foreign->archives[a].member_count / 64 + 1;
    }
    uint64_t* pending = bases != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, words + 1) : 0;
    uint32_t added = 0;
    if (pending == 0)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
    }
    else
    {
        memset(pending, 0, (words + 1) * sizeof(uint64_t));
        for (uint32_t l = 0; l < foreign->lanes; l += 1)
        {
            const ForeignLane* other = &foreign->lane_states[l];
            for (uint32_t n = 0; n < other->need_count; n += 1)
            {
                const uint32_t a = (uint32_t)(other->needs[n] >> 32);
                const uint32_t m = (uint32_t)other->needs[n];
                ForeignArchive* archive = &foreign->archives[a];
                if (!foreign_member_loaded(archive, m))
                {
                    archive->loaded[m / 64] |= 1ull << (m % 64);
                    pending[bases[a] + m / 64] |= 1ull << (m % 64);
                }
            }
        }
        for (uint32_t a = 0; a < foreign->archive_count; a += 1)
        {
            ForeignArchive* archive = &foreign->archives[a];
            const uint32_t archive_words = archive->member_count / 64 + 1;
            for (uint32_t w = 0; w < archive_words; w += 1)
            {
                for (uint64_t bits = pending[bases[a] + w]; bits != 0; bits &= bits - 1)
                {
                    const uint32_t m = w * 64 + (uint32_t)__builtin_ctzll(bits);
                    const uint32_t id = foreign->object_count;
                    archive->member_objects[m] = id + 1;
                    foreign->objects[id] = (ForeignObject){
                        .position = foreign_position(archive->input, m + 1),
                        .input = archive->input,
                        .member = m + 1,
                    };
                    foreign->positions[id] = foreign->objects[id].position;
                    foreign->object_count += 1;
                    added += 1;
                }
            }
        }
    }
    scratch_end(scratch);
    foreign->round_count = index + 1;
    foreign->rounds[index + 1] = (ForeignRound){
        .first_object = foreign->rounds[index].end_object,
        .end_object = foreign->object_count,
    };
    if (added == 0)
    {
        uint64_t names = 0;
        for (uint32_t p = 0; p < foreign->partition_count; p += 1)
        {
            names += foreign->tables[p].count;
        }
        ForeignNames* columns = &foreign->names;
        Arena* arena = &foreign->arena;
        columns->count = (uint32_t)names;
        columns->bytes = BUSTER_ARENA_PUSH_ARRAY(arena, const uint8_t*, names + 1);
        columns->length = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, names + 1);
        columns->hash = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, names + 1);
        columns->resolution = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, names + 1);
        columns->symbol = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, names + 1);
        columns->library = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, names + 1);
        columns->definitions = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, names + 1);
        columns->second = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, names + 1);
        columns->common_size = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, names + 1);
        columns->common_alignment = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, names + 1);
        columns->visibility = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, names + 1);
        columns->flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, names + 1);
        const bool pushed = names < UINT32_MAX && columns->bytes != 0 && columns->length != 0 && columns->hash != 0 && columns->resolution != 0 && columns->symbol != 0 &&
                            columns->library != 0 && columns->definitions != 0 && columns->second != 0 && columns->common_size != 0 && columns->common_alignment != 0 &&
                            columns->visibility != 0 && columns->flags != 0;
        if (!pushed)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
        }
    }
    start = foreign_time(lane, FOREIGN_PHASE_LOAD, start);
    (void)start;
    return added;
}

// ---------------------------------------------------------------------------------------------------------------------
// COMDAT groups (step 5) and the winners
//
// After the last round, each group signature's entry has its earliest group by position: each partition's owner marks
// its groups kept or not, then each lane marks its objects' sections of the groups not kept as discarded. Then each
// partition's owner takes its records of every round again, for the winners of its names: a definition in a discarded
// section counts as a reference of its binding (GNU ld treats it so; loading is over, so it loads nothing). A name's
// winner: its earliest strong definition by position (two or more are a duplicate), else its largest common symbol
// (the earliest of equal sizes, with the largest alignment of them all), else its earliest weak definition, else its
// first shared library's export if every symbol of it has default visibility, else nothing: undefined if a strong
// reference refers to it, 0 if only weak ones do. Every lane places every partition's names by a prefix sum of their
// counts and fills its own partitions' columns, then each symbol's name id. Both paths share this pass: it reads each
// record once, and folds it into its entry's winners by compares.

// The round a global group id belongs to.
static const ForeignRound* foreign_group_round(const Foreign* foreign, uint32_t group)
{
    uint32_t low = 0;
    uint32_t high = foreign->round_count;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (foreign->rounds[middle].first_group <= group)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return &foreign->rounds[low];
}

static void foreign_groups(Foreign* foreign, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    const uint32_t partitions = foreign->partition_count;
    const Range64 mine = lane_range(partitions);
    for (uint64_t p = mine.begin; p < mine.end; p += 1)
    {
        const ForeignTable* table = &foreign->tables[p];
        for (uint32_t r = 0; r < foreign->round_count; r += 1)
        {
            ForeignRound* round = &foreign->rounds[r];
            for (uint64_t i = round->partition_begin[p]; i < round->partition_begin[p + 1]; i += 1)
            {
                const ForeignRecord record = round->records[i];
                if ((record.tag & BUSTER_FOREIGN_TAG_CLASS) == FOREIGN_CLASS_GROUP)
                {
                    const uint32_t group = round->first_group + record.item;
                    round->groups.kept[record.item] = table->groups[round->groups.entry[record.item]].group == group ? 1 : 0;
                }
            }
        }
    }
    lane_sync();
    const Range64 objects = lane_range(foreign->object_count);
    for (uint64_t id = objects.begin; id < objects.end; id += 1)
    {
        ForeignObject* object = &foreign->objects[id];
        const ForeignRound* round = object->group_count != 0 ? foreign_group_round(foreign, object->group_begin) : 0;
        for (uint32_t g = 0; g < object->group_count; g += 1)
        {
            if (round->groups.kept[object->group_begin - round->first_group + g] == 0)
            {
                if (object->discarded == 0)
                {
                    object->discarded = BUSTER_ARENA_PUSH_ARRAY(&lane->arena, uint8_t, object->elf.section_count);
                    if (object->discarded != 0)
                    {
                        memset(object->discarded, 0, object->elf.section_count);
                    }
                }
                const ImportGroup* group = &object->group_list[g];
                for (uint32_t k = 0; object->discarded != 0 && k < group->count; k += 1)
                {
                    object->discarded[object->group_sections[group->first + k]] = 1;
                }
                if (object->discarded == 0)
                {
                    foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
                }
            }
        }
    }
    start = foreign_time(lane, FOREIGN_PHASE_GROUPS, start);
    (void)start;
    lane_sync();
}

// One name's winners, as the records of it come.
typedef struct ForeignWinner ForeignWinner;
struct ForeignWinner
{
    uint64_t strong_position;
    uint64_t common_position;
    uint64_t weak_position;
    uint64_t common_size;
    uint32_t strong;
    uint32_t second;
    uint32_t strong_count;
    uint32_t common;
    uint32_t weak;
    uint8_t common_alignment;
    uint8_t flags;
};

// Folds a record of round `round` into its name's winners (symbols as global ids plus 1, 0 for none).
static void foreign_win(ForeignWinner* winner, const Foreign* foreign, const ForeignRound* round, const ForeignRecord* record)
{
    const uint32_t at = record->item;
    const uint32_t symbol = round->first_symbol + at;
    const uint32_t object = record->tag >> BUSTER_FOREIGN_TAG_OBJECT_SHIFT;
    const ForeignObject* holder = &foreign->objects[object];
    const uint64_t position = foreign->positions[object];
    const uint32_t section = round->symbols.section[at];
    const bool discarded = holder->discarded != 0 && section != SHN_UNDEF && section < holder->elf.section_count && holder->discarded[section] != 0;
    ForeignClass class = (ForeignClass)(record->tag & BUSTER_FOREIGN_TAG_CLASS);
    if ((class == FOREIGN_CLASS_STRONG || class == FOREIGN_CLASS_WEAK) && discarded)
    {
        class = class == FOREIGN_CLASS_STRONG ? FOREIGN_CLASS_REFERENCE : FOREIGN_CLASS_WEAK_REFERENCE;
    }
    if (class == FOREIGN_CLASS_STRONG)
    {
        const bool earlier = winner->strong == 0 || position < winner->strong_position || (position == winner->strong_position && symbol + 1 < winner->strong);
        // The second earliest, for a duplicate's report: the displaced first, or this one if it isn't earlier.
        const uint32_t displaced = earlier ? winner->strong : symbol + 1;
        const bool second = displaced != 0 && (winner->second == 0 || earlier || displaced < winner->second);
        winner->second = second ? displaced : winner->second;
        winner->strong_position = earlier ? position : winner->strong_position;
        winner->strong = earlier ? symbol + 1 : winner->strong;
        winner->strong_count += 1;
    }
    else if (class == FOREIGN_CLASS_COMMON)
    {
        const uint64_t size = round->symbols.size[at];
        const uint8_t alignment = (uint8_t)__builtin_ctzll(round->symbols.value[at]);
        const bool better = winner->common == 0 || size > winner->common_size || (size == winner->common_size && position < winner->common_position);
        winner->common_position = better ? position : winner->common_position;
        winner->common_size = better ? size : winner->common_size;
        winner->common = better ? symbol + 1 : winner->common;
        winner->common_alignment = alignment > winner->common_alignment ? alignment : winner->common_alignment;
    }
    else if (class == FOREIGN_CLASS_WEAK)
    {
        const bool earlier = winner->weak == 0 || position < winner->weak_position || (position == winner->weak_position && symbol + 1 < winner->weak);
        winner->weak_position = earlier ? position : winner->weak_position;
        winner->weak = earlier ? symbol + 1 : winner->weak;
    }
    else
    {
        winner->flags |= class == FOREIGN_CLASS_REFERENCE ? BUSTER_FOREIGN_NAME_REFERENCED : BUSTER_FOREIGN_NAME_WEAK_REFERENCED;
    }
}

static void foreign_winners(Foreign* foreign, ForeignLane* lane)
{
    uint64_t start = foreign_now();
    const uint32_t partitions = foreign->partition_count;
    const Range64 mine = lane_range(partitions);
    uint32_t base = 0;
    for (uint32_t p = 0; p < partitions; p += 1)
    {
        if (p >= mine.begin && p < mine.end)
        {
            foreign->tables[p].name_base = base;
        }
        base += foreign->tables[p].count;
    }
    ForeignNames* names = &foreign->names;
    for (uint64_t p = mine.begin; p < mine.end; p += 1)
    {
        ForeignTable* table = &foreign->tables[p];
        Scratch scratch = scratch_begin(&lane->arena);
        ForeignWinner* winners = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ForeignWinner, (uint64_t)table->count + 1);
        if (winners == 0)
        {
            foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
        }
        else
        {
            memset(winners, 0, ((uint64_t)table->count + 1) * sizeof(ForeignWinner));
            for (uint32_t r = 0; r < foreign->round_count; r += 1)
            {
                const ForeignRound* round = &foreign->rounds[r];
                for (uint64_t i = round->partition_begin[p]; i < round->partition_begin[p + 1]; i += 1)
                {
                    const ForeignRecord* record = &round->records[i];
                    if ((record->tag & BUSTER_FOREIGN_TAG_CLASS) != FOREIGN_CLASS_GROUP)
                    {
                        foreign_win(&winners[round->symbols.entry[record->item]], foreign, round, record);
                    }
                }
            }
            for (uint32_t e = 0; e < table->count; e += 1)
            {
                ForeignEntry* entry = &table->entries[e];
                const ForeignWinner* winner = &winners[e];
                const uint32_t id = table->name_base + e;
                ForeignResolution resolution = FOREIGN_RESOLUTION_UNDEFINED;
                uint32_t symbol = 0;
                uint32_t library = 0;
                if (winner->strong != 0 || winner->common != 0 || winner->weak != 0)
                {
                    resolution = winner->strong != 0 ? FOREIGN_RESOLUTION_STRONG : winner->common != 0 ? FOREIGN_RESOLUTION_COMMON : FOREIGN_RESOLUTION_WEAK;
                    symbol = (winner->strong != 0 ? winner->strong : winner->common != 0 ? winner->common : winner->weak) - 1;
                }
                else
                {
                    if ((entry->flags & BUSTER_FOREIGN_ENTRY_PROBED) == 0)
                    {
                        foreign_probe(foreign, entry);
                    }
                    const bool shared = foreign_shared_position(foreign, entry) != BUSTER_FOREIGN_NO_POSITION;
                    resolution = shared ? FOREIGN_RESOLUTION_SHARED : (winner->flags & BUSTER_FOREIGN_NAME_REFERENCED) != 0 ? FOREIGN_RESOLUTION_UNDEFINED : FOREIGN_RESOLUTION_WEAK_UNDEFINED;
                    symbol = shared ? entry->shared_export : 0;
                    library = shared ? entry->shared_library - 1 : 0;
                }
                names->bytes[id] = entry->name;
                names->length[id] = entry->length;
                names->hash[id] = entry->hash;
                names->resolution[id] = (uint8_t)resolution;
                names->symbol[id] = symbol;
                names->library[id] = library;
                names->definitions[id] = winner->strong_count;
                names->second[id] = winner->second != 0 ? winner->second - 1 : 0;
                names->common_size[id] = winner->common != 0 ? winner->common_size : 0;
                names->common_alignment[id] = winner->common != 0 ? winner->common_alignment : 0;
                names->visibility[id] = foreign_rank_visibilities[entry->visibility];
                names->flags[id] = winner->flags;
            }
        }
        scratch_end(scratch);
    }
    lane_sync();
    // Each symbol's name id: its partition's base and its entry, round by round.
    for (uint32_t r = 0; r < foreign->round_count; r += 1)
    {
        ForeignRound* round = &foreign->rounds[r];
        const Range64 symbols = lane_range(round->symbol_count);
        for (uint64_t s = symbols.begin; s < symbols.end; s += 1)
        {
            round->symbols.name_id[s] = foreign->tables[foreign_partition(foreign, round->symbols.hash[s])].name_base + round->symbols.entry[s];
        }
    }
    start = foreign_time(lane, FOREIGN_PHASE_WINNERS, start);
    (void)start;
    lane_sync();
}

// ---------------------------------------------------------------------------------------------------------------------
// Running

static void foreign_lane_main(void* argument)
{
    Foreign* foreign = argument;
    ForeignLane* lane = &foreign->lane_states[lane_index()];
    lane->arena = arena_reserve(BUSTER_FOREIGN_LANE_ARENA_BYTES);
    if (lane->arena.base == 0)
    {
        foreign_fail(lane, foreign_failure(FOREIGN_ERROR_MEMORY, 0, 0, 0));
    }
    lane_sync();
    if (!foreign_failed(foreign))
    {
        foreign_open(foreign, lane);
    }
    if (!foreign_failed(foreign))
    {
        uint64_t start = foreign_now();
        if (lane_index() == 0)
        {
            foreign_list_inputs(foreign, lane);
        }
        lane_sync();
        if (!foreign_failed(foreign))
        {
            foreign_read_indices(foreign, lane);
        }
        lane_sync();
        if (!foreign_failed(foreign) && lane_index() == 0)
        {
            foreign_first_round(foreign, lane);
        }
        start = foreign_time(lane, FOREIGN_PHASE_INDICES, start);
        (void)start;
        lane_sync();
    }
    bool more = !foreign_failed(foreign);
    for (uint32_t round = 0; more; round += 1)
    {
        foreign_parse(foreign, round, lane);
        more = !foreign_failed(foreign);
        if (more)
        {
            foreign_exchange(foreign, round, lane);
            more = !foreign_failed(foreign);
        }
        if (more)
        {
            foreign_resolve(foreign, round, lane);
            more = !foreign_failed(foreign);
        }
        uint32_t added = 0;
        if (more && lane_index() == 0)
        {
            added = foreign_load(foreign, round, lane);
        }
        added = (uint32_t)lane_broadcast(added, 0);
        more = more && !foreign_failed(foreign) && added != 0;
    }
    if (!foreign_failed(foreign))
    {
        foreign_groups(foreign, lane);
    }
    if (!foreign_failed(foreign))
    {
        foreign_winners(foreign, lane);
    }
}

static ForeignFailure foreign_run(Foreign* foreign, const ForeignCommand* command, ForeignPath path, uint32_t lanes, const uint32_t* cpus)
{
    *foreign = (Foreign){
        .path = path,
        .lanes = lanes,
        .command = command,
        .arena = arena_reserve(BUSTER_FOREIGN_ARENA_BYTES),
    };
    ForeignFailure result = { 0 };
    foreign->lane_states = BUSTER_ARENA_PUSH_ARRAY(&foreign->arena, ForeignLane, lanes);
    foreign->inputs = BUSTER_ARENA_PUSH_ARRAY(&foreign->arena, ForeignInput, (uint64_t)command->argument_count + 1);
    if (lanes == 0 || lanes > BUSTER_LANE_MAX_COUNT || foreign->lane_states == 0 || foreign->inputs == 0)
    {
        result = foreign_failure(lanes == 0 || lanes > BUSTER_LANE_MAX_COUNT ? FOREIGN_ERROR_LANES : FOREIGN_ERROR_MEMORY, 0, 0, 0);
    }
    else
    {
        memset(foreign->lane_states, 0, lanes * sizeof(ForeignLane));
        for (uint32_t i = 0; i < command->argument_count; i += 1)
        {
            foreign->inputs[i] = (ForeignInput){
                .name = command->arguments[i].name,
                .flags = command->arguments[i].flags,
                .bytes = command->arguments[i].bytes,
                .given = command->arguments[i].bytes.pointer != 0,
            };
        }
        foreign->input_count = command->argument_count;
        // lane_run returns false too when a lane couldn't be pinned, which only a benchmark asks for: it ran then.
        if (!lane_run(lanes, foreign_lane_main, foreign, cpus) && cpus == 0)
        {
            result = foreign_failure(FOREIGN_ERROR_LANES, 0, 0, 0);
        }
        for (uint32_t i = 0; i < lanes; i += 1)
        {
            const ForeignFailure* failure = &foreign->lane_states[i].failure;
            result = failure->error != FOREIGN_ERROR_NONE && foreign_earlier(failure, &result) ? *failure : result;
        }
    }
    foreign->failure = result;
    return result;
}

static void foreign_release(Foreign* foreign)
{
    for (uint32_t i = 0; foreign->inputs != 0 && i < foreign->input_count; i += 1)
    {
        if (foreign->inputs[i].bytes.length != 0 && !foreign->inputs[i].given)
        {
            munmap(foreign->inputs[i].bytes.pointer, foreign->inputs[i].bytes.length);
        }
    }
    for (uint32_t id = 0; foreign->objects != 0 && id < foreign->object_count; id += 1)
    {
        if (foreign->objects[id].mapped)
        {
            munmap(foreign->objects[id].bytes.pointer, foreign->objects[id].bytes.length);
        }
    }
    for (uint32_t i = 0; foreign->lane_states != 0 && i < foreign->lanes; i += 1)
    {
        arena_release(&foreign->lane_states[i].arena);
    }
    arena_release(&foreign->arena);
    *foreign = (Foreign){ 0 };
}

// The last object whose global_begin is at most the symbol: an object without globals begins where the next one does,
// so the last such is the one whose range holds it.
static uint32_t foreign_symbol_object(const Foreign* foreign, uint32_t symbol)
{
    uint32_t low = 0;
    uint32_t high = foreign->object_count;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (foreign->objects[middle].global_begin <= symbol)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return low;
}

static const ForeignRound* foreign_symbol_round(const Foreign* foreign, uint32_t symbol)
{
    uint32_t low = 0;
    uint32_t high = foreign->round_count;
    while (high - low > 1)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (foreign->rounds[middle].first_symbol <= symbol)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return &foreign->rounds[low];
}

static uint32_t foreign_symbol_index(const Foreign* foreign, uint32_t symbol)
{
    const ForeignObject* object = &foreign->objects[foreign_symbol_object(foreign, symbol)];
    return object->first_global + (symbol - object->global_begin);
}

static uint32_t foreign_find_name(const Foreign* foreign, const uint8_t* name, uint32_t length, uint64_t hash)
{
    uint32_t result = 0;
    const ForeignTable* table = foreign->tables != 0 ? &foreign->tables[foreign_partition(foreign, hash)] : 0;
    if (table != 0 && table->slots != 0)
    {
        uint32_t slot = (uint32_t)hash & table->slot_mask;
        while (result == 0 && table->slots[slot] != 0)
        {
            const uint32_t e = table->slots[slot] - 1;
            const ForeignEntry* entry = &table->entries[e];
            const bool same = entry->hash == hash && entry->length == length && import_same_bytes(entry->name, name, length);
            result = same ? table->name_base + e + 1 : 0;
            slot = (slot + 1) & table->slot_mask;
        }
    }
    return result;
}
