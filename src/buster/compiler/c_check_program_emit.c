// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The real consumer of CheckProgram: lower, validate, compile, size branches, encode, and publish code atoms and
// their symbolic fixups in the Link shared with static data. check_program assigns source object symbols;
// lowering appends helper and explicit target-runtime symbols before any atom/fixup publication.
// data_capacity reserves the data consumer's atoms/fixups; that consumer defines its already-created symbols.
// The caller performs final link_check after both publications. Output tables/names and byte arenas are distinct
// from scratch; all three roll back on failure. Backend template initialization precedes concurrent workers, only
// when the selected program has a body: its helpers also come only from those bodies. Data-only links need no tables.
// A template that takes directives is an assembled unit (c_check_program_asm.c): its atoms, symbols and fixups are
// merged into the link (check_program_emit_asm_unit), its code becoming its helper's or naked function's atom, and its
// sections joining -c's object as file-scope assembly's do.
// Every function's code is the tier-neutral EmittedCode (emitted_code.c, #1056): Tier 0's adapted (emitted_from_tier0),
// or Tier 1's (check_tier1_compile). With `unwind`, every function's unwind table too (#395): its frame events (Tier 0's
// from the frame it published, its tail calls' teardowns among them, #147; Tier 1's from its frame emitter, its early
// returns' epilogues bracketed and open where its code doesn't end in one, #177) resolved through its records' starts
// and ends after branch sizing (emitted_unwind), written after the last function (unwind_write) as one EH_FRAME atom,
// a CIE and an FDE per function after it, each FDE's initial location a PC32 fixup to its function's symbol; with
// `verify`, each proved against its records/control flow, with the same CFI as the fixed-frame analyzer (unwind_tier0).
// Backend tests share only the frame verifier; source consumers retain the complete emission below.
#if !defined(BUSTER_UNIT) || BUSTER_SOURCE_CONSUMER
#include "worker_budget.c"
#include "c_check_tier1_outcome.c"

// The class constant CFStrings point to (#148), clang's global of this name.
static const char check_program_cfstring_class_name[] = "__CFConstantStringClassReference";

// What an executable's link binds against (the direct path, #157): the C runtime, which bind_begin joins between the
// program's symbols and its atoms, its arrays from `arena` and its atoms' bytes copied into the emission's byte arena.
// check_program_emit makes room for it in the link and calls bind_begin; its caller adds the static data and calls
// bind_end. An object's emission has none.
typedef struct CheckProgramBinding CheckProgramBinding;
struct CheckProgramBinding
{
    const Runtime* runtime;
    Bind* bind;
    BindPath path;
    Arena* arena;
    // The static data the caller publishes after the code (check_data_prepare's objects): binding takes their symbols,
    // as the bodies' and the helpers', for the program's definitions before their atoms join.
    const CheckDataObject* data_objects;
    uint32_t data_count;
    // When bind_begin fails about a name (Bind.name), its bytes, since a failed emission rolls its link back.
    char failed_name[256];
};

// A file-scope assembly label that names a C definition of the unit (#1848, check_program_emit_asm_symbols): clang's
// assembler reports "symbol 'NAME' is already defined" without a source location as the module's definition is
// streamed after the label, once for a function or a common symbol and twice for another variable's, which the driver
// prints as it does (check_driver_emission_failed). The name is copied, as a failed emission rolls its arenas back.
typedef struct CheckProgramRedefined CheckProgramRedefined;
struct CheckProgramRedefined
{
    char name[256];
    uint32_t length;
    uint32_t lines;
};

typedef struct CheckProgramCode CheckProgramCode;
struct CheckProgramCode
{
    Link link;
    CheckProgramLowered lowered;
    AtomId* function_atoms;
    // #619: the file-scope assembly's atoms (check_program_asm_plan_file's, which check_program_emit_asm_unit publishes),
    // in the link before the functions', and each one's section, which -c's object gives it (check_program_sections).
    AtomId* asm_atoms;
    ObjectAtomSection* asm_sections;
    uint32_t asm_atom_count;
    // Its section groups (COMDAT, #1848), the assembler's (asm_object_groups) with the program's names, symbols and atoms,
    // group 0 null, and each asm atom's group (0 for none), parallel to asm_atoms; check_program_sections publishes them
    // with selectany's in the link's LinkGroups. None for a unit whose assembly makes none.
    LinkGroup* asm_groups;
    uint32_t* asm_atom_groups;
    uint32_t asm_group_count;
    // GNU's retain attribute (#147): the symbols of the program whose declarations have one before their definitions, in
    // symbol order, whose definitions -c's object puts in sections of their own with SHF_GNU_RETAIN
    // (check_program_sections); none in a unit without one.
    SymbolId* retained_symbols;
    uint32_t retained_count;
    // selectany definitions are weak symbols whose atoms and relocations form COMDAT groups of their names.
    SymbolId* selectany_symbols;
    uint32_t selectany_count;
    // The atoms of templates that take directives (CheckProgramAsm.unit), which the body loop publishes: each one's
    // section, with the file-scope assembly's, for -c's object.
    AtomId* unit_atoms;
    ObjectAtomSection* unit_sections;
    uint32_t unit_atom_count;
    // The units' labels that -c's object leaves out, as the assembler's own temporary symbols: their `.L` names, their
    // numeric labels and what asm_object_options marked (ObjectOptions.temporary_symbols).
    SymbolId* unit_temporary;
    uint32_t unit_temporary_count;
    // The unit program symbols whose `.size` a template set, indexed by SymbolId: the values, and which ones. The
    // definitions' atoms exist only after the static data is published, so check_program_sections applies them.
    uint64_t* asm_sizes;
    uint8_t* asm_size_set;
    // A template's `.comm` of a variable the unit defines (#1848): clang's assembler makes the name its common symbol, of
    // the C definition's size at the `.comm`'s alignment, and leaves the definition's bytes in their section without a
    // symbol. asm_commoned marks such names (by CheckProgramAsms.names index, null for none); check_program_emit_asm_unit
    // leaves each one's symbol undefined and records it with its common atom (commoned_symbols, commoned_atoms), which
    // check_program_sections defines it at once the static data has defined it at its C definition's atom.
    uint8_t* asm_commoned;
    SymbolId* commoned_symbols;
    AtomId* commoned_atoms;
    uint32_t commoned_count;
    CheckProgramRedefined redefined;
    // patchable_function_entry's (#1447, check_program_patchable): each such function's entry, `patchable_count` of them in
    // its bodies' order, an 8-byte atom (patchable_atoms) whose absolute fixup points to its NOP sled, through a label at its
    // function's atom's start (patchable_functions): local temporaries from patchable_symbol_base, named
    // .Lbuster_patchable_N, which -c's object leaves out as clang's assembler leaves its .Ltmp label. Each entry's section
    // (patchable_sections, empty for __patchable_function_entries) is the one check_program_sections gives it, linked to
    // its function's (SHF_LINK_ORDER). None in a unit without the attribute or -fpatchable-function-entry.
    AtomId* patchable_atoms;
    AtomId* patchable_functions;
    String8* patchable_sections;
    SymbolId patchable_symbol_base;
    uint32_t patchable_count;
    CheckProgramStatus status;
    Tier0Error backend_error;
    CheckTier1Outcome tier1_failure;
    uint32_t token;
    uint32_t backend_node;
    // CheckProgramLowered.reason, or the attribute or construct the emission refused.
    const char* reason;
    uint64_t records;
    uint64_t deferred;
    uint64_t code_bytes;
    // Optional source-body identities for --tier1-function-sizes. Kept until the driver has published the object,
    // so the manifest reads its final symbol sizes after named sections and assembly .size directives are applied.
    SymbolId* tier1_size_symbols;
    uint8_t* tier1_size_kinds;
    uint32_t tier1_size_count;
    bool tier1_sizes_ready;
    // The first of the symbols cpu_dispatch's and cpu_specific's groups add (#1447, check_program_cpu_symbols).
    SymbolId cpu_first;
    // The first of the symbols the groups of multiversioning by target strings add (#1838, check_program_target_symbols).
    SymbolId target_first;
    // References that take another symbol of the unit's (#2204, check_program_label_forwards): keys of the referring
    // symbol in the high half and the one its fixups take in the low; null in a unit without them, the usual one.
    uint64_t* label_forwards;
    uint32_t label_forward_count;
};

#include "c_check_program_cpu_dispatch.c"
#include "c_check_program_target_versions.c"

// Whether symbol `t` is named as `s` is.
static bool check_program_label_same_name(const Link* link, SymbolId s, SymbolId t)
{
    const NameId name = link->symbols.name[s];
    const NameId other = link->symbols.name[t];
    return t != s && name != 0 && other != 0 && link->names.hash[other] == link->names.hash[name] && link->names.length[other] == link->names.length[name] &&
           memcmp(link->names.bytes[other], link->names.bytes[name], link->names.length[name]) == 0;
}

// The made symbol of multiversioning named as `s` that the unit defines (#2204), or 0: a target version's own or a
// target_clones body's (CheckProgram.target_symbols), a copy's (clone_symbols), a cpu_specific version's (cpu_symbols),
// or one of the names their groups add at their bodies, after the program's symbols (from `first`, the program's source).
static SymbolId check_program_label_made(const CheckProgramCode* code, const CheckProgram* program, SymbolId s, SymbolId first)
{
    const Link* link = &code->link;
    SymbolId found = 0;
    for (uint32_t i = 0; found == 0 && program->target_symbols != NULL && i < program->target_version_count + program->target_group_count; i += 1)
    {
        found = program->target_symbols[i] != 0 && check_program_label_same_name(link, s, program->target_symbols[i]) ? program->target_symbols[i] : 0;
    }
    for (uint32_t i = 0; found == 0 && program->clone_symbols != NULL && i < program->clone_option_count; i += 1)
    {
        found = program->clone_symbols[i] != 0 && check_program_label_same_name(link, s, program->clone_symbols[i]) ? program->clone_symbols[i] : 0;
    }
    for (uint32_t i = 0; found == 0 && program->cpu_symbols != NULL && i < program->cpu_version_count + program->cpu_group_count; i += 1)
    {
        found = program->cpu_symbols[i] != 0 && check_program_label_same_name(link, s, program->cpu_symbols[i]) ? program->cpu_symbols[i] : 0;
    }
    for (SymbolId t = first; found == 0 && first != 0 && t < link->symbols.count; t += 1)
    {
        found = link->symbols.source[t] == BUSTER_LINK_SOURCE_PROGRAM && check_program_label_same_name(link, s, t) ? t : 0;
    }
    return found;
}

// The references of a declaration whose asm label names a symbol the unit makes and defines (#2204): a target version's
// (f.avx2), a target_clones body's or version's (f.avx2.0, f.default.1), a cpu_specific version's. clang's module has one
// global per name, so the declaration's references are the definition's; here each program symbol of an object the
// unit doesn't define whose name is such a symbol's is forwarded to it (CheckProgramCode.label_forwards), as its source
// the program's, so that no runtime binds it; check_program_sections moves its fixups once every atom exists. With no
// fixup left, neither -c's object nor the executable has it. Only a name with a '.' can be made, so only such a symbol
// is looked for. Returns false where there was no room.
static bool check_program_label_forwards(CheckProgramCode* code, const CheckProgram* program, Arena* output)
{
    Link* link = &code->link;
    const SymbolId first = code->cpu_first != 0 && (code->target_first == 0 || code->cpu_first < code->target_first) ? code->cpu_first : code->target_first;
    uint32_t count = 0;
    uint64_t* forwards = NULL;
    bool valid = true;
    // Twice: count, then write.
    for (uint32_t pass = 0; valid && pass < 2; pass += 1)
    {
        count = 0;
        for (SymbolId s = 1; s <= program->symbol_count && s < link->symbols.count; s += 1)
        {
            const uint32_t object = program->symbol_objects[s - 1];
            const NameId name = link->symbols.name[s];
            const bool dotted = name != 0 && memchr(link->names.bytes[name], '.', link->names.length[name]) != NULL;
            const SymbolId target = dotted && object != 0 && !check_program_cpu_defined(program, object) ? check_program_label_made(code, program, s, first) : 0;
            if (target != 0 && forwards != NULL)
            {
                forwards[count] = (uint64_t)s << 32 | target;
                link->symbols.source[s] = BUSTER_LINK_SOURCE_PROGRAM;
            }
            count += target != 0;
        }
        if (pass == 0)
        {
            forwards = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, count) : NULL;
            valid = count == 0 || forwards != NULL;
            pass += count == 0;
        }
    }
    code->label_forwards = forwards;
    code->label_forward_count = valid ? count : 0;
    if (!valid)
    {
        code->status = CHECK_PROGRAM_RESOURCE;
        code->reason = "an asm label naming a multiversioned function's symbol couldn't be joined to it";
    }
    return valid;
}

// Moves the fixups of each forwarded symbol (CheckProgramCode.label_forwards, #2204) to the symbol it takes.
static void check_program_label_forward_fixups(CheckProgramCode* code)
{
    Link* link = &code->link;
    for (uint32_t f = 0; code->label_forward_count != 0 && f < link->fixups.count; f += 1)
    {
        const SymbolId target = link->fixups.target[f];
        for (uint32_t k = 0; k < code->label_forward_count; k += 1)
        {
            link->fixups.target[f] = code->label_forwards[k] >> 32 == target ? (SymbolId)code->label_forwards[k] : link->fixups.target[f];
        }
    }
    // No fixup refers to them now: an undefined reference no object lists, as no runtime bound it, and weak, which an
    // executable resolves to nothing rather than refuse.
    for (uint32_t k = 0; k < code->label_forward_count; k += 1)
    {
        const SymbolId symbol = (SymbolId)(code->label_forwards[k] >> 32);
        link->symbols.source[symbol] = BUSTER_LINK_SOURCE_NONE;
        link->symbols.flags[symbol] = (uint8_t)((link->symbols.flags[symbol] & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_WEAK);
    }
}

// The local symbols whose names the compiler makes are named as clang names them, so that tools and #129's comparison
// with clang's objects (c_lowering_symbols.c) read them alike: a block-scope static is "function.name", its function's
// symbol name (its asm label, if it has one) and its own (an asm label of the static's own replaces both), and a
// file-scope compound literal is ".compoundliteral". A made name that a symbol has already (one the source gives, an
// identifier or an asm label, or one made before) gets ".N", N from one counter for the unit, as LLVM makes a name
// unique in its module. `taken` is a table of mask + 1 NameIds, 0 for an empty slot, with room for every symbol's name
// and at least as many empty slots.
typedef struct CheckProgramNames CheckProgramNames;
struct CheckProgramNames
{
    NameId* taken;
    uint32_t mask;
    uint32_t unique;
};

// Whether a name of `names` is the `length` bytes at `bytes`, of hash `hash`: its slot in `slot` if so, and the empty
// slot it would take if not.
static bool check_program_emit_taken(const Link* link, const CheckProgramNames* names, const uint8_t* bytes, uint32_t length, uint64_t hash, uint32_t* slot)
{
    uint32_t probe = (uint32_t)hash & names->mask;
    bool taken = false;
    while (names->taken[probe] != 0 && !taken)
    {
        const NameId other = names->taken[probe];
        taken = link->names.hash[other] == hash && link->names.length[other] == length && memcmp(link->names.bytes[other], bytes, length) == 0;
        probe = taken ? probe : (probe + 1) & names->mask;
    }
    *slot = probe;
    return taken;
}

// Whether the compiler makes the name of `declaration`'s symbol (`renamed`: its asm label, if it has one): a file-scope
// compound literal's, and a block-scope static's without a label of its own.
static bool check_program_emit_makes_name(const CheckInput* input, uint32_t declaration, String8 renamed)
{
    return check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) ||
           (renamed.length == 0 && check_sparse_get(input->declaration_bodies, declaration) != BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, declaration) == 0);
}

// Adds `spelling`, the name the source gives a symbol, to the link's names, and to the names a made one must not be if
// it could be one: its NameId, or 0 when the link has no room. Every made name has a '.', which no identifier has, so
// only an asm label with one can be a made name, and only it goes in the table (#442: most units' given names are
// identifiers, whose probes the table then doesn't pay for).
static NameId check_program_emit_given_name(Link* link, CheckProgramNames* names, const uint8_t* spelling, uint32_t length, bool label)
{
    const uint64_t hash = link_hash(spelling, length);
    uint32_t slot = 0;
    const bool dotted = label && memchr(spelling, '.', length) != 0;
    const bool taken = dotted && check_program_emit_taken(link, names, spelling, length, hash, &slot);
    const NameId result = link_add_name_hashed(link, spelling, length, hash);
    if (dotted && !taken && result != 0)
    {
        names->taken[slot] = result;
    }
    return result;
}

// Adds `base`, or the first of its ".N" spellings no name of `names` has, to the link's names: its NameId, or 0 when the
// link or scratch has no room.
static NameId check_program_emit_made_name(Link* link, CheckProgramNames* names, const uint8_t* base, uint32_t base_length, Arena* scratch)
{
    // ".N" takes at most 11 bytes past the base, and snprintf a 12th for its NUL.
    uint8_t* candidate = base_length <= UINT32_MAX - 12 ? arena_push(scratch, (uint64_t)base_length + 12, 1) : 0;
    NameId result = 0;
    if (candidate != 0)
    {
        memcpy(candidate, base, base_length);
        uint32_t length = base_length;
        uint64_t hash = link_hash(candidate, length);
        uint32_t slot = 0;
        while (check_program_emit_taken(link, names, candidate, length, hash, &slot))
        {
            names->unique += 1;
            length = base_length + (uint32_t)snprintf((char*)candidate + base_length, 12, ".%u", names->unique);
            hash = link_hash(candidate, length);
        }
        result = link_add_name_hashed(link, candidate, length, hash);
        names->taken[slot] = result;
    }
    return result;
}

// The raw rows' room (CheckProgramRows): each body's entries, its prologue's and its epilogue's. A program whose
// selection kept no function has none, an empty set of rows (a unit of no functions to debug.c), not a failure. Only -g
// reaches it, out of line so that the plain emission carries none of it.
static __attribute__((noinline)) bool check_program_emit_reserve_rows(CheckProgramRows* rows, uint64_t entries, uint32_t count, Arena* output)
{
    const uint64_t capacity = entries + 2 * (uint64_t)count;
    const bool fits = capacity <= UINT32_MAX - BUSTER_CHECK_DEBUG_SLACK;
    *rows = (CheckProgramRows){ .capacity = fits ? (uint32_t)capacity : 0 };
    rows->offsets = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)rows->capacity + BUSTER_CHECK_DEBUG_SLACK) : 0;
    rows->tokens = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)rows->capacity + BUSTER_CHECK_DEBUG_SLACK) : 0;
    rows->first = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->counts = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->sizes = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->bodies = fits ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)count + 1) : 0;
    rows->values = fits ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDebugBody, (uint64_t)count + 1) : 0;
    return rows->offsets != 0 && rows->tokens != 0 && rows->first != 0 && rows->counts != 0 && rows->sizes != 0 && rows->bodies != 0 && rows->values != 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// The functions on lanes (#151)
//
// check_program_emit_lanes runs each function's lowering, Tier 0, branch sizing and encoding as a lane item, cut into
// parts of consecutive bodies by their tokens (lane_parts_by_cost) and taken costliest first (lane_take), each lane with
// a writer and arenas of its own; then one lane, the caller's, adds the functions' atoms, symbols and fixups to the link
// in source order, so that the link, and the object or executable made from it, don't depend on the lane count. A part
// takes the steps source order's emission takes for the whole program, in the same order: its bodies' lowering
// (check_lower_part), then each body's Tier 0 code and, while the part has no failure, its encoding. The failure
// reported is source order's: the first step's that fails, its first body's (check_lower_finish orders the lowering's,
// and the merge Tier 0's, then the link's and its bindings', then the encodings'). A part that fails stops, but past a
// failure after Tier 0 it goes on with Tier 0 alone, whose failures come first. With one lane its bodies are one part,
// and the emission takes source order's steps in source order's order.
//
// With several source files the units run on lanes (c_checker_units.c), each unit on one, where a unit's emission can't
// run lanes of its own: lane_run refuses a group inside the units' group. The units' lanes are a board instead (#777,
// CheckEmitBoard): a unit's emission that starts while one of them has no unit left to take cuts its bodies into parts
// as lanes of its own would (parts_by_cost), shares them on its lane's share and takes them itself, and the lanes that
// take no units take them too (check_program_emit_help), each with a writer and arenas of its own in that emission,
// until no lane takes units. The parts are the same steps, merged the same way, so the outputs are one lane's.

// The most lanes an emission runs.
#define BUSTER_CHECK_EMIT_MAX_LANES 64
// The bodies' tokens that pay for a lane past the first, beyond twice the largest body's (check_program_emit_lanes): a
// lane costs its thread's start, its writer's columns and its arenas' first pages, which its share of the bodies must pay
// for, and the lane that takes the largest body takes at least as long as it.
#define BUSTER_CHECK_EMIT_LANE_TOKENS 4096
// A lane's arenas, as CheckProgramLanes.squeeze_arenas names them (the tests').
#define BUSTER_CHECK_EMIT_SQUEEZE_OUTPUT 1u
#define BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH 2u
#define BUSTER_CHECK_EMIT_SQUEEZE_BYTES 4u

// Why an emission on lanes ran its bodies again on the calling thread alone (CheckProgramLanes.again): its lanes didn't
// start (lane_run, which counts a lane it couldn't pin as one), or a part ran out of room in its lowering (or its lane
// had none for its writer), in a body's Tier 0 code or in a body's code after Tier 0. One lane's arenas, the caller's,
// then decide whether the unit fits.
typedef enum CheckProgramAgain
{
    CHECK_PROGRAM_AGAIN_NONE,
    CHECK_PROGRAM_AGAIN_START,
    CHECK_PROGRAM_AGAIN_LOWERING,
    CHECK_PROGRAM_AGAIN_TIER0,
    CHECK_PROGRAM_AGAIN_CODE,
} CheckProgramAgain;

// A lane's share on the units' board (#777): the emission its unit shares, one at a time, each an epoch of the share.
// The epoch's next part to take (low 32 bits of `ticket`) and its parts' count (low 32 bits of `limit`), each beside the
// epoch (their high 32 bits): a take is a compare-and-swap of the ticket, whose epoch must be the limit's, so that a lane
// that read an epoch's ticket takes none of the next's. Its owner sets the epoch's `run` and `order` (its parts, the
// costliest first) before its ticket. Each part finishes every access to the run before incrementing `done`; the last
// publishes `complete` (lane_publish, lane_wait), which the owner acquires before its emission goes on or the share
// opens another epoch. The count alone is not that handoff: the last part may not have published complete yet.
typedef struct CheckEmitShare CheckEmitShare;
struct CheckEmitShare
{
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t ticket;
    uint64_t limit;
    uint32_t done;
    uint32_t complete;
    struct CheckEmitRun* run;
    const uint32_t* order;
    // The halves of its parts that the epoch's takers run (CheckEmitHalf): both, or with inlining (#1282) the lowering in
    // one epoch and the code in the next.
    uint32_t half;
};

// The units' lanes as one board (#777): how many there are, how many still take units (a lane that takes no more helps,
// check_program_emit_help, until none takes any), the word that advances (lane_advance) when a share opens and when a
// lane stops taking units, which the helping lanes wait on, the parts that lanes other than the owner's took (a test's
// count), and a share per lane.
typedef struct CheckEmitBoard CheckEmitBoard;
struct CheckEmitBoard
{
    uint32_t lanes;
    uint32_t taking;
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t generation;
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t others;
    CheckEmitShare shares[BUSTER_LANE_MAX_COUNT];
};

// A unit's emission in its command's flow (#905, the C driver's), a LaneFlow of the session's lanes: lane 0 runs the
// unit's stages alone, and each of its emissions that pays for more than one lane is a round of the bodies
// (BUSTER_CHECK_EMIT_ROUND_BODIES), which lane 0 announces with the emission's lanes (check_program_emit_round): the
// first of them, lane 0 among them, take the bodies' parts (check_program_emit_lane), and the others follow the rounds
// (check_program_emit_follow) until lane 0 ends the flow (lane_flow_end).
#define BUSTER_CHECK_EMIT_ROUND_BODIES 1u

// A team that grows while it runs (#673's tail): in a command's flow, an emission whose GNU make jobserver gave it fewer
// lanes than its bodies pay for, as when another recipe held the spare tokens as it started. Its parts are cut for every
// lane it may take (BUSTER_CHECK_EMIT_GROW_PARTS_PER_LANE each) and put in one order, the costliest first and the others
// in source order, and each part is two items, its lowering and its code (CheckEmitHalf). A take of `ticket` is an item
// of twice the parts: the parts' lowering in that order, then their code in it, each part's once its place's word in
// `lowered` says its lowering is done (check_program_emit_item). Lane 0 takes them alone, then in a round of all those
// lanes once it has admitted another (check_program_emit_growing), in which each lane past the admitted ones sleeps
// until lane 0 admits it (check_program_emit_admitted). Lowering every part before any part's code keeps each half's
// locality: on one lane, the parts' halves by turns cost sqlite3.c's -c 0.35% more user cycles with its bodies in 2 parts
// than in 1, 0.79% in 16 and 0.92% in 128, and the halves in this order none (#673). Between items, lane 0 reads whether
// the jobserver has spare tokens, at most every BUSTER_CHECK_EMIT_GROW_NANOSECONDS, and takes them under a lease of its
// own, up to BUSTER_CHECK_EMIT_GROW_LEASES in all, when the work left pays for it (check_program_emit_grow): `rest` holds
// the tokens of the parts from each place in the order on. `admission` is the lanes admitted, shifted left by two, with
// bit 1 set once lane 0 has no item left to take, so that the lanes still asleep leave (bit 0 is lane_sleep's).
typedef struct CheckEmitGrowth CheckEmitGrowth;
struct CheckEmitGrowth
{
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint64_t ticket;
    _Alignas(BUSTER_LANE_CACHE_LINE_BYTES) uint32_t admission;
    uint64_t parts;
    const uint32_t* order;
    uint64_t* rest;
    // Each place's word: 2 once its part's lowering is done (lane_publish), for the lane that takes its code (lane_wait).
    uint32_t* lowered;
};
// About 100 µs: a team grows that much later than the tokens it takes are spare, at worst, plus a part's time; and a team
// that never grows reads the clock once per part and the jobserver once per interval.
#define BUSTER_CHECK_EMIT_GROW_NANOSECONDS 100000ull
// The bodies' tokens whose emission on one lane takes about as long as a growth's guardian takes to start
// (worker_budget_grow: 161 to 164 µs on the 9700X, #937), at about 10 ns a token (sqlite3.c's on one lane).
#define BUSTER_CHECK_EMIT_GROW_TOKENS 16384u
#define BUSTER_CHECK_EMIT_GROW_LEASES 8u
// A growing team's parts per lane it may take, a quarter of a round's (BUSTER_LANE_PARTS_PER_LANE).
#define BUSTER_CHECK_EMIT_GROW_PARTS_PER_LANE 16u
// The lowering's share of a token's emission, in sixteenths, by which the growth gate weighs the parts not lowered yet
// against those not coded yet (check_program_emit_grow): sqlite3.c's on one lane on the 9700X, whose parts' lowering
// took 11.0 ms (--time-stages' lower.statements, lower.bodies and lower.validate) and their code 4.9 (tier0).
#define BUSTER_CHECK_EMIT_GROW_LOWER_SIXTEENTHS 11u
// The first of a test's supplied tokens (CheckProgramLanes.grow_supply).
#define BUSTER_CHECK_EMIT_SUPPLY_BYTE 0xa0u

// The C driver's --tier1 (c_check_tier1.c, #138's piece for #140): each body Tier 1's pipeline takes compiled by it,
// explicitly unsupported bodies by Tier 0; resource/internal failures stop emission. The lane owns each step's
// output and the steps' scratch, reserved at the
// lane's first body and emptied for each, and the forms the lane's last target allows (X86_FORM_COUNT entries).
#define BUSTER_CHECK_TIER1_STEPS 8
// --tier1-schedule's variants (#172): none, each block's records after allocation (c_check_tier1_schedule.c), or each
// block's IR instructions before selection and allocation (tier1_schedule_ir.c).
#define BUSTER_CHECK_TIER1_SCHEDULE_NONE 0
#define BUSTER_CHECK_TIER1_SCHEDULE_POST 1
#define BUSTER_CHECK_TIER1_SCHEDULE_PRE 2

typedef struct CheckTier1Lane CheckTier1Lane;
struct CheckTier1Lane
{
    Arena steps[BUSTER_CHECK_TIER1_STEPS];
    Arena scratch;
    Arena forms;
    uint8_t* allowed;
    X86Target target;
    bool reserved;
    // Whether the solver's arena (step 6) is among the reserved steps: --tier1-gvn's, only.
    bool gvn_reserved;
    bool targeted;
    // The emission's inline callees (#1282, #2541, c_check_tier1_inline.c), null for none; and the two arenas of the run's
    // lane state this lane takes the bodies on, where the summaries it builds live until the run ends: their functions'
    // and their analyses'.
    const struct CheckTier1Inline* inlining;
    Arena* summaries;
    // --tier1-schedule's variant (BUSTER_CHECK_TIER1_SCHEDULE_*, #172), which the emission sets for each body.
    uint8_t schedule;
};

// The unit's inline callees (#1282, check_tier1_inline_table): by symbol (of `symbol_count`), the index plus one of the body
// of each always_inline function of internal linkage, 0 for every other symbol (Tier 0's splice, #1398); the bodies' typed
// streams in `lowered`. With --tier1's inlining, its unit stage and summaries (#2541, c_check_tier1_inline.c).
typedef struct CheckTier1Inline CheckTier1Inline;
struct CheckTier1Inline
{
    const CheckProgram* program;
    const CheckProgramLowered* lowered;
    const uint32_t* bodies;
    // Per body, whether its part's lowering succeeded (set by check_program_emit_lower_half before any part's code): only
    // such a callee's typed stream is read.
    uint8_t* ready;
    uint32_t symbol_count;
    uint64_t generation;
    // Tier 1's candidates by symbol (a body index plus one), and per body its class as a callee, whether it is in a cycle
    // of the candidates' calls, its summary's state (a lane word) and its summary (c_check_tier1_inline.c's
    // CheckTier1Summary); the unit stage's word; the summaries' arenas, two per lane state of the run (the functions' and
    // their analyses'). All null without --tier1's inlining.
    const uint32_t* candidates;
    const uint8_t* classes;
    uint8_t* cyclic;
    uint32_t* states;
    struct CheckTier1Summary* summaries;
    uint32_t* stage;
    Arena* arenas;
    uint32_t body_count;
};

// What Tier 1's code of a body holds: its records, and the allocation's traffic, its spills' stores and reloads, the
// saves and restores around its calls and fixed registers' sequences, its recomputed values (constants and addresses,
// #178), the definitions of recomputed values it left out, which nothing read, and its edges' moves; its spill slots, and
// its frame's bytes (Tier1Frame.maximum_stack_bytes: what it takes below the return address, its saved registers among
// them).
typedef struct CheckTier1Counts CheckTier1Counts;
struct CheckTier1Counts
{
    uint32_t records;
    uint32_t spill_stores;
    uint32_t spill_loads;
    uint32_t call_stores;
    uint32_t call_loads;
    uint32_t rematerializations;
    uint32_t dropped_definitions;
    uint32_t edge_moves;
    uint32_t spill_slots;
    uint32_t frame_bytes;
    // Its unwind table's shape (#177): an open frame (UnwindFunction.open), its returns published as tails, and whether
    // its objects are in the red zone.
    uint32_t open_frames;
    uint32_t return_tails;
    uint32_t red_zones;
    // With --verify (#939), whether its code passed the certificate decoded from its records, or was left uncertified
    // for a shape the decoding doesn't take yet (Tier1EmitResult.code_reason).
    uint32_t code_certified;
    uint32_t code_uncertified;
    // Its machine rows (#141) and the moves around them into their roles' registers and out of them (#1283).
    uint32_t machine_rows;
    uint32_t row_moves;
    // The calls of always_inline functions it expanded (#1282, c_check_tier1_inline.c).
    uint32_t inlined_calls;
    // With --tier1-schedule (#172), the blocks scheduled and the records or IR instructions that changed places.
    uint32_t schedule_blocks;
    uint32_t schedule_moved;
};

#define BUSTER_CHECK_TIER1_OPERATIONS 256
typedef struct CheckTier1Report CheckTier1Report;
struct CheckTier1Report
{
    uint32_t functions;
    uint32_t requested;
    uint32_t admitted;
    uint32_t categories[CHECK_TIER1_CATEGORY_COUNT];
    uint32_t fallbacks;
    uint32_t stages[CHECK_TIER1_STAGE_COUNT];
    uint32_t operations[BUSTER_CHECK_TIER1_OPERATIONS];
    uint32_t emission[BUSTER_CHECK_TIER1_OPERATIONS];
    // The refused calls by kind (CheckTier1CallKind), a call counted under each kind it has (#1116).
    uint32_t calls[CHECK_TIER1_CALL_KIND_COUNT];
    // The counts of Tier 1's bodies, summed.
    uint64_t records;
    uint64_t spill_stores;
    uint64_t spill_loads;
    uint64_t call_stores;
    uint64_t call_loads;
    uint64_t rematerializations;
    uint64_t dropped_definitions;
    uint64_t edge_moves;
    uint64_t spill_slots;
    uint64_t frame_bytes;
    uint64_t open_frames;
    uint64_t return_tails;
    uint64_t red_zones;
    uint64_t code_certified;
    uint64_t code_uncertified;
    uint64_t machine_rows;
    uint64_t row_moves;
    uint64_t inlined_calls;
    uint64_t schedule_blocks;
    uint64_t schedule_moved;
};

// #1059's coverage of the operations a helper may implement, summed over a command's units (--helper-report): each
// family's direct forms in the lowered code (an atomic access's LOAD, STORE or SET with its marker, a builtin's or an
// implicit _Atomic object's; an atomic read/modify/write's ATOMIC_RMW; a fence's FENCE; an asm statement placed in its
// caller, its X86 entry), its helpers' call sites (a builtin's, an
// implicit _Atomic access's, a generic form's and an asm statement's alike: each call site once), the helpers planned,
// the units lowered, and those whose lowering refused an operation by name (CHECK_PROGRAM_UNSUPPORTED: a builtin or a
// statement no form takes, which -c and the direct path then refuse whole).
typedef struct CheckHelperReport CheckHelperReport;
struct CheckHelperReport
{
    uint32_t units;
    uint32_t refused;
    uint32_t direct[CHECK_PROGRAM_HELPER_FAMILIES];
    uint32_t helper[CHECK_PROGRAM_HELPER_FAMILIES];
    uint32_t planned[CHECK_PROGRAM_HELPER_FAMILIES];
};

// A unit's lowering into `report` (check_program_emit's, once the lowering has finished): where it lowered every body
// (`lowered_all`), its `count` bodies' direct forms and calls of each family's helpers, a call's target a helper's symbol
// (CheckProgramHelper.symbol: the program's symbols' count, plus one, plus its index), and its planned helpers; and
// otherwise a refusal where the lowering refused an operation by name.
static void check_helper_report_add(CheckHelperReport* report, const CheckProgram* program, const CheckProgramLowered* lowered, uint32_t count, bool lowered_all)
{
    report->units += 1;
    report->refused += !lowered_all && lowered->status == CHECK_PROGRAM_UNSUPPORTED ? 1 : 0;
    for (uint32_t b = 0; lowered_all && b < count; b += 1)
    {
        const Tier0Function* function = &lowered->functions[b];
        for (uint32_t e = 0; e < function->count; e += 1)
        {
            const uint8_t kind = function->kinds[e];
            const uint8_t class = function->classes[e];
            const uint64_t value = function->values[e];
            const bool access = (kind == BUSTER_TYPED_LOAD || kind == BUSTER_TYPED_STORE || kind == BUSTER_TYPED_SET) && class >= BUSTER_TYPED_CLASS_I8 &&
                                class <= BUSTER_TYPED_CLASS_U64 && (value & BUSTER_TYPED_ACCESS_ATOMIC) != 0;
            report->direct[CHECK_PROGRAM_HELPER_ATOMIC_ACCESS] += access;
            report->direct[CHECK_PROGRAM_HELPER_ATOMIC_RMW] += kind == BUSTER_TYPED_ATOMIC_RMW || kind == BUSTER_TYPED_ATOMIC_CAS;
            report->direct[CHECK_PROGRAM_HELPER_FENCE] += kind == BUSTER_TYPED_FENCE;
        }
    }
    // An asm statement placed in its caller (#1059, #1839): its X86 entry or its asm body, once per statement as its
    // helper's call would be.
    for (uint32_t s = 0; lowered_all && lowered->asms.plans != 0 && s < lowered->asms.plan_count; s += 1)
    {
        report->direct[CHECK_PROGRAM_HELPER_ASM] += lowered->asms.plans[s].planned && (lowered->asms.plans[s].row != 0 || lowered->asms.plans[s].inlined);
    }
    for (uint32_t h = 0; lowered_all && h < lowered->helpers.count; h += 1)
    {
        report->planned[check_program_helper_family(&lowered->helpers.items[h])] += 1;
    }
    const uint64_t first = (uint64_t)program->symbol_count + 1;
    for (uint32_t b = 0; lowered_all && b < count; b += 1)
    {
        const Tier0Function* function = &lowered->functions[b];
        for (uint32_t c = 0; c < function->call_count; c += 1)
        {
            const uint64_t helper = (uint64_t)function->call_targets[c] - first;
            if (helper < lowered->helpers.count)
            {
                report->helper[check_program_helper_family(&lowered->helpers.items[helper])] += 1;
            }
        }
    }
}

// --helper-forms' families (#1059's paired measurements, CheckProgramOptions.helper_forms): a comma-separated list of the
// families that have a direct form (CheckProgramHelperContract.direct), by --helper-report's names (fence,
// atomic-access, atomic-rmw, asm), or `all` of them, as a bit each; 0 for an empty list, an empty name, or a name of
// another family or of none.
static uint32_t check_helper_forms_parse(const char* text)
{
    uint32_t direct = 0;
    for (uint32_t f = 0; f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
    {
        direct |= check_program_helper_contracts[f].direct != NULL ? 1u << f : 0;
    }
    uint32_t forms = 0;
    bool valid = text[0] != 0;
    const char* at = text;
    while (valid && *at != 0)
    {
        const size_t length = strcspn(at, ",");
        uint32_t found = length == 3 && memcmp(at, "all", 3) == 0 ? direct : 0;
        for (uint32_t f = 0; found == 0 && f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
        {
            const char* name = check_program_helper_contracts[f].name;
            found = (direct >> f & 1u) != 0 && strlen(name) == length && memcmp(name, at, length) == 0 ? 1u << f : 0;
        }
        forms |= found;
        // A comma takes a name after it.
        const bool comma = at[length] == ',';
        valid = found != 0 && (!comma || at[length + 1] != 0);
        at += length + (comma ? 1 : 0);
    }
    return valid ? forms : 0;
}

// --helper-report's lines on `stream`: the totals, then a line per family.
static void check_helper_print_report(FILE* stream, const CheckHelperReport* report)
{
    uint32_t direct = 0;
    uint32_t helper = 0;
    uint32_t planned = 0;
    for (uint32_t f = 0; f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
    {
        direct += report->direct[f];
        helper += report->helper[f];
        planned += report->planned[f];
    }
    fprintf(stream, "helpers\tunits=%" PRIu32 "\trefused=%" PRIu32 "\tdirect=%" PRIu32 "\thelper=%" PRIu32 "\tplanned=%" PRIu32 "\n", report->units, report->refused, direct,
            helper, planned);
    for (uint32_t f = 0; f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
    {
        fprintf(stream, "helpers.%s\tdirect=%" PRIu32 "\thelper=%" PRIu32 "\tplanned=%" PRIu32 "\n", check_program_helper_contracts[f].name, report->direct[f],
                report->helper[f], report->planned[f]);
    }
}

// A lane's arenas past the first lane's, which are the caller's: its functions' lowered columns (`output`), its writer's
// columns, its functions' Tier 0 code and what their fixups and rows need until the merge (`scratch`), and its
// functions' bytes (`bytes`); and with --tier1, every lane's (the first's too) for Tier 1's pipeline.
typedef struct CheckProgramLaneArenas CheckProgramLaneArenas;
// A lane's cache of the callees Tier 0 inlines (#1398, c_check_tier0_inline.c): per body, its state and its expanded
// stream, in `cache`, for the emission's inline table's generation.
typedef struct CheckTier0InlineLane CheckTier0InlineLane;
struct CheckTier0InlineLane
{
    Arena cache;
    uint64_t generation;
    Tier0Function* functions;
    uint8_t* states;
    // Tarjan's search of the callees' graph (check_tier0_inline_prepare), per body: its visit's index plus one and its
    // low link, and once settled its height; the component stack, the search's stack of bodies and each one's next row,
    // and the visits so far.
    uint32_t* indices;
    uint32_t* links;
    uint32_t* heights;
    uint32_t* components;
    uint32_t* path;
    uint32_t* cursors;
    uint32_t visits;
};

struct CheckProgramLaneArenas
{
    Arena output;
    Arena scratch;
    Arena bytes;
    CheckTier1Lane tier1;
    CheckTier0InlineLane tier0_inline;
};

// How check_program_emit_lanes runs a unit's functions on lanes, and what it ran. The caller keeps it until it is done
// with the emission's link and lowering: the atoms' bytes and the functions' columns of the lanes past the first are in
// their arenas here (check_program_lanes_release), which the next emission with it reuses, as it reuses the first's
// scratch for lane 0's writer's columns (CheckEmitRun.columns) while it has their room.
typedef struct CheckProgramLanes CheckProgramLanes;
struct CheckProgramLanes
{
    // The most lanes to run (0 or 1 for one), and whether to run all of them, as long as there are bodies for them,
    // whatever the bodies' tokens (the tests'); the lanes' CPUs for lane_run, or 0.
    uint32_t lanes;
    bool every_lane;
    const uint32_t* cpus;
    // Optional external ownership for the extra workers. The C driver supplies it; isolated callers/tests may omit it.
    WorkerBudget* budget;
    // A test's lane that sleeps `slow_microseconds` after each part it takes, its index plus one (0 for none), so that the
    // other lanes take the parts it would have and finish theirs first; in a growing team (CheckEmitGrowth), before each
    // part's lowering it takes, so that the lanes that take that part's code wait for it.
    uint32_t slow_lane;
    uint32_t slow_microseconds;
    // A test's lane short of room: the lane that takes the part holding body `squeeze_body` (its index plus one, 0 for
    // none) has only `squeeze_room` bytes to spare in each of its arenas that `squeeze_arenas` names
    // (BUSTER_CHECK_EMIT_SQUEEZE_*), as if they were that full, while it makes its writer, if that is its first part, and
    // works on that part. What fails is a resource's, which runs the emission again on one lane, without the squeeze.
    uint32_t squeeze_body;
    uint32_t squeeze_arenas;
    uint64_t squeeze_room;
    // With several source files (#777), the units' lanes' board, on which the emission shares its parts with the lanes
    // that have no unit left to take rather than run lanes of its own, and its lane's place there; and a test's pause
    // after each part the sharing lane takes itself, in microseconds, so that the other lanes take parts. Their lanes'
    // states and arenas are their places plus one: the first is the sharing lane's, with the caller's arenas.
    CheckEmitBoard* board;
    uint32_t board_lane;
    uint32_t board_pause_microseconds;
    // In a command's flow (#905), its rounds' rendezvous: the emission's lanes are rounds of the flow's lanes rather
    // than a lane_run of its own. Null elsewhere.
    LaneFlow* flow;
    // In a command of several units' flow (#905, c_checker_units.c), the rounds of the units' run, the link's merge,
    // check and write. Null elsewhere.
    struct CheckUnitsFlow* units_flow;
    // In a units' run, the lane of the unit that these lanes' emission is (c_checker_units.c), whose unit takes its
    // estimated peak from the run's memory once preprocessed (#1109, check_units_admit). Null elsewhere.
    struct CheckUnitsLane* units_lane;
    // A command of one source file's attested declarations' state, where it keeps a memo across processes (#2531's step 4,
    // --attested-memo, check_driver_one_unit). Null elsewhere: a units' run's lanes have theirs.
    struct CheckAttested* attested;
    // A test's tokens that lane 0 of a growing team (CheckEmitGrowth) writes to the jobserver after its first item, as
    // another recipe returning them would, each byte BUSTER_CHECK_EMIT_SUPPLY_BYTE plus its index (0 for none).
    uint32_t grow_supply;
    // What the last emission ran: its lanes (a growing team's at its end) and parts, the lanes a growing team admitted
    // after it started, the lanes that took a part of the bodies that made its output (1 for the calling thread alone, a
    // serial retry's among them; on the units' board, the helping lanes too; 0 without bodies), the lane that took the
    // squeezed part (its index plus one, 0 for none), and whether it ran them again on one lane and why; with `timing`, how long its steps took:
    // from its start to the lanes' (the lowering's preparation and the access column), the lanes' run (with their
    // threads' start), and from the lanes' end to its own (the merge), in nanoseconds (the benchmark's).
    uint32_t active;
    uint32_t grown;
    uint32_t worked;
    uint32_t parts;
    uint32_t squeezed;
    CheckProgramAgain again;
    bool timing;
    uint64_t prepare_ns;
    uint64_t lanes_ns;
    uint64_t merge_ns;
    // --tier1, and what became of the emissions' bodies, summed over them; with --tier1-refusals, the stream that each
    // body Tier 0 compiled gets a line on, its name and the step that refused it (check_tier1_print_refusal). With
    // --tier1-no-rematerialize, Tier 1's allocation spills what it would recompute (#178's comparison). With
    // --tier1-no-lifetimes, the checked program gives its objects no lifetimes, and Tier 1's frame shares no object's slot
    // (#177's comparison, c_check_program_lifetime.c).
    bool tier1;
    bool tier1_strict;
    const CheckTier1Fault* tier1_fault;
    // --tier1-gvn runs #866's congruence solver on the lowered IR, before promotion: a measurement option, off by
    // default, because the class representatives it merges lengthen live ranges that Tier 1's allocation pays for.
    bool tier1_gvn;
    bool tier1_no_rematerialize;
    bool tier1_no_lifetimes;
    // --tier1-no-inline keeps every call of the unit's always_inline functions (#1282's comparison).
    bool tier1_no_inline;
    // --tier0-no-inline keeps Tier 0's (#1398's comparison, c_check_tier0_inline.c).
    bool tier0_no_inline;
    // --tier1-schedule's variant (BUSTER_CHECK_TIER1_SCHEDULE_*, #172's measurement): none by default.
    uint8_t tier1_schedule;
    CheckTier1Report tier1_report;
    FILE* tier1_refusals;
    FILE* tier1_function_sizes;
    // Every emission's plan of its helpers' operations, summed (#1059, --helper-report), where the command asks for it
    // (`helper_reporting`): its direct forms are a pass over every lowered entry, which on sqlite3.c cost every compile
    // about a millisecond of the lowering's stage (#1048).
    bool helper_reporting;
    CheckHelperReport helper_report;
    // --helper-forms' families (CheckProgramOptions.helper_forms): each one's operations take their helpers.
    uint32_t helper_forms;
    // How many of the lanes' arenas, from lane 0's, an emission may have reserved (check_program_lanes_use): the ones
    // check_program_lanes_release releases. Reading the others' would touch every page of the 98 KB they take, which a
    // driver keeps in .bss so that a compile touches only those it uses (#652).
    uint32_t arenas_used;
    CheckProgramLaneArenas arenas[BUSTER_CHECK_EMIT_MAX_LANES];
};

// Whether --tier1's tables are built (check_program_emit_timed), once per process.
static bool check_tier1_ready = false;

// Defined in c_check_tier1.c, after Tier 1's modules: a body's code by Tier 1's pipeline on `lane`'s arenas, from its
// typed stream `function` whose result has the typed class `result_class` (check_program_result_class), its records,
// branch columns, references, frame events and with `sources` -g's source events in `output`, into *code (with
// `scalar` each step's twin). Returns an explicit success, unsupported, resource or internal outcome. With `certify`
// (--verify), each allocation must pass its certificate (#939, Tier1EmitOptions.certify): a refusal is an internal
// failure, and the checker's capacity a resource failure. A failed attempt leaves output unchanged and clears
// code/counts, retaining its cause and location by value.
static CheckTier1Outcome check_tier1_compile(CheckTier1Lane* lane, const Tier0Function* function, uint8_t result_class, const X86Target* target, bool scalar,
                                             bool rematerialize, bool gvn, bool certify, bool sources, Arena* output, EmittedCode* code,
                                             CheckTier1Counts* counts, const CheckTier1Fault* fault);
// Releases a lane's arenas for the pipeline.
static void check_tier1_lane_release(CheckTier1Lane* lane);
// Defined in c_check_tier1_inline.c (#1282): the unit's inline callees by symbol, in `arena`, or false for none.
static bool check_tier1_inline_table(const CheckProgram* program, const CheckProgramLowered* lowered, uint32_t symbol_count, bool tier1, Arena* arena, CheckTier1Inline* table);
static void check_tier1_inline_release(CheckTier1Inline* table);
// Gives `table` a new generation, which no lane's cache is valid for: its bodies are lowered again.
static void check_tier1_inline_renew(CheckTier1Inline* table);
static uint32_t check_tier1_operation(CheckTier1Outcome outcome);
// Prints `report`, a line on `stream`: how many functions each tier compiled, and why Tier 1 refused the others.
static void check_tier1_print_report(FILE* stream, const CheckTier1Report* report);
// Prints a line on `stream` for a function Tier 1 refused: its name, `length` bytes at `name`, and its outcome's step and
// IR operation or error.
static void check_tier1_print_refusal(FILE* stream, const uint8_t* name, uint32_t length, CheckTier1Outcome outcome);

// CLOCK_MONOTONIC, for CheckProgramLanes.timing.
static uint64_t check_program_lanes_now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

// Notes that an emission may reserve the arenas of the lanes below `count` (CheckProgramLanes.arenas_used): on lane 0,
// before the other lanes run.
static void check_program_lanes_use(CheckProgramLanes* lanes, uint32_t count)
{
    if (lanes != NULL && lanes->arenas_used < count)
    {
        lanes->arenas_used = count < BUSTER_CHECK_EMIT_MAX_LANES ? count : BUSTER_CHECK_EMIT_MAX_LANES;
    }
}

// Releases the lanes' arenas: after the caller is done with the last emission's link. Lane 0's writer's columns too, which
// a settle (check_program_lanes_settle) kept past the lanes it marked unused.
static void check_program_lanes_release(CheckProgramLanes* lanes)
{
    for (uint32_t lane = 0; lanes != NULL && lane < lanes->arenas_used; lane += 1)
    {
        arena_release(&lanes->arenas[lane].output);
        arena_release(&lanes->arenas[lane].scratch);
        arena_release(&lanes->arenas[lane].bytes);
        check_tier1_lane_release(&lanes->arenas[lane].tier1);
        arena_release(&lanes->arenas[lane].tier0_inline.cache);
        lanes->arenas[lane].tier0_inline = (CheckTier0InlineLane){ 0 };
    }
    if (lanes != NULL)
    {
        arena_release(&lanes->arenas[0].scratch);
        lanes->arenas_used = 0;
    }
}

// check_program_lanes_release but for lane 0's writer's columns (the first lanes' scratch), which the next emission with
// these lanes takes while it has their room, as it does within a CheckProgramLanes' life: a units' lane's, from one unit
// to the next (c_checker_units.c, #442), as the lane keeps its own arenas. No emission reads a column's bytes before it
// writes them, so the pages stay as the last one left them.
static void check_program_lanes_settle(CheckProgramLanes* lanes)
{
    const Arena columns = lanes->arenas[0].scratch;
    lanes->arenas[0].scratch = (Arena){ 0 };
    check_program_lanes_release(lanes);
    lanes->arenas[0].scratch = columns;
}

// Lane 0's writer's columns without lanes (check_program_emit's): the calling thread's, reserved for the room they take
// and kept while it has that room, as a CheckProgramLanes keeps its first lane's scratch, so that an emission after the
// thread's first maps, protects and faults in none of their pages again (#442). Never released: the thread's largest
// unit's columns, of which only the pages its bodies touched are resident.
static _Thread_local Arena check_program_thread_columns;
// Lane 0's cache of Tier 0's expanded inline callees without lanes (#1398, c_check_tier0_inline.c): the calling thread's,
// as its columns are, so that an emission without lanes inlines what one with them does and makes the same object.
// Its pages are popped by the next emission's first use; never released.
static _Thread_local CheckTier0InlineLane check_program_thread_inline;

// A body's code, as its lane made it (check_program_emit_code): its bytes, encoded after branch sizing; each marked
// instruction's end (x86_encode's fixup ends) and each label's offset (or UINT32_MAX for a record past the code), and a
// naked function's, each of its templates' labels' (CheckProgramNaked.labels), for the merge's link; with -g its raw
// rows, with room for its entries, its prologue's, its epilogue's and the slack; and its failure after Tier 0, as source
// order's emission reports it (CHECK_PROGRAM_OK for none): at its declaration but for a resource's.
typedef struct CheckEmitBody CheckEmitBody;
struct CheckEmitBody
{
    uint8_t* bytes;
    uint64_t length;
    CheckDebugValueSpan* debug_spans;
    uint32_t debug_span_count;
    CheckDebugLocation* debug_locations;
    uint32_t* debug_location_first;
    uint32_t debug_location_count;
    uint32_t* fixup_ends;
    uint32_t* label_offsets;
    uint32_t* asm_offsets;
    uint32_t* row_offsets;
    uint32_t* row_tokens;
    uint32_t row_count;
    uint32_t body_offset;
    CheckProgramStatus status;
    uint32_t token;
    const char* reason;
    // Only a failed frame verification retains its verdict, until the calling thread reports it in source order.
    const EmittedVerdict* frame_verdict;
    // patchable_function_entry's NOPs (#1447, check_program_emit_patchable): `sled` bytes at the start of `bytes`, `prefix`
    // of them before the function's symbol; 0 for a function without them, the usual one.
    uint32_t sled;
    uint32_t prefix;
};

// A part's steps (CheckLowerPart, the lowering's), and its first body whose backend failed (either tier) and whose code
// failed after Tier 0, its end for none.
typedef struct CheckEmitPart CheckEmitPart;
struct CheckEmitPart
{
    CheckLowerPart lower;
    uint32_t tier0_failed;
    uint32_t code_failed;
};

// A lane's writer of its parts' lowering (check_lower_writer_begin's), made when it takes its first part, and its arenas:
// the caller's for lane 0, CheckProgramLanes.arenas' for the others.
typedef struct CheckEmitLane CheckEmitLane;
struct CheckEmitLane
{
    CheckLowerWriter writer;
    CheckLowerAbi abi;
    CheckProgramLowered status;
    Arena* output;
    Arena* scratch;
    Arena* bytes;
    // The arena of its writer's columns (check_lower_writer_begin): its scratch, but lane 0's (CheckEmitRun.columns).
    Arena* columns;
    // With --tier1, its arenas for Tier 1's pipeline (CheckProgramLaneArenas.tier1); null without.
    CheckTier1Lane* tier1;
    // With Tier 0's inlining (CheckEmitRun.tier0_inline), its cache of expanded callees; null without.
    CheckTier0InlineLane* tier0_inline;
    bool prepared;
    bool ready;
};

// What every lane of an emission reads (`program` to `frames`, and the parts' cut), and what each fills: the bodies'
// Tier0Functions (CheckProgramLowered.functions), their codes and their frames, each body's by the lane that takes it,
// and each part's result.
typedef struct CheckEmitRun CheckEmitRun;
struct CheckEmitRun
{
    const CheckProgram* program;
    const Parser* parser;
    const CheckLowerShared* shared;
    CheckProgramLowered* lowered;
    CheckProgramLanes* lanes;
    const uint8_t* access;
    uint32_t access_count;
    uint32_t body_count;
    bool scalar;
    bool verify;
    bool unwind;
    bool sources;
    Arena* output;
    Arena* scratch;
    Arena* bytes;
    // Lane 0's writer's columns, in base pages, as the other lanes' scratch is: each column has room for any of the
    // program's bodies, of which a body touches a little at the column's start, and in the caller's scratch, whose pages
    // past its first 64 KB are huge, each column's first touch cleared a huge page of its own (#531). The caller's
    // scratch where no address space can be reserved for them (check_program_emit_lanes).
    Arena* columns;
    // Each body's emitted code (emitted_code.c's EmittedCode): Tier 0's, adapted, or Tier 1's.
    EmittedCode* codes;
    CheckEmitBody* bodies;
    UnwindFunction* frames;
    // With --tier1, each body's complete outcome and its published code's counts (zero on refusal).
    CheckTier1Outcome* tier1_outcomes;
    CheckTier1Counts* tier1_counts;
    uint64_t* costs;
    uint64_t* boundaries;
    uint32_t* orders[BUSTER_CHECK_EMIT_MAX_LANES];
    CheckEmitPart* parts;
    uint32_t part_count;
    CheckEmitLane* lane_states;
    // A growing team's (CheckEmitGrowth), which its round's lanes take the parts by; null for any other run.
    CheckEmitGrowth* growth;
    // With --tier1, the unit's inline callees (#1282, check_tier1_inline_table), or null for none. With them every part is
    // lowered before any part's code, so that a body's lane reads any callee's typed stream: each team's lanes take the
    // parts' lowering, meet, and take their code (check_program_emit_lane, check_program_emit_item,
    // check_program_emit_shared).
    const CheckTier1Inline* tier1_inline;
    // The unit's inline callees for Tier 0's splice (#1398, c_check_tier0_inline.c), unless --tier0-no-inline; and the
    // table either tier reads, which makes the emission's two epochs (a lowering for every part, then their code): null
    // where neither inlines.
    const CheckTier1Inline* tier0_inline;
    const CheckTier1Inline* inline_table;
    // The driver's --time-stages clock (c_check_clock.c), which the calling thread's laps charge per function when it
    // compiles the bodies alone (check_program_emit_alone): null on the lanes, whose run its caller's mark charges.
    CheckClock* stage_clock;
};

// The typed class of body `b`'s result, as Tier 1's lowering takes it: its RETURNs' class, or for a function without one
// (a void function's, or one that never returns) its declared result's, UINT8_MAX for none (a naked function's).
static uint8_t check_program_result_class(const Tier0Function* function, const CheckProgram* program, uint32_t b, bool naked)
{
    uint8_t class = UINT8_MAX;
    for (uint32_t i = 0; class == UINT8_MAX && i < function->count; i += 1)
    {
        class = function->kinds[i] == BUSTER_TYPED_RETURN ? function->classes[i] : class;
    }
    if (class == UINT8_MAX && !naked)
    {
        const TypeTable* types = program->types;
        const TypeId result = type_return(types, program->input->declared_types[program->bodies[b].declaration]);
        class = check_lower_type_class(types, result);
        class = class != UINT8_MAX ? class : type_size(types, result) == 0 ? BUSTER_TYPED_CLASS_VOID : BUSTER_TYPED_CLASS_U64;
    }
    return class;
}

// Body `b`'s raw source rows (c_check_debug.c), from its code's source events (EmittedCode.source_records and
// source_origins) and where each one's record starts now that the branches are sized: the record count's offset is the
// end. Only -g reaches it, out of line so that the plain emission carries none of it.
static __attribute__((noinline)) bool check_program_emit_body_rows(CheckEmitBody* body, const CheckProgram* program, const Parser* parser,
                                                                   const uint32_t* tokens, const EmittedCode* code, uint32_t b, uint32_t* offsets, uint32_t length,
                                                                   bool scalar, Arena* scratch)
{
    offsets[code->count] = length;
    const uint32_t declaration = program->bodies[b].declaration;
    const uint32_t closing = parser->tree.parents[program->bodies[b].opening];
    const uint32_t* records = code->source_records;
    const uint32_t* origins = code->source_origins;
    body->row_count = BUSTER_SCALAR_PATH(scalar) ? check_debug_rows_scalar(records, origins, tokens, code->source_count, offsets, length, declaration, closing,
                                                       body->row_offsets, body->row_tokens) :
                               check_debug_rows(records, origins, tokens, code->source_count, offsets, length, declaration, closing, body->row_offsets,
                                                body->row_tokens);
    body->body_offset = offsets[records[0]];
    EmissionSourceEvent* events = BUSTER_ARENA_PUSH_ARRAY(scratch, EmissionSourceEvent, (uint64_t)code->source_count + 1);
    EmissionSourceRow* resolved = BUSTER_ARENA_PUSH_ARRAY(scratch, EmissionSourceRow, (uint64_t)code->source_count + 1);
    bool valid = events != 0 && resolved != 0;
    body->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
    body->reason = valid ? 0 : "no room for the source metadata protocol's rows";
    body->token = valid ? body->token : declaration;
    if (valid)
    {
        valid = check_debug_rows_protocol(records, origins, tokens, code->source_count, offsets, code->count, length, declaration, closing, body->row_offsets,
                                          body->row_tokens, body->row_count, scalar, events, resolved);
        body->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
        body->reason = valid ? 0 : "internal compiler error: source metadata disagrees with the raw rows";
        body->token = valid ? body->token : declaration;
    }
    return valid;
}
#endif
// A function's unwind descriptor, `frame`, from its emitted code's frame events after branch sizing (`offsets`, which
// gets the length at the record count): resolved through the final layout (emitted_unwind) into `rows` (frame_count
// room, which the tables read once the lanes are done). With `verify`, the events are proved against the records and
// their control flow too (emitted_verify_frames: but an assembly template's code, the user's, a function an asm
// statement's template returns from, and a rule a form
// x86_semantics.c doesn't cover leaves unproven), and where Tier 0's fixed-frame analyzer takes the records
// (unwind_tier0, which reads them alone), its frame must be the same CFI. Code past 2 GB is unsupported, not an
// internal error; no room is a resource failure,
// which the lanes retry on one. Returns the status of a failure (CHECK_PROGRAM_OK for none) and its reason. Where
// provided, failed receives a rejected verifier verdict for the calling thread's diagnostic, never printed on a lane.
static CheckProgramStatus check_program_emit_frame_checked(const EmittedCode* code, uint32_t* offsets, int64_t length, bool verify, bool scalar, EmissionFrameRow* rows,
                                                           Arena* scratch, UnwindFunction* frame, const char** reason, EmittedVerdict* failed)
{
    if (failed != NULL)
    {
        *failed = (EmittedVerdict){ 0 };
    }
    const char* const mismatch = "internal compiler error: a function's prologue or epilogue isn't the frame its unwind table describes";
    const uint32_t frame_status = code->frame_status;
    CheckProgramStatus status = frame_status == EMITTED_FRAME_MEMORY ? CHECK_PROGRAM_RESOURCE :
                                length > INT32_MAX ? CHECK_PROGRAM_UNSUPPORTED :
                                frame_status == EMITTED_FRAME_UNDESCRIBED ? CHECK_PROGRAM_BACKEND : CHECK_PROGRAM_OK;
    *reason = status == CHECK_PROGRAM_RESOURCE ? "no room for a function's frame events" :
              status == CHECK_PROGRAM_UNSUPPORTED ? "a function of 2 GB of code or more, past its unwind table's offsets" :
              status != CHECK_PROGRAM_OK ? mismatch : 0;
    if (status == CHECK_PROGRAM_OK)
    {
        offsets[code->count] = (uint32_t)length;
        const UnwindError error = emitted_unwind(code, offsets, (uint32_t)length, scalar, rows, frame);
        status = error == UNWIND_OK ? status : CHECK_PROGRAM_BACKEND;
        *reason = error == UNWIND_OK ? 0 : "internal compiler error: a function's frame events don't resolve to an unwind table";
    }
    if (status == CHECK_PROGRAM_OK && verify && !code->assembly)
    {
        // A frame the verifier can't prove past a form x86_semantics.c doesn't cover (an x86 builtin's leaf of one)
        // stands, as Tier 0's analyzer below leaves it.
        const EmittedVerdict verdict = emitted_verify_frames(code, scratch);
        const bool stands = verdict.error == EMITTED_VERIFY_OK || verdict.error == EMITTED_VERIFY_UNPROVEN;
        if (!stands && failed != NULL)
        {
            *failed = verdict;
        }
        status = stands ? status : verdict.error == EMITTED_VERIFY_MEMORY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
        *reason = stands ? 0 : verdict.error == EMITTED_VERIFY_MEMORY ? "no room to verify a function's frame events" :
                                                                        "internal compiler error: a function's frame events aren't its code's";
    }
    if (status == CHECK_PROGRAM_OK && verify && !code->assembly)
    {
        // Tier 0's analyzer reads the records and the branch columns alone, whichever generator made them.
        const Tier0Code records = {
            .records = code->records,
            .count = code->count,
            .branches = code->branches,
            .targets = code->targets,
            .branch_count = code->branch_count,
            .error = TIER0_OK,
        };
        UnwindFunction proven;
        const UnwindError analyzed = scalar ? unwind_tier0_scalar(&records, offsets, (uint32_t)length, &proven) : unwind_tier0(&records, offsets, (uint32_t)length, &proven);
        const EmittedSame same = analyzed != UNWIND_OK ? EMITTED_SAME : emitted_unwind_same(&proven, frame, scratch);
        status = same == EMITTED_SAME ? status : same == EMITTED_SAME_MEMORY ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
        *reason = same == EMITTED_SAME ? 0 : same == EMITTED_SAME_MEMORY ? "no room to compare a function's unwind table with Tier 0's analyzer's" : mismatch;
    }
    return status;
}

// The status-only entry point used by the frame checks; compilation retains failures for source-order reporting.
static __attribute__((unused)) CheckProgramStatus check_program_emit_frame(const EmittedCode* code, uint32_t* offsets, int64_t length, bool verify, bool scalar,
                                                                           EmissionFrameRow* rows, Arena* scratch, UnwindFunction* frame, const char** reason)
{
    return check_program_emit_frame_checked(code, offsets, length, verify, scalar, rows, scratch, frame, reason, NULL);
}
#if !defined(BUSTER_UNIT) || BUSTER_SOURCE_CONSUMER
// A bounded diagnostic for the selected failure only, after the lanes have joined. Numeric form and raw operand
// fields identify the exact record; the resolved width is what the frame verifier uses to clobber saved slots.
static void check_program_emit_frame_record(const EmittedCode* code, uint32_t body, uint32_t index)
{
    const X86Instruction* record = &code->records[index];
    const X86Effects* effects = x86_effects(record->form);
    const X86FormDescription* description = x86_form_description(record->form);
    int64_t target = -1;
    for (uint32_t k = 0; k < code->branch_count; k += 1)
    {
        target = code->branches[k] == index ? code->targets[k] : target;
    }
    fprintf(stderr, "frame-record: body=%u record=%u form=%u mnemonic=%s reg=%u rm=%u index=%u mode=%u vvvv=%u decorations=%u immediate=%d displacement=%d width=%u target=%" PRId64 "\n",
            body, index, record->form, description->mnemonic, record->reg, record->rm, record->index, record->mode, record->vvvv, record->decorations,
            record->immediate, record->displacement, effects->width, target);
}

static void check_program_emit_frame_failure(const EmittedCode* code, uint32_t body, const EmittedVerdict* failed)
{
    static const char* const rules[] = { "none", "cfa", "same", "offset", "register" };
    const uint32_t count = code->count;
    const uint32_t frames = code->frame_count;
    fprintf(stderr, "frame-verifier: body=%u error=%u record=%u/%u event=%u/%u rule=%s reg=%u holder=%u expected=%" PRId64 " actual=%" PRId64 " actual_unknown=%u\n",
            body, failed->error, failed->record, count, failed->event, frames, rules[failed->rule], failed->reg, failed->holder, failed->expected, failed->actual,
            failed->actual == BUSTER_EMITTED_UNKNOWN);
    const uint32_t prologue = count < 12 ? count : 12;
    for (uint32_t i = 0; i < prologue; i += 1)
    {
        check_program_emit_frame_record(code, body, i);
    }
    const uint32_t at = failed->record < count ? failed->record : count;
    const uint32_t first = at > 4 ? at - 4 : 0;
    const uint32_t end = at < count ? at + 1 : count;
    for (uint32_t i = first; i < end; i += 1)
    {
        if (i >= prologue)
        {
            check_program_emit_frame_record(code, body, i);
        }
    }
    const uint32_t event = failed->event < frames ? failed->event : 0;
    const uint32_t event_first = event > 2 ? event - 2 : 0;
    for (uint32_t k = event_first; k < frames && k - event_first < 5; k += 1)
    {
        const EmissionFrameEvent* frame = &code->frames[k];
        fprintf(stderr, "frame-event: body=%u event=%u boundary=%u side=%u kind=%u reg=%u value=%" PRId64 "\n",
                body, k, frame->at.boundary, frame->at.side, frame->kind, frame->reg, frame->value);
    }
}

static uint64_t check_program_emit_marked(const EmittedCode* code);

// `count` bytes of NOPs at `bytes`, as LLVM's X86 printer writes a patchable function entry's (emitX86Nops, for a CPU whose
// NOPs decode fast up to 15 bytes, every x86-64 tuning but a few Atoms'): the longest it can, of 15 bytes at most, then the
// rest. Each is nopl or nopw of rax with LLVM's displacements (8, then 512), past 9 bytes a cs override and up to five
// 66 prefixes before it; 2 bytes are xchg ax, ax.
static void check_program_emit_nops(uint8_t* bytes, uint32_t count)
{
    static const uint8_t nops[11][10] = {
        { 0 },
        { 0x90 },
        { 0x66, 0x90 },
        { 0x0f, 0x1f, 0x00 },
        { 0x0f, 0x1f, 0x40, 0x08 },
        { 0x0f, 0x1f, 0x44, 0x00, 0x08 },
        { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x08 },
        { 0x0f, 0x1f, 0x80, 0x00, 0x02, 0x00, 0x00 },
        { 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 },
        { 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 },
        { 0x2e, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00 },
    };
    uint32_t at = 0;
    while (at < count)
    {
        const uint32_t size = count - at < 15 ? count - at : 15;
        const uint32_t prefixes = size > 10 ? size - 10 : 0;
        const uint32_t base = size - prefixes;
        memset(bytes + at, 0x66, prefixes);
        memcpy(bytes + at + prefixes, nops[base], base);
        at += size;
    }
}

// Body `b`'s patchable_function_entry NOPs (#1447, check_program_patchable), once its code is encoded at `bytes`
// (`length` of them, with `sled` bytes of room after them): the code moved past the sled, whose first `prefix` bytes are
// one-byte NOPs before the function's symbol, as LLVM's emitNops writes them, and the rest the longest NOPs at its entry,
// before its prologue (check_program_emit_nops). What refers into the code moves with it: its fixups' fields, its labels
// and its templates' (from the atom's start, by the sled); its source rows and value spans (from the symbol, by the entry's
// NOPs, the declaration's row at 0 left there, so that its line covers them) and its unwind descriptor's transitions and size,
// whose entry rules (a row at 0) hold through the NOPs, as clang's FDE starts at the symbol. Out of line and cold: a unit
// without the attribute or -fpatchable-function-entry never reaches it.
static __attribute__((noinline, cold)) void check_program_emit_patchable(CheckEmitRun* run, CheckEmitBody* body, const EmittedCode* code, uint32_t b, uint8_t* bytes,
                                                                        uint32_t length, uint32_t sled, uint32_t prefix, EmissionFrameRow* frame_rows)
{
    const uint32_t entry = sled - prefix;
    memmove(bytes + sled, bytes, length);
    memset(bytes, 0x90, prefix);
    check_program_emit_nops(bytes + prefix, entry);
    const uint64_t marked = check_program_emit_marked(code);
    for (uint64_t k = 0; k < marked; k += 1)
    {
        body->fixup_ends[k] += sled;
    }
    // (A label past the code, UINT32_MAX, stays the failure it is.)
    for (uint32_t k = 0; k < code->label_count; k += 1)
    {
        body->label_offsets[k] += body->label_offsets[k] != UINT32_MAX ? sled : 0;
    }
    const CheckProgramNaked* naked = run->lowered->asms.naked != 0 && run->lowered->asms.naked[b].planned ? run->lowered->asms.naked + b : 0;
    const uint32_t asm_label_count = naked != 0 ? naked->label_count : code->asm_label_count;
    for (uint32_t k = 0; k < asm_label_count; k += 1)
    {
        body->asm_offsets[k] += body->asm_offsets[k] != UINT32_MAX ? sled : 0;
    }
    // Its rows from the symbol: the first, the declaration's, stays at 0 over the entry's NOPs, and the others move past
    // them, as the body's start does. A body without a prologue, whose first entry's row took the declaration's at 0
    // (check_debug_rows), gets that one back first, so that its body starts at the end of the NOPs, as clang's line table
    // has it: one row more than it had, which the rows' room has (the declaration's).
    const bool bare = run->sources && entry != 0 && body->body_offset == 0 && body->row_count != 0 && body->row_offsets[0] == 0;
    if (bare)
    {
        memmove(body->row_offsets + 1, body->row_offsets, (uint64_t)body->row_count * sizeof(uint32_t));
        memmove(body->row_tokens + 1, body->row_tokens, (uint64_t)body->row_count * sizeof(uint32_t));
        body->row_offsets[0] = 0;
        body->row_tokens[0] = run->program->bodies[b].declaration;
        body->row_count += 1;
    }
    for (uint32_t r = 1; run->sources && r < body->row_count; r += 1)
    {
        body->row_offsets[r] += entry;
    }
    body->body_offset += entry;
    for (uint32_t s = 0; run->sources && s < body->debug_span_count; s += 1)
    {
        body->debug_spans[s].begin += body->debug_spans[s].begin != 0 ? entry : 0;
        body->debug_spans[s].end += entry;
    }
    for (uint32_t r = 0; run->sources && r < body->debug_location_count; r += 1)
    {
        body->debug_locations[r].begin += entry;
        body->debug_locations[r].end += entry;
    }
    if (run->unwind)
    {
        UnwindFunction* frame = &run->frames[b];
        frame->size += entry;
        for (uint32_t k = 0; frame->open != BUSTER_UNWIND_ROWS && k < frame->event_count; k += 1)
        {
            frame->offsets[k] += entry;
        }
        for (uint32_t r = 0; frame->open == BUSTER_UNWIND_ROWS && frame_rows != 0 && r < frame->row_count; r += 1)
        {
            frame_rows[r].offset += frame_rows[r].offset != 0 ? entry : 0;
        }
    }
    body->sled = sled;
    body->prefix = prefix;
}

// Resolve optional optimized locations after branch sizing, then group by stable source local in one count/place/fill
// pass. The input remains record-relative; only these retained bytes acquire patchable-entry adjustments. All arrays
// are lane-owned and disjoint, and their counts/pointers remain unchanged across these bounded scans.
static CheckProgramStatus check_program_emit_locations(CheckEmitBody* body, const EmittedCode* code, uint32_t locals, const uint32_t* offsets, Arena* scratch)
{
    const EmittedLocalRange* ranges = code->local_ranges;
    const uint32_t count = code->local_range_count;
    CheckProgramStatus status = count == 0 || ranges != NULL ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    if (status == CHECK_PROGRAM_OK && count != 0)
    {
        uint32_t* first = body->debug_location_first;
        CheckDebugLocation* locations = body->debug_locations;
        uint32_t* next = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)locals + 1);
        status = first != NULL && locations != NULL && next != NULL ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
        if (status == CHECK_PROGRAM_OK)
        {
            memset(first, 0, ((uint64_t)locals + 1) * sizeof(uint32_t));
            for (uint32_t r = 0; status == CHECK_PROGRAM_OK && r < count; r += 1)
            {
                const EmittedLocalRange range = ranges[r];
                const bool valid = range.local < locals && range.begin < range.end && range.end <= code->count && range.value_bytes <= 8;
                status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
                if (valid && offsets[range.begin] < offsets[range.end])
                {
                    first[range.local + 1] += 1;
                }
            }
            for (uint32_t local = 0; status == CHECK_PROGRAM_OK && local < locals; local += 1)
            {
                first[local + 1] += first[local];
                next[local] = first[local];
            }
            for (uint32_t r = 0; status == CHECK_PROGRAM_OK && r < count; r += 1)
            {
                const EmittedLocalRange range = ranges[r];
                const uint32_t begin = offsets[range.begin];
                const uint32_t end = offsets[range.end];
                if (begin < end)
                {
                    const uint32_t index = next[range.local];
                    const bool ordered = index == first[range.local] || locations[index - 1].end <= begin;
                    status = ordered ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
                    if (ordered)
                    {
                        locations[index] = (CheckDebugLocation){
                            .home = range.home,
                            .value = range.value,
                            .begin = begin,
                            .end = end,
                            .value_bytes = range.value_bytes,
                        };
                        next[range.local] = index + 1;
                    }
                }
            }
            body->debug_location_count = status == CHECK_PROGRAM_OK ? first[locals] : 0;
        }
    }
    return status;
}

// Body `b`'s code after its code generator (Tier 0's or Tier 1's), on its lane (`lane`): branch sizing, encoding into
// the lane's bytes, its unwind frame, its labels' offsets and with -g its raw rows, and what its fixups need. Returns
// false for a failure, which it records in the body as source order's emission reports it: no room; a record
// x86_validate rejects (with `verify`); or a sizing or an encoding its records shouldn't have, or a frame other than its
// unwind table's, at its declaration.
static bool check_program_emit_code(CheckEmitRun* run, CheckEmitLane* lane, uint32_t b)
{
    const CheckProgram* program = run->program;
    const EmittedCode* code = run->codes + b;
    CheckEmitBody* body = run->bodies + b;
    Arena* scratch = lane->scratch;
    Arena* bytes = lane->bytes;
    const CheckProgramNaked* naked = run->lowered->asms.naked != 0 && run->lowered->asms.naked[b].planned ? run->lowered->asms.naked + b : 0;
    *body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
    // What the merge reads stays on the lane's scratch; the offsets don't.
    body->fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
    body->label_offsets = code->label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, code->label_count) : 0;
    // A naked function's templates' labels, or its asm bodies' (#1839, EmittedCode.asm_label_records): it has no body.
    const uint32_t asm_label_count = naked != 0 ? naked->label_count : code->asm_label_count;
    body->asm_offsets = asm_label_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, asm_label_count) : 0;
    const uint64_t rows = run->sources ? (uint64_t)code->source_count + 2 + BUSTER_CHECK_DEBUG_SLACK : 0;
    body->row_offsets = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, rows) : 0;
    body->row_tokens = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, rows) : 0;
    // Its frame's rows (#1056, emitted_unwind), which the tables read once the lanes are done.
    EmissionFrameRow* frame_rows = run->unwind && code->frame_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, EmissionFrameRow, code->frame_count) : 0;
    body->debug_spans = run->sources ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDebugValueSpan, (uint64_t)code->source_count + 1) : NULL;
    const uint32_t locals = run->lowered->functions[b].local_count;
    body->debug_locations = run->sources && code->local_range_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDebugLocation, code->local_range_count) : NULL;
    body->debug_location_first = run->sources && code->local_range_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)locals + 1) : NULL;
    const uint64_t work_mark = scratch->length;
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
    // patchable_function_entry's NOPs (#1447): room for them after the code, which check_program_emit_patchable moves past
    // them. A unit without the attribute or -fpatchable-function-entry reads two fields.
    const CheckProgramPatchable patchable = program->patchable_key_count != 0 || program->patchable_count != 0 ? check_program_patchable(program, b) :
                                                                                                                  (CheckProgramPatchable){ 0 };
    const uint32_t sled = patchable.count;
    const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
    const uint64_t before = bytes->length;
    uint8_t* encoded = arena_push(bytes, byte_capacity + sled, 16);
    bool valid = offsets != 0 && body->fixup_ends != 0 && encoded != 0 && (code->label_count == 0 || body->label_offsets != 0) &&
                 (asm_label_count == 0 || body->asm_offsets != 0) && (!run->sources || (body->row_offsets != 0 && body->row_tokens != 0 && body->debug_spans != NULL)) &&
                 (!run->sources || code->local_range_count == 0 || (body->debug_locations != NULL && body->debug_location_first != NULL)) &&
                 (!run->unwind || code->frame_count == 0 || frame_rows != 0);
    body->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
    // A failure past the encoding is the backend's, but where a frame's room ran out.
    CheckProgramStatus failure = CHECK_PROGRAM_BACKEND;
    EmittedVerdict frame_verdict = { 0 };
    if (valid && run->verify && x86_validate(code->records, code->count) != code->count)
    {
        body->status = CHECK_PROGRAM_BACKEND;
        body->reason = "internal compiler error: a record of the code generator's fails x86_validate";
        body->token = program->bodies[b].declaration;
        valid = false;
    }
    if (valid)
    {
        // The branch columns alone, which sizing reads and writes by: the code generators' records are valid by
        // construction, which x86_validate checks above only with `verify` (#532).
        const BranchSizingCheck shape = run->scalar ? branch_sizing_validate_branches_scalar(code->records, code->count, code->branches, code->targets, code->branch_count) :
                                                      branch_sizing_validate_branches(code->records, code->count, code->branches, code->targets, code->branch_count);
        const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
        check_clock_lap(run->stage_clock, CHECK_STAGE_BRANCH_SIZING);
        const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                               run->scalar ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends) :
                                             x86_encode(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends);
        check_clock_lap(run->stage_clock, CHECK_STAGE_ENCODE);
        valid = length >= 0;
        if (valid && run->sources)
        {
            // The rows' own failure: no room (a lane's, which the emission retries on one lane) or the protocol's refusal.
            valid = check_program_emit_body_rows(body, program, run->parser, run->lowered->entry_tokens[b], code, b, offsets, (uint32_t)length, run->scalar, scratch);
            failure = valid ? failure : body->status;
            if (valid)
            {
                CheckDebugValueSpan* spans = body->debug_spans;
                const Tier0Function* function = run->lowered->functions + b;
                const uint32_t* tokens = run->lowered->entry_tokens[b];
                for (uint32_t e = 0; valid && e < code->source_count; e += 1)
                {
                    // Tail-call entries include frame teardown. Omit that whole entry, rather than extend a home
                    // over instructions which have restored its register or moved its stack base.
                    const bool tail = code->local_homes != NULL && function->kinds[e] == BUSTER_TYPED_TAIL_CALL;
                    const uint32_t begin = offsets[code->source_records[e]];
                    const uint32_t end = offsets[code->source_records[e + 1]];
                    const bool known = code->source_origins == NULL || code->source_origins[e] != BUSTER_EMISSION_NO_ORIGIN;
                    if (!tail && known && begin < end)
                    {
                        spans[body->debug_span_count++] = (CheckDebugValueSpan){ .begin = begin, .end = end, .token = check_debug_origin_token(code->source_origins, tokens, e, program->bodies[b].declaration) };
                    }
                }
                if (valid)
                {
                    const CheckProgramStatus locations = check_program_emit_locations(body, code, locals, offsets, scratch);
                    valid = locations == CHECK_PROGRAM_OK;
                    failure = valid ? failure : locations;
                }
            }
            check_clock_lap(run->stage_clock, CHECK_STAGE_DEBUG);
        }
        if (valid && sled != 0 && (uint64_t)length + sled > INT32_MAX)
        {
            body->reason = "a function of 2 GB of code or more, past its unwind table's offsets";
            failure = CHECK_PROGRAM_UNSUPPORTED;
            valid = false;
        }
        if (valid && run->unwind)
        {
            // Its frame events resolved after sizing (a naked function's code is its templates', which say nothing of
            // its frame: no events, so its unwind entry, as clang's, has the call's rule alone, whatever they push). A
            // frame its producer couldn't describe is the compiler's own failure; and with --verify, so are events its
            // records don't establish.
            const char* reason = 0;
            failure = check_program_emit_frame_checked(code, offsets, length, run->verify, run->scalar, frame_rows, scratch, &run->frames[b], &reason, &frame_verdict);
            valid = failure == CHECK_PROGRAM_OK;
            failure = valid ? CHECK_PROGRAM_BACKEND : failure;
            body->reason = valid ? body->reason : reason;
            check_clock_lap(run->stage_clock, CHECK_STAGE_UNWIND);
        }
        if (valid)
        {
            const uint64_t end = (uint64_t)(encoded - bytes->base) + (uint64_t)length + sled;
            arena_pop_to(bytes, end > before ? end : before);
            body->bytes = encoded;
            body->length = (uint64_t)length + sled;
            for (uint32_t k = 0; k < code->label_count; k += 1)
            {
                // An optimized-away label has no control-flow target. Keep its symbol's existing entry placeholder
                // and index; only that explicit sentinel is accepted without a record in the emitted function.
                const uint32_t record = code->label_records[k];
                body->label_offsets[k] = record == UINT32_MAX ? 0 : record < code->count ? offsets[record] : UINT32_MAX;
            }
            // A template's label may be its code's end.
            for (uint32_t k = 0; k < asm_label_count; k += 1)
            {
                const uint32_t record = naked != 0 ? naked->labels[k].record : code->asm_label_records[k];
                body->asm_offsets[k] = record < code->count ? offsets[record] : record == code->count ? (uint32_t)length : UINT32_MAX;
            }
            if (sled != 0)
            {
                check_program_emit_patchable(run, body, code, b, encoded, (uint32_t)length, sled, patchable.offset, frame_rows);
            }
        }
        else
        {
            body->status = failure;
            body->token = program->bodies[b].declaration;
        }
    }
    if (!valid)
    {
        arena_pop_to(bytes, before);
    }
    arena_pop_to(scratch, work_mark);
    if (frame_verdict.error != EMITTED_VERIFY_OK && frame_verdict.error != EMITTED_VERIFY_MEMORY)
    {
        // Offsets and verification scratch are gone; this small failure record survives until source-order emission.
        // A diagnostic allocation failure must not replace the original compiler failure.
        EmittedVerdict* retained = BUSTER_ARENA_PUSH_ARRAY(scratch, EmittedVerdict, 1);
        if (retained != NULL)
        {
            *retained = frame_verdict;
            body->frame_verdict = retained;
        }
    }
    check_clock_lap(run->stage_clock, CHECK_STAGE_CODE_ATOMS);
    return valid;
}

// Body `b`'s code by Tier 1's pipeline (--tier1, check_tier1_compile) into run->codes[b], on its lane's arenas for it,
// the code in its lane's scratch as Tier 0's is, with an explicit outcome and disposition. A naked
// function's code is its templates'. Out of line, so that the plain emission carries none of it.
static __attribute__((noinline)) CheckTier1Outcome check_program_emit_tier1(CheckEmitRun* run, CheckEmitLane* lane, uint32_t b, bool naked)
{
    const CheckProgram* program = run->program;
    const Tier0Function* function = &run->lowered->functions[b];
    const uint32_t body_target = program->body_targets != 0 ? program->body_targets[b] : 0;
    const X86Target* target = body_target == 0 || body_target == BUSTER_CHECK_PROGRAM_TARGET_VERSIONS ? &program->target : &program->targets[body_target - 1];
    const CheckTier1Fault* fault = run->lanes->tier1_fault;
    fault = fault != NULL && fault->body == b + 1 ? fault : NULL;
    lane->tier1->inlining = run->tier1_inline;
    // The summaries this lane builds live in its lane state's arena of the run (#2541): another lane may read them.
    lane->tier1->summaries = run->tier1_inline != NULL && run->tier1_inline->arenas != NULL ? &run->tier1_inline->arenas[2 * (lane - run->lane_states)] : NULL;
    lane->tier1->schedule = run->lanes->tier1_schedule;
    CheckTier1Outcome outcome = naked ? check_tier1_failure(CHECK_TIER1_NAKED, CHECK_TIER1_CAUSE_ADMISSION, BUSTER_CHECK_TIER1_NAKED, UINT32_MAX, 0) :
                                        check_tier1_compile(lane->tier1, function, check_program_result_class(function, program, b, false), target, run->scalar,
                                                            !run->lanes->tier1_no_rematerialize, run->lanes->tier1_gvn, run->verify, run->sources, lane->scratch,
                                                            &run->codes[b], &run->tier1_counts[b], fault);
    outcome = check_tier1_policy(outcome, run->lanes->tier1_strict);
    run->tier1_outcomes[b] = outcome;
    return outcome;
}

// What a symbol's declarations make of it for GNU's common attribute (#147, check_program_emit's commons): the attribute,
// a section attribute, which keeps it a definition of its own, an initializer's definition, which clang's
// isVarDeclStrongDefinition makes a strong one, a tentative definition, without which the unit defines none, and the
// nocommon attribute, which keeps -fcommon's from it. Its symbol is a common one where a tentative definition is all
// that defines it, without a section, the attribute or -fcommon without nocommon make it one, and its flags have it
// global (not weak or of internal linkage), of an object (not a thread-local) and no alias's
// (check_program_emit_common).
#define BUSTER_CHECK_EMIT_COMMON 0x01u
#define BUSTER_CHECK_EMIT_COMMON_SECTION 0x02u
#define BUSTER_CHECK_EMIT_COMMON_DEFINED 0x04u
#define BUSTER_CHECK_EMIT_COMMON_TENTATIVE 0x08u
#define BUSTER_CHECK_EMIT_NOCOMMON 0x10u
// A loader_uninitialized declaration (#147): a definition that clang's code gives common linkage where a tentative one
// would have it, made weak for its initializer that isn't zero (EmitGlobalVarDefinition).
#define BUSTER_CHECK_EMIT_COMMON_LOADER 0x20u
// A selectany definition uses weak binding and a COMDAT section, never a common symbol.
#define BUSTER_CHECK_EMIT_SELECTANY 0x40u

// The definitions of the unit's objects, an initializer's or a tentative one, for its symbols' commons: a pass over the
// tokens that declare objects of static storage, few of a unit's, 64 at a time (check_program_object_access's scan; the
// scalar path, `scalar`, takes every token), which only a unit with a common attribute, or compiled with -fcommon, pays
// for.
static void check_program_emit_commons(const CheckProgram* program, uint8_t* commons, bool scalar)
{
    (void)scalar;
    const CheckInput* input = program->input;
    for (uint32_t first = 0; first < input->token_count; first += 64)
    {
        const uint64_t lanes = BUSTER_SCALAR_PATH(scalar) ? check_input_scan_all(first, input->token_count) :
                                        check_input_scan_bit(input->declaration_bits, first, input->token_count, BUSTER_CHECK_BITS_STATIC);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            const bool defined = check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED);
            const bool declares = input->declarations[token] == token &&
                                  (defined || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_TENTATIVE));
            const uint32_t object = declares ? input->objects[token] : 0;
            const SymbolId taken = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
            commons[symbol] |= symbol == 0 ? 0 : defined ? BUSTER_CHECK_EMIT_COMMON_DEFINED : BUSTER_CHECK_EMIT_COMMON_TENTATIVE;
        }
    }
}

// Whether a symbol's declarations (`bits`, check_program_emit_commons') make it a common one, with -fcommon's `all`.
static bool check_program_emit_common(uint8_t bits, bool all)
{
    const bool asked = (bits & BUSTER_CHECK_EMIT_COMMON) != 0 || (all && (bits & BUSTER_CHECK_EMIT_NOCOMMON) == 0);
    return asked && (bits & (BUSTER_CHECK_EMIT_COMMON_SECTION | BUSTER_CHECK_EMIT_COMMON_DEFINED | BUSTER_CHECK_EMIT_COMMON_TENTATIVE)) ==
                        BUSTER_CHECK_EMIT_COMMON_TENTATIVE;
}

// Which of a part's halves a take runs (check_program_emit_part): both, as every team's lanes take their parts but a
// growing one's, or for a growing team (CheckEmitGrowth) its lowering, or its code once its lowering is done.
typedef enum CheckEmitHalf
{
    CHECK_EMIT_HALF_BOTH,
    CHECK_EMIT_HALF_LOWER,
    CHECK_EMIT_HALF_CODE,
} CheckEmitHalf;

// Defined in c_check_tier0_inline.c (#1398): body `b`'s typed stream with its calls of the unit's inline callees spliced
// in (tier0_inline), into *result in `arena`, or false where it has none or the splice refuses it.
static bool check_tier0_inline_body(CheckEmitRun* run, CheckEmitLane* lane, uint32_t b, Arena* arena, Tier0Inlined* result);

// A part's lowering (check_lower_part) on lane `lane`.
static void check_program_emit_lower_half(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part)
{
    if (run->sources)
    {
        check_lower_part_sources(&lane->writer, run->shared, &part->lower, lane->output, lane->scratch);
    }
    else
    {
        check_lower_part(&lane->writer, run->shared, &part->lower, lane->output, lane->scratch);
    }
    // What the code reads of each lowered function beside its stream: the access column, and the entries' first records
    // only for source rows. Set here rather than by its code (#1282), since with inlining another body's lane reads this
    // function's stream while its own code runs.
    for (uint32_t b = part->lower.first; part->lower.stage == CHECK_LOWER_STAGE_NONE && b < part->lower.end; b += 1)
    {
        Tier0Function* function = &run->lowered->functions[b];
        function->symbol_access = run->access;
        function->symbol_count = run->access_count;
        function->entry_records = run->sources;
        // The frame pointer the options ask for (#177), which Tier 0's frame and Tier 1's take.
        _Static_assert(BUSTER_PREDEFINES_FRAME_POINTER_ALL == BUSTER_TIER0_FRAME_POINTER_ALL && BUSTER_PREDEFINES_FRAME_POINTER_NON_LEAF == BUSTER_TIER0_FRAME_POINTER_NON_LEAF &&
                           BUSTER_PREDEFINES_FRAME_POINTER_NONE == BUSTER_TIER0_FRAME_POINTER_NONE, "the options' frame pointer is Tier 0's");
        function->frame_pointer = run->program->frame_pointer;
        // Stack-clash protection's probes (#177), which Tier 0's frame and allocations take, and Tier 1's.
        function->probe_stack = run->program->probe_stack;
        if (run->inline_table != NULL)
        {
            run->inline_table->ready[b] = 1;
        }
    }
    check_clock_lap(run->stage_clock, CHECK_STAGE_LOWER);
    check_clock_lower(run->stage_clock, CHECK_CLOCK_LOWER_STATEMENTS, part->lower.statement_ticks);
    check_clock_lower(run->stage_clock, CHECK_CLOCK_LOWER_BODIES, part->lower.body_ticks);
    check_clock_lower(run->stage_clock, CHECK_CLOCK_LOWER_VALIDATE, part->lower.validate_ticks);
}

// A lowered part's code on lane `lane`: each body's Tier 0 code, the lane's function access column given, and its code
// after Tier 0 (check_program_emit_code) until one of them fails.
static void check_program_emit_code_half(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part)
{
    const uint32_t end = part->lower.end;
    part->tier0_failed = end;
    part->code_failed = end;
    for (uint32_t b = part->lower.first; part->lower.stage == CHECK_LOWER_STAGE_NONE && part->tier0_failed == end && b < end; b += 1)
    {
        Tier0Function* function = &run->lowered->functions[b];
        // A naked function's code is its templates' (check_program_asm_plan_naked), not its function's; one that takes
        // directives is an assembled unit, which the merge publishes (check_program_emit_asm_unit).
        const CheckProgramNaked* naked = run->lowered->asms.naked != 0 && run->lowered->asms.naked[b].planned ? run->lowered->asms.naked + b : 0;
        // With --tier1, Tier 1's code where its pipeline takes the body (check_program_emit_tier1). A naked function
        // whose templates take directives is an assembled unit, which remains an explicit admission refusal here.
        const bool unit_naked = naked != 0 && naked->unit_assembled;
        CheckTier1Outcome outcome = (CheckTier1Outcome){ 0 };
        if (unit_naked)
        {
            if (lane->tier1 != NULL)
            {
                outcome = check_tier1_failure(CHECK_TIER1_NAKED, CHECK_TIER1_CAUSE_ADMISSION, BUSTER_CHECK_TIER1_NAKED, UINT32_MAX, 0);
                outcome = check_tier1_policy(outcome, run->lanes->tier1_strict);
                run->tier1_outcomes[b] = outcome;
            }
        }
        else
        {
            outcome = lane->tier1 != NULL ? check_program_emit_tier1(run, lane, b, naked != 0) : (CheckTier1Outcome){ 0 };
        }
        const bool tier1 = outcome.category == CHECK_TIER1_SUCCESS;
        const bool forbidden = outcome.fallback == CHECK_TIER1_FALLBACK_FORBIDDEN;
        if (unit_naked)
        {
            if (!forbidden)
            {
                // Its code is its unit's entry atom, which the merge publishes; its length and start are known here for
                // -g's rows, before the merge.
                run->codes[b] = emitted_from_tier0(&naked->code, false, run->sources, true, lane->scratch);
                CheckEmitBody* body = run->bodies + b;
                *body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK, .token = BUSTER_CHECK_NONE };
                body->length = naked->unit.entry_atom != 0 ? naked->unit.link.atoms.size[naked->unit.entry_atom] : 0;
            }
            else
            {
                part->tier0_failed = b;
            }
        }
        else
        {
            if (!tier1 && !forbidden)
            {
                // Tier 0's code as the emission takes it (emitted_from_tier0), with its frame's events where the tables
                // are written: a naked function's templates have none.
                // With Tier 0's inlining (#1398), the body's stream with its inline callees' calls spliced in, its code's
                // entries and locals then put back in terms of its own (tier0_inline_code).
                Tier0Inlined inlined;
                const bool spliced = naked == 0 && lane->tier0_inline != NULL && check_tier0_inline_body(run, lane, b, lane->scratch, &inlined);
                const Tier0Function* source = spliced ? &inlined.function : function;
                Tier0Code compiled = naked != 0 ? naked->code : run->scalar ? tier0_compile_scalar(source, lane->scratch) : tier0_compile(source, lane->scratch);
                if (spliced)
                {
                    tier0_inline_code(&compiled, function, &inlined, lane->scratch);
                }
                // A body whose asm template returns (#1839) leaves its frame as its template does, which the verifier doesn't
                // read, as it doesn't a naked function's; and so does one that moves a value into rsp (#1846, tier0_asm_stack).
                run->codes[b] = emitted_from_tier0(&compiled, run->unwind && naked == 0, run->sources,
                                                   naked != 0 || tier0_asm_leaves(source) || tier0_asm_stack(source),
                                                   lane->scratch);
            }
            check_clock_lap(run->stage_clock, CHECK_STAGE_TIER0);
            if (forbidden || run->codes[b].error != TIER0_OK)
            {
                part->tier0_failed = b;
            }
            else if (part->code_failed == end && !check_program_emit_code(run, lane, b))
            {
                part->code_failed = b;
            }
        }
    }
}

// A part of the bodies on lane `lane`, its halves that `half` names: its lowering, then its code.
static void check_program_emit_part(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part, CheckEmitHalf half)
{
    if (half != CHECK_EMIT_HALF_CODE)
    {
        check_program_emit_lower_half(run, lane, part);
    }
    if (half != CHECK_EMIT_HALF_LOWER)
    {
        check_program_emit_code_half(run, lane, part);
    }
}

// Lane `index`'s arenas, for its first part: the caller's for lane 0, but its writer's columns' (CheckEmitRun.columns),
// and for another lane its own, reserved the first time and otherwise put back (the emission before this one's are done
// with), its scratch its writer's columns'. Returns false if one can't be reserved.
static bool check_program_emit_lane_arenas(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index)
{
    bool valid = true;
    if (index == 0)
    {
        lane->output = run->output;
        lane->scratch = run->scratch;
        lane->bytes = run->bytes;
        lane->columns = run->columns;
    }
    else
    {
        CheckProgramLaneArenas* arenas = run->lanes->arenas + index;
        Arena* each[3] = { &arenas->output, &arenas->scratch, &arenas->bytes };
        for (uint32_t i = 0; i < 3; i += 1)
        {
            if (each[i]->base == 0)
            {
                // The writer's columns (the scratch) have room for any of the program's bodies each, far apart, of which a
                // lane touches a little at each column's start: in base pages, as only those are cleared, where a huge
                // page each, cleared whole, made 8 lanes' kernel time 40 times one lane's on the 9700X. The functions'
                // columns and bytes are packed, and take huge pages past the first 64 KB (arena_reserve).
                *each[i] = i == 1 ? arena_reserve_small(UINT64_C(1) << 36, UINT64_C(1) << 36) : arena_reserve(UINT64_C(1) << 36);
            }
            arena_pop_to(each[i], 0);
            valid = valid && each[i]->base != 0;
        }
        lane->output = &arenas->output;
        lane->scratch = &arenas->scratch;
        lane->bytes = &arenas->bytes;
        lane->columns = &arenas->scratch;
    }
    lane->tier1 = run->tier1_outcomes != NULL ? &run->lanes->arenas[index].tier1 : NULL;
    lane->tier0_inline = run->tier0_inline == NULL ? NULL : run->lanes != NULL ? &run->lanes->arenas[index].tier0_inline : &check_program_thread_inline;
    return valid;
}

// Lane `index`'s arenas and writer, for its first part.
static bool check_program_emit_lane_prepare(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index)
{
    return check_program_emit_lane_arenas(run, lane, index) &&
           check_lower_writer_begin(&lane->writer, &lane->abi, &lane->status, run->shared, run->sources, lane->columns);
}

// A part's failure before it lowered anything: its lane had no room for its writer.
static void check_program_emit_part_refused(CheckEmitPart* part)
{
    part->lower.stage = CHECK_LOWER_STAGE_BODY;
    part->lower.status = CHECK_PROGRAM_RESOURCE;
    part->lower.token = BUSTER_CHECK_NONE;
    part->lower.backend_node = 0;
    part->lower.reason = NULL;
    part->lower.final_token = BUSTER_CHECK_NONE;
    part->lower.entries = 0;
    part->lower.label_symbol_count = 0;
    part->lower.runtime_count = 0;
    part->tier0_failed = part->lower.end;
    part->code_failed = part->lower.end;
}

// The halves `half` of the part `part` on lane `index`, which holds the test's squeezed body (CheckProgramLanes.squeeze_body):
// each of the lane's arenas that squeeze_arenas names has only squeeze_room bytes to spare while the lane makes its
// writer, if this is its first part, and works on the part; then its room comes back. Lane 0's writer's columns, an
// arena of their own, are squeezed with its scratch. Out of line, so that the lanes' loop carries none of it.
static __attribute__((noinline)) void check_program_emit_squeezed(CheckEmitRun* run, CheckEmitLane* lane, CheckEmitPart* part, uint32_t index, CheckEmitHalf half)
{
    CheckProgramLanes* lanes = run->lanes;
    const bool arenas = lane->prepared ? lane->ready : check_program_emit_lane_arenas(run, lane, index);
    Arena* each[4] = { lane->output, lane->scratch, lane->bytes, lane->columns };
    const uint32_t names[4] = { BUSTER_CHECK_EMIT_SQUEEZE_OUTPUT, BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH, BUSTER_CHECK_EMIT_SQUEEZE_BYTES, BUSTER_CHECK_EMIT_SQUEEZE_SCRATCH };
    const uint32_t count = arenas && lane->columns != lane->scratch ? 4 : 3;
    uint64_t capacities[4] = { 0, 0, 0, 0 };
    for (uint32_t i = 0; arenas && i < count; i += 1)
    {
        capacities[i] = each[i]->capacity;
        const bool named = (lanes->squeeze_arenas & names[i]) != 0;
        each[i]->capacity = named && lanes->squeeze_room < capacities[i] - each[i]->length ? each[i]->length + lanes->squeeze_room : capacities[i];
    }
    lane->ready = lane->prepared ? lane->ready :
                                   arenas && check_lower_writer_begin(&lane->writer, &lane->abi, &lane->status, run->shared, run->sources, lane->columns);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part, half);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
    for (uint32_t i = 0; arenas && i < count; i += 1)
    {
        each[i]->capacity = capacities[i];
    }
    lanes->squeezed = index + 1;
}

// A test's slow lane's sleep (CheckProgramLanes.slow_lane). Out of line, so that the lanes' loops carry none of it.
static __attribute__((noinline)) void check_program_emit_pause(const CheckProgramLanes* lanes)
{
    const struct timespec pause = { .tv_sec = 0, .tv_nsec = (long)lanes->slow_microseconds * 1000 };
    nanosleep(&pause, NULL);
}

// The halves `half` of part `part` of the bodies, cut by CheckEmitRun.boundaries, on lane `index`, whose state is `lane`.
// A lane prepares its writer when it takes its first part, so that one that takes none spends nothing on it; one that
// can't refuses the part, whichever half it took.
static void check_program_emit_take(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index, uint32_t part, CheckEmitHalf half)
{
    CheckEmitPart* result = run->parts + part;
    result->lower.first = (uint32_t)run->boundaries[part];
    result->lower.end = (uint32_t)run->boundaries[part + 1];
    const uint32_t squeeze = run->lanes->squeeze_body;
    if (squeeze != 0 && squeeze - 1 >= result->lower.first && squeeze - 1 < result->lower.end)
    {
        check_program_emit_squeezed(run, lane, result, index, half);
    }
    else
    {
        lane->ready = lane->prepared ? lane->ready : check_program_emit_lane_prepare(run, lane, index);
        lane->prepared = true;
        if (lane->ready)
        {
            check_program_emit_part(run, lane, result, half);
        }
        else
        {
            check_program_emit_part_refused(result);
        }
    }
    if (half == CHECK_EMIT_HALF_BOTH && run->lanes->slow_lane == index + 1)
    {
        check_program_emit_pause(run->lanes);
    }
}

// Every lane's entry: the parts by cost (CheckEmitRun.costs), taken costliest first.
static void check_program_emit_lane(void* argument)
{
    CheckEmitRun* run = argument;
    const uint32_t index = lane_index();
    CheckEmitLane* lane = run->lane_states + index;
    const uint64_t parts = lane_parts_by_cost(run->costs, run->body_count, run->boundaries, run->orders[index]);
    if (run->inline_table != NULL)
    {
        // Every part's lowering, then, once the lanes have met, every part's code (#1282).
        for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
        {
            check_program_emit_take(run, lane, index, run->orders[index][taken], CHECK_EMIT_HALF_LOWER);
        }
        lane_sync();
        for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
        {
            check_program_emit_take(run, lane, index, run->orders[index][taken], CHECK_EMIT_HALF_CODE);
        }
    }
    else
    {
        for (uint64_t taken = lane_take(parts); taken < parts; taken = lane_take(parts))
        {
            check_program_emit_take(run, lane, index, run->orders[index][taken], CHECK_EMIT_HALF_BOTH);
        }
    }
    if (index == 0)
    {
        run->part_count = (uint32_t)parts;
    }
}

// Lane 0, alone in the flow: a round of the emission `run` on `lanes` of the flow's lanes (at least 2), after which it
// goes on alone. Its first round learns how the session's lanes started (lane_flow_lanes): false if they didn't, for the
// bodies to go on one lane.
static bool check_program_emit_round(LaneFlow* flow, CheckEmitRun* run, uint32_t lanes)
{
    const bool started = lane_flow_lanes(flow) > 1;
    if (started)
    {
        lane_flow_round_begin(flow, BUSTER_CHECK_EMIT_ROUND_BODIES, run, lanes);
        check_program_emit_lane(run);
        lane_flow_round_end(flow);
    }
    return started;
}

// How many more lanes a growing team (CheckEmitGrowth) of `admitted` lanes, of up to `lanes`, admits now, under the
// lease `lease`: as many as the jobserver has spare tokens, up to the lanes it may take and the items left, if the work
// left pays for the guardian's start: its tokens on `admitted` lanes less on that many more (rest × more / (admitted ×
// (admitted + more))) at least BUSTER_CHECK_EMIT_GROW_TOKENS, or whatever they pay with --lanes N (every_lane). The work
// left is the lowering's share (BUSTER_CHECK_EMIT_GROW_LOWER_SIXTEENTHS) of the tokens of the parts not taken for their
// lowering yet, and the code's of those not taken for their code.
static uint32_t check_program_emit_grow(const CheckEmitRun* run, const CheckEmitGrowth* growth, uint32_t admitted, uint32_t lanes, WorkerBudget* budget,
                                        WorkerBudgetLease* lease)
{
    const uint64_t ticket = __atomic_load_n(&growth->ticket, __ATOMIC_RELAXED);
    const uint64_t parts = growth->parts;
    const uint64_t left = ticket < 2 * parts ? 2 * parts - ticket : 0;
    uint64_t more = lanes - admitted;
    more = left < more ? left : more;
    const uint32_t spare = more != 0 ? worker_budget_available(budget) : 0;
    more = spare < more ? spare : more;
    const uint64_t lowering = ticket < parts ? growth->rest[ticket] : 0;
    const uint64_t coding = ticket < parts ? growth->rest[0] : left != 0 ? growth->rest[ticket - parts] : 0;
    const uint64_t rest = (lowering * BUSTER_CHECK_EMIT_GROW_LOWER_SIXTEENTHS + coding * (16 - BUSTER_CHECK_EMIT_GROW_LOWER_SIXTEENTHS)) / 16;
    const bool pays = run->lanes->every_lane || rest * more >= (uint64_t)BUSTER_CHECK_EMIT_GROW_TOKENS * admitted * (admitted + more);
    *lease = (WorkerBudgetLease){ .control = -1, .reply = -1, .child = -1 };
    const uint32_t granted = more != 0 && pays ? worker_budget_grow(budget, (uint32_t)more, lease) : 0;
    // A guardian that took none (another recipe took the tokens since FIONREAD) goes at once, rather than hold a lease
    // of the team's. One that took fewer stays: its start is paid, and each token it holds is a lane's.
    if (granted == 0)
    {
        worker_budget_release(lease);
    }
    return granted;
}

// A test's `count` tokens for the jobserver's writer `fd` (CheckProgramLanes.grow_supply), as another recipe returning
// them would write them: each byte BUSTER_CHECK_EMIT_SUPPLY_BYTE plus its index, up to BUSTER_CHECK_EMIT_MAX_LANES of
// them. Out of line, so that lane 0's loop carries none of it.
static __attribute__((noinline)) void check_program_emit_supply(int fd, uint32_t count)
{
    uint8_t bytes[BUSTER_CHECK_EMIT_MAX_LANES];
    const uint32_t total = count < BUSTER_CHECK_EMIT_MAX_LANES ? count : BUSTER_CHECK_EMIT_MAX_LANES;
    for (uint32_t i = 0; i < total; i += 1)
    {
        bytes[i] = (uint8_t)(BUSTER_CHECK_EMIT_SUPPLY_BYTE + i);
    }
    uint32_t done = 0;
    bool writing = true;
    while (writing && done < total)
    {
        const ssize_t written = write(fd, bytes + done, total - done);
        done += written > 0 ? (uint32_t)written : 0;
        writing = written > 0 || (written < 0 && errno == EINTR);
    }
}

// Item `item` of a growing team's (CheckEmitGrowth) twice `parts`, on lane `index`, whose state is `lane`: the lowering
// of the part at its place in `order`, after which that place's word in `lowered` turns 2, or past the parts, the code
// of the part at its place less `parts`, once that place's word has turned (another lane may still be lowering it).
static void check_program_emit_item(CheckEmitRun* run, CheckEmitLane* lane, uint32_t index, uint64_t item, uint64_t parts, const uint32_t* order, uint32_t* lowered)
{
    const bool lowering = item < parts;
    const uint64_t place = lowering ? item : item - parts;
    // A part's code waits for its lowering, or with inlining (#1282) for every part's: each was taken before any code was.
    for (uint64_t other = run->inline_table != NULL ? 0 : place; !lowering && other < (run->inline_table != NULL ? parts : place + 1); other += 1)
    {
        if ((__atomic_load_n(lowered + other, __ATOMIC_ACQUIRE) & ~1u) == 0)
        {
            lane_wait(lowered + other, 0);
        }
    }
    if (lowering && run->lanes->slow_lane == index + 1)
    {
        check_program_emit_pause(run->lanes);
    }
    check_program_emit_take(run, lane, index, order[place], lowering ? CHECK_EMIT_HALF_LOWER : CHECK_EMIT_HALF_CODE);
    if (lowering)
    {
        lane_publish(lowered + place, 2);
    }
}

// Lane 0's growing team (CheckEmitGrowth, whose `rest` has room for a place per part and one more, and `lowered` a word
// per part) of up to `lanes` of the flow's lanes, `admitted` of them as it starts, its first lease leases[0]: the parts
// cut for `lanes` lanes (parts_by_cost), their items taken by lane 0 alone until it admits another lane, then in a round
// of `lanes` lanes. Each growth's lease goes in `leases` after the others, `*lease_count` of them. When lane 0 finds no
// item left it closes the admission, and the round ends as every round does, once every lane has met. Returns the lanes
// it admitted.
static uint32_t check_program_emit_growing(LaneFlow* flow, CheckEmitRun* run, CheckEmitGrowth* growth, uint32_t lanes, uint32_t admitted, WorkerBudget* budget,
                                           WorkerBudgetLease* leases, uint32_t* lease_count)
{
    const uint64_t parts = parts_cut_by_cost(run->costs, run->body_count, (uint64_t)BUSTER_CHECK_EMIT_GROW_PARTS_PER_LANE * lanes, run->boundaries, run->orders[0]);
    // The costliest part first, as in every lane's order, then the others in source order: lane 0 alone takes most of
    // them, and in source order its lowering and code keep more of one lane's locality than in cost order.
    uint32_t* order = run->orders[0];
    const uint32_t costliest = order[0];
    uint64_t next = 1;
    for (uint32_t part = 0; part < parts; part += 1)
    {
        if (part != costliest)
        {
            order[next] = part;
            next += 1;
        }
    }
    growth->parts = parts;
    growth->order = order;
    uint32_t* const lowered = growth->lowered;
    memset(lowered, 0, parts * sizeof(uint32_t));
    // The tokens of the parts from each place in the order on, its last place's first.
    uint64_t rest = 0;
    growth->rest[parts] = 0;
    for (uint64_t place = parts; place > 0; place -= 1)
    {
        const uint32_t part = order[place - 1];
        const uint64_t first = run->boundaries[part];
        const uint64_t end = run->boundaries[part + 1];
        rest += end > first ? run->costs[end - 1] - (first != 0 ? run->costs[first - 1] : 0) : 0;
        growth->rest[place - 1] = rest;
    }
    run->part_count = (uint32_t)parts;
    __atomic_store_n(&growth->ticket, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&growth->admission, admitted << 2, __ATOMIC_RELAXED);
    bool round = admitted > 1;
    if (round)
    {
        lane_flow_round_begin(flow, BUSTER_CHECK_EMIT_ROUND_BODIES, run, lanes);
    }
    CheckEmitLane* lane = run->lane_states;
    uint32_t supply = run->lanes->grow_supply;
    uint32_t count = *lease_count;
    uint64_t polled = 0;
    const uint64_t items = 2 * parts;
    uint64_t taken = __atomic_fetch_add(&growth->ticket, 1, __ATOMIC_RELAXED);
    for (; taken < items; taken = __atomic_fetch_add(&growth->ticket, 1, __ATOMIC_RELAXED))
    {
        check_program_emit_item(run, lane, 0, taken, parts, order, lowered);
        if (supply != 0)
        {
            check_program_emit_supply(budget->write_fd, supply);
            supply = 0;
        }
        const bool open = admitted < lanes && count < BUSTER_CHECK_EMIT_GROW_LEASES;
        const uint64_t now = open ? check_program_lanes_now() : polled;
        if (now - polled >= BUSTER_CHECK_EMIT_GROW_NANOSECONDS)
        {
            polled = now;
            WorkerBudgetLease* lease = leases + count;
            const uint32_t more = check_program_emit_grow(run, growth, admitted, lanes, budget, lease);
            count += lease->child > 0 ? 1 : 0;
            admitted += more;
            if (more != 0 && round)
            {
                lane_publish(&growth->admission, admitted << 2);
            }
            else if (more != 0)
            {
                __atomic_store_n(&growth->admission, admitted << 2, __ATOMIC_RELAXED);
                lane_flow_round_begin(flow, BUSTER_CHECK_EMIT_ROUND_BODIES, run, lanes);
                round = true;
            }
        }
    }
    *lease_count = count;
    if (round)
    {
        lane_publish(&growth->admission, (admitted << 2) | 2u);
        lane_flow_round_end(flow);
    }
    return admitted;
}

// A lane of a growing team's round past lane 0 (CheckEmitGrowth): asleep until lane 0 admits it, then the items left,
// or none once lane 0 has closed the admission.
static void check_program_emit_admitted(CheckEmitRun* run)
{
    CheckEmitGrowth* growth = run->growth;
    const uint32_t index = lane_index();
    uint32_t admission = __atomic_load_n(&growth->admission, __ATOMIC_ACQUIRE) & ~1u;
    while (admission >> 2 <= index && (admission & 2u) == 0)
    {
        admission = lane_sleep(&growth->admission, admission);
    }
    const uint64_t parts = growth->parts;
    const uint32_t* const order = growth->order;
    uint32_t* const lowered = growth->lowered;
    CheckEmitLane* lane = run->lane_states + index;
    const uint64_t items = 2 * parts;
    uint64_t taken = admission >> 2 > index ? __atomic_fetch_add(&growth->ticket, 1, __ATOMIC_RELAXED) : items;
    for (; taken < items; taken = __atomic_fetch_add(&growth->ticket, 1, __ATOMIC_RELAXED))
    {
        check_program_emit_item(run, lane, index, taken, parts, order, lowered);
    }
}

// The flow's other lanes: each round's part on its lanes, until lane 0 ends the flow: the checker's deferred bodies and
// its merge's pass (check_deferred_follow, #151), or the emission's bodies' parts.
static void check_program_emit_follow(LaneFlow* flow)
{
    void* argument = NULL;
    for (uint32_t round = lane_flow_next(flow, &argument); round != 0; round = lane_flow_next(flow, &argument))
    {
        CheckEmitRun* run = check_deferred_follow(round, argument) ? NULL : argument;
        if (run != NULL && run->growth != NULL)
        {
            check_program_emit_admitted(run);
        }
        else if (run != NULL)
        {
            check_program_emit_lane(run);
        }
        lane_widen();
    }
}

// The bodies as one part on the calling thread, without the lanes' calls: one lane's emission, which a caller running in
// a group of lanes of its own may make.
static void check_program_emit_alone(CheckEmitRun* run)
{
    CheckEmitLane* lane = run->lane_states;
    CheckEmitPart* part = run->parts;
    part->lower.first = 0;
    part->lower.end = run->body_count;
    run->part_count = 1;
    lane->ready = check_program_emit_lane_prepare(run, lane, 0);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part, CHECK_EMIT_HALF_BOTH);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
}

// Takes the next part of `share`'s open epoch: its place in the epoch's order into *taken and true, or false when the
// epoch has none left or none is open. A failed exchange loads the ticket's latest value, another lane's take or a new
// epoch's, which the loop reads against the limit again.
static bool check_program_share_take(CheckEmitShare* share, uint64_t* taken)
{
    uint64_t ticket = __atomic_load_n(&share->ticket, __ATOMIC_ACQUIRE);
    bool open = true;
    bool took = false;
    while (open && !took)
    {
        const uint64_t limit = __atomic_load_n(&share->limit, __ATOMIC_ACQUIRE);
        open = (limit >> 32) == (ticket >> 32) && (uint32_t)ticket < (uint32_t)limit;
        took = open && __atomic_compare_exchange_n(&share->ticket, &ticket, ticket + 1, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
    }
    *taken = (uint32_t)ticket;
    return took;
}

// The part `taken` of `share`'s epoch, on its run's lane state `state` (a writer and arenas made at the state's first
// part, as a lane's: check_program_emit_lane_prepare), then counted done; the last part done publishes the epoch
// complete. Every access to the run, including its part count, precedes this part's acknowledgment: another part may
// publish complete as soon as this one increments done, letting the owner reuse the run and its arenas.
static void check_program_share_run(CheckEmitShare* share, uint64_t taken, uint32_t state)
{
    CheckEmitRun* run = share->run;
    CheckEmitLane* lane = run->lane_states + state;
    CheckEmitPart* part = run->parts + share->order[taken];
    lane->ready = lane->prepared ? lane->ready : check_program_emit_lane_prepare(run, lane, state);
    lane->prepared = true;
    if (lane->ready)
    {
        check_program_emit_part(run, lane, part, (CheckEmitHalf)share->half);
    }
    else
    {
        check_program_emit_part_refused(part);
    }
    const uint32_t parts = run->part_count;
    if (__atomic_add_fetch(&share->done, 1, __ATOMIC_ACQ_REL) == parts)
    {
        lane_publish(&share->complete, 2);
    }
}

// The bodies' parts shared on the units' board (#777, CheckProgramLanes.board): cut for `active` lanes as lanes of its
// own would cut them (parts_by_cost), opened as a new epoch of lane `index`'s share, taken by this lane (its run's first
// state, with the caller's arenas) and by the board's lanes that take no units, and waited for.
static void check_program_emit_shared(CheckEmitRun* run, CheckEmitBoard* board, uint32_t index, uint32_t active, uint32_t pause_microseconds)
{
    const uint64_t parts = parts_by_cost(run->costs, run->body_count, active, run->boundaries, run->orders[0]);
    for (uint64_t p = 0; p < parts; p += 1)
    {
        run->parts[p].lower.first = (uint32_t)run->boundaries[p];
        run->parts[p].lower.end = (uint32_t)run->boundaries[p + 1];
    }
    run->part_count = (uint32_t)parts;
    CheckEmitShare* share = board->shares + index;
    // With inlining (#1282), an epoch of the parts' lowering and then one of their code; else one of both halves.
    const CheckEmitHalf halves[2] = { run->inline_table != NULL ? CHECK_EMIT_HALF_LOWER : CHECK_EMIT_HALF_BOTH, CHECK_EMIT_HALF_CODE };
    for (uint32_t h = 0; h < (run->inline_table != NULL ? 2u : 1u); h += 1)
    {
        const uint64_t epoch = (__atomic_load_n(&share->ticket, __ATOMIC_RELAXED) >> 32) + 1;
        share->run = run;
        share->order = run->orders[0];
        share->half = halves[h];
        __atomic_store_n(&share->done, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&share->complete, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&share->limit, epoch << 32 | parts, __ATOMIC_RELEASE);
        __atomic_store_n(&share->ticket, epoch << 32, __ATOMIC_RELEASE);
        lane_advance(&board->generation);
        uint64_t taken = 0;
        while (check_program_share_take(share, &taken))
        {
            check_program_share_run(share, taken, 0);
            if (pause_microseconds != 0)
            {
                const struct timespec pause = { .tv_sec = 0, .tv_nsec = (long)pause_microseconds * 1000 };
                nanosleep(&pause, NULL);
            }
        }
        // There is at least one part (the caller has bodies and active > 1). Acquire its last publisher even if done
        // already counts every part: it may still be about to publish complete, which must not reach the next epoch.
        lane_wait(&share->complete, 0);
    }
}

// A lane of the units' board with no unit left to take (#777): takes the parts that the other lanes' emissions share,
// any lane's, until no lane takes units any more and no share has a part left. Its state in each emission is its place
// plus one (its arenas CheckProgramLanes.arenas' of the sharing lane, which that lane releases after its unit); a lane
// whose state would be past an emission's room takes none.
static void check_program_emit_help(CheckEmitBoard* board, uint32_t index)
{
    const uint32_t state = index + 1;
    bool helping = state < BUSTER_CHECK_EMIT_MAX_LANES;
    while (helping)
    {
        const uint32_t generation = __atomic_load_n(&board->generation, __ATOMIC_ACQUIRE) & ~1u;
        bool took = false;
        for (uint32_t i = 1; i <= board->lanes; i += 1)
        {
            CheckEmitShare* share = board->shares + (index + i) % board->lanes;
            uint64_t taken = 0;
            while (check_program_share_take(share, &taken))
            {
                check_program_share_run(share, taken, state);
                __atomic_add_fetch(&board->others, 1, __ATOMIC_RELAXED);
                took = true;
            }
        }
        // A share that opens, and a lane that stops taking units, advance the word after this lane read it. The wait is
        // for another lane's front end, a unit's preprocessing to its selection: long enough to sleep at once.
        helping = took || __atomic_load_n(&board->taking, __ATOMIC_ACQUIRE) != 0;
        if (!took && helping)
        {
            lane_sleep(&board->generation, generation);
        }
    }
}

// The asm templates' names' symbols (CheckProgramAsms.names), into `symbols`, by name: another name's the program's
// symbol of that link name (its asm label or its identifier: a name the emission makes is no template's to know), found
// in one pass over the program's symbols; a label's (or a `.comm`'s) the program's symbol of its name where the unit
// declares that symbol without defining it (`defined`, by object: `void f(void);` with a template's `f:`, or `extern int
// x;` with its `.comm x,4,4`), which the merge then defines where its template puts it, as clang's assembler binds the
// C references to the template's definition; another label's a local symbol of its own; or else an undefined global
// symbol of its own, as GNU as and clang make one for a name the unit doesn't define. The symbols of its own are
// numbered from `first` in the names' order, `*own` of them, and `externals` (with binding, or 0) gets each undefined
// one's spelling. A template's name of a symbol the unit defines is what clang's assembler makes of it (#1848):
// - A `.comm` of a variable the unit defines (`int x = 1;`, a tentative `int x;` without -fcommon) is clang's common
//   symbol of the name, of C's size at the `.comm`'s alignment, and the definition's bytes stay in its section without a
//   symbol: `commoned` (by name) marks it, and the name takes the program's symbol. Only an external, non-weak object
//   that isn't thread-local, an alias's or selectany's: clang's backend refuses a C common symbol's (-fcommon's
//   tentative definition's or the common attribute's) `.comm` of another size or alignment ("x redeclared as different
//   type"), and gives the others shapes of their own (a function's code without its symbol, a weak or thread-local
//   common), all refused by name (`*reason`), as check_program_sections refuses a read-only definition's.
// - A file-scope label of an external, non-weak definition (not an alias's or selectany's), without a binding directive
//   but `.globl`, is clang's assembler error, which `redefined` gets (and `*reason`): clang streams the module's
//   assembly before its definitions, and reports each definition of the label's name without a source location, a
//   function's and a common symbol's once and another variable's twice. One such name: several are refused by name, as
//   clang orders their reports by its module's emission, and so are a statement template's labels (clang reports an
//   earlier function's at the template), a static definition's (clang emits none for an unused one) and a local
//   `.comm`'s or `.lcomm`'s, whose binding clang reports too.
// - An absolute (`.set`) symbol of a definition's name, refused by name.
// False for those refusals and errors, or an arena without room. The pass is scalar: a unit's templates have few names
// (main.c's units 6), and it looks up only symbols' names of their lengths.
static bool check_program_emit_asm_symbols(const CheckProgram* program, const Parser* parser, const InternTable* identifiers, const String8* symbol_names,
                                           const uint8_t* defined, const uint8_t* commons, const uint8_t* weak, const uint32_t* symbol_aliases,
                                           const CheckProgramAsms* asms, SymbolId first, SymbolId* symbols, uint32_t* own, String8* externals,
                                           uint32_t* external_count, uint8_t* commoned, CheckProgramRedefined* redefined, const char** reason, Arena* scratch)
{
    const CheckInput* input = program->input;
    const uint32_t count = asms->name_count;
    uint32_t slots = 2;
    while (slots < 2 * (uint64_t)count)
    {
        slots *= 2;
    }
    const uint64_t mark = scratch->length;
    uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, slots);
    bool valid = table != 0;
    // Bit i: a name of i bytes (63 for 63 and more), as a symbol's must be to be looked up.
    uint64_t lengths = 0;
    if (valid)
    {
        memset(table, 0, (uint64_t)slots * sizeof(uint32_t));
        for (uint32_t n = 0; n < count; n += 1)
        {
            const String8 spelling = asms->names[n].spelling;
            uint32_t slot = (uint32_t)link_hash((const uint8_t*)spelling.pointer, (uint32_t)spelling.length) & (slots - 1);
            while (table[slot] != 0)
            {
                slot = (slot + 1) & (slots - 1);
            }
            table[slot] = n + 1;
            symbols[n] = 0;
            lengths |= UINT64_C(1) << (spelling.length < 63 ? spelling.length : 63);
        }
    }
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        const String8 renamed = symbol_names[s + 1];
        // The names the source gives (check_program_emit_given_name's): not a literal's, a compound literal's or a
        // block-scope static's, which the emission makes.
        const bool given = declaration != BUSTER_CHECK_NONE && !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) &&
                           !(renamed.length == 0 && check_sparse_get(input->declaration_bodies, declaration) != BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, declaration) == 0);
        const uint32_t identifier = given ? parser->tokens.names[declaration] : 0;
        const String8 spelling = !given ? (String8){ 0 } : renamed.length != 0 ? renamed :
                                 (String8){ .pointer = (char*)intern_bytes(identifiers, identifier), .length = intern_length(identifiers, identifier) };
        if (given && (lengths >> (spelling.length < 63 ? spelling.length : 63) & 1) != 0)
        {
            uint32_t slot = (uint32_t)link_hash((const uint8_t*)spelling.pointer, (uint32_t)spelling.length) & (slots - 1);
            while (table[slot] != 0 && (asms->names[table[slot] - 1].spelling.length != spelling.length ||
                                        memcmp(asms->names[table[slot] - 1].spelling.pointer, spelling.pointer, spelling.length) != 0))
            {
                slot = (slot + 1) & (slots - 1);
            }
            const uint32_t n = table[slot] != 0 ? table[slot] - 1 : BUSTER_CHECK_NONE;
            const CheckAsmName* name = n != BUSTER_CHECK_NONE ? &asms->names[n] : NULL;
            const bool conflict = name != NULL && name->label && (name->absolute || defined[program->symbol_objects[s]] != 0);
            // What clang's assembler makes of the template's definition of a C definition's name (#1848).
            const bool external = conflict && check_input_linkage(input->declaration_bits, declaration) == 2 && weak[s + 1] == 0 &&
                                  (symbol_aliases == 0 || symbol_aliases[s + 1] == 0) && (commons[s + 1] & BUSTER_CHECK_EMIT_SELECTANY) == 0;
            const bool function = conflict && input->declaration_kinds[declaration] == C_BINDING_FUNCTION;
            const bool thread = conflict && check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL);
            const bool common = conflict && !function && check_program_emit_common(commons[s + 1], input->common_tentatives);
            const bool comm = conflict && !name->absolute && (name->flags & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON;
            const uint8_t binding = conflict ? name->flags & BUSTER_LINK_SYMBOL_BINDING : 0;
            const bool plain = conflict && (name->directives & BUSTER_ASM_DIRECTIVE_LOCAL_COMMON) == 0 &&
                               ((name->directives & BUSTER_ASM_DIRECTIVE_BINDING) == 0 || binding == BUSTER_LINK_SYMBOL_GLOBAL);
            const bool reported = conflict && !name->absolute && !comm && name->file && external && plain && spelling.length < sizeof(redefined->name);
            if (conflict && name->absolute)
            {
                *reason = "inline assembly absolute symbols named as the unit's definitions";
                valid = false;
            }
            else if (comm && external && !function && !thread && !common)
            {
                commoned[n] = 1;
                symbols[n] = s + 1;
            }
            else if (comm)
            {
                *reason = common   ? "inline assembly .comm of the unit's common symbol, which clang's backend refuses for another size or alignment" :
                          function ? "inline assembly .comm of the unit's function" :
                                     "inline assembly .comm of a static, weak, thread-local or aliased definition";
                valid = false;
            }
            else if (reported && redefined->lines == 0)
            {
                memcpy(redefined->name, spelling.pointer, spelling.length);
                redefined->length = (uint32_t)spelling.length;
                redefined->lines = function || common ? 1 : 2;
            }
            else if (conflict)
            {
                *reason = reported ? "inline assembly labels named as several of the unit's definitions" : "inline assembly labels named as the unit's symbols";
                redefined->lines = 0;
                valid = false;
            }
            else if (n != BUSTER_CHECK_NONE)
            {
                symbols[n] = s + 1;
            }
        }
    }
    if (valid && redefined->lines != 0)
    {
        *reason = "inline assembly labels named as the unit's definitions, which clang's assembler refuses";
        valid = false;
    }
    for (uint32_t n = 0; valid && n < count; n += 1)
    {
        const CheckAsmName* name = &asms->names[n];
        const uint8_t binding = name->flags & BUSTER_LINK_SYMBOL_BINDING;
        // An absolute symbol has no link symbol: its value is the assembler's, and another template's reference to it was
        // refused by name (check_program_asm_names).
        if (name->absolute)
        {
            symbols[n] = 0;
            continue;
        }
        // An undefined global that no fixup refers to is a declaration, as `.globl sym2` alone: the direct path's binding
        // must not take it for a strong reference, and no object needs it without a relocation. One naming the program's
        // symbol still takes that symbol.
        const bool declaration = symbols[n] == 0 && !name->label && !name->referenced && binding != BUSTER_LINK_SYMBOL_WEAK;
        if (declaration)
        {
            symbols[n] = 0;
        }
        else if (symbols[n] == 0)
        {
            symbols[n] = first + *own;
            *own += 1;
            if (!name->label && externals != 0)
            {
                externals[*external_count] = name->spelling;
                *external_count += 1;
            }
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The instructions of `code` that have references (x86_encode's marked ones): its references' distinct instructions. A
// template's may have two, a displacement's and an immediate's; Tier 0's have one each.
static uint64_t check_program_emit_marked(const EmittedCode* code)
{
    uint64_t marked = 0;
    for (uint32_t i = 0; i < code->reference_count; i += 1)
    {
        marked += i == 0 || code->references[i].instruction != code->references[i - 1].instruction;
    }
    return marked;
}

// Body `b`'s patchable_function_entry entry (#1447), its function's atom `function` published: its label
// (CheckProgramCode.patchable_symbol_base's next) defined at the atom's start, where its NOP sled begins, and an 8-byte
// atom of writable data, zero, whose absolute fixup (R_X86_64_64 in an object, a relative relocation in a PIE) points there,
// as clang's entry does; its section (check_program_patchable's) for check_program_sections. Returns false for no room.
static bool check_program_emit_patchable_entry(CheckProgramCode* result, const CheckProgram* program, uint32_t b, AtomId function, Arena* bytes)
{
    const uint32_t k = result->patchable_count;
    const SymbolId label = result->patchable_symbol_base + k;
    uint8_t* entry = arena_push(bytes, 8, 8);
    const AtomId atom = entry != 0 ? link_add_atom(&result->link, BUSTER_LINK_CLASS_DATA, 3, 8, memset(entry, 0, 8), 0) : 0;
    const bool valid = atom != 0 && link_define_symbol(&result->link, label, function, 0, 0) &&
                       link_add_fixup(&result->link, atom, 0, BUSTER_LINK_FIXUP_ABS64, label, 0) != 0;
    if (valid)
    {
        result->link.symbols.source[label] = BUSTER_LINK_SOURCE_PROGRAM;
        result->patchable_atoms[k] = atom;
        result->patchable_functions[k] = function;
        result->patchable_sections[k] = check_program_patchable(program, b).section;
        result->patchable_count = k + 1;
    }
    else
    {
        result->status = CHECK_PROGRAM_RESOURCE;
    }
    return valid;
}

// `spelling`'s index in the asm names (`asms->names`), or BUSTER_CHECK_NONE: the unit's symbols' names are the names
// check_program_asm_names gathered, and a unit has few.
static uint32_t check_program_emit_asm_name(const CheckProgramAsms* asms, String8 spelling)
{
    uint32_t result = BUSTER_CHECK_NONE;
    for (uint32_t n = 0; result == BUSTER_CHECK_NONE && n < asms->name_count; n += 1)
    {
        const String8 other = asms->names[n].spelling;
        result = other.length == spelling.length && memcmp(other.pointer, spelling.pointer, spelling.length) == 0 ? n : BUSTER_CHECK_NONE;
    }
    return result;
}

// Publishes a template's assembled unit (check_program_asm_unit): copies its atoms' bytes into `bytes`, adds its atoms,
// symbols and fixups to the emission's link, and maps its symbols to the names' symbols (`asm_symbols`), defining its
// labels and the function's or helper's `symbol` at the unit's entry atom. Its entry atom, bytes and length are
// `*entry_atom`, `*entry_bytes` and `*entry_length`; `atoms` and `sections` (`atoms` of room, if not null) get each added
// atom and its section descriptor. False for an arena without room or an inconsistent unit.
// With `omit_frames`, the unit's .eh_frame atom, its symbols and its fixups are left out: a naked function's frame rows
// are the emission's unwind entry instead (#1848, CheckProgramNaked.frame_rows).
static bool check_program_emit_asm_unit(CheckProgramCode* result, const CheckProgramAsms* asms, const AsmResult* unit, const SymbolId* asm_symbols,
                                        SymbolId symbol, AtomId* atoms, ObjectAtomSection* sections, uint32_t* section_count, Arena* bytes, Arena* scratch,
                                        AtomId* entry_atom, uint8_t** entry_bytes, uint64_t* entry_length, bool omit_frames)
{
    const Link* source = &unit->link;
    const uint64_t mark = scratch->length;
    AtomId* mapped = source->atoms.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, AtomId, source->atoms.count) : 0;
    SymbolId* symbols = source->symbols.count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, source->symbols.count) : 0;
    bool valid = mapped != 0 && symbols != 0;
    *entry_atom = 0;
    *entry_bytes = 0;
    *entry_length = 0;
    if (valid)
    {
        memset(mapped, 0, source->atoms.count * sizeof(AtomId));
        memset(symbols, 0, source->symbols.count * sizeof(SymbolId));
    }
    for (uint32_t a = 1; valid && a < source->atoms.count; a += 1)
    {
        const uint8_t class = source->atoms.class[a];
        if (omit_frames && class == BUSTER_LINK_CLASS_EH_FRAME)
        {
            continue;
        }
        const uint64_t size = source->atoms.size[a];
        const bool zero = link_class_is_zero_fill(class);
        uint8_t* copy = size != 0 && !zero ? arena_push(bytes, size, 16) : 0;
        valid = size == 0 || zero || copy != 0;
        if (valid && copy != 0)
        {
            memcpy(copy, source->atoms.bytes[a], size);
        }
        mapped[a] = valid ? link_add_atom(&result->link, class, source->atoms.alignment_log2[a], size, copy, 0) : 0;
        valid = mapped[a] != 0;
        if (valid && atoms != 0 && sections != 0)
        {
            atoms[*section_count] = mapped[a];
            sections[*section_count] = unit->object_options.atom_sections != 0 ? unit->object_options.atom_sections[a] : (ObjectAtomSection){ 0 };
            *section_count += 1;
        }
    }
    for (uint32_t s = 1; valid && s < source->symbols.count; s += 1)
    {
        const uint32_t atom = source->symbols.atom[s];
        const bool defined = atom != 0;
        if (defined && atom < source->atoms.count && mapped[atom] == 0)
        {
            // An omitted .eh_frame's own label.
            continue;
        }
        const String8 spelling = check_asm_unit_name(unit, s);
        if (spelling.length == 0)
        {
            // A numeric label or `.`: an unnamed local symbol, defined at its atom.
            symbols[s] = defined && atom < source->atoms.count ?
                         link_add_symbol(&result->link, 0, mapped[atom], source->symbols.value[s], source->symbols.size[s],
                                         BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_NOTYPE, BUSTER_LINK_SOURCE_PROGRAM) :
                         0;
            valid = symbols[s] != 0;
        }
        else
        {
            const uint32_t n = check_program_emit_asm_name(asms, spelling);
            valid = n != BUSTER_CHECK_NONE;
            symbols[s] = valid ? asm_symbols[n] : 0;
            // An undefined global that no fixup refers to is a declaration, which check_program_emit_asm_symbols leaves
            // without a symbol: it has none to map here either.
            if (valid && symbols[s] == 0 && !defined)
            {
                continue;
            }
            valid = valid && symbols[s] != 0;
            // A `.comm` of a C definition's name (#1848): the static data defines the symbol at C's atom first, and
            // check_program_sections then moves it to the common's.
            const bool commoned = valid && defined && result->asm_commoned != 0 && result->asm_commoned[n] != 0;
            if (commoned)
            {
                valid = atom < source->atoms.count;
                result->commoned_symbols[result->commoned_count] = symbols[s];
                result->commoned_atoms[result->commoned_count] = valid ? mapped[atom] : 0;
                result->commoned_count += valid;
            }
            else if (valid && defined)
            {
                valid = atom < source->atoms.count && link_define_symbol(&result->link, symbols[s], mapped[atom], source->symbols.value[s], source->symbols.size[s]);
                if (valid)
                {
                    result->link.symbols.source[symbols[s]] = BUSTER_LINK_SOURCE_PROGRAM;
                }
            }
        }
        if (valid && symbols[s] != 0 &&
            (spelling.length == 0 || (unit->object_options.temporary_symbols != 0 && unit->object_options.temporary_symbols[s])))
        {
            result->unit_temporary[result->unit_temporary_count++] = symbols[s];
        }
    }
    for (uint32_t a = 1; valid && a < source->atoms.count; a += 1)
    {
        const uint32_t begin = source->atoms.fixup_begin[a];
        for (uint32_t f = 0; valid && mapped[a] != 0 && f < source->atoms.fixup_count[a]; f += 1)
        {
            const FixupId fixup = begin + f;
            const SymbolId target = source->fixups.target[fixup] < source->symbols.count ? symbols[source->fixups.target[fixup]] : 0;
            valid = target != 0 && link_add_fixup(&result->link, mapped[a], source->fixups.offset[fixup], source->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND,
                                                   target, link_fixup_addend(source, fixup)) != 0;
        }
    }
    if (valid && unit->entry_atom != 0 && unit->entry_atom < source->atoms.count)
    {
        *entry_atom = mapped[unit->entry_atom];
        *entry_bytes = result->link.atoms.bytes[*entry_atom];
        *entry_length = result->link.atoms.size[*entry_atom];
    }
    valid = valid && *entry_atom != 0;
    if (valid && symbol != 0)
    {
        valid = link_define_symbol(&result->link, symbol, *entry_atom, 0, *entry_length);
        if (valid)
        {
            result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The file-scope assembly's section groups (#1848), once check_program_emit_asm_unit published its atoms, the unit's
// atom a at result->asm_atoms[a - 1]: each group of the assembler's (asm_object_groups) as the program's, in `output`
// (CheckProgramCode.asm_groups), its signature the program's symbol of its name (asm_symbols, as the unit's symbols
// map), or, for a section's name, that section's atom, or, for a name the unit has no symbol of (a group's own, as
// `other_group` in `.section .data.x,"awG",@progbits,other_group,comdat`), none, which asks the object writer for the
// local symbol clang's assembler makes in the group's section. The scans are scalar: only a unit whose assembly makes
// groups takes them, and it makes a few. False for an arena without room.
static bool check_program_emit_asm_groups(CheckProgramCode* result, const CheckProgramAsms* asms, const AsmResult* unit, const SymbolId* asm_symbols, Arena* output)
{
    const Link* source = &unit->link;
    const LinkGroups* groups = &source->groups;
    const bool grouped = groups->count > 1 && groups->atom_count == source->atoms.count && result->asm_atom_count + 1 == source->atoms.count;
    result->asm_groups = grouped ? BUSTER_ARENA_PUSH_ARRAY(output, LinkGroup, groups->count) : 0;
    result->asm_atom_groups = grouped ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, result->asm_atom_count) : 0;
    bool valid = groups->count <= 1 || (result->asm_groups != 0 && result->asm_atom_groups != 0);
    if (valid && grouped)
    {
        result->asm_groups[0] = (LinkGroup){ 0 };
        for (uint32_t a = 0; a < result->asm_atom_count; a += 1)
        {
            result->asm_atom_groups[a] = groups->atom_groups[a + 1];
        }
    }
    for (uint32_t g = 1; valid && grouped && g < groups->count; g += 1)
    {
        const LinkGroup group = groups->data[g];
        const String8 spelling = group.signature != 0 ? check_asm_unit_name(unit, group.signature) : (String8){ 0 };
        const uint32_t n = spelling.length != 0 ? check_program_emit_asm_name(asms, spelling) : BUSTER_CHECK_NONE;
        const AtomId signature_atom = group.signature_atom != 0 && group.signature_atom < source->atoms.count ? result->asm_atoms[group.signature_atom - 1] : 0;
        SymbolId signature = n != BUSTER_CHECK_NONE ? asm_symbols[n] : 0;
        // A name the assembly has no symbol of may be C's (`int sig;` beside `.section ...,sig,comdat`), whose symbol is
        // the signature, as one module's symbol is clang's; a common one stays the group's own, which no group has.
        const bool named = signature == 0 && signature_atom == 0 && group.name != 0 && group.name < source->names.count;
        for (SymbolId s = 1; named && signature == 0 && s < result->link.symbols.count; s += 1)
        {
            const NameId other = result->link.symbols.name[s];
            const bool same = other != 0 && result->link.names.length[other] == source->names.length[group.name] &&
                              memcmp(result->link.names.bytes[other], source->names.bytes[group.name], source->names.length[group.name]) == 0 &&
                              (result->link.symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_COMMON;
            signature = same ? s : 0;
        }
        // A member's unit, the program's.
        AtomId member = signature_atom;
        for (uint32_t a = 0; member == 0 && a < result->asm_atom_count; a += 1)
        {
            member = result->asm_atom_groups[a] == g ? result->asm_atoms[a] : 0;
        }
        const NameId name = signature != 0 ? result->link.symbols.name[signature] :
                            group.name != 0 && group.name < source->names.count ? link_add_name(&result->link, source->names.bytes[group.name], source->names.length[group.name]) :
                                                                                  0;
        valid = name != 0 && result->link.error == LINK_ERROR_NONE;
        result->asm_groups[g] = (LinkGroup){
            .name = name,
            .signature = signature,
            .signature_atom = signature != 0 ? 0 : signature_atom,
            .unit = member != 0 ? result->link.atoms.unit[member] : 0,
            .flags = group.flags,
        };
    }
    result->asm_group_count = valid && grouped ? groups->count : 0;
    return valid;
}

// A unit helper's unwind entry, from the frame it published (CheckProgramAsm.saved and .frame) and its length: the pushes'
// and the reservation's transitions at the prologue, and the reservation's release and the pops' at the epilogue, with
// the body between them unknown. `length` is the unit's entry atom's size.
static UnwindError check_asm_unit_frame(uint32_t saved, uint32_t frame, uint32_t length, UnwindFunction* output)
{
    const uint32_t pushes = (uint32_t)__builtin_popcount(saved);
    const uint32_t prologue_count = pushes + 1;
    const uint32_t epilogue_count = pushes + 2;
    const uint32_t count = prologue_count + 1 + epilogue_count;
    X86Instruction records[2 * BUSTER_TIER0_CALLEE_SAVED + 4];
    X86Instruction prologue[1 + BUSTER_TIER0_CALLEE_SAVED];
    X86Instruction epilogue[1 + BUSTER_TIER0_CALLEE_SAVED + 1];
    uint8_t bytes[BUSTER_X86_ENCODER_CAPACITY(1)];
    uint32_t offsets[2 * BUSTER_TIER0_CALLEE_SAVED + 5];
    uint32_t at = 0;
    for (uint32_t r = 0; r < BUSTER_TIER0_CALLEE_SAVED; r += 1)
    {
        if ((saved >> r & 1) != 0)
        {
            const X86Instruction push = { .form = X86_FORM_PUSH_R64, .rm = tier0_callee_saved[r] };
            prologue[at] = push;
            records[at] = push;
            at += 1;
        }
    }
    prologue[at] = (X86Instruction){ .form = X86_FORM_SUB_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = (int32_t)frame };
    records[at] = prologue[at];
    at += 1;
    records[at++] = (X86Instruction){ .form = X86_FORM_RET };
    epilogue[0] = (X86Instruction){ .form = X86_FORM_ADD_RM64_IMM8, .rm = X86_REGISTER_RSP, .immediate = (int32_t)frame };
    uint32_t epilogue_length = 1;
    for (uint32_t r = BUSTER_TIER0_CALLEE_SAVED; r-- > 0;)
    {
        if ((saved >> r & 1) != 0)
        {
            epilogue[epilogue_length++] = (X86Instruction){ .form = X86_FORM_POP_R64, .rm = tier0_callee_saved[r] };
        }
    }
    epilogue[epilogue_length++] = (X86Instruction){ .form = X86_FORM_RET };
    memcpy(records + at, epilogue, (uint64_t)epilogue_length * sizeof(X86Instruction));
    // The prologue's and the epilogue's own lengths, record by record, as the encoder writes them.
    uint32_t sizes[2 * BUSTER_TIER0_CALLEE_SAVED + 3];
    bool valid = epilogue_length == epilogue_count && at + epilogue_length == count;
    uint32_t sizes_count = 0;
    for (uint32_t r = 0; valid && r < prologue_count; r += 1)
    {
        const int64_t encoded = x86_encode_scalar(prologue + r, 1, bytes, sizeof(bytes), 0, 0);
        valid = encoded > 0;
        sizes[sizes_count++] = encoded > 0 ? (uint32_t)encoded : 0;
    }
    for (uint32_t r = 0; valid && r < epilogue_length; r += 1)
    {
        const int64_t encoded = x86_encode_scalar(epilogue + r, 1, bytes, sizeof(bytes), 0, 0);
        valid = encoded > 0;
        sizes[sizes_count++] = encoded > 0 ? (uint32_t)encoded : 0;
    }
    uint32_t prologue_bytes = 0;
    for (uint32_t r = 0; valid && r < prologue_count; r += 1)
    {
        prologue_bytes += sizes[r];
    }
    uint32_t epilogue_bytes = 0;
    for (uint32_t r = prologue_count; valid && r < sizes_count; r += 1)
    {
        epilogue_bytes += sizes[r];
    }
    valid = valid && (uint64_t)prologue_bytes + epilogue_bytes <= length;
    if (valid)
    {
        uint32_t index = 0;
        offsets[index++] = 0;
        for (uint32_t r = 0; r < prologue_count; r += 1)
        {
            offsets[index] = offsets[index - 1] + sizes[r];
            index += 1;
        }
        offsets[index++] = length - epilogue_bytes;
        for (uint32_t r = prologue_count; r < sizes_count; r += 1)
        {
            offsets[index] = offsets[index - 1] + sizes[r];
            index += 1;
        }
        const Tier0Code code = {
            .records = records,
            .count = count,
            .frame_saved = saved,
            .frame_size = frame,
            .error = TIER0_OK,
        };
        UnwindFunction built;
        const UnwindError error = index == count + 1 ? unwind_tier0_frame(&code, offsets, length, &built) : UNWIND_ERROR_INVALID;
        if (error == UNWIND_OK)
        {
            *output = built;
        }
        return error;
    }
    return UNWIND_ERROR_INVALID;
}

// The objects the unit defines, a byte per object in `defined` (1 where it does), and how Tier 0 reaches each of the
// program's symbols, `access` 1 to symbol_count (Tier0Function.symbol_access): a symbol this unit doesn't define may be a
// shared library's, which the direct path, without copy relocations, reaches only through its GOT slot (an executable's
// writer relaxes the load where the symbol is in the image), and a thread-local is the image's by local-exec, or
// another's by initial-exec. The unit defines its bodies' functions, the objects it defines or tentatively defines (as
// check_data_prepare selects them), its aliases and ifuncs (#147), its literals and whatever has no external linkage
// but a weakref's own symbol, a weak reference to another's name (check_program_alias_external). The emission's column
// (check_program_emit_lanes) and check_program_access's share it. The objects it defines are found among the tokens
// that define or tentatively define, few of a unit's (#592), 64 at a time (the scalar path, `scalar`, takes every token).
static void check_program_object_access(const CheckProgram* program, uint8_t* access, uint8_t* defined, bool scalar)
{
    (void)scalar;
    const CheckInput* input = program->input;
    // Only symbols' objects are consumed below or by the linker's external-name collection.
    for (uint32_t s = 0; s < program->symbol_count; s += 1)
    {
        defined[program->symbol_objects[s]] = 0;
    }
    for (uint32_t b = 0; b < program->body_count; b += 1)
    {
        defined[program->bodies[b].object] = 1;
    }
    for (uint32_t first = 0; first < input->token_count; first += 64)
    {
        const uint64_t lanes = BUSTER_SCALAR_PATH(scalar) ? check_input_scan_all(first, input->token_count) :
                                        check_input_scan_bit(input->declaration_bits, first, input->token_count, BUSTER_CHECK_BITS_STATIC);
        for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            const uint32_t object = input->objects[token];
            if (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC) && input->declarations[token] == token && object < program->object_count && input->declaration_kinds[token] == C_BINDING_OBJECT &&
                (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_TENTATIVE)))
            {
                defined[object] = 1;
            }
        }
    }
    // GNU's aliases and ifuncs are definitions of the unit's (#147), whose symbols the emission defines at their bases,
    // and so are a cpu_dispatch group's f.ifunc and f, at its resolver (#1447).
    for (uint32_t k = 0; k < program->alias_count; k += 1)
    {
        defined[program->aliases[k].object] |= program->aliases[k].kind != CHECK_PROGRAM_WEAKREF;
    }
    for (uint32_t g = 0; g < program->cpu_group_count; g += 1)
    {
        defined[program->cpu_groups[g].reference] |= program->cpu_groups[g].dispatch != 0;
        defined[program->cpu_groups[g].dispatch] |= program->cpu_groups[g].dispatch != 0;
    }
    // So are a resolved group's R (or target_clones' f) of multiversioning by target strings (#1838), at its resolver,
    // and a target_clones body's own symbol's object, at its body.
    for (uint32_t g = 0; g < program->target_group_count; g += 1)
    {
        const CheckProgramTargetGroup* group = program->target_groups + g;
        defined[group->reference] |= group->resolved;
        defined[group->body] |= group->body != 0 && defined[group->clones];
        defined[group->clones] = group->clones != 0 ? group->resolved : defined[group->clones];
    }
    for (uint32_t s = 0; s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        const bool literal = declaration == BUSTER_CHECK_NONE || check_input_bit(program->input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND);
        // A weakref's own symbol (one without a target in the unit) is a weak reference to another's name, and so is a
        // weakref's target of internal linkage that the unit doesn't define and refers to through weakrefs alone.
        const bool internal = !literal && !defined[object] && check_input_linkage(program->input->declaration_bits, declaration) != 2;
        const bool external = internal && program->alias_count != 0 &&
                              (check_program_alias_external(program, object) || check_program_alias_weak_target(program, object));
        const bool own = literal || defined[object] || (check_input_linkage(program->input->declaration_bits, declaration) != 2 && !external);
        const bool thread_local = !literal && check_input_bit(program->input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL);
        access[s + 1] = thread_local ? (own ? BUSTER_TIER0_ACCESS_LOCAL_EXEC : BUSTER_TIER0_ACCESS_INITIAL_EXEC) : own ? BUSTER_TIER0_ACCESS_DIRECT : BUSTER_TIER0_ACCESS_GOT;
    }
}

// How Tier 0 reaches each symbol of a lowered program, as check_lower_finish numbered them (Tier0Function.symbol_access):
// one column, from `output`, for every body that `code`'s emission lowered of `program`, and `*defined_objects`, from
// `scratch` (check_program_object_access). It has an entry for every symbol of the emission's link (CheckProgramCode.link):
// the jump tables' and their labels' too, after the runtime functions', the unit's own and direct (Tier 0 compiles a
// symbol past its column as direct, but tier0_validate refuses one). The helpers are the unit's, and the C runtime's
// errno function isn't; libgcc's functions are linked into the image.
// Returns whether there was room. benchmark-backend-source (tier0_source_benchmark.c) compiles the lowered functions
// again with it: the emission's own column, made before its lanes, has the runtime functions' provisional symbols
// (check_lower_runtime), and goes with its scratch.
static bool check_program_access(CheckProgramCode* code, const CheckProgram* program, Arena* output, Arena* scratch, uint8_t** defined_objects)
{
    CheckProgramLowered* lowered = &code->lowered;
    const uint32_t count = program->body_count;
    const uint32_t numbered = program->symbol_count + lowered->helpers.count + lowered->initializers.count + lowered->runtime_symbol_count + 1;
    const uint32_t access_count = code->link.symbols.count > numbered ? code->link.symbols.count : numbered;
    uint8_t* access = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, access_count);
    uint8_t* defined = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1);
    const bool valid = access != 0 && defined != 0;
    *defined_objects = defined;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        check_program_object_access(program, access, defined, false);
        for (uint32_t r = 0; r < lowered->runtime_symbol_count; r += 1)
        {
            const CheckLowerRuntime runtime = (CheckLowerRuntime)lowered->runtime_order[r];
            const uint32_t symbol = lowered->runtime_symbols[runtime];
            // The C library's and libm's functions are a shared library's, which the GOT reaches.
            access[symbol] = runtime == CHECK_LOWER_RUNTIME_ERRNO || runtime == CHECK_LOWER_RUNTIME_MEMCPY ||
                                     (runtime >= CHECK_LOWER_RUNTIME_FMINF && runtime <= CHECK_LOWER_RUNTIME_FMAX) || runtime >= BUSTER_CHECK_LOWER_LIBM_FIRST
                                 ? BUSTER_TIER0_ACCESS_GOT
                                 : BUSTER_TIER0_ACCESS_DIRECT;
        }
        for (uint32_t b = 0; b < count; b += 1)
        {
            lowered->functions[b].symbol_access = access;
            lowered->functions[b].symbol_count = access_count;
        }
    }
    return valid;
}

// With `verify`, every function's records must pass x86_validate before they are sized and encoded (the tests', and
// #145's c-testsuite harness's, check of Tier 0's output), or the emission fails with CHECK_PROGRAM_BACKEND; without
// it, only their branch columns are checked (branch_sizing_validate_branches), which sizing needs inside each
// function, since Tier 0's records are valid by construction and x86_validate of all of them made branch sizing take
// 14 times the encoder's time on sqlite3.c (#532). With
// `rows`, each source body's raw source rows too (c_check_debug.c), from `output`. With `lanes`, on up to lanes->lanes
// lanes (above): one, and one more per BUSTER_CHECK_EMIT_LANE_TOKENS of the bodies' tokens beyond twice the largest
// body's, but with lanes->every_lane as many as there are parts for; without, on the calling thread alone. With
// `stage_clock` (the driver's --time-stages), each step's time is charged to its stage (c_check_clock.c).
// Returns an emission's `count` leases (worker_budget_release): false if one of them couldn't return its tokens.
static bool check_program_emit_return(WorkerBudgetLease* leases, uint32_t count)
{
    bool result = true;
    for (uint32_t i = 0; i < count; i += 1)
    {
        result = worker_budget_release(leases + i) && result;
    }
    return result;
}

static bool check_program_emit_timed(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                                     const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                                     CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify, CheckProgramLanes* lanes,
                                     CheckClock* stage_clock)
{
    (void)scalar;

    *result = (CheckProgramCode){ .token = BUSTER_CHECK_NONE };
    const bool timing = lanes != NULL && lanes->timing;
    const uint64_t started = timing ? check_program_lanes_now() : 0;
    const uint64_t mark = output->length;
    const uint64_t byte_mark = bytes->length;
    const uint64_t temporary = scratch->length;
    const uint32_t count = program->body_count;
    bool valid = output != bytes && output != scratch && bytes != scratch && program->status == CHECK_PROGRAM_OK;
    result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_INVALID;
    if (valid && count != 0 && !tier0_initialized)
    {
        valid = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    if (valid && count != 0 && unwind && !unwind_ready)
    {
        valid = unwind_initialize();
        result->status = valid ? CHECK_PROGRAM_OK : CHECK_PROGRAM_BACKEND;
    }
    // The runtime functions' names' lengths and hashes, which the lowering's preparation looks up (#442), where they
    // aren't baked: a lowering that finds them unmade measures and hashes the names itself.
    if (valid && !check_lower_runtime_names_ready)
    {
        check_lower_runtime_names_initialize();
    }
    check_clock_mark(stage_clock, CHECK_STAGE_BACKEND_SETUP);
    CheckLowerShared shared;
    const bool lowering = valid;
    if (valid)
    {
        const uint64_t prepare_start = stage_clock != NULL ? __rdtsc() : 0;
        valid = check_lower_prepare(&shared, &result->lowered, program, BUSTER_SCALAR_PATH(scalar), rows != NULL, lanes != NULL && lanes->tier1, output, scratch);
        check_clock_lower(stage_clock, CHECK_CLOCK_LOWER_PREPARE, stage_clock != NULL ? __rdtsc() - prepare_start : 0);
    }
    check_clock_mark(stage_clock, CHECK_STAGE_LOWER);
    // How Tier 0 reaches each symbol (Tier0Function.symbol_access), one column for every body: the program's symbols' as
    // check_program_object_access gives them. The helpers are the unit's, and the C runtime's errno function isn't;
    // libgcc's functions are linked into the image. The runtime's functions have their provisional symbols while the
    // bodies compile (check_lower_runtime), each its own, and the jump tables theirs after them
    // (CheckLowerShared.provisional_tables), the unit's own: direct.
    const uint32_t access_count = valid ? shared.provisional_tables + result->lowered.table_count : 0;
    uint8_t* access = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, access_count) : 0;
    uint8_t* defined = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->object_count + 1) : 0;
    valid = valid && access != 0 && defined != 0;
    if (valid)
    {
        memset(access, BUSTER_TIER0_ACCESS_DIRECT, access_count);
        check_program_object_access(program, access, defined, BUSTER_SCALAR_PATH(scalar));
        access[shared.provisional_runtimes + CHECK_LOWER_RUNTIME_ERRNO] = BUSTER_TIER0_ACCESS_GOT;
        access[shared.provisional_runtimes + CHECK_LOWER_RUNTIME_MEMCPY] = BUSTER_TIER0_ACCESS_GOT;
    }
    // Lane 0's writer's columns (CheckEmitRun.columns), in an arena of base pages reserved for the room they take
    // (check_lower_writer_bytes): the lanes' first arenas' scratch, or without lanes the calling thread's
    // (check_program_thread_columns), each kept while it has the room and otherwise reserved again; none without bodies,
    // whose writer never begins. Where that address space can't be reserved (RLIMIT_AS, or overcommit refused), they go
    // in the caller's scratch, as before #531, if it has their room: the calling thread runs the bodies when the lanes
    // can't, and then needs no address space past the caller's arenas.
    check_program_lanes_use(lanes, 1);
    Arena* reserved = lanes != NULL ? &lanes->arenas[0].scratch : &check_program_thread_columns;
    const uint64_t column_bytes = valid && count != 0 ? check_lower_writer_bytes(&shared, rows != NULL) : 0;
    if (column_bytes > reserved->capacity)
    {
        arena_release(reserved);
        // In base pages throughout: `small` past the capacity.
        *reserved = arena_reserve_small(column_bytes, BUSTER_ARENA_MAX_CAPACITY);
    }
    arena_pop_to(reserved, 0);
    Arena* columns = column_bytes != 0 && reserved->base != 0 ? reserved : scratch;
    // The lanes' run: the bodies' codes and code, their frames, and the parts' cut by the bodies' tokens.
    // (And the resolvers' of multiversioning by target strings, #1838, after them.)
    const uint32_t target_frames = unwind && program->target_group_count != 0 ? check_program_target_frames(program) : 0;
    const uint64_t frames = unwind ? (uint64_t)count + (valid ? result->lowered.helpers.count : 0) + target_frames : 0;
    CheckEmitRun run = {
        .program = program,
        .parser = parser,
        .shared = &shared,
        .lowered = &result->lowered,
        .lanes = lanes,
        .access = access,
        .access_count = access_count,
        .body_count = count,
        .scalar = BUSTER_SCALAR_PATH(scalar),
        .verify = verify,
        .unwind = unwind,
        .sources = rows != NULL,
        .output = output,
        .scratch = scratch,
        .bytes = bytes,
        .columns = columns,
    };
    run.codes = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, EmittedCode, count) : 0;
    run.bodies = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitBody, count) : 0;
    run.frames = valid && frames != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, UnwindFunction, frames) : 0;
    run.costs = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    // --tier1's outcomes, and its tables, once: the cost model's (x86_zen5.c), which its selection reads.
    const bool tier1 = valid && count != 0 && lanes != NULL && lanes->tier1;
    run.tier1_outcomes = tier1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTier1Outcome, count) : 0;
    run.tier1_counts = tier1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckTier1Counts, count) : 0;
    if (run.tier1_outcomes != 0)
    {
        // A body a failure before it left unreached has no outcome, never success.
        memset(run.tier1_outcomes, 0, (uint64_t)count * sizeof(CheckTier1Outcome));
    }
    // The unit's inline callees (#1282, #1398), unless --tier1-no-inline and --tier0-no-inline keep every call: Tier 1's
    // with --tier1, and Tier 0's for the bodies it compiles. A unit without them runs one epoch.
    CheckTier1Inline tier1_inline = { 0 };
    const bool tier1_inlines = tier1 && run.tier1_outcomes != 0 && !lanes->tier1_no_inline;
    const bool tier0_inlines = valid && (lanes == NULL || !lanes->tier0_no_inline);
    if ((tier1_inlines || tier0_inlines) && check_tier1_inline_table(program, &result->lowered, access_count, tier1_inlines, scratch, &tier1_inline))
    {
        run.inline_table = &tier1_inline;
        run.tier1_inline = tier1_inlines ? &tier1_inline : NULL;
        run.tier0_inline = tier0_inlines ? &tier1_inline : NULL;
    }
    if (tier1 && run.tier1_outcomes != 0 && !check_tier1_ready)
    {
        check_tier1_ready = x86_zen5_initialize();
        result->status = check_tier1_ready ? result->status : CHECK_PROGRAM_BACKEND;
    }
    valid = valid && (count == 0 || (run.codes != 0 && run.bodies != 0)) && (frames == 0 || run.frames != 0) && run.costs != 0 &&
            (!tier1 || (run.tier1_outcomes != 0 && run.tier1_counts != 0 && check_tier1_ready));
    uint64_t total = 0;
    uint64_t largest = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const uint32_t opening = program->bodies[b].opening;
        const uint64_t tokens = (uint64_t)parser->tree.parents[opening] - opening + 1;
        total += tokens;
        largest = tokens > largest ? tokens : largest;
        run.costs[b] = total;
    }
    // One lane, and one more per BUSTER_CHECK_EMIT_LANE_TOKENS of the bodies' tokens beyond twice the largest body's; no
    // more than the bodies, which are at least as many as the parts. On the units' board (#777), only while one of its
    // lanes takes no units: an emission that starts while every lane still takes units runs alone, its bodies one part.
    CheckEmitBoard* board = lanes != NULL && lanes->board != NULL && lanes->board_lane < lanes->board->lanes ? lanes->board : NULL;
    const bool sharing = board == NULL || __atomic_load_n(&board->taking, __ATOMIC_ACQUIRE) < board->lanes;
    const uint32_t most = lanes != NULL && lanes->lanes > 1 && sharing ? (lanes->lanes < BUSTER_CHECK_EMIT_MAX_LANES ? lanes->lanes : BUSTER_CHECK_EMIT_MAX_LANES) : 1;
    const uint64_t paid = lanes != NULL && lanes->every_lane ? UINT64_MAX : 1 + (total > 2 * largest ? total - 2 * largest : 0) / BUSTER_CHECK_EMIT_LANE_TOKENS;
    const uint32_t bodies = count != 0 ? count : 1;
    uint32_t active = most < bodies ? most : bodies;
    active = paid < active ? (uint32_t)paid : active;
    // In a command's flow (#905), no more than its session asks for: by default one lane per core, which a lazy session's
    // lane 1 has been counting meanwhile (lane_session_asked). An on-demand session's lanes start here if they haven't
    // (lane_session_start, #1444): the C driver starts them once a unit large enough to pay for them is preprocessed.
    if (active > 1 && lanes->flow != NULL)
    {
        lane_session_start();
        const uint32_t asked = lane_session_asked();
        active = asked < active ? asked : active;
    }
    // The jobserver's slots, taken without waiting (worker_budget_acquire). A team it gives fewer lanes than these, in a
    // command's flow whose lanes started, grows as other recipes' tokens come back (CheckEmitGrowth), up to the explicit
    // cap: its parts are cut for every lane it may take, and each growth is a lease of its own.
    WorkerBudgetLease leases[BUSTER_CHECK_EMIT_GROW_LEASES];
    for (uint32_t i = 0; i < BUSTER_CHECK_EMIT_GROW_LEASES; i += 1)
    {
        leases[i] = (WorkerBudgetLease){ .control = -1, .reply = -1, .child = -1 };
    }
    uint32_t lease_count = 1;
    const uint32_t wanted = active;
    if (valid && count != 0 && lanes != NULL && lanes->budget != NULL)
    {
        active = worker_budget_acquire(lanes->budget, active, leases);
    }
    const uint32_t granted = active;
    uint32_t room = active;
    if (valid && count != 0 && lanes != NULL && lanes->budget != NULL && active < wanted && board == NULL && lanes->flow != NULL &&
        lanes->budget->mode == WORKER_BUDGET_JOBSERVER)
    {
        const uint32_t cap = worker_budget_cap(lanes->budget);
        const uint32_t flow_lanes = lane_flow_lanes(lanes->flow);
        room = wanted < cap ? wanted : cap;
        room = flow_lanes < room ? flow_lanes : room;
        room = active > room ? active : room;
    }
    const bool growing = room > active;
    const uint64_t per_lane = growing ? BUSTER_CHECK_EMIT_GROW_PARTS_PER_LANE : BUSTER_LANE_PARTS_PER_LANE;
    const uint64_t part_room = room > 1 ? per_lane * room : 1;
    // On the board, a state for the sharing lane and one for each lane that may take its parts, its place plus one.
    const uint32_t states = board != NULL && active > 1 ? (board->lanes < BUSTER_CHECK_EMIT_MAX_LANES ? board->lanes + 1 : BUSTER_CHECK_EMIT_MAX_LANES) : room;
    // Each lane state's arenas are its index's (check_program_emit_lane_arenas).
    check_program_lanes_use(lanes, states);
    run.parts = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitPart, part_room) : 0;
    run.lane_states = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckEmitLane, states) : 0;
    run.boundaries = valid && room > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, part_room + 1) : 0;
    // Each lane's order of the parts, or on the board and for a growing team the one order every lane takes them in.
    for (uint32_t lane = 0; valid && room > 1 && lane < (board != NULL || growing ? 1 : active); lane += 1)
    {
        run.orders[lane] = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, part_room + 1);
        valid = run.orders[lane] != 0;
    }
    CheckEmitGrowth growth = { 0 };
    growth.rest = valid && growing ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, part_room + 1) : 0;
    growth.lowered = valid && growing ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, part_room) : 0;
    run.growth = growing ? &growth : NULL;
    valid = valid && run.parts != 0 && run.lane_states != 0 && (room == 1 || run.boundaries != 0) && (!growing || (growth.rest != 0 && growth.lowered != 0));
    check_clock_mark(stage_clock, CHECK_STAGE_EMIT_SETUP);
    CheckProgramAgain again = CHECK_PROGRAM_AGAIN_NONE;
    if (lanes != NULL)
    {
        lanes->squeezed = 0;
    }
    const uint64_t lanes_begin = timing ? check_program_lanes_now() : 0;
    uint32_t worked = 0;
    if (valid && count != 0)
    {
        // The lanes' run, or the calling thread's alone; lanes that couldn't start, or a part that ran out of room, go
        // again on the calling thread alone, from the arenas as they were (each body sets what it reads of the shared
        // tables before it reads it, and puts local_map's entries back). The run alone writes every field of lane 0's
        // state (check_program_emit_alone), and the lanes' start from zero (#1629).
        const uint64_t output_before = output->length;
        const uint64_t bytes_before = bytes->length;
        const uint64_t scratch_before = scratch->length;
        const uint64_t columns_before = columns->length;
        const bool laned = active > 1 || growing;
        if (laned)
        {
            memset(run.lane_states, 0, (uint64_t)states * sizeof(CheckEmitLane));
            if (growing)
            {
                active = check_program_emit_growing(lanes->flow, &run, &growth, room, active, lanes->budget, leases, &lease_count);
            }
            else if (board != NULL)
            {
                check_program_emit_shared(&run, board, lanes->board_lane, active, lanes->board_pause_microseconds);
            }
            else if (lanes->flow != NULL)
            {
                // The flow's lanes (#905), which a lazy session's lane 1 has been starting meanwhile: a round of them, or
                // if they didn't start, the bodies again on one lane.
                again = check_program_emit_round(lanes->flow, &run, active) ? CHECK_PROGRAM_AGAIN_NONE : CHECK_PROGRAM_AGAIN_START;
            }
            else
            {
                again = lane_run(active, check_program_emit_lane, &run, lanes->cpus) ? CHECK_PROGRAM_AGAIN_NONE : CHECK_PROGRAM_AGAIN_START;
            }
            valid = check_program_emit_return(leases, lease_count);
            for (uint32_t lane = 0; lanes != NULL && lane < states; lane += 1)
            {
                worked += run.lane_states[lane].prepared ? 1 : 0;
            }
            for (uint32_t p = 0; again == CHECK_PROGRAM_AGAIN_NONE && p < run.part_count; p += 1)
            {
                const CheckEmitPart* part = run.parts + p;
                const uint32_t failed_body = part->tier0_failed;
                const bool blocked = failed_body != part->lower.end && run.tier1_outcomes != NULL && run.tier1_outcomes[failed_body].fallback == CHECK_TIER1_FALLBACK_FORBIDDEN;
                const bool tier0 = failed_body != part->lower.end && !blocked && run.codes[failed_body].error == TIER0_ERROR_MEMORY;
                const bool code = part->code_failed != part->lower.end && run.bodies[part->code_failed].status == CHECK_PROGRAM_RESOURCE;
                again = part->lower.status == CHECK_PROGRAM_RESOURCE ? CHECK_PROGRAM_AGAIN_LOWERING :
                        tier0 ? CHECK_PROGRAM_AGAIN_TIER0 :
                        code ? CHECK_PROGRAM_AGAIN_CODE :
                               CHECK_PROGRAM_AGAIN_NONE;
            }
            for (uint32_t b = 0; again != CHECK_PROGRAM_AGAIN_NONE && run.tier1_outcomes != NULL && b < count; b += 1)
            {
                if (run.tier1_outcomes[b].fallback == CHECK_TIER1_FALLBACK_FORBIDDEN)
                {
                    again = CHECK_PROGRAM_AGAIN_NONE;
                }
            }
            check_clock_mark(stage_clock, CHECK_STAGE_LANES);
        }
        else
        {
            valid = check_program_emit_return(leases, lease_count);
        }
        if (valid && (!laned || again != CHECK_PROGRAM_AGAIN_NONE))
        {
            arena_pop_to(output, output_before);
            arena_pop_to(bytes, bytes_before);
            arena_pop_to(scratch, scratch_before);
            arena_pop_to(columns, columns_before);
            if (run.tier1_outcomes != NULL)
            {
                memset(run.tier1_outcomes, 0, (uint64_t)count * sizeof(CheckTier1Outcome));
            }
            if (run.inline_table != NULL)
            {
                // The bodies are lowered again (#1282), and the lanes' caches of their streams are stale (#1398).
                memset(tier1_inline.ready, 0, count);
                check_tier1_inline_renew(&tier1_inline);
            }
            run.stage_clock = stage_clock;
            // The parts read the TSC for the lowering's rows on one lane alone, whose clock takes them; on several, the
            // lanes' run is one stage, and they read none (CheckLowerShared.timed, false since check_lower_prepare).
            shared.timed = stage_clock != NULL;
            check_program_emit_alone(&run);
            worked = 1;
        }
    }
    // Also return the leases when an active-lane allocation failed before dispatch. A returned lease is a no-op.
    valid = check_program_emit_return(leases, lease_count) && valid;
    // Every body's code is done: the summaries (#2541) have no reader left.
    check_tier1_inline_release(&tier1_inline);
    if (lanes != NULL && lanes->budget != NULL && lanes->budget->return_failed)
    {
        result->status = CHECK_PROGRAM_BACKEND;
        result->reason = "could not return GNU make worker slots";
    }
    const uint64_t lanes_end = timing ? check_program_lanes_now() : 0;
    if (lanes != NULL)
    {
        lanes->active = active;
        lanes->grown = active - granted;
        lanes->worked = worked;
        lanes->parts = run.part_count;
        lanes->again = again;
        lanes->prepare_ns = lanes_begin - started;
        lanes->lanes_ns = lanes_end - lanes_begin;
    }
    // The lowering's result, in source order's order (check_lower_finish): its first failure, or its label symbols, its
    // runtime functions' symbols and its entries.
    CheckLowerPart* parts = valid && run.part_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckLowerPart, run.part_count) : 0;
    valid = valid && (run.part_count == 0 || parts != 0);
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        parts[p] = run.parts[p].lower;
    }
    if (valid && !check_lower_finish(&result->lowered, &shared, parts, run.part_count))
    {
        valid = false;
    }
    if (lanes != NULL && lanes->helper_reporting && lowering)
    {
        check_helper_report_add(&lanes->helper_report, program, &result->lowered, count, valid && result->lowered.status == CHECK_PROGRAM_OK);
    }
    if (lowering && result->lowered.status != CHECK_PROGRAM_OK)
    {
        check_lower_failed(&result->lowered, output, mark);
        result->status = result->lowered.status;
        result->token = result->lowered.token;
        result->backend_node = result->lowered.backend_node;
        result->reason = result->lowered.reason;
    }
    else if (valid)
    {
        result->lowered.count = count;
        result->lowered.output_bytes = output->length - mark;
    }
    check_clock_mark(stage_clock, CHECK_STAGE_LOWER);
    // The file-scope assembly's refusal (check_program_asm_plan_file), at its first statement, after the bodies' own.
    const AsmResult* file_unit = valid && result->lowered.asms.file_assembled && result->lowered.asms.file_reason == NULL ? &result->lowered.asms.file_unit : 0;
    if (valid && result->lowered.asms.file_reason != NULL)
    {
        result->status = CHECK_PROGRAM_UNSUPPORTED;
        result->reason = result->lowered.asms.file_reason;
        result->token = result->lowered.asms.file_token;
        valid = false;
    }
    uint8_t* alignments = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    String8* symbol_names = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + 1) : 0;
    // Each symbol's weak binding, and its constructor's and destructor's priorities (#147), two a symbol, each plus one (0
    // for none), with the declaration whose attribute gave it: the latest declaration's first, as clang takes it.
    uint8_t* weak = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    uint32_t* structors = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 2 * ((uint64_t)program->symbol_count + 1)) : 0;
    uint32_t* structor_tokens = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, 2 * ((uint64_t)program->symbol_count + 1)) : 0;
    // Each symbol's alias, ifunc or weakref (#147), its entry plus one: an alias's or ifunc's own symbol, and a weakref's
    // own one (a weak reference to its name); none in a unit without them. And whether a retain attribute (#147) keeps
    // its definition's section.
    uint32_t* symbol_aliases = valid && program->alias_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->symbol_count + 1) : 0;
    uint8_t* retained = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    bool retaining = false;
    // Each symbol's common attribute (#147) and what keeps it from being a common one: BUSTER_CHECK_EMIT_COMMON and the
    // others' bits (check_program_emit_commons).
    uint8_t* commons = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)program->symbol_count + 1) : 0;
    bool common = false;
    bool selecting = false;
    valid = valid && weak != 0 && structors != 0 && structor_tokens != 0 && (program->alias_count == 0 || symbol_aliases != 0) && retained != 0 &&
            commons != 0;
    result->function_atoms = valid && count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, count) : 0;
    // The declaring tokens, for the attributes' passes (one vector pass: cheaper than holding them through the
    // bodies' compiles), pushed last and given back after them.
    const uint64_t attributes_mark = scratch->length;
    uint32_t* declared = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->input->token_count + 16) : 0;
    valid = valid && alignments != 0 && symbol_names != 0 && declared != 0 && (count == 0 || result->function_atoms != 0);
    if (valid)
    {
        memset(alignments, 0, (uint64_t)program->symbol_count + 1);
        memset(symbol_names, 0, ((uint64_t)program->symbol_count + 1) * sizeof(String8));
        memset(weak, 0, (uint64_t)program->symbol_count + 1);
        memset(retained, 0, (uint64_t)program->symbol_count + 1);
        memset(commons, 0, (uint64_t)program->symbol_count + 1);
        memset(structors, 0, 2 * ((uint64_t)program->symbol_count + 1) * sizeof(uint32_t));
        // Find the tokens with attributes first, 16 at a time, then keep the declaring tokens. This avoids reading every
        // declaration's attributes and keeps parameters' retain obligations as well as static declarations'. The input's
        // columns are complete here, after the bodies' lanes, and the scan writes `declared` alone.
        uint32_t attributed = 0;
        const CheckInput* input = program->input;
        const uint32_t tokens = input->token_count;
        const uint32_t* declarations = input->declarations;
        const CheckSparseWide attributes = input->attributes;
        for (uint32_t first = check_input_attributed_next(attributes, 0, tokens, scalar); first < tokens; first = check_input_attributed_next(attributes, first + 16, tokens, scalar))
        {
            for (uint32_t selected = check_input_attributed_group(attributes, first, tokens, scalar); selected != 0; selected &= selected - 1)
            {
                const uint32_t token = first + (uint32_t)__builtin_ctz(selected);
                if (declarations[token] == token)
                {
                    declared[attributed++] = token;
                }
            }
        }
        // The first emitted variable under active #pragma clang section object settings, or BUSTER_CHECK_NONE (#619).
        // Static objects are conservative candidates under any active object setting; section-kind selection belongs
        // with actual section emission, so the refusal must not claim that this object would use the named section. A
        // function's sections, a text setting's and a section attribute's, are check_program_sections'.
        const CheckInputSections* sections = program->input->sections;
        uint32_t placed = BUSTER_CHECK_NONE;
        for (uint32_t i = 0; sections != NULL && placed == BUSTER_CHECK_NONE && i < sections->definition_count; i += 1)
        {
            const uint32_t token = sections->definitions[i];
            const uint32_t object = token < program->input->token_count ? program->input->objects[token] : 0;
            placed = object != 0 && object < program->object_count && program->object_symbols[object] != 0 ? token : placed;
        }
        for (uint32_t d = 0; valid && d < attributed && declared[d] <= placed; d += 1)
        {
            const uint32_t token = declared[d];
            const CheckInputAttributes* attributes = check_input_attributes_at(program->input, token);
            const uint32_t object = program->input->objects[token];
            if (object != 0 && object < program->object_count)
            {
                // The object's own symbol: a referenced weakref's is its target's (#147), which its attributes don't touch.
                const SymbolId taken = program->object_symbols[object];
                const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
                alignments[symbol] = attributes->alignment_log2 > alignments[symbol] ? attributes->alignment_log2 : alignments[symbol];
                for (uint32_t i = 0; valid && i < attributes->count; i += 1)
                {
                    const CheckInputAttribute* attribute = attributes->records + i;
                    if (symbol != 0 && !attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL && !check_input_bit(program->input->declaration_bits, token, BUSTER_CHECK_BITS_REGISTER))
                    {
                        const String8 previous = symbol_names[symbol];
                        valid = previous.length == 0 || (previous.length == attribute->text.length &&
                                memcmp(previous.pointer, attribute->text.pointer, previous.length) == 0);
                        if (valid)
                        {
                            symbol_names[symbol] = attribute->text;
                        }
                        else
                        {
                            result->status = CHECK_PROGRAM_INVALID;
                            result->token = attribute->name_token;
                        }
                    }
                    const CheckInputAttributeKind kind = attribute->kind;
                    if (symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_SELECTANY && defined[object])
                    {
                        // The weak definition's whole section, including its relocations, is selected by name.
                        // A declaration without a definition remains an ordinary external reference.
                        weak[symbol] = 1;
                        commons[symbol] |= BUSTER_CHECK_EMIT_SELECTANY;
                        selecting = true;
                    }
                    // A weak declaration's symbol is weak: the reader ignores one after the definition and refuses one
                    // without external linkage. A constructor's or destructor's priority is its argument, 65535 without
                    // one, past which LLVM takes 65535 (AsmPrinter's preprocessXXStructorList).
                    weak[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_WEAK;
                    // A weak_import declaration's symbol is a weak reference where the unit doesn't define it (#147, clang's
                    // isWeakImported, of declarations alone).
                    weak[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_WEAK_IMPORT && !defined[object];
                    const uint32_t structor = (kind == CHECK_ATTRIBUTE_DESTRUCTOR ? 1u : 0u) + 2 * symbol;
                    if (symbol != 0 && !attribute->ignored && (kind == CHECK_ATTRIBUTE_CONSTRUCTOR || kind == CHECK_ATTRIBUTE_DESTRUCTOR) &&
                        program->input->declaration_kinds[token] == C_BINDING_FUNCTION && (structors[structor] == 0 || structor_tokens[structor] < token))
                    {
                        const uint64_t priority = attribute->argument_count != 0 && attribute->resolved ? (uint64_t)attribute->value : 65535;
                        structors[structor] = (uint32_t)(priority < 65535 ? priority : 65535) + 1;
                        structor_tokens[structor] = token;
                    }
                    // A retain attribute keeps an emitted definition's section (the reader ignores one after the
                    // definition), and makes it one of its own (check_program_sections).
                    retained[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_RETAIN;
                    retaining |= retained[symbol];
                    // A common attribute makes its object's tentative definition a common symbol, and a section
                    // attribute keeps it a definition of its own (check_program_emit_commons).
                    commons[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_COMMON ? BUSTER_CHECK_EMIT_COMMON : 0;
                    commons[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_SECTION ? BUSTER_CHECK_EMIT_COMMON_SECTION : 0;
                    commons[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_NOCOMMON ? BUSTER_CHECK_EMIT_NOCOMMON : 0;
                    commons[symbol] |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_LOADER_UNINITIALIZED ? BUSTER_CHECK_EMIT_COMMON_LOADER : 0;
                    common |= symbol != 0 && !attribute->ignored && kind == CHECK_ATTRIBUTE_COMMON;
                }
            }
        }
        // A late redefine_extname keeps the declaration's asm-label fact, but an earlier emitted reference may have
        // fixed its original name already. Only the selection's compact live references can decide that; syntax
        // checking and deferred bodies must not guess from an earlier occurrence of the identifier alone.
        const CheckInputSymbolPragmas* symbol_pragmas = input->symbol_pragmas;
        for (uint32_t i = 0; valid && symbol_pragmas != NULL && i < symbol_pragmas->count; i += 1)
        {
            const CheckInputSymbolPragma* record = symbol_pragmas->records + i;
            const uint32_t object = record->declaration < input->token_count ? input->objects[record->declaration] : 0;
            const SymbolId symbol = object < program->object_count ? program->object_symbols[object] : 0;
            if (record->after_use && symbol != 0)
            {
                if (program->symbol_pragma_originals == NULL)
                {
                    result->status = CHECK_PROGRAM_UNSUPPORTED;
                    result->reason = "source-time symbol renaming without an emission selection";
                    result->token = record->token;
                    valid = false;
                }
                else if (program->symbol_pragma_originals[i])
                {
                    symbol_names[symbol] = (String8){ 0 };
                }
            }
        }
        // GNU's alias, ifunc and weakref attributes (#147): each entry's own symbol, and its name where it is a weakref's,
        // a weak reference to its target's name; a weakref's target's undefined symbol is weak where the unit refers to it
        // through weakrefs alone (BUSTER_CHECK_PROGRAM_ALIAS_WEAK). An alias or ifunc clang's code generation refuses (the
        // driver reports its error before the emission: check_driver_aliases), or one the emission doesn't take, is
        // refused at its attribute, in source order.
        // A late weak declaration (#1849) leaves the symbol of an object global where the selection found that clang's
        // code generation binds it before the declaration applies (check_program_weak_bindings), or can't tell.
        for (uint32_t i = 0; valid && program->weak_decided && i < program->weak_global_count; i += 1)
        {
            const uint32_t object = program->weak_globals[i];
            const SymbolId taken = object < program->object_count ? program->object_symbols[object] : 0;
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
            weak[symbol] = symbol != 0 ? 0 : weak[symbol];
        }
        // Two variables' definitions of one symbol name through an asm label (#1979): clang keeps the later one's storage
        // alone, which the emission doesn't.
        if (valid && program->label_refused != 0)
        {
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "a second definition of a variable's symbol name through an asm label";
            result->token = program->label_refused - 1;
            valid = false;
        }
        if (valid && program->weak_decided && program->weak_refused != 0)
        {
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "a weak declaration after a use that only code clang emits at the unit's end makes, through declarations before and after it";
            result->token = program->weak_refused - 1;
            valid = false;
        }
        // cpu_specific's and cpu_dispatch's names (#1447): f.ifunc for the references, f.X for each version's own.
        valid = valid && check_program_cpu_names(program, symbol_names, output);
        // Multiversioning by target strings' (#1838): f.ifunc for target's references, each version's own name, and
        // each target_clones body's.
        valid = valid && check_program_target_names(program, symbol_names, output);
        if (symbol_aliases != 0)
        {
            memset(symbol_aliases, 0, ((uint64_t)program->symbol_count + 1) * sizeof(uint32_t));
        }
        uint32_t refused = BUSTER_CHECK_NONE;
        for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
        {
            const CheckProgramAlias* alias = program->aliases + k;
            const SymbolId taken = program->object_symbols[alias->object];
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == alias->object ? taken : 0;
            const uint8_t state = program->alias_states[k];
            const bool mismatched = alias->error == CHECK_PROGRAM_ALIAS_THREAD_LOCAL;
            const bool failed = (mismatched ? (state & BUSTER_CHECK_PROGRAM_ALIAS_REFERENCED) != 0 : alias->error != CHECK_PROGRAM_ALIAS_OK) ||
                                (state & BUSTER_CHECK_PROGRAM_ALIAS_UNEMITTED) != 0 || (alias->kind != CHECK_PROGRAM_WEAKREF && alias->base == 0);
            refused = failed && alias->attribute < refused ? alias->attribute : refused;
            if (symbol != 0)
            {
                symbol_aliases[symbol] = k + 1;
                symbol_names[symbol] = alias->kind == CHECK_PROGRAM_WEAKREF ? alias->target : symbol_names[symbol];
                // An alias has its base's atom, so selectany changes its binding alone. It must not allocate an
                // empty COMDAT group before check_program_define_aliases places it in the base's section.
                commons[symbol] &= (uint8_t)~BUSTER_CHECK_EMIT_SELECTANY;
            }
            const SymbolId target = alias->kind == CHECK_PROGRAM_WEAKREF && alias->target_object != 0 ? program->object_symbols[alias->target_object] : 0;
            weak[target] |= target != 0 && (state & BUSTER_CHECK_PROGRAM_ALIAS_WEAK) != 0 && !defined[alias->target_object];
        }
        for (uint32_t k = 0; valid && refused != BUSTER_CHECK_NONE && k < program->alias_count; k += 1)
        {
            const CheckProgramAlias* alias = program->aliases + k;
            if (alias->attribute == refused)
            {
                result->status = CHECK_PROGRAM_UNSUPPORTED;
                result->reason = alias->error == CHECK_PROGRAM_ALIAS_THREAD_LOCAL ? "a reference to an alias whose thread-local storage isn't its definition's" :
                                 alias->kind == CHECK_PROGRAM_WEAKREF ? "a weakref without a target" :
                                                                         "an alias or ifunc that clang's code generation refuses";
                result->token = refused;
                valid = false;
            }
        }
        // A variable under active #pragma clang section object settings (#619) is refused, where no attribute's refusal
        // comes before it: the loop above stops at its token. Such a variable needn't have an attribute at all (a section
        // attribute of its own keeps it out), so the settings list them (CheckInputSections.definitions).
        if (valid && placed != BUSTER_CHECK_NONE)
        {
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "a definition under active '#pragma clang section' settings";
            result->token = placed;
            valid = false;
        }
    }
    arena_pop_to(scratch, attributes_mark);
    common |= program->input->common_tentatives;
    if (valid && common)
    {
        check_program_emit_commons(program, commons, BUSTER_SCALAR_PATH(scalar));
    }
    // A loader_uninitialized variable of external linkage that common or -fcommon (without nocommon or a section) would
    // make common is a weak definition, as clang's code has it (#147).
    for (uint32_t s = 1; valid && common && s <= program->symbol_count; s += 1)
    {
        const uint8_t bits = commons[s];
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s - 1]];
        const bool asked = (bits & BUSTER_CHECK_EMIT_COMMON) != 0 || (program->input->common_tentatives && (bits & BUSTER_CHECK_EMIT_NOCOMMON) == 0);
        weak[s] |= (bits & BUSTER_CHECK_EMIT_COMMON_LOADER) != 0 && asked && (bits & BUSTER_CHECK_EMIT_COMMON_SECTION) == 0 && declaration < program->input->token_count &&
                   check_input_linkage(program->input->declaration_bits, declaration) == 2 && !check_input_bit(program->input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL);
    }
    // An alias of a variable whose definition is a common symbol (#147, #1126's review): clang's integrated assembler
    // refuses it ("Common symbol 'x' cannot be used in assignment expr"), and a common's storage is the linker's, which no
    // symbol of -c's object can be placed in. It is refused at its attribute, as -c's output would be wrong.
    for (uint32_t k = 0; valid && common && k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        const uint32_t base = alias->kind == CHECK_PROGRAM_ALIAS && alias->error == CHECK_PROGRAM_ALIAS_OK ? alias->base : 0;
        const SymbolId taken = base != 0 && base < program->object_count ? program->object_symbols[base] : 0;
        const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == base ? taken : 0;
        const uint32_t declaration = symbol != 0 ? program->object_declarations[base] : BUSTER_CHECK_NONE;
        const CheckInput* input = program->input;
        const bool variable = declaration < input->token_count && input->declaration_kinds[declaration] != C_BINDING_FUNCTION &&
                              !check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) && check_input_linkage(input->declaration_bits, declaration) == 2 && !weak[symbol] && (symbol_aliases == 0 || symbol_aliases[symbol] == 0);
        if (variable && check_program_emit_common(commons[symbol], input->common_tentatives))
        {
            result->status = CHECK_PROGRAM_UNSUPPORTED;
            result->reason = "an alias of a common symbol, which clang's assembler refuses";
            result->token = alias->attribute;
            valid = false;
        }
    }
    // The retained and selectany symbols (#147), for check_program_sections, in symbol order; the usual unit pays
    // for neither counting pass nor output column.
    uint32_t retained_count = 0;
    uint32_t selectany_count = 0;
    for (uint32_t s = 1; valid && (retaining || selecting) && s <= program->symbol_count; s += 1)
    {
        retained_count += retained[s];
        selectany_count += (commons[s] & BUSTER_CHECK_EMIT_SELECTANY) != 0;
    }
    result->retained_symbols = valid && retained_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, retained_count) : 0;
    result->selectany_symbols = valid && selectany_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, selectany_count) : 0;
    valid = valid && (retained_count == 0 || result->retained_symbols != 0) && (selectany_count == 0 || result->selectany_symbols != 0);
    for (uint32_t s = 1; valid && (retained_count != 0 || selectany_count != 0) && s <= program->symbol_count; s += 1)
    {
        if (retained[s])
        {
            result->retained_symbols[result->retained_count++] = s;
        }
        if ((commons[s] & BUSTER_CHECK_EMIT_SELECTANY) != 0)
        {
            result->selectany_symbols[result->selectany_count++] = s;
        }
    }
    // Symbols already carry the checker's selected object identities. Assign each overload's ABI name once, after
    // asm labels (which override mangling), before external binding, static-local naming and object publication.
    CheckMangleSource mangling = {
        .input = program->input,
        .parser = parser,
        .identifiers = identifiers,
        .types = program->types,
        .output = scratch,
        .scalar = scalar,
    };
    Scratch mangle_scratch = { 0 };
    for (uint32_t i = 0; valid && i < program->input->graph.overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = program->input->graph.overload_candidates + i;
        const SymbolId symbol = candidate->object != 0 && candidate->object < program->object_count ? program->object_symbols[candidate->object] : 0;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && symbol != 0 && symbol_names[symbol].length == 0)
        {
            if (mangle_scratch.arena == NULL)
            {
                mangle_scratch = scratch_begin(scratch);
            }
            mangling.internal = check_input_linkage(program->input->declaration_bits, candidate->declaration) == 1;
            mangling.object_size_compat = candidate->object_sizes != NULL;
            const CheckMangleStatus status = check_mangle_name(&mangling, candidate->type, parser->tokens.names[candidate->declaration], candidate->declaration,
                                                               mangle_scratch.arena, &symbol_names[symbol]);
            valid = status == CHECK_MANGLE_OK;
            if (!valid)
            {
                result->status = status == CHECK_MANGLE_RESOURCE ? CHECK_PROGRAM_RESOURCE :
                                 status == CHECK_MANGLE_INVALID ? CHECK_PROGRAM_INVALID : CHECK_PROGRAM_UNSUPPORTED;
                result->reason = "this overloadable signature's ABI name (local/anonymous tags, variable arrays or calling convention)";
                result->token = candidate->declaration;
            }
        }
    }
    if (mangle_scratch.arena != NULL)
    {
        scratch_end(mangle_scratch);
    }
    // Tier 0's failures, the first body's: its own refusals are features it doesn't have yet (an entry without a
    // template, a mask stack beyond its bound, or an operand window beyond its registers: tier0_select_deep).
    uint32_t failed = count;
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        failed = run.parts[p].tier0_failed < failed && run.parts[p].tier0_failed != run.parts[p].lower.end ? run.parts[p].tier0_failed : failed;
    }
    if (valid && failed != count)
    {
        const CheckTier1Outcome outcome = run.tier1_outcomes != NULL ? run.tier1_outcomes[failed] : (CheckTier1Outcome){ 0 };
        if (outcome.fallback == CHECK_TIER1_FALLBACK_FORBIDDEN)
        {
            result->tier1_failure = outcome;
            result->status = outcome.category == CHECK_TIER1_RESOURCE_FAILURE ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
            result->backend_node = outcome.instruction;
        }
        else
        {
            const EmittedCode* code = run.codes + failed;
            const Tier0Error error = (Tier0Error)code->error;
            result->status = error == TIER0_ERROR_MEMORY ? CHECK_PROGRAM_RESOURCE :
                             error == TIER0_ERROR_UNSUPPORTED || error == TIER0_ERROR_DEPTH ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
            result->reason = error == TIER0_ERROR_DEPTH ? "Tier 0 mask depth or operand register window exceeded" :
                             error != TIER0_ERROR_UNSUPPORTED ? NULL : "this operation in Tier 0";
            result->backend_error = error;
            result->backend_node = code->node;
        }
        result->token = program->bodies[failed].declaration;
        valid = false;
    }
    uint64_t references = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        references += run.codes[b].reference_count;
        result->records += run.codes[b].count;
        result->deferred += run.codes[b].deferred;
    }
    // A failure before the rows' room leaves `rows` to the failure's reset below; the rows themselves are the bodies',
    // each body's after the ones before it, up to the first body whose code failed after Tier 0 (whose failure the
    // emission reports, unless the link's or its binding's comes first), past which no body has its code.
    uint32_t encoded = count;
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        const uint32_t code_failed = run.parts[p].code_failed;
        encoded = code_failed != run.parts[p].lower.end && code_failed < encoded ? code_failed : encoded;
    }
    if (valid && rows != NULL)
    {
        valid = check_program_emit_reserve_rows(rows, result->lowered.entries, count, output);
        rows->link = &result->link;
        for (uint32_t b = 0; valid && b < encoded; b += 1)
        {
            const CheckEmitBody* body = run.bodies + b;
            rows->first[b] = rows->count;
            rows->counts[b] = body->row_count;
            // (From its symbol: past a patchable entry's NOPs before it, #1447.)
            rows->sizes[b] = (uint32_t)(body->length - body->prefix);
            rows->bodies[b] = body->body_offset;
            const EmittedCode* code = run.codes + b;
            CheckDebugBody* value = rows->values + b;
            *value = (CheckDebugBody){ 0 };
            const uint32_t locals = result->lowered.functions[b].local_count;
            CheckDebugBinding* bindings = BUSTER_ARENA_PUSH_ARRAY(output, CheckDebugBinding, (uint64_t)locals + 1);
            Tier0Home* homes = BUSTER_ARENA_PUSH_ARRAY(output, Tier0Home, (uint64_t)locals + 1);
            uint8_t* classes = BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, (uint64_t)locals + 1);
            CheckDebugValueSpan* spans = BUSTER_ARENA_PUSH_ARRAY(output, CheckDebugValueSpan, (uint64_t)body->debug_span_count + 1);
            const uint32_t location_count = body->debug_location_count;
            CheckDebugLocation* locations = location_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDebugLocation, location_count) : NULL;
            uint32_t* location_first = location_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, (uint64_t)locals + 1) : NULL;
            valid = bindings != NULL && homes != NULL && classes != NULL && spans != NULL && (location_count == 0 || (locations != NULL && location_first != NULL));
            if (valid)
            {
                if (locals != 0)
                {
                    memcpy(bindings, result->lowered.debug_bindings[b], (uint64_t)locals * sizeof(*bindings));
                    if (code->local_homes != NULL && code->local_count == locals)
                    {
                        memcpy(homes, code->local_homes, (uint64_t)locals * sizeof(*homes));
                    }
                    else
                    {
                        memset(homes, 0, (uint64_t)locals * sizeof(*homes));
                    }
                    memcpy(classes, result->lowered.functions[b].local_classes, locals);
                }
                if (body->debug_span_count != 0)
                {
                    memcpy(spans, body->debug_spans, (uint64_t)body->debug_span_count * sizeof(*spans));
                }
                if (location_count != 0)
                {
                    memcpy(locations, body->debug_locations, (uint64_t)location_count * sizeof(*locations));
                    memcpy(location_first, body->debug_location_first, ((uint64_t)locals + 1) * sizeof(*location_first));
                }
                *value = (CheckDebugBody){
                    .bindings = bindings,
                    .homes = homes,
                    .classes = classes,
                    .spans = spans,
                    .locations = locations,
                    .location_first = location_first,
                    .local_count = locals,
                    .span_count = body->debug_span_count,
                    .location_count = location_count,
                };
            }
            if (body->row_count != 0)
            {
                memcpy(rows->offsets + rows->count, body->row_offsets, (uint64_t)body->row_count * sizeof(uint32_t));
                memcpy(rows->tokens + rows->count, body->row_tokens, (uint64_t)body->row_count * sizeof(uint32_t));
            }
            rows->count += body->row_count;
        }
    }
    const CheckProgramHelpers* helpers = &result->lowered.helpers;
    for (uint32_t h = 0; valid && h < helpers->count; h += 1)
    {
        references += helpers->items[h].code.reference_count;
        result->records += helpers->items[h].code.count;
        result->deferred += helpers->items[h].code.deferred;
    }
    // An executable's link has room for the runtime's records too: the crt objects', and the archives' members' that the
    // names the unit refers to without defining them may load (bind_capacity_needed), its symbols' and the runtime
    // functions' the lowering calls. With unwind tables, it has room for their EH_FRAME atom and a fixup per function.
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const CheckProgramAsms* asms = &result->lowered.asms;
    String8* externals = binding != NULL && valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)program->symbol_count + result->lowered.runtime_symbol_count +
                                                                                                    asms->name_count + 1) : 0;
    uint32_t external_count = 0;
    valid = valid && (binding == NULL || externals != 0);
    // The jump tables: an atom each, a fixup per entry, and their symbols and their labels'.
    uint64_t table_entries = 0;
    for (uint32_t t = 0; valid && t < result->lowered.table_count; t += 1)
    {
        table_entries += result->lowered.tables[t].count;
    }
    const uint64_t switch_symbols = valid ? (uint64_t)result->lowered.table_count + result->lowered.case_symbol_count : 0;
    // The asm templates' names' symbols (check_program_emit_asm_symbols), their own after the runtime's and the jump
    // tables' (CheckProgramLowered.case_symbol_base): its undefined globals among the names the unit refers to without
    // defining.
    const CheckLowerInitializers* pools = &result->lowered.initializers;
    const SymbolId asm_first = (SymbolId)(program->symbol_count + helpers->count + pools->count + result->lowered.runtime_symbol_count + 1 + switch_symbols);
    SymbolId* asm_symbols = valid && asms->name_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, asms->name_count) : 0;
    uint32_t asm_own = 0;
    valid = valid && (asms->name_count == 0 || asm_symbols != 0);
    // The names whose `.comm` makes a C definition's symbol a common one (#1848, CheckProgramCode.asm_commoned), and the
    // room to record their symbols and atoms as the templates' units are published.
    uint8_t* asm_commoned = valid && asms->name_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, asms->name_count) : 0;
    valid = valid && (asms->name_count == 0 || asm_commoned != 0);
    if (valid && asms->name_count != 0)
    {
        memset(asm_commoned, 0, asms->name_count);
        const char* reason = NULL;
        valid = check_program_emit_asm_symbols(program, parser, identifiers, symbol_names, defined, commons, weak, symbol_aliases, asms, asm_first, asm_symbols,
                                               &asm_own, externals, &external_count, asm_commoned, &result->redefined, &reason, scratch);
        result->status = valid ? result->status : result->redefined.lines != 0 ? CHECK_PROGRAM_BACKEND : reason != NULL ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_RESOURCE;
        result->reason = reason != NULL ? reason : result->reason;
    }
    uint32_t commoning = 0;
    for (uint32_t n = 0; valid && n < asms->name_count; n += 1)
    {
        commoning += asm_commoned[n];
    }
    result->asm_commoned = commoning != 0 ? asm_commoned : 0;
    result->commoned_symbols = commoning != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, commoning) : 0;
    result->commoned_atoms = commoning != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, commoning) : 0;
    result->commoned_count = 0;
    valid = valid && (commoning == 0 || (result->commoned_symbols != 0 && result->commoned_atoms != 0));
    // The program's symbols a template's label or `.comm` defines (check_program_emit_asm_symbols), by symbol: its name's
    // index plus one. Such a symbol is the template's definition, not an undefined name for the runtime to supply. A
    // `.comm` of a C definition's name (asm_commoned) isn't: the symbol is C's, which the static data defines first.
    bool asm_defining = false;
    for (uint32_t n = 0; valid && n < asms->name_count; n += 1)
    {
        asm_defining |= asms->names[n].label && asm_symbols[n] != 0 && asm_symbols[n] < asm_first && asm_commoned[n] == 0;
    }
    uint32_t* asm_defines = asm_defining ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)program->symbol_count + 1) : 0;
    valid = valid && (!asm_defining || asm_defines != 0);
    if (valid && asm_defining)
    {
        memset(asm_defines, 0, ((uint64_t)program->symbol_count + 1) * sizeof(uint32_t));
        for (uint32_t n = 0; n < asms->name_count; n += 1)
        {
            if (asms->names[n].label && asm_symbols[n] != 0 && asm_symbols[n] < asm_first && asm_commoned[n] == 0)
            {
                asm_defines[asm_symbols[n]] = n + 1;
            }
        }
    }
    for (uint32_t s = 0; valid && externals != 0 && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        // A weakref's own symbol (#147) refers to its name, which symbol_names holds, and a weakref's target referred to
        // through weakrefs alone is weak whatever its linkage.
        const bool weakref = symbol_aliases != 0 && symbol_aliases[s + 1] != 0 && program->aliases[symbol_aliases[s + 1] - 1].kind == CHECK_PROGRAM_WEAKREF;
        if (declaration != BUSTER_CHECK_NONE && !check_input_bit(program->input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND) && !defined[object] &&
            (asm_defines == 0 || asm_defines[s + 1] == 0) && (check_input_linkage(program->input->declaration_bits, declaration) == 2 || weakref || weak[s + 1]))
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            externals[external_count++] = symbol_names[s + 1].length != 0 ? symbol_names[s + 1] :
                                                                              (String8){ .pointer = (char*)intern_bytes(identifiers, identifier),
                                                                                         .length = intern_length(identifiers, identifier) };
        }
        else if (program->cfstring_class_own && object == program->cfstring_class)
        {
            // The class the unit's constant CFStrings point to (#148), which it doesn't declare.
            externals[external_count++] = (String8){ .pointer = (char*)check_program_cfstring_class_name, .length = sizeof(check_program_cfstring_class_name) - 1 };
        }
    }
    for (uint32_t r = 0; valid && externals != 0 && r < result->lowered.runtime_symbol_count; r += 1)
    {
        const char* spelling = check_lower_runtime_names[result->lowered.runtime_order[r]];
        externals[external_count++] = (String8){ .pointer = (char*)spelling, .length = strlen(spelling) };
    }
    const LinkCapacity runtime = binding != NULL && valid ? bind_capacity_needed(binding->runtime, externals, external_count, scratch, &name_bytes, &atom_bytes) :
                                                            (LinkCapacity){ 0 };
    // The defined functions' constructors' and destructors' entries, an atom of an init or fini array each (#147).
    uint32_t structor_count = 0;
    for (uint32_t b = 0; valid && b < count; b += 1)
    {
        const SymbolId symbol = check_program_body_symbol(program, b);
        structor_count += (structors[2 * symbol] != 0) + (structors[2 * symbol + 1] != 0);
    }
    // Templates that take directives publish their assembled units' tables too: their atoms (one is the helper's or the
    // naked function's own), symbols, names and fixups.
    uint64_t unit_atoms = 0;
    uint64_t unit_symbols = 0;
    uint64_t unit_names = 0;
    uint64_t unit_fixups = 0;
    uint64_t unit_wide = 0;
    for (uint32_t s = 0; valid && asms->plans != 0 && s < asms->plan_count; s += 1)
    {
        const CheckProgramAsm* plan = asms->plans + s;
        if (plan != 0 && plan->unit_assembled)
        {
            unit_atoms += plan->unit.link.atoms.count != 0 ? plan->unit.link.atoms.count - 1 : 0;
            unit_symbols += plan->unit.link.symbols.count != 0 ? plan->unit.link.symbols.count - 1 : 0;
            unit_names += plan->unit.link.names.count != 0 ? plan->unit.link.names.count - 1 : 0;
            unit_fixups += plan->unit.link.fixups.count;
            unit_wide += plan->unit.link.fixups.wide_count;
        }
    }
    for (uint32_t b = 0; valid && asms->naked != 0 && b < program->body_count; b += 1)
    {
        const CheckProgramNaked* naked = asms->naked + b;
        if (naked->unit_assembled)
        {
            unit_atoms += naked->unit.link.atoms.count != 0 ? naked->unit.link.atoms.count - 1 : 0;
            unit_symbols += naked->unit.link.symbols.count != 0 ? naked->unit.link.symbols.count - 1 : 0;
            unit_names += naked->unit.link.names.count != 0 ? naked->unit.link.names.count - 1 : 0;
            unit_fixups += naked->unit.link.fixups.count;
            unit_wide += naked->unit.link.fixups.wide_count;
        }
    }
    // The file-scope assembly's (check_program_asm_plan_file), whose atoms are counted apart: they come first.
    const uint64_t file_atoms = file_unit != 0 && file_unit->link.atoms.count != 0 ? file_unit->link.atoms.count - 1 : 0;
    if (file_unit != 0)
    {
        unit_symbols += file_unit->link.symbols.count != 0 ? file_unit->link.symbols.count - 1 : 0;
        unit_names += file_unit->link.names.count != 0 ? file_unit->link.names.count - 1 : 0;
        unit_fixups += file_unit->link.fixups.count;
        unit_wide += file_unit->link.fixups.wide_count;
    }
    // patchable_function_entry's entries (#1447, CheckProgramCode.patchable_atoms): an atom, a fixup, a label and its name
    // each.
    uint32_t patched = 0;
    for (uint32_t b = 0; valid && (program->patchable_key_count != 0 || program->patchable_count != 0) && b < count; b += 1)
    {
        patched += check_program_patchable(program, b).count != 0 ? 1 : 0;
    }
    result->patchable_atoms = patched != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, patched) : 0;
    result->patchable_functions = patched != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, patched) : 0;
    result->patchable_sections = patched != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, String8, patched) : 0;
    valid = valid && (patched == 0 || (result->patchable_atoms != 0 && result->patchable_functions != 0 && result->patchable_sections != 0));
    // cpu_dispatch's resolvers and their symbols, and cpu_specific's versions' other names (#1447, check_program_cpu_emit).
    uint64_t cpu_atoms = 0;
    uint64_t cpu_symbols = 0;
    uint64_t cpu_fixups = 0;
    check_program_cpu_room(program, &cpu_atoms, &cpu_symbols, &cpu_fixups);
    // And multiversioning by target strings' (#1838).
    check_program_target_room(program, &cpu_atoms, &cpu_symbols, &cpu_fixups);
    // And `#pragma weak A = T`'s symbols of undeclared A's (#1849, check_program_define_pragma_aliases), a name each.
    for (uint32_t i = 0; program->input->symbol_pragmas != NULL && i < program->input->symbol_pragmas->count; i += 1)
    {
        cpu_symbols += program->input->symbol_pragmas->records[i].alias_target != BUSTER_CHECK_NONE ? 1u : 0u;
    }
    const uint64_t atom_count = (uint64_t)data_capacity.atoms + count + helpers->count + pools->count + runtime.atoms + (frames != 0 ? 1 : 0) +
                                (valid ? result->lowered.table_count : 0) + file_atoms + structor_count + unit_atoms + patched + cpu_atoms;
    const uint64_t fixup_count = (uint64_t)data_capacity.fixups + references + runtime.fixups + frames + table_entries + structor_count + unit_fixups + patched +
                                 cpu_fixups;
    const uint64_t wide_count = (uint64_t)data_capacity.wide_addends + references + runtime.wide_addends + unit_wide;
    const uint64_t symbol_count = (uint64_t)program->symbol_count + helpers->count + pools->count + result->lowered.runtime_symbol_count + switch_symbols + asm_own + runtime.symbols +
                                  unit_symbols + patched + cpu_symbols;
    const uint64_t name_count = (uint64_t)program->symbol_count + helpers->count + pools->count + result->lowered.runtime_symbol_count + switch_symbols + asm_own + runtime.names +
                                unit_names + patched + cpu_symbols;
    valid = valid && atom_count < UINT32_MAX && fixup_count < UINT32_MAX && wide_count < UINT32_MAX && symbol_count < UINT32_MAX &&
            name_count < UINT32_MAX && (uint64_t)data_capacity.priorities + runtime.priorities + structor_count < UINT32_MAX;
    const LinkCapacity capacity = {
        .names = valid ? (uint32_t)name_count : 0,
        .atoms = valid ? (uint32_t)atom_count : 0,
        .symbols = valid ? (uint32_t)symbol_count : 0,
        .fixups = valid ? (uint32_t)fixup_count : 0,
        .wide_addends = valid ? (uint32_t)wide_count : 0,
        .priorities = valid ? data_capacity.priorities + runtime.priorities + structor_count : 0,
    };
    valid = valid && link_initialize(&result->link, output, output, capacity);
    // A block-scope static's declaration is in a body (declaration_bodies) and has no linkage; a block-scope extern or
    // function declaration has linkage, and its symbol is the file-scope entity's.
    const CheckInput* input = program->input;
    // Only a unit that makes a name, which most don't, needs the table of the names a made one must not be, and the
    // source's names in it first; the others add their names as they come.
    bool makes = false;
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        makes |= declaration != BUSTER_CHECK_NONE && check_program_emit_makes_name(input, declaration, symbol_names[s + 1]);
    }
    uint32_t slots = 2;
    while (makes && slots <= (uint64_t)program->symbol_count * 2 && slots < (UINT32_C(1) << 31))
    {
        slots *= 2;
    }
    CheckProgramNames names = { .mask = slots - 1 };
    names.taken = valid && makes ? BUSTER_ARENA_PUSH_ARRAY(scratch, NameId, slots) : 0;
    NameId* given = valid && makes ? BUSTER_ARENA_PUSH_ARRAY(scratch, NameId, program->symbol_count) : 0;
    valid = valid && (!makes || (names.taken != 0 && given != 0));
    if (names.taken != 0)
    {
        memset(names.taken, 0, (uint64_t)slots * sizeof(NameId));
    }
    // The names the source gives come first, so that no made name is one of them.
    for (uint32_t s = 0; valid && makes && s < program->symbol_count; s += 1)
    {
        const uint32_t declaration = program->object_declarations[program->symbol_objects[s]];
        const String8 renamed = symbol_names[s + 1];
        const bool made = declaration != BUSTER_CHECK_NONE && check_program_emit_makes_name(input, declaration, renamed);
        given[s] = 0;
        if (declaration != BUSTER_CHECK_NONE && !made)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            given[s] = renamed.length != 0 ? check_program_emit_given_name(&result->link, &names, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length, true) :
                                             check_program_emit_given_name(&result->link, &names, intern_bytes(identifiers, identifier),
                                                                           intern_length(identifiers, identifier), false);
            valid = given[s] != 0;
        }
    }
    for (uint32_t s = 0; valid && s < program->symbol_count; s += 1)
    {
        const uint32_t object = program->symbol_objects[s];
        const uint32_t declaration = program->object_declarations[object];
        NameId name = 0;
        uint8_t flags = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT;
        if (declaration != BUSTER_CHECK_NONE)
        {
            const uint32_t identifier = parser->tokens.names[declaration];
            const String8 renamed = symbol_names[s + 1];
            const uint32_t opening = check_sparse_get(input->declaration_bodies, declaration);
            if (check_input_bit(input->declaration_bits, declaration, BUSTER_CHECK_BITS_COMPOUND))
            {
                name = check_program_emit_made_name(&result->link, &names, (const uint8_t*)".compoundliteral", sizeof(".compoundliteral") - 1, scratch);
            }
            else if (renamed.length == 0 && opening != BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, declaration) == 0)
            {
                // The function's name: its symbol's asm label, or its identifier (--lower-all selects the statics of
                // an inline-only function, which has no symbol, and then no label).
                const uint32_t function = opening < input->token_count ? check_sparse_get(input->body_declarations, opening) : BUSTER_CHECK_NONE;
                const uint32_t function_object = function != BUSTER_CHECK_NONE ? input->objects[function] : 0;
                SymbolId function_symbol = function_object != 0 && function_object < program->object_count ? program->object_symbols[function_object] : 0;
                // (A multiversioned function's are clang's: its versions' references reach the group's IFUNC, #1838.)
                SymbolId version_symbol = 0;
                if (program->target_group_count != 0 && check_program_target_static_context(program, function_object, &version_symbol))
                {
                    function_symbol = version_symbol;
                }
                const String8 label = function_symbol != 0 ? symbol_names[function_symbol] : (String8){ 0 };
                const uint32_t function_identifier = function != BUSTER_CHECK_NONE ? parser->tokens.names[function] : 0;
                const uint8_t* context = label.length != 0 ? (const uint8_t*)label.pointer : intern_bytes(identifiers, function_identifier);
                const uint32_t context_length = label.length != 0 ? (uint32_t)label.length : intern_length(identifiers, function_identifier);
                const uint32_t own_length = intern_length(identifiers, identifier);
                const uint64_t base_length = (uint64_t)context_length + 1 + own_length;
                uint8_t* base = function != BUSTER_CHECK_NONE && base_length < UINT32_MAX - 12 ? arena_push(scratch, base_length, 1) : 0;
                if (base != 0)
                {
                    memcpy(base, context, context_length);
                    base[context_length] = '.';
                    memcpy(base + context_length + 1, intern_bytes(identifiers, identifier), own_length);
                    name = check_program_emit_made_name(&result->link, &names, base, (uint32_t)base_length, scratch);
                }
                // An asm label naming the static's own name, `f.n` (#2439), which a given name takes first, so that the
                // static is renamed: clang's module joins the label to the static where it emits the static's function
                // before the label's first use, and doesn't otherwise. The emission doesn't model that order: refused by
                // name (its name 0, which ends the symbols' loop). Only a renamed static looks.
                for (uint32_t t = 0; base != 0 && name != 0 && result->link.names.length[name] != base_length && t < program->symbol_count; t += 1)
                {
                    const String8 label = symbol_names[t + 1];
                    if (label.length == base_length && memcmp(label.pointer, base, base_length) == 0)
                    {
                        result->status = CHECK_PROGRAM_UNSUPPORTED;
                        result->reason = "an asm label naming a block-scope static's symbol, which clang joins to the static by its emission order";
                        result->token = declaration;
                        name = 0;
                    }
                }
            }
            else if (given != 0)
            {
                name = given[s];
            }
            else
            {
                name = renamed.length != 0 ? link_add_name(&result->link, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length) :
                                             link_add_name(&result->link, intern_bytes(identifiers, identifier), intern_length(identifiers, identifier));
            }
            const uint8_t kind = program->input->declaration_kinds[declaration] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION :
                                 check_input_bit(program->input->declaration_bits, declaration, BUSTER_CHECK_BITS_THREAD_LOCAL) ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            // Weak where a weak attribute makes it so, or a weakref's target that the unit doesn't define is referred to
            // through weakrefs alone (#147), whatever linkage its own declarations give it (the reader refuses weak on
            // one of internal linkage).
            const bool private_size_wrapper = check_program_object_size_inline(program->input, declaration);
            flags = kind | ((check_input_linkage(program->input->declaration_bits, declaration) != 2 || private_size_wrapper) && !weak[s + 1] ? BUSTER_LINK_SYMBOL_LOCAL :
                            weak[s + 1]                                      ? BUSTER_LINK_SYMBOL_WEAK :
                                                                               BUSTER_LINK_SYMBOL_GLOBAL);
            if (check_input_linkage(program->input->declaration_bits, declaration) == 2 && program->input->object_visibility != NULL && program->input->object_visibility[object] != 0)
            {
                flags |= (uint8_t)((program->input->object_visibility[object] - 1) << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT);
            }
            // An alias's symbol (#147) has its entry's type with its own binding and visibility; an ifunc's is global
            // whatever its declaration's linkage or weak attribute, as clang gives every ifunc external linkage; and a
            // weakref's own one is a weak reference to its name, of default visibility.
            const uint32_t entry = symbol_aliases != 0 ? symbol_aliases[s + 1] : 0;
            const CheckProgramAlias* alias = entry != 0 ? program->aliases + entry - 1 : NULL;
            flags = alias == NULL ? flags :
                    alias->kind == CHECK_PROGRAM_WEAKREF ? (uint8_t)(kind | BUSTER_LINK_SYMBOL_WEAK) :
                    alias->kind == CHECK_PROGRAM_IFUNC ? (uint8_t)(BUSTER_LINK_SYMBOL_IFUNC | BUSTER_LINK_SYMBOL_GLOBAL | (flags & BUSTER_LINK_SYMBOL_VISIBILITY)) :
                                                         (uint8_t)(alias->type | (flags & (BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_VISIBILITY)));
            // A common attribute's object (#147), or with -fcommon any, a tentative definition of external linkage that
            // nothing else makes a definition of its own (check_program_emit_common): no initializer, section, weak
            // attribute, alias or thread-local storage.
            const bool is_common = check_program_emit_common(commons[s + 1], program->input->common_tentatives) && alias == NULL &&
                                   kind == BUSTER_LINK_SYMBOL_OBJECT && (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_GLOBAL;
            flags = is_common ? (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_COMMON) : flags;
            // A symbol a template defines (asm_defines: a declaration's, `f:` or `.comm x,4,4`) is the template's label,
            // as clang's assembler makes it: its type the template's (none without `.type`, a `.comm`'s common), local
            // without a binding directive, and of the template's visibility or else the declaration's.
            const CheckAsmName* defining = asm_defines != 0 && asm_defines[s + 1] != 0 ? &asms->names[asm_defines[s + 1] - 1] : NULL;
            const uint8_t defining_visibility = defining != NULL ? defining->flags & BUSTER_LINK_SYMBOL_VISIBILITY : BUSTER_LINK_SYMBOL_DEFAULT;
            flags = defining == NULL ? flags :
                    (uint8_t)((defining->flags & (BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_BINDING)) |
                              (defining_visibility != BUSTER_LINK_SYMBOL_DEFAULT ? defining_visibility : flags & BUSTER_LINK_SYMBOL_VISIBILITY));
        }
        else if (object != 0 && object - 1 < parser->tokens.count &&
                 (parser->tokens.kinds[object - 1] == PARSE_KIND_STRING || check_input_function_name_token(parser, object - 1) ||
                  parser->tokens.names[object - 1] == C_NAME_BUILTIN_FILE || parser->tokens.names[object - 1] == C_NAME_BUILTIN_FILE_NAME ||
                  parser->tokens.names[object - 1] == C_NAME_BUILTIN_FUNCTION))
        {
            // A string literal's, a function name's array (__func__, __FUNCTION__, __PRETTY_FUNCTION__) or the string a
            // builtin of a name points to (__builtin_FILE's and its kin's, #148), which clang makes a private constant as
            // it makes a literal's.
            char spelling[48];
            const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_string_%u", parse_origin(parser, object - 1) + 1);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        }
        else if (program->cfstring_class_own && object == program->cfstring_class)
        {
            // The class the unit's constant CFStrings point to (#148, check_data_cfstring_keys), which the unit doesn't
            // declare: clang's external global of its name, undefined here.
            name = link_add_name(&result->link, (const uint8_t*)check_program_cfstring_class_name, (uint32_t)(sizeof(check_program_cfstring_class_name) - 1));
            flags = BUSTER_LINK_SYMBOL_GLOBAL | BUSTER_LINK_SYMBOL_NOTYPE;
        }
        else if (object != 0 && object - 1 < parser->tokens.count && program->cfstring_count != 0 &&
                 (parser->tree.roles[object - 1] == PARSE_ROLE_CLOSE_CALL || parser->tree.roles[object - 1] == PARSE_ROLE_OPEN_CALL))
        {
            // A constant CFString's record (at its call's `)`) or its UTF-16 characters (at its `(`), #148: private, as
            // clang's .L_unnamed_cfstring_ and its characters' constant are.
            char spelling[48];
            const bool record = parser->tree.roles[object - 1] == PARSE_ROLE_CLOSE_CALL;
            const int length = snprintf(spelling, sizeof(spelling), record ? ".Lbuster_cfstring_%u" : ".Lbuster_string_%u", parse_origin(parser, object - 1) + 1);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        }
        else if (object != 0 && object - 1 < parser->tokens.count && parser->tree.roles[object - 1] == PARSE_ROLE_LABEL)
        {
            // A label whose address the program takes (GNU C's &&label): a place in its function's code, defined below.
            char spelling[48];
            // (A target_clones copy's of the label, #2247, by its own symbol.)
            const bool copied = program->object_symbols[object] != s + 1;
            const uint32_t numbered = parse_origin(parser, object - 1) + 1;
            const int length = copied ? snprintf(spelling, sizeof(spelling), ".Lbuster_label_%u_%u", numbered, s + 1) :
                                        snprintf(spelling, sizeof(spelling), ".Lbuster_label_%u", numbered);
            name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
            flags = BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_NOTYPE;
        }
        else
        {
            result->status = CHECK_PROGRAM_INVALID;
            result->token = object != 0 ? object - 1 : 0;
        }
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == s + 1;
    }
    for (uint32_t h = 0; valid && h < helpers->count; h += 1)
    {
        const CheckProgramHelper* helper = helpers->items + h;
        char spelling[64];
        // An asm statement's helper is named after its statement (c_check_program_asm.c), a builtin's after its builtin.
        const int length = helper->assembly != 0 ? snprintf(spelling, sizeof(spelling), ".Lbuster_asm_%u", helper->assembly - 1) :
                                                   snprintf(spelling, sizeof(spelling), ".Lbuster_builtin_%u_%u", helper->id, helper->bytes);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_FUNCTION,
                                            BUSTER_LINK_SOURCE_NONE) == helper->symbol;
    }
    // Constant automatic initializers have one local readonly symbol per pooled byte image, before runtime symbols.
    for (uint32_t p = 0; valid && p < pools->count; p += 1)
    {
        char spelling[48];
        const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_initializer_%u", p);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_OBJECT,
                                            BUSTER_LINK_SOURCE_NONE) == pools->symbol_base + p;
        if (valid && binding != NULL)
        {
            result->link.symbols.source[pools->symbol_base + p] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    // The runtime's functions the lowering made symbols of its own for, in their order (CheckProgramLowered.runtime_order).
    for (uint32_t r = 0; valid && r < result->lowered.runtime_symbol_count; r += 1)
    {
        const CheckLowerRuntime runtime = (CheckLowerRuntime)result->lowered.runtime_order[r];
        const char* spelling = check_lower_runtime_names[runtime];
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)strlen(spelling));
        // libgcc's CPU model's objects are data, every other runtime entry a function (check_lower_runtime_object).
        const uint8_t kind = check_lower_runtime_object(runtime) ? BUSTER_LINK_SYMBOL_NOTYPE : BUSTER_LINK_SYMBOL_FUNCTION;
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_GLOBAL | kind, BUSTER_LINK_SOURCE_NONE) ==
                                 result->lowered.runtime_symbols[runtime];
    }
    // The jump tables' symbols, then their labels' (CheckProgramLowered.case_symbol_base): local, with names a source
    // can't spell, which -c's object leaves out as an assembler leaves clang's .LJTI and .LBB labels (check_driver_object).
    for (uint32_t k = 0; valid && k < switch_symbols; k += 1)
    {
        const bool table = k < result->lowered.table_count;
        char spelling[48];
        const int length = snprintf(spelling, sizeof(spelling), table ? ".Lbuster_switch_%u" : ".Lbuster_case_%u", table ? k : k - result->lowered.table_count);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        const uint8_t flags = BUSTER_LINK_SYMBOL_LOCAL | (table ? BUSTER_LINK_SYMBOL_OBJECT : BUSTER_LINK_SYMBOL_NOTYPE);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == result->lowered.case_symbol_base + k;
    }
    // The asm templates' own symbols, in their names' order, neither typed: a label local, the program's definition
    // (bind.c's), and an undefined name global.
    for (uint32_t n = 0; valid && n < asms->name_count; n += 1)
    {
        if (asm_symbols[n] >= asm_first)
        {
            const String8 spelling = asms->names[n].spelling;
            const NameId name = link_add_name(&result->link, (const uint8_t*)spelling.pointer, (uint32_t)spelling.length);
            const uint8_t asm_binding = asms->names[n].flags & BUSTER_LINK_SYMBOL_BINDING;
            const uint8_t chosen = asm_binding != BUSTER_LINK_SYMBOL_LOCAL ? asm_binding : asms->names[n].label ? BUSTER_LINK_SYMBOL_LOCAL : BUSTER_LINK_SYMBOL_GLOBAL;
            const uint8_t flags = (asms->names[n].flags & (BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_VISIBILITY)) | chosen;
            valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_NONE) == asm_symbols[n];
            if (valid && binding != NULL && asms->names[n].label)
            {
                result->link.symbols.source[asm_symbols[n]] = BUSTER_LINK_SOURCE_PROGRAM;
            }
        }
    }
    // A name that maps to one of the unit's program symbols takes the template's binding, visibility and `.size`
    // directives, as clang's assembled module does: `.weak target`, `.hidden target`, `.globl static_target` and
    // `.size target, 7` change the C symbol's own flags and size.
    for (uint32_t n = 0; valid && n < asms->name_count; n += 1)
    {
        const SymbolId symbol = asm_symbols[n];
        const CheckAsmName* name = &asms->names[n];
        const bool visibility = (name->flags & BUSTER_LINK_SYMBOL_VISIBILITY) != BUSTER_LINK_SYMBOL_DEFAULT;
        if (symbol == 0 || symbol >= asm_first || (name->directives == 0 && !visibility))
        {
            continue;
        }
        uint8_t* flags = &result->link.symbols.flags[symbol];
        if ((name->directives & BUSTER_ASM_DIRECTIVE_BINDING) != 0)
        {
            // Clang's integrated assembler refuses `.weak` on a defined C variable of external linkage, though a
            // function's is a warning, a static or block-scope variable's is accepted, and an extern's is a declaration.
            // The template's is refused by name too.
            const uint32_t object = program->symbol_objects[symbol - 1];
            const uint32_t declaration = object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
            const bool external = declaration != BUSTER_CHECK_NONE && declaration < program->input->token_count &&
                                  check_input_linkage(program->input->declaration_bits, declaration) == 2;
            if ((name->flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK && (*flags & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_FUNCTION &&
                object < program->object_count && defined[object] != 0 && external)
            {
                result->status = CHECK_PROGRAM_UNSUPPORTED;
                result->reason = "inline assembly .weak on a defined variable";
                valid = false;
                break;
            }
            *flags = (uint8_t)((*flags & ~BUSTER_LINK_SYMBOL_BINDING) | (name->flags & BUSTER_LINK_SYMBOL_BINDING));
        }
        if (visibility)
        {
            *flags = (uint8_t)((*flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | (name->flags & BUSTER_LINK_SYMBOL_VISIBILITY));
        }
    }
    // patchable_function_entry's labels (#1447, CheckProgramCode.patchable_symbol_base), after every symbol whose id the
    // lowering numbered: local, with names a source can't spell, each defined at its function's atom's start as the body is
    // published, the program's definitions for an executable's runtime to join.
    result->patchable_symbol_base = valid ? (SymbolId)result->link.symbols.count : 0;
    for (uint32_t k = 0; valid && k < patched; k += 1)
    {
        char spelling[48];
        const int length = snprintf(spelling, sizeof(spelling), ".Lbuster_patchable_%u", k);
        const NameId name = link_add_name(&result->link, (const uint8_t*)spelling, (uint32_t)length);
        valid = name != 0 && link_add_symbol(&result->link, name, 0, 0, 0, BUSTER_LINK_SYMBOL_LOCAL | BUSTER_LINK_SYMBOL_NOTYPE,
                                            binding != NULL ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE) == result->patchable_symbol_base + k;
    }
    // cpu_specific's and cpu_dispatch's symbols beyond the program's (#1447).
    valid = valid && (program->cpu_group_count == 0 || check_program_cpu_symbols(result, program, output));
    // Multiversioning by target strings' (#1838).
    valid = valid && (program->target_group_count == 0 || check_program_target_symbols(result, program, output));
    // Declarations whose asm labels name the made symbols of those groups (#2204).
    valid = valid && ((program->target_group_count == 0 && program->cpu_group_count == 0) || check_program_label_forwards(result, program, output));
    // Every symbol of the program's exists: an executable's runtime joins now, before the program's atoms, with the
    // symbols this emission and the static data will define marked as the program's definitions (bind.c).
    for (uint32_t b = 0; valid && binding != NULL && b < count + helpers->count + binding->data_count; b += 1)
    {
        const uint32_t object = b < count ? program->bodies[b].object : b >= count + helpers->count ? binding->data_objects[b - count - helpers->count].object : 0;
        const SymbolId symbol = b < count ? check_program_body_symbol(program, b) : b < count + helpers->count ? helpers->items[b - count].symbol :
                                object < program->object_count ? program->object_symbols[object] : 0;
        if (symbol != 0 && symbol < result->link.symbols.count)
        {
            result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    for (uint32_t k = 0; valid && binding != NULL && k < result->lowered.label_symbol_count; k += 1)
    {
        result->link.symbols.source[result->lowered.label_symbols[k]] = BUSTER_LINK_SOURCE_PROGRAM;
    }
    // The program's symbols the templates define (asm_defines), which the units' merge defines.
    for (uint32_t s = 1; valid && binding != NULL && asm_defines != 0 && s <= program->symbol_count; s += 1)
    {
        result->link.symbols.source[s] = asm_defines[s] != 0 ? BUSTER_LINK_SOURCE_PROGRAM : result->link.symbols.source[s];
    }
    // The aliases' and ifuncs' symbols (#147), which check_program_sections defines once the static data's atoms exist:
    // the program's in -c's link too, so that the static data may refer to a local one before it has an atom
    // (check_data_emit's local targets).
    for (SymbolId s = 1; valid && symbol_aliases != 0 && s <= program->symbol_count; s += 1)
    {
        if (symbol_aliases[s] != 0 && program->aliases[symbol_aliases[s] - 1].kind != CHECK_PROGRAM_WEAKREF)
        {
            result->link.symbols.source[s] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    for (uint32_t t = 0; valid && binding != NULL && t < result->lowered.table_count; t += 1)
    {
        result->link.symbols.source[result->lowered.case_symbol_base + t] = BUSTER_LINK_SOURCE_PROGRAM;
    }
    if (valid && binding != NULL && !bind_begin(binding->bind, &result->link, binding->runtime, binding->path, bytes, 1, binding->arena))
    {
        const NameId name = binding->bind->name;
        if (name != 0 && name < result->link.names.count)
        {
            snprintf(binding->failed_name, sizeof(binding->failed_name), "%.*s", (int)result->link.names.length[name],
                     (const char*)result->link.names.bytes[name]);
        }
        result->status = binding->bind->error == BIND_ERROR_FULL ? CHECK_PROGRAM_RESOURCE : CHECK_PROGRAM_BACKEND;
        result->reason = "binding against the C runtime failed";
        valid = false;
    }
    // Every reference of a runtime function's provisional symbol takes the function's (check_lower_finish numbered them):
    // only a part that called one has them.
    for (uint32_t p = 0; valid && p < run.part_count; p += 1)
    {
        for (uint32_t b = run.parts[p].lower.first; run.parts[p].lower.runtime_count != 0 && b < run.parts[p].lower.end; b += 1)
        {
            // A naked function's references are its templates' names (mapped below), which no runtime function's are.
            const EmittedCode* code = run.codes + b;
            const bool naked = asms->naked != 0 && asms->naked[b].planned;
            for (uint32_t i = 0; !naked && i < code->reference_count; i += 1)
            {
                const uint64_t provisional = (uint64_t)code->references[i].target - shared.provisional_runtimes;
                code->references[i].target = provisional < CHECK_LOWER_RUNTIMES ? result->lowered.runtime_symbols[provisional] : code->references[i].target;
            }
        }
    }
    // Then every reference of a jump table's provisional symbol takes the table's, in the functions that have tables
    // (CheckLowerTable.function, in their order). In this order each reference is rewritten once: a runtime function's
    // symbol is below the tables' provisional ones, while a table's final symbol is among the runtime functions'.
    for (uint32_t t = 0; valid && t < result->lowered.table_count; t += 1)
    {
        const uint32_t b = result->lowered.tables[t].function;
        const EmittedCode* code = run.codes + b;
        for (uint32_t i = 0; (t == 0 || result->lowered.tables[t - 1].function != b) && i < code->reference_count; i += 1)
        {
            const uint64_t table = (uint64_t)code->references[i].target - shared.provisional_tables;
            code->references[i].target = table < result->lowered.table_count ? result->lowered.case_symbol_base + (SymbolId)table : code->references[i].target;
        }
    }
    // Every reference of an asm template's (an asm helper's or a naked function's) takes its name's symbol: its target is
    // the name's index (check_program_asm_names).
    for (uint32_t f = 0; valid && asms->name_count != 0 && f < count + helpers->count; f += 1)
    {
        const bool assembly = f < count ? asms->naked != 0 && asms->naked[f].planned : helpers->items[f - count].assembly != 0;
        LinkReference* references = f < count ? run.codes[f].references : helpers->items[f - count].code.references;
        const uint32_t reference_count = f < count ? run.codes[f].reference_count : helpers->items[f - count].code.reference_count;
        for (uint32_t i = 0; valid && assembly && i < reference_count; i += 1)
        {
            valid = references[i].target < asms->name_count;
            references[i].target = valid ? asm_symbols[references[i].target] : 0;
        }
    }
    // The bodies' atoms, symbols and fixups in source order, each body's code as its lane made it, then the helpers', which
    // are encoded here: the labels' symbols, each function's after the ones before it (CheckProgramLowered.label_symbols),
    // and with unwind tables each function's frame, as its code is sized.
    check_clock_mark(stage_clock, CHECK_STAGE_EMIT_SETUP);
    result->unit_atoms = valid && unit_atoms != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, unit_atoms) : 0;
    result->unit_sections = valid && unit_atoms != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, ObjectAtomSection, unit_atoms) : 0;
    result->unit_atom_count = 0;
    result->unit_temporary = valid && unit_symbols != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, unit_symbols) : 0;
    result->unit_temporary_count = 0;
    valid = valid && (unit_atoms == 0 || (result->unit_atoms != 0 && result->unit_sections != 0)) &&
            (unit_symbols == 0 || result->unit_temporary != 0);
    // The file-scope assembly's atoms, symbols and fixups (check_program_asm_plan_file), before the functions', as clang's
    // assembly output has a module's asm first: each atom in its section in -c's object (CheckProgramCode.asm_sections).
    result->asm_atoms = valid && file_atoms != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, AtomId, file_atoms) : 0;
    result->asm_sections = valid && file_atoms != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, ObjectAtomSection, file_atoms) : 0;
    result->asm_atom_count = 0;
    valid = valid && (file_atoms == 0 || (result->asm_atoms != 0 && result->asm_sections != 0));
    if (valid && file_unit != 0)
    {
        AtomId entry_atom = 0;
        uint8_t* entry_bytes = 0;
        uint64_t entry_length = 0;
        valid = check_program_emit_asm_unit(result, asms, file_unit, asm_symbols, 0, result->asm_atoms, result->asm_sections, &result->asm_atom_count, bytes,
                                            scratch, &entry_atom, &entry_bytes, &entry_length, false);
        valid = valid && check_program_emit_asm_groups(result, asms, file_unit, asm_symbols, output);
        // Its atoms' bytes are the only room it takes that the link's capacity doesn't count, as the file-scope assembly's
        // copy did before (#619): a failure is the bytes arena's.
        result->status = valid ? result->status : CHECK_PROGRAM_RESOURCE;
    }
    // The templates of alignment and section directives alone: their data atoms and labels, without a helper to call.
    for (uint32_t s = 0; valid && asms->plans != 0 && s < asms->plan_count; s += 1)
    {
        const CheckProgramAsm* plan = asms->plans + s;
        if (plan->unit_assembled && plan->helper == BUSTER_CHECK_NONE)
        {
            AtomId entry_atom = 0;
            uint8_t* entry_bytes = 0;
            uint64_t entry_length = 0;
            valid = check_program_emit_asm_unit(result, asms, &plan->unit, asm_symbols, 0, result->unit_atoms, result->unit_sections, &result->unit_atom_count,
                                                bytes, scratch, &entry_atom, &entry_bytes, &entry_length, false);
            if (!valid)
            {
                result->status = CHECK_PROGRAM_BACKEND;
                result->reason = "an inline assembly unit could not be published";
            }
        }
    }
    uint32_t labels = 0;
    CheckEmitBody helper_body;
    for (uint32_t b = 0; valid && b < count + helpers->count; b += 1)
    {
        const bool source_body = b < count;
        // A helper's code as the emission takes it (emitted_from_tier0, below), a body's as its lane made it.
        EmittedCode helper_code = { 0 };
        const EmittedCode* code = source_body ? &run.codes[b] : &helper_code;
        const SymbolId symbol = source_body ? check_program_body_symbol(program, b) : helpers->items[b - count].symbol;
        const uint8_t alignment = source_body && alignments[symbol] > 5 ? alignments[symbol] - 1 : 4;
        const uint64_t work_mark = scratch->length;
        CheckEmitBody* body = source_body ? run.bodies + b : &helper_body;
        EmittedVerdict helper_verdict = { 0 };
        // An asm template's code: a naked function's or an asm statement's helper's, whose labels are defined below.
        const bool assembly = source_body ? asms->naked != 0 && asms->naked[b].planned : helpers->items[b - count].assembly != 0;
        // A template that takes directives is an assembled unit of its own (check_program_emit_asm_unit), not records.
        const CheckProgramAsm* unit_plan = !source_body && assembly ? asms->plans + (helpers->items[b - count].assembly - 1) : 0;
        const AsmResult* unit = unit_plan != 0 ? (unit_plan->unit_assembled ? &unit_plan->unit : 0) :
                                source_body && asms->naked != 0 && asms->naked[b].planned && asms->naked[b].unit_assembled ? &asms->naked[b].unit :
                                                                                                                                  0;
        if (unit != 0)
        {
            AtomId entry_atom = 0;
            uint8_t* entry_bytes = 0;
            uint64_t entry_length = 0;
            body->status = CHECK_PROGRAM_OK;
            body->token = BUSTER_CHECK_NONE;
            const char* reason = NULL;
            // (A naked function whose assembly takes directives has no NOP sled of patchable_function_entry's yet, #1447.)
            const bool patchable = source_body && patched != 0 && check_program_patchable(program, b).count != 0;
            // A naked function's own CFI (#1848): its rows are its unwind entry's, its unit's .eh_frame left out.
            const CheckProgramNaked* framed = source_body && asms->naked != 0 && asms->naked[b].framed ? asms->naked + b : NULL;
            const bool naked_frames = framed != NULL;
            valid = symbol != 0 && !patchable &&
                    check_program_emit_asm_unit(result, asms, unit, asm_symbols, symbol, result->unit_atoms, result->unit_sections,
                                                &result->unit_atom_count, bytes, scratch, &entry_atom, &entry_bytes, &entry_length, naked_frames);
            reason = patchable ? "patchable_function_entry on a naked function whose assembly takes directives" :
                     valid ? NULL : "an inline assembly unit could not be published";
            if (valid)
            {
                body->bytes = entry_bytes;
                body->length = entry_length;
                if (unwind)
                {
                    if (source_body)
                    {
                        // A naked function's unit says nothing of its frame but its own CFI's rows (#1848): without them its
                        // entry has the call's rule alone, as its records' path does.
                        valid = entry_length <= INT32_MAX;
                        run.frames[b] = (UnwindFunction){ .size = valid ? (uint32_t)entry_length : 0 };
                        if (valid && framed != NULL && framed->frame_row_count != 0)
                        {
                            run.frames[b].open = BUSTER_UNWIND_ROWS;
                            run.frames[b].rows = framed->frame_rows;
                            run.frames[b].row_count = framed->frame_row_count;
                        }
                    }
                    else
                    {
                        const UnwindError frame = entry_length <= INT32_MAX ? check_asm_unit_frame(unit_plan->saved, unit_plan->frame, (uint32_t)entry_length, &run.frames[b]) :
                                                                              UNWIND_ERROR_LIMIT;
                        valid = frame == UNWIND_OK;
                        reason = valid ? NULL : frame == UNWIND_ERROR_LIMIT ? "an inline assembly helper is too large for its unwind entry" :
                                                                              "an inline assembly helper's frame is not the one its unwind entry describes";
                    }
                }
            }
            if (valid)
            {
                if (source_body)
                {
                    result->function_atoms[b] = entry_atom;
                }
                result->code_bytes += entry_length;
            }
            else
            {
                result->status = patchable ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
                result->reason = reason != NULL ? reason : "an inline assembly unit could not be published";
                result->token = source_body ? program->bodies[b].declaration : BUSTER_CHECK_NONE;
                valid = false;
            }
            if (!source_body)
            {
                arena_pop_to(scratch, work_mark);
            }
            continue;
        }
        uint32_t* offsets = 0;
        if (!source_body)
        {
            // A helper's code, encoded on the calling thread as a body's is on its lane, its frame after the bodies': Tier
            // 0's, with its frame's events where the tables are written (an asm statement's helper's frame is the one its
            // plan built around the templates), and its rows in `output`, which the tables read after the loop.
            helper_code = emitted_from_tier0(&helpers->items[b - count].code, unwind, false, assembly, scratch);
            EmissionFrameRow* helper_rows = unwind && code->frame_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, EmissionFrameRow, code->frame_count) : 0;
            helper_body = (CheckEmitBody){ .status = CHECK_PROGRAM_OK };
            offsets = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)code->count + 1);
            body->fixup_ends = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, BUSTER_X86_ENCODER_FIXUP_CAPACITY(code->reference_count));
            const uint64_t byte_capacity = BUSTER_X86_ENCODER_CAPACITY(code->count);
            const uint64_t before = bytes->length;
            uint8_t* encoded = arena_push(bytes, byte_capacity, 16);
            bool encodes = offsets != 0 && body->fixup_ends != 0 && encoded != 0 && (!unwind || code->frame_count == 0 || helper_rows != 0);
            body->status = encodes ? CHECK_PROGRAM_OK : CHECK_PROGRAM_RESOURCE;
            body->token = BUSTER_CHECK_NONE;
            CheckProgramStatus failure = CHECK_PROGRAM_BACKEND;
            if (encodes && verify && x86_validate(code->records, code->count) != code->count)
            {
                body->status = CHECK_PROGRAM_BACKEND;
                body->reason = "internal compiler error: a record of Tier 0's fails x86_validate";
                encodes = false;
            }
            if (encodes)
            {
                const BranchSizingCheck shape = BUSTER_SCALAR_PATH(scalar) ? branch_sizing_validate_branches_scalar(code->records, code->count, code->branches, code->targets, code->branch_count) :
                                                         branch_sizing_validate_branches(code->records, code->count, code->branches, code->targets, code->branch_count);
                const BranchSizing sizing = shape.error == BRANCH_SIZING_OK ? size_branches(code->records, code->count, code->branches, code->targets, code->branch_count, offsets) : (BranchSizing){ .error = shape.error };
                const int64_t length = sizing.error != BRANCH_SIZING_OK ? -1 :
                                       BUSTER_SCALAR_PATH(scalar) ? x86_encode_scalar(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends) :
                                                x86_encode(code->records, code->count, encoded, byte_capacity, 0, body->fixup_ends);
                encodes = length >= 0;
                if (encodes && unwind)
                {
                    const char* reason = 0;
                    failure = check_program_emit_frame_checked(code, offsets, length, verify, BUSTER_SCALAR_PATH(scalar), helper_rows, scratch, &run.frames[b], &reason, &helper_verdict);
                    body->frame_verdict = helper_verdict.error != EMITTED_VERIFY_OK ? &helper_verdict : NULL;
                    encodes = failure == CHECK_PROGRAM_OK;
                    failure = encodes ? CHECK_PROGRAM_BACKEND : failure;
                    body->reason = encodes ? body->reason : reason;
                }
                if (encodes)
                {
                    const uint64_t end = (uint64_t)(encoded - bytes->base) + (uint64_t)length;
                    arena_pop_to(bytes, end > before ? end : before);
                    body->bytes = encoded;
                    body->length = (uint64_t)length;
                }
                else
                {
                    body->status = failure;
                }
            }
        }
        if (body->status != CHECK_PROGRAM_OK)
        {
            if (body->status == CHECK_PROGRAM_BACKEND && body->frame_verdict != NULL)
            {
                check_program_emit_frame_failure(code, b, body->frame_verdict);
            }
            result->status = body->status;
            result->reason = body->reason != NULL ? body->reason : result->reason;
            result->token = body->status == CHECK_PROGRAM_RESOURCE ? result->token : body->token;
            valid = false;
        }
        if (valid)
        {
            // A function with patchable_function_entry's NOPs (#1447, CheckEmitBody.sled) has its symbol after the first
            // `prefix` of them, which the atom's alignment comes before, as clang's: its entry's label is the atom's start.
            const AtomId atom = link_add_atom(&result->link, BUSTER_LINK_CLASS_TEXT, alignment, body->length, body->bytes, 0);
            valid = atom != 0 && symbol != 0 && link_define_symbol(&result->link, symbol, atom, body->prefix, body->length - body->prefix) &&
                    link_add_code_fixups(&result->link, atom, code->records, code->count, body->fixup_ends,
                                         assembly ? check_program_emit_marked(code) : code->reference_count, code->references, code->reference_count);
            if (valid && source_body && body->sled != 0)
            {
                valid = check_program_emit_patchable_entry(result, program, b, atom, bytes);
            }
            // The labels its templates define, each at its record's offset (its lane's, for a naked function): the
            // program's definitions.
            const CheckProgramAsmLabel* asm_labels = !assembly ? 0 : source_body ? asms->naked[b].labels : asms->plans[helpers->items[b - count].assembly - 1].labels;
            const uint32_t asm_label_count = !assembly ? 0 : source_body ? asms->naked[b].label_count : asms->plans[helpers->items[b - count].assembly - 1].label_count;
            for (uint32_t k = 0; valid && k < asm_label_count; k += 1)
            {
                const uint32_t record = asm_labels[k].record;
                const uint32_t offset = source_body ? body->asm_offsets[k] : record < code->count ? offsets[record] : record == code->count ? (uint32_t)body->length : UINT32_MAX;
                const SymbolId label = asm_symbols[asm_labels[k].name];
                valid = offset != UINT32_MAX && link_define_symbol(&result->link, label, atom, offset, 0);
                if (valid)
                {
                    result->link.symbols.source[label] = BUSTER_LINK_SOURCE_PROGRAM;
                }
            }
            // Its asm bodies' labels (#1839, EmittedCode.asm_label_records), each at its record's offset in the body: the
            // program's definitions, as a helper's template's labels are.
            for (uint32_t k = 0; valid && source_body && !assembly && k < code->asm_label_count; k += 1)
            {
                const uint32_t name = code->asm_label_names[k];
                const SymbolId label = name < asms->name_count ? asm_symbols[name] : 0;
                valid = label != 0 && body->asm_offsets[k] != UINT32_MAX && link_define_symbol(&result->link, label, atom, body->asm_offsets[k], 0);
                if (valid)
                {
                    result->link.symbols.source[label] = BUSTER_LINK_SOURCE_PROGRAM;
                }
            }
            // Each label at its resolved offset, after branch sizing, including an absent label's entry placeholder.
            for (uint32_t k = 0; valid && source_body && k < code->label_count; k += 1)
            {
                valid = labels < result->lowered.label_symbol_count && body->label_offsets[k] != UINT32_MAX &&
                        link_define_symbol(&result->link, result->lowered.label_symbols[labels], atom, body->label_offsets[k], 0);
                if (valid)
                {
                    result->link.symbols.source[result->lowered.label_symbols[labels]] = BUSTER_LINK_SOURCE_PROGRAM;
                }
                labels += 1;
            }
            if (valid)
            {
                result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
            }
            if (source_body)
            {
                result->function_atoms[b] = atom;
            }
            result->code_bytes += body->length;
        }
        if (!source_body)
        {
            arena_pop_to(scratch, work_mark);
        }
    }
    // Copy bytes into the byte arena, whose lifetime and rollback contract cover every published atom.
    for (uint32_t p = 0; valid && p < pools->count; p += 1)
    {
        const CheckLowerInitPool* pool = pools->items + p;
        uint8_t* data = arena_push(bytes, pool->size, 1);
        const AtomId atom = data != NULL ? link_add_atom(&result->link, BUSTER_LINK_CLASS_RODATA, 0, pool->size,
                                                       memcpy(data, pool->bytes, pool->size), 0) : 0;
        valid = atom != 0 && link_define_symbol(&result->link, pools->symbol_base + p, atom, 0, pool->size);
        if (valid)
        {
            result->link.symbols.source[pools->symbol_base + p] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    // cpu_dispatch's resolvers, and cpu_specific's versions' other names at their code (#1447).
    valid = valid && (program->cpu_group_count == 0 || check_program_cpu_atoms(result, program, bytes));
    // Multiversioning by target strings' resolvers, and target_clones' other names at their bodies (#1838).
    const uint64_t target_first = frames - target_frames;
    SymbolId* target_frame_symbols = valid && target_frames != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, target_frames) : 0;
    valid = valid && (target_frames == 0 || target_frame_symbols != 0);
    valid = valid && (program->target_group_count == 0 ||
                      check_program_target_atoms(result, program, target_frames != 0 ? run.frames + target_first : NULL, target_frame_symbols, bytes));
    check_clock_mark(stage_clock, CHECK_STAGE_CODE_ATOMS);
    // The unwind tables, after the last function: measured, then written into the byte arena, one CIE and an FDE per
    // function after it, as one EH_FRAME atom whose FDEs' initial locations refer to their functions.
    if (valid && frames != 0)
    {
        UnwindOutput tables = { .location_capacity = (uint32_t)frames, .shared = true };
        valid = unwind_write(run.frames, (uint32_t)frames, &tables, BUSTER_SCALAR_PATH(scalar)) == UNWIND_ERROR_CAPACITY;
        uint32_t* locations = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, frames) : 0;
        uint8_t* table_bytes = valid ? arena_push(bytes, tables.size, 8) : 0;
        tables.bytes = table_bytes;
        tables.capacity = tables.size;
        tables.locations = locations;
        valid = locations != 0 && table_bytes != 0 && unwind_write(run.frames, (uint32_t)frames, &tables, BUSTER_SCALAR_PATH(scalar)) == UNWIND_OK;
        const AtomId atom = valid ? link_add_atom(&result->link, BUSTER_LINK_CLASS_EH_FRAME, 3, tables.size, table_bytes, 0) : 0;
        valid = atom != 0;
        for (uint32_t f = 0; valid && f < frames; f += 1)
        {
            const SymbolId symbol = f < count ? check_program_body_symbol(program, f) : f < target_first ? helpers->items[f - count].symbol :
                                    target_frame_symbols[f - target_first];
            valid = link_add_fixup(&result->link, atom, locations[f], BUSTER_LINK_FIXUP_PC32, symbol, 0) != 0;
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_UNWIND);
    // Each jump table, in .rodata: an entry per value, its label's offset from the table, which a PC32 fixup gives
    // (S + A - P, P the entry's place, A its offset in the table), as clang's .long .LBB - .LJTI entries are.
    for (uint32_t t = 0; valid && t < result->lowered.table_count; t += 1)
    {
        const CheckLowerTable* table = result->lowered.tables + t;
        const uint64_t size = (uint64_t)table->count * 4;
        uint8_t* data = arena_push(bytes, size, 4);
        const AtomId atom = data != 0 ? link_add_atom(&result->link, BUSTER_LINK_CLASS_RODATA, 2, size, (uint8_t*)memset(data, 0, size), 0) : 0;
        const SymbolId symbol = result->lowered.case_symbol_base + t;
        const SymbolId labels_base = result->lowered.case_symbol_base + result->lowered.table_count;
        valid = atom != 0 && link_define_symbol(&result->link, symbol, atom, 0, size);
        for (uint32_t i = 0; valid && i < table->count; i += 1)
        {
            valid = link_add_fixup(&result->link, atom, i * 4, BUSTER_LINK_FIXUP_PC32, labels_base + table->entries[i], (int64_t)i * 4) != 0;
        }
        if (valid)
        {
            result->link.symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
    }
    // The constructors' and destructors' entries (#147): a pointer to the function each, in an init or fini array atom
    // of its own, in the order LLVM writes llvm.global_ctors and llvm.global_dtors: stably by priority, from the
    // definitions' order. One below 65535 has its priority (-c's .init_array.N and .fini_array.N, check_program_sections),
    // and the others are their arrays' own.
    if (valid && structor_count != 0)
    {
        uint64_t* order = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, structor_count);
        uint64_t* sorting = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, structor_count);
        valid = order != 0 && sorting != 0;
        uint32_t entries = 0;
        for (uint32_t b = 0; valid && b < count; b += 1)
        {
            const SymbolId symbol = check_program_body_symbol(program, b);
            for (uint32_t k = 0; k < 2; k += 1)
            {
                if (structors[2 * symbol + k] != 0)
                {
                    order[entries++] = ((uint64_t)((k << 16) | (structors[2 * symbol + k] - 1)) << 32) | b;
                }
            }
        }
        const uint64_t* sorted = valid ? c_bindings_sort(order, sorting, structor_count, !BUSTER_SCALAR_PATH(scalar), UINT32_MAX) : 0;
        for (uint32_t e = 0; valid && e < structor_count; e += 1)
        {
            const uint32_t k = (uint32_t)(sorted[e] >> 48);
            const uint32_t priority = (uint32_t)(sorted[e] >> 32) & 0xffff;
            const SymbolId symbol = check_program_body_symbol(program, (uint32_t)sorted[e]);
            uint8_t* data = arena_push(bytes, 8, 8);
            const AtomId atom = data != 0 ? link_add_atom(&result->link, k != 0 ? BUSTER_LINK_CLASS_FINI_ARRAY : BUSTER_LINK_CLASS_INIT_ARRAY, 3, 8,
                                                       (uint8_t*)memset(data, 0, 8), 0) : 0;
            valid = atom != 0 && link_add_fixup(&result->link, atom, 0, BUSTER_LINK_FIXUP_ABS64, symbol, 0) != 0 &&
                    (priority == 65535 || link_add_priority(&result->link, atom, priority));
        }
    }
    // A template's `.size` on one of the unit's program symbols, which check_program_sections applies once the static
    // data's atoms exist: a variable's atom is published after this emission.
    bool sized = false;
    // An earlier failure can leave asm_symbols unallocated. Keep that failure's diagnostic instead of reading it.
    for (uint32_t n = 0; valid && sized == false && n < asms->name_count; n += 1)
    {
        const SymbolId symbol = asm_symbols[n];
        sized = symbol != 0 && symbol < asm_first && (asms->names[n].directives & BUSTER_ASM_DIRECTIVE_SIZE) != 0 && asm_commoned[n] == 0;
    }
    result->asm_sizes = sized ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, result->link.symbols.count) : 0;
    result->asm_size_set = sized ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, result->link.symbols.count) : 0;
    valid = valid && (!sized || (result->asm_sizes != 0 && result->asm_size_set != 0));
    if (valid && sized)
    {
        memset(result->asm_size_set, 0, result->link.symbols.count);
        for (uint32_t n = 0; n < asms->name_count; n += 1)
        {
            const SymbolId symbol = asm_symbols[n];
            if (symbol != 0 && symbol < asm_first && (asms->names[n].directives & BUSTER_ASM_DIRECTIVE_SIZE) != 0 && asm_commoned[n] == 0)
            {
                result->asm_sizes[symbol] = asms->names[n].size;
                result->asm_size_set[symbol] = 1;
            }
        }
    }
    check_clock_mark(stage_clock, CHECK_STAGE_CODE_ATOMS);
    if (valid && lanes != NULL && lanes->tier1_function_sizes != NULL)
    {
        result->tier1_size_symbols = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, count) : NULL;
        result->tier1_size_kinds = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint8_t, count) : NULL;
        valid = count == 0 || (result->tier1_size_symbols != NULL && result->tier1_size_kinds != NULL && run.tier1_outcomes != NULL);
        for (uint32_t b = 0; valid && b < count; b += 1)
        {
            const CheckTier1Outcome outcome = run.tier1_outcomes[b];
            result->tier1_size_symbols[b] = check_program_body_symbol(program, b);
            result->tier1_size_kinds[b] = outcome.category == CHECK_TIER1_SUCCESS ? 1 : 0;
            valid = outcome.category == CHECK_TIER1_SUCCESS || outcome.fallback == CHECK_TIER1_FALLBACK_TIER0;
        }
        result->tier1_size_count = valid ? count : 0;
        result->tier1_sizes_ready = valid;
    }
    // Report only source order's prefix through the winning Tier 1 failure, not the speculative work later parts
    // happened to finish. A failure of an earlier compiler phase publishes no Tier 1 success counters. This is
    // independent of lane completion order, and a rejected body's counts are always zero.
    const uint32_t report_end = valid ? count : result->tier1_failure.category != CHECK_TIER1_UNATTEMPTED ? failed + 1 : 0;
    if (lanes != NULL && lanes->tier1)
    {
        lanes->tier1_report.requested += count;
    }
    for (uint32_t b = 0; run.tier1_outcomes != 0 && b < report_end; b += 1)
    {
        const CheckTier1Outcome outcome = run.tier1_outcomes[b];
        const uint32_t stage = outcome.stage;
        const bool attempted = outcome.category != CHECK_TIER1_UNATTEMPTED;
        lanes->tier1_report.functions += attempted;
        lanes->tier1_report.admitted += attempted && stage != CHECK_TIER1_NAKED;
        lanes->tier1_report.categories[outcome.category] += attempted;
        lanes->tier1_report.fallbacks += outcome.fallback == CHECK_TIER1_FALLBACK_TIER0;
        lanes->tier1_report.stages[stage] += attempted;
        const uint32_t operation = check_tier1_operation(outcome);
        if (stage == CHECK_TIER1_SELECTION && operation < BUSTER_CHECK_TIER1_OPERATIONS)
        {
            lanes->tier1_report.operations[operation] += 1;
        }
        for (uint32_t k = 0; k < CHECK_TIER1_CALL_KIND_COUNT; k += 1)
        {
            lanes->tier1_report.calls[k] += (outcome.call_kinds >> k) & 1;
        }
        if (stage == CHECK_TIER1_EMISSION && outcome.emission_error != 0 && outcome.emission_error < BUSTER_CHECK_TIER1_OPERATIONS)
        {
            lanes->tier1_report.emission[outcome.emission_error] += 1;
        }
        const CheckTier1Counts* counts = &run.tier1_counts[b];
        const bool compiled = outcome.category == CHECK_TIER1_SUCCESS;
        lanes->tier1_report.records += compiled ? counts->records : 0;
        lanes->tier1_report.spill_stores += compiled ? counts->spill_stores : 0;
        lanes->tier1_report.spill_loads += compiled ? counts->spill_loads : 0;
        lanes->tier1_report.call_stores += compiled ? counts->call_stores : 0;
        lanes->tier1_report.call_loads += compiled ? counts->call_loads : 0;
        lanes->tier1_report.rematerializations += compiled ? counts->rematerializations : 0;
        lanes->tier1_report.dropped_definitions += compiled ? counts->dropped_definitions : 0;
        lanes->tier1_report.edge_moves += compiled ? counts->edge_moves : 0;
        lanes->tier1_report.spill_slots += compiled ? counts->spill_slots : 0;
        lanes->tier1_report.frame_bytes += compiled ? counts->frame_bytes : 0;
        lanes->tier1_report.open_frames += compiled ? counts->open_frames : 0;
        lanes->tier1_report.return_tails += compiled ? counts->return_tails : 0;
        lanes->tier1_report.red_zones += compiled ? counts->red_zones : 0;
        lanes->tier1_report.code_certified += compiled ? counts->code_certified : 0;
        lanes->tier1_report.code_uncertified += compiled ? counts->code_uncertified : 0;
        lanes->tier1_report.machine_rows += compiled ? counts->machine_rows : 0;
        lanes->tier1_report.row_moves += compiled ? counts->row_moves : 0;
        lanes->tier1_report.inlined_calls += compiled ? counts->inlined_calls : 0;
        lanes->tier1_report.schedule_blocks += compiled ? counts->schedule_blocks : 0;
        lanes->tier1_report.schedule_moved += compiled ? counts->schedule_moved : 0;
    }
    if (!valid)
    {
        const CheckProgramStatus status = result->status == CHECK_PROGRAM_OK ? CHECK_PROGRAM_RESOURCE : result->status;
        const uint32_t token = result->token;
        const Tier0Error backend = result->backend_error;
        const CheckTier1Outcome tier1_failure = result->tier1_failure;
        const uint32_t backend_node = result->backend_node;
        const char* reason = result->reason;
        const CheckProgramRedefined redefined = result->redefined;
        arena_pop_to(output, mark);
        arena_pop_to(bytes, byte_mark);
        *result = (CheckProgramCode){ .status = status, .token = token, .backend_error = backend, .backend_node = backend_node, .reason = reason, .tier1_failure = tier1_failure,
                                      .redefined = redefined };
        if (rows != NULL)
        {
            *rows = (CheckProgramRows){ 0 };
        }
        for (uint32_t lane = 1; lanes != NULL && lane < lanes->arenas_used; lane += 1)
        {
            arena_pop_to(&lanes->arenas[lane].output, 0);
            arena_pop_to(&lanes->arenas[lane].scratch, 0);
            arena_pop_to(&lanes->arenas[lane].bytes, 0);
        }
    }
    // With --tier1-refusals, each body Tier 0 compiled, in source order, by its symbol's name.
    for (uint32_t b = 0; valid && run.tier1_outcomes != 0 && lanes->tier1_refusals != NULL && b < count; b += 1)
    {
        const SymbolId symbol = check_program_body_symbol(program, b);
        const NameId name = symbol < result->link.symbols.count ? result->link.symbols.name[symbol] : 0;
        if (run.tier1_outcomes[b].fallback == CHECK_TIER1_FALLBACK_TIER0 && name != 0)
        {
            check_tier1_print_refusal(lanes->tier1_refusals, result->link.names.bytes[name], result->link.names.length[name], run.tier1_outcomes[b]);
        }
    }
    arena_pop_to(scratch, temporary);
    if (timing)
    {
        lanes->merge_ns = check_program_lanes_now() - lanes_end;
    }
    return valid;
}

// The opt-in manifest describes source bodies only, never inferred helper or alias symbols. Names are hexadecimal
// bytes, so assembler labels containing whitespace cannot split a record. A successful publication calls this once;
// even a declaration-only unit has a header and footer. Consumers validate these sizes against the published ELF.
static void check_program_print_tier1_sizes(FILE* stream, const CheckProgramCode* code)
{
    if (stream != NULL && code->tier1_sizes_ready)
    {
        const uint32_t count = code->tier1_size_count;
        const Link* link = &code->link;
        static const char digits[] = "0123456789abcdef";
        fprintf(stream, "tier1-function-sizes\tversion=1\tfunctions=%" PRIu32 "\n", count);
        for (uint32_t b = 0; b < count; b += 1)
        {
            const SymbolId symbol = code->tier1_size_symbols[b];
            const NameId name = symbol < link->symbols.count ? link->symbols.name[symbol] : 0;
            const uint64_t size = symbol < link->symbols.count ? link->symbols.size[symbol] : 0;
            const uint32_t length = name != 0 ? link->names.length[name] : 0;
            fprintf(stream, "tier1-function-size\ttier=%s\tbytes=%" PRIu64 "\tname=", code->tier1_size_kinds[b] != 0 ? "tier1" : "tier0", size);
            for (uint32_t i = 0; i < length; i += 1)
            {
                const uint8_t byte = link->names.bytes[name][i];
                fputc(digits[byte >> 4], stream);
                fputc(digits[byte & 15], stream);
            }
            fputc('\n', stream);
        }
        fprintf(stream, "tier1-function-sizes-end\tfunctions=%" PRIu32 "\n", count);
    }
}

// check_program_emit_timed without a clock: the benchmark's and the tests' emission on lanes.
static bool check_program_emit_lanes(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                                     const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                                     CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify, CheckProgramLanes* lanes)
{
    (void)scalar;

    return check_program_emit_timed(result, program, parser, identifiers, data_capacity, BUSTER_SCALAR_PATH(scalar), output, bytes, scratch, binding, rows, unwind, verify, lanes,
                                    NULL);
}

// check_program_emit_lanes on the calling thread alone.
static bool check_program_emit(CheckProgramCode* result, const CheckProgram* program, const Parser* parser,
                               const InternTable* identifiers, LinkCapacity data_capacity, bool scalar, Arena* output, Arena* bytes, Arena* scratch,
                               CheckProgramBinding* binding, CheckProgramRows* rows, bool unwind, bool verify)
{
    (void)scalar;

    return check_program_emit_lanes(result, program, parser, identifiers, data_capacity, BUSTER_SCALAR_PATH(scalar), output, bytes, scratch, binding, rows, unwind, verify, NULL);
}

// Whether `name` is `prefix`, or starts with it and a '.', as LLVM's section names group (hasPrefix).
static bool check_program_section_family(String8 name, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    return name.length >= length && memcmp(name.pointer, prefix, length) == 0 && (name.length == length || name.pointer[length] == '.');
}

// Whether `name` starts with `prefix`.
static bool check_program_section_prefix(String8 name, const char* prefix)
{
    const uint64_t length = strlen(prefix);
    return name.length >= length && memcmp(name.pointer, prefix, length) == 0;
}

// GNU's aliases and ifuncs (#147), once every atom of the unit's is in the link: each one's symbol defined at its base's
// atom and place, with its size, as an assembler defines clang's `.set alias, base` (LLVM's ELF writer gives such a
// symbol its base's section and value, and its size where it has none of its own, which clang's hasn't). Returns false,
// with `code`'s status, reason and token, where a base has no atom (its definition wasn't emitted).
static bool check_program_define_aliases(CheckProgramCode* code, const CheckProgram* program)
{
    Link* link = &code->link;
    bool valid = true;
    for (uint32_t k = 0; valid && k < program->alias_count; k += 1)
    {
        const CheckProgramAlias* alias = program->aliases + k;
        const SymbolId taken = alias->kind != CHECK_PROGRAM_WEAKREF ? program->object_symbols[alias->object] : 0;
        const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == alias->object ? taken : 0;
        const SymbolId base = symbol != 0 && alias->base != 0 ? program->object_symbols[alias->base] : 0;
        const AtomId atom = base != 0 && base < link->symbols.count ? link->symbols.atom[base] : 0;
        valid = symbol == 0 || (atom != 0 && link_define_symbol(link, symbol, atom, link->symbols.value[base], link->symbols.size[base]));
        if (valid && symbol != 0)
        {
            link->symbols.source[symbol] = BUSTER_LINK_SOURCE_PROGRAM;
        }
        else if (!valid)
        {
            code->status = atom == 0 ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
            code->reason = atom == 0 ? "an alias or ifunc whose definition isn't emitted" : "an alias's or ifunc's symbol couldn't be defined at its definition";
            code->token = alias->attribute;
        }
    }
    return valid;
}

// `#pragma weak A = T` whose A the unit never declares (#1849, CheckInputSymbolPragma.alias_target): a weak symbol A of
// its own at T's definition, of T's type and size, as clang's clone of T's declaration is. No C reference names A, so it
// is a symbol of the object alone, added once T's atom exists. Units with no such directive pay one test.
static bool check_program_define_pragma_aliases(CheckProgramCode* code, const CheckProgram* program)
{
    const CheckInput* input = program->input;
    const CheckInputSymbolPragmas* history = input->symbol_pragmas;
    const InternTable* identifiers = program->identifiers != NULL ? program->identifiers :
                                     program->parser->preprocessor != NULL ? program->parser->preprocessor->table : NULL;
    Link* link = &code->link;
    bool valid = true;
    for (uint32_t i = 0; valid && history != NULL && i < history->count; i += 1)
    {
        const CheckInputSymbolPragma* record = history->records + i;
        const uint32_t object = record->alias_target < input->token_count ? input->objects[record->alias_target] : 0;
        const SymbolId taken = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
        const SymbolId base = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
        const AtomId atom = base != 0 && base < link->symbols.count ? link->symbols.atom[base] : 0;
        // A unit of several (#777) without T's definition leaves the alias to the unit with it.
        if (record->alias_target != BUSTER_CHECK_NONE && identifiers != NULL && atom != 0)
        {
            const NameId name = link_add_name(link, intern_bytes(identifiers, record->name), intern_length(identifiers, record->name));
            const uint8_t flags = (uint8_t)((link->symbols.flags[base] & BUSTER_LINK_SYMBOL_TYPE) | BUSTER_LINK_SYMBOL_WEAK);
            const SymbolId symbol = name != 0 ? link_add_symbol(link, name, atom, link->symbols.value[base], link->symbols.size[base], flags, BUSTER_LINK_SOURCE_PROGRAM) : 0;
            valid = symbol != 0;
            if (!valid)
            {
                code->status = CHECK_PROGRAM_BACKEND;
                code->reason = "a '#pragma weak' alias's symbol couldn't be defined at its target's definition";
                code->token = record->token;
            }
        }
    }
    return valid;
}

// The named sections of the unit's emitted definitions (#619), once its static data is published, for -c's object
// (ObjectOptions.atom_sections) and the direct path alike: each definition CheckInputSections.placements names a
// section for, which the unit defines in an atom of the link, gets a descriptor of that name, of the kind LLVM gives a
// global with an explicit section (TargetLoweringObjectFileELF's getExplicitSectionGlobal): a function's text (AX); a
// variable's by its atom's class, data (WA), read-only (A), read-only after relocation (WA) or thread-local data (WAT),
// or by its name's family, .bss's (WA, NOBITS) or .tbss's (WAT, NOBITS) for a zero-filled one. A zero-filled variable
// in another section's name is data, as LLVM doesn't put a global with an explicit section in BSS: its atom becomes
// zeroed bytes of the class it is in (DATA or TDATA), in `bytes`. A name another kind's section has (a function's
// .data) is a section of its own with this kind's flags, as LLVM makes it. The file-scope assembly's atoms keep their
// own (CheckProgramCode.asm_sections). Refused by name: a name LLVM gives another kind than the definition's (an
// initialized variable in a .bss, which clang's code generation refuses too, a function in a .tdata), and one it gives
// another type (.note, the arrays'), or the conservatively reserved .llvm.* family (including ordinary PROGBITS names).
// `*table` is null for a unit without either, and otherwise the descriptors by AtomId,
// in `output`. Then, every atom in place, the aliases' and ifuncs' symbols are defined at their bases' (#147,
// check_program_define_aliases): every caller that publishes the static data calls this after it. Returns false with
// `code`'s status, reason and token set.
static bool check_program_sections(CheckProgramCode* code, const CheckProgram* program, Arena* bytes, Arena* output, ObjectAtomSection** table)
{
    const CheckInputSections* sections = program->input->sections;
    const uint32_t placements = sections != NULL ? sections->placement_count : 0;
    Link* link = &code->link;
    *table = 0;
    bool valid = true;
    if (placements != 0 || code->asm_atom_count != 0 || link->priorities.count > 1 || code->retained_count != 0 || code->unit_atom_count != 0 ||
        program->cfstring_count != 0 || code->selectany_count != 0 || code->patchable_count != 0)
    {
        uint32_t refused = BUSTER_CHECK_NONE;
        const char* reason = NULL;
        ObjectAtomSection* descriptors = BUSTER_ARENA_PUSH_ARRAY(output, ObjectAtomSection, link->atoms.count);
        valid = descriptors != 0;
        code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
        if (valid)
        {
            memset(descriptors, 0, (uint64_t)link->atoms.count * sizeof(ObjectAtomSection));
            for (uint32_t a = 0; a < code->asm_atom_count; a += 1)
            {
                descriptors[code->asm_atoms[a]] = code->asm_sections[a];
                valid = valid && link_set_atom_section(link, code->asm_atoms[a], code->asm_sections[a].name);
            }
            // A prioritized constructor's or destructor's entry (#147) is in .init_array.N or .fini_array.N, as clang's
            // object has it (TargetLoweringObjectFileELF's getStaticCtorSection), of its array's type and flags.
            for (uint32_t p = 1; valid && p < link->priorities.count; p += 1)
            {
                const AtomId atom = link->priorities.atom[p];
                const uint8_t class = link->atoms.class[atom];
                char* name = BUSTER_ARENA_PUSH_ARRAY(output, char, 24);
                valid = name != 0;
                const int length = valid ? snprintf(name, 24, "%s.%u", object_class_sections[class].name, (unsigned)link->priorities.priority[p]) : 0;
                descriptors[atom] = (ObjectAtomSection){ .name = { .pointer = name, .length = (uint64_t)length }, .type = object_class_sections[class].type,
                                                         .flags = object_class_sections[class].flags };
                valid = valid && link_set_atom_section(link, atom, descriptors[atom].name);
            }
            code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
            for (uint32_t a = 0; a < code->unit_atom_count; a += 1)
            {
                descriptors[code->unit_atoms[a]] = code->unit_sections[a];
                valid = valid && link_set_atom_section(link, code->unit_atoms[a], code->unit_sections[a].name);
            }
            // patchable_function_entry's entries (#1447, CheckProgramCode.patchable_atoms): each in its section,
            // __patchable_function_entries unless its attribute or -fpatchable-function-entry names another, writable data
            // linked to its function's section (SHF_LINK_ORDER, ObjectAtomSection.link), one per function as clang's object
            // has them, which a linker keeps or drops with it and orders by it.
            static const char patchable_default[] = "__patchable_function_entries";
            for (uint32_t k = 0; k < code->patchable_count; k += 1)
            {
                const String8 named = code->patchable_sections[k];
                const String8 name = named.length != 0 ? named : (String8){ .pointer = (char*)patchable_default, .length = sizeof(patchable_default) - 1 };
                const AtomId atom = code->patchable_atoms[k];
                descriptors[atom] = (ObjectAtomSection){ .name = name, .type = SHT_PROGBITS, .flags = SHF_ALLOC | SHF_WRITE | SHF_LINK_ORDER,
                                                         .link = code->patchable_functions[k] };
                valid = valid && link_set_atom_section(link, atom, name);
            }
            *table = descriptors;
        }
        for (uint32_t p = 0; valid && p < placements; p += 1)
        {
            const CheckInputSectionPlacement* placement = sections->placements + p;
            const uint32_t object = placement->token < program->input->token_count ? program->input->objects[placement->token] : 0;
            // The object's own symbol, defined: not a weakref's target's (#147), nor an alias's or ifunc's, which clang
            // keeps in its base's section, and which check_program_define_aliases defines after the placements.
            const SymbolId taken = object != 0 && object < program->object_count ? program->object_symbols[object] : 0;
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == object ? taken : 0;
            const AtomId atom = symbol != 0 && symbol < link->symbols.count ? link->symbols.atom[symbol] : 0;
            const String8 name = placement->name;
            const uint8_t class = atom != 0 ? link->atoms.class[atom] : BUSTER_LINK_CLASS_NONE;
            // The kind its name gives it, if any (getELFKindForNamedSection), and the class it then goes in.
            const bool bss = check_program_section_family(name, ".bss") || check_program_section_prefix(name, ".gnu.linkonce.b.") ||
                             check_program_section_prefix(name, ".llvm.linkonce.b.") || check_program_section_family(name, ".sbss") ||
                             check_program_section_prefix(name, ".gnu.linkonce.sb.") || check_program_section_prefix(name, ".llvm.linkonce.sb.");
            const bool tdata = check_program_section_family(name, ".tdata") || check_program_section_prefix(name, ".gnu.linkonce.td.") ||
                               check_program_section_prefix(name, ".llvm.linkonce.td.");
            const bool tbss = check_program_section_family(name, ".tbss") || check_program_section_prefix(name, ".gnu.linkonce.tb.") ||
                              check_program_section_prefix(name, ".llvm.linkonce.tb.");
            // Keep the entire .llvm.* family unsupported, including names Clang treats as ordinary PROGBITS. This
            // is a conservative reserved-name boundary, separate from the names ELF assigns another section type.
            const bool restricted = check_program_section_prefix(name, ".note") || check_program_section_family(name, ".init_array") ||
                                    check_program_section_family(name, ".fini_array") || check_program_section_family(name, ".preinit_array") ||
                                    check_program_section_prefix(name, ".llvm.");
            const bool loader = check_input_attribute_of(check_input_attributes_at(program->input, placement->token), CHECK_ATTRIBUTE_LOADER_UNINITIALIZED) != NULL;
            // LLVM's undefined initializer has read-only section kind when explicitly placed. Named BSS/TLS families
            // still impose their own kind; ordinary loader definitions without a placement keep their default BSS.
            const uint8_t placed = loader && !bss && !tdata && !tbss && class == BUSTER_LINK_CLASS_BSS ? BUSTER_LINK_CLASS_RODATA :
                                   class == BUSTER_LINK_CLASS_BSS && !bss ? BUSTER_LINK_CLASS_DATA : class == BUSTER_LINK_CLASS_TBSS && !tbss ? BUSTER_LINK_CLASS_TDATA : class;
            const bool fits = restricted ? false :
                              bss ? class == BUSTER_LINK_CLASS_BSS :
                              tdata ? class == BUSTER_LINK_CLASS_TDATA || class == BUSTER_LINK_CLASS_TBSS :
                              tbss ? class == BUSTER_LINK_CLASS_TBSS :
                                     class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_DATA || class == BUSTER_LINK_CLASS_BSS ||
                                     class == BUSTER_LINK_CLASS_RODATA || class == BUSTER_LINK_CLASS_DATA_RELRO || class == BUSTER_LINK_CLASS_TDATA ||
                                     class == BUSTER_LINK_CLASS_TBSS;
            const ObjectSectionFormat* format = &object_class_sections[placed];
            const ObjectAtomSection descriptor = {
                .name = name,
                .type = format->type,
                .flags = format->flags,
            };
            if (atom != 0 && !fits)
            {
                // The placements are in source order but for the unit's end's tentative definitions, after the rest:
                // the refusal is the earliest's.
                refused = placement->token < refused ? placement->token : refused;
                reason = placement->token == refused ? (restricted ? "a section name with an unsupported ELF type or reserved family" :
                                                                "a definition in a section whose name gives it another kind") :
                                                       reason;
            }
            // An empty section attribute clears inherited placement and overrides pragma settings, but LLVM keeps
            // the definition in its default class. Retain its reader event for Sema's conflicts without materializing
            // a zero-filled BSS/TBSS atom as data for a descriptor that the object writer treats as absent.
            else if (atom != 0 && name.length != 0)
            {
                if (placed != class)
                {
                    // A zero-filled variable, as data in its section: its atom's zeroed bytes.
                    const uint64_t size = link->atoms.size[atom];
                    uint8_t* zeros = size != 0 ? arena_push(bytes, size, 16) : 0;
                    valid = size == 0 || zeros != 0;
                    code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
                    if (valid)
                    {
                        if (zeros != 0)
                        {
                            memset(zeros, 0, size);
                        }
                        link->atoms.class[atom] = placed;
                        link->atoms.bytes[atom] = zeros;
                    }
                }
                descriptors[atom] = descriptor;
                valid = valid && link_set_atom_section(link, atom, descriptor.name);
            }
        }
        // The constant CFStrings' records (#148, check_data_cfstring_keys) are in cfstring, writable data, as clang's.
        for (uint32_t k = 0; valid && refused == BUSTER_CHECK_NONE && k < program->cfstring_count; k += 1)
        {
            const CheckDataCFString* call = program->cfstrings + k;
            const SymbolId taken = call->canonical == call->object && call->object < program->object_count ? program->object_symbols[call->object] : 0;
            const SymbolId symbol = taken != 0 && program->symbol_objects[taken - 1] == call->object ? taken : 0;
            const AtomId atom = symbol != 0 && symbol < link->symbols.count ? link->symbols.atom[symbol] : 0;
            if (atom != 0)
            {
                const ObjectSectionFormat* format = &object_class_sections[link->atoms.class[atom]];
                descriptors[atom] = (ObjectAtomSection){ .name = { .pointer = (char*)"cfstring", .length = 8 }, .type = format->type, .flags = format->flags };
                valid = link_set_atom_section(link, atom, descriptors[atom].name);
            }
        }
        // Retained and selectany definitions (#147) need their named section or one of their own, their
        // class's section's name and its symbol's ("<section>.<name>", as LLVM names a retained global's unique section,
        // getELFSectionNameForGlobal), of its class's type and flags. Retain adds SHF_GNU_RETAIN; selectany supplies the
        // group signature, which also makes otherwise equal explicit section names distinct between groups. The
        // file-scope assembly's groups (#1848, CheckProgramCode.asm_groups) come first, once, as its atoms do.
        const uint32_t base_groups = link->groups.count != 0 ? link->groups.count : 1;
        const uint32_t asm_groups = link->groups.count == 0 && code->asm_group_count > 1 ? code->asm_group_count - 1 : 0;
        const uint32_t previous_groups = base_groups + asm_groups;
        const uint64_t group_count = (uint64_t)previous_groups + code->selectany_count;
        const bool grouping = code->selectany_count != 0 || asm_groups != 0;
        LinkGroup* groups = valid && grouping && group_count < UINT32_MAX ? BUSTER_ARENA_PUSH_ARRAY(output, LinkGroup, group_count) : NULL;
        uint32_t* memberships = groups != NULL ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, link->atoms.count) : NULL;
        valid = valid && (!grouping || (groups != NULL && memberships != NULL));
        code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
        if (valid && groups != NULL)
        {
            memset(groups, 0, group_count * sizeof(LinkGroup));
            memset(memberships, 0, (uint64_t)link->atoms.count * sizeof(uint32_t));
            if (link->groups.count != 0)
            {
                memcpy(groups, link->groups.data, (uint64_t)link->groups.count * sizeof(LinkGroup));
                memcpy(memberships, link->groups.atom_groups, (uint64_t)link->groups.atom_count * sizeof(uint32_t));
            }
            for (uint32_t g = 0; g < asm_groups; g += 1)
            {
                groups[base_groups + g] = code->asm_groups[g + 1];
            }
            for (uint32_t a = 0; asm_groups != 0 && a < code->asm_atom_count; a += 1)
            {
                const uint32_t group = code->asm_atom_groups[a];
                memberships[code->asm_atoms[a]] = group != 0 ? base_groups + group - 1 : 0;
            }
        }
        const uint64_t unique_count = (uint64_t)code->retained_count + code->selectany_count;
        for (uint64_t r = 0; valid && refused == BUSTER_CHECK_NONE && r < unique_count; r += 1)
        {
            const bool selectany = r >= code->retained_count;
            const SymbolId symbol = selectany ? code->selectany_symbols[r - code->retained_count] : code->retained_symbols[r];
            const AtomId atom = link->symbols.atom[symbol];
            const NameId name = link->symbols.name[symbol];
            const ObjectSectionFormat* format = atom != 0 ? &object_class_sections[link->atoms.class[atom]] : NULL;
            const uint64_t prefix = format != NULL ? strlen(format->name) : 0;
            char* spelled = format != NULL && descriptors[atom].name.length == 0 ? BUSTER_ARENA_PUSH_ARRAY(output, char, prefix + 1 + link->names.length[name]) : 0;
            valid = format == NULL || descriptors[atom].name.length != 0 || spelled != 0;
            code->status = valid ? code->status : CHECK_PROGRAM_RESOURCE;
            if (valid && spelled != 0)
            {
                memcpy(spelled, format->name, prefix);
                spelled[prefix] = '.';
                memcpy(spelled + prefix + 1, link->names.bytes[name], link->names.length[name]);
                descriptors[atom] = (ObjectAtomSection){ .name = { .pointer = spelled, .length = prefix + 1 + link->names.length[name] }, .type = format->type,
                                                         .flags = format->flags, .entry_size = format->entry_size };
                valid = valid && link_set_atom_section(link, atom, descriptors[atom].name);
            }
            if (valid && format != NULL)
            {
                descriptors[atom].flags |= selectany ? SHF_GROUP : SHF_GNU_RETAIN;
                if (selectany)
                {
                    const uint32_t group = previous_groups + (uint32_t)(r - code->retained_count);
                    groups[group] = (LinkGroup){ .name = name, .signature = symbol, .unit = link->atoms.unit[atom], .flags = BUSTER_LINK_GROUP_COMDAT };
                    memberships[atom] = group;
                }
            }
        }
        if (valid && refused == BUSTER_CHECK_NONE && groups != NULL)
        {
            link->groups = (LinkGroups){ .data = groups, .atom_groups = memberships, .count = (uint32_t)group_count, .atom_count = link->atoms.count };
        }
        if (valid && refused != BUSTER_CHECK_NONE)
        {
            code->status = CHECK_PROGRAM_UNSUPPORTED;
            code->reason = reason;
            code->token = refused;
            valid = false;
        }
    }
    // A template's `.size` on one of the unit's program symbols, now that a variable's atom is published: a function's
    // size may override its body's, as clang's assembler writes it; an extern's is the no-op both compilers make it. A
    // size past the definition's atom would break the link's symbol rule, which clang's writer lets through; the
    // template's is refused by name instead.
    for (SymbolId symbol = 1; valid && code->asm_size_set != 0 && symbol < code->link.symbols.count; symbol += 1)
    {
        if (code->asm_size_set[symbol] == 0)
        {
            continue;
        }
        const AtomId atom = code->link.symbols.atom[symbol];
        const uint64_t value = code->link.symbols.value[symbol];
        if (atom == 0)
        {
            continue;
        }
        if (atom >= code->link.atoms.count || code->asm_sizes[symbol] > code->link.atoms.size[atom] - value)
        {
            code->status = CHECK_PROGRAM_UNSUPPORTED;
            code->reason = "inline assembly .size on the unit's symbol exceeds its definition";
            valid = false;
        }
        else
        {
            code->link.symbols.size[symbol] = code->asm_sizes[symbol];
        }
    }
    // A template's `.comm` of a C definition's name (#1848, CheckProgramCode.commoned_symbols): the static data defined
    // the symbol at C's atom, which keeps its bytes in its section without a symbol, as clang's object does; the symbol is
    // the common one, of C's size at the `.comm`'s alignment (its atom's), as clang's assembler makes it. A read-only
    // definition's is refused by name: clang's code reads a const variable's initializer where the program reads the
    // common's zeros.
    for (uint32_t k = 0; valid && k < code->commoned_count; k += 1)
    {
        const SymbolId symbol = code->commoned_symbols[k];
        const AtomId atom = code->commoned_atoms[k];
        const AtomId data = symbol < link->symbols.count ? link->symbols.atom[symbol] : 0;
        const bool constant = data != 0 && data < link->atoms.count &&
                              (link->atoms.class[data] == BUSTER_LINK_CLASS_RODATA || link->atoms.class[data] == BUSTER_LINK_CLASS_DATA_RELRO);
        valid = data != 0 && data < link->atoms.count && atom != 0 && atom < link->atoms.count && !constant;
        if (valid)
        {
            link->atoms.size[atom] = link->symbols.size[symbol];
            link->symbols.atom[symbol] = atom;
            link->symbols.value[symbol] = 0;
            link->symbols.flags[symbol] = (uint8_t)((link->symbols.flags[symbol] & ~(BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_BINDING)) |
                                                    BUSTER_LINK_SYMBOL_COMMON | BUSTER_LINK_SYMBOL_GLOBAL);
        }
        else
        {
            code->status = constant ? CHECK_PROGRAM_UNSUPPORTED : CHECK_PROGRAM_BACKEND;
            code->reason = constant ? "inline assembly .comm of a read-only definition" : "a .comm of a definition whose static data wasn't emitted";
        }
    }
    valid = valid && check_program_define_aliases(code, program) && check_program_define_pragma_aliases(code, program);
    // Every atom exists: the forwarded references take their symbols (#2204).
    check_program_label_forward_fixups(code);
    // A symbol of internal linkage that no definition emitted is still an undefined reference: clang writes it as a
    // global undefined symbol (GNU ld and lld report it, and an executable link fails), never as an undefined local,
    // which an ELF object can't hold. The definitions' and aliases' symbols above have their sources set.
    for (SymbolId symbol = 1; valid && symbol < code->link.symbols.count; symbol += 1)
    {
        if ((code->link.symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL &&
            code->link.symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE)
        {
            code->link.symbols.flags[symbol] = (uint8_t)((code->link.symbols.flags[symbol] & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_GLOBAL);
        }
    }
    return valid;
}
#endif
