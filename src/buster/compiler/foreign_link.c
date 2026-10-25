// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The handoff from foreign_run's resolved inputs to executable_write: selected sections become atoms, each global
// name gets one canonical symbol, and every input symbol index maps to that symbol or to its own local. Relocations
// use import.c's vector decoder and exact fallback. No names are resolved a second time, and no unselected archive
// member is imported. Sections follow input position (not the round which loaded an archive member), preserving the
// order of .init/.fini fragments and equally prioritized arrays. COMDAT sections are removed before symbols/fixups.
//
// foreign_link_build borrows contents from Foreign's private mappings. ForeignArgument.bytes supplied by a caller
// must likewise be writable while executable_write patches them. Keep Foreign and both output arenas alive until
// the executable has been written; rebuild Foreign before writing a second variant. The caller owns the arenas,
// including partial output on failure. foreign_link_build_kind takes the output's kind (RuntimeKind): a static
// executable (foreign_link_build) and a static PIE get the static runtime's ExecutableRuntime (the start file's ABI note
// and the link's __tls_get_addr, which the writer's TLS relaxation consumes), and a static PIE the ExecutableDynamic
// without libraries that dynamic_static_pie_inputs gives the direct path's; a dynamic PIE (foreign_link_build_dynamic)
// and a shared library also get the resolver's selected shared libraries and globally numbered versions, a dynamic
// PIE the definitions its libraries need exported and a shared library every visible definition.
//
// Layout: API; checked capacities/order; section and local-symbol import; canonical globals; relocation handoff;
// foreign_link_build. main.c includes this after foreign.c and executable.c.

typedef enum ForeignLinkError
{
    FOREIGN_LINK_ERROR_NONE,
    FOREIGN_LINK_ERROR_INPUT,
    FOREIGN_LINK_ERROR_MEMORY,
    FOREIGN_LINK_ERROR_CAPACITY,
    FOREIGN_LINK_ERROR_DUPLICATE,
    FOREIGN_LINK_ERROR_UNDEFINED,
    FOREIGN_LINK_ERROR_UNSUPPORTED,
    FOREIGN_LINK_ERROR_IMPORT,
    FOREIGN_LINK_ERROR_LINK,
    FOREIGN_LINK_ERROR_IFUNC,
    // A compressed debug section that doesn't decompress: a compression other than zlib, or a malformed stream.
    FOREIGN_LINK_ERROR_COMPRESSION,
} ForeignLinkError;

typedef struct ForeignLinkResult ForeignLinkResult;
struct ForeignLinkResult
{
    ForeignLinkError error;
    uint32_t object;
    // ForeignNames index plus one, or zero when the failure is not about a global name.
    uint32_t name;
    ImportResult import;
};

// The conversion's phases, which link-foreign's --time reports, each from the end of the one before to its own on the
// thread that takes the steps (lane 0 on lanes, whose syncs end the passes after every lane's part): the libraries, the
// order and every object's sections and locals; the global names' symbols; the objects' maps of them; the COMDAT unwind
// plans' symbols and the __start_ and __stop_ sections; the relocations; the bounds' relocations, the copies, _DYNAMIC
// and the IFUNCs' refusal; link_check; and a PIE's exports.
typedef enum ForeignLinkPhase
{
    FOREIGN_LINK_PHASE_SECTIONS,
    FOREIGN_LINK_PHASE_NAMES,
    FOREIGN_LINK_PHASE_MAPS,
    FOREIGN_LINK_PHASE_UNWIND,
    FOREIGN_LINK_PHASE_RELOCATIONS,
    FOREIGN_LINK_PHASE_COPIES,
    FOREIGN_LINK_PHASE_CHECK,
    FOREIGN_LINK_PHASE_EXPORTS,
    FOREIGN_LINK_PHASE_DEBUG,
    FOREIGN_LINK_PHASES,
} ForeignLinkPhase;

typedef struct ForeignLink ForeignLink;
typedef struct ForeignLinkUnwind ForeignLinkUnwind;
struct ForeignLink
{
    Link link;
    // Canonical symbol by ForeignNames index, and input-index maps by ForeignObject index.
    SymbolId* names;
    ImportObject* objects;
    ExecutableDynamic dynamic;
    // A static executable's and a static PIE's (foreign_link_static_finish).
    ExecutableRuntime runtime;
    // The selected objects' debug sections, built whole, and their references to the link's symbols (foreign_debug.c).
    ExecutableDebugSections debug;
    // Original library version index -> output version index, one table per ForeignLibrary; null for dropped DSOs.
    uint16_t** library_versions;
    ForeignLinkUnwind** unwind;
    // The copy relocations' definitions (foreign_link_copies): every symbol defined at a copy, its aliases among them,
    // each exported at its version; and per copy the one symbol its R_X86_64_COPY names.
    SymbolId* copy_exports;
    uint32_t copy_export_count;
    SymbolId* copies;
    uint32_t copy_count;
    // Each phase's time, in nanoseconds, from foreign_link_build_mode.
    uint64_t phase_nanoseconds[FOREIGN_LINK_PHASES];
};

// The most aliases (a library's other names at a copy's address that the link has no symbol of) a copy may add: glibc's
// have at most three names (environ, _environ and __environ).
#define BUSTER_FOREIGN_LINK_COPY_ALIASES 4

// The __start_ and __stop_ names a link defines, and their sections (foreign_link_bounds_plan).
typedef struct ForeignLinkBounds ForeignLinkBounds;
static bool foreign_link_bounded(const ForeignLinkBounds* plan, uint32_t name);

static __attribute__((unused)) ForeignLinkResult foreign_link_build(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena);
static __attribute__((unused)) ForeignLinkResult foreign_link_build_dynamic(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, String8 interpreter);
// What the caller asks of a conversion: for a static link, the entry's name and the caller's word that the output's own
// startup applies its IRELATIVE relocations (link-foreign's --startup-applies-irelative), which the conversion asks
// only when glibc's startup doesn't (foreign_link_startup_applies); and whether the output leaves the inputs' DWARF out
// (link-foreign's -S and -s), which then isn't built (foreign_debug_build).
typedef struct ForeignLinkOptions ForeignLinkOptions;
struct ForeignLinkOptions
{
    String8 entry;
    bool applies_irelative;
    bool strip_debug;
};

// `interpreter` is a dynamic PIE's alone. A shared library's soname is its caller's to set in the result's
// ExecutableDynamic (empty for none).
static __attribute__((unused)) ForeignLinkResult foreign_link_build_kind(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind,
                                                                         String8 interpreter, ForeignLinkOptions options);
static __attribute__((unused)) ForeignLinkResult foreign_link_build_for_gc(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter,
                                                                           ForeignLinkOptions options);

static bool foreign_link_add_capacity(LinkCapacity* total, LinkCapacity added)
{
    const bool fits = added.names < UINT32_MAX - total->names && added.atoms < UINT32_MAX - total->atoms &&
                      added.symbols < UINT32_MAX - total->symbols && added.fixups < UINT32_MAX - total->fixups &&
                      added.wide_addends < UINT32_MAX - total->wide_addends && added.priorities < UINT32_MAX - total->priorities;
    if (fits)
    {
        total->names += added.names;
        total->atoms += added.atoms;
        total->symbols += added.symbols;
        total->fixups += added.fixups;
        total->wide_addends += added.wide_addends;
        total->priorities += added.priorities;
    }
    return fits;
}

// The resolver has already validated these tables. Count only local symbol records here: globals are represented
// once per resolved name, so sizing by every input's full symbol table would reserve that handoff a second time.
static bool foreign_link_capacity(const Foreign* foreign, uint64_t bound_groups, uint64_t bound_relocations, LinkCapacity* capacity)
{
    *capacity = (LinkCapacity){ 0 };
    capacity->names = foreign->names.count;
    capacity->symbols = foreign->names.count;
    // A copy relocation (foreign_link_copies) takes an atom of a library's name, and may define up to
    // BUSTER_FOREIGN_LINK_COPY_ALIASES of that library's names the link has no symbol of.
    uint64_t shared = 0;
    for (uint32_t first = 0; first < foreign->names.count; first += 64)
    {
        const uint32_t count = foreign->names.count - first < 64 ? foreign->names.count - first : 64;
        if (foreign->path == FOREIGN_PATH_VECTOR)
        {
            const __mmask64 mask = _bzhi_u64(~0ull, count);
            const __m512i resolutions = _mm512_maskz_loadu_epi8(mask, foreign->names.resolution + first);
            capacity->atoms += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(mask, resolutions, _mm512_set1_epi8(FOREIGN_RESOLUTION_COMMON)));
            shared += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(mask, resolutions, _mm512_set1_epi8(FOREIGN_RESOLUTION_SHARED)));
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                capacity->atoms += foreign->names.resolution[first + i] == FOREIGN_RESOLUTION_COMMON ? 1 : 0;
                shared += foreign->names.resolution[first + i] == FOREIGN_RESOLUTION_SHARED ? 1 : 0;
            }
        }
    }
    // And _DYNAMIC (foreign_link_dynamic_symbol).
    const uint64_t aliases = shared * BUSTER_FOREIGN_LINK_COPY_ALIASES + 1;
    // And a merged atom for each __start_ and __stop_ group, with its sections' relocations again (foreign_link_bounds_merge).
    capacity->atoms = capacity->atoms + shared + bound_groups < UINT32_MAX ? capacity->atoms + (uint32_t)(shared + bound_groups) : UINT32_MAX;
    capacity->fixups = capacity->fixups + bound_relocations < UINT32_MAX ? capacity->fixups + (uint32_t)bound_relocations : UINT32_MAX;
    capacity->wide_addends = capacity->wide_addends + bound_relocations < UINT32_MAX ? capacity->wide_addends + (uint32_t)bound_relocations : UINT32_MAX;
    capacity->names = capacity->names + aliases < UINT32_MAX ? capacity->names + (uint32_t)aliases : UINT32_MAX;
    capacity->symbols = capacity->symbols + aliases < UINT32_MAX ? capacity->symbols + (uint32_t)aliases : UINT32_MAX;
    bool result = capacity->names < UINT32_MAX && capacity->symbols < UINT32_MAX && capacity->atoms < UINT32_MAX;
    for (uint32_t id = 1; result && id < foreign->object_count; id += 1)
    {
        const ForeignObject* input = &foreign->objects[id];
        if (input->library == 0)
        {
            LinkCapacity part = {
                .names = input->first_global,
                .atoms = input->elf.section_count != 0 ? input->elf.section_count - 1 : 0,
                .symbols = input->first_global,
                .priorities = input->elf.section_count,
            };
            result = foreign_link_add_capacity(capacity, part);
            for (uint32_t r = 0; result && r < input->relocation_count; r += 1)
            {
                const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
                const uint64_t count = header.sh_size / sizeof(Elf64_Rela);
                result = count < UINT32_MAX;
                if (result)
                {
                    const LinkCapacity relocations = {
                        .fixups = (uint32_t)count,
                        .wide_addends = (uint32_t)count,
                    };
                    result = foreign_link_add_capacity(capacity, relocations);
                }
            }
        }
    }
    return result;
}

// Stable bottom-up merge of input ids by the positions the resolver already chose. IDs include the command pseudo
// object and library references; those carry no sections and are skipped by the producer passes.
static uint32_t* foreign_link_order(const Foreign* foreign, Arena* arena)
{
    const uint32_t count = foreign->object_count;
    uint32_t* order = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)count * 2);
    uint32_t* from = order;
    uint32_t* to = order != 0 ? order + count : 0;
    for (uint32_t i = 0; order != 0 && i < count; i += 1)
    {
        order[i] = i;
    }
    for (uint64_t width = 1; order != 0 && width < count; width *= 2)
    {
        for (uint64_t begin = 0; begin < count; begin += width * 2)
        {
            const uint64_t middle = begin + width < count ? begin + width : count;
            const uint64_t end = middle + width < count ? middle + width : count;
            uint64_t left = begin;
            uint64_t right = middle;
            for (uint64_t at = begin; at < end; at += 1)
            {
                const bool take_left = left < middle && (right == end || foreign->objects[from[left]].position <= foreign->objects[from[right]].position);
                to[at] = take_left ? from[left] : from[right];
                left += take_left ? 1 : 0;
                right += take_left ? 0 : 1;
            }
        }
        uint32_t* swap = from;
        from = to;
        to = swap;
    }
    return from;
}

// Compact this object's atoms only, before anything refers to them. The section map supplies the kept old-to-new
// ids; priorities are rewritten while their section is visited. Objects without discarded COMDAT sections avoid it.
static void foreign_link_keep_sections(Link* link, const ForeignObject* input, ImportObject* output, uint32_t first_atom, uint32_t first_priority)
{
    if (input->discarded != 0)
    {
        LinkAtoms* atoms = &link->atoms;
        uint32_t next = first_atom;
        uint32_t priority = first_priority;
        uint32_t next_priority = first_priority;
        for (uint32_t section = 1; section < output->section_count; section += 1)
        {
            const AtomId old = output->section_atoms[section];
            const bool prioritized = priority < link->priorities.count && link->priorities.atom[priority] == old;
            if (old != 0 && input->discarded[section] == 0)
            {
                atoms->class[next] = atoms->class[old];
                atoms->alignment_log2[next] = atoms->alignment_log2[old];
                atoms->unit[next] = atoms->unit[old];
                atoms->size[next] = atoms->size[old];
                atoms->bytes[next] = atoms->bytes[old];
                atoms->fixup_begin[next] = 0;
                atoms->fixup_count[next] = 0;
                atoms->address[next] = 0;
                output->section_atoms[section] = next;
                if (prioritized)
                {
                    link->priorities.atom[next_priority] = next;
                    link->priorities.priority[next_priority] = link->priorities.priority[priority];
                    next_priority += 1;
                }
                next += 1;
            }
            else
            {
                output->section_atoms[section] = 0;
            }
            priority += prioritized ? 1 : 0;
        }
        atoms->count = next;
        link->priorities.count = next_priority;
    }
}

static ImportResult foreign_link_sections(ForeignLink* output, const ForeignObject* input, uint32_t id, ForeignPath path, Arena* arena)
{
    ImportObject* object = &output->objects[id];
    Link* link = &output->link;
    ImportResult result = { 0 };
    object->section_count = input->elf.section_count;
    object->symbol_count = input->symbols.count;
    object->executable_stack_section = 0;
    object->section_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, object->section_count);
    object->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, object->symbol_count);
    if (object->section_atoms == 0 || object->symbols == 0)
    {
        result.error = IMPORT_ERROR_MEMORY;
    }
    else
    {
        memset(object->section_atoms, 0, (uint64_t)object->section_count * sizeof(AtomId));
        memset(object->symbols, 0, (uint64_t)object->symbol_count * sizeof(SymbolId));
        const uint32_t first_atom = link->atoms.count;
        const uint32_t first_priority = link->priorities.count;
        bool vector = false;
        if (path == FOREIGN_PATH_VECTOR)
        {
            Scratch scratch = scratch_begin(arena);
            ImportVectorSections found = { 0 };
            found.symbol_tables = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, object->section_count);
            found.extended = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, object->section_count);
            found.relocations = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, object->section_count);
            vector = found.symbol_tables != 0 && found.extended != 0 && found.relocations != 0 && import_vector_sections(link, &input->elf, id, object, &found);
            scratch_end(scratch);
            if (!vector)
            {
                link->atoms.count = first_atom;
                link->priorities.count = first_priority;
                object->executable_stack_section = 0;
                memset(object->section_atoms, 0, (uint64_t)object->section_count * sizeof(AtomId));
            }
        }
        if (!vector)
        {
            result = import_sections(link, &input->elf, id, object);
        }
        if (result.error == IMPORT_ERROR_NONE)
        {
            foreign_link_keep_sections(link, input, object, first_atom, first_priority);
        }
    }
    return result;
}

static ImportResult foreign_link_locals(ForeignLink* output, const ForeignObject* input, uint32_t id, ForeignPath path, Arena* arena)
{
    ImportSymbols symbols = input->symbols;
    symbols.count = input->first_global;
    Link* link = &output->link;
    ImportObject* object = &output->objects[id];
    ImportResult result = { 0 };
    bool vector = false;
    if (path == FOREIGN_PATH_VECTOR)
    {
        const ImportMark mark = import_mark(link, arena);
        Scratch scratch = scratch_begin(arena);
        vector = import_vector_symbols(link, &input->elf, &symbols, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, object, scratch.arena);
        scratch_end(scratch);
        if (!vector)
        {
            import_restore(link, arena, mark);
            memset(object->symbols, 0, (uint64_t)symbols.count * sizeof(SymbolId));
        }
    }
    if (!vector)
    {
        result = import_object_symbols(link, &input->elf, &symbols, BUSTER_LINK_SOURCE_FOREIGN_OBJECT, object);
    }
    return result;
}

#include "buster/compiler/foreign_unwind_link.c"

// The names the linker defines in a foreign output of `kind`: lld's for any output, which in a dynamic PIE and a shared
// library are the static PIE runtime's set (the image's and the sections' bounds, _DYNAMIC) with the dynamic runtime's.
static bool foreign_link_linker_name(RuntimeKind kind, const uint8_t* bytes, uint32_t length)
{
    const bool dynamic = kind == RUNTIME_KIND_DYNAMIC || kind == RUNTIME_KIND_SHARED;
    return runtime_linker_name(kind, bytes, length) || (dynamic && runtime_linker_name(RUNTIME_KIND_STATIC_PIE, bytes, length));
}

// The name the static kinds leave undefined for the writer: __tls_get_addr, which libc.a doesn't define, and whose
// general- and local-dynamic accesses' calls the writer's TLS relaxation consumes (ExecutableRuntime.tls_get_addr), as
// GNU ld and lld relax them in a static link.
static bool foreign_link_relaxed_name(RuntimeKind kind, const uint8_t* bytes, uint32_t length)
{
    return runtime_kind_is_static(kind) && length == 14 && memcmp(bytes, "__tls_get_addr", 14) == 0;
}

// A linker-defined name's visibility, as lld defines it: the end, text and data bounds of the traditional names
// (end, _end, etext, _etext, edata, _edata and __bss_start) with default visibility, and the others (the ELF header's,
// the arrays' bounds, _GLOBAL_OFFSET_TABLE_, _DYNAMIC) hidden, which a regular symbol table then has local.
static uint8_t foreign_link_linker_visibility(const uint8_t* bytes, uint32_t length)
{
    static const char* const visible[] = { "end", "_end", "etext", "_etext", "__etext", "edata", "_edata", "__bss_start" };
    uint8_t result = STV_HIDDEN;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(visible); i += 1)
    {
        result = strlen(visible[i]) == length && memcmp(visible[i], bytes, length) == 0 ? STV_DEFAULT : result;
    }
    return result;
}

// A symbol table alone cannot tell whether an undefined reference survived COMDAT selection. Only the surviving
// sections' relocations require a definition, together with explicit -u roots. The extra scan is needed only when
// resolution left an ordinary strong name undefined or assigned it to an unneeded DSO; ordinary resolved links
// avoid it entirely. A shared library's own references require nothing of the output: the loader binds them among the
// libraries, and a name only they refer to gets no symbol (lld's --allow-shlib-undefined, which is its default for an
// executable whose libraries' dependencies it doesn't see).
static uint8_t* foreign_link_undefined(const ForeignLink* output, const Foreign* foreign, RuntimeKind kind, Arena* arena)
{
    uint8_t* required = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, foreign->names.count);
    bool unresolved = false;
    for (uint32_t name = 0; required != 0 && name < foreign->names.count; name += 1)
    {
        required[name] = 0;
        const bool regular = (foreign->names.flags[name] & BUSTER_FOREIGN_NAME_REGULAR_REFERENCED) != 0;
        const bool unneeded_shared = foreign->names.resolution[name] == FOREIGN_RESOLUTION_SHARED &&
                                     !foreign->libraries[foreign->names.library[name]].needed && regular;
        unresolved = unresolved || (foreign->names.resolution[name] == FOREIGN_RESOLUTION_UNDEFINED && regular &&
                                    !foreign_link_linker_name(kind, foreign->names.bytes[name], foreign->names.length[name]) &&
                                    !foreign_link_relaxed_name(kind, foreign->names.bytes[name], foreign->names.length[name])) ||
                     unneeded_shared;
    }
    for (uint32_t id = 0; required != 0 && unresolved && id < foreign->object_count; id += 1)
    {
        const ForeignObject* input = &foreign->objects[id];
        const ForeignRound* round = input->global_count != 0 ? foreign_symbol_round(foreign, input->global_begin) : 0;
        if (id == 0)
        {
            for (uint32_t i = 0; i < input->global_count; i += 1)
            {
                const uint32_t at = input->global_begin - round->first_symbol + i;
                required[round->symbols.name_id[at]] = 1;
            }
        }
        else if (input->library == 0)
        {
            for (uint32_t r = 0; r < input->relocation_count; r += 1)
            {
                const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
                if (output->objects[id].section_atoms[header.sh_info] != 0)
                {
                    const String8 bytes = foreign_link_relocation_bytes(output, input, id, &header);
                    const uint64_t count = bytes.length / sizeof(Elf64_Rela);
                    for (uint64_t first = 0; first < count; first += BUSTER_IMPORT_BATCH)
                    {
                        const uint32_t n = (uint32_t)(count - first < BUSTER_IMPORT_BATCH ? count - first : BUSTER_IMPORT_BATCH);
                        uint64_t infos[BUSTER_IMPORT_BATCH];
                        if (foreign->path == FOREIGN_PATH_VECTOR)
                        {
                            __m512i words[3];
                            import_load_records3(bytes.pointer + first * sizeof(Elf64_Rela), n, words);
                            import_store64(infos, words[1], n);
                        }
                        else
                        {
                            for (uint32_t lane = 0; lane < n; lane += 1)
                            {
                                Elf64_Rela relocation;
                                memcpy(&relocation, bytes.pointer + (first + lane) * sizeof(Elf64_Rela), sizeof(relocation));
                                infos[lane] = relocation.r_info;
                            }
                        }
                        for (uint32_t lane = 0; lane < n; lane += 1)
                        {
                            const uint64_t symbol = ELF64_R_SYM(infos[lane]);
                            if (ELF64_R_TYPE(infos[lane]) != R_X86_64_NONE && symbol >= input->first_global && symbol < input->symbols.count)
                            {
                                const uint32_t at = input->global_begin - round->first_symbol + (uint32_t)symbol - input->first_global;
                                required[round->symbols.name_id[at]] = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    return required;
}

// What foreign_link_names does with a name: whether it gets a symbol (`kept`), and the symbol's atom, value, size,
// flags, source, version and object; for a common symbol, its new atom's class, alignment and size instead of an atom.
// Or the error that stops the conversion there.
typedef struct ForeignLinkName ForeignLinkName;
struct ForeignLinkName
{
    uint64_t value;
    uint64_t size;
    AtomId atom;
    uint32_t object;
    uint16_t version;
    uint8_t flags;
    uint8_t source;
    uint8_t common_class;
    uint8_t common_alignment;
    bool kept;
    ForeignLinkError error;
};

// The decision for name `name`, which reads the conversion so far and changes nothing (foreign_link_add_name acts on
// it): the same on any lane.
static ForeignLinkName foreign_link_decide_name(const ForeignLink* output, const Foreign* foreign, const uint8_t* required, const ForeignLinkBounds* bounds,
                                                RuntimeKind kind, bool defer_undefined, uint32_t name)
{
    ForeignLinkName result = { 0 };
    const ForeignNames* names = &foreign->names;
    const uint8_t resolution = names->resolution[name];
    const bool defined = resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_WEAK || resolution == FOREIGN_RESOLUTION_COMMON;
    uint32_t object_id = 0;
    uint8_t flags = BUSTER_LINK_SYMBOL_GLOBAL;
    uint8_t source = BUSTER_LINK_SOURCE_NONE;
    AtomId atom = 0;
    uint64_t value = 0;
    uint64_t size = 0;
    uint16_t version = 0;
    bool discarded_shared = false;
    bool linker = false;
    bool bounded = false;
    bool relaxed = false;
    if (names->definitions[name] > 1)
    {
        result.error = FOREIGN_LINK_ERROR_DUPLICATE;
    }
    else if (defined)
    {
        const uint32_t winner = names->symbol[name];
        object_id = foreign_symbol_object(foreign, winner);
        const ForeignObject* object = &foreign->objects[object_id];
        Elf64_Sym symbol = import_symbol(&object->symbols, object->first_global + (winner - object->global_begin));
        // The resolver ranks GNU_UNIQUE with weak definitions; the executable keeps the winner's binding, as GNU ld,
        // lld and mold do.
        const bool unique = ELF64_ST_BIND(symbol.st_info) == STB_GNU_UNIQUE;
        if (unique)
        {
            symbol.st_info = ELF64_ST_INFO(STB_GLOBAL, ELF64_ST_TYPE(symbol.st_info));
        }
        if (ELF64_ST_TYPE(symbol.st_info) == STT_COMMON)
        {
            symbol.st_info = ELF64_ST_INFO(ELF64_ST_BIND(symbol.st_info), STT_OBJECT);
        }
        const ForeignRound* round = foreign_symbol_round(foreign, winner);
        const uint32_t section = round->symbols.section[winner - round->first_symbol];
        value = symbol.st_value;
        size = symbol.st_size;
        source = BUSTER_LINK_SOURCE_FOREIGN_OBJECT;
        const bool known = import_symbol_flags(&symbol, &flags);
        flags = unique ? (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_BINDING) | BUSTER_LINK_SYMBOL_UNIQUE) : flags;
        if (!known)
        {
            result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
        }
        else if (resolution == FOREIGN_RESOLUTION_COMMON)
        {
            size = names->common_size[name];
            value = 0;
            result.common_class = ELF64_ST_TYPE(symbol.st_info) == STT_TLS ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_BSS;
            result.common_alignment = names->common_alignment[name];
        }
        else if (section == BUSTER_FOREIGN_SECTION_ABS)
        {
            source = BUSTER_LINK_SOURCE_ABSOLUTE;
        }
        else if (section < output->objects[object_id].section_count)
        {
            atom = output->objects[object_id].section_atoms[section];
        }
        if (result.error == FOREIGN_LINK_ERROR_NONE && source == BUSTER_LINK_SOURCE_FOREIGN_OBJECT && atom == 0 && result.common_class == 0)
        {
            result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
        }
    }
    else if (resolution == FOREIGN_RESOLUTION_SHARED)
    {
        const ForeignLibrary* library = &foreign->libraries[names->library[name]];
        const ImportExports* exports = &library->library.exports;
        const uint32_t index = names->symbol[name];
        // Only a regular object's (or the command line's) reference imports the name: one that only libraries
        // refer to is the loader's to bind among them.
        const bool regular = (names->flags[name] & (BUSTER_FOREIGN_NAME_REGULAR_REFERENCED | BUSTER_FOREIGN_NAME_REGULAR_WEAK_REFERENCED)) != 0;
        const bool strong = (names->flags[name] & BUSTER_FOREIGN_NAME_REGULAR_REFERENCED) != 0;
        discarded_shared = !regular || (strong && !library->needed && required[name] == 0);
        flags = (uint8_t)((strong ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_WEAK) | (exports->flags[index] & BUSTER_LINK_SYMBOL_TYPE));
        size = exports->size[index];
        if (discarded_shared)
        {
            // The resolver saw the reference before COMDAT selection; no retained relocation imports it.
        }
        else if (runtime_kind_is_static(kind))
        {
            result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
        }
        else if (library->needed)
        {
            source = BUSTER_LINK_SOURCE_FOREIGN_SHARED;
            const uint16_t original = exports->version[index];
            version = original > VER_NDX_GLOBAL ? output->library_versions[names->library[name]][original] : 0;
            result.error = original > VER_NDX_GLOBAL && version == 0 ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_NONE;
        }
        else if (strong)
        {
            result.error = defer_undefined ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_UNDEFINED;
        }
        // A weak reference to an unneeded --as-needed DSO remains unversioned and undefined, retaining its type.
    }
    else if (foreign_link_linker_name(kind, names->bytes[name], names->length[name]))
    {
        // With lld's visibility (foreign_link_linker_visibility).
        source = BUSTER_LINK_SOURCE_LINKER;
        linker = true;
    }
    else if (foreign_link_bounded(bounds, name))
    {
        // Defined by foreign_link_bounds_merge, global and protected as GNU ld and lld define it.
        bounded = true;
    }
    else if (resolution == FOREIGN_RESOLUTION_UNDEFINED && foreign_link_relaxed_name(kind, names->bytes[name], names->length[name]))
    {
        // Undefined, for the writer's relaxation (ExecutableRuntime.tls_get_addr): kept whether or not the scan of the
        // retained relocations ran, since only its own calls refer to it.
        relaxed = true;
    }
    else if (resolution == FOREIGN_RESOLUTION_UNDEFINED && required[name] != 0 && (names->flags[name] & BUSTER_FOREIGN_NAME_REGULAR_REFERENCED) != 0)
    {
        // A shared library leaves a strong reference of default visibility to nothing for the loader to bind, as lld's
        // -shared does (its -z undefs); one of another visibility can't bind outside the library, and lld refuses it.
        const bool loader = kind == RUNTIME_KIND_SHARED && names->visibility[name] == STV_DEFAULT;
        result.error = defer_undefined || loader ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_UNDEFINED;
    }
    else
    {
        flags = BUSTER_LINK_SYMBOL_WEAK;
    }
    const uint8_t visibility = linker                                         ? foreign_link_linker_visibility(names->bytes[name], names->length[name]) :
                               bounded && names->visibility[name] == STV_DEFAULT ? STV_PROTECTED :
                                                                                names->visibility[name];
    flags = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT);
    // Nor does a name nothing defines that only libraries refer to (libstdc++.so.6's weak _ITM_ references).
    const bool regular = (names->flags[name] & (BUSTER_FOREIGN_NAME_REGULAR_REFERENCED | BUSTER_FOREIGN_NAME_REGULAR_WEAK_REFERENCED)) != 0;
    const bool unreferenced = !defined && source == BUSTER_LINK_SOURCE_NONE && !regular;
    const bool discarded_only = !bounded && !relaxed &&
                                (discarded_shared || unreferenced || (resolution == FOREIGN_RESOLUTION_UNDEFINED && source == BUSTER_LINK_SOURCE_NONE && required[name] == 0));
    result.value = value;
    result.size = size;
    result.atom = atom;
    result.object = object_id;
    result.version = version;
    result.flags = flags;
    result.source = source;
    result.kept = result.error == FOREIGN_LINK_ERROR_NONE && !discarded_only;
    return result;
}

// Name `name`'s symbol, as foreign_link_decide_name decided it: a common symbol's atom first, then its name and its
// symbol, into output->names. Returns FOREIGN_LINK_ERROR_LINK if the link refuses one.
static ForeignLinkError foreign_link_add_name(Link* link, ForeignLink* output, const Foreign* foreign, uint32_t name, const ForeignLinkName* decision)
{
    const ForeignNames* names = &foreign->names;
    const AtomId atom = decision->common_class != 0 ? link_add_atom(link, decision->common_class, decision->common_alignment, decision->size, 0, decision->object) :
                                                     decision->atom;
    const NameId named = decision->common_class == 0 || atom != 0 ? link_add_name_hashed(link, names->bytes[name], names->length[name], names->hash[name]) : 0;
    output->names[name] = named != 0 ? link_add_symbol(link, named, atom, decision->value, decision->size, decision->flags, decision->source) : 0;
    if (output->names[name] != 0)
    {
        link->symbols.version[output->names[name]] = decision->version;
    }
    return output->names[name] == 0 ? FOREIGN_LINK_ERROR_LINK : FOREIGN_LINK_ERROR_NONE;
}

static ForeignLinkResult foreign_link_names(ForeignLink* output, const Foreign* foreign, const uint8_t* required, const ForeignLinkBounds* bounds, RuntimeKind kind,
                                            bool defer_undefined)
{
    ForeignLinkResult result = { 0 };
    const ForeignNames* names = &foreign->names;
    for (uint32_t name = 0; result.error == FOREIGN_LINK_ERROR_NONE && name < names->count; name += 1)
    {
        const ForeignLinkName decision = foreign_link_decide_name(output, foreign, required, bounds, kind, defer_undefined, name);
        result.error = decision.error;
        if (result.error == FOREIGN_LINK_ERROR_NONE && decision.kept)
        {
            result.error = foreign_link_add_name(&output->link, output, foreign, name, &decision);
        }
        if (result.error != FOREIGN_LINK_ERROR_NONE)
        {
            result.name = name + 1;
            result.object = decision.object;
        }
    }
    return result;
}

static bool foreign_link_bound_section(const ForeignLinkBounds* plan, uint32_t object, uint32_t section);

static ImportResult foreign_link_relocations(ForeignLink* output, const Foreign* foreign, const ForeignLinkBounds* bounds, uint32_t id, ForeignPath path, Arena* arena)
{
    const ForeignObject* input = &foreign->objects[id];
    Link* link = &output->link;
    ImportObject* object = &output->objects[id];
    ImportResult result = { 0 };
    const __m512i kinds = import_kind_table();
    for (uint32_t i = 0; result.error == IMPORT_ERROR_NONE && i < input->relocation_count; i += 1)
    {
        const uint32_t section = input->relocations[i];
        const Elf64_Shdr header = import_section_header(&input->elf, section);
        // A bound section's go into its merged atom, after the others (foreign_link_bounds_relocations).
        const bool bound = foreign_link_bound_section(bounds, id, header.sh_info);
        const AtomId atom = bound ? 0 : object->section_atoms[header.sh_info];
        const String8 entries = foreign_link_relocation_bytes(output, input, id, &header);
        result = atom != 0 ? foreign_link_unwind_references(output, foreign, id, section, entries) : (ImportResult){ 0 };
        Scratch scratch = scratch_begin(arena);
        if (bound)
        {
            // Imported later, into the merged atom.
        }
        else if (result.error == IMPORT_ERROR_NONE && foreign_link_unwind(output, id, header.sh_info) != 0)
        {
            if (path == FOREIGN_PATH_EXACT || !import_vector_relocation_entries(link, &input->symbols, object, atom, entries, kinds, scratch.arena))
            {
                result = import_relocation_entries(link, &input->symbols, object, atom, entries, 0, scratch.arena);
                result.section = result.error != IMPORT_ERROR_NONE ? section : 0;
            }
        }
        else if (result.error == IMPORT_ERROR_NONE && (path == FOREIGN_PATH_EXACT || !import_vector_relocations(link, &input->elf, &input->symbols, section, object, kinds, scratch.arena)))
        {
            result = import_relocations(link, &input->elf, &input->symbols, section, object, scratch.arena);
        }
        scratch_end(scratch);
    }
    return result;
}

// Preserve the resolver's needed-library choices, including an unversioned as-needed DSO. Original ELF version
// indices are local to each library; the writer needs one global index space and version rows grouped by library.
// A needed library's DT_NEEDED name: its soname, or its path without one.
static String8 foreign_link_library_name(const Foreign* foreign, const ForeignLibrary* library)
{
    return library->library.soname.length != 0 ? library->library.soname : foreign->inputs[library->input].path;
}

// The earlier needed library of the same DT_NEEDED name as needed library `l` (given twice, as g++'s driver gives -lgcc_s),
// plus 1, or 0: GNU ld, lld and mold take a library once, by its soname, and so does the output.
static uint32_t foreign_link_duplicate(const Foreign* foreign, uint32_t l)
{
    const String8 name = foreign_link_library_name(foreign, &foreign->libraries[l]);
    uint32_t result = 0;
    for (uint32_t e = 0; result == 0 && e < l; e += 1)
    {
        const String8 earlier = foreign->libraries[e].needed ? foreign_link_library_name(foreign, &foreign->libraries[e]) : (String8){ 0 };
        result = earlier.length == name.length && memcmp(earlier.pointer, name.pointer, name.length) == 0 ? e + 1 : 0;
    }
    return result;
}

static ForeignLinkError foreign_link_libraries(ForeignLink* output, const Foreign* foreign, Arena* arena, String8 interpreter)
{
    uint64_t version_count = 0;
    uint32_t library_count = 0;
    for (uint32_t l = 0; l < foreign->library_count; l += 1)
    {
        const ForeignLibrary* library = &foreign->libraries[l];
        const bool listed = library->needed && foreign_link_duplicate(foreign, l) == 0;
        library_count += listed ? 1 : 0;
        for (uint32_t v = 0; listed && v < library->library.version_count; v += 1)
        {
            version_count += !library->library.versions[v].base && library->library.versions[v].index > VER_NDX_GLOBAL ? 1 : 0;
        }
    }
    ForeignLinkError result = version_count > 0x7ffe ? FOREIGN_LINK_ERROR_CAPACITY : FOREIGN_LINK_ERROR_NONE;
    ExecutableLibrary* libraries = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableLibrary, library_count);
    ExecutableVersion* versions = BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableVersion, version_count);
    output->library_versions = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t*, foreign->library_count);
    if (result == FOREIGN_LINK_ERROR_NONE && (libraries == 0 || versions == 0 || output->library_versions == 0))
    {
        result = FOREIGN_LINK_ERROR_MEMORY;
    }
    uint32_t next_library = 0;
    uint32_t next_version = 0;
    for (uint32_t l = 0; result == FOREIGN_LINK_ERROR_NONE && l < foreign->library_count; l += 1)
    {
        const ForeignLibrary* library = &foreign->libraries[l];
        const uint32_t duplicate = library->needed ? foreign_link_duplicate(foreign, l) : 0;
        // A library given again binds at the first one's versions.
        output->library_versions[l] = duplicate != 0 ? output->library_versions[duplicate - 1] : 0;
        if (library->needed && duplicate == 0)
        {
            const String8 name = foreign_link_library_name(foreign, library);
            libraries[next_library] = (ExecutableLibrary){
                .name = name,
                .required = true,
            };
            uint32_t limit = VER_NDX_GLOBAL + 1;
            for (uint32_t v = 0; v < library->library.version_count; v += 1)
            {
                const uint32_t index = library->library.versions[v].index;
                limit = index + 1 > limit ? index + 1 : limit;
            }
            uint16_t* map = BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, limit);
            output->library_versions[l] = map;
            result = map == 0 ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_NONE;
            if (map != 0)
            {
                memset(map, 0, (uint64_t)limit * sizeof(uint16_t));
                for (uint32_t v = 0; v < library->library.version_count; v += 1)
                {
                    const ImportVersion* version = &library->library.versions[v];
                    if (!version->base && version->index > VER_NDX_GLOBAL)
                    {
                        const uint16_t index = (uint16_t)(next_version + 2);
                        versions[next_version] = (ExecutableVersion){
                            .name = version->name,
                            .hash = version->hash,
                            .index = index,
                            .library = next_library,
                        };
                        map[version->index] = index;
                        next_version += 1;
                    }
                }
            }
            next_library += 1;
        }
    }
    if (result == FOREIGN_LINK_ERROR_NONE)
    {
        output->dynamic = (ExecutableDynamic){
            .interpreter = interpreter,
            .libraries = libraries,
            .library_count = library_count,
            .versions = versions,
            .version_count = next_version,
        };
    }
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Copy relocations
//
// As GNU ld and lld make them in an executable: a library's data that a retained fixup reaches other than through the
// GOT or by an address the loader relocates (GCC's -fPIE code reads stdout and environ by PC32, as if the executable
// defined them) gets a copy of its own in .bss, where the loader copies the library's initial bytes (R_X86_64_COPY),
// and the executable exports it, so that the library's own references bind to the copy. The library's other names at
// the same address (environ's _environ and __environ) are defined at the copy too and exported, as lld defines them
// (its getSymbolsAt), so that a reference to any of them finds the one object. The copy is aligned as lld aligns it, by
// the largest power of two that divides the definition's address, at most its section's alignment, and it is as large
// as the largest of its names. A function reached that way would need a canonical PLT entry, and a definition of no
// size can't be copied: both are refused, naming the symbol.
//
// The imports are a bitset, 64 symbols' sources at a time; the fixups' kinds and targets are tested 16 at a time against
// it (a gather of its words) on the vector path, one at a time on the exact path. Only the hits, a few in a program,
// and the copies' aliases (the library's export addresses compared eight at a time) are handled one at a time.

#define BUSTER_FOREIGN_LINK_DIRECT_KINDS                                                                                                                \
    ((1u << BUSTER_LINK_FIXUP_PC32) | (1u << BUSTER_LINK_FIXUP_PC64) | (1u << BUSTER_LINK_FIXUP_ABS32) | (1u << BUSTER_LINK_FIXUP_ABS32S) |         \
     (1u << BUSTER_LINK_FIXUP_CALL))

static bool foreign_link_bit32(const uint32_t* bits, uint32_t index)
{
    return (bits[index / 32] >> (index % 32) & 1) != 0;
}

// Whether a fixup of `kind` (without the wide flag) to an import needs a copy of it: a kind that reaches a library's
// symbol neither through the GOT nor by an ABS64 nor, for a function, by a call (link_check_target's rule).
static bool foreign_link_needs_copy(const Link* link, uint8_t kind, SymbolId target)
{
    const bool function = (link->symbols.flags[target] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_FUNCTION;
    return (BUSTER_FOREIGN_LINK_DIRECT_KINDS >> kind & 1) != 0 && !(kind == BUSTER_LINK_FIXUP_CALL && function);
}

// The link's imports as a bitset of 32-bit words, 64 symbols' sources at a time; their count.
static uint64_t foreign_link_imports(const Link* link, uint32_t* imports, ForeignPath path)
{
    const LinkSymbols* symbols = &link->symbols;
    uint64_t total = 0;
    for (uint32_t first = 0; first < symbols->count; first += 64)
    {
        const uint32_t n = symbols->count - first < 64 ? symbols->count - first : 64;
        uint64_t mask = 0;
        if (path == FOREIGN_PATH_VECTOR)
        {
            const __mmask64 lanes = _bzhi_u64(~0ull, n);
            mask = _mm512_mask_cmpeq_epi8_mask(lanes, _mm512_maskz_loadu_epi8(lanes, symbols->source + first), _mm512_set1_epi8(BUSTER_LINK_SOURCE_FOREIGN_SHARED));
        }
        else
        {
            for (uint32_t i = 0; i < n; i += 1)
            {
                mask |= (uint64_t)(symbols->source[first + i] == BUSTER_LINK_SOURCE_FOREIGN_SHARED) << i;
            }
        }
        imports[first / 32] = (uint32_t)mask;
        imports[first / 32 + 1] = (uint32_t)(mask >> 32);
        total += (uint64_t)__builtin_popcountll(mask);
    }
    return total;
}

// The imports that some fixup needs a copy of, into the bitset `copied`.
static void foreign_link_mark_copies(const Link* link, const uint32_t* imports, uint32_t* copied, ForeignPath path)
{
    const LinkFixups* fixups = &link->fixups;
    FixupId first = 1;
    if (path == FOREIGN_PATH_VECTOR)
    {
        const __m512i direct = _mm512_set1_epi32((int)BUSTER_FOREIGN_LINK_DIRECT_KINDS);
        for (; first + 16 <= fixups->count; first += 16)
        {
            const __m512i kinds = _mm512_and_si512(_mm512_cvtepu8_epi32(_mm_loadu_si128((const __m128i*)(fixups->kind + first))), _mm512_set1_epi32(BUSTER_LINK_FIXUP_KIND));
            const __mmask16 candidates = _mm512_test_epi32_mask(_mm512_srlv_epi32(direct, kinds), _mm512_set1_epi32(1));
            if (candidates != 0)
            {
                const __m512i targets = _mm512_loadu_si512(fixups->target + first);
                const __m512i words = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), candidates, _mm512_srli_epi32(targets, 5), imports, 4);
                const __m512i bits = _mm512_srlv_epi32(words, _mm512_and_si512(targets, _mm512_set1_epi32(31)));
                for (uint32_t hits = _mm512_mask_test_epi32_mask(candidates, bits, _mm512_set1_epi32(1)); hits != 0; hits &= hits - 1)
                {
                    const FixupId fixup = first + (FixupId)__builtin_ctz(hits);
                    const SymbolId target = fixups->target[fixup];
                    copied[target / 32] |= foreign_link_needs_copy(link, fixups->kind[fixup] & BUSTER_LINK_FIXUP_KIND, target) ? 1u << (target % 32) : 0;
                }
            }
        }
    }
    for (; first < fixups->count; first += 1)
    {
        const SymbolId target = fixups->target[first];
        const bool copy = foreign_link_bit32(imports, target) && foreign_link_needs_copy(link, fixups->kind[first] & BUSTER_LINK_FIXUP_KIND, target);
        copied[target / 32] |= copy ? 1u << (target % 32) : 0;
    }
}

// A copy's alignment (log2), as lld takes it: the largest power of two that divides the definition's address, at most
// its section's sh_addralign when the library's section table has that section.
static uint8_t foreign_link_copy_alignment(const Foreign* foreign, const ForeignLibrary* library, uint64_t value, uint16_t section)
{
    const String8 bytes = foreign->inputs[library->input].bytes;
    uint8_t result = value != 0 ? (uint8_t)__builtin_ctzll(value) : BUSTER_LINK_MAX_ALIGNMENT_LOG2;
    Elf64_Ehdr header = { 0 };
    if (bytes.length >= sizeof(header))
    {
        memcpy(&header, bytes.pointer, sizeof(header));
    }
    const uint64_t at = header.e_shoff + (uint64_t)section * sizeof(Elf64_Shdr);
    if (section != SHN_UNDEF && section < SHN_LORESERVE && section < header.e_shnum && header.e_shentsize == sizeof(Elf64_Shdr) &&
        import_within(bytes.length, at, sizeof(Elf64_Shdr)))
    {
        Elf64_Shdr found;
        memcpy(&found, bytes.pointer + at, sizeof(found));
        const uint8_t log2 = found.sh_addralign > 1 ? (uint8_t)(63 - __builtin_clzll(found.sh_addralign)) : 0;
        result = log2 < result ? log2 : result;
    }
    return result < BUSTER_LINK_MAX_ALIGNMENT_LOG2 ? result : BUSTER_LINK_MAX_ALIGNMENT_LOG2;
}

// A copy: the library and the address its names share, the first symbol a fixup copies it for, and its atom.
typedef struct ForeignLinkCopy ForeignLinkCopy;
struct ForeignLinkCopy
{
    uint32_t library;
    uint64_t value;
    uint16_t section;
    SymbolId symbol;
    AtomId atom;
};

// Whether export `e` of `exports` is one of a copy's names: defined in a section of the library (not absolute) at
// `value`, not thread-local.
static bool foreign_link_alias(const ImportExports* exports, uint32_t e, uint64_t value)
{
    const uint16_t section = exports->section[e];
    return exports->value[e] == value && section != SHN_UNDEF && section != SHN_ABS && (exports->flags[e] & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_TLS;
}

// The exports at `value`, eight addresses at a time: the first `capacity` of their places into `found`; how many there
// are, all of them, so that a caller counts with no room and then gathers into room of the count.
static uint32_t foreign_link_aliases(const ImportExports* exports, uint64_t value, uint32_t* found, uint32_t capacity)
{
    uint32_t count = 0;
    for (uint32_t first = 0; first < exports->count; first += 8)
    {
        const __mmask8 lanes = (__mmask8)_bzhi_u32(0xff, exports->count - first < 8 ? exports->count - first : 8);
        for (uint32_t hits = _mm512_mask_cmpeq_epi64_mask(lanes, _mm512_maskz_loadu_epi64(lanes, exports->value + first), _mm512_set1_epi64((long long)value)); hits != 0;
             hits &= hits - 1)
        {
            const uint32_t e = first + (uint32_t)__builtin_ctz(hits);
            const bool alias = foreign_link_alias(exports, e, value);
            if (alias && count < capacity)
            {
                found[count] = e;
            }
            count += alias ? 1 : 0;
        }
    }
    return count;
}

// Defines `symbol` (an import of the copy's library, or a new alias) at the copy's atom: a definition of the link's
// from now on, exported at its version (the import's, mapped to the executable's numbering).
static bool foreign_link_define_copy(Link* link, SymbolId symbol, AtomId atom, uint64_t size, uint16_t version)
{
    const bool result = link_define_symbol(link, symbol, atom, 0, size);
    if (result)
    {
        link->symbols.source[symbol] = BUSTER_LINK_SOURCE_FOREIGN_OBJECT;
        link->symbols.version[symbol] = version;
    }
    return result;
}

static ForeignLinkResult foreign_link_copies(ForeignLink* output, const Foreign* foreign, Arena* arena)
{
    ForeignLinkResult result = { 0 };
    Link* link = &output->link;
    const uint32_t words = 2 * ((link->symbols.count + 63) / 64);
    Scratch scratch = scratch_begin(arena);
    uint32_t* imports = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, words);
    uint32_t* copied = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, words);
    result.error = imports != 0 && copied != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    uint64_t marked = 0;
    if (result.error == FOREIGN_LINK_ERROR_NONE && foreign_link_imports(link, imports, foreign->path) != 0)
    {
        memset(copied, 0, (uint64_t)words * sizeof(uint32_t));
        foreign_link_mark_copies(link, imports, copied, foreign->path);
        for (uint32_t w = 0; w < words; w += 1)
        {
            marked += (uint64_t)__builtin_popcount(copied[w]);
        }
    }
    ForeignLinkCopy* copies = marked != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, ForeignLinkCopy, marked) : 0;
    output->copies = marked != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, marked) : 0;
    result.error = marked != 0 && (copies == 0 || output->copies == 0) ? FOREIGN_LINK_ERROR_MEMORY : result.error;
    uint32_t copy_count = 0;
    // The copied imports' names, 16 names' symbols at a time against the bitset: each one's copy, by library and address.
    const ForeignNames* names = &foreign->names;
    for (uint32_t first = 0; result.error == FOREIGN_LINK_ERROR_NONE && marked != 0 && first < names->count; first += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, names->count - first < 16 ? names->count - first : 16);
        const __m512i symbols = _mm512_maskz_loadu_epi32(lanes, output->names + first);
        const __m512i bits = _mm512_srlv_epi32(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, _mm512_srli_epi32(symbols, 5), copied, 4),
                                               _mm512_and_si512(symbols, _mm512_set1_epi32(31)));
        for (uint32_t hits = _mm512_mask_test_epi32_mask(lanes, bits, _mm512_set1_epi32(1)); result.error == FOREIGN_LINK_ERROR_NONE && hits != 0; hits &= hits - 1)
        {
            const uint32_t name = first + (uint32_t)__builtin_ctz(hits);
            const SymbolId symbol = output->names[name];
            const uint32_t library = names->library[name];
            const ImportExports* exports = &foreign->libraries[library].library.exports;
            const uint32_t e = names->symbol[name];
            const bool copyable = names->resolution[name] == FOREIGN_RESOLUTION_SHARED && exports->value != 0 && exports->size[e] != 0 &&
                                  (exports->flags[e] & BUSTER_LINK_SYMBOL_TYPE) != BUSTER_LINK_SYMBOL_FUNCTION;
            uint32_t c = 0;
            while (copyable && c < copy_count && (copies[c].library != library || copies[c].value != exports->value[e]))
            {
                c += 1;
            }
            if (!copyable)
            {
                result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
                result.name = name + 1;
            }
            else if (c == copy_count)
            {
                copies[c] = (ForeignLinkCopy){
                    .library = library,
                    .value = exports->value[e],
                    .section = exports->section[e],
                    .symbol = symbol,
                };
                copy_count += 1;
            }
        }
    }
    // Each copy's names, every export of its library at its address, counted and then gathered into room of their
    // count: each is defined at the copy, so that the library's own references to any of them bind to it, as lld's
    // getSymbolsAt defines them. The copies record at most each name and the copied import itself as exports.
    uint32_t** copy_names = copy_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t*, copy_count) : 0;
    uint32_t* name_counts = copy_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, copy_count) : 0;
    result.error = copy_count != 0 && (copy_names == 0 || name_counts == 0) ? FOREIGN_LINK_ERROR_MEMORY : result.error;
    uint64_t export_capacity = 0;
    for (uint32_t c = 0; result.error == FOREIGN_LINK_ERROR_NONE && c < copy_count; c += 1)
    {
        const ImportExports* exports = &foreign->libraries[copies[c].library].library.exports;
        const uint32_t count = foreign_link_aliases(exports, copies[c].value, 0, 0);
        copy_names[c] = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)count + 1);
        name_counts[c] = copy_names[c] != 0 ? foreign_link_aliases(exports, copies[c].value, copy_names[c], count) : 0;
        result.error = copy_names[c] != 0 ? result.error : FOREIGN_LINK_ERROR_MEMORY;
        export_capacity += (uint64_t)count + 1;
    }
    output->copy_exports = result.error == FOREIGN_LINK_ERROR_NONE && export_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, export_capacity) : 0;
    result.error = result.error == FOREIGN_LINK_ERROR_NONE && export_capacity != 0 && output->copy_exports == 0 ? FOREIGN_LINK_ERROR_MEMORY : result.error;
    // Each copy's atom, as large as its largest name, then its names defined there: the library's exports at its address,
    // the link's imports of them and new symbols for the others.
    for (uint32_t c = 0; result.error == FOREIGN_LINK_ERROR_NONE && c < copy_count; c += 1)
    {
        ForeignLinkCopy* copy = &copies[c];
        const ForeignLibrary* library = &foreign->libraries[copy->library];
        const ImportExports* exports = &library->library.exports;
        const uint32_t* aliases = copy_names[c];
        const uint32_t alias_count = name_counts[c];
        uint64_t size = link->symbols.size[copy->symbol];
        for (uint32_t a = 0; a < alias_count; a += 1)
        {
            size = exports->size[aliases[a]] > size ? exports->size[aliases[a]] : size;
        }
        copy->atom = link_add_atom(link, BUSTER_LINK_CLASS_BSS, foreign_link_copy_alignment(foreign, library, copy->value, copy->section), size, 0, 0);
        result.error = copy->atom != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_LINK;
        uint32_t added = 0;
        for (uint32_t a = 0; result.error == FOREIGN_LINK_ERROR_NONE && a < alias_count; a += 1)
        {
            const uint32_t e = aliases[a];
            const uint32_t name = foreign_find_name(foreign, exports->name[e], exports->length[e], exports->hash[e]);
            const SymbolId existing = name != 0 ? output->names[name - 1] : 0;
            const uint16_t original = exports->version[e];
            const uint16_t version = original > VER_NDX_GLOBAL ? output->library_versions[copy->library][original] : 0;
            SymbolId symbol = 0;
            if (existing != 0 && link->symbols.source[existing] == BUSTER_LINK_SOURCE_FOREIGN_SHARED && names->library[name - 1] == copy->library)
            {
                symbol = foreign_link_define_copy(link, existing, copy->atom, exports->size[e], version) ? existing : 0;
                result.error = symbol != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_LINK;
            }
            else if (existing == 0 && added < BUSTER_FOREIGN_LINK_COPY_ALIASES)
            {
                // A name of the library's that nothing in the link refers to: its own symbol, with the library's binding.
                const NameId named = link_add_name_hashed(link, exports->name[e], exports->length[e], exports->hash[e]);
                const uint8_t flags = exports->flags[e] & (BUSTER_LINK_SYMBOL_TYPE | BUSTER_LINK_SYMBOL_BINDING);
                symbol = named != 0 ? link_add_symbol(link, named, copy->atom, 0, exports->size[e], flags, BUSTER_LINK_SOURCE_FOREIGN_OBJECT) : 0;
                result.error = symbol != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_LINK;
                link->symbols.version[symbol] = version;
                added += 1;
            }
            else if (existing == 0)
            {
                result.error = FOREIGN_LINK_ERROR_CAPACITY;
            }
            // Room for every name and the import itself was counted above; one past it would be a bug, refused.
            const bool room = output->copy_export_count < export_capacity;
            if (symbol != 0 && room)
            {
                output->copy_exports[output->copy_export_count] = symbol;
                output->copy_export_count += 1;
            }
            result.error = symbol != 0 && !room ? FOREIGN_LINK_ERROR_CAPACITY : result.error;
        }
        // The copied import itself, if its library's export table didn't list it at its address (its default version
        // is another than the one it binds to).
        if (result.error == FOREIGN_LINK_ERROR_NONE && link->symbols.source[copy->symbol] == BUSTER_LINK_SOURCE_FOREIGN_SHARED)
        {
            const bool defined = foreign_link_define_copy(link, copy->symbol, copy->atom, link->symbols.size[copy->symbol], link->symbols.version[copy->symbol]);
            result.error = !defined ? FOREIGN_LINK_ERROR_LINK : output->copy_export_count < export_capacity ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_CAPACITY;
            if (result.error == FOREIGN_LINK_ERROR_NONE)
            {
                output->copy_exports[output->copy_export_count] = copy->symbol;
                output->copy_export_count += 1;
            }
        }
        output->copies[c] = copy->symbol;
    }
    output->copy_count = result.error == FOREIGN_LINK_ERROR_NONE ? copy_count : 0;
    scratch_end(scratch);
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// __start_ and __stop_
//
// As GNU ld and lld define them: when something refers to __start_NAME or __stop_NAME and input sections are named
// NAME, a C identifier, the output's section of that name starts and ends there, and both linkers define the two names
// global and protected, of no size. The input sections of the name, in input order, become one atom of their class (of
// the shared unit, BUSTER_LINK_UNIT_SHARED, since they come from several inputs), each at its alignment after the one
// before, with their symbols and fixups moved into it and the old atoms left empty. Only an undefined name with one of
// the prefixes asks for this, so that a link without one reads no section's name; the names are a few in a program, and
// are handled one at a time, and the symbols' move is tested 16 at a time against a map of the merged atoms.

typedef struct ForeignLinkBound ForeignLinkBound;
struct ForeignLinkBound
{
    uint32_t object;
    uint32_t section;
    uint32_t group;
    // Its place in its group's merged atom.
    uint64_t base;
};

struct ForeignLinkBounds
{
    // The names that ask (ForeignNames ids), each one's group (a section name) and whether it is __stop_'s.
    uint32_t* names;
    uint32_t* name_groups;
    uint8_t* stops;
    uint32_t name_count;
    String8* groups;
    AtomId* group_atoms;
    uint64_t* group_sizes;
    uint32_t group_count;
    // The input sections of the groups' names, in input order.
    ForeignLinkBound* sections;
    uint32_t section_count;
    uint64_t relocations;
};

static bool foreign_link_c_identifier(const uint8_t* bytes, uint64_t length)
{
    bool result = length != 0 && !(bytes[0] >= '0' && bytes[0] <= '9');
    for (uint64_t i = 0; result && i < length; i += 1)
    {
        const uint8_t c = bytes[i];
        result = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
    return result;
}

// The suffix of a __start_ or __stop_ name, and whether it is __stop_'s; length 0 for another name.
static String8 foreign_link_bound_suffix(const uint8_t* bytes, uint32_t length, bool* stop)
{
    const bool start = length > 8 && memcmp(bytes, "__start_", 8) == 0;
    *stop = length > 7 && memcmp(bytes, "__stop_", 7) == 0;
    const uint32_t prefix = start ? 8 : *stop ? 7 : length;
    const String8 result = { .pointer = (char*)bytes + prefix, .length = length - prefix };
    return foreign_link_c_identifier((const uint8_t*)result.pointer, result.length) ? result : (String8){ 0 };
}

// The undefined names (64 resolutions at a time) with one of the prefixes and a C identifier after it: how many, and
// with the plan's columns pushed, each one, its group and its prefix, the groups gathered.
static uint32_t foreign_link_bounds_names(const Foreign* foreign, ForeignLinkBounds* plan)
{
    const ForeignNames* names = &foreign->names;
    uint32_t result = 0;
    for (uint32_t first = 0; first < names->count; first += 64)
    {
        const uint32_t count = names->count - first < 64 ? names->count - first : 64;
        const __mmask64 lanes = _bzhi_u64(~0ull, count);
        const __m512i resolutions = _mm512_maskz_loadu_epi8(lanes, names->resolution + first);
        uint64_t undefined = _mm512_mask_cmpeq_epi8_mask(lanes, resolutions, _mm512_set1_epi8(FOREIGN_RESOLUTION_UNDEFINED)) |
                             _mm512_mask_cmpeq_epi8_mask(lanes, resolutions, _mm512_set1_epi8(FOREIGN_RESOLUTION_WEAK_UNDEFINED));
        for (; undefined != 0; undefined &= undefined - 1)
        {
            const uint32_t name = first + (uint32_t)__builtin_ctzll(undefined);
            bool stop = false;
            const String8 suffix = foreign_link_bound_suffix(names->bytes[name], names->length[name], &stop);
            if (suffix.length != 0 && plan->names != 0)
            {
                uint32_t group = 0;
                while (group < plan->group_count && !(plan->groups[group].length == suffix.length && memcmp(plan->groups[group].pointer, suffix.pointer, suffix.length) == 0))
                {
                    group += 1;
                }
                plan->groups[group] = suffix;
                plan->group_count += group == plan->group_count ? 1 : 0;
                plan->names[plan->name_count] = name;
                plan->name_groups[plan->name_count] = group;
                plan->stops[plan->name_count] = stop ? 1 : 0;
                plan->name_count += 1;
            }
            result += suffix.length != 0 ? 1 : 0;
        }
    }
    return result;
}

// The plan, from those names and the selected objects' sections in input order.
static ForeignLinkError foreign_link_bounds_plan(const Foreign* foreign, const uint32_t* order, ForeignLinkBounds* plan, Arena* arena)
{
    *plan = (ForeignLinkBounds){ 0 };
    const uint32_t candidates = foreign_link_bounds_names(foreign, plan);
    if (candidates != 0)
    {
        plan->names = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, candidates);
        plan->name_groups = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, candidates);
        plan->stops = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, candidates);
        plan->groups = BUSTER_ARENA_PUSH_ARRAY(arena, String8, candidates);
        plan->group_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, candidates);
        plan->group_sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, candidates);
        const bool pushed = plan->names != 0 && plan->name_groups != 0 && plan->stops != 0 && plan->groups != 0 && plan->group_atoms != 0 && plan->group_sizes != 0;
        plan->names = pushed ? plan->names : 0;
        if (pushed)
        {
            foreign_link_bounds_names(foreign, plan);
        }
    }
    ForeignLinkError result = candidates != 0 && plan->names == 0 ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_NONE;
    // The groups' sections: counted, then listed, in input order.
    for (uint32_t pass = 0; result == FOREIGN_LINK_ERROR_NONE && plan->group_count != 0 && pass < 2; pass += 1)
    {
        uint32_t found = 0;
        for (uint32_t at = 0; at < foreign->object_count; at += 1)
        {
            const uint32_t id = order[at];
            const ForeignObject* input = &foreign->objects[id];
            for (uint32_t section = 1; id != 0 && input->library == 0 && section < input->elf.section_count; section += 1)
            {
                const Elf64_Shdr header = import_section_header(&input->elf, section);
                const char* name = header.sh_name < input->elf.section_names.length ? input->elf.section_names.pointer + header.sh_name : "";
                const uint64_t length = strnlen(name, input->elf.section_names.length - (header.sh_name < input->elf.section_names.length ? header.sh_name : 0));
                const bool kept = (header.sh_flags & SHF_ALLOC) != 0 && (input->discarded == 0 || input->discarded[section] == 0);
                uint32_t group = 0;
                while (kept && group < plan->group_count && !(plan->groups[group].length == length && memcmp(plan->groups[group].pointer, name, length) == 0))
                {
                    group += 1;
                }
                if (kept && group < plan->group_count)
                {
                    if (pass == 1)
                    {
                        plan->sections[found] = (ForeignLinkBound){ .object = id, .section = section, .group = group };
                    }
                    found += 1;
                }
            }
            for (uint32_t r = 0; pass == 1 && id != 0 && input->library == 0 && r < input->relocation_count; r += 1)
            {
                const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
                for (uint32_t b = 0; b < found; b += 1)
                {
                    plan->relocations += plan->sections[b].object == id && plan->sections[b].section == header.sh_info ? header.sh_size / sizeof(Elf64_Rela) : 0;
                }
            }
        }
        if (pass == 0)
        {
            plan->sections = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkBound, (uint64_t)found + 1);
            result = plan->sections != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
        }
        plan->section_count = found;
    }
    // A name whose group has no section stays undefined.
    uint32_t kept = 0;
    for (uint32_t n = 0; result == FOREIGN_LINK_ERROR_NONE && n < plan->name_count; n += 1)
    {
        bool sectioned = false;
        for (uint32_t b = 0; !sectioned && b < plan->section_count; b += 1)
        {
            sectioned = plan->sections[b].group == plan->name_groups[n];
        }
        plan->names[kept] = plan->names[n];
        plan->name_groups[kept] = plan->name_groups[n];
        plan->stops[kept] = plan->stops[n];
        kept += sectioned ? 1 : 0;
    }
    plan->name_count = result == FOREIGN_LINK_ERROR_NONE ? kept : 0;
    return result;
}

// Whether section `section` of object `object` is one of the plan's.
static bool foreign_link_bound_section(const ForeignLinkBounds* plan, uint32_t object, uint32_t section)
{
    bool result = false;
    for (uint32_t b = 0; !result && plan != 0 && b < plan->section_count; b += 1)
    {
        result = plan->sections[b].object == object && plan->sections[b].section == section;
    }
    return result;
}

// Whether foreign name `name` is one the plan defines.
static bool foreign_link_bounded(const ForeignLinkBounds* plan, uint32_t name)
{
    bool result = false;
    for (uint32_t n = 0; !result && plan != 0 && n < plan->name_count; n += 1)
    {
        result = plan->names[n] == name;
    }
    return result;
}

// Each group's sections merged into an atom, the symbols moved and the names defined (see the section's comment), before
// the relocations: foreign_link_bounds_relocations imports the sections' relocations into the merged atoms afterwards.
static ForeignLinkResult foreign_link_bounds_merge(ForeignLink* output, const ForeignLinkBounds* plan, Arena* arena)
{
    Link* link = &output->link;
    ForeignLinkResult result = { 0 };
    Scratch scratch = scratch_begin(arena);
    // Each old atom's merged atom (0 for none) and its place there.
    AtomId* merged = plan->group_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, AtomId, link->atoms.count) : 0;
    uint64_t* bases = plan->group_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, link->atoms.count) : 0;
    result.error = plan->group_count != 0 && (merged == 0 || bases == 0) ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_NONE;
    if (merged != 0)
    {
        memset(merged, 0, (uint64_t)link->atoms.count * sizeof(AtomId));
    }
    const uint32_t atom_count = link->atoms.count;
    for (uint32_t group = 0; result.error == FOREIGN_LINK_ERROR_NONE && group < plan->group_count; group += 1)
    {
        uint8_t class = BUSTER_LINK_CLASS_NONE;
        uint8_t alignment = 0;
        uint64_t size = 0;
        bool alike = true;
        for (uint32_t b = 0; b < plan->section_count; b += 1)
        {
            const AtomId atom = plan->sections[b].group == group ? output->objects[plan->sections[b].object].section_atoms[plan->sections[b].section] : 0;
            if (atom != 0)
            {
                // Sections of one name but of other flags make one writable section, as GNU ld's and lld's do: read-only,
                // RELRO, writable and zero-fill ones merge as data (the zero-fill ones' bytes zeros); code and TLS
                // sections merge only with their own class.
                const uint8_t own = link->atoms.class[atom];
                const bool data = own == BUSTER_LINK_CLASS_RODATA || own == BUSTER_LINK_CLASS_DATA_RELRO || own == BUSTER_LINK_CLASS_DATA || own == BUSTER_LINK_CLASS_BSS;
                const bool data_class = class == BUSTER_LINK_CLASS_RODATA || class == BUSTER_LINK_CLASS_DATA_RELRO || class == BUSTER_LINK_CLASS_DATA || class == BUSTER_LINK_CLASS_BSS;
                alike = alike && !link_class_is_tls(own) && (class == BUSTER_LINK_CLASS_NONE || own == class || (data && data_class));
                class = class == BUSTER_LINK_CLASS_NONE || own == class ? own : BUSTER_LINK_CLASS_DATA;
                alignment = link->atoms.alignment_log2[atom] > alignment ? link->atoms.alignment_log2[atom] : alignment;
                const uint64_t mask = ((uint64_t)1 << link->atoms.alignment_log2[atom]) - 1;
                bases[atom] = (size + mask) & ~mask;
                size = bases[atom] + link->atoms.size[atom];
            }
        }
        // A group whose name no section has (a weak reference's) merges nothing; its names stay undefined.
        const bool sectioned = class != BUSTER_LINK_CLASS_NONE;
        const bool zero_fill = link_class_is_zero_fill(class);
        uint8_t* bytes = sectioned && !zero_fill && size != 0 ? arena_push(arena, size, 64) : 0;
        const AtomId atom = alike && sectioned && (zero_fill || size == 0 || bytes != 0) ? link_add_atom(link, class, alignment, size, bytes, BUSTER_LINK_UNIT_SHARED) : 0;
        result.error = !alike ? FOREIGN_LINK_ERROR_UNSUPPORTED : sectioned && atom == 0 ? FOREIGN_LINK_ERROR_LINK : FOREIGN_LINK_ERROR_NONE;
        if (bytes != 0)
        {
            memset(bytes, 0, size);
        }
        plan->group_atoms[group] = atom;
        plan->group_sizes[group] = size;
        for (uint32_t b = 0; result.error == FOREIGN_LINK_ERROR_NONE && b < plan->section_count; b += 1)
        {
            const AtomId old = plan->sections[b].group == group ? output->objects[plan->sections[b].object].section_atoms[plan->sections[b].section] : 0;
            if (old != 0)
            {
                merged[old] = atom;
                plan->sections[b].base = bases[old];
                if (bytes != 0 && link->atoms.size[old] != 0 && link->atoms.bytes[old] != 0)
                {
                    memcpy(bytes + bases[old], link->atoms.bytes[old], link->atoms.size[old]);
                }
                link->atoms.size[old] = 0;
                link->atoms.alignment_log2[old] = 0;
                // From now on the section is the group's atom, at its base (which the relocations take from the plan):
                // the passes after the conversion find a section's atom through the map (GC's retained roots and its
                // unwind frames, foreign_gc_link.c), and must find the atom that holds its bytes.
                output->objects[plan->sections[b].object].section_atoms[plan->sections[b].section] = atom;
            }
        }
    }
    // The symbols of the old atoms, 16 atoms' maps at a time.
    LinkSymbols* symbols = &link->symbols;
    for (uint32_t first = 1; result.error == FOREIGN_LINK_ERROR_NONE && plan->group_count != 0 && first < symbols->count; first += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, symbols->count - first < 16 ? symbols->count - first : 16);
        const __m512i atoms = _mm512_maskz_loadu_epi32(lanes, symbols->atom + first);
        const __mmask16 inside = _mm512_mask_cmplt_epu32_mask(lanes, atoms, _mm512_set1_epi32((int)atom_count));
        const __m512i targets = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), inside, atoms, merged, 4);
        for (uint32_t moved = _mm512_mask_test_epi32_mask(inside, targets, targets); moved != 0; moved &= moved - 1)
        {
            const SymbolId symbol = first + (SymbolId)__builtin_ctz(moved);
            const AtomId old = symbols->atom[symbol];
            symbols->value[symbol] += bases[old];
            symbols->atom[symbol] = merged[old];
        }
    }
    for (uint32_t n = 0; result.error == FOREIGN_LINK_ERROR_NONE && n < plan->name_count; n += 1)
    {
        const uint32_t group = plan->name_groups[n];
        const SymbolId symbol = output->names[plan->names[n]];
        const bool defined = symbol != 0 && link_define_symbol(link, symbol, plan->group_atoms[group], plan->stops[n] != 0 ? plan->group_sizes[group] : 0, 0);
        result.error = defined ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_LINK;
        result.name = defined ? 0 : plan->names[n] + 1;
        if (defined)
        {
            symbols->source[symbol] = BUSTER_LINK_SOURCE_FOREIGN_OBJECT;
        }
    }
    scratch_end(scratch);
    return result;
}

// The bound sections' relocations, after every other atom's, into their groups' merged atoms in plan order: each section's
// records with its place in the merged atom added to their offsets, so that the atom's fixups are one range in order.
static ImportResult foreign_link_bounds_relocations(ForeignLink* output, const Foreign* foreign, const ForeignLinkBounds* plan, Arena* arena)
{
    ImportResult result = { 0 };
    for (uint32_t group = 0; result.error == IMPORT_ERROR_NONE && group < plan->group_count; group += 1)
    {
        for (uint32_t b = 0; result.error == IMPORT_ERROR_NONE && b < plan->section_count; b += 1)
        {
            const ForeignLinkBound* bound = &plan->sections[b];
            const ForeignObject* input = &foreign->objects[bound->object];
            for (uint32_t r = 0; bound->group == group && result.error == IMPORT_ERROR_NONE && r < input->relocation_count; r += 1)
            {
                const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
                if (header.sh_info == bound->section)
                {
                    const String8 entries = foreign_link_relocation_bytes(output, input, bound->object, &header);
                    result = foreign_link_unwind_references(output, foreign, bound->object, input->relocations[r], entries);
                    Scratch scratch = scratch_begin(arena);
                    Elf64_Rela* rebased = result.error == IMPORT_ERROR_NONE ? arena_push(scratch.arena, entries.length + 1, 8) : 0;
                    result = result.error == IMPORT_ERROR_NONE && rebased == 0 ? import_fail(IMPORT_ERROR_MEMORY, 0) : result;
                    for (uint64_t i = 0; rebased != 0 && i < entries.length / sizeof(Elf64_Rela); i += 1)
                    {
                        memcpy(&rebased[i], entries.pointer + i * sizeof(Elf64_Rela), sizeof(Elf64_Rela));
                        rebased[i].r_offset += bound->base;
                    }
                    if (rebased != 0)
                    {
                        const String8 moved = { .pointer = (char*)rebased, .length = entries.length };
                        result = import_relocation_entries(&output->link, &input->symbols, &output->objects[bound->object], plan->group_atoms[group], moved, 0, scratch.arena);
                        result.section = result.error != IMPORT_ERROR_NONE ? input->relocations[r] : 0;
                    }
                    scratch_end(scratch);
                }
            }
        }
    }
    return result;
}

// The IFUNCs an output can't take that it defines and refers to. The writer gives an IFUNC a stub and an IRELATIVE
// relocation, which something must apply before the IFUNC is called: in a dynamic PIE the loader, which applies them
// all; in a static executable the startup code that reads them between __rela_iplt_start and __rela_iplt_end (libc.a's
// __libc_start_main), and in a static PIE the one that relocates the image through _DYNAMIC (libc.a's
// _dl_relocate_static_pie). Other static startup (a program without libc, whose own _start applies nothing) may have
// no such code, and the kernel applies no relocations, where GNU ld and lld would write a program whose first call
// faults: so for a static link every IFUNC a fixup reaches is found (`every`), and the conversion refuses it unless the
// startup applies it (foreign_link_startup_applies, asked only then). A shared library can't take one the loader can't
// bind as a dynamic symbol yet: one that isn't preemptible (local, or of hidden or protected visibility) needs an
// IRELATIVE relocation, which the writer's shared mode doesn't write, while a preemptible one's GOT slot gets a GLOB_DAT
// that the loader binds by calling its resolver, as it binds an IFUNC that another library defines. The first refused
// IFUNC a fixup reaches is refused, by its name and object, rather than written as a plain function, which calls the
// resolver; one that nothing reaches is written as it is. The refused IFUNCs are a bitset, 64 symbols' flags at a time,
// and only a link with one tests the fixups' targets against it, 16 at a time.
static ForeignLinkResult foreign_link_refuse_ifuncs(const ForeignLink* output, const Foreign* foreign, Arena* arena, bool every)
{
    const Link* link = &output->link;
    const LinkSymbols* symbols = &link->symbols;
    ForeignLinkResult result = { 0 };
    Scratch scratch = scratch_begin(arena);
    const uint32_t words = 2 * ((symbols->count + 63) / 64);
    uint32_t* ifuncs = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, words);
    result.error = ifuncs != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    uint64_t defined = 0;
    for (uint32_t first = 0; ifuncs != 0 && first < symbols->count; first += 64)
    {
        const __mmask64 lanes = _bzhi_u64(~0ull, symbols->count - first < 64 ? symbols->count - first : 64);
        const __m512i flags = _mm512_maskz_loadu_epi8(lanes, symbols->flags + first);
        const __m512i types = _mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_TYPE));
        const uint64_t local = _mm512_mask_cmpeq_epi8_mask(lanes, _mm512_and_si512(flags, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_BINDING)), _mm512_setzero_si512());
        const uint64_t visible = _mm512_mask_cmpeq_epi8_mask(lanes, _mm512_and_si512(flags, _mm512_set1_epi8((char)BUSTER_LINK_SYMBOL_VISIBILITY)), _mm512_setzero_si512());
        uint64_t mask = _mm512_mask_cmpeq_epi8_mask(lanes, types, _mm512_set1_epi8(BUSTER_LINK_SYMBOL_IFUNC)) & (every ? ~0ull : local | ~visible);
        for (uint64_t left = mask; left != 0; left &= left - 1)
        {
            const uint32_t bit = (uint32_t)__builtin_ctzll(left);
            mask &= symbols->atom[first + bit] != 0 ? ~0ull : ~(1ull << bit);
        }
        ifuncs[first / 32] = (uint32_t)mask;
        ifuncs[first / 32 + 1] = (uint32_t)(mask >> 32);
        defined += (uint64_t)__builtin_popcountll(mask);
    }
    const LinkFixups* fixups = &link->fixups;
    SymbolId reached = 0;
    for (FixupId first = 1; defined != 0 && reached == 0 && first < fixups->count; first += 16)
    {
        const __mmask16 lanes = (__mmask16)_bzhi_u32(0xffff, fixups->count - first < 16 ? fixups->count - first : 16);
        const __m512i targets = _mm512_maskz_loadu_epi32(lanes, fixups->target + first);
        const __m512i bits = _mm512_srlv_epi32(_mm512_mask_i32gather_epi32(_mm512_setzero_si512(), lanes, _mm512_srli_epi32(targets, 5), ifuncs, 4),
                                               _mm512_and_si512(targets, _mm512_set1_epi32(31)));
        const uint32_t hits = _mm512_mask_test_epi32_mask(lanes, bits, _mm512_set1_epi32(1));
        reached = hits != 0 ? fixups->target[first + (FixupId)__builtin_ctz(hits)] : 0;
    }
    if (reached != 0)
    {
        const NameId named = symbols->name[reached];
        const uint32_t name = named != 0 ? foreign_find_name(foreign, link->names.bytes[named], link->names.length[named], link->names.hash[named]) : 0;
        const uint32_t unit = link->atoms.unit[symbols->atom[reached]];
        result.error = FOREIGN_LINK_ERROR_IFUNC;
        result.name = name != 0 && output->names[name - 1] == reached ? name : 0;
        result.object = unit < foreign->object_count ? unit : 0;
    }
    scratch_end(scratch);
    return result;
}

// _DYNAMIC, which every linker defines in a dynamic image (lld weak and hidden) whether or not anything refers to it,
// so that a regular symbol table has it: added if the link has no symbol of it.
static ForeignLinkError foreign_link_dynamic_symbol(ForeignLink* output, const Foreign* foreign)
{
    const uint8_t* text = (const uint8_t*)"_DYNAMIC";
    const uint32_t length = 8;
    const uint64_t hash = link_hash(text, length);
    const uint32_t name = foreign_find_name(foreign, text, length, hash);
    ForeignLinkError result = FOREIGN_LINK_ERROR_NONE;
    if (name == 0 || output->names[name - 1] == 0)
    {
        const uint8_t flags = BUSTER_LINK_SYMBOL_WEAK | BUSTER_LINK_SYMBOL_HIDDEN;
        const NameId named = link_add_name_hashed(&output->link, text, length, hash);
        const SymbolId symbol = named != 0 ? link_add_symbol(&output->link, named, 0, 0, 0, flags, BUSTER_LINK_SOURCE_LINKER) : 0;
        result = symbol != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_LINK;
    }
    return result;
}

static SymbolId foreign_link_named(const ForeignLink* output, const Foreign* foreign, const char* text)
{
    const uint32_t length = (uint32_t)strlen(text);
    const uint32_t name = foreign_find_name(foreign, (const uint8_t*)text, length, link_hash((const uint8_t*)text, length));
    const SymbolId symbol = name != 0 ? output->names[name - 1] : 0;
    const AtomId atom = output->link.symbols.atom[symbol];
    const uint8_t class = output->link.atoms.class[atom];
    // DT_INIT/DT_FINI name callbacks in this image. An undefined weak name or a DSO's definition is not one.
    const bool code = atom != 0 && (class == BUSTER_LINK_CLASS_TEXT || class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI);
    return code ? symbol : 0;
}

// The ABI note of the first regular object that has one: the start file's (Scrt1.o's, crt1.o's or rcrt1.o's), which the
// output repeats, borrowed from Foreign's mapping.
static String8 foreign_link_abi_note(const Foreign* foreign)
{
    String8 result = { 0 };
    for (uint32_t id = 1; id < foreign->object_count && result.length == 0; id += 1)
    {
        if (foreign->objects[id].library == 0)
        {
            result = runtime_section(foreign->objects[id].bytes, ".note.ABI-tag");
        }
    }
    return result;
}

// The link's symbol of a global name, or 0.
static SymbolId foreign_link_symbol(const ForeignLink* output, const Foreign* foreign, const char* text)
{
    const uint32_t length = (uint32_t)strlen(text);
    const uint32_t name = foreign_find_name(foreign, (const uint8_t*)text, length, link_hash((const uint8_t*)text, length));
    return name != 0 ? output->names[name - 1] : 0;
}

// A needed library may refer back to the executable, or define a name the executable interposes. Export those
// visible definitions once in canonical name order. Borrowed version strings and note bytes live with Foreign.
static ForeignLinkError foreign_link_dynamic_finish(ForeignLink* output, const Foreign* foreign, Arena* arena)
{
    uint8_t* marked = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, foreign->names.count);
    const uint64_t capacity = (uint64_t)foreign->names.count + output->copy_export_count;
    SymbolId* exports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, capacity);
    uint16_t* versions = output->copy_export_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint16_t, capacity) : 0;
    ForeignLinkError result = marked == 0 || exports == 0 || (output->copy_export_count != 0 && versions == 0) ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_NONE;
    if (result == FOREIGN_LINK_ERROR_NONE)
    {
        memset(marked, 0, foreign->names.count);
        for (uint32_t l = 0; l < foreign->library_count; l += 1)
        {
            const ForeignLibrary* library = &foreign->libraries[l];
            if (library->needed)
            {
                const ImportExports* definitions = &library->library.exports;
                const ImportReferences* references = &library->library.references;
                for (uint32_t d = 0; d < definitions->count; d += 1)
                {
                    const uint32_t name = foreign_find_name(foreign, definitions->name[d], definitions->length[d], definitions->hash[d]);
                    if (name != 0)
                    {
                        marked[name - 1] = 1;
                    }
                }
                for (uint32_t r = 0; r < references->count; r += 1)
                {
                    const uint32_t name = foreign_find_name(foreign, references->name[r], references->length[r], references->hash[r]);
                    if (name != 0)
                    {
                        marked[name - 1] = 1;
                    }
                }
            }
        }
        const LinkSymbols* symbols = &output->link.symbols;
        // The copies' names go last, each once, at their versions; a copied name is a library's, so marked.
        for (uint32_t c = 0; c < output->copy_export_count; c += 1)
        {
            const NameId named = symbols->name[output->copy_exports[c]];
            const uint32_t name = foreign_find_name(foreign, output->link.names.bytes[named], output->link.names.length[named], output->link.names.hash[named]);
            if (name != 0 && output->names[name - 1] == output->copy_exports[c])
            {
                marked[name - 1] = 0;
            }
        }
        uint32_t count = 0;
        for (uint32_t n = 0; n < foreign->names.count; n += 1)
        {
            const SymbolId symbol = output->names[n];
            const bool defined = symbol != 0 && (symbols->atom[symbol] != 0 || symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE);
            const uint8_t visibility = symbols->flags[symbol] & BUSTER_LINK_SYMBOL_VISIBILITY;
            if (marked[n] != 0 && defined && (visibility == BUSTER_LINK_SYMBOL_DEFAULT || visibility == BUSTER_LINK_SYMBOL_PROTECTED))
            {
                exports[count] = symbol;
                if (versions != 0)
                {
                    versions[count] = 0;
                }
                count += 1;
            }
        }
        for (uint32_t c = 0; c < output->copy_export_count; c += 1)
        {
            exports[count] = output->copy_exports[c];
            versions[count] = symbols->version[output->copy_exports[c]];
            count += 1;
        }
        output->dynamic.exports = exports;
        output->dynamic.export_count = count;
        output->dynamic.export_versions = versions;
        output->dynamic.copies = output->copies;
        output->dynamic.copy_count = output->copy_count;
        output->dynamic.init = foreign_link_named(output, foreign, "_init");
        output->dynamic.fini = foreign_link_named(output, foreign, "_fini");
        output->dynamic.note = foreign_link_abi_note(foreign);
    }
    return result;
}

// The conversion's state, through its steps: the exact path takes them on the calling thread, and the lane path
// (foreign_link_lanes.c) takes them on lane 0 between its passes on lanes.
typedef struct ForeignLinkBuild ForeignLinkBuild;
struct ForeignLinkBuild
{
    ForeignLink* output;
    const Foreign* foreign;
    Arena* arena;
    Arena* name_arena;
    RuntimeKind kind;
    String8 interpreter;
    bool defer_undefined;
    ForeignLinkOptions options;
    // The calling thread's scratch, from foreign_link_build_begin to foreign_link_build_finish: the input order, the
    // __start_ and __stop_ plan and the names the link requires live there.
    Scratch scratch;
    uint32_t* order;
    uint8_t* required;
    ForeignLinkBounds bounds;
    ForeignLinkResult result;
    // When the conversion started, and when each phase ended.
    uint64_t start;
    uint64_t ends[FOREIGN_LINK_PHASES];
};

// The steps up to the global names: the libraries, the order, the __start_ and __stop_ plan, the link, every object's
// sections and locals, the COMDAT unwind plans and the names the link requires.
static void foreign_link_build_begin(ForeignLinkBuild* build)
{
    ForeignLink* output = build->output;
    const Foreign* foreign = build->foreign;
    Arena* arena = build->arena;
    const RuntimeKind kind = build->kind;
    *output = (ForeignLink){ 0 };
    ForeignLinkResult result = { 0 };
    LinkCapacity capacity = { 0 };
    const bool kind_known = kind == RUNTIME_KIND_STATIC || kind == RUNTIME_KIND_STATIC_PIE || kind == RUNTIME_KIND_DYNAMIC || kind == RUNTIME_KIND_SHARED;
    if (!foreign->started || foreign->failure.error != FOREIGN_ERROR_NONE || !kind_known ||
        (kind == RUNTIME_KIND_DYNAMIC && (build->interpreter.pointer == 0 || build->interpreter.length == 0)))
    {
        result.error = FOREIGN_LINK_ERROR_INPUT;
    }
    for (uint32_t library = 0; result.error == FOREIGN_LINK_ERROR_NONE && runtime_kind_is_static(kind) && library < foreign->library_count; library += 1)
    {
        result.error = foreign->libraries[library].needed ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_NONE;
    }
    // A shared library's libraries and versions are a dynamic PIE's, without the interpreter.
    if (result.error == FOREIGN_LINK_ERROR_NONE && (kind == RUNTIME_KIND_DYNAMIC || kind == RUNTIME_KIND_SHARED))
    {
        result.error = foreign_link_libraries(output, foreign, arena, kind == RUNTIME_KIND_DYNAMIC ? build->interpreter : (String8){ 0 });
    }
    build->scratch = scratch_begin(arena);
    uint32_t* order = result.error == FOREIGN_LINK_ERROR_NONE ? foreign_link_order(foreign, build->scratch.arena) : 0;
    build->order = order;
    build->bounds = (ForeignLinkBounds){ 0 };
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result.error = order != 0 ? foreign_link_bounds_plan(foreign, order, &build->bounds, build->scratch.arena) : FOREIGN_LINK_ERROR_MEMORY;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && !foreign_link_capacity(foreign, build->bounds.group_count, build->bounds.relocations, &capacity))
    {
        result.error = FOREIGN_LINK_ERROR_CAPACITY;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        output->names = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, foreign->names.count);
        output->objects = BUSTER_ARENA_PUSH_ARRAY(arena, ImportObject, foreign->object_count);
        const bool allocated = order != 0 && output->names != 0 && output->objects != 0 && link_initialize(&output->link, arena, build->name_arena, capacity);
        result.error = allocated ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
        if (allocated)
        {
            memset(output->names, 0, (uint64_t)foreign->names.count * sizeof(SymbolId));
            memset(output->objects, 0, (uint64_t)foreign->object_count * sizeof(ImportObject));
        }
    }
    for (uint32_t at = 0; result.error == FOREIGN_LINK_ERROR_NONE && at < foreign->object_count; at += 1)
    {
        const uint32_t id = order[at];
        const ForeignObject* input = &foreign->objects[id];
        if (id != 0 && input->library == 0)
        {
            result.import = foreign_link_sections(output, input, id, foreign->path, arena);
            const uint32_t stack_section = output->objects[id].executable_stack_section;
            if (result.import.error == IMPORT_ERROR_NONE && stack_section != 0)
            {
                // The writer always emits an NX stack. A selected object's explicit request survives removal of
                // its code. Conservatively include a marker itself discarded by COMDAT or SHF_EXCLUDE as well;
                // that edge is our refusal policy, not a claim that every other linker makes the same choice.
                const Elf64_Shdr header = import_section_header(&input->elf, stack_section);
                result.import = import_fail(IMPORT_ERROR_UNSUPPORTED, input->elf.section_offset + (uint64_t)stack_section * sizeof(Elf64_Shdr));
                result.import.section = stack_section;
                result.import.type = header.sh_type;
            }
            if (result.import.error == IMPORT_ERROR_NONE)
            {
                result.import = foreign_link_locals(output, input, id, foreign->path, arena);
            }
            result.error = result.import.error != IMPORT_ERROR_NONE ? FOREIGN_LINK_ERROR_IMPORT : FOREIGN_LINK_ERROR_NONE;
            result.object = result.error != FOREIGN_LINK_ERROR_NONE ? id : 0;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_link_prepare_unwind(output, foreign, arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        build->required = foreign_link_undefined(output, foreign, kind, build->scratch.arena);
        result.error = build->required != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
    }
    build->result = result;
}

// Object `id`'s map of its global symbols to the link's. `first_typed`, with a slot per symbol, takes the lanes' ids of
// the objects whose references give an undefined symbol a type (its least, by an atomic minimum), for the lane path;
// without it, the first such object gives the symbol its type at once, as the exact path does in id order.
static void foreign_link_map_object(ForeignLink* output, const Foreign* foreign, uint32_t id, uint32_t* first_typed)
{
    const ForeignObject* input = &foreign->objects[id];
    if (input->library == 0)
    {
        const ForeignRound* round = input->global_count != 0 ? foreign_symbol_round(foreign, input->global_begin) : 0;
        for (uint32_t i = 0; i < input->global_count; i += 1)
        {
            const uint32_t symbol = input->global_begin + i;
            const uint32_t at = symbol - round->first_symbol;
            const uint32_t name = round->symbols.name_id[at];
            const SymbolId target = output->names[name];
            output->objects[id].symbols[input->first_global + i] = target;
            // An undefined weak reference keeps its declared type, notably STT_TLS. The resolver has no winner
            // for it; unlike a definition, its type must come from a referring object's symbol record.
            const uint8_t type = ELF64_ST_TYPE(round->symbols.info[at]);
            if (target != 0 && type != 0 && output->link.symbols.source[target] == BUSTER_LINK_SOURCE_NONE &&
                (output->link.symbols.flags[target] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_NOTYPE)
            {
                if (first_typed != 0)
                {
                    __atomic_fetch_min(&first_typed[target], id, __ATOMIC_RELAXED);
                }
                else
                {
                    output->link.symbols.flags[target] |= type;
                }
            }
        }
    }
}

// The steps between the global names and the relocations: the COMDAT unwind plans' symbols, and the __start_ and
// __stop_ sections merged. Each object's map of its global symbols comes before (foreign_link_map_object).
static void foreign_link_build_middle(ForeignLinkBuild* build)
{
    ForeignLink* output = build->output;
    const Foreign* foreign = build->foreign;
    ForeignLinkResult result = build->result;
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_link_finish_unwind(output, foreign);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && build->bounds.group_count != 0)
    {
        result = foreign_link_bounds_merge(output, &build->bounds, build->arena);
    }
    build->result = result;
}

// The name id (from 1) of `name` among the inputs' names, or 0.
static uint32_t foreign_link_name_id(const Foreign* foreign, String8 name)
{
    const uint32_t length = (uint32_t)name.length;
    return name.length != 0 && name.length <= UINT32_MAX ? foreign_find_name(foreign, (const uint8_t*)name.pointer, length, link_hash((const uint8_t*)name.pointer, length)) : 0;
}

// Whether the object the link takes name `definer`'s definition from (name ids from 1) refers to name `referenced`: an
// undefined symbol of that name among its global ones.
static bool foreign_link_refers(const Foreign* foreign, uint32_t definer, uint32_t referenced)
{
    const uint8_t resolution = definer != 0 ? foreign->names.resolution[definer - 1] : FOREIGN_RESOLUTION_UNDEFINED;
    const bool by_object = referenced != 0 && (resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_WEAK);
    const ForeignObject* object = by_object ? &foreign->objects[foreign_symbol_object(foreign, foreign->names.symbol[definer - 1])] : 0;
    bool result = false;
    for (uint32_t s = 0; object != 0 && !result && s < object->global_count; s += 1)
    {
        const uint32_t symbol = object->global_begin + s;
        const ForeignRound* round = foreign_symbol_round(foreign, symbol);
        const uint32_t at = symbol - round->first_symbol;
        result = round->symbols.name_id[at] + 1 == referenced && round->symbols.section[at] == SHN_UNDEF;
    }
    return result;
}

// Whether the object the link takes name `definer`'s definition from (a name id from 1) was read from `file`: an object
// file of that identity, or a member of an archive of that identity (the archive's input, even for a thin archive's),
// as the resolution opened it (ForeignInput.identity).
static bool foreign_link_defined_in(const Foreign* foreign, uint32_t definer, ForeignFileIdentity file)
{
    const uint8_t resolution = definer != 0 ? foreign->names.resolution[definer - 1] : FOREIGN_RESOLUTION_UNDEFINED;
    const bool by_object = file.inode != 0 && (resolution == FOREIGN_RESOLUTION_STRONG || resolution == FOREIGN_RESOLUTION_WEAK);
    const ForeignObject* object = by_object ? &foreign->objects[foreign_symbol_object(foreign, foreign->names.symbol[definer - 1])] : 0;
    const ForeignInput* input = object != 0 && object->input < foreign->input_count ? &foreign->inputs[object->input] : 0;
    return input != 0 && input->identity.inode != 0 && input->identity.device == file.device && input->identity.inode == file.inode;
}

// Whether no input defines name `id` (from 1), which the linker then defines; false for a name no input has.
static bool foreign_link_left_to_linker(const Foreign* foreign, uint32_t id)
{
    const uint8_t resolution = id != 0 ? foreign->names.resolution[id - 1] : FOREIGN_RESOLUTION_STRONG;
    return resolution == FOREIGN_RESOLUTION_UNDEFINED || resolution == FOREIGN_RESOLUTION_WEAK_UNDEFINED;
}

// Whether a static link's startup applies its IRELATIVE relocations, which the kernel doesn't: the caller says the
// output's own does (link-foreign's --startup-applies-irelative), or it is glibc's, recognized by the files its code
// comes from, not by names, which any input may mention without running what they name (#405's reviews). The entry must
// be defined by the system's start file (crt1.o, or rcrt1.o for a static PIE: the first library directory's, where
// GCC's driver finds it, runtime_library_identity), whose _start calls __libc_start_main. That must be defined by a
// member of the system's libc.a (found the same way, so that one only in GCC's installation isn't recognized, which
// can only refuse), whose libc-start.o applies a static executable's relocations between __rela_iplt_start and
// __rela_iplt_end, and for a static PIE calls _dl_relocate_static_pie, from libc.a too, which applies the image's
// relocations through _DYNAMIC; glibc 2.44's also calls _dl_relocate_static_pie_ifunc for the IRELATIVE ones, which
// must then come from libc.a as well, not be an empty one like crt1.o's. Each step refers to the next name, as glibc's
// files do, and the names whose places the startup reads must be left to the linker, which defines them where the
// relocations are. Another C library's startup isn't these files (musl's applies no IRELATIVE relocation), nor is a
// copy of them elsewhere: such a link needs the caller's word. The inputs' identities are the files the resolution
// read, and only a link with an IFUNC that a fixup reaches asks (foreign_link_build_end): a few stats, and none for a
// static link without one.
static bool foreign_link_startup_applies(const Foreign* foreign, RuntimeKind kind, ForeignLinkOptions options)
{
    ForeignFileIdentity start_file = { 0 };
    ForeignFileIdentity libc_archive = { 0 };
    if (!options.applies_irelative)
    {
        runtime_library_identity(runtime_object_names[kind][RUNTIME_OBJECT_START], &start_file.device, &start_file.inode);
        runtime_library_identity("libc.a", &libc_archive.device, &libc_archive.inode);
    }
    const uint32_t start = foreign_link_name_id(foreign, options.entry);
    const uint32_t main_start = foreign_link_name_id(foreign, foreign_string("__libc_start_main"));
    const uint32_t relocator = foreign_link_name_id(foreign, foreign_string("_dl_relocate_static_pie"));
    const uint32_t ifunc_relocator = foreign_link_name_id(foreign, foreign_string("_dl_relocate_static_pie_ifunc"));
    const uint32_t first = foreign_link_name_id(foreign, foreign_string("__rela_iplt_start"));
    const uint32_t last = foreign_link_name_id(foreign, foreign_string("__rela_iplt_end"));
    const uint32_t dynamic = foreign_link_name_id(foreign, foreign_string("_DYNAMIC"));
    const bool entered = foreign_link_defined_in(foreign, start, start_file) && foreign_link_refers(foreign, start, main_start) &&
                         foreign_link_defined_in(foreign, main_start, libc_archive);
    const bool applies = kind == RUNTIME_KIND_STATIC ? foreign_link_refers(foreign, main_start, first) && foreign_link_left_to_linker(foreign, first) &&
                                                           foreign_link_left_to_linker(foreign, last) :
                                                       foreign_link_refers(foreign, main_start, relocator) && foreign_link_defined_in(foreign, relocator, libc_archive) &&
                                                           foreign_link_refers(foreign, relocator, dynamic) && foreign_link_left_to_linker(foreign, dynamic) &&
                                                           (!foreign_link_refers(foreign, main_start, ifunc_relocator) || foreign_link_defined_in(foreign, ifunc_relocator, libc_archive));
    return options.applies_irelative || (entered && applies);
}

// The steps after the relocations but the check: the __start_ and __stop_ sections' relocations, a dynamic PIE's copies,
// the dynamic images' _DYNAMIC, and the refusal of the IFUNCs a fixup reaches that the output can't take
// (foreign_link_refuse_ifuncs): a shared library's that the loader can't bind, and every one of a static link whose
// startup applies no IRELATIVE relocation (foreign_link_startup_applies).
static void foreign_link_build_end(ForeignLinkBuild* build)
{
    ForeignLink* output = build->output;
    const Foreign* foreign = build->foreign;
    ForeignLinkResult result = build->result;
    if (result.error == FOREIGN_LINK_ERROR_NONE && build->bounds.group_count != 0)
    {
        result.import = foreign_link_bounds_relocations(output, foreign, &build->bounds, build->arena);
        result.error = result.import.error != IMPORT_ERROR_NONE ? FOREIGN_LINK_ERROR_IMPORT : FOREIGN_LINK_ERROR_NONE;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && build->kind == RUNTIME_KIND_DYNAMIC)
    {
        result = foreign_link_copies(output, foreign, build->arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && build->kind != RUNTIME_KIND_STATIC)
    {
        result.error = foreign_link_dynamic_symbol(output, foreign);
    }
    const bool static_kind = runtime_kind_is_static(build->kind);
    if (result.error == FOREIGN_LINK_ERROR_NONE && (build->kind == RUNTIME_KIND_SHARED || static_kind))
    {
        result = foreign_link_refuse_ifuncs(output, foreign, build->arena, static_kind);
    }
    // A static link's IFUNCs stand when its startup applies their IRELATIVE relocations, which only a link with one
    // reached asks (from the resolution's names and objects: _DYNAMIC's own symbol, added above, isn't one).
    if (result.error == FOREIGN_LINK_ERROR_IFUNC && static_kind && foreign_link_startup_applies(foreign, build->kind, build->options))
    {
        result = (ForeignLinkResult){ 0 };
    }
    build->result = result;
}

// A static executable's and a static PIE's ExecutableRuntime, and a static PIE's ExecutableDynamic, as dynamic.c's
// dynamic_static_inputs and dynamic_static_pie_inputs give the direct path's: the start file's ABI note, the link's
// __tls_get_addr (foreign_link_relaxed_name) and for a static PIE, _init and _fini for DT_INIT and DT_FINI, without
// RELR, as lld writes -static-pie without -z pack-relative-relocs (libc.a's startup applies .rela.dyn whatever its
// release).
static void foreign_link_static_finish(ForeignLink* output, const Foreign* foreign, RuntimeKind kind)
{
    const String8 note = foreign_link_abi_note(foreign);
    output->runtime = (ExecutableRuntime){
        .note = note,
        .tls_get_addr = foreign_link_symbol(output, foreign, "__tls_get_addr"),
    };
    if (kind == RUNTIME_KIND_STATIC_PIE)
    {
        output->dynamic = (ExecutableDynamic){
            .note = note,
            .init = foreign_link_named(output, foreign, "_init"),
            .fini = foreign_link_named(output, foreign, "_fini"),
        };
    }
}

// A shared library's ExecutableDynamic, as lld's -shared writes one without a version script: its libraries and
// versions (foreign_link_libraries's), every global, weak and unique definition of default or protected visibility
// exported, in an atom or absolute, in name order (the writer orders .dynsym's exports by .gnu.hash's buckets), at the
// library's own base version, and _init and _fini for DT_INIT and DT_FINI. Its soname is the caller's.
static ForeignLinkError foreign_link_shared_finish(ForeignLink* output, const Foreign* foreign, Arena* arena)
{
    const LinkSymbols* symbols = &output->link.symbols;
    SymbolId* exports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, (uint64_t)foreign->names.count + 1);
    uint32_t count = 0;
    for (uint32_t n = 0; exports != 0 && n < foreign->names.count; n += 1)
    {
        const SymbolId symbol = output->names[n];
        const uint8_t flags = symbols->flags[symbol];
        const uint8_t visibility = flags & BUSTER_LINK_SYMBOL_VISIBILITY;
        const bool defined = symbol != 0 && (symbols->atom[symbol] != 0 || symbols->source[symbol] == BUSTER_LINK_SOURCE_ABSOLUTE);
        const bool global = (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL;
        if (defined && global && (visibility == BUSTER_LINK_SYMBOL_DEFAULT || visibility == BUSTER_LINK_SYMBOL_PROTECTED))
        {
            exports[count] = symbol;
            count += 1;
        }
    }
    output->dynamic.exports = exports;
    output->dynamic.export_count = count;
    output->dynamic.init = foreign_link_named(output, foreign, "_init");
    output->dynamic.fini = foreign_link_named(output, foreign, "_fini");
    output->dynamic.shared = true;
    return exports != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
}

#include "buster/compiler/foreign_debug.c"

// The step after the check: a dynamic PIE's exports, a shared library's or a static executable's inputs for the writer.
static void foreign_link_build_finish(ForeignLinkBuild* build)
{
    if (build->result.error == FOREIGN_LINK_ERROR_NONE && build->kind == RUNTIME_KIND_DYNAMIC)
    {
        build->result.error = foreign_link_dynamic_finish(build->output, build->foreign, build->arena);
    }
    else if (build->result.error == FOREIGN_LINK_ERROR_NONE && build->kind == RUNTIME_KIND_SHARED)
    {
        build->result.error = foreign_link_shared_finish(build->output, build->foreign, build->arena);
    }
    else if (build->result.error == FOREIGN_LINK_ERROR_NONE)
    {
        foreign_link_static_finish(build->output, build->foreign, build->kind);
    }
}

// The last step: the debug sections, unless the output leaves them out; and the scratch given back.
static void foreign_link_build_debug(ForeignLinkBuild* build)
{
    if (build->result.error == FOREIGN_LINK_ERROR_NONE && !build->options.strip_debug)
    {
        build->result = foreign_debug_build(build->output, build->foreign, build->order, build->arena);
    }
    scratch_end(build->scratch);
}

#include "buster/compiler/foreign_link_lanes.c"

static ForeignLinkResult foreign_link_build_mode(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter, bool defer_undefined,
                                                 ForeignLinkOptions options)
{
    ForeignLinkBuild build = {
        .output = output,
        .foreign = foreign,
        .arena = arena,
        .name_arena = name_arena,
        .kind = kind,
        .interpreter = interpreter,
        .defer_undefined = defer_undefined,
        .options = options,
        .start = foreign_now(),
    };
    if (foreign->path == FOREIGN_PATH_VECTOR && foreign->lanes > 1)
    {
        foreign_link_build_lanes(&build, foreign->lanes);
    }
    else
    {
        foreign_link_build_begin(&build);
        build.ends[FOREIGN_LINK_PHASE_SECTIONS] = foreign_now();
        if (build.result.error == FOREIGN_LINK_ERROR_NONE)
        {
            build.result = foreign_link_names(output, foreign, build.required, &build.bounds, kind, defer_undefined);
        }
        build.ends[FOREIGN_LINK_PHASE_NAMES] = foreign_now();
        for (uint32_t id = 1; build.result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
        {
            foreign_link_map_object(output, foreign, id, 0);
        }
        build.ends[FOREIGN_LINK_PHASE_MAPS] = foreign_now();
        foreign_link_build_middle(&build);
        build.ends[FOREIGN_LINK_PHASE_UNWIND] = foreign_now();
        for (uint32_t id = 1; build.result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
        {
            if (foreign->objects[id].library == 0)
            {
                build.result.import = foreign_link_relocations(output, foreign, &build.bounds, id, foreign->path, arena);
                build.result.error = build.result.import.error != IMPORT_ERROR_NONE ? FOREIGN_LINK_ERROR_IMPORT : FOREIGN_LINK_ERROR_NONE;
                build.result.object = build.result.error != FOREIGN_LINK_ERROR_NONE ? id : 0;
            }
        }
        build.ends[FOREIGN_LINK_PHASE_RELOCATIONS] = foreign_now();
        foreign_link_build_end(&build);
        build.ends[FOREIGN_LINK_PHASE_COPIES] = foreign_now();
        if (build.result.error == FOREIGN_LINK_ERROR_NONE && link_check(&output->link) != LINK_ERROR_NONE)
        {
            build.result.error = FOREIGN_LINK_ERROR_LINK;
        }
        build.ends[FOREIGN_LINK_PHASE_CHECK] = foreign_now();
        foreign_link_build_finish(&build);
        build.ends[FOREIGN_LINK_PHASE_EXPORTS] = foreign_now();
        foreign_link_build_debug(&build);
        build.ends[FOREIGN_LINK_PHASE_DEBUG] = foreign_now();
    }
    uint64_t previous = build.start;
    for (uint32_t phase = 0; phase < FOREIGN_LINK_PHASES; phase += 1)
    {
        const uint64_t end = build.ends[phase] > previous ? build.ends[phase] : previous;
        output->phase_nanoseconds[phase] = end - previous;
        previous = end;
    }
    return build.result;
}

// A static executable whose entry is _start.
static ForeignLinkResult foreign_link_build(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena)
{
    const ForeignLinkOptions options = { .entry = foreign_string("_start") };
    return foreign_link_build_mode(output, foreign, arena, name_arena, RUNTIME_KIND_STATIC, (String8){ 0 }, false, options);
}

static ForeignLinkResult foreign_link_build_dynamic(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, String8 interpreter)
{
    return foreign_link_build_mode(output, foreign, arena, name_arena, RUNTIME_KIND_DYNAMIC, interpreter, false, (ForeignLinkOptions){ 0 });
}

static ForeignLinkResult foreign_link_build_kind(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter,
                                                 ForeignLinkOptions options)
{
    return foreign_link_build_mode(output, foreign, arena, name_arena, kind, interpreter, false, options);
}

// Reachability, including FDE/CIE dependencies, decides which unresolved references remain errors. Preserve those
// names as GLOBAL/NONE until foreign_gc_link diagnoses the surviving closure; ordinary producers remain eager.
static ForeignLinkResult foreign_link_build_for_gc(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter,
                                                   ForeignLinkOptions options)
{
    return foreign_link_build_mode(output, foreign, arena, name_arena, kind, interpreter, true, options);
}
