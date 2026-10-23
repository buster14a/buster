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
// including partial output on failure. This initial interface produces
// static inputs; needed shared libraries are refused until their versions and DT_NEEDED have an output handoff.
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
struct ForeignLink
{
    Link link;
    // Canonical symbol by ForeignNames index, and input-index maps by ForeignObject index.
    SymbolId* names;
    ImportObject* objects;
};

static __attribute__((unused)) ForeignLinkResult foreign_link_build(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena);

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
    for (uint32_t first = 0; first < foreign->names.count; first += 64)
    {
        const uint32_t count = foreign->names.count - first < 64 ? foreign->names.count - first : 64;
        if (foreign->path == FOREIGN_PATH_VECTOR)
        {
            const __mmask64 mask = _bzhi_u64(~0ull, count);
            const __m512i resolutions = _mm512_maskz_loadu_epi8(mask, foreign->names.resolution + first);
            capacity->atoms += (uint32_t)__builtin_popcountll(_mm512_mask_cmpeq_epi8_mask(mask, resolutions, _mm512_set1_epi8(FOREIGN_RESOLUTION_COMMON)));
        }
        else
        {
            for (uint32_t i = 0; i < count; i += 1)
            {
                capacity->atoms += foreign->names.resolution[first + i] == FOREIGN_RESOLUTION_COMMON ? 1 : 0;
            }
        }
    }
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

// A symbol table alone cannot tell whether an undefined reference survived COMDAT selection. Only the surviving
// sections' relocations require a definition, together with explicit -u roots. The extra scan is needed only when
// resolution left an ordinary strong name undefined; successful ordinary links avoid it entirely.
static uint8_t* foreign_link_undefined(const ForeignLink* output, const Foreign* foreign, Arena* arena)
{
    uint8_t* required = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, foreign->names.count);
    bool unresolved = false;
    for (uint32_t name = 0; required != 0 && name < foreign->names.count; name += 1)
    {
        required[name] = 0;
        unresolved = unresolved || (foreign->names.resolution[name] == FOREIGN_RESOLUTION_UNDEFINED &&
                                    !runtime_linker_name(RUNTIME_KIND_STATIC, foreign->names.bytes[name], foreign->names.length[name]));
    }
    for (uint32_t id = 0; required != 0 && unresolved && id < foreign->object_count; id += 1)
    {
        const ForeignObject* input = &foreign->objects[id];
        const ForeignRound* round = input->global_count != 0 ? foreign_symbol_round(foreign, input->global_begin) : 0;
        if (id == 0 || input->library != 0)
        {
            for (uint32_t i = 0; i < input->global_count; i += 1)
            {
                const uint32_t at = input->global_begin - round->first_symbol + i;
                required[round->symbols.name_id[at]] = 1;
            }
        }
        else
        {
            for (uint32_t r = 0; r < input->relocation_count; r += 1)
            {
                const Elf64_Shdr header = import_section_header(&input->elf, input->relocations[r]);
                if (output->objects[id].section_atoms[header.sh_info] != 0)
                {
                    const String8 bytes = import_section_bytes(&input->elf, &header);
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

static ForeignLinkResult foreign_link_names(ForeignLink* output, const Foreign* foreign, const uint8_t* required)
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
            // The resolver ranks GNU_UNIQUE with weak definitions; the final executable has one canonical definition.
            if (ELF64_ST_BIND(symbol.st_info) == STB_GNU_UNIQUE)
            {
                symbol.st_info = ELF64_ST_INFO(STB_WEAK, ELF64_ST_TYPE(symbol.st_info));
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
            if (!import_symbol_flags(&symbol, &flags))
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
            result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
        }
        else if (runtime_linker_name(RUNTIME_KIND_STATIC, names->bytes[name], names->length[name]))
        {
            source = BUSTER_LINK_SOURCE_LINKER;
        }
        else if (resolution == FOREIGN_RESOLUTION_UNDEFINED && required[name] != 0)
        {
            result.error = FOREIGN_LINK_ERROR_UNDEFINED;
        }
        else
        {
            flags = BUSTER_LINK_SYMBOL_WEAK;
        }
        flags = (uint8_t)((flags & ~BUSTER_LINK_SYMBOL_VISIBILITY) | names->visibility[name] << BUSTER_LINK_SYMBOL_VISIBILITY_SHIFT);
        const bool discarded_only = resolution == FOREIGN_RESOLUTION_UNDEFINED && source == BUSTER_LINK_SOURCE_NONE && required[name] == 0;
        if (result.error == FOREIGN_LINK_ERROR_NONE && !discarded_only)
        {
            const NameId named = link_add_name_hashed(link, names->bytes[name], names->length[name], names->hash[name]);
            output->names[name] = named != 0 ? link_add_symbol(link, named, atom, value, size, flags, source) : 0;
            result.error = output->names[name] == 0 ? FOREIGN_LINK_ERROR_LINK : FOREIGN_LINK_ERROR_NONE;
        }
        if (result.error != FOREIGN_LINK_ERROR_NONE)
        {
            result.name = name + 1;
            result.object = object_id;
        }
    }
    return result;
}

static ImportResult foreign_link_relocations(ForeignLink* output, const ForeignObject* input, uint32_t id, ForeignPath path, Arena* arena)
{
    Link* link = &output->link;
    ImportObject* object = &output->objects[id];
    ImportResult result = { 0 };
    const __m512i kinds = import_kind_table();
    for (uint32_t i = 0; result.error == IMPORT_ERROR_NONE && i < input->relocation_count; i += 1)
    {
        const uint32_t section = input->relocations[i];
        Scratch scratch = scratch_begin(arena);
        if (path == FOREIGN_PATH_EXACT || !import_vector_relocations(link, &input->elf, &input->symbols, section, object, kinds, scratch.arena))
        {
            result = import_relocations(link, &input->elf, &input->symbols, section, object, scratch.arena);
        }
        scratch_end(scratch);
    }
    return result;
}

static ForeignLinkResult foreign_link_build(ForeignLink* output, const Foreign* foreign, Arena* arena, Arena* name_arena)
{
    *output = (ForeignLink){ 0 };
    ForeignLinkResult result = { 0 };
    LinkCapacity capacity = { 0 };
    if (!foreign->started || foreign->failure.error != FOREIGN_ERROR_NONE)
    {
        result.error = FOREIGN_LINK_ERROR_INPUT;
    }
    for (uint32_t library = 0; result.error == FOREIGN_LINK_ERROR_NONE && library < foreign->library_count; library += 1)
    {
        result.error = foreign->libraries[library].needed ? FOREIGN_LINK_ERROR_UNSUPPORTED : FOREIGN_LINK_ERROR_NONE;
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
        const uint8_t* required = foreign_link_undefined(output, foreign, scratch.arena);
        if (required != 0)
        {
            result = foreign_link_names(output, foreign, required);
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
            result.import = foreign_link_relocations(output, input, id, foreign->path, arena);
            result.error = result.import.error != IMPORT_ERROR_NONE ? FOREIGN_LINK_ERROR_IMPORT : FOREIGN_LINK_ERROR_NONE;
            result.object = result.error != FOREIGN_LINK_ERROR_NONE ? id : 0;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && link_check(&output->link) != LINK_ERROR_NONE)
    {
        result.error = FOREIGN_LINK_ERROR_LINK;
    }
    scratch_end(scratch);
    return result;
}
