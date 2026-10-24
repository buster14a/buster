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
// including partial output on failure. foreign_link_build produces static inputs; foreign_link_build_dynamic also
// publishes the resolver's selected shared libraries, globally numbered versions and required executable exports.
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

typedef struct ForeignLink ForeignLink;
typedef struct ForeignLinkUnwind ForeignLinkUnwind;
struct ForeignLink
{
    Link link;
    // Canonical symbol by ForeignNames index, and input-index maps by ForeignObject index.
    SymbolId* names;
    ImportObject* objects;
    ExecutableDynamic dynamic;
    // Original library version index -> output version index, one table per ForeignLibrary; null for dropped DSOs.
    uint16_t** library_versions;
    ForeignLinkUnwind** unwind;
    // The copy relocations' definitions (foreign_link_copies): every symbol defined at a copy, its aliases among them,
    // each exported at its version; and per copy the one symbol its R_X86_64_COPY names.
    SymbolId* copy_exports;
    uint32_t copy_export_count;
    SymbolId* copies;
    uint32_t copy_count;
};

// The most aliases (a library's other names at a copy's address that the link has no symbol of) a copy may add: glibc's
// have at most three names (environ, _environ and __environ).
#define BUSTER_FOREIGN_LINK_COPY_ALIASES 4

static __attribute__((unused)) ForeignLinkResult foreign_link_build(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena);
static __attribute__((unused)) ForeignLinkResult foreign_link_build_dynamic(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, String8 interpreter);
static __attribute__((unused)) ForeignLinkResult foreign_link_build_for_gc(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter);

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
static bool foreign_link_capacity(const Foreign* foreign, LinkCapacity* capacity)
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
    capacity->atoms = capacity->atoms + shared < UINT32_MAX ? capacity->atoms + (uint32_t)shared : UINT32_MAX;
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

// The names the linker defines in a foreign executable of `kind`: lld's for any executable, which in a PIE are the
// static PIE runtime's set (the image's and the sections' bounds, _DYNAMIC) with the dynamic runtime's.
static bool foreign_link_linker_name(RuntimeKind kind, const uint8_t* bytes, uint32_t length)
{
    return runtime_linker_name(kind, bytes, length) || (kind == RUNTIME_KIND_DYNAMIC && runtime_linker_name(RUNTIME_KIND_STATIC_PIE, bytes, length));
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
                                    !foreign_link_linker_name(kind, foreign->names.bytes[name], foreign->names.length[name])) || unneeded_shared;
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

static ForeignLinkResult foreign_link_names(ForeignLink* output, const Foreign* foreign, const uint8_t* required, RuntimeKind kind, bool defer_undefined)
{
    ForeignLinkResult result = { 0 };
    Link* link = &output->link;
    const ForeignNames* names = &foreign->names;
    for (uint32_t name = 0; result.error == FOREIGN_LINK_ERROR_NONE && name < names->count; name += 1)
    {
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
        if (names->definitions[name] > 1)
        {
            result.error = FOREIGN_LINK_ERROR_DUPLICATE;
        }
        else if (defined)
        {
            const uint32_t winner = names->symbol[name];
            object_id = foreign_symbol_object(foreign, winner);
            const ForeignObject* object = &foreign->objects[object_id];
            Elf64_Sym symbol = import_symbol(&object->symbols, foreign_symbol_index(foreign, winner));
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
                const uint8_t class = ELF64_ST_TYPE(symbol.st_info) == STT_TLS ? BUSTER_LINK_CLASS_TBSS : BUSTER_LINK_CLASS_BSS;
                atom = link_add_atom(link, class, names->common_alignment[name], size, 0, object_id);
            }
            else if (section == BUSTER_FOREIGN_SECTION_ABS)
            {
                source = BUSTER_LINK_SOURCE_ABSOLUTE;
            }
            else if (section < output->objects[object_id].section_count)
            {
                atom = output->objects[object_id].section_atoms[section];
            }
            if (result.error == FOREIGN_LINK_ERROR_NONE && source == BUSTER_LINK_SOURCE_FOREIGN_OBJECT && atom == 0)
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
            else if (kind == RUNTIME_KIND_STATIC)
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
        else if (resolution == FOREIGN_RESOLUTION_UNDEFINED && required[name] != 0 && (names->flags[name] & BUSTER_FOREIGN_NAME_REGULAR_REFERENCED) != 0)
        {
            result.error = defer_undefined ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_UNDEFINED;
        }
        else
        {
            flags = BUSTER_LINK_SYMBOL_WEAK;
        }
        const uint8_t visibility = linker ? foreign_link_linker_visibility(names->bytes[name], names->length[name]) : names->visibility[name];
        flags = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | visibility << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT);
        // Nor does a name nothing defines that only libraries refer to (libstdc++.so.6's weak _ITM_ references).
        const bool regular = (names->flags[name] & (BUSTER_FOREIGN_NAME_REGULAR_REFERENCED | BUSTER_FOREIGN_NAME_REGULAR_WEAK_REFERENCED)) != 0;
        const bool unreferenced = !defined && source == BUSTER_LINK_SOURCE_NONE && !regular;
        const bool discarded_only = discarded_shared || unreferenced || (resolution == FOREIGN_RESOLUTION_UNDEFINED && source == BUSTER_LINK_SOURCE_NONE && required[name] == 0);
        if (result.error == FOREIGN_LINK_ERROR_NONE && !discarded_only)
        {
            const NameId named = link_add_name_hashed(link, names->bytes[name], names->length[name], names->hash[name]);
            output->names[name] = named != 0 ? link_add_symbol(link, named, atom, value, size, flags, source) : 0;
            result.error = output->names[name] == 0 ? FOREIGN_LINK_ERROR_LINK : FOREIGN_LINK_ERROR_NONE;
            link->symbols.version[output->names[name]] = version;
        }
        if (result.error != FOREIGN_LINK_ERROR_NONE)
        {
            result.name = name + 1;
            result.object = object_id;
        }
    }
    return result;
}

static ImportResult foreign_link_relocations(ForeignLink* output, const Foreign* foreign, uint32_t id, ForeignPath path, Arena* arena)
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
        const AtomId atom = object->section_atoms[header.sh_info];
        const String8 entries = foreign_link_relocation_bytes(output, input, id, &header);
        result = atom != 0 ? foreign_link_unwind_references(output, foreign, id, section, entries) : (ImportResult){ 0 };
        Scratch scratch = scratch_begin(arena);
        if (result.error == IMPORT_ERROR_NONE && foreign_link_unwind(output, id, header.sh_info) != 0)
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

// The exports at `value`, eight addresses at a time: their places into `found` (at most `capacity`); their count.
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
            if (foreign_link_alias(exports, e, value) && count < capacity)
            {
                found[count] = e;
                count += 1;
            }
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
    output->copy_exports = marked != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, marked * (1 + BUSTER_FOREIGN_LINK_COPY_ALIASES)) : 0;
    output->copies = marked != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, marked) : 0;
    result.error = marked != 0 && (copies == 0 || output->copy_exports == 0 || output->copies == 0) ? FOREIGN_LINK_ERROR_MEMORY : result.error;
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
    // Each copy's atom, as large as its largest name, then its names defined there: the library's exports at its address,
    // the link's imports of them and new symbols for the others.
    for (uint32_t c = 0; result.error == FOREIGN_LINK_ERROR_NONE && c < copy_count; c += 1)
    {
        ForeignLinkCopy* copy = &copies[c];
        const ForeignLibrary* library = &foreign->libraries[copy->library];
        const ImportExports* exports = &library->library.exports;
        uint32_t aliases[2 * BUSTER_FOREIGN_LINK_COPY_ALIASES + 1];
        const uint32_t alias_count = foreign_link_aliases(exports, copy->value, aliases, 2 * BUSTER_FOREIGN_LINK_COPY_ALIASES + 1);
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
            if (symbol != 0)
            {
                output->copy_exports[output->copy_export_count] = symbol;
                output->copy_export_count += 1;
            }
        }
        // The copied import itself, if its library's export table didn't list it at its address (its default version
        // is another than the one it binds to).
        if (result.error == FOREIGN_LINK_ERROR_NONE && link->symbols.source[copy->symbol] == BUSTER_LINK_SOURCE_FOREIGN_SHARED)
        {
            const bool defined = foreign_link_define_copy(link, copy->symbol, copy->atom, link->symbols.size[copy->symbol], link->symbols.version[copy->symbol]);
            result.error = defined ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_LINK;
            output->copy_exports[output->copy_export_count] = copy->symbol;
            output->copy_export_count += defined ? 1 : 0;
        }
        output->copies[c] = copy->symbol;
    }
    output->copy_count = result.error == FOREIGN_LINK_ERROR_NONE ? copy_count : 0;
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
        for (uint32_t id = 1; id < foreign->object_count && output->dynamic.note.length == 0; id += 1)
        {
            if (foreign->objects[id].library == 0)
            {
                output->dynamic.note = runtime_section(foreign->objects[id].bytes, ".note.ABI-tag");
            }
        }
    }
    return result;
}

static ForeignLinkResult foreign_link_build_mode(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter, bool defer_undefined)
{
    *output = (ForeignLink){ 0 };
    ForeignLinkResult result = { 0 };
    LinkCapacity capacity = { 0 };
    if (!foreign->started || foreign->failure.error != FOREIGN_ERROR_NONE || (kind != RUNTIME_KIND_STATIC && kind != RUNTIME_KIND_DYNAMIC) ||
        (kind == RUNTIME_KIND_DYNAMIC && (interpreter.pointer == 0 || interpreter.length == 0)))
    {
        result.error = FOREIGN_LINK_ERROR_INPUT;
    }
    for (uint32_t library = 0; result.error == FOREIGN_LINK_ERROR_NONE && kind == RUNTIME_KIND_STATIC && library < foreign->library_count; library += 1)
    {
        result.error = foreign->libraries[library].needed ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_NONE;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && kind == RUNTIME_KIND_DYNAMIC)
    {
        result.error = foreign_link_libraries(output, foreign, arena, interpreter);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && !foreign_link_capacity(foreign, &capacity))
    {
        result.error = FOREIGN_LINK_ERROR_CAPACITY;
    }
    Scratch scratch = scratch_begin(arena);
    uint32_t* order = result.error == FOREIGN_LINK_ERROR_NONE ? foreign_link_order(foreign, scratch.arena) : 0;
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        output->names = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, foreign->names.count);
        output->objects = BUSTER_ARENA_PUSH_ARRAY(arena, ImportObject, foreign->object_count);
        const bool allocated = order != 0 && output->names != 0 && output->objects != 0 && link_initialize(&output->link, arena, name_arena, capacity);
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
        const uint8_t* required = foreign_link_undefined(output, foreign, kind, scratch.arena);
        if (required != 0)
        {
            result = foreign_link_names(output, foreign, required, kind, defer_undefined);
        }
        else
        {
            result.error = FOREIGN_LINK_ERROR_MEMORY;
        }
    }
    for (uint32_t id = 1; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
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
                if (target != 0 && output->link.symbols.source[target] == BUSTER_LINK_SOURCE_NONE && (output->link.symbols.flags[target] & BUSTER_LINK_SYMBOL_TYPE) == BUSTER_LINK_SYMBOL_NOTYPE)
                {
                    output->link.symbols.flags[target] |= ELF64_ST_TYPE(round->symbols.info[at]);
                }
            }
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_link_finish_unwind(output, foreign);
    }
    for (uint32_t id = 1; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        if (foreign->objects[id].library == 0)
        {
            result.import = foreign_link_relocations(output, foreign, id, foreign->path, arena);
            result.error = result.import.error != IMPORT_ERROR_NONE ? FOREIGN_LINK_ERROR_IMPORT : FOREIGN_LINK_ERROR_NONE;
            result.object = result.error != FOREIGN_LINK_ERROR_NONE ? id : 0;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && kind == RUNTIME_KIND_DYNAMIC)
    {
        result = foreign_link_copies(output, foreign, arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && kind == RUNTIME_KIND_DYNAMIC)
    {
        result.error = foreign_link_dynamic_symbol(output, foreign);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && link_check(&output->link) != LINK_ERROR_NONE)
    {
        result.error = FOREIGN_LINK_ERROR_LINK;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && kind == RUNTIME_KIND_DYNAMIC)
    {
        result.error = foreign_link_dynamic_finish(output, foreign, arena);
    }
    scratch_end(scratch);
    return result;
}

static ForeignLinkResult foreign_link_build(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena)
{
    return foreign_link_build_mode(output, foreign, arena, name_arena, RUNTIME_KIND_STATIC, (String8){ 0 }, false);
}

static ForeignLinkResult foreign_link_build_dynamic(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, String8 interpreter)
{
    return foreign_link_build_mode(output, foreign, arena, name_arena, RUNTIME_KIND_DYNAMIC, interpreter, false);
}

// Reachability, including FDE/CIE dependencies, decides which unresolved references remain errors. Preserve those
// names as GLOBAL/NONE until foreign_gc_link diagnoses the surviving closure; ordinary producers remain eager.
static ForeignLinkResult foreign_link_build_for_gc(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena, RuntimeKind kind, String8 interpreter)
{
    return foreign_link_build_mode(output, foreign, arena, name_arena, kind, interpreter, true);
}
