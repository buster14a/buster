// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Foreign GC's producer/consumer adapter. Include after foreign_link.c, foreign_unwind.c and foreign_gc.c.
// foreign_gc_link builds a new compact ForeignLink without modifying input, Foreign, or their section bytes. Names
// and all retained non-zero-fill bytes are copied, so patching the result cannot damage either input. Library/version
// strings and metadata remain borrowed from input and Foreign, which must outlive the result. The two output arenas
// must also outlive it; failure restores their lengths and leaves output unchanged. Input/output must be distinct and all argument
// pointers non-null. The input is an unlaid-out conversion belonging to this Foreign. Use foreign_link_build_for_gc
// when dead strong references need deferred diagnostics; validated ordinary conversion is also accepted.
//
// Unwind graph: each current EH record becomes a temporary atom owning that record's fixups. Original code -> FDE
// and FDE -> CIE edges are added to the marker's CSR dependencies. CIE personality and FDE LSDA references therefore
// participate in the same closure as ordinary code. The original EH atom is a non-root placeholder: an explicit
// reference to it keeps its entire record graph conservatively. Automatic roots also include dynamic exports/init/
// fini, command-line -u, and SHF_GNU_RETAIN. Init/fini fragments and arrays remain the marker's implicit roots.
//
// After closure, foreign_unwind_prune replays the original ELF records with dead code sections removed. It rewrites
// CIE distances and relocation offsets, using the same validated encodings as the COMDAT producer. Current-offset
// maps compose that result with prior COMDAT compaction. References into removed records fail; nonzero fixup addends
// into a changed EH section remain unsupported because their architectural bias has not been recovered. Record-only
// terminators survive without rooting code. Unreferenced unsupported EH formats fail rather than lose unwind data.
//
// Layout: roots, original/current unwind plans, virtual graph, final record plans, compact Link and external maps.

typedef struct ForeignGcFrame ForeignGcFrame;
struct ForeignGcFrame
{
    uint32_t object;
    uint32_t section;
    AtomId atom;
    AtomId first_node;
    ForeignUnwindInput source;
    // Original-object coordinates, from COMDAT pruning when already present, otherwise parsed here.
    const ForeignUnwind* original;
    ForeignUnwind parsed;
    // Current input Link coordinates; index maps these records back to original->records.
    ForeignUnwind current;
    uint32_t* index;
    // The final plan is allocated in the output arena and keeps original-object coordinates.
    ForeignLinkUnwind* final;
};

typedef struct ForeignGcLinkState ForeignGcLinkState;
struct ForeignGcLinkState
{
    ForeignGcFrame** frames;
    ForeignGcRoots roots;
    ForeignGcEdges edges;
    Link graph;
    ForeignGc marked;
    AtomId* atoms;
    SymbolId* symbols;
    uint64_t* values;
    uint64_t* sizes;
    LinkCapacity capacity;
};

static __attribute__((unused)) ForeignLinkResult foreign_gc_link(ForeignLink* output, const Foreign* foreign, const ForeignLink* input,
                                                                const ForeignGcRoots* roots, ForeignPath path, Arena* arena, Arena* name_arena);

static ForeignLinkResult foreign_gc_link_failure(ForeignLinkError error, uint32_t object, uint32_t section)
{
    return (ForeignLinkResult){
        .error = error,
        .object = object,
        .import = {
            .section = section,
        },
    };
}

static ForeignLinkResult foreign_gc_link_roots(ForeignGcLinkState* state, const Foreign* foreign, const ForeignLink* input, const ForeignGcRoots* roots, Arena* arena)
{
    const uint32_t forced = foreign->object_count != 0 ? foreign->objects[0].global_count : 0;
    const uint64_t exports = (uint64_t)roots->export_count + input->dynamic.export_count + forced + 2;
    const uint64_t retained = (uint64_t)roots->retained_count + input->link.atoms.count;
    ForeignLinkResult result = { 0 };
    if (exports >= UINT32_MAX || retained >= UINT32_MAX || (roots->export_count != 0 && roots->exports == 0) ||
        (roots->retained_count != 0 && roots->retained == 0) || (input->dynamic.export_count != 0 && input->dynamic.exports == 0))
    {
        result.error = FOREIGN_LINK_ERROR_INPUT;
    }
    SymbolId* symbols = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, exports) : 0;
    AtomId* atoms = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, retained) : 0;
    if (result.error == FOREIGN_LINK_ERROR_NONE && (symbols == 0 || atoms == 0))
    {
        result.error = FOREIGN_LINK_ERROR_MEMORY;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        state->roots = *roots;
        state->roots.exports = symbols;
        state->roots.retained = atoms;
        for (uint32_t i = 0; i < roots->export_count; i += 1)
        {
            symbols[i] = roots->exports[i];
        }
        for (uint32_t i = 0; i < roots->retained_count; i += 1)
        {
            atoms[i] = roots->retained[i];
            if (atoms[i] == 0 || atoms[i] >= input->link.atoms.count)
            {
                // Virtual EH nodes belong only to this adapter; caller IDs always refer to the input Link.
                result.error = FOREIGN_LINK_ERROR_INPUT;
            }
        }
        for (uint32_t i = 0; i < input->dynamic.export_count; i += 1)
        {
            symbols[state->roots.export_count] = input->dynamic.exports[i];
            state->roots.export_count += 1;
        }
        if (input->dynamic.init != 0)
        {
            symbols[state->roots.export_count] = input->dynamic.init;
            state->roots.export_count += 1;
        }
        if (input->dynamic.fini != 0)
        {
            symbols[state->roots.export_count] = input->dynamic.fini;
            state->roots.export_count += 1;
        }
        const ForeignObject* command = foreign->object_count != 0 ? &foreign->objects[0] : 0;
        for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < forced; i += 1)
        {
            const uint32_t id = command->global_begin + i;
            const ForeignRound* round = foreign_symbol_round(foreign, id);
            const uint32_t name = round->symbols.name_id[id - round->first_symbol];
            if (name >= foreign->names.count || input->names[name] == 0)
            {
                result.error = FOREIGN_LINK_ERROR_UNDEFINED;
                result.name = name + 1;
            }
            else
            {
                symbols[state->roots.export_count] = input->names[name];
                state->roots.export_count += 1;
            }
        }
        for (uint32_t id = 1; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
        {
            const ForeignObject* object = &foreign->objects[id];
            const ImportObject* imported = &input->objects[id];
            for (uint32_t section = 1; object->library == 0 && section < imported->section_count; section += 1)
            {
                const AtomId atom = imported->section_atoms[section];
                const Elf64_Shdr header = import_section_header(&object->elf, section);
                if (atom != 0 && (header.sh_flags & SHF_GNU_RETAIN) != 0)
                {
                    atoms[state->roots.retained_count] = atom;
                    state->roots.retained_count += 1;
                }
            }
        }
    }
    return result;
}

static ForeignLinkResult foreign_gc_link_frame(ForeignGcFrame* frame, const Foreign* foreign, const ForeignLink* input, ForeignPath path, Arena* arena)
{
    const ForeignObject* object = &foreign->objects[frame->object];
    const ImportObject* imported = &input->objects[frame->object];
    const Elf64_Shdr section = import_section_header(&object->elf, frame->section);
    frame->source = (ForeignUnwindInput){
        .bytes = import_section_bytes(&object->elf, &section),
        .symbols = &object->symbols,
        .sections = imported->section_atoms,
        .section_count = imported->section_count,
    };
    ForeignLinkResult result = { 0 };
    for (uint32_t i = 0; i < object->relocation_count; i += 1)
    {
        const Elf64_Shdr header = import_section_header(&object->elf, object->relocations[i]);
        if (header.sh_info == frame->section)
        {
            const String8 bytes = import_section_bytes(&object->elf, &header);
            frame->source.relocations = (const Elf64_Rela*)bytes.pointer;
            frame->source.relocation_count = (uint32_t)(bytes.length / sizeof(Elf64_Rela));
        }
    }
    const ForeignLinkUnwind* previous = foreign_link_unwind(input, frame->object, frame->section);
    if (previous != 0)
    {
        frame->original = &previous->plan;
    }
    else
    {
        const ForeignUnwindResult parsed = foreign_unwind_prune(&frame->source, path, arena, &frame->parsed);
        if (parsed.error != FOREIGN_UNWIND_ERROR_NONE)
        {
            result = foreign_gc_link_failure(parsed.error == FOREIGN_UNWIND_ERROR_MEMORY ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_UNSUPPORTED,
                                             frame->object, frame->section);
            result.import.offset = parsed.offset;
        }
        frame->original = &frame->parsed;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        frame->current.records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindRecord, frame->original->record_count);
        frame->index = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, frame->original->record_count);
        result.error = frame->current.records != 0 && frame->index != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY;
        frame->current.old_length = input->link.atoms.size[frame->atom];
        frame->current.bytes = (String8){
            .pointer = (char*)input->link.atoms.bytes[frame->atom],
            .length = frame->current.old_length,
        };
    }
    for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && i < frame->original->record_count; i += 1)
    {
        const ForeignUnwindRecord* source = &frame->original->records[i];
        if (previous == 0 || source->new_offset != UINT64_MAX)
        {
            const uint32_t at = frame->current.record_count;
            ForeignUnwindRecord record = *source;
            record.old_offset = previous != 0 ? source->new_offset : source->old_offset;
            record.new_offset = UINT64_MAX;
            if (previous != 0 && record.kind == FOREIGN_UNWIND_FDE && !foreign_unwind_offset(frame->original, source->cie_offset, &record.cie_offset))
            {
                result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_INPUT, frame->object, frame->section);
            }
            frame->current.records[at] = record;
            frame->index[at] = i;
            frame->current.record_count += 1;
        }
    }
    return result;
}

static ForeignLinkResult foreign_gc_link_frames(ForeignGcLinkState* state, const Foreign* foreign, const ForeignLink* input, ForeignPath path, Arena* arena)
{
    const Link* link = &input->link;
    state->frames = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignGcFrame*, link->atoms.count);
    ForeignLinkResult result = { .error = state->frames != 0 ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    if (state->frames != 0)
    {
        memset(state->frames, 0, (uint64_t)link->atoms.count * sizeof(ForeignGcFrame*));
    }
    uint64_t nodes = link->atoms.count;
    for (uint32_t id = 1; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        const ImportObject* imported = &input->objects[id];
        for (uint32_t section = 1; result.error == FOREIGN_LINK_ERROR_NONE && object->library == 0 && section < imported->section_count; section += 1)
        {
            const AtomId atom = imported->section_atoms[section];
            if (atom != 0 && link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
            {
                ForeignGcFrame* frame = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignGcFrame, 1);
                if (frame == 0)
                {
                    result.error = FOREIGN_LINK_ERROR_MEMORY;
                }
                else
                {
                    *frame = (ForeignGcFrame){
                        .object = id,
                        .section = section,
                        .atom = atom,
                        .first_node = (AtomId)nodes,
                    };
                    result = foreign_gc_link_frame(frame, foreign, input, path, arena);
                    state->frames[atom] = frame;
                    nodes += frame->current.record_count;
                    if (nodes >= UINT32_MAX)
                    {
                        result.error = FOREIGN_LINK_ERROR_CAPACITY;
                    }
                }
            }
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < link->atoms.count; atom += 1)
    {
        if (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME && state->frames[atom] == 0)
        {
            result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
        }
    }
    state->graph = *link;
    state->graph.atoms.count = (uint32_t)nodes;
    state->graph.atoms.capacity = (uint32_t)nodes;
    return result;
}

// Only the atom columns and fixup offsets change in this temporary graph; the other immutable Link columns are
// borrowed. No consumer except foreign_gc_mark_edges reads it, and its graph checks validate every virtual range.
static ForeignLinkResult foreign_gc_link_graph(ForeignGcLinkState* state, const ForeignLink* input, Arena* arena)
{
    const Link* link = &input->link;
    Link* graph = &state->graph;
    LinkAtoms* atoms = &graph->atoms;
    atoms->class = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, atoms->count);
    atoms->size = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, atoms->count);
    atoms->bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t*, atoms->count);
    atoms->fixup_begin = BUSTER_ARENA_PUSH_ARRAY(arena, FixupId, atoms->count);
    atoms->fixup_count = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, atoms->count);
    graph->fixups.offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, link->fixups.count);
    uint32_t* offsets = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)atoms->count + 1);
    const bool allocated = atoms->class != 0 && atoms->size != 0 && atoms->bytes != 0 && atoms->fixup_begin != 0 &&
                           atoms->fixup_count != 0 && graph->fixups.offset != 0 && offsets != 0;
    ForeignLinkResult result = { .error = allocated ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    if (allocated)
    {
        memcpy(atoms->class, link->atoms.class, link->atoms.count);
        memcpy(atoms->size, link->atoms.size, (uint64_t)link->atoms.count * sizeof(uint64_t));
        memcpy(atoms->bytes, link->atoms.bytes, (uint64_t)link->atoms.count * sizeof(uint8_t*));
        memcpy(atoms->fixup_begin, link->atoms.fixup_begin, (uint64_t)link->atoms.count * sizeof(FixupId));
        memcpy(atoms->fixup_count, link->atoms.fixup_count, (uint64_t)link->atoms.count * sizeof(uint32_t));
        memcpy(graph->fixups.offset, link->fixups.offset, (uint64_t)link->fixups.count * sizeof(uint32_t));
        memset(offsets, 0, ((uint64_t)atoms->count + 1) * sizeof(uint32_t));
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < link->atoms.count; atom += 1)
    {
        const ForeignGcFrame* frame = state->frames[atom];
        if (frame != 0)
        {
            atoms->class[atom] = BUSTER_LINK_CLASS_RODATA;
            atoms->size[atom] = 0;
            atoms->bytes[atom] = 0;
            atoms->fixup_begin[atom] = 0;
            atoms->fixup_count[atom] = 0;
            const FixupId first = link->atoms.fixup_begin[atom];
            const FixupId end = first + link->atoms.fixup_count[atom];
            FixupId fixup = first;
            for (uint32_t r = 0; r < frame->current.record_count; r += 1)
            {
                const ForeignUnwindRecord* record = &frame->current.records[r];
                const AtomId node = frame->first_node + r;
                atoms->class[node] = BUSTER_LINK_CLASS_RODATA;
                atoms->size[node] = record->length;
                atoms->bytes[node] = link->atoms.bytes[atom] + record->old_offset;
                atoms->fixup_begin[node] = 0;
                atoms->fixup_count[node] = 0;
                while (fixup < end && link->fixups.offset[fixup] < record->old_offset + record->length)
                {
                    if (link->fixups.offset[fixup] < record->old_offset)
                    {
                        result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_INPUT, frame->object, frame->section);
                    }
                    if (atoms->fixup_count[node] == 0)
                    {
                        atoms->fixup_begin[node] = fixup;
                    }
                    graph->fixups.offset[fixup] = (uint32_t)(link->fixups.offset[fixup] - record->old_offset);
                    atoms->fixup_count[node] += 1;
                    fixup += 1;
                }
                offsets[(uint64_t)atom + 1] += 1;
                if (record->kind == FOREIGN_UNWIND_FDE)
                {
                    if (record->atom == 0 || record->atom >= link->atoms.count)
                    {
                        result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_INPUT, frame->object, frame->section);
                    }
                    else
                    {
                        offsets[(uint64_t)record->atom + 1] += 1;
                        offsets[(uint64_t)node + 1] += 1;
                    }
                }
            }
            if (fixup != end)
            {
                result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_INPUT, frame->object, frame->section);
            }
        }
    }
    uint64_t edges = 0;
    for (uint32_t atom = 0; result.error == FOREIGN_LINK_ERROR_NONE && atom < atoms->count; atom += 1)
    {
        edges += offsets[(uint64_t)atom + 1];
        if (edges >= UINT32_MAX)
        {
            result.error = FOREIGN_LINK_ERROR_CAPACITY;
        }
        else
        {
            offsets[(uint64_t)atom + 1] = (uint32_t)edges;
        }
    }
    AtomId* targets = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, edges) : 0;
    uint32_t* cursors = result.error == FOREIGN_LINK_ERROR_NONE ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, atoms->count) : 0;
    if (result.error == FOREIGN_LINK_ERROR_NONE && (targets == 0 || cursors == 0))
    {
        result.error = FOREIGN_LINK_ERROR_MEMORY;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        memcpy(cursors, offsets, (uint64_t)atoms->count * sizeof(uint32_t));
        state->edges = (ForeignGcEdges){
            .offsets = offsets,
            .targets = targets,
            .count = (uint32_t)edges,
        };
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < link->atoms.count; atom += 1)
    {
        const ForeignGcFrame* frame = state->frames[atom];
        for (uint32_t r = 0; frame != 0 && r < frame->current.record_count; r += 1)
        {
            const ForeignUnwindRecord* record = &frame->current.records[r];
            const AtomId node = frame->first_node + r;
            targets[cursors[atom]] = node;
            cursors[atom] += 1;
            if (record->kind == FOREIGN_UNWIND_FDE)
            {
                const uint32_t cie = foreign_unwind_record_at(&frame->current, record->cie_offset);
                if (cie >= frame->current.record_count || frame->current.records[cie].kind != FOREIGN_UNWIND_CIE ||
                    frame->current.records[cie].old_offset != record->cie_offset)
                {
                    result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_INPUT, frame->object, frame->section);
                }
                else
                {
                    targets[cursors[record->atom]] = node;
                    cursors[record->atom] += 1;
                    targets[cursors[node]] = frame->first_node + cie;
                    cursors[node] += 1;
                }
            }
        }
    }
    return result;
}

static ForeignLinkResult foreign_gc_link_plans(ForeignGcLinkState* state, const ForeignLink* input, ForeignPath path, Arena* arena, Arena* scratch)
{
    ForeignLinkResult result = { 0 };
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < input->link.atoms.count; atom += 1)
    {
        ForeignGcFrame* frame = state->frames[atom];
        if (frame != 0)
        {
            const ImportObject* object = &input->objects[frame->object];
            AtomId* sections = BUSTER_ARENA_PUSH_ARRAY(scratch, AtomId, object->section_count);
            frame->final = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkUnwind, 1);
            if (sections == 0 || frame->final == 0)
            {
                result.error = FOREIGN_LINK_ERROR_MEMORY;
            }
            else
            {
                for (uint32_t section = 0; section < object->section_count; section += 1)
                {
                    const AtomId target = object->section_atoms[section];
                    sections[section] = target != 0 && state->marked.atoms[target] != 0 ? target : 0;
                }
                *frame->final = (ForeignLinkUnwind){ .section = frame->section };
                ForeignUnwindInput source = frame->source;
                source.sections = sections;
                const ForeignUnwindResult pruned = foreign_unwind_prune(&source, path, arena, &frame->final->plan);
                if (pruned.error != FOREIGN_UNWIND_ERROR_NONE)
                {
                    result = foreign_gc_link_failure(pruned.error == FOREIGN_UNWIND_ERROR_MEMORY ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_UNSUPPORTED,
                                                     frame->object, frame->section);
                    result.import.offset = pruned.offset;
                }
            }
            for (uint32_t r = 0; result.error == FOREIGN_LINK_ERROR_NONE && r < frame->current.record_count; r += 1)
            {
                ForeignUnwindRecord* record = &frame->current.records[r];
                const ForeignUnwindRecord* final = &frame->final->plan.records[frame->index[r]];
                record->new_offset = final->new_offset;
                const bool marked = state->marked.atoms[frame->first_node + r] != 0;
                if (marked && record->kind != FOREIGN_UNWIND_TERMINATOR && final->new_offset == UINT64_MAX)
                {
                    result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_UNSUPPORTED, frame->object, frame->section);
                    result.import.offset = record->old_offset;
                }
            }
            if (result.error == FOREIGN_LINK_ERROR_NONE)
            {
                frame->current.bytes = frame->final->plan.bytes;
            }
        }
    }
    return result;
}

static ForeignLinkResult foreign_gc_link_maps(ForeignGcLinkState* state, const Foreign* foreign, const ForeignLink* input, Arena* arena)
{
    const Link* link = &input->link;
    state->atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, link->atoms.count);
    state->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, link->symbols.count);
    state->values = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, link->symbols.count);
    state->sizes = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, link->symbols.count);
    uint8_t* section_symbols = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, link->symbols.count);
    ForeignLinkResult result = { .error = state->atoms != 0 && state->symbols != 0 && state->values != 0 && state->sizes != 0 && section_symbols != 0 ?
                                        FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        memset(state->atoms, 0, (uint64_t)link->atoms.count * sizeof(AtomId));
        memset(state->symbols, 0, (uint64_t)link->symbols.count * sizeof(SymbolId));
        memcpy(state->values, link->symbols.value, (uint64_t)link->symbols.count * sizeof(uint64_t));
        memcpy(state->sizes, link->symbols.size, (uint64_t)link->symbols.count * sizeof(uint64_t));
        memset(section_symbols, 0, link->symbols.count);
        state->capacity.names = link->names.count - 1;
    }
    for (uint32_t id = 1; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ForeignObject* object = &foreign->objects[id];
        const ImportObject* imported = &input->objects[id];
        for (uint32_t i = 1; object->library == 0 && i < object->first_global; i += 1)
        {
            const SymbolId symbol = imported->symbols[i];
            const Elf64_Sym original = import_symbol(&object->symbols, i);
            if (symbol != 0 && ELF64_ST_TYPE(original.st_info) == STT_SECTION)
            {
                section_symbols[symbol] = 1;
            }
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < link->atoms.count; atom += 1)
    {
        const ForeignGcFrame* frame = state->frames[atom];
        const bool kept = frame != 0 ? frame->final->plan.bytes.length != 0 || state->marked.atoms[atom] != 0 : state->marked.atoms[atom] != 0;
        if (kept)
        {
            state->capacity.atoms += 1;
            state->atoms[atom] = state->capacity.atoms;
            const FixupId begin = link->atoms.fixup_begin[atom];
            for (uint32_t i = 0; i < link->atoms.fixup_count[atom]; i += 1)
            {
                const FixupId fixup = begin + i;
                if (state->marked.fixups[fixup] != 0)
                {
                    state->capacity.fixups += 1;
                    state->capacity.wide_addends += (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0 ? 1 : 0;
                }
            }
        }
    }
    for (SymbolId symbol = 1; result.error == FOREIGN_LINK_ERROR_NONE && symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        const ForeignGcFrame* frame = atom != 0 ? state->frames[atom] : 0;
        bool kept = atom != 0 ? state->atoms[atom] != 0 : state->marked.symbols[symbol] != 0;
        if (kept && frame != 0)
        {
            uint64_t value = 0;
            uint64_t end = 0;
            uint64_t old_end = 0;
            const bool section = section_symbols[symbol] != 0;
            const bool fits = !__builtin_add_overflow(link->symbols.value[symbol], link->symbols.size[symbol], &old_end);
            kept = section || (fits && foreign_unwind_offset(&frame->current, link->symbols.value[symbol], &value) &&
                               foreign_unwind_offset(&frame->current, old_end - (link->symbols.size[symbol] != 0 ? 1 : 0), &end) && end >= value);
            if (!kept && state->marked.symbols[symbol] != 0)
            {
                result = foreign_gc_link_failure(FOREIGN_LINK_ERROR_UNSUPPORTED, frame->object, frame->section);
                result.import.offset = link->symbols.value[symbol];
            }
            state->values[symbol] = value;
            state->sizes[symbol] = section ? 0 : kept ? end - value + (link->symbols.size[symbol] != 0 ? 1 : 0) : 0;
        }
        if (kept)
        {
            const uint8_t binding = link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING;
            const bool shared_undefined = input->dynamic.shared && (link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_VISIBILITY) == BUSTER_LINK_SYMBOL_DEFAULT;
            // A static link's __tls_get_addr stays undefined for the writer's TLS relaxation (ExecutableRuntime.tls_get_addr).
            const bool relaxed = symbol == input->runtime.tls_get_addr;
            if (link->symbols.source[symbol] == BUSTER_LINK_SOURCE_NONE && binding != BUSTER_LINK_SYMBOL_WEAK && !shared_undefined && !relaxed)
            {
                result.error = FOREIGN_LINK_ERROR_UNDEFINED;
                for (uint32_t name = 0; name < foreign->names.count; name += 1)
                {
                    if (input->names[name] == symbol)
                    {
                        result.name = name + 1;
                    }
                }
            }
            state->capacity.symbols += 1;
            state->symbols[symbol] = state->capacity.symbols;
        }
    }
    for (uint32_t i = 1; result.error == FOREIGN_LINK_ERROR_NONE && i < link->priorities.count; i += 1)
    {
        state->capacity.priorities += state->atoms[link->priorities.atom[i]] != 0 ? 1 : 0;
    }
    return result;
}

static ForeignLinkResult foreign_gc_link_copy(ForeignLink* output, const ForeignGcLinkState* state, const ForeignLink* input, Arena* arena, Arena* name_arena)
{
    const Link* original = &input->link;
    Link* link = &output->link;
    ForeignLinkResult result = { .error = link_initialize(link, arena, name_arena, state->capacity) ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    for (NameId name = 1; result.error == FOREIGN_LINK_ERROR_NONE && name < original->names.count; name += 1)
    {
        if (link_add_name_hashed(link, original->names.bytes[name], original->names.length[name], original->names.hash[name]) != name)
        {
            result.error = FOREIGN_LINK_ERROR_LINK;
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < original->atoms.count; atom += 1)
    {
        if (state->atoms[atom] != 0)
        {
            const ForeignGcFrame* frame = state->frames[atom];
            const uint64_t size = frame != 0 ? frame->final->plan.bytes.length : original->atoms.size[atom];
            uint8_t* bytes = frame != 0 ? (uint8_t*)frame->final->plan.bytes.pointer : 0;
            if (frame == 0 && !link_class_is_zero_fill(original->atoms.class[atom]))
            {
                bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, size);
                if (bytes == 0)
                {
                    result.error = FOREIGN_LINK_ERROR_MEMORY;
                }
                else if (size != 0)
                {
                    memcpy(bytes, original->atoms.bytes[atom], size);
                }
            }
            if (result.error == FOREIGN_LINK_ERROR_NONE && link_add_atom(link, original->atoms.class[atom], original->atoms.alignment_log2[atom],
                                                                        size, bytes, original->atoms.unit[atom]) != state->atoms[atom])
            {
                result.error = FOREIGN_LINK_ERROR_LINK;
            }
        }
    }
    for (SymbolId symbol = 1; result.error == FOREIGN_LINK_ERROR_NONE && symbol < original->symbols.count; symbol += 1)
    {
        if (state->symbols[symbol] != 0)
        {
            const SymbolId mapped = link_add_symbol(link, original->symbols.name[symbol], state->atoms[original->symbols.atom[symbol]], state->values[symbol],
                                                   state->sizes[symbol], original->symbols.flags[symbol], original->symbols.source[symbol]);
            if (mapped != state->symbols[symbol])
            {
                result.error = FOREIGN_LINK_ERROR_LINK;
            }
            else
            {
                link->symbols.version[mapped] = original->symbols.version[symbol];
            }
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < original->atoms.count; atom += 1)
    {
        const FixupId begin = original->atoms.fixup_begin[atom];
        const ForeignGcFrame* frame = state->frames[atom];
        for (uint32_t i = 0; result.error == FOREIGN_LINK_ERROR_NONE && state->atoms[atom] != 0 && i < original->atoms.fixup_count[atom]; i += 1)
        {
            const FixupId fixup = begin + i;
            if (state->marked.fixups[fixup] != 0)
            {
                uint64_t offset = original->fixups.offset[fixup];
                const SymbolId symbol = original->fixups.target[fixup];
                const AtomId target = original->symbols.atom[symbol];
                const ForeignGcFrame* target_frame = target != 0 ? state->frames[target] : 0;
                const int64_t addend = link_fixup_addend(original, fixup);
                const bool changed = target_frame != 0 && target_frame->current.bytes.length != target_frame->current.old_length;
                if (state->symbols[symbol] == 0 || (frame != 0 && !foreign_unwind_offset(&frame->current, offset, &offset)) || (changed && addend != 0))
                {
                    result.error = FOREIGN_LINK_ERROR_UNSUPPORTED;
                    result.import.offset = original->fixups.offset[fixup];
                }
                else if (link_add_fixup(link, state->atoms[atom], (uint32_t)offset, original->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND,
                                        state->symbols[symbol], addend) == 0)
                {
                    result.error = FOREIGN_LINK_ERROR_LINK;
                }
            }
        }
    }
    for (uint32_t i = 1; result.error == FOREIGN_LINK_ERROR_NONE && i < original->priorities.count; i += 1)
    {
        const AtomId atom = state->atoms[original->priorities.atom[i]];
        if (atom != 0 && !link_add_priority(link, atom, original->priorities.priority[i]))
        {
            result.error = FOREIGN_LINK_ERROR_LINK;
        }
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE && link_check(link) != LINK_ERROR_NONE)
    {
        result.error = FOREIGN_LINK_ERROR_LINK;
    }
    return result;
}

static ForeignLinkResult foreign_gc_link_external(ForeignLink* output, const ForeignGcLinkState* state, const Foreign* foreign, const ForeignLink* input, Arena* arena)
{
    output->names = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, foreign->names.count);
    output->objects = BUSTER_ARENA_PUSH_ARRAY(arena, ImportObject, foreign->object_count);
    output->unwind = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignLinkUnwind*, foreign->object_count);
    SymbolId* exports = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, input->dynamic.export_count);
    SymbolId* copies = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, input->dynamic.copy_count);
    ForeignLinkResult result = { .error = output->names != 0 && output->objects != 0 && output->unwind != 0 && exports != 0 && copies != 0 ?
                                        FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_MEMORY };
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        memset(output->unwind, 0, (uint64_t)foreign->object_count * sizeof(ForeignLinkUnwind*));
        for (uint32_t name = 0; name < foreign->names.count; name += 1)
        {
            output->names[name] = state->symbols[input->names[name]];
        }
        output->dynamic = input->dynamic;
        output->dynamic.exports = exports;
        for (uint32_t i = 0; i < input->dynamic.export_count; i += 1)
        {
            exports[i] = state->symbols[input->dynamic.exports[i]];
        }
        // The copies are exports, roots that stay; export_versions keeps the exports' order.
        output->dynamic.copies = copies;
        for (uint32_t i = 0; i < input->dynamic.copy_count; i += 1)
        {
            copies[i] = state->symbols[input->dynamic.copies[i]];
        }
        output->dynamic.init = state->symbols[input->dynamic.init];
        output->dynamic.fini = state->symbols[input->dynamic.fini];
        output->runtime = input->runtime;
        output->runtime.tls_get_addr = state->symbols[input->runtime.tls_get_addr];
        output->library_versions = input->library_versions;
        // The debug sections' bytes are shared; their references move to the live symbols, and one to a symbol that
        // went gets the writer's tombstone (target 0).
        output->debug = input->debug;
        ExecutableDebugRelocation* relocations = input->debug.relocation_count != 0 ?
                                                     BUSTER_ARENA_PUSH_ARRAY(arena, ExecutableDebugRelocation, input->debug.relocation_count) : 0;
        result.error = input->debug.relocation_count != 0 && relocations == 0 ? FOREIGN_LINK_ERROR_MEMORY : result.error;
        for (uint64_t r = 0; relocations != 0 && r < input->debug.relocation_count; r += 1)
        {
            relocations[r] = input->debug.relocations[r];
            relocations[r].target = state->symbols[input->debug.relocations[r].target];
        }
        output->debug.relocations = relocations;
    }
    for (uint32_t id = 0; result.error == FOREIGN_LINK_ERROR_NONE && id < foreign->object_count; id += 1)
    {
        const ImportObject* original = &input->objects[id];
        ImportObject* object = &output->objects[id];
        *object = *original;
        object->section_atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, original->section_count);
        object->symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, original->symbol_count);
        if (object->section_atoms == 0 || object->symbols == 0)
        {
            result.error = FOREIGN_LINK_ERROR_MEMORY;
        }
        else
        {
            for (uint32_t section = 0; section < original->section_count; section += 1)
            {
                object->section_atoms[section] = state->atoms[original->section_atoms[section]];
            }
            for (uint32_t symbol = 0; symbol < original->symbol_count; symbol += 1)
            {
                object->symbols[symbol] = state->symbols[original->symbols[symbol]];
            }
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_LINK_ERROR_NONE && atom < input->link.atoms.count; atom += 1)
    {
        const ForeignGcFrame* frame = state->frames[atom];
        if (frame != 0 && state->atoms[atom] != 0)
        {
            for (uint32_t r = 0; r < frame->final->plan.record_count; r += 1)
            {
                ForeignUnwindRecord* record = &frame->final->plan.records[r];
                record->atom = state->atoms[record->atom];
            }
            frame->final->next = output->unwind[frame->object];
            output->unwind[frame->object] = frame->final;
        }
    }
    return result;
}

static ForeignLinkResult foreign_gc_link(ForeignLink* output, const Foreign* foreign, const ForeignLink* input,
                                        const ForeignGcRoots* roots, ForeignPath path, Arena* arena, Arena* name_arena)
{
    const bool arguments = output != 0 && foreign != 0 && input != 0 && roots != 0 && arena != 0 && name_arena != 0 && output != input &&
                           (path == FOREIGN_PATH_EXACT || path == FOREIGN_PATH_VECTOR);
    const uint64_t saved = arena != 0 ? arena->length : 0;
    const uint64_t saved_names = name_arena != 0 ? name_arena->length : 0;
    ForeignLinkResult result = { .error = arguments ? FOREIGN_LINK_ERROR_NONE : FOREIGN_LINK_ERROR_INPUT };
    if (result.error == FOREIGN_LINK_ERROR_NONE && (!foreign->started || foreign->failure.error != FOREIGN_ERROR_NONE || input->names == 0 || input->objects == 0 ||
                                                  input->link.error != LINK_ERROR_NONE || input->link.atoms.count == 0 || input->link.symbols.count == 0))
    {
        result.error = FOREIGN_LINK_ERROR_INPUT;
    }
    Scratch scratch = result.error == FOREIGN_LINK_ERROR_NONE ? scratch_begin(arena) : (Scratch){ 0 };
    if (result.error == FOREIGN_LINK_ERROR_NONE && (scratch.arena == name_arena || scratch.arena->base == 0))
    {
        result.error = FOREIGN_LINK_ERROR_MEMORY;
    }
    ForeignGcLinkState state = { 0 };
    ForeignLink built = { 0 };
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_roots(&state, foreign, input, roots, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_frames(&state, foreign, input, path, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_graph(&state, input, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        const ForeignGcError marked = foreign_gc_mark_edges(&state.graph, &state.roots, &state.edges, path, scratch.arena, &state.marked);
        result.error = marked == FOREIGN_GC_ERROR_NONE ? FOREIGN_LINK_ERROR_NONE : marked == FOREIGN_GC_ERROR_MEMORY ? FOREIGN_LINK_ERROR_MEMORY : FOREIGN_LINK_ERROR_INPUT;
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_plans(&state, input, path, arena, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_maps(&state, foreign, input, scratch.arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_copy(&built, &state, input, arena, name_arena);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        result = foreign_gc_link_external(&built, &state, foreign, input, arena);
    }
    if (scratch.arena != 0)
    {
        scratch_end(scratch);
    }
    if (result.error == FOREIGN_LINK_ERROR_NONE)
    {
        *output = built;
    }
    else
    {
        if (arena != 0)
        {
            arena_pop_to(arena, saved);
        }
        if (name_arena != 0 && name_arena != arena)
        {
            arena_pop_to(name_arena, saved_names);
        }
    }
    return result;
}
