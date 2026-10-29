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

// A unit's link kept for the units' link (check_units_keep): a copy of its records, with room for no more, and which of
// its undefined global symbols a system header declared, for the link's error about them (check_driver_executable's,
// as check_driver_system_declared tells it for one unit).
struct CheckUnitsKept
{
    Link link;
    uint8_t* system;
    // Where the copy goes: its tables and names, and its atoms' bytes (its lane's arenas, which keep every unit's that
    // the lane compiled until the link).
    Arena* arena;
    Arena* bytes;
};

// A unit of the command: its source file, and its size, and what compiling it gave: its exit status, its diagnostics'
// text in the order it wrote them (from open_memstream, or null), whether it was compiled, and its kept link. `object`
// is the object -c writes.
struct CheckUnitsUnit
{
    const char* path;
    uint64_t bytes;
    const char* object;
    int status;
    char* diagnostics;
    size_t diagnostic_bytes;
    bool unbuffered;
    bool compiled;
    CheckUnitsKept kept;
};

// A lane's state, which its units take turns with: the intern table and the file cache they preprocess with, a unit's
// preprocessor, parser, memory and function lanes, the memory of the links it keeps, and the pool of the arenas its
// units release (arena_pool).
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
};

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
};

// Copies `link`, a unit's whole link (check_driver_object's, its code and data published and link_check passed), into
// `kept`: its tables, names and atoms' bytes into its arenas, with each record's id as it was; and marks its undefined
// global symbols that `program`'s declarations in a system header gave (check_driver_system_declared's rule). False
// without room, with the arenas as they were.
static bool check_units_keep(CheckUnitsKept* kept, const Link* link, const CheckProgram* program, const Parser* parser, Preprocessor* pp)
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
    if (!result)
    {
        arena_pop_to(kept->arena, mark);
        arena_pop_to(kept->bytes, byte_mark);
        kept->link = (Link){ 0 };
        kept->system = NULL;
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

// The units' kept links joined into `link`, in the units' order, as check_program_emit builds one unit's (#152): every
// symbol first, a global name's once and each unit's locals its own, the units' definitions marked as the program's;
// then the C runtime, which binding (`binding`, bind_begin) joins between the program's symbols and its atoms, its units
// numbered after the units'; then each unit's atoms in its order, each with its fixups, and its priorities and the
// definitions it makes. A name takes the most constraining visibility any unit gives it, and a reference only takes
// the binding of a weak one when no unit refers to the name otherwise, as a linker resolves the objects' symbols. Its
// tables go in `output`, the runtime's atoms' bytes in `bytes`; the units' atoms' bytes stay in their kept links, where
// the executable's write patches them. A name that two units define (strongly: a weak definition yields to another) is
// an error, which this prints, as it prints a failure of memory; bind_begin's failure is binding's (bind->error, with
// binding->failed_name), which its caller reports as it reports one unit's.
static bool check_units_merge(Link* link, const CheckUnitsUnit* units, uint32_t count, CheckProgramBinding* binding, Arena* output, Arena* bytes, Arena* scratch)
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
    // The global names' table, open addressing on their hashes: twice the slots of the symbols, at least, each the link's
    // symbol of its name or 0.
    uint64_t slots = 16;
    while (slots < 2 * symbols)
    {
        slots *= 2;
    }
    SymbolId* table = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, slots) : NULL;
    valid = valid && maps != NULL && first_unit != NULL && first_symbol != NULL && defining_unit != NULL && defining_symbol != NULL && visibility != NULL &&
            strong != NULL && table != NULL;
    for (uint32_t u = 0; valid && u < count; u += 1)
    {
        maps[u] = BUSTER_ARENA_PUSH_ARRAY(scratch, SymbolId, units[u].kept.link.symbols.count);
        valid = maps[u] != NULL;
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
            const uint8_t flags = unit->symbols.flags[s];
            const uint8_t binding_flags = flags & BUSTER_LINK_SYMBOL_BINDING;
            const NameId name = unit->symbols.name[s];
            const bool global = binding_flags != BUSTER_LINK_SYMBOL_LOCAL && name != 0;
            const bool defined = unit->symbols.atom[s] != 0 || unit->symbols.source[s] == BUSTER_LINK_SOURCE_ABSOLUTE;
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
            if (defined)
            {
                const bool weak = binding_flags == BUSTER_LINK_SYMBOL_WEAK;
                const bool earlier = defining_unit[merged] != count;
                const bool earlier_weak = earlier && (units[defining_unit[merged]].kept.link.symbols.flags[defining_symbol[merged]] & BUSTER_LINK_SYMBOL_BINDING) ==
                                                         BUSTER_LINK_SYMBOL_WEAK;
                if (!earlier || (earlier_weak && !weak))
                {
                    defining_unit[merged] = u;
                    defining_symbol[merged] = s;
                }
                else if (!earlier_weak && !weak)
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
    const uint64_t total_names = (uint64_t)next - 1 + runtime.names;
    const uint64_t total_atoms = atoms + runtime.atoms;
    const uint64_t total_symbols = (uint64_t)next - 1 + runtime.symbols;
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
            placed[a] = link_add_atom(link, unit->atoms.class[a], unit->atoms.alignment_log2[a], unit->atoms.size[a], unit->atoms.bytes[a], u);
            valid = placed[a] != 0;
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
            valid = link_add_priority(link, placed[unit->priorities.atom[p]], unit->priorities.priority[p]);
        }
        for (SymbolId s = 1; valid && s < unit->symbols.count; s += 1)
        {
            const SymbolId merged = maps[u][s];
            if (unit->symbols.atom[s] != 0 && defining_unit[merged] == u && defining_symbol[merged] == s)
            {
                valid = link_define_symbol(link, merged, placed[unit->symbols.atom[s]], unit->symbols.value[s], unit->symbols.size[s]);
            }
        }
        arena_pop_to(scratch, unit_mark);
    }
    if (!valid && duplicate == 0 && (!symbols_added || begun))
    {
        fprintf(stderr, "fatal error: can't link the units: %s (link error %u)\n", sized ? "their records don't fit the link" : "no memory", (uint32_t)link->error);
    }
    arena_pop_to(scratch, mark);
    return valid;
}

// The lanes' setup: the intern table and the file cache a lane's units share, the arenas they take turns with, and the
// memory of the links it keeps, the first time the lane takes a unit. False, with the lane failed, if it can't be
// reserved: what was stays the lane's, for its release.
static bool check_units_lane_ready(CheckUnitsLane* lane, const CheckDriverCommand* command)
{
    if (!lane->ready && !lane->failed)
    {
        lane->arena = arena_reserve(1ull << 36);
        lane->scratch = arena_reserve(1ull << 36);
        lane->output = arena_reserve(1ull << 36);
        lane->kept = arena_reserve(1ull << 36);
        lane->kept_bytes = arena_reserve(1ull << 36);
        lane->ready = lane->arena.base != 0 && lane->scratch.base != 0 && lane->output.base != 0 && lane->kept.base != 0 && lane->kept_bytes.base != 0 &&
                      intern_initialize(&lane->identifiers) && file_cache_initialize(&lane->cache, &lane->identifiers, pp_prepare_options(&command->options));
        lane->failed = !lane->ready;
    }
    return lane->ready;
}

// A unit, on the lane that took it: compiled by check_driver_unit with the lane's intern table and file cache, its
// functions on the lane alone, its diagnostics into its own stream, and with a link to make, its link kept. The arenas
// it reserves start with BUSTER_CHECK_UNITS_SMALL_BYTES of base pages unless its source is large. The lane's three
// arenas are emptied after it, their pages kept for the next unit with the bytes the unit left there: the compile
// reads nothing in them it didn't write, which --poison-arenas checks (arena_poison: a pattern in place of those bytes;
// docs/translation-units.md, "Memory", has why and what reserving or clearing them again costs).
static void check_units_compile(const CheckUnitsRun* run, CheckUnitsLane* lane, CheckUnitsUnit* unit)
{
    const CheckDriverCommand* command = run->command;
    FILE* errors = open_memstream(&unit->diagnostics, &unit->diagnostic_bytes);
    unit->unbuffered = errors == NULL;
    unit->status = 1;
    unit->compiled = true;
    if (errors != NULL && !check_units_lane_ready(lane, command))
    {
        fprintf(errors, "fatal error: can't reserve the lane's memory\n");
    }
    else if (errors != NULL)
    {
        PPOptions options = command->options;
        options.files = &lane->cache;
        options.diagnostics = errors;
        unit->kept = (CheckUnitsKept){ .arena = &lane->kept, .bytes = &lane->kept_bytes };
        lane->functions = (CheckProgramLanes){ .lanes = 1 };
        uint64_t ticks[PP_PHASE_COUNT] = { 0 };
        arena_small_floor = unit->bytes < BUSTER_CHECK_UNITS_LARGE_SOURCE ? BUSTER_CHECK_UNITS_SMALL_BYTES : 0;
        unit->status = check_driver_unit(command, &options, unit->path, &lane->identifiers, &lane->pp, &lane->parser, &lane->arena, &lane->scratch, &lane->output,
                                         &lane->functions, NULL, ticks, errors, command->link ? &unit->kept : NULL, true);
        arena_small_floor = 0;
        check_program_lanes_release(&lane->functions);
        Arena* arenas[] = { &lane->output, &lane->scratch, &lane->arena };
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
    }
    if (errors != NULL && fclose(errors) != 0)
    {
        unit->status = 1;
        unit->unbuffered = true;
    }
}

// A lane of the units' run: takes the units in turn, costliest first, and compiles each as a lane of its own
// (lane_alone), so that nothing a unit runs takes the group's other lanes for its own. The arenas its units release go
// to its pool for the next units' reservations, and are unmapped at its end.
static void check_units_lane(void* argument)
{
    const CheckUnitsRun* run = argument;
    CheckUnitsLane* lane = run->lanes + lane_index();
    arena_pool = &lane->pool;
    for (uint64_t taken = lane_take(run->count); taken < run->count; taken = lane_take(run->count))
    {
        const Lane group = lane_alone();
        check_units_compile(run, lane, run->units + run->order[taken]);
        lane_restore(group);
    }
    arena_pool = 0;
    arena_pool_release(&lane->pool);
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

// The command's translation units (`count` source files, `inputs`, two or more): the lanes compile them (at most
// `lanes->lanes` of them, one per unit, which a GNU make jobserver's budget may reduce), the units' diagnostics are
// printed in the command line's order, and then with a link to make, the units' links are merged and linked
// (check_driver_executable). -o with -c, a dump and -g's DWARF for a link are refused: their outputs are one unit's.
// With `stage_clock`, the setup, the lanes' run, the link and the release are its stages. Returns the command's exit
// status: 0 when every unit compiled without errors and every output was written.
static int check_driver_units(const CheckDriverCommand* command, char** inputs, uint32_t count, CheckProgramLanes* lanes, CheckClock* stage_clock)
{
    int result = 1;
    Arena arena = arena_reserve(1ull << 36);
    CheckUnitsUnit* units = arena.base != 0 ? BUSTER_ARENA_PUSH_ARRAY(&arena, CheckUnitsUnit, count) : NULL;
    uint64_t* keys = units != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count) : NULL;
    uint64_t* sorting = keys != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint64_t, count) : NULL;
    uint32_t* order = sorting != NULL ? BUSTER_ARENA_PUSH_ARRAY(&arena, uint32_t, count) : NULL;
    if (command->emit_object && command->output != NULL)
    {
        fprintf(stderr, "error: cannot specify -o when generating multiple output files\n");
    }
    else if (command->dump || command->dump_static || command->id_ordered)
    {
        fprintf(stderr, "error: --dump-typed, --dump-static and --id-order take one source file\n");
    }
    else if (command->link && command->source_rows)
    {
        fprintf(stderr, "error: unsupported: -g for an executable of several translation units (-c -g writes each one's object with it)\n");
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
            units[u].bytes = size;
            keys[u] = (uint64_t)(UINT32_MAX - cost) << 32 | u;
        }
        const uint64_t* sorted = c_bindings_sort(keys, sorting, count, !command->scalar, UINT32_MAX);
        for (uint32_t i = 0; i < count; i += 1)
        {
            order[i] = (uint32_t)sorted[i];
        }
        const bool named = !command->emit_object || check_units_objects(units, count, &arena);
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
        if (ready && emitting && command->unwind_tables && !unwind_ready)
        {
            ready = unwind_initialize();
        }
        // A lane per unit, at most, and the lanes past the calling thread's own as the budget gives them (#673). The
        // lease covers the units' lanes and the executable's writers, as many, and goes back once both are done.
        const uint32_t most = lanes->lanes != 0 ? lanes->lanes : 1;
        const uint32_t wanted = most < count ? most : count;
        WorkerBudgetLease lease = { .control = -1, .reply = -1, .child = -1 };
        const uint32_t active = ready && lanes->budget != NULL ? worker_budget_acquire(lanes->budget, wanted, &lease) : wanted;
        CheckUnitsLane* states = ready ? BUSTER_ARENA_PUSH_ARRAY(&arena, CheckUnitsLane, active) : NULL;
        // check_units_objects says why it fails.
        if (named && states == NULL)
        {
            fprintf(stderr, "fatal error: can't reserve checker memory\n");
        }
        if (states != NULL)
        {
            memset(states, 0, (uint64_t)active * sizeof(*states));
            CheckUnitsRun run = {
                .command = command,
                .units = units,
                .count = count,
                .order = order,
                .lanes = states,
            };
            check_clock_mark(stage_clock, CHECK_STAGE_SETUP);
            // Lanes that couldn't start (lane_run runs none of them then) leave their units untaken: the calling thread
            // compiles those alone, as its own lane 0.
            const bool ran = lane_run(active, check_units_lane, &run, NULL);
            const Lane caller = lane_alone();
            arena_pool = &states->pool;
            for (uint32_t i = 0; i < count; i += 1)
            {
                if (!units[order[i]].compiled)
                {
                    check_units_compile(&run, states, units + order[i]);
                }
            }
            arena_pool = 0;
            arena_pool_release(&states->pool);
            lane_restore(caller);
            lanes->active = ran ? active : 1;
            check_clock_mark(stage_clock, CHECK_STAGE_LANES);
            // The units' diagnostics, in the command line's order.
            result = 0;
            for (uint32_t u = 0; u < count; u += 1)
            {
                if (units[u].diagnostic_bytes != 0)
                {
                    fwrite(units[u].diagnostics, 1, units[u].diagnostic_bytes, stderr);
                }
                if (units[u].unbuffered)
                {
                    fprintf(stderr, "fatal error: %s: can't buffer its diagnostics\n", units[u].path);
                }
                free(units[u].diagnostics);
                units[u].diagnostics = NULL;
                result = units[u].status != 0 ? 1 : result;
            }
            // The lanes' caches and arenas: the kept links are all the link reads. A lane whose setup failed releases
            // what it reserved before the failure, which its table and cache keep as their initializers left them (as a
            // lane that took no unit has them zero, which their releases take too).
            for (uint32_t l = 0; l < active; l += 1)
            {
                CheckUnitsLane* lane = states + l;
                file_cache_release(&lane->cache);
                intern_release(&lane->identifiers);
                arena_release(&lane->output);
                arena_release(&lane->scratch);
                arena_release(&lane->arena);
            }
            if (result == 0 && command->link)
            {
                // The executable is written on the units' lanes.
                CheckProgramLanes writers = { .lanes = ran ? active : 1 };
                const bool linked = check_driver_executable(NULL, NULL, NULL, NULL, NULL, command->output != NULL ? command->output : "a.out", command->scalar,
                                                            command->verify, false, command->unwind_tables, command->libraries, &writers, &arena, stage_clock, units,
                                                            count);
                result = linked ? 0 : 1;
                check_clock_mark(stage_clock, CHECK_STAGE_EXECUTABLE);
            }
            for (uint32_t l = 0; l < active; l += 1)
            {
                arena_release(&states[l].kept_bytes);
                arena_release(&states[l].kept);
            }
        }
        // The lease's slots go back whatever happened since it was taken.
        if (!worker_budget_release(&lease))
        {
            fprintf(stderr, "error: could not return GNU make worker slots\n");
            result = 1;
        }
    }
    arena_release(&arena);
    return result;
}
