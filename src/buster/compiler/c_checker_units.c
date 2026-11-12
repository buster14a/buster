// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The C driver's translation units (#152): `compiler [options] a.c b.c ...` compiles every source file of its command
// line in one process. -fsyntax-only checks each, -c writes each one's object (named as clang names it, in the current
// directory), and the default command links one executable of them all, through the direct path: the units' links,
// kept as each unit finishes (check_units_keep), merged into one (check_units_merge) and bound against the C runtime
// as one unit's link is (check_driver_executable).
//
// The units are a lane group's items (lane.c), taken costliest first by the size of their source files. Each runs on
// one lane from preprocessing to its code (check_driver_unit, as a command of one file runs it), its functions on that
// lane alone (#151's function lanes inside a unit's lane would be a group in a group, which lane_run refuses). A lane's
// units share its intern table and its file cache, so that a header the lane's units include is prepared once on the
// lane; the lanes share no state the units write, and each unit's output is its own: the same at every lane count, and
// as it is with the unit alone on the command line. A unit's identifiers that its lane's earlier units gave ids to
// keep them, and its others get the next ones: ids are opaque (docs/determinism.md), and nothing a unit writes
// depends on them. Each unit's diagnostics go to a stream of its own, printed in the command line's order once every
// unit has run, as clang prints them.

// The base pages that the arenas a small unit reserves start with (arena_small_floor), and the size of a unit's source
// past which its arenas start as a command of one file's do: docs/translation-units.md's "Memory" has the 9700X's
// measurements.
#define BUSTER_CHECK_UNITS_SMALL_BYTES (16ull << 20)
#define BUSTER_CHECK_UNITS_LARGE_SOURCE (1ull << 20)
// What --poison-arenas writes over the bytes a unit left in its lane's arenas (arena_poison).
#define BUSTER_CHECK_UNITS_POISON 0xa5
// The units' sources' bytes that pay for a lane past one per unit, whose only work is the units' functions' parts
// (#777): about the bodies' tokens that pay for a function lane (BUSTER_CHECK_EMIT_LANE_TOKENS, at about 5 bytes of
// source a token) times two.
#define BUSTER_CHECK_UNITS_LANE_BYTES (32ull << 10)
// --shared-parts' pause after each part that a unit's emission takes itself on the board (#777), in microseconds: long
// enough that the lanes without units, woken from their futex in 10 to 14 µs, take some of its parts.
#define BUSTER_CHECK_UNITS_SHARED_PAUSE 1000
// A unit's estimated peak (#1109, check_units_estimate): bytes per preprocessed token, and bytes for any unit; and the
// part of what the process may take that the units' estimates may hold at once, in eighths, the rest left for what they
// don't count (the links the lanes keep, the lanes that take only functions' parts, the link).
#define BUSTER_CHECK_UNITS_TOKEN_BYTES 600
#define BUSTER_CHECK_UNITS_UNIT_BYTES (48ull << 20)
#define BUSTER_CHECK_UNITS_MEMORY_EIGHTHS 7

// A unit's link kept for the units' link (check_units_keep): a copy of its records, with room for no more, and which of
// its undefined global symbols a system header declared, for the link's error about them (check_driver_executable's,
// as check_driver_system_declared tells it for one unit); and with -g (#779), its DWARF as -c -g's object carries it
// (c_check_debug.c's unit written alone, whose four fields hold offsets in its own sections), its functions the copy's
// symbols, or none.
struct CheckUnitsKept
{
    // Mixed-input links retain exactly the ELF object -c would write, including named sections and DWARF.
    String8 object;
    Link link;
    uint8_t* system;
    // COMDAT membership by AtomId, the unit's signature SymbolId; allocated only for a unit with groups.
    SymbolId* comdat_signature;
    ObjectDebug debug;
    // Where the copy goes: its tables and names, and its atoms' and DWARF's bytes (its lane's arenas, which keep every
    // unit's that the lane compiled until the link).
    Arena* arena;
    Arena* bytes;
};

// A unit of the command: its source file, and its size, and what compiling it gave: its exit status, its diagnostics'
// text in the order it wrote them (from open_memstream, or null), whether publication reached a fatal, whether it was
// compiled, and its kept link. `object` is the object -c writes.
struct CheckUnitsUnit
{
    const char* path;
    CheckDriverInputKind kind;
    bool preprocessed;
    uint64_t bytes;
    const char* object;
    int status;
    char* diagnostics;
    size_t diagnostic_bytes;
    bool unbuffered;
    bool fatal;
    bool compiled;
    CheckUnitsKept kept;
    DependencyOutput dependency;
};

static bool check_units_keep_object(CheckUnitsKept* kept, const Object* object)
{
    char* bytes = object->error == OBJECT_ERROR_NONE ? arena_push(kept->bytes, object->size, 16) : NULL;
    if (bytes != NULL)
    {
        memcpy(bytes, object->bytes, object->size);
        kept->object = (String8){ .pointer = bytes, .length = object->size };
    }
    return bytes != NULL;
}

// The memory the units' lanes may hold at once (#1109), against which each unit, once preprocessed, takes an estimate of
// its peak (check_units_admit): `limit`, what the process may still take when the run starts
// (check_units_memory_available), or --units-memory's; `held`, what the lanes hold, each lane its largest unit's since it
// last gave memory back, whose pages its three arenas keep for its next units. A unit that doesn't fit beside what the
// other lanes hold waits on `released`, which every lane that gives memory back advances (lane_advance), until it fits
// or no other lane holds any: a unit alone always runs. `waiting` counts the lanes waiting now, which makes a lane that
// ends a unit give its memory back rather than keep it for its next; and for --units-memory's report, `waits` the units
// that waited, and `holders` the lanes that hold memory now, which a lane adds itself to after it takes the memory and
// takes itself out of before it gives it back, so that it never counts more than hold it, and `most` the most it counted.
// Which lane may keep its pages for the process's exit, and so returns its share alone, the run's count decides
// (CheckUnitsRun's `taking`).
typedef struct CheckUnitsMemory CheckUnitsMemory;
struct CheckUnitsMemory
{
    uint64_t limit;
    uint64_t held;
    uint32_t released;
    uint32_t waiting;
    uint32_t waits;
    uint32_t holders;
    uint32_t most;
};

// A lane's state, which its units take turns with: the intern table and the file cache they preprocess with, a unit's
// preprocessor, parser, memory and function lanes, the memory of the links it keeps, the pool of the arenas its units
// release (arena_pool), and the part of the run's memory it holds (CheckUnitsMemory).
typedef struct CheckUnitsLane CheckUnitsLane;
struct CheckUnitsLane
{
    InternTable identifiers;
    FileCache cache;
    bool ready;
    bool failed;
    Preprocessor pp;
    Parser parser;
    Arena arena;
    Arena scratch;
    Arena output;
    Arena kept;
    Arena kept_bytes;
    CheckProgramLanes functions;
    ArenaPool pool;
    CheckUnitsMemory* memory;
    uint64_t held;
    // The run whose units the lane takes, which tells its unit's checker whether a unit is left to take
    // (check_units_discard_huge), and the lane's units whose checker gave its scratch's pages back in whole huge pages,
    // for one (--shared-parts' report).
    const struct CheckUnitsRun* run;
    uint32_t discarded_huge;
};

#include "c_driver_mixed.c"

// What every lane of the units' run reads, and each unit's result, which the lane that takes it fills.
typedef struct CheckUnitsRun CheckUnitsRun;
struct CheckUnitsRun
{
    const CheckDriverCommand* command;
    CheckUnitsUnit* units;
    uint32_t count;
    // The units' indices, costliest first.
    const uint32_t* order;
    CheckUnitsLane* lanes;
    // The lanes' board (#777), on which a unit's emission shares its functions' parts with the lanes that have no unit
    // left to take; null on one lane.
    CheckEmitBoard* board;
    // The memory the lanes' units take turns with (#1109).
    CheckUnitsMemory* memory;
    // Whether every lane releases its memory, as in a command that releases its memory (check_driver_units' `release`) and
    // before a link, whose peak then stays the units'; otherwise the process's exit, which follows the lanes, frees the
    // memory of the lane that stops taking units last.
    bool release;
    // The lanes that still take units, which the run counts itself, with or without a board (whose own count gates the
    // help): the board's allocation may fail on several lanes, and only the lane that stops last may leave its memory to
    // the exit (check_units_lane), and so may still take the units' memory (#1109). `kept` counts the lanes that did,
    // for --shared-parts' and --units-memory's reports.
    uint32_t taking;
    uint32_t kept;
    // The units the lanes have taken so far (check_units_lane), which reaches `count` once none is left to take
    // (check_units_discard_huge). A lane counts its unit after taking it, so the count may lag the takes, never lead
    // them.
    uint32_t started;
};

// Whether a unit of `lane`'s run is still to be taken, which a lane that finishes its unit takes next, likely this one,
// pushing as far into its scratch again. The count may lag a take, which only frees a unit's pages in whole huge pages
// where none was left to take.
static bool check_units_unit_left(const CheckUnitsLane* lane)
{
    return lane->run != NULL && __atomic_load_n(&lane->run->started, __ATOMIC_RELAXED) < lane->run->count;
}

// Whether `lane`'s unit gives its scratch's pages back in whole huge pages after check_program
// (CheckProgramOptions.discard_huge, #777), counted on the lane: when a unit is left (check_units_unit_left).
static bool check_units_discard_huge(CheckUnitsLane* lane)
{
    const bool result = check_units_unit_left(lane);
    lane->discarded_huge += result ? 1 : 0;
    return result;
}

// The first of `link`'s section groups that the units' selection can't key, or 0 for none: a group is selected by its
// signature, which must be a nonlocal symbol defined in one of its members, as selectany's definition is and as the
// file-scope assembly's symbol of a group's name (`.section .data.x,"awG",@progbits,x,comdat` with `.globl x` or
// `.weak x` and `x:` in it) is (#1848). A local signature, a section's or a name no symbol has (which -c's object makes
// a local symbol of, as clang's assembler does), has no global name for the units' exchange.
static uint32_t check_units_group_unkept(const Link* link)
{
    uint32_t result = 0;
    for (uint32_t g = 1; result == 0 && g < link->groups.count; g += 1)
    {
        // The C producer uses its selectany definition as the signature. Keep that stable symbol identity; the
        // object's ordinary and relocation sections are the writer's consumer of the same LinkGroups metadata.
        const LinkGroup group = link->groups.data[g];
        const SymbolId signature = group.signature;
        const AtomId defining = signature != 0 && signature < link->symbols.count ? link->symbols.atom[signature] : 0;
        const bool keyed = signature != 0 && signature < link->symbols.count && defining != 0 && defining < link->atoms.count &&
                           group.flags == BUSTER_LINK_GROUP_COMDAT && (link->symbols.flags[signature] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL &&
                           link->groups.atom_groups[defining] == g;
        result = keyed ? 0 : g;
    }
    return result;
}

// Copies `link`, a unit's whole link (check_driver_object's, its code and data published and link_check passed), into
// `kept`: its tables, names and atoms' bytes into its arenas, with each record's id as it was; and marks its undefined
// global symbols that `program`'s declarations in a system header gave (check_driver_system_declared's rule). With
// `debug` (-g), the unit's DWARF too, its sections' bytes, its functions' symbols and line addresses and its fields.
// False without room, with the arenas as they were.
static bool check_units_keep(CheckUnitsKept* kept, const Link* link, const CheckProgram* program, const Parser* parser, Preprocessor* pp,
                             const ObjectDebug* debug)
{
    const uint64_t mark = kept->arena->length;
    const uint64_t byte_mark = kept->bytes->length;
    const uint32_t names = link->names.count;
    const uint32_t atoms = link->atoms.count;
    const uint32_t symbols = link->symbols.count;
    const uint32_t fixups = link->fixups.count;
    const uint32_t wides = link->fixups.wide_count;
    const uint32_t priorities = link->priorities.count;
    const LinkCapacity capacity = {
        .names = names - 1,
        .atoms = atoms - 1,
        .symbols = symbols - 1,
        .fixups = fixups - 1,
        .wide_addends = wides - 1,
        .priorities = priorities - 1,
    };
    Link* copy = &kept->link;
    bool result = link_initialize(copy, kept->arena, kept->arena, capacity);
    kept->system = result ? BUSTER_ARENA_PUSH_ARRAY(kept->arena, uint8_t, symbols) : NULL;
    result = result && kept->system != NULL;
    const bool grouped = link->groups.count != 0;
    result = result && check_units_group_unkept(link) == 0;
    kept->comdat_signature = result && grouped ? BUSTER_ARENA_PUSH_ARRAY(kept->arena, SymbolId, atoms) : NULL;
    result = result && (!grouped || kept->comdat_signature != NULL);
    for (AtomId a = 0; result && grouped && a < atoms; a += 1)
    {
        const uint32_t group = link->groups.atom_groups[a];
        kept->comdat_signature[a] = group != 0 ? link->groups.data[group].signature : 0;
    }
    for (NameId n = 1; result && n < names; n += 1)
    {
        // Each name with the NUL after it, as link_add_name keeps it.
        uint8_t* name = arena_push(kept->arena, (uint64_t)link->names.length[n] + 1, 1);
        result = name != NULL;
        if (result)
        {
            memcpy(name, link->names.bytes[n], (uint64_t)link->names.length[n] + 1);
            copy->names.bytes[n] = name;
        }
    }
    for (AtomId a = 1; result && a < atoms; a += 1)
    {
        // A zero-fill atom has no bytes; another of no size may have them, and keeps a pointer to none.
        const uint64_t size = link->atoms.size[a];
        uint8_t* bytes = link->atoms.bytes[a] != NULL ? arena_push(kept->bytes, size != 0 ? size : 1, 16) : NULL;
        result = link->atoms.bytes[a] == NULL || bytes != NULL;
        if (bytes != NULL)
        {
            memcpy(bytes, link->atoms.bytes[a], size);
        }
        copy->atoms.bytes[a] = bytes;
        const String8 section = link->atoms.section[a];
        char* name = section.length != 0 ? arena_push(kept->arena, section.length, 1) : NULL;
        result = result && (section.length == 0 || name != NULL);
        if (name != NULL)
        {
            memcpy(name, section.pointer, section.length);
            copy->atoms.section[a] = (String8){ .pointer = name, .length = section.length };
        }
        else
        {
            copy->atoms.section[a] = (String8){ 0 };
        }
    }
    if (result)
    {
        // The other columns as they are, record by record; the linker's are zero, as no linker has read the unit's link.
        const uint64_t named = (uint64_t)names - 1;
        const uint64_t placed = (uint64_t)atoms - 1;
        const uint64_t defined = (uint64_t)symbols - 1;
        const uint64_t fixed = (uint64_t)fixups - 1;
        memcpy(copy->names.length + 1, link->names.length + 1, named * sizeof(*link->names.length));
        memcpy(copy->names.hash + 1, link->names.hash + 1, named * sizeof(*link->names.hash));
        memcpy(copy->atoms.class + 1, link->atoms.class + 1, placed * sizeof(*link->atoms.class));
        memcpy(copy->atoms.alignment_log2 + 1, link->atoms.alignment_log2 + 1, placed * sizeof(*link->atoms.alignment_log2));
        memcpy(copy->atoms.unit + 1, link->atoms.unit + 1, placed * sizeof(*link->atoms.unit));
        memcpy(copy->atoms.size + 1, link->atoms.size + 1, placed * sizeof(*link->atoms.size));
        memcpy(copy->atoms.fixup_begin + 1, link->atoms.fixup_begin + 1, placed * sizeof(*link->atoms.fixup_begin));
        memcpy(copy->atoms.fixup_count + 1, link->atoms.fixup_count + 1, placed * sizeof(*link->atoms.fixup_count));
        memset(copy->atoms.address + 1, 0, placed * sizeof(*copy->atoms.address));
        memcpy(copy->symbols.name + 1, link->symbols.name + 1, defined * sizeof(*link->symbols.name));
        memcpy(copy->symbols.atom + 1, link->symbols.atom + 1, defined * sizeof(*link->symbols.atom));
        memcpy(copy->symbols.value + 1, link->symbols.value + 1, defined * sizeof(*link->symbols.value));
        memcpy(copy->symbols.size + 1, link->symbols.size + 1, defined * sizeof(*link->symbols.size));
        memcpy(copy->symbols.flags + 1, link->symbols.flags + 1, defined * sizeof(*link->symbols.flags));
        memcpy(copy->symbols.source + 1, link->symbols.source + 1, defined * sizeof(*link->symbols.source));
        memset(copy->symbols.got_slot + 1, 0, defined * sizeof(*copy->symbols.got_slot));
        memset(copy->symbols.dynamic_index + 1, 0, defined * sizeof(*copy->symbols.dynamic_index));
        memset(copy->symbols.version + 1, 0, defined * sizeof(*copy->symbols.version));
        memset(copy->symbols.reference + 1, 0, defined * sizeof(*copy->symbols.reference));
        memcpy(copy->fixups.offset + 1, link->fixups.offset + 1, fixed * sizeof(*link->fixups.offset));
        memcpy(copy->fixups.target + 1, link->fixups.target + 1, fixed * sizeof(*link->fixups.target));
        memcpy(copy->fixups.addend + 1, link->fixups.addend + 1, fixed * sizeof(*link->fixups.addend));
        memcpy(copy->fixups.kind + 1, link->fixups.kind + 1, fixed * sizeof(*link->fixups.kind));
        memcpy(copy->fixups.wide_addends + 1, link->fixups.wide_addends + 1, ((uint64_t)wides - 1) * sizeof(*link->fixups.wide_addends));
        memcpy(copy->priorities.atom + 1, link->priorities.atom + 1, ((uint64_t)priorities - 1) * sizeof(*link->priorities.atom));
        memcpy(copy->priorities.priority + 1, link->priorities.priority + 1, ((uint64_t)priorities - 1) * sizeof(*link->priorities.priority));
        copy->names.count = names;
        copy->atoms.count = atoms;
        copy->symbols.count = symbols;
        copy->fixups.count = fixups;
        copy->fixups.wide_count = wides;
        copy->priorities.count = priorities;
        // The program's symbols are the link's first (check_program_emit), each its object's.
        memset(kept->system, 0, symbols);
        for (uint32_t s = 0; s < program->symbol_count && s + 1 < symbols; s += 1)
        {
            const SymbolId symbol = s + 1;
            const uint32_t object = program->symbol_objects[s];
            const uint32_t declaration = object < program->object_count ? program->object_declarations[object] : BUSTER_CHECK_NONE;
            const bool undefined = link->symbols.atom[symbol] == 0 && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
            kept->system[symbol] = undefined && declaration < parser->tokens.count && check_lazy_system(pp, parser->tokens.sources[declaration]);
        }
    }
    kept->debug = (ObjectDebug){ 0 };
    if (result && debug != NULL)
    {
        const uint64_t functions = debug->function_count;
        SymbolId* symbols_copy = BUSTER_ARENA_PUSH_ARRAY(kept->arena, SymbolId, functions + 1);
        uint32_t* addresses = BUSTER_ARENA_PUSH_ARRAY(kept->arena, uint32_t, functions + 1);
        uint32_t* fields = BUSTER_ARENA_PUSH_ARRAY(kept->arena, uint32_t, BUSTER_OBJECT_DEBUG_FIELDS);
        NativeDebugField* value_fields = BUSTER_ARENA_PUSH_ARRAY(kept->arena, NativeDebugField, (uint64_t)debug->extra.field_count + 1);
        NativeDebugAddress* value_addresses = BUSTER_ARENA_PUSH_ARRAY(kept->arena, NativeDebugAddress, (uint64_t)debug->extra.address_count + 1);
        result = symbols_copy != NULL && addresses != NULL && fields != NULL && value_fields != NULL && value_addresses != NULL &&
                 functions + debug->extra.address_count < UINT32_MAX;
        for (uint32_t section = 0; result && section < BUSTER_OBJECT_DEBUG_SECTIONS; section += 1)
        {
            const String8 bytes = debug->sections[section];
            char* copy_bytes = arena_push(kept->bytes, bytes.length != 0 ? bytes.length : 1, 16);
            result = copy_bytes != NULL;
            if (result)
            {
                if (bytes.length != 0)
                {
                    memcpy(copy_bytes, bytes.pointer, bytes.length);
                }
                kept->debug.sections[section] = (String8){ .pointer = copy_bytes, .length = bytes.length };
            }
        }
        if (result)
        {
            memcpy(symbols_copy, debug->functions, functions * sizeof(*symbols_copy));
            memcpy(addresses, debug->line_addresses, functions * sizeof(*addresses));
            memcpy(fields, debug->fields, BUSTER_OBJECT_DEBUG_FIELDS * sizeof(*fields));
            kept->debug.functions = symbols_copy;
            kept->debug.line_addresses = addresses;
            kept->debug.function_count = debug->function_count;
            kept->debug.fields = fields;
            if (debug->extra.field_count != 0)
            {
                memcpy(value_fields, debug->extra.fields, (uint64_t)debug->extra.field_count * sizeof(*value_fields));
            }
            if (debug->extra.address_count != 0)
            {
                memcpy(value_addresses, debug->extra.addresses, (uint64_t)debug->extra.address_count * sizeof(*value_addresses));
            }
            kept->debug.extra = (NativeDebugExtra){
                .fields = value_fields,
                .addresses = value_addresses,
                .field_count = debug->extra.field_count,
                .address_count = debug->extra.address_count,
            };
        }
    }
    if (!result)
    {
        arena_pop_to(kept->arena, mark);
        arena_pop_to(kept->bytes, byte_mark);
        kept->link = (Link){ 0 };
        kept->system = NULL;
        kept->comdat_signature = NULL;
        kept->debug = (ObjectDebug){ 0 };
    }
    return result;
}

// Whether a unit's undefined global symbol of `link`'s name `name` (the units' merged link's) is one that a system header
// declared (CheckUnitsKept.system): a library's function the C library doesn't have, for the error that names it.
static bool check_units_system_declared(const CheckUnitsUnit* units, uint32_t count, const Link* link, NameId name)
{
    bool system = false;
    const bool named = name != 0 && name < link->names.count;
    for (uint32_t u = 0; named && !system && u < count; u += 1)
    {
        const Link* kept = &units[u].kept.link;
        for (SymbolId s = 1; !system && units[u].kept.system != NULL && s < kept->symbols.count; s += 1)
        {
            const NameId own = kept->symbols.name[s];
            system = units[u].kept.system[s] && own != 0 && kept->names.length[own] == link->names.length[name] &&
                     memcmp(kept->names.bytes[own], link->names.bytes[name], link->names.length[name]) == 0;
        }
    }
    return system;
}

// How constraining a symbol's visibility is (bind_visibility_rank's order): default, protected, hidden, then internal.
static uint32_t check_units_visibility_rank(uint8_t flags)
{
    const uint8_t visibility = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
    return visibility == BUSTER_LINK_SYMBOL_DEFAULT     ? 0
           : visibility == BUSTER_LINK_SYMBOL_PROTECTED ? 1
           : visibility == BUSTER_LINK_SYMBOL_HIDDEN    ? 2
                                                        : 3;
}

// -g's DWARF of the units' link (#779): each unit's (CheckUnitsKept.debug, as -c -g's object carries it) joined into
// `debug`, in the units' order, as a linker joins the objects' sections. Each section's bytes are the units'
// contributions one after another, in `arena`; each unit's four fields, which hold offsets in its own sections, move by
// where its contributions start, as a linker's relocations against the sections move them (object_writer.c's
// ObjectDebug); its functions are `described`'s symbols, in the link, and its line addresses move by where its
// .debug_line contribution starts. False, with nothing pushed, for sections past DWARF32's or without room.
static bool check_units_merge_debug(ExecutableDebug* debug, const CheckUnitsUnit* units, uint32_t count, SymbolId* const* described, Arena* arena)
{
    const uint64_t mark = arena->length;
    uint64_t sizes[BUSTER_OBJECT_DEBUG_SECTIONS] = { 0 };
    uint64_t functions = 0;
    uint64_t value_fields = 0;
    uint64_t value_addresses = 0;
    bool valid = true;
    for (uint32_t u = 0; u < count; u += 1)
    {
        const ObjectDebug* own = &units[u].kept.debug;
        valid = valid && native_debug_extra_valid(own->sections, &own->extra);
        for (uint32_t section = 0; section < BUSTER_OBJECT_DEBUG_SECTIONS; section += 1)
        {
            sizes[section] += own->sections[section].length;
        }
        for (uint32_t field = 0; field < BUSTER_OBJECT_DEBUG_FIELDS; field += 1)
        {
            valid = valid && own->sections[BUSTER_OBJECT_DEBUG_INFO].length >= 4 && own->fields[field] <= own->sections[BUSTER_OBJECT_DEBUG_INFO].length - 4;
        }
        functions += own->function_count;
        value_fields += own->extra.field_count;
        value_addresses += own->extra.address_count;
    }
    valid = valid && functions < UINT32_MAX && value_fields < UINT32_MAX && value_addresses < UINT32_MAX;
    char* sections[BUSTER_OBJECT_DEBUG_SECTIONS] = { 0 };
    for (uint32_t section = 0; valid && section < BUSTER_OBJECT_DEBUG_SECTIONS; section += 1)
    {
        valid = sizes[section] < BUSTER_DEBUG_DWARF32_LIMIT;
        sections[section] = valid ? arena_push(arena, sizes[section] != 0 ? sizes[section] : 1, 16) : NULL;
        valid = sections[section] != NULL;
    }
    SymbolId* symbols = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, functions + 1) : NULL;
    uint32_t* addresses = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, functions + 1) : NULL;
    uint32_t* unit_functions = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, count) : NULL;
    NativeDebugField* extra_fields = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, NativeDebugField, value_fields + 1) : NULL;
    NativeDebugAddress* extra_addresses = valid ? BUSTER_ARENA_PUSH_ARRAY(arena, NativeDebugAddress, value_addresses + 1) : NULL;
    valid = valid && symbols != NULL && addresses != NULL && unit_functions != NULL && extra_fields != NULL && extra_addresses != NULL;
    uint32_t next_field = 0;
    uint32_t next_address = 0;
    uint64_t bases[BUSTER_OBJECT_DEBUG_SECTIONS] = { 0 };
    uint32_t function = 0;
    for (uint32_t u = 0; valid && u < count; u += 1)
    {
        const ObjectDebug* own = &units[u].kept.debug;
        for (uint32_t section = 0; section < BUSTER_OBJECT_DEBUG_SECTIONS; section += 1)
        {
            if (own->sections[section].length != 0)
            {
                memcpy(sections[section] + bases[section], own->sections[section].pointer, own->sections[section].length);
            }
        }
        // The header's abbreviation offset, and the DIE's line program, address base and range lists, in that order.
        for (uint32_t field = 0; field < BUSTER_OBJECT_DEBUG_FIELDS; field += 1)
        {
            char* at = sections[BUSTER_OBJECT_DEBUG_INFO] + bases[BUSTER_OBJECT_DEBUG_INFO] + own->fields[field];
            uint32_t value = 0;
            memcpy(&value, at, 4);
            value += (uint32_t)bases[BUSTER_OBJECT_DEBUG_ABBREV + field];
            memcpy(at, &value, 4);
        }
        for (uint32_t f = 0; f < own->function_count; f += 1)
        {
            symbols[function] = described[u][f];
            addresses[function] = (uint32_t)(bases[BUSTER_OBJECT_DEBUG_LINE] + own->line_addresses[f]);
            function += 1;
        }
        // Additional sec_offset fields move with the unit's range/location contribution. ref4 type references do not.
        for (uint32_t f = 0; f < own->extra.field_count; f += 1)
        {
            NativeDebugField field = own->extra.fields[f];
            field.offset += (uint32_t)bases[BUSTER_OBJECT_DEBUG_INFO];
            char* at = sections[BUSTER_OBJECT_DEBUG_INFO] + field.offset;
            uint32_t value = 0;
            memcpy(&value, at, 4);
            value += (uint32_t)bases[field.section];
            memcpy(at, &value, 4);
            extra_fields[next_field++] = field;
        }
        for (uint32_t a = 0; a < own->extra.address_count; a += 1)
        {
            NativeDebugAddress address = own->extra.addresses[a];
            address.offset += (uint32_t)bases[BUSTER_OBJECT_DEBUG_INFO];
            address.symbol = described[u][own->function_count + a];
            if (address.symbol != 0)
            {
                extra_addresses[next_address++] = address;
            }
            else
            {
                // The definition's COMDAT was discarded. Its validated address field becomes a zero tombstone; never
                // relocate this DIE to an atom that no longer exists or to an unrelated replacement definition.
                memset(sections[BUSTER_OBJECT_DEBUG_INFO] + address.offset, 0, 8);
            }
        }
        unit_functions[u] = own->function_count;
        for (uint32_t section = 0; section < BUSTER_OBJECT_DEBUG_SECTIONS; section += 1)
        {
            bases[section] += own->sections[section].length;
        }
    }
    if (valid)
    {
        *debug = (ExecutableDebug){
            .functions = symbols,
            .line_addresses = addresses,
            .function_count = (uint32_t)functions,
            .unit_functions = unit_functions,
            .unit_count = count,
            .extra = {
                .fields = extra_fields,
                .addresses = extra_addresses,
                .field_count = next_field,
                .address_count = next_address,
            },
        };
        for (uint32_t section = 0; section < BUSTER_OBJECT_DEBUG_SECTIONS; section += 1)
        {
            debug->sections[section] = (String8){ .pointer = sections[section], .length = sizes[section] };
        }
    }
    else
    {
        arena_pop_to(arena, mark);
    }
    return valid;
}

// Native debug addresses bind to definitions, not interposable names, for variables as well as functions.
// check_units_keep bounds this sum; fields are not included because they reference sections rather than symbols.
static uint32_t check_units_debug_symbol_count(const ObjectDebug* debug)
{
    return debug->function_count + debug->extra.address_count;
}

static SymbolId check_units_debug_symbol(const ObjectDebug* debug, uint32_t index)
{
    return index < debug->function_count ? debug->functions[index] : debug->extra.addresses[index - debug->function_count].symbol;
}

// Whether a unit's definition `symbol` of `link` takes a name's place from the earlier one, `earlier` of `earlier_link`,
// and whether the two are a duplicate (#147): a strong definition takes a weak one's or a common one's place, a common
// one (BUSTER_LINK_SYMBOL_COMMON) a weak one's and a smaller common one's, as a linker keeps the largest of a name's
// commons, and only two strong definitions are a duplicate. The kept common takes the largest alignment of the name's
// commons, whichever gives it, as GNU ld and lld take both maxima (check_units_common_alignment).
static bool check_units_merge_replaces(const Link* earlier_link, SymbolId earlier, const Link* link, SymbolId symbol, bool* duplicate)
{
    const uint8_t was = earlier_link->symbols.flags[earlier];
    const uint8_t is = link->symbols.flags[symbol];
    const bool was_weak = (was & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool is_weak = (is & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
    const bool was_common = (was & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON;
    const bool is_common = (is & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON;
    const bool was_strong = !was_weak && !was_common;
    const bool is_strong = !is_weak && !is_common;
    const bool larger = link->symbols.size[symbol] > earlier_link->symbols.size[earlier];
    *duplicate = was_strong && is_strong;
    return is_strong ? !was_strong : is_common && (was_weak || (was_common && larger));
}

// The alignment (log2) a unit's symbol gives its name's common: its atom's for a common definition, else 0.
static uint8_t check_units_common_alignment(const Link* unit, SymbolId symbol)
{
    const bool common = (unit->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON && unit->symbols.atom[symbol] != 0;
    return common ? unit->atoms.alignment_log2[unit->symbols.atom[symbol]] : 0;
}

// The optional COMDAT selection, prepared before symbol resolution. Atom maps hold one-based ordinals of the kept
// atoms (zero for discarded ones); symbol masks keep definitions and references outside discarded groups. Commands
// without groups allocate none of these columns and use the ordinary contiguous records unchanged.
typedef struct CheckUnitsSelection CheckUnitsSelection;
struct CheckUnitsSelection
{
    AtomId** atoms;
    uint8_t** symbols;
    LinkCapacity* counts;
    SymbolId* failed;
};

static bool check_units_selection_initialize(CheckUnitsSelection* selection, const CheckUnitsUnit* units, uint32_t count, Arena* arena)
{
    bool grouped = false;
    for (uint32_t u = 0; u < count; u += 1)
    {
        grouped |= units[u].kept.comdat_signature != NULL;
    }
    bool valid = true;
    if (grouped)
    {
        selection->atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId*, count);
        selection->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, count);
        selection->counts = BUSTER_ARENA_PUSH_ARRAY(arena, LinkCapacity, count);
        selection->failed = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, count);
        valid = selection->atoms != NULL && selection->symbols != NULL && selection->counts != NULL && selection->failed != NULL;
        for (uint32_t u = 0; valid && u < count; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const uint32_t atoms = unit->atoms.count;
            const uint32_t symbols = unit->symbols.count;
            selection->atoms[u] = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, atoms);
            selection->symbols[u] = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, symbols);
            valid = selection->atoms[u] != NULL && selection->symbols[u] != NULL;
        }
    }
    return valid;
}

// A group signature is a named nonlocal symbol defined in one of its member atoms, checked when the unit is kept.
static bool check_units_group_signature(const CheckUnitsKept* kept, SymbolId symbol)
{
    const AtomId atom = kept->link.symbols.atom[symbol];
    return kept->comdat_signature != NULL && atom != 0 && kept->comdat_signature[atom] == symbol;
}

static bool check_units_selected_symbol(const CheckUnitsSelection* selection, uint32_t unit, SymbolId symbol)
{
    return selection->symbols == NULL || selection->symbols[unit][symbol] != 0;
}

static bool check_units_selected_definition(const CheckUnitsSelection* selection, uint32_t unit, const Link* link, SymbolId symbol)
{
    const AtomId atom = link->symbols.atom[symbol];
    return link->symbols.source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE ||
           (atom != 0 && (selection->atoms == NULL || selection->atoms[unit][atom] != 0));
}

static LinkCapacity check_units_selected_counts(const CheckUnitsSelection* selection, uint32_t unit, const Link* link)
{
    return selection->counts != NULL ? selection->counts[unit] : (LinkCapacity){
        .atoms = link->atoms.count - 1,
        .fixups = link->fixups.count - 1,
        .wide_addends = link->fixups.wide_count - 1,
        .priorities = link->priorities.count - 1,
    };
}

static AtomId check_units_selected_atom(const CheckUnitsSelection* selection, uint32_t unit, AtomId atom)
{
    return selection->atoms != NULL ? selection->atoms[unit][atom] : atom;
}

// Finish one unit independently after its group selection. Only a reference contributed exclusively by discarded
// fixups disappears; an unrelated undefined symbol retains the ordinary merge's behavior. A live relocation may
// name a discarded global through the winning name, but cannot name a discarded local from another group.
static void check_units_selection_finish(CheckUnitsSelection* selection, const CheckUnitsUnit* units, uint32_t u)
{
    const Link* unit = &units[u].kept.link;
    // Kept unit records are immutable. The selection owns separate output columns, so these counts stay fixed.
    const uint32_t symbol_count = unit->symbols.count;
    const uint32_t atom_count = unit->atoms.count;
    const uint32_t priority_count = unit->priorities.count;
    AtomId* atoms = selection->atoms[u];
    uint8_t* symbols = selection->symbols[u];
    LinkCapacity counts = { 0 };
    selection->failed[u] = 0;
    atoms[0] = 0;
    symbols[0] = 0;
    for (SymbolId s = 1; s < symbol_count; s += 1)
    {
        const AtomId atom = unit->symbols.atom[s];
        symbols[s] = atom != 0 ? (atoms[atom] != 0 ? 1 : 2) : unit->symbols.source[s] == BUSTER_LINK_SOURCE_ABSOLUTE ? 1 : 0;
    }
    for (AtomId a = 1; a < atom_count; a += 1)
    {
        const bool kept = atoms[a] != 0;
        atoms[a] = kept ? ++counts.atoms : 0;
        const FixupId begin = unit->atoms.fixup_begin[a];
        const uint32_t fixup_count = unit->atoms.fixup_count[a];
        for (uint32_t i = 0; i < fixup_count; i += 1)
        {
            const FixupId f = begin + i;
            symbols[unit->fixups.target[f]] |= kept ? 1 : 2;
            counts.fixups += kept;
            counts.wide_addends += kept && (unit->fixups.kind[f] & BUSTER_LINK_FIXUP_WIDE) != 0;
        }
    }
    for (SymbolId s = 1; s < symbol_count; s += 1)
    {
        const AtomId atom = unit->symbols.atom[s];
        const bool local = (unit->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL;
        const bool invalid = local && atom != 0 && atoms[atom] == 0 && (symbols[s] & 1) != 0;
        selection->failed[u] = selection->failed[u] == 0 && invalid ? s : selection->failed[u];
        symbols[s] = symbols[s] != 2;
    }
    for (uint32_t p = 1; p < priority_count; p += 1)
    {
        counts.priorities += atoms[unit->priorities.atom[p]] != 0;
    }
    selection->counts[u] = counts;
}

static bool check_units_selection_valid(const CheckUnitsSelection* selection, const CheckUnitsUnit* units, uint32_t count)
{
    bool valid = true;
    for (uint32_t u = 0; valid && selection->failed != NULL && u < count; u += 1)
    {
        const SymbolId symbol = selection->failed[u];
        if (symbol != 0)
        {
            const Link* unit = &units[u].kept.link;
            const NameId name = unit->symbols.name[symbol];
            fprintf(stderr, "error: relocation in %s refers to discarded local symbol '%.*s'\n", units[u].path,
                    name != 0 ? (int)unit->names.length[name] : 11, name != 0 ? (const char*)unit->names.bytes[name] : "<anonymous>");
            valid = false;
        }
    }
    return valid;
}

// Scalar twin of the name exchange's group phase: each signature selects its earliest group in command order,
// independently of the ordinary strong/weak symbol resolution. Several member atoms of the same unit stay together.
static void check_units_select_groups(CheckUnitsSelection* selection, const CheckUnitsUnit* units, uint32_t count, SymbolId* table,
                                      uint64_t slots, uint32_t* first_unit, SymbolId* first_symbol)
{
    memset(table, 0, slots * sizeof(*table));
    SymbolId next = 1;
    for (uint32_t u = 0; u < count; u += 1)
    {
        const CheckUnitsKept* kept = &units[u].kept;
        const Link* unit = &kept->link;
        const uint32_t atom_count = unit->atoms.count;
        for (AtomId a = 1; a < atom_count; a += 1)
        {
            const SymbolId signature = kept->comdat_signature != NULL ? kept->comdat_signature[a] : 0;
            SymbolId group = 0;
            const NameId name = signature != 0 ? unit->symbols.name[signature] : 0;
            uint64_t slot = name != 0 ? unit->names.hash[name] & (slots - 1) : 0;
            while (signature != 0 && group == 0 && table[slot] != 0)
            {
                const SymbolId candidate = table[slot];
                const Link* other = &units[first_unit[candidate]].kept.link;
                const NameId other_name = other->symbols.name[first_symbol[candidate]];
                const bool same = other->names.hash[other_name] == unit->names.hash[name] && other->names.length[other_name] == unit->names.length[name] &&
                                  memcmp(other->names.bytes[other_name], unit->names.bytes[name], unit->names.length[name]) == 0;
                group = same ? candidate : 0;
                slot = same ? slot : (slot + 1) & (slots - 1);
            }
            if (signature != 0 && group == 0)
            {
                group = next++;
                table[slot] = group;
                first_unit[group] = u;
                first_symbol[group] = signature;
            }
            selection->atoms[u][a] = signature == 0 || first_unit[group] == u;
        }
        check_units_selection_finish(selection, units, u);
    }
}

// The units' kept links joined into `link`, in the units' order, as check_program_emit builds one unit's (#152): every
// symbol first, a global name's once and each unit's locals its own, the units' definitions marked as the program's;
// then the C runtime, which binding (`binding`, bind_begin) joins between the program's symbols and its atoms, its units
// numbered after the units'; then each unit's atoms in its order, each with its fixups, and its priorities and the
// definitions it makes. A name takes the most constraining visibility any unit gives it, and a reference only takes
// the binding of a weak one when no unit refers to the name otherwise, as a linker resolves the objects' symbols. Its
// tables go in `output`, the runtime's atoms' bytes in `bytes`; the units' atoms' bytes stay in their kept links, where
// the executable's write patches them. A name that two units define (strongly: a weak definition yields to another) is
// an error, which this prints, as it prints a failure of memory; bind_begin's failure is binding's (bind->error, with
// binding->failed_name), which its caller reports as it reports one unit's. With `debug` (-g, #779), the units' DWARF
// joined into it, in `output` (check_units_merge_debug).
static bool check_units_merge(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, ExecutableDebug* debug, Arena* output,
                              Arena* bytes, Arena* scratch)
{
    const uint64_t mark = scratch->length;
    uint64_t symbols = 0;
    uint64_t atoms = 0;
    uint64_t fixups = 0;
    uint64_t wides = 0;
    uint64_t priorities = 0;
    for (uint32_t u = 0; u < count; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        symbols += unit->symbols.count - 1;
        atoms += unit->atoms.count - 1;
        fixups += unit->fixups.count - 1;
        wides += unit->fixups.wide_count - 1;
        priorities += unit->priorities.count - 1;
    }
    bool valid = symbols < UINT32_MAX / 2 && atoms < UINT32_MAX && fixups < UINT32_MAX && wides < UINT32_MAX && priorities < UINT32_MAX;
    // Each unit's symbols' ids in the link; and for each of the link's symbols, its first occurrence (unit and symbol),
    // which gives its name, the definition's (count for none) and the visibility and binding the units give it.
    SymbolId** maps = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId*, count) : NULL;
    uint32_t* first_unit = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, symbols + 1) : NULL;
    SymbolId* first_symbol = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, symbols + 1) : NULL;
    uint32_t* defining_unit = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, symbols + 1) : NULL;
    SymbolId* defining_symbol = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, symbols + 1) : NULL;
    uint8_t* visibility = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1) : NULL;
    uint8_t* strong = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1) : NULL;
    // The largest alignment of each name's common definitions, which the kept common takes.
    uint8_t* common_alignment = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1) : NULL;
    // The global names' table, open addressing on their hashes: twice the slots of the symbols, at least, each the link's
    // symbol of its name or 0.
    uint64_t slots = 16;
    while (slots < 2 * symbols)
    {
        slots *= 2;
    }
    SymbolId* table = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, slots) : NULL;
    valid = valid && maps != NULL && first_unit != NULL && first_symbol != NULL && defining_unit != NULL && defining_symbol != NULL && visibility != NULL &&
            strong != NULL && common_alignment != NULL && table != NULL;
    for (uint32_t u = 0; valid && u < count; u += 1)
    {
        maps[u] = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, units[u].kept.link.symbols.count);
        valid = maps[u] != NULL;
    }
    CheckUnitsSelection selection = { 0 };
    bool discarded_local = false;
    valid = valid && check_units_selection_initialize(&selection, units, count, scratch);
    if (valid && selection.atoms != NULL)
    {
        check_units_select_groups(&selection, units, count, table, slots, first_unit, first_symbol);
        valid = check_units_selection_valid(&selection, units, count);
        discarded_local = !valid;
    }
    if (valid)
    {
        memset(table, 0, slots * sizeof(*table));
    }
    SymbolId next = 1;
    uint32_t duplicate_unit = count;
    SymbolId duplicate = 0;
    for (uint32_t u = 0; valid && duplicate == 0 && u < count; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        maps[u][0] = 0;
        for (SymbolId s = 1; duplicate == 0 && s < unit->symbols.count; s += 1)
        {
            if (!check_units_selected_symbol(&selection, u, s))
            {
                maps[u][s] = 0;
                continue;
            }
            const uint8_t flags = unit->symbols.flags[s];
            const uint8_t binding_flags = flags & BUSTER_LINK_SYMBOL_BINDING;
            const NameId name = unit->symbols.name[s];
            const bool global = binding_flags != BUSTER_LINK_SYMBOL_LOCAL && name != 0;
            const bool defined = check_units_selected_definition(&selection, u, unit, s);
            SymbolId merged = 0;
            uint64_t slot = global ? unit->names.hash[name] & (slots - 1) : 0;
            while (global && merged == 0 && table[slot] != 0)
            {
                const SymbolId candidate = table[slot];
                const Link* other = &units[first_unit[candidate]].kept.link;
                const NameId other_name = other->symbols.name[first_symbol[candidate]];
                const bool same = other->names.hash[other_name] == unit->names.hash[name] && other->names.length[other_name] == unit->names.length[name] &&
                                  memcmp(other->names.bytes[other_name], unit->names.bytes[name], unit->names.length[name]) == 0;
                merged = same ? candidate : 0;
                slot = same ? slot : (slot + 1) & (slots - 1);
            }
            if (merged == 0)
            {
                merged = next;
                next += 1;
                first_unit[merged] = u;
                first_symbol[merged] = s;
                defining_unit[merged] = count;
                defining_symbol[merged] = 0;
                visibility[merged] = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
                strong[merged] = 0;
                common_alignment[merged] = 0;
                if (global)
                {
                    table[slot] = merged;
                }
            }
            if (check_units_visibility_rank(flags) > check_units_visibility_rank(visibility[merged]))
            {
                visibility[merged] = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
            }
            strong[merged] |= binding_flags != BUSTER_LINK_SYMBOL_WEAK;
            const uint8_t aligned = check_units_common_alignment(unit, s);
            common_alignment[merged] = aligned > common_alignment[merged] ? aligned : common_alignment[merged];
            if (defined)
            {
                const bool earlier = defining_unit[merged] != count;
                bool twice = false;
                const bool replaces = earlier && check_units_merge_replaces(&units[defining_unit[merged]].kept.link, defining_symbol[merged], unit, s, &twice);
                if (!earlier || replaces)
                {
                    defining_unit[merged] = u;
                    defining_symbol[merged] = s;
                }
                else if (twice)
                {
                    duplicate_unit = u;
                    duplicate = merged;
                }
            }
            maps[u][s] = merged;
        }
    }
    if (duplicate != 0)
    {
        const Link* unit = &units[first_unit[duplicate]].kept.link;
        const NameId name = unit->symbols.name[first_symbol[duplicate]];
        fprintf(stderr, "error: multiple definition of '%.*s': %s defines it, and %s first\n", (int)unit->names.length[name],
                (const char*)unit->names.bytes[name], units[duplicate_unit].path, units[defining_unit[duplicate]].path);
        valid = false;
    }
    // With -g, each unit's DWARF functions' symbols in the link (`described`). A function that another unit's definition
    // takes the place of (a weak one, which a strong one or an earlier weak one replaces) keeps its code, which its
    // unit's DWARF describes: it gets a local symbol of its own at that code, after the program's others, as a linker
    // relocates an object's DWARF against its section rather than its name.
    SymbolId** described = valid && debug != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId*, count) : NULL;
    valid = valid && (debug == NULL || described != NULL);
    uint64_t replaced = 0;
    for (uint32_t u = 0; described != NULL && valid && u < count; u += 1)
    {
        const ObjectDebug* own = &units[u].kept.debug;
        described[u] = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, (uint64_t)check_units_debug_symbol_count(own) + 1);
        valid = described[u] != NULL;
        for (uint32_t f = 0; valid && f < check_units_debug_symbol_count(own); f += 1)
        {
            const SymbolId s = check_units_debug_symbol(own, f);
            const SymbolId merged = maps[u][s];
            const AtomId atom = units[u].kept.link.symbols.atom[s];
            const bool discarded = f >= own->function_count && atom != 0 && check_units_selected_atom(&selection, u, atom) == 0;
            const bool kept = !discarded && merged != 0 && defining_unit[merged] == u && defining_symbol[merged] == s;
            described[u][f] = discarded ? 0 : kept ? merged : (SymbolId)(next + replaced);
            replaced += !discarded && !kept;
        }
    }
    // The names no unit defines, for the runtime's room in the link (bind_capacity_needed).
    String8* externals = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, next) : NULL;
    uint32_t external_count = 0;
    valid = valid && externals != NULL;
    for (SymbolId m = 1; valid && m < next; m += 1)
    {
        const Link* unit = &units[first_unit[m]].kept.link;
        const SymbolId s = first_symbol[m];
        const NameId name = unit->symbols.name[s];
        if ((unit->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL && name != 0 && defining_unit[m] == count)
        {
            externals[external_count++] = (String8){ .pointer = (char*)unit->names.bytes[name], .length = unit->names.length[name] };
        }
    }
    uint64_t name_bytes = 0;
    uint64_t atom_bytes = 0;
    const LinkCapacity runtime = valid ? bind_capacity_needed(binding->runtime, externals, external_count, scratch, &name_bytes, &atom_bytes) : (LinkCapacity){ 0 };
    const uint64_t total_names = (uint64_t)next - 1 + replaced + runtime.names;
    const uint64_t total_atoms = atoms + runtime.atoms;
    const uint64_t total_symbols = (uint64_t)next - 1 + replaced + runtime.symbols;
    const uint64_t total_fixups = fixups + runtime.fixups;
    const uint64_t total_wides = wides + runtime.wide_addends;
    const uint64_t total_priorities = priorities + runtime.priorities;
    valid = valid && total_names < UINT32_MAX && total_atoms < UINT32_MAX && total_symbols < UINT32_MAX && total_fixups < UINT32_MAX &&
            total_wides < UINT32_MAX && total_priorities < UINT32_MAX;
    const LinkCapacity capacity = {
        .names = valid ? (uint32_t)total_names : 0,
        .atoms = valid ? (uint32_t)total_atoms : 0,
        .symbols = valid ? (uint32_t)total_symbols : 0,
        .fixups = valid ? (uint32_t)total_fixups : 0,
        .wide_addends = valid ? (uint32_t)total_wides : 0,
        .priorities = valid ? (uint32_t)total_priorities : 0,
    };
    const bool sized = valid && link_initialize(link, output, output, capacity);
    valid = sized;
    // The link's symbols in its order, each a definition's (its flags, and the program's) or the first reference's.
    for (SymbolId m = 1; valid && m < next; m += 1)
    {
        const bool defined = defining_unit[m] != count;
        const Link* unit = &units[defined ? defining_unit[m] : first_unit[m]].kept.link;
        const SymbolId s = defined ? defining_symbol[m] : first_symbol[m];
        const NameId own = unit->symbols.name[s];
        const NameId name = own != 0 ? link_add_name_hashed(link, unit->names.bytes[own], unit->names.length[own], unit->names.hash[own]) : 0;
        const bool absolute = defined && unit->symbols.source[s] == BUSTER_LINK_SOURCE_ABSOLUTE;
        uint8_t flags = (uint8_t)((unit->symbols.flags[s] & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility[m]);
        if (!defined && (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK && strong[m])
        {
            flags = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_GLOBAL);
        }
        const uint8_t source = absolute ? BUSTER_LINK_SOURCE_ABSOLUTE : defined ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE;
        valid = (own == 0 || name != 0) && link_add_symbol(link, name, 0, absolute ? unit->symbols.value[s] : 0, absolute ? unit->symbols.size[s] : 0, flags, source) == m;
    }
    // The replaced functions' local symbols (`described`), which the units' atoms define.
    for (uint32_t u = 0; described != NULL && valid && u < count; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        const ObjectDebug* own = &units[u].kept.debug;
        for (uint32_t f = 0; valid && f < check_units_debug_symbol_count(own); f += 1)
        {
            const SymbolId s = check_units_debug_symbol(own, f);
            const NameId own_name = unit->symbols.name[s];
            if (described[u][f] != 0 && described[u][f] != maps[u][s])
            {
                const NameId name = own_name != 0 ? link_add_name_hashed(link, unit->names.bytes[own_name], unit->names.length[own_name], unit->names.hash[own_name]) : 0;
                const uint8_t flags = (uint8_t)((unit->symbols.flags[s] & ~(BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_VISIBILITY)) | BUSTER_LINK_SYMBOL_LOCAL |
                                                BUSTER_LINK_SYMBOL_DEFAULT);
                valid = (own_name == 0 || name != 0) && link_add_symbol(link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_PROGRAM) == described[u][f];
            }
        }
    }
    const bool symbols_added = valid;
    // Every symbol of the program's exists: the runtime joins now, before the program's atoms.
    if (valid && !bind_begin(binding->bind, link, binding->runtime, binding->path, bytes, count, binding->arena))
    {
        const NameId name = binding->bind->name;
        if (name != 0 && name < link->names.count)
        {
            snprintf(binding->failed_name, sizeof(binding->failed_name), "%.*s", (int)link->names.length[name], (const char*)link->names.bytes[name]);
        }
        valid = false;
    }
    const bool begun = valid;
    for (uint32_t u = 0; valid && u < count; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        const uint64_t unit_mark = scratch->length;
        AtomId* placed = BUSTER_ARENA_PUSH_ARRAY(scratch, AtomId, unit->atoms.count);
        valid = placed != NULL;
        for (AtomId a = 1; valid && a < unit->atoms.count; a += 1)
        {
            if (selection.atoms != NULL && selection.atoms[u][a] == 0)
            {
                placed[a] = 0;
                continue;
            }
            placed[a] = link_add_atom(link, unit->atoms.class[a], unit->atoms.alignment_log2[a], unit->atoms.size[a], unit->atoms.bytes[a], u);
            valid = placed[a] != 0 && link_set_atom_section(link, placed[a], unit->atoms.section[a]);
            const FixupId begin = unit->atoms.fixup_begin[a];
            for (uint32_t i = 0; valid && i < unit->atoms.fixup_count[a]; i += 1)
            {
                const FixupId f = begin + i;
                valid = link_add_fixup(link, placed[a], unit->fixups.offset[f], unit->fixups.kind[f] & BUSTER_LINK_FIXUP_KIND, maps[u][unit->fixups.target[f]],
                                       link_fixup_addend(unit, f)) != 0;
            }
        }
        for (uint32_t p = 1; valid && p < unit->priorities.count; p += 1)
        {
            const AtomId atom = placed[unit->priorities.atom[p]];
            valid = atom == 0 || link_add_priority(link, atom, unit->priorities.priority[p]);
        }
        for (SymbolId s = 1; valid && s < unit->symbols.count; s += 1)
        {
            const SymbolId merged = maps[u][s];
            if (merged != 0 && unit->symbols.atom[s] != 0 && defining_unit[merged] == u && defining_symbol[merged] == s)
            {
                valid = link_define_symbol(link, merged, placed[unit->symbols.atom[s]], unit->symbols.value[s], unit->symbols.size[s]);
                // A kept common takes its name's commons' largest alignment.
                const AtomId atom = placed[unit->symbols.atom[s]];
                const bool common = (unit->symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON;
                link->atoms.alignment_log2[atom] = common && common_alignment[merged] > link->atoms.alignment_log2[atom] ? common_alignment[merged] : link->atoms.alignment_log2[atom];
            }
        }
        for (uint32_t f = 0; described != NULL && valid && f < check_units_debug_symbol_count(&units[u].kept.debug); f += 1)
        {
            const SymbolId s = check_units_debug_symbol(&units[u].kept.debug, f);
            if (described[u][f] != 0 && described[u][f] != maps[u][s])
            {
                valid = link_define_symbol(link, described[u][f], placed[unit->symbols.atom[s]], unit->symbols.value[s], unit->symbols.size[s]);
            }
        }
        arena_pop_to(scratch, unit_mark);
    }
    const bool atoms_added = valid;
    if (valid && debug != NULL)
    {
        valid = check_units_merge_debug(debug, units, count, described, output);
        if (!valid)
        {
            fprintf(stderr, "error: can't make the debug information: no memory, or sections past DWARF32's\n");
        }
    }
    if (!atoms_added && duplicate == 0 && !discarded_local && (!symbols_added || begun))
    {
        fprintf(stderr, "fatal error: can't link the units: %s (link error %u)\n", sized ? "their records don't fit the link" : "no memory", (uint32_t)link->error);
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The units' merge on lanes (#778): check_units_merge's link, the same records with the same ids, built by every lane
// of a round of the units' flow (check_units_merge_lanes); check_units_merge is its exact twin, which --scalar and one
// lane run.
//   1. The global names, rule 4's exchange: each lane counts its range of the units' global symbols by partition of
//      their names' hashes and places them, partition by partition and in the lanes' order, so that each partition
//      holds its symbols in the units' order. Then each lane resolves the partitions it takes, in a table of each one's
//      names: a name's first symbol stands for it, and its definition, its first duplicate definition, its most
//      constraining visibility and whether any symbol binds it otherwise than weakly are the first's, as the twin
//      decides them symbol by symbol.
//   2. The ids: each unit's locals and its global symbols that are their names' first take the link's next ids, in the
//      units' order (each unit's count, a prefix sum on lane 0, then each lane its units'), and every other global
//      symbol its name's first's.
//   3. On lane 0: the first duplicate (the one the twin finds first), -g's functions that another unit's definition
//      replaces, the names no unit defines, the link's room and its initialization.
//   4. The symbols and their names, each lane a range of the ids, their names' bytes placed by a prefix sum of their
//      lengths, as link_add_name_hashed and link_add_symbol would add them one after another.
//   5. On lane 0: the replaced functions' symbols, and bind_begin.
//   6. Each unit's atoms in its order, each atom's fixups in the atoms' order (wide addends numbered so, as
//      link_add_fixup numbers them), its priorities and its definitions, at their places (prefix sums of the units'
//      counts), each lane its units by their records.
//   7. On lane 0: -g's DWARF (check_units_merge_debug).
// The kept links passed link_check, so their records are copied as they are, with only their ids remapped: link_check
// checks the merged link before its write, as it checks the twin's.

// A unit's global symbol in the merge's exchange: its unit, and its id there.
typedef struct CheckUnitsMergeRecord CheckUnitsMergeRecord;
struct CheckUnitsMergeRecord
{
    uint32_t unit;
    SymbolId symbol;
};

// A global name's resolution, at its first record: its definition's record (check_units_merge_replaces') or UINT32_MAX,
// its first duplicate definition's (a strong one after a strong one) or UINT32_MAX, the most constraining visibility its
// symbols give it, whether any of them binds it otherwise than weakly, and its commons' largest alignment.
typedef struct CheckUnitsMergeName CheckUnitsMergeName;
struct CheckUnitsMergeName
{
    uint32_t defining;
    uint32_t duplicate;
    uint8_t visibility;
    uint8_t strong;
    uint8_t common_alignment;
};

// The partitions of the exchange per lane: enough that a lane's partitions balance, few enough that each one's table is
// small.
#define BUSTER_CHECK_UNITS_MERGE_PARTITIONS 64

// What the merge's lanes share. Lane 0 writes the steps' results it takes alone between syncs; each lane's own results
// are its slots of the per-lane arrays.
typedef struct CheckUnitsMerge CheckUnitsMerge;
struct CheckUnitsMerge
{
    Link* link;
    const CheckUnitsUnit* units;
    uint32_t count;
    CheckProgramBinding* binding;
    ExecutableDebug* debug;
    Arena* output;
    Arena* bytes;
    Arena* scratch;
    // Per unit, inclusive prefix sums: of its symbols (the exchange's lanes' costs) and of its records (the atoms' lanes').
    uint64_t* symbol_costs;
    uint64_t* record_costs;
    // The exchange: each lane's count per partition, then where its records go (lane-major counts, partition-major
    // places); each partition's first record (partitions + 1); the records; each record's name's first record; and at
    // each first record, its name's resolution.
    uint32_t partitions;
    uint64_t* counts;
    uint64_t* partition_begin;
    CheckUnitsMergeRecord* records;
    uint32_t* firsts;
    CheckUnitsMergeName* names;
    uint64_t global_count;
    // Per unit and symbol: its record (UINT32_MAX for a local) and its id in the link.
    uint32_t** entries;
    SymbolId** maps;
    // Per unit: its symbols that take ids of their own, then the first of them (exclusive prefix sums, from 1).
    uint64_t* unit_ids;
    // Per record: the id of its name in the link, at a first record. Per id: the symbol that gives it its name and flags
    // (the definition's, else the first reference's), whether it is defined, its visibility, whether it binds otherwise
    // than weakly and its commons' largest alignment, which a kept common takes.
    SymbolId* record_ids;
    uint32_t* source_unit;
    SymbolId* source_symbol;
    CheckUnitsSelection selection;
    uint8_t* defined;
    uint8_t* visibility;
    uint8_t* strong;
    uint8_t* common_alignment;
    SymbolId next;
    // -g's functions' symbols in the link, per unit (check_units_merge's `described`), and how many are replaced.
    SymbolId** described;
    uint64_t replaced;
    // The symbols' names: each lane's named symbols and their bytes, then their places, and the bytes' push.
    uint64_t named[BUSTER_LANE_MAX_COUNT];
    uint64_t name_bytes[BUSTER_LANE_MAX_COUNT];
    uint64_t name_base[BUSTER_LANE_MAX_COUNT];
    uint64_t byte_base[BUSTER_LANE_MAX_COUNT];
    uint8_t* name_storage;
    // Per unit: where its atoms, fixups, wide addends and priorities start in the link.
    uint64_t* atom_base;
    uint64_t* fixup_base;
    uint64_t* wide_base;
    uint64_t* priority_base;
    // Each lane's first duplicate (its key, unit << 32 | symbol, and its record), and the merge's progress, lane 0's:
    // as check_units_merge's, which messages its failures make.
    uint64_t duplicate_key[BUSTER_LANE_MAX_COUNT];
    uint32_t duplicate_record[BUSTER_LANE_MAX_COUNT];
    bool resolved[BUSTER_LANE_MAX_COUNT];
    bool duplicated;
    bool discarded_local;
    bool sized;
    bool symbols_added;
    bool begun;
    bool atoms_added;
    bool valid;
};

// Whether every lane goes on: lane 0's verdict, which every lane gets.
static bool check_units_merge_proceed(const CheckUnitsMerge* merge)
{
    const bool proceed = lane_index() == 0 && merge->valid;
    return lane_broadcast(proceed, 0) != 0;
}

// Whether a unit's symbol is global in the merge (named and not local), as check_units_merge decides it.
static bool check_units_merge_global(const Link* unit, SymbolId symbol)
{
    return (unit->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL && unit->symbols.name[symbol] != 0;
}

// Resolves the exchange's records [begin, end), one partition's, in `table` (room for `slots` record indices, a power of
// two): each record's name's first record, and at each first, the name's resolution, symbol by symbol in the units'
// order as check_units_merge decides it. Returns the partition's first duplicate's key (unit << 32 | symbol), or
// UINT64_MAX, and its record in *duplicate.
static uint64_t check_units_merge_resolve(CheckUnitsMerge* merge, uint64_t begin, uint64_t end, uint32_t* table, uint64_t slots, uint32_t* duplicate, bool groups)
{
    memset(table, 0xff, slots * sizeof(*table));
    uint64_t first_key = UINT64_MAX;
    for (uint64_t r = begin; r < end; r += 1)
    {
        const CheckUnitsMergeRecord record = merge->records[r];
        const Link* unit = &merge->units[record.unit].kept.link;
        if (groups ? !check_units_group_signature(&merge->units[record.unit].kept, record.symbol) :
                     !check_units_selected_symbol(&merge->selection, record.unit, record.symbol))
        {
            merge->firsts[r] = UINT32_MAX;
            continue;
        }
        const NameId name = unit->symbols.name[record.symbol];
        const uint64_t hash = unit->names.hash[name];
        // The partition took the hash's low bits; the table takes its high ones.
        uint64_t slot = (hash >> 32) & (slots - 1);
        uint32_t first = UINT32_MAX;
        while (first == UINT32_MAX && table[slot] != UINT32_MAX)
        {
            const CheckUnitsMergeRecord other = merge->records[table[slot]];
            const Link* other_unit = &merge->units[other.unit].kept.link;
            const NameId other_name = other_unit->symbols.name[other.symbol];
            const bool same = other_unit->names.hash[other_name] == hash && other_unit->names.length[other_name] == unit->names.length[name] &&
                              memcmp(other_unit->names.bytes[other_name], unit->names.bytes[name], unit->names.length[name]) == 0;
            first = same ? table[slot] : UINT32_MAX;
            slot = same ? slot : (slot + 1) & (slots - 1);
        }
        const uint8_t flags = unit->symbols.flags[record.symbol];
        if (first == UINT32_MAX)
        {
            first = (uint32_t)r;
            table[slot] = first;
            merge->names[r] = (CheckUnitsMergeName){
                .defining = UINT32_MAX,
                .duplicate = UINT32_MAX,
                .visibility = flags & BUSTER_LINK_SYMBOL_VISIBILITY,
            };
        }
        merge->firsts[r] = first;
        if (groups)
        {
            continue;
        }
        CheckUnitsMergeName* resolution = &merge->names[first];
        if (check_units_visibility_rank(flags) > check_units_visibility_rank(resolution->visibility))
        {
            resolution->visibility = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
        }
        const bool weak = (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK;
        resolution->strong |= !weak;
        const uint8_t aligned = check_units_common_alignment(unit, record.symbol);
        resolution->common_alignment = aligned > resolution->common_alignment ? aligned : resolution->common_alignment;
        if (check_units_selected_definition(&merge->selection, record.unit, unit, record.symbol))
        {
            const bool earlier = resolution->defining != UINT32_MAX;
            const CheckUnitsMergeRecord definer = earlier ? merge->records[resolution->defining] : (CheckUnitsMergeRecord){ 0 };
            bool twice = false;
            const bool replaces = earlier && check_units_merge_replaces(&merge->units[definer.unit].kept.link, definer.symbol, unit, record.symbol, &twice);
            if (!earlier || replaces)
            {
                resolution->defining = (uint32_t)r;
            }
            else if (twice && resolution->duplicate == UINT32_MAX)
            {
                resolution->duplicate = (uint32_t)r;
                const uint64_t key = (uint64_t)record.unit << 32 | record.symbol;
                *duplicate = key < first_key ? (uint32_t)r : *duplicate;
                first_key = key < first_key ? key : first_key;
            }
        }
    }
    return first_key;
}

// A lane of the units' merge (see check_units_merge_lanes' comment); lane 0 sets merge->valid.
static void check_units_merge_lane(void* argument)
{
    CheckUnitsMerge* merge = argument;
    const uint32_t lane = lane_index();
    const uint32_t lanes = lane_count();
    const CheckUnitsUnit* units = merge->units;
    const uint32_t count = merge->count;
    Link* link = merge->link;
    // 1. The exchange: each lane's units' global symbols counted by partition, then placed.
    const Range64 range = lane_range_by_cost(merge->symbol_costs, count);
    uint64_t* counts = merge->counts + (uint64_t)lane * merge->partitions;
    memset(counts, 0, merge->partitions * sizeof(*counts));
    for (uint64_t u = range.begin; u < range.end; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        for (SymbolId s = 1; s < unit->symbols.count; s += 1)
        {
            if (check_units_merge_global(unit, s))
            {
                counts[unit->names.hash[unit->symbols.name[s]] & (merge->partitions - 1)] += 1;
            }
        }
    }
    lane_sync();
    if (lane == 0)
    {
        // Partition by partition, each lane's records after the lanes' before it.
        uint64_t place = 0;
        for (uint32_t p = 0; p < merge->partitions; p += 1)
        {
            merge->partition_begin[p] = place;
            for (uint32_t other = 0; other < lanes; other += 1)
            {
                const uint64_t taken = merge->counts[(uint64_t)other * merge->partitions + p];
                merge->counts[(uint64_t)other * merge->partitions + p] = place;
                place += taken;
            }
        }
        merge->partition_begin[merge->partitions] = place;
    }
    lane_sync();
    for (uint64_t u = range.begin; u < range.end; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        uint32_t* entries = merge->entries[u];
        entries[0] = UINT32_MAX;
        for (SymbolId s = 1; s < unit->symbols.count; s += 1)
        {
            uint32_t entry = UINT32_MAX;
            if (check_units_merge_global(unit, s))
            {
                uint64_t* place = &counts[unit->names.hash[unit->symbols.name[s]] & (merge->partitions - 1)];
                entry = (uint32_t)*place;
                merge->records[entry] = (CheckUnitsMergeRecord){ .unit = (uint32_t)u, .symbol = s };
                *place += 1;
            }
            entries[s] = entry;
        }
    }
    lane_sync();
    // Select COMDAT groups with the same hash partitions as global names, before their symbols are resolved. Every
    // unit then marks its own retained references and compacts its atom ordinals, independently of the other units.
    if (merge->selection.atoms != NULL)
    {
        Scratch group_scratch = scratch_begin(0);
        bool grouped = true;
        for (uint64_t p = lane_take(merge->partitions); p < merge->partitions; p = lane_take(merge->partitions))
        {
            const uint64_t begin = merge->partition_begin[p];
            const uint64_t end = merge->partition_begin[p + 1];
            uint64_t slots = 16;
            while (slots < 2 * (end - begin))
            {
                slots *= 2;
            }
            const uint64_t mark = group_scratch.arena->length;
            uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(group_scratch.arena, uint32_t, slots);
            grouped = grouped && table != NULL;
            uint32_t unused = UINT32_MAX;
            if (table != NULL)
            {
                check_units_merge_resolve(merge, begin, end, table, slots, &unused, true);
            }
            arena_pop_to(group_scratch.arena, mark);
        }
        scratch_end(group_scratch);
        merge->resolved[lane] = grouped;
        lane_sync();
        if (lane == 0)
        {
            for (uint32_t other = 0; other < lanes; other += 1)
            {
                merge->valid = merge->valid && merge->resolved[other];
            }
        }
        if (check_units_merge_proceed(merge))
        {
            for (uint64_t u = range.begin; u < range.end; u += 1)
            {
                const CheckUnitsKept* kept = &units[u].kept;
                for (AtomId a = 1; a < kept->link.atoms.count; a += 1)
                {
                    const SymbolId signature = kept->comdat_signature != NULL ? kept->comdat_signature[a] : 0;
                    const uint32_t first = signature != 0 ? merge->firsts[merge->entries[u][signature]] : UINT32_MAX;
                    merge->selection.atoms[u][a] = signature == 0 || merge->records[first].unit == u;
                }
                check_units_selection_finish(&merge->selection, units, (uint32_t)u);
            }
        }
        lane_sync();
        if (lane == 0 && merge->valid)
        {
            merge->valid = check_units_selection_valid(&merge->selection, units, count);
            merge->discarded_local = !merge->valid;
        }
        lane_sync();
    }
    // Each lane's partitions resolved, in a table of its own scratch's.
    Scratch scratch = scratch_begin(0);
    uint64_t lane_key = UINT64_MAX;
    uint32_t lane_record = UINT32_MAX;
    bool resolved = merge->valid;
    for (uint64_t p = lane_take(merge->partitions); p < merge->partitions; p = lane_take(merge->partitions))
    {
        const uint64_t begin = merge->partition_begin[p];
        const uint64_t end = merge->partition_begin[p + 1];
        uint64_t slots = 16;
        while (slots < 2 * (end - begin))
        {
            slots *= 2;
        }
        const uint64_t mark = scratch.arena->length;
        uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, slots);
        resolved = resolved && table != NULL;
        uint32_t record = UINT32_MAX;
        const uint64_t key = table != NULL && merge->valid ? check_units_merge_resolve(merge, begin, end, table, slots, &record, false) : UINT64_MAX;
        lane_record = key < lane_key ? record : lane_record;
        lane_key = key < lane_key ? key : lane_key;
        arena_pop_to(scratch.arena, mark);
    }
    scratch_end(scratch);
    merge->duplicate_key[lane] = lane_key;
    merge->duplicate_record[lane] = lane_record;
    merge->resolved[lane] = resolved;
    lane_sync();
    // 2. The ids: each unit's symbols that take ids of their own counted, their bases, then theirs and the others'.
    for (uint64_t u = range.begin; u < range.end; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        const uint32_t* entries = merge->entries[u];
        uint64_t own = 0;
        for (SymbolId s = 1; merge->valid && s < unit->symbols.count; s += 1)
        {
            own += check_units_selected_symbol(&merge->selection, (uint32_t)u, s) &&
                   (entries[s] == UINT32_MAX || merge->firsts[entries[s]] == entries[s]);
        }
        merge->unit_ids[u] = own;
    }
    lane_sync();
    if (lane == 0)
    {
        uint64_t duplicate_key = UINT64_MAX;
        uint32_t duplicate = UINT32_MAX;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            merge->valid = merge->valid && merge->resolved[other];
            duplicate = merge->duplicate_key[other] < duplicate_key ? merge->duplicate_record[other] : duplicate;
            duplicate_key = merge->duplicate_key[other] < duplicate_key ? merge->duplicate_key[other] : duplicate_key;
        }
        if (merge->valid && duplicate != UINT32_MAX)
        {
            // The first duplicate in the units' order, as the twin reports it: the name's, its unit's and the first
            // strong definition's.
            const CheckUnitsMergeRecord record = merge->records[duplicate];
            const CheckUnitsMergeName* resolution = &merge->names[merge->firsts[duplicate]];
            const CheckUnitsMergeRecord definer = merge->records[resolution->defining];
            const Link* unit = &units[record.unit].kept.link;
            const NameId name = unit->symbols.name[record.symbol];
            fprintf(stderr, "error: multiple definition of '%.*s': %s defines it, and %s first\n", (int)unit->names.length[name],
                    (const char*)unit->names.bytes[name], units[record.unit].path, units[definer.unit].path);
            merge->valid = false;
            merge->duplicated = true;
        }
        uint64_t next = 1;
        for (uint32_t u = 0; u < count; u += 1)
        {
            const uint64_t own = merge->unit_ids[u];
            merge->unit_ids[u] = next;
            next += own;
        }
        merge->valid = merge->valid && next < UINT32_MAX / 2;
        merge->next = merge->valid ? (SymbolId)next : 0;
    }
    if (check_units_merge_proceed(merge))
    {
        for (uint64_t u = range.begin; u < range.end; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const uint32_t* entries = merge->entries[u];
            SymbolId* map = merge->maps[u];
            SymbolId id = (SymbolId)merge->unit_ids[u];
            map[0] = 0;
            for (SymbolId s = 1; s < unit->symbols.count; s += 1)
            {
                if (!check_units_selected_symbol(&merge->selection, (uint32_t)u, s))
                {
                    map[s] = 0;
                    continue;
                }
                const uint32_t entry = entries[s];
                if (entry == UINT32_MAX || merge->firsts[entry] == entry)
                {
                    // A local, or a name's first: the id, and what the symbol it stands for takes from the units.
                    map[s] = id;
                    uint32_t source_unit = (uint32_t)u;
                    SymbolId source_symbol = s;
                    uint8_t visibility = unit->symbols.flags[s] & BUSTER_LINK_SYMBOL_VISIBILITY;
                    uint8_t strong = (unit->symbols.flags[s] & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_WEAK;
                    uint8_t common_alignment = check_units_common_alignment(unit, s);
                    bool defined = check_units_selected_definition(&merge->selection, (uint32_t)u, unit, s);
                    if (entry != UINT32_MAX)
                    {
                        const CheckUnitsMergeName* resolution = &merge->names[entry];
                        defined = resolution->defining != UINT32_MAX;
                        source_unit = defined ? merge->records[resolution->defining].unit : source_unit;
                        source_symbol = defined ? merge->records[resolution->defining].symbol : source_symbol;
                        visibility = resolution->visibility;
                        strong = resolution->strong;
                        common_alignment = resolution->common_alignment;
                        merge->record_ids[entry] = id;
                    }
                    merge->source_unit[id] = source_unit;
                    merge->source_symbol[id] = source_symbol;
                    merge->defined[id] = defined;
                    merge->visibility[id] = visibility;
                    merge->strong[id] = strong;
                    merge->common_alignment[id] = common_alignment;
                    id += 1;
                }
            }
        }
    }
    lane_sync();
    if (merge->next != 0)
    {
        for (uint64_t u = range.begin; u < range.end; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const uint32_t* entries = merge->entries[u];
            SymbolId* map = merge->maps[u];
            for (SymbolId s = 1; s < unit->symbols.count; s += 1)
            {
                const uint32_t entry = entries[s];
                map[s] = !check_units_selected_symbol(&merge->selection, (uint32_t)u, s) ? 0 :
                         entry != UINT32_MAX && merge->firsts[entry] != entry ? merge->record_ids[merge->firsts[entry]] : map[s];
            }
        }
    }
    lane_sync();
    // 3. On lane 0: -g's replaced functions, the names no unit defines, the link's room and its initialization.
    if (lane == 0 && merge->valid)
    {
        const SymbolId next = merge->next;
        for (uint32_t u = 0; merge->valid && merge->debug != NULL && u < count; u += 1)
        {
            const ObjectDebug* own = &units[u].kept.debug;
            merge->described[u] = BUSTER_ARENA_PUSH_ARRAY(merge->scratch, SymbolId, (uint64_t)check_units_debug_symbol_count(own) + 1);
            merge->valid = merge->described[u] != NULL;
            for (uint32_t f = 0; merge->valid && f < check_units_debug_symbol_count(own); f += 1)
            {
                const SymbolId s = check_units_debug_symbol(own, f);
                const SymbolId merged = merge->maps[u][s];
                const AtomId atom = units[u].kept.link.symbols.atom[s];
                const bool discarded = f >= own->function_count && atom != 0 && check_units_selected_atom(&merge->selection, u, atom) == 0;
                const bool kept = !discarded && merged != 0 && merge->source_unit[merged] == u && merge->source_symbol[merged] == s && merge->defined[merged];
                merge->described[u][f] = discarded ? 0 : kept ? merged : (SymbolId)(next + merge->replaced);
                merge->replaced += !discarded && !kept;
            }
        }
        String8* externals = merge->valid ? BUSTER_ARENA_PUSH_ARRAY(merge->scratch, String8, next) : NULL;
        uint32_t external_count = 0;
        merge->valid = merge->valid && externals != NULL;
        for (SymbolId m = 1; merge->valid && m < next; m += 1)
        {
            const Link* unit = &units[merge->source_unit[m]].kept.link;
            const SymbolId s = merge->source_symbol[m];
            if (!merge->defined[m] && check_units_merge_global(unit, s))
            {
                const NameId name = unit->symbols.name[s];
                externals[external_count++] = (String8){ .pointer = (char*)unit->names.bytes[name], .length = unit->names.length[name] };
            }
        }
        uint64_t atoms = 0;
        uint64_t fixups = 0;
        uint64_t wides = 0;
        uint64_t priorities = 0;
        for (uint32_t u = 0; u < count; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const LinkCapacity kept = check_units_selected_counts(&merge->selection, u, unit);
            atoms += kept.atoms;
            fixups += kept.fixups;
            wides += kept.wide_addends;
            priorities += kept.priorities;
        }
        uint64_t name_bytes = 0;
        uint64_t atom_bytes = 0;
        const LinkCapacity runtime =
            merge->valid ? bind_capacity_needed(merge->binding->runtime, externals, external_count, merge->scratch, &name_bytes, &atom_bytes) : (LinkCapacity){ 0 };
        const uint64_t total_names = (uint64_t)next - 1 + merge->replaced + runtime.names;
        const uint64_t total_atoms = atoms + runtime.atoms;
        const uint64_t total_symbols = (uint64_t)next - 1 + merge->replaced + runtime.symbols;
        const uint64_t total_fixups = fixups + runtime.fixups;
        const uint64_t total_wides = wides + runtime.wide_addends;
        const uint64_t total_priorities = priorities + runtime.priorities;
        merge->valid = merge->valid && atoms < UINT32_MAX && fixups < UINT32_MAX && wides < UINT32_MAX && priorities < UINT32_MAX && total_names < UINT32_MAX &&
                       total_atoms < UINT32_MAX && total_symbols < UINT32_MAX && total_fixups < UINT32_MAX && total_wides < UINT32_MAX && total_priorities < UINT32_MAX;
        const LinkCapacity capacity = {
            .names = merge->valid ? (uint32_t)total_names : 0,
            .atoms = merge->valid ? (uint32_t)total_atoms : 0,
            .symbols = merge->valid ? (uint32_t)total_symbols : 0,
            .fixups = merge->valid ? (uint32_t)total_fixups : 0,
            .wide_addends = merge->valid ? (uint32_t)total_wides : 0,
            .priorities = merge->valid ? (uint32_t)total_priorities : 0,
        };
        merge->sized = merge->valid && link_initialize(link, merge->output, merge->output, capacity);
        merge->valid = merge->sized;
    }
    // 4. The symbols and their names, each lane a range of the ids: the names counted, placed and copied.
    const Range64 ids = check_units_merge_proceed(merge) ? lane_range(merge->next - 1) : (Range64){ 0 };
    uint64_t named = 0;
    uint64_t bytes = 0;
    for (uint64_t at = ids.begin; at < ids.end; at += 1)
    {
        const SymbolId m = (SymbolId)(at + 1);
        const Link* unit = &units[merge->source_unit[m]].kept.link;
        const NameId own = unit->symbols.name[merge->source_symbol[m]];
        named += own != 0;
        bytes += own != 0 ? (uint64_t)unit->names.length[own] + 1 : 0;
    }
    merge->named[lane] = named;
    merge->name_bytes[lane] = bytes;
    lane_sync();
    if (lane == 0 && merge->valid)
    {
        uint64_t name_at = link->names.count;
        uint64_t byte_at = 0;
        for (uint32_t other = 0; other < lanes; other += 1)
        {
            merge->name_base[other] = name_at;
            merge->byte_base[other] = byte_at;
            name_at += merge->named[other];
            byte_at += merge->name_bytes[other];
        }
        merge->name_storage = byte_at != 0 ? arena_push(link->name_arena, byte_at, 1) : NULL;
        merge->valid = (byte_at == 0 || merge->name_storage != NULL) && name_at <= link->names.capacity && merge->next <= link->symbols.capacity;
    }
    if (check_units_merge_proceed(merge))
    {
        NameId name_id = (NameId)merge->name_base[lane];
        uint8_t* storage = merge->name_storage + merge->byte_base[lane];
        for (uint64_t at = ids.begin; at < ids.end; at += 1)
        {
            const SymbolId m = (SymbolId)(at + 1);
            const Link* unit = &units[merge->source_unit[m]].kept.link;
            const SymbolId s = merge->source_symbol[m];
            const NameId own = unit->symbols.name[s];
            NameId name = 0;
            if (own != 0)
            {
                const uint32_t length = unit->names.length[own];
                memcpy(storage, unit->names.bytes[own], length);
                storage[length] = 0;
                link->names.bytes[name_id] = storage;
                link->names.length[name_id] = length;
                link->names.hash[name_id] = unit->names.hash[own];
                name = name_id;
                name_id += 1;
                storage += (uint64_t)length + 1;
            }
            const bool defined = merge->defined[m];
            const bool absolute = defined && unit->symbols.source[s] == BUSTER_LINK_SOURCE_ABSOLUTE;
            uint8_t flags = (uint8_t)((unit->symbols.flags[s] & ~BUSTER_LINK_SYMBOL_VISIBILITY) | merge->visibility[m]);
            if (!defined && (flags & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_WEAK && merge->strong[m])
            {
                flags = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_GLOBAL);
            }
            link->symbols.name[m] = name;
            link->symbols.atom[m] = 0;
            link->symbols.value[m] = absolute ? unit->symbols.value[s] : 0;
            link->symbols.size[m] = absolute ? unit->symbols.size[s] : 0;
            link->symbols.flags[m] = flags;
            link->symbols.source[m] = absolute ? BUSTER_LINK_SOURCE_ABSOLUTE : defined ? BUSTER_LINK_SOURCE_PROGRAM : BUSTER_LINK_SOURCE_NONE;
            link->symbols.got_slot[m] = 0;
            link->symbols.dynamic_index[m] = 0;
            link->symbols.version[m] = 0;
            link->symbols.reference[m] = 0;
        }
    }
    lane_sync();
    // 5. On lane 0: the replaced functions' symbols, then the runtime, which binding joins before the atoms; and each
    // unit's places among the atoms, fixups, wide addends and priorities.
    if (lane == 0 && merge->valid)
    {
        link->names.count = (uint32_t)(merge->name_base[lanes - 1] + merge->named[lanes - 1]);
        link->symbols.count = merge->next;
        for (uint32_t u = 0; merge->valid && merge->debug != NULL && u < count; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const ObjectDebug* own = &units[u].kept.debug;
            for (uint32_t f = 0; merge->valid && f < check_units_debug_symbol_count(own); f += 1)
            {
                const SymbolId s = check_units_debug_symbol(own, f);
                const NameId own_name = unit->symbols.name[s];
                if (merge->described[u][f] != 0 && merge->described[u][f] != merge->maps[u][s])
                {
                    const NameId name = own_name != 0 ? link_add_name_hashed(link, unit->names.bytes[own_name], unit->names.length[own_name], unit->names.hash[own_name]) : 0;
                    const uint8_t flags = (uint8_t)((unit->symbols.flags[s] & ~(BUSTER_LINK_SYMBOL_BINDING | BUSTER_LINK_SYMBOL_VISIBILITY)) | BUSTER_LINK_SYMBOL_LOCAL |
                                                    BUSTER_LINK_SYMBOL_DEFAULT);
                    merge->valid = (own_name == 0 || name != 0) && link_add_symbol(link, name, 0, 0, 0, flags, BUSTER_LINK_SOURCE_PROGRAM) == merge->described[u][f];
                }
            }
        }
        merge->symbols_added = merge->valid;
        CheckProgramBinding* binding = merge->binding;
        if (merge->valid && !bind_begin(binding->bind, link, binding->runtime, binding->path, merge->bytes, count, binding->arena))
        {
            const NameId name = binding->bind->name;
            if (name != 0 && name < link->names.count)
            {
                snprintf(binding->failed_name, sizeof(binding->failed_name), "%.*s", (int)link->names.length[name], (const char*)link->names.bytes[name]);
            }
            merge->valid = false;
        }
        merge->begun = merge->valid;
        uint64_t atom_at = link->atoms.count;
        uint64_t fixup_at = link->fixups.count;
        uint64_t wide_at = link->fixups.wide_count;
        uint64_t priority_at = link->priorities.count;
        for (uint32_t u = 0; u < count; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const LinkCapacity kept = check_units_selected_counts(&merge->selection, u, unit);
            merge->atom_base[u] = atom_at;
            merge->fixup_base[u] = fixup_at;
            merge->wide_base[u] = wide_at;
            merge->priority_base[u] = priority_at;
            atom_at += kept.atoms;
            fixup_at += kept.fixups;
            wide_at += kept.wide_addends;
            priority_at += kept.priorities;
        }
        merge->valid = merge->valid && atom_at <= link->atoms.capacity && fixup_at <= link->fixups.capacity && wide_at <= link->fixups.wide_capacity &&
                       priority_at <= link->priorities.capacity;
    }
    // 6. Each unit's atoms, fixups, priorities and definitions at their places, each lane its units by their records.
    if (check_units_merge_proceed(merge))
    {
        const Range64 records = lane_range_by_cost(merge->record_costs, count);
        for (uint64_t u = records.begin; u < records.end; u += 1)
        {
            const Link* unit = &units[u].kept.link;
            const SymbolId* map = merge->maps[u];
            const AtomId atom_base = (AtomId)merge->atom_base[u];
            FixupId fixup = (FixupId)merge->fixup_base[u];
            uint32_t wide = (uint32_t)merge->wide_base[u];
            for (AtomId a = 1; a < unit->atoms.count; a += 1)
            {
                const AtomId selected = check_units_selected_atom(&merge->selection, (uint32_t)u, a);
                if (selected == 0)
                {
                    continue;
                }
                const AtomId placed = atom_base + selected - 1;
                const uint32_t fixup_count = unit->atoms.fixup_count[a];
                link->atoms.class[placed] = unit->atoms.class[a];
                link->atoms.alignment_log2[placed] = unit->atoms.alignment_log2[a];
                link->atoms.unit[placed] = (uint32_t)u;
                link->atoms.size[placed] = unit->atoms.size[a];
                link->atoms.section[placed] = unit->atoms.section[a];
                link->atoms.bytes[placed] = unit->atoms.bytes[a];
                link->atoms.fixup_begin[placed] = fixup_count != 0 ? fixup : 0;
                link->atoms.fixup_count[placed] = fixup_count;
                link->atoms.address[placed] = 0;
                const FixupId begin = unit->atoms.fixup_begin[a];
                for (uint32_t i = 0; i < fixup_count; i += 1)
                {
                    const FixupId f = begin + i;
                    const int64_t addend = link_fixup_addend(unit, f);
                    const bool wide_addend = addend < INT32_MIN || addend > INT32_MAX;
                    const uint8_t kind = unit->fixups.kind[f] & BUSTER_LINK_FIXUP_KIND;
                    link->fixups.offset[fixup] = unit->fixups.offset[f];
                    link->fixups.target[fixup] = map[unit->fixups.target[f]];
                    link->fixups.kind[fixup] = wide_addend ? kind | BUSTER_LINK_FIXUP_WIDE : kind;
                    link->fixups.addend[fixup] = wide_addend ? (int32_t)wide : (int32_t)addend;
                    if (wide_addend)
                    {
                        link->fixups.wide_addends[wide] = addend;
                        wide += 1;
                    }
                    fixup += 1;
                }
            }
            uint64_t priority = merge->priority_base[u];
            for (uint32_t p = 1; p < unit->priorities.count; p += 1)
            {
                const AtomId selected = check_units_selected_atom(&merge->selection, (uint32_t)u, unit->priorities.atom[p]);
                if (selected != 0)
                {
                    link->priorities.atom[priority] = atom_base + selected - 1;
                    link->priorities.priority[priority] = unit->priorities.priority[p];
                    priority += 1;
                }
            }
            for (SymbolId s = 1; s < unit->symbols.count; s += 1)
            {
                const SymbolId merged = map[s];
                if (merged != 0 && unit->symbols.atom[s] != 0 && merge->source_unit[merged] == u && merge->source_symbol[merged] == s && merge->defined[merged])
                {
                    const AtomId atom = atom_base + check_units_selected_atom(&merge->selection, (uint32_t)u, unit->symbols.atom[s]) - 1;
                    const bool common = (unit->symbols.flags[s] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_COMMON;
                    link->symbols.atom[merged] = atom;
                    link->symbols.value[merged] = unit->symbols.value[s];
                    link->symbols.size[merged] = unit->symbols.size[s];
                    // A kept common takes its name's commons' largest alignment, as the twin's does; its atom is this unit's.
                    link->atoms.alignment_log2[atom] = common && merge->common_alignment[merged] > link->atoms.alignment_log2[atom] ? merge->common_alignment[merged] :
                                                                                                                                   link->atoms.alignment_log2[atom];
                }
            }
            for (uint32_t f = 0; merge->described != NULL && merge->described[u] != NULL && f < check_units_debug_symbol_count(&units[u].kept.debug); f += 1)
            {
                const SymbolId s = check_units_debug_symbol(&units[u].kept.debug, f);
                if (merge->described[u][f] != 0 && merge->described[u][f] != map[s])
                {
                    link->symbols.atom[merge->described[u][f]] = atom_base + check_units_selected_atom(&merge->selection, (uint32_t)u, unit->symbols.atom[s]) - 1;
                    link->symbols.value[merge->described[u][f]] = unit->symbols.value[s];
                    link->symbols.size[merge->described[u][f]] = unit->symbols.size[s];
                }
            }
        }
    }
    lane_sync();
    // 7. On lane 0: the tables' counts, and -g's DWARF.
    if (lane == 0)
    {
        merge->atoms_added = merge->valid;
    }
    if (lane == 0 && merge->valid)
    {
        const Link* last = &units[count - 1].kept.link;
        const LinkCapacity kept = check_units_selected_counts(&merge->selection, count - 1, last);
        link->atoms.count = (uint32_t)(merge->atom_base[count - 1] + kept.atoms);
        link->fixups.count = (uint32_t)(merge->fixup_base[count - 1] + kept.fixups);
        link->fixups.wide_count = (uint32_t)(merge->wide_base[count - 1] + kept.wide_addends);
        link->priorities.count = (uint32_t)(merge->priority_base[count - 1] + kept.priorities);
        if (merge->debug != NULL)
        {
            merge->valid = check_units_merge_debug(merge->debug, units, count, merge->described, merge->output);
            if (!merge->valid)
            {
                fprintf(stderr, "error: can't make the debug information: no memory, or sections past DWARF32's\n");
            }
        }
    }
}

// The units' merge on `lanes` lanes of the units' flow (see the comment before CheckUnitsMerge), a round of `flow`'s
// (#905): check_units_merge's link and results, from shared state in `scratch` that lane 0 reserves before the round.
// Without its memory, return the round's lease before check_units_merge runs on lane 0 alone. The caller's final
// return remains idempotent, and a failed early return stays in flow->slots_lost.
static bool check_units_merge_lanes(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, ExecutableDebug* debug, Arena* output,
                                    Arena* bytes, Arena* scratch, uint32_t lanes, CheckUnitsFlow* flow, WorkerBudgetLease* lease)
{
    const uint64_t mark = scratch->length;
    uint64_t symbols = 0;
    for (uint32_t u = 0; u < count; u += 1)
    {
        symbols += units[u].kept.link.symbols.count - 1;
    }
    uint32_t partitions = 1;
    while (partitions < BUSTER_CHECK_UNITS_MERGE_PARTITIONS * lanes)
    {
        partitions *= 2;
    }
    CheckUnitsMerge merge = {
        .link = link,
        .units = units,
        .count = count,
        .binding = binding,
        .debug = debug,
        .output = output,
        .bytes = bytes,
        .scratch = scratch,
        .partitions = partitions,
        .valid = symbols < UINT32_MAX / 2,
    };
    merge.symbol_costs = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    merge.record_costs = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    merge.counts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)lanes * partitions);
    merge.partition_begin = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)partitions + 1);
    merge.records = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckUnitsMergeRecord, symbols + 1);
    merge.firsts = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, symbols + 1);
    merge.names = BUSTER_ARENA_PUSH_ARRAY(scratch, CheckUnitsMergeName, symbols + 1);
    merge.record_ids = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, symbols + 1);
    merge.entries = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t*, count);
    merge.maps = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId*, count);
    merge.unit_ids = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    merge.source_unit = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, symbols + 1);
    merge.source_symbol = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, symbols + 1);
    merge.defined = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1);
    merge.visibility = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1);
    merge.strong = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1);
    merge.common_alignment = BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, symbols + 1);
    merge.described = debug != NULL ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId*, count) : NULL;
    merge.atom_base = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    merge.fixup_base = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    merge.wide_base = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    merge.priority_base = BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count);
    bool reserved = merge.symbol_costs != NULL && merge.record_costs != NULL && merge.counts != NULL && merge.partition_begin != NULL && merge.records != NULL &&
                    merge.firsts != NULL && merge.names != NULL && merge.record_ids != NULL && merge.entries != NULL && merge.maps != NULL && merge.unit_ids != NULL &&
                    merge.source_unit != NULL && merge.source_symbol != NULL && merge.defined != NULL && merge.visibility != NULL && merge.strong != NULL &&
                    merge.common_alignment != NULL && (debug == NULL || merge.described != NULL) && merge.atom_base != NULL && merge.fixup_base != NULL && merge.wide_base != NULL &&
                    merge.priority_base != NULL;
    uint64_t symbol_cost = 0;
    uint64_t record_cost = 0;
    for (uint32_t u = 0; reserved && u < count; u += 1)
    {
        const Link* unit = &units[u].kept.link;
        merge.entries[u] = BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, unit->symbols.count);
        merge.maps[u] = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, unit->symbols.count);
        reserved = merge.entries[u] != NULL && merge.maps[u] != NULL;
        symbol_cost += unit->symbols.count;
        record_cost += (uint64_t)unit->symbols.count + unit->atoms.count + unit->fixups.count;
        merge.symbol_costs[u] = symbol_cost;
        merge.record_costs[u] = record_cost;
        if (merge.described != NULL)
        {
            merge.described[u] = NULL;
        }
    }
    reserved = reserved && check_units_selection_initialize(&merge.selection, units, count, scratch);
    merge.valid = merge.valid && reserved;
    bool result = false;
    if (!reserved)
    {
        // Without its memory, the twin, from the link as it was.
        arena_pop_to(scratch, mark);
        check_units_slots_back(flow, lease);
        result = check_units_merge(link, units, count, binding, debug, output, bytes, scratch);
    }
    else
    {
        lane_flow_round_begin(&flow->rounds, CHECK_UNITS_ROUND_MERGE, &merge, lanes);
        check_units_merge_lane(&merge);
        lane_flow_round_end(&flow->rounds);
        result = merge.valid;
        if (!merge.atoms_added && !merge.duplicated && !merge.discarded_local && (!merge.symbols_added || merge.begun))
        {
            fprintf(stderr, "fatal error: can't link the units: %s (link error %u)\n", merge.sized ? "their records don't fit the link" : "no memory",
                    (uint32_t)link->error);
        }
    }
    arena_pop_to(scratch, mark);
    return result;
}

// Up to `size` - 1 bytes of the file at `path` into `buffer`, NUL-terminated: their count, 0 if it can't be read.
static uint64_t check_units_read_text(const char* path, char* buffer, uint64_t size)
{
    uint64_t length = 0;
    const int file = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t got = file >= 0 ? 1 : 0;
    while (got > 0 && length + 1 < size)
    {
        got = read(file, buffer + length, size - 1 - length);
        length += got > 0 ? (uint64_t)got : 0;
    }
    if (file >= 0)
    {
        close(file);
    }
    buffer[length] = 0;
    return length;
}

// The number after `key` at the start of one of `text`'s lines (blanks skipped), times `unit`; `missing` if no line
// starts with `key` and a number.
static uint64_t check_units_text_value(const char* text, const char* key, uint64_t unit, uint64_t missing)
{
    const uint64_t key_length = strlen(key);
    uint64_t result = missing;
    bool found = false;
    const char* line = text;
    while (!found && line != NULL && *line != 0)
    {
        if (strncmp(line, key, key_length) == 0)
        {
            char* end = NULL;
            const unsigned long long value = strtoull(line + key_length, &end, 10);
            found = end != line + key_length;
            result = found ? (value <= UINT64_MAX / unit ? (uint64_t)value * unit : UINT64_MAX) : result;
        }
        const char* next = strchr(line, '\n');
        line = next != NULL ? next + 1 : NULL;
    }
    return result;
}

// What the process may still take of the machine's memory (#1109), read from `meminfo`, `cgroup` and the cgroup v2
// hierarchy at `root` (/proc/meminfo, /proc/self/cgroup and /sys/fs/cgroup, which test-check-units replaces with files of
// its own): the least of MemAvailable (or, without it, the free pages, _SC_AVPHYS_PAGES, which leave out what the kernel
// could take back) and, for each cgroup from the process's own (`cgroup`'s "0::" line) up to the hierarchy's root, its
// memory.max less its memory.current, the files' pages on its lists (memory.stat's active_file and inactive_file)
// counted as free, since the kernel takes them back before it kills. A limit whose memory.current can't be read, or
// doesn't hold a number, leaves nothing: what the cgroup uses is unknown, and the lanes then take turns. A cgroup v1
// limit isn't read: under one, the machine's memory is the bound.
static uint64_t check_units_memory_read(const char* meminfo, const char* cgroup, const char* root)
{
    char text[8192];
    uint64_t result = check_units_read_text(meminfo, text, sizeof(text)) != 0 ? check_units_text_value(text, "MemAvailable:", 1024, UINT64_MAX) : UINT64_MAX;
    if (result == UINT64_MAX)
    {
        const long free_pages = sysconf(_SC_AVPHYS_PAGES);
        const long page_bytes = sysconf(_SC_PAGESIZE);
        result = free_pages > 0 && page_bytes > 0 ? (uint64_t)free_pages * (uint64_t)page_bytes : 0;
    }
    char directory[PATH_MAX];
    const uint64_t prefix = strlen(root);
    const uint64_t length = check_units_read_text(cgroup, text, sizeof(text));
    const char* line = length != 0 && strncmp(text, "0::/", 4) == 0 ? text : length != 0 ? strstr(text, "\n0::/") : NULL;
    const char* path = line != NULL ? line + (line == text ? 3 : 4) : NULL;
    const uint64_t path_length = path != NULL ? strcspn(path, "\n") : 0;
    uint64_t end = path != NULL && prefix + path_length < sizeof(directory) ? prefix + path_length : 0;
    if (end != 0)
    {
        memcpy(directory, root, prefix);
        memcpy(directory + prefix, path, path_length);
        // The hierarchy's root itself, "0::/", is root alone.
        end = directory[end - 1] == '/' ? end - 1 : end;
    }
    while (end >= prefix)
    {
        char file[PATH_MAX + 32];
        snprintf(file, sizeof(file), "%.*s/memory.max", (int)end, directory);
        const uint64_t limit = check_units_read_text(file, text, sizeof(text)) != 0 ? check_units_text_value(text, "", 1, UINT64_MAX) : UINT64_MAX;
        if (limit != UINT64_MAX)
        {
            snprintf(file, sizeof(file), "%.*s/memory.current", (int)end, directory);
            const uint64_t current = check_units_read_text(file, text, sizeof(text)) != 0 ? check_units_text_value(text, "", 1, UINT64_MAX) : UINT64_MAX;
            snprintf(file, sizeof(file), "%.*s/memory.stat", (int)end, directory);
            const bool stat = check_units_read_text(file, text, sizeof(text)) != 0;
            const uint64_t files = stat ? check_units_text_value(text, "active_file ", 1, 0) + check_units_text_value(text, "inactive_file ", 1, 0) : 0;
            const uint64_t used = current == UINT64_MAX ? limit : current > files ? current - files : 0;
            const uint64_t headroom = limit > used ? limit - used : 0;
            result = headroom < result ? headroom : result;
        }
        // Up a level: to the last '/' past root, or past root's own level, out of the loop.
        uint64_t parent = end;
        while (parent > prefix && directory[parent - 1] != '/')
        {
            parent -= 1;
        }
        end = end > prefix ? parent - 1 : 0;
    }
    return result;
}

// What the process may still take of the machine's memory when the units' run starts (check_units_memory_read).
static uint64_t check_units_memory_available(void)
{
    return check_units_memory_read("/proc/meminfo", "/proc/self/cgroup", "/sys/fs/cgroup");
}

// The peak a unit of `tokens` preprocessed tokens adds to the process's resident memory, by estimate (#1109): what its
// parse, checking and emission take per token, and what any unit takes, measured on units from a few tokens to
// main.c's (docs/translation-units.md, "Memory").
static uint64_t check_units_estimate(uint32_t tokens)
{
    return (uint64_t)tokens * BUSTER_CHECK_UNITS_TOKEN_BYTES + BUSTER_CHECK_UNITS_UNIT_BYTES;
}

// Takes `need` bytes of the run's memory for a lane that holds `held` of it, in place of those: true if they fit beside
// what the other lanes hold, or no other lane holds any.
static bool check_units_memory_take(CheckUnitsMemory* memory, uint64_t held, uint64_t need)
{
    uint64_t total = __atomic_load_n(&memory->held, __ATOMIC_ACQUIRE);
    bool fits = total - held + need <= memory->limit || total == held;
    while (fits && !__atomic_compare_exchange_n(&memory->held, &total, total - held + need, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        fits = total - held + need <= memory->limit || total == held;
    }
    if (fits && held == 0)
    {
        const uint32_t holders = __atomic_add_fetch(&memory->holders, 1, __ATOMIC_ACQ_REL);
        uint32_t most = __atomic_load_n(&memory->most, __ATOMIC_RELAXED);
        while (holders > most && !__atomic_compare_exchange_n(&memory->most, &most, holders, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        {
        }
    }
    return fits;
}

// `lane`'s part of the run's memory, returned (#1109), which wakes the lanes waiting for it; its pages stay.
static void check_units_memory_return(CheckUnitsLane* lane)
{
    if (lane->held != 0)
    {
        __atomic_sub_fetch(&lane->memory->holders, 1, __ATOMIC_ACQ_REL);
        __atomic_sub_fetch(&lane->memory->held, lane->held, __ATOMIC_RELEASE);
        lane->held = 0;
        lane_advance(&lane->memory->released);
    }
}

// What `lane` held of the run's memory, given back (#1109): its three arenas' pages from the first huge page boundary past
// their lengths (arena_discard; none after its release), and its part of the run's memory (check_units_memory_return).
static void check_units_memory_give(CheckUnitsLane* lane)
{
    if (lane->held != 0)
    {
        arena_discard(&lane->arena);
        arena_discard(&lane->scratch);
        arena_discard(&lane->output);
        check_units_memory_return(lane);
    }
}

// A unit of `tokens` preprocessed tokens on `lane`, before its parse (#1109, check_driver_unit): it takes its estimated
// peak from the run's memory, in place of what the lane holds, where that is less. If that doesn't fit beside what the
// other lanes hold, the lane gives back what it holds and waits until it fits or no other lane holds any.
static void check_units_admit(CheckUnitsLane* lane, uint32_t tokens)
{
    CheckUnitsMemory* memory = lane->memory;
    const uint64_t need = check_units_estimate(tokens);
    if (memory != NULL && need > lane->held && !check_units_memory_take(memory, lane->held, need))
    {
        check_units_memory_give(lane);
        __atomic_add_fetch(&memory->waiting, 1, __ATOMIC_ACQ_REL);
        __atomic_add_fetch(&memory->waits, 1, __ATOMIC_RELAXED);
        // The word is read before each try, so that a lane that gives memory back after a failed try wakes this one.
        uint32_t released = __atomic_load_n(&memory->released, __ATOMIC_ACQUIRE) & ~1u;
        while (!check_units_memory_take(memory, 0, need))
        {
            released = lane_sleep(&memory->released, released);
        }
        __atomic_sub_fetch(&memory->waiting, 1, __ATOMIC_ACQ_REL);
    }
    lane->held = memory != NULL && need > lane->held ? need : lane->held;
}

// The lanes' setup: the intern table and the file cache a lane's units share, the arenas they take turns with, and the
// memory of the links it keeps (once: the links it kept stay there), the first time the lane takes a unit. False, with
// the lane failed, if it can't be reserved: what was stays the lane's, for its release.
static bool check_units_lane_ready(CheckUnitsLane* lane, const CheckDriverCommand* command)
{
    if (!lane->ready && !lane->failed)
    {
        lane->arena = arena_reserve(1ull << 36);
        lane->scratch = arena_reserve(1ull << 36);
        lane->output = arena_reserve(1ull << 36);
        lane->kept = lane->kept.base != 0 ? lane->kept : arena_reserve(1ull << 36);
        lane->kept_bytes = lane->kept_bytes.base != 0 ? lane->kept_bytes : arena_reserve(1ull << 36);
        lane->ready = lane->arena.base != 0 && lane->scratch.base != 0 && lane->output.base != 0 && lane->kept.base != 0 && lane->kept_bytes.base != 0 &&
                      intern_initialize(&lane->identifiers) && file_cache_initialize(&lane->cache, &lane->identifiers, pp_prepare_options(&command->options));
        lane->failed = !lane->ready;
    }
    return lane->ready;
}

// A lane's memory that the link doesn't read, its file cache, intern table, three arenas and its functions' lane 0's
// writer's columns, released when it has compiled its units (#778): into its pool (arena_pool), when its caller set
// one, whose MADV_DONTNEED frees an arena's pages under the address space's lock for reading, so that the lanes free
// theirs at once, at the end of their run, where the calling thread would free every lane's after it, one after another.
// The links it kept stay in their arenas.
// A lane whose setup failed releases what it reserved before the failure, which its table and cache keep as their
// initializers left them (a lane that took no unit, or released them already, has them zero, which their releases take).
// Then it gives back what it held of the run's memory (check_units_memory_give).
static void check_units_lane_release(CheckUnitsLane* lane)
{
    file_cache_release(&lane->cache);
    intern_release(&lane->identifiers);
    lane->ready = false;
    arena_release(&lane->output);
    arena_release(&lane->scratch);
    arena_release(&lane->arena);
    check_program_lanes_release(&lane->functions);
    check_units_memory_give(lane);
}

// A unit, on the lane that took it: compiled by check_driver_unit with the lane's intern table and file cache, its
// functions on the lane alone, its diagnostics into its own stream, and with a link to make, its link kept. The arenas
// it reserves start with BUSTER_CHECK_UNITS_SMALL_BYTES of base pages unless its source is large. The lane's three
// arenas, and its functions' lane 0's writer's columns (#442), are emptied after it, their pages kept for the next unit
// with the bytes the unit left there: the compile reads nothing in them it didn't write, which --poison-arenas checks
// (arena_poison: a pattern in place of those bytes; docs/translation-units.md, "Memory", has why and what reserving or
// clearing them again costs).
static void check_units_compile(const CheckUnitsRun* run, CheckUnitsLane* lane, CheckUnitsUnit* unit)
{
    const CheckDriverCommand* command = run->command;
    FILE* errors = open_memstream(&unit->diagnostics, &unit->diagnostic_bytes);
    unit->unbuffered = errors == NULL;
    unit->status = 1;
    unit->compiled = true;
    if (errors != NULL && unit->kind == CHECK_DRIVER_INPUT_LINK)
    {
        unit->status = 0;
    }
    else if (errors != NULL && !check_units_lane_ready(lane, command))
    {
        fprintf(errors, "fatal error: can't reserve the lane's memory\n");
        unit->fatal = true;
    }
    else if (errors != NULL)
    {
        PPOptions options = command->options;
        options.files = &lane->cache;
        options.diagnostics = errors;
        options.preprocessed = unit->preprocessed;
        unit->kept = (CheckUnitsKept){ .arena = &lane->kept, .bytes = &lane->kept_bytes };
        // On the board, the unit's functions as parts that the lanes without units may take (#777). Lane 0's writer's
        // columns stay the lane's from one unit to the next, as its three arenas do (check_program_lanes_settle, #442).
        const Arena columns = lane->functions.arenas[0].scratch;
        lane->functions = (CheckProgramLanes){
            .lanes = run->board != NULL ? run->board->lanes : 1,
            .board = run->board,
            .board_lane = (uint32_t)(lane - run->lanes),
            .board_pause_microseconds = command->shared_parts ? BUSTER_CHECK_UNITS_SHARED_PAUSE : 0,
            .tier1 = command->tier1,
            .tier1_strict = command->tier1_strict,
            .tier1_no_rematerialize = command->tier1_no_rematerialize,
            .tier1_gvn = command->tier1_gvn,
            .tier1_no_lifetimes = command->tier1_no_lifetimes,
            .tier1_no_inline = command->tier1_no_inline,
            .tier0_no_inline = command->tier0_no_inline,
            .tier1_schedule = command->tier1_schedule,
            .tier1_refusals = command->tier1_refusals != NULL ? errors : NULL,
            .helper_reporting = command->helper_report != NULL,
            .units_lane = lane,
            .helper_forms = command->helper_forms,
        };
        lane->functions.arenas[0].scratch = columns;
        lane->memory = run->memory;
        lane->run = run;
        uint64_t ticks[PP_PHASE_COUNT] = { 0 };
        arena_small_floor = unit->bytes < BUSTER_CHECK_UNITS_LARGE_SOURCE ? BUSTER_CHECK_UNITS_SMALL_BYTES : 0;
        if (unit->kind == CHECK_DRIVER_INPUT_ASSEMBLY || unit->kind == CHECK_DRIVER_INPUT_ASSEMBLY_CPP)
        {
            unit->status = check_driver_assembly(command, &options, unit, lane, errors);
        }
        else
        {
            unit->status = check_driver_unit(command, &options, unit->path, &lane->identifiers, &lane->pp, &lane->parser, &lane->arena, &lane->scratch, &lane->output,
                                             &lane->functions, NULL, ticks, errors, command->link ? &unit->kept : NULL, true, &unit->dependency, &lane->kept, NULL, &unit->fatal);
        }
        arena_small_floor = 0;
        // The unit's helper plan into the command's report (#1059), which the other lanes' units add to too.
        if (command->helper_report != NULL)
        {
            CheckHelperReport* report = command->helper_report;
            const CheckHelperReport* unit_report = &lane->functions.helper_report;
            __atomic_fetch_add(&report->units, unit_report->units, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->refused, unit_report->refused, __ATOMIC_RELAXED);
            for (uint32_t f = 0; f < CHECK_PROGRAM_HELPER_FAMILIES; f += 1)
            {
                __atomic_fetch_add(&report->direct[f], unit_report->direct[f], __ATOMIC_RELAXED);
                __atomic_fetch_add(&report->helper[f], unit_report->helper[f], __ATOMIC_RELAXED);
                __atomic_fetch_add(&report->planned[f], unit_report->planned[f], __ATOMIC_RELAXED);
            }
        }
        // The unit's --tier1 outcomes into the command's report, which the other lanes' units add to too.
        for (uint32_t k = 0; command->tier1 && command->tier1_report != NULL && k < CHECK_TIER1_STAGE_COUNT; k += 1)
        {
            __atomic_fetch_add(&command->tier1_report->stages[k], lane->functions.tier1_report.stages[k], __ATOMIC_RELAXED);
        }
        for (uint32_t k = 0; command->tier1 && command->tier1_report != NULL && k < BUSTER_CHECK_TIER1_OPERATIONS; k += 1)
        {
            __atomic_fetch_add(&command->tier1_report->operations[k], lane->functions.tier1_report.operations[k], __ATOMIC_RELAXED);
            __atomic_fetch_add(&command->tier1_report->emission[k], lane->functions.tier1_report.emission[k], __ATOMIC_RELAXED);
        }
        for (uint32_t k = 0; command->tier1 && command->tier1_report != NULL && k < CHECK_TIER1_CALL_KIND_COUNT; k += 1)
        {
            __atomic_fetch_add(&command->tier1_report->calls[k], lane->functions.tier1_report.calls[k], __ATOMIC_RELAXED);
        }
        if (command->tier1 && command->tier1_report != NULL)
        {
            CheckTier1Report* report = command->tier1_report;
            const CheckTier1Report* unit_report = &lane->functions.tier1_report;
            __atomic_fetch_add(&report->functions, unit_report->functions, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->requested, unit_report->requested, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->admitted, unit_report->admitted, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->fallbacks, unit_report->fallbacks, __ATOMIC_RELAXED);
            for (uint32_t k = 1; k < CHECK_TIER1_CATEGORY_COUNT; k += 1)
            {
                __atomic_fetch_add(&report->categories[k], unit_report->categories[k], __ATOMIC_RELAXED);
            }
            __atomic_fetch_add(&report->records, unit_report->records, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->spill_stores, unit_report->spill_stores, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->spill_loads, unit_report->spill_loads, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->call_stores, unit_report->call_stores, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->call_loads, unit_report->call_loads, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->rematerializations, unit_report->rematerializations, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->dropped_definitions, unit_report->dropped_definitions, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->edge_moves, unit_report->edge_moves, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->spill_slots, unit_report->spill_slots, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->frame_bytes, unit_report->frame_bytes, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->open_frames, unit_report->open_frames, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->return_tails, unit_report->return_tails, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->red_zones, unit_report->red_zones, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->code_certified, unit_report->code_certified, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->code_uncertified, unit_report->code_uncertified, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->machine_rows, unit_report->machine_rows, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->row_moves, unit_report->row_moves, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->inlined_calls, unit_report->inlined_calls, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->schedule_blocks, unit_report->schedule_blocks, __ATOMIC_RELAXED);
            __atomic_fetch_add(&report->schedule_moved, unit_report->schedule_moved, __ATOMIC_RELAXED);
        }
        check_program_lanes_settle(&lane->functions);
        Arena* arenas[] = { &lane->output, &lane->scratch, &lane->arena, &lane->functions.arenas[0].scratch };
        for (uint32_t a = 0; a < BUSTER_ARRAY_LENGTH(arenas); a += 1)
        {
            if (command->poison_arenas)
            {
                arena_poison(arenas[a], BUSTER_CHECK_UNITS_POISON);
            }
            else
            {
                arena_pop_to(arenas[a], 0);
            }
        }
        // A lane that waits for memory (#1109) takes what this one gave back, rather than wait for its later units too.
        if (lane->memory != NULL && __atomic_load_n(&lane->memory->waiting, __ATOMIC_ACQUIRE) != 0)
        {
            check_units_memory_give(lane);
        }
    }
    if (errors != NULL && fclose(errors) != 0)
    {
        unit->status = 1;
        unit->unbuffered = true;
    }
}

// A lane of the units' run: takes the units in turn, costliest first, and compiles each as a lane of its own
// (lane_alone), so that nothing a unit runs takes the group's other lanes for its own. The arenas its units release go
// to its pool for the next units' reservations. Once it has no unit left to take, it releases its own memory into the
// pool too (check_units_lane_release) and unmaps the pool's arenas, their pages freed, before it helps the others
// (#777): a lane that compiled a small unit beside a large one frees its memory while the large one compiles, rather
// than after it, when the command waits for every lane. The lane that stops taking units last has no other unit's work
// to overlap its release with: in a command without a link that doesn't release its memory (main's, whose process exits
// after the lanes: run_check_command_mode), the process's exit frees that lane's with the rest of the address space.
// Before a link it releases it, as every lane does, so that the link's peak stays the units'.
// A lane holds the units' memory (#1109) only while it takes units, and waits for nothing but its own units' work
// while it does, so that a unit waiting for memory always has a lane to give it back: the release gives it back
// before the help, and the last lane, which keeps its memory for the exit, returns its share alone.
static void check_units_lane(void* argument)
{
    CheckUnitsRun* run = argument;
    const uint32_t index = lane_index();
    CheckUnitsLane* lane = run->lanes + index;
    arena_pool = &lane->pool;
    for (uint64_t taken = lane_take(run->count); taken < run->count; taken = lane_take(run->count))
    {
        __atomic_add_fetch(&run->started, 1, __ATOMIC_RELAXED);
        const Lane group = lane_alone();
        check_units_compile(run, lane, run->units + run->order[taken]);
        lane_restore(group);
    }
    // The lanes that still take units after this one: the board's, which help the units' emissions (#777) from now on,
    // and the run's, which counts them without a board too. Every lane counts itself out of the board's first, so the
    // one that leaves the run's at 0 finds no unit's emission left to help, and no lane takes the units' memory after it.
    if (run->board != NULL)
    {
        __atomic_sub_fetch(&run->board->taking, 1, __ATOMIC_RELEASE);
    }
    const uint32_t taking = __atomic_sub_fetch(&run->taking, 1, __ATOMIC_RELEASE);
    if (run->release || taking != 0)
    {
        // Its release gives back its part of the units' memory (#1109), its pages freed, before it helps: the help
        // waits for the lanes still taking units, one of which may wait for that memory.
        check_units_lane_release(lane);
        arena_pool_release(&lane->pool);
    }
    else
    {
        // The lane that stops taking units last keeps its memory for the exit, and no lane takes the units' memory
        // after it: it returns its share alone, without freeing pages.
        check_units_memory_return(lane);
        __atomic_add_fetch(&run->kept, 1, __ATOMIC_RELAXED);
    }
    arena_pool = 0;
    // No unit left to take: the lane takes the parts of the units' emissions still running (#777), alone in its group as
    // a unit is, until no lane takes units. The parts it takes run on their sharing lane's arenas, not its own.
    if (run->board != NULL)
    {
        const Lane group = lane_alone();
        lane_advance(&run->board->generation);
        check_program_emit_help(run->board, index);
        lane_restore(group);
    }
}

// The units' objects' names, -c's (check_driver_object_name's), into `units`; false, after saying why, if two units'
// are the same, which would write one file twice, or without room for them.
static bool check_units_objects(CheckUnitsUnit* units, uint32_t count, Arena* arena)
{
    uint64_t slots = 16;
    while (slots < 2 * (uint64_t)count)
    {
        slots *= 2;
    }
    uint32_t* table = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slots);
    bool reserved = table != NULL;
    bool distinct = true;
    if (reserved)
    {
        memset(table, 0, slots * sizeof(*table));
    }
    for (uint32_t u = 0; reserved && distinct && u < count; u += 1)
    {
        if (units[u].kind == CHECK_DRIVER_INPUT_LINK)
        {
            continue;
        }
        units[u].object = check_driver_object_name(arena, units[u].path);
        reserved = units[u].object != NULL;
        const uint64_t length = reserved ? strlen(units[u].object) : 0;
        uint64_t slot = reserved ? link_hash((const uint8_t*)units[u].object, length) & (slots - 1) : 0;
        while (reserved && table[slot] != 0 && strcmp(units[table[slot] - 1].object, units[u].object) != 0)
        {
            slot = (slot + 1) & (slots - 1);
        }
        distinct = !reserved || table[slot] == 0;
        if (!distinct)
        {
            fprintf(stderr, "error: %s and %s would both write %s\n", units[table[slot] - 1].path, units[u].path, units[u].object);
        }
        else if (reserved)
        {
            table[slot] = u + 1;
        }
    }
    if (!reserved)
    {
        fprintf(stderr, "fatal error: can't reserve the driver's memory\n");
    }
    return reserved && distinct;
}

// ---------------------------------------------------------------------------------------------------------------------
// The units' flow (#905, see CheckUnitsFlow)

static void check_units_flow_begin(CheckUnitsFlow* flow)
{
    lane_flow_begin(&flow->rounds);
    flow->slots_lost = false;
}

static void check_units_flow_end(CheckUnitsFlow* flow)
{
    lane_flow_end(&flow->rounds);
}

// The flow's other lanes: each round's operation on its lanes, by its kind, until lane 0 ends the flow.
static void check_units_follow(CheckUnitsFlow* flow)
{
    void* argument = NULL;
    for (uint32_t round = lane_flow_next(&flow->rounds, &argument); round != CHECK_UNITS_ROUND_END; round = lane_flow_next(&flow->rounds, &argument))
    {
        if (argument != NULL)
        {
            switch ((CheckUnitsRound)round)
            {
                case CHECK_UNITS_ROUND_UNITS: check_units_lane(argument); break;
                case CHECK_UNITS_ROUND_MERGE: check_units_merge_lane(argument); break;
                case CHECK_UNITS_ROUND_CHECK: check_driver_check_main(argument); break;
                case CHECK_UNITS_ROUND_WRITE: check_driver_write_main(argument); break;
                case CHECK_UNITS_ROUND_FOREIGN: foreign_driver_flow(argument); break;
                case CHECK_UNITS_ROUND_END: break;
            }
        }
        lane_widen();
    }
}

static uint32_t check_units_slots(WorkerBudget* budget, uint32_t wanted, WorkerBudgetLease* lease)
{
    *lease = (WorkerBudgetLease){ .control = -1, .reply = -1, .child = -1 };
    return budget != NULL ? worker_budget_acquire(budget, wanted, lease) : wanted;
}

static void check_units_slots_back(CheckUnitsFlow* flow, WorkerBudgetLease* lease)
{
    const bool returned = worker_budget_release(lease);
    if (!returned && flow != NULL)
    {
        flow->slots_lost = true;
    }
}

// The command's translation units (`count` source files, `inputs`, two or more): the lanes compile them (at most
// `lanes->lanes` of them, a unit at a time each and past the units as many as their sources pay for, which a GNU make
// jobserver's budget may reduce; the lanes without a unit left take the units' functions' parts), the units' diagnostics are
// printed in the command line's order, and then with a link to make, the units' links are merged and linked
// (check_driver_executable), with -g their DWARF units, one each. -o with -c and a dump are refused: their
// outputs are one unit's.
// With `stage_clock`, the setup, the lanes' run, the link and the release are its stages. Without `release` (a command
// whose process exits after it) and without a link, the last lane to stop taking units leaves its memory to the exit.
// Returns the command's exit status: 0 when every unit compiled without errors and every output was written.
static int check_driver_units(const CheckDriverCommand* command, char** inputs, uint32_t count, CheckProgramLanes* lanes, CheckClock* stage_clock, bool release)
{
    int result = 1;
    Arena arena = arena_reserve(1ull << 36);
    CheckUnitsUnit* units = arena.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&arena, CheckUnitsUnit, count) : NULL;
    uint64_t* keys = units != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count) : NULL;
    uint64_t* sorting = keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count) : NULL;
    uint32_t* order = sorting != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count) : NULL;
    const uint32_t sources = command->inputs != NULL ? command->source_count : count;
    if (command->emit_object && command->output != NULL && sources > 1)
    {
        fprintf(stderr, "error: cannot specify -o when generating multiple output files\n");
    }
    else if (command->dump || command->dump_static)
    {
        fprintf(stderr, "error: --dump-typed and --dump-static take one source file\n");
    }
    else if (order == NULL)
    {
        fprintf(stderr, "fatal error: can't reserve the driver's memory\n");
    }
    else
    {
        // The units in the command line's order, and their costs, their sizes: each a key whose high half is its cost
        // inverted, so that the stable sort puts the costliest first, the command line's order among equals.
        memset(units, 0, (uint64_t)count * sizeof(*units));
        for (uint32_t u = 0; u < count; u += 1)
        {
            struct stat status;
            const uint64_t size = stat(inputs[u], &status) == 0 && status.st_size > 0 ? (uint64_t)status.st_size : 0;
            const uint32_t cost = size < UINT32_MAX ? (uint32_t)size : UINT32_MAX - 1;
            units[u].path = inputs[u];
            units[u].kind = command->inputs != NULL ? command->inputs[u].kind : CHECK_DRIVER_INPUT_C;
            units[u].preprocessed = command->inputs != NULL && command->inputs[u].preprocessed;
            units[u].bytes = units[u].kind != CHECK_DRIVER_INPUT_LINK ? size : 0;
            keys[u] = (uint64_t)(UINT32_MAX - cost) << 32 | u;
        }
        const uint64_t* sorted = c_bindings_sort(keys, sorting, count, !command->scalar, UINT32_MAX);
        for (uint32_t i = 0; i < count; i += 1)
        {
            order[i] = (uint32_t)sorted[i];
        }
        const bool named = !command->emit_object || command->output != NULL || check_units_objects(units, count, &arena);
        // The tables every unit's code reads, which the first to need them would build: built here, before the lanes,
        // which then only read them. The parser's are the front end's home's (main.c), which parse_initialize builds.
        // The parser starts zeroed: a refusal of the units' objects' names skips its initialization, and its release
        // then releases nothing.
        Parser tables_parser = { 0 };
        const ParseOptions tables_options = { .scalar = command->scalar, .tree_only = true };
        bool ready = named && parse_initialize(&tables_parser, &tables_options, command->options.language);
        parse_release(&tables_parser);
        const bool emitting = command->emit_object || command->link;
        if (ready && emitting && !tier0_initialized)
        {
            ready = x86_encoder_initialize() && x86_semantics_initialize() && tier0_initialize();
        }
        // And the lowering's runtime functions' names' lengths and hashes (#442), which every unit's lowering reads, before
        // the lanes start: a lowering that finds them unmade measures and hashes the names itself, as the emission's
        // backend setup leaves it to.
        if (ready && emitting && !check_lower_runtime_names_ready)
        {
            check_lower_runtime_names_initialize();
        }
        // And the asm matcher's index (#2532), which every unit's planning of its asm statements reads: baked, or made
        // here once rather than by each unit with asm statements.
        if (ready && emitting && !check_asm_index_ready)
        {
            ready = check_asm_index_initialize();
        }
        // A baked executable starts with the semantics tables built (baked.h, #590), and x86_semantics_initialize rebuilds
        // them unconditionally, writing the baked pages: only an executable that isn't baked runs it (#1571).
        if (ready && command->mixed_inputs)
        {
            ready = x86_encoder_initialize() && (x86_semantics_initialized || x86_semantics_initialize());
        }
        if (ready && emitting && command->unwind_tables && !unwind_ready)
        {
            ready = unwind_initialize();
        }
        // A lane per unit, and past the units, lanes whose only work is the units' functions' parts (#777): one per
        // BUSTER_CHECK_UNITS_LANE_BYTES of the units' sources, so that a few large units' functions get about the lanes
        // they would get alone; no more than `lanes->lanes`, and those past the calling thread's own as the budget gives
        // them (#673). The lease covers the units' run alone, and goes back after it (#905): the merge, the link's check
        // and the write take their own.
        uint64_t source_bytes = 0;
        for (uint32_t u = 0; u < count; u += 1)
        {
            source_bytes += units[u].bytes;
        }
        // The command's session's lanes (#905), which a lazy session's lane 1 started while the tables above were built:
        // lane 0 learns how they started, and the units' run, the merge, the link's check and the write are rounds of
        // them (CheckUnitsFlow).
        const uint32_t flow_lanes = lanes->units_flow != NULL ? lane_flow_lanes(&lanes->units_flow->rounds) : 1;
        lanes->lanes = lanes->lanes < flow_lanes ? lanes->lanes : flow_lanes;
        const uint32_t most = lanes->lanes != 0 ? lanes->lanes : 1;
        const uint64_t paid = (uint64_t)count + source_bytes / BUSTER_CHECK_UNITS_LANE_BYTES;
        const uint32_t wanted = most < paid ? most : (uint32_t)paid;
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t active = ready ? check_units_slots(lanes->budget, wanted, &lease) : wanted;
        CheckUnitsLane* states = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, CheckUnitsLane, active) : NULL;
        // check_units_objects says why it fails.
        if (named && states == NULL)
        {
            fprintf(stderr, "fatal error: can't reserve checker memory\n");
        }
        if (states != NULL)
        {
            memset(states, 0, (uint64_t)active * sizeof(*states));
            // The lanes' board (#777): every lane takes units at first. Without its memory, each unit's functions compile
            // on its lane alone; BUSTER_TEST_NO_UNITS_BOARD, a test hook (test-check-units), stands for its allocation
            // failing.
            const bool boarded = active > 1 && getenv("BUSTER_TEST_NO_UNITS_BOARD") == NULL;
            CheckEmitBoard* board = boarded ? BUSTER_ARENA_PUSH_ARRAY(&arena, CheckEmitBoard, 1) : NULL;
            if (board != NULL)
            {
                memset(board, 0, sizeof(*board));
                board->lanes = active;
                board->taking = active;
            }
            // The memory the units take turns with (#1109): part of what the process may take, or --units-memory's.
            const uint64_t available = command->units_memory != 0 ? 0 : check_units_memory_available();
            CheckUnitsMemory memory = {
                .limit = command->units_memory != 0 ? command->units_memory : available / 8 * BUSTER_CHECK_UNITS_MEMORY_EIGHTHS,
            };
            CheckUnitsRun run = {
                .command = command,
                .units = units,
                .count = count,
                .order = order,
                .lanes = states,
                .board = board,
                .memory = &memory,
                .release = release || command->link,
                .taking = active,
            };
            // The units' run is a round of the command's flow (#905), whose lanes keep their scratch arenas from one round
            // to the next (#778), on lane 0 alone in a flow of one; its slots go back after it.
            check_clock_mark(stage_clock, CHECK_STAGE_SETUP);
            lane_flow_round_begin(&lanes->units_flow->rounds, CHECK_UNITS_ROUND_UNITS, &run, active);
            check_units_lane(&run);
            lane_flow_round_end(&lanes->units_flow->rounds);
            check_units_slots_back(lanes->units_flow, &lease);
            // A unit no lane took compiles on lane 0 alone, whose memory goes as the last lane's does.
            const Lane caller = lane_alone();
            arena_pool = &states->pool;
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (!units[order[i]].compiled)
                {
                    __atomic_add_fetch(&run.started, 1, __ATOMIC_RELAXED);
                    check_units_compile(&run, states, units + order[i]);
                }
            }
            if (run.release)
            {
                check_units_lane_release(states);
                arena_pool_release(&states->pool);
            }
            arena_pool = 0;
            lane_restore(caller);
            lanes->active = active;
            check_clock_mark(stage_clock, CHECK_STAGE_LANES);
            // All jobs have finished: publish input order's prefix through its first fatal, including that unit's notes
            // and summary. A later job's fatal cannot cancel an earlier job. Release every buffer even past the cutoff.
            result = 0;
            bool stopped = false;
            for (uint32_t u = 0; u < count; u += 1)
            {
                if (!stopped && units[u].diagnostic_bytes != 0)
                {
                    result = fwrite(units[u].diagnostics, 1, units[u].diagnostic_bytes, stderr) == units[u].diagnostic_bytes ? result : 1;
                }
                if (!stopped && units[u].unbuffered)
                {
                    fprintf(stderr, "fatal error: %s: can't buffer its diagnostics\n", units[u].path);
                }
                stopped |= units[u].fatal || units[u].unbuffered;
                free(units[u].diagnostics);
                units[u].diagnostics = NULL;
                result = units[u].status != 0 ? 1 : result;
            }
            if (command->shared_parts)
            {
                uint32_t discarded_huge = 0;
                for (uint32_t l = 0; l < active; l += 1)
                {
                    discarded_huge += states[l].discarded_huge;
                }
                fprintf(stderr, "shared parts: %llu taken by lanes without units\n", (unsigned long long)(board != NULL ? board->others : 0));
                fprintf(stderr, "units' lanes: %u left %s memory to the exit\n", run.kept, run.kept != 1 ? "their" : "its");
                fprintf(stderr, "units' scratch: %u gave %s pages back in whole huge pages, for a unit left to take\n", discarded_huge,
                        discarded_huge != 1 ? "their" : "its");
            }
            if (command->units_memory != 0)
            {
                fprintf(stderr, "units memory: %u units waited, at most %u lane%s held memory at once, %u lane%s kept pages for the exit\n", memory.waits,
                        memory.most, memory.most != 1 ? "s" : "", run.kept, run.kept != 1 ? "s" : "");
            }
            // The lanes released their caches and arenas as they ended (check_units_lane_release), but the last one's in a
            // command without a link that doesn't release its memory: the kept links are all the link reads.
            for (uint32_t l = 0; run.release && l < active; l += 1)
            {
                check_units_lane_release(states + l);
            }
            check_clock_mark(stage_clock, CHECK_STAGE_RELEASE);
            if (result == 0 && command->link)
            {
                // The executable is written on as many lanes as the units' run took, each of its rounds (the merge, the
                // link's check and the write) on those the worker budget gives of them.
                CheckProgramLanes writers = { .lanes = active, .budget = lanes->budget, .units_flow = lanes->units_flow };
                const bool linked = command->mixed_inputs ? check_driver_mixed_link(command, units, count, lanes) :
                    check_driver_executable(NULL, NULL, NULL, NULL, NULL, command->output != NULL ? command->output : "a.out", command->scalar,
                                            command->verify, command->source_rows, command->unwind_tables, command->libraries, &writers, &arena,
                                            stage_clock, units, count, command->runtime_kind);
                result = linked ? 0 : 1;
            }
            for (uint32_t u = 0; result == 0 && u < count; u += 1)
            {
                result = dependency_publish(&units[u].dependency, stderr) ? 0 : 1;
            }
            for (uint32_t l = 0; l < active; l += 1)
            {
                arena_release(&states[l].kept_bytes);
                arena_release(&states[l].kept);
            }
        }
        // The units' run's slots go back whatever happened since they were taken, and the command fails if any round's
        // didn't.
        check_units_slots_back(lanes->units_flow, &lease);
        if (lanes->units_flow != NULL && lanes->units_flow->slots_lost)
        {
            fprintf(stderr, "error: could not return GNU make worker slots\n");
            result = 1;
        }
    }
    arena_release(&arena);
    return result;
}
