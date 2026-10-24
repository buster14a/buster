// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Terminal-executable CIE pooling planner. Include after foreign_unwind.c. Input frames describe the CURRENT Link
// bytes, already validated by the foreign unwind producer; they never authorize replaying an original ELF section.
// Consecutive EH atoms in emitted order may share one byte-identical CIE with identical canonical fixups. Whole
// atoms move together, FDE order stays unchanged, and terminators/anchors close a pool. A cap of one distinct CIE
// preserves the executable writer's single-CIE vector path. Non-EH atoms retain their relative order.
//
// The plan owns record maps, atom/fixup maps, and member chains, but no output bytes. Aliased CIEs have a valid mapped
// offset and kept == 0; they are NOT dead records. Each original atom has its own explicit endpoint in the pool.
// Exact/vector twins compare bounded byte and fixup batches. Ordered pool formation and variable record traversal
// are shared; no throughput claim is made until measured with the producer and writer on the approved host.
// section_symbols is a producer-validated byte per SymbolId: the Link flags erase ELF STT_SECTION provenance.
// All inputs are immutable. On failure the arena length and output are unchanged. Empty slices may have null data.

typedef struct ForeignUnwindDedupAtom ForeignUnwindDedupAtom;
struct ForeignUnwindDedupAtom
{
    ForeignUnwindRecord* records;
    uint8_t* kept;
    uint32_t record_count;
    AtomId owner;
    AtomId next;
    // Only an owner's length describes an emitted atom; endpoint belongs to this input section.
    uint64_t length;
    uint64_t endpoint;
    uint8_t alignment_log2;
    bool eligible;
    bool pooled;
};

typedef struct ForeignUnwindDedup ForeignUnwindDedup;
struct ForeignUnwindDedup
{
    ForeignUnwindDedupAtom* maps;
    AtomId* atoms;
    FixupId* fixups;
    uint32_t atom_count;
    uint32_t fixup_count;
    uint32_t removed_cies;
    uint32_t removed_atoms;
    uint64_t removed_bytes;
    LinkCapacity capacity;
};

static bool foreign_unwind_dedup_bytes(const uint8_t* left, const uint8_t* right, uint64_t length, ForeignPath path)
{
    bool equal = true;
    for (uint64_t at = 0; equal && at < length; at += 64)
    {
        const uint32_t count = (uint32_t)(length - at < 64 ? length - at : 64);
        if (path == FOREIGN_PATH_VECTOR)
        {
            const __mmask64 mask = count == 64 ? UINT64_MAX : (1ull << count) - 1;
            const __m512i a = _mm512_maskz_loadu_epi8(mask, left + at);
            const __m512i b = _mm512_maskz_loadu_epi8(mask, right + at);
            equal = (_mm512_cmpeq_epi8_mask(a, b) & mask) == mask;
        }
        else
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t i = 0; i < count; i += 1)
            {
                equal = equal && left[at + i] == right[at + i];
            }
        }
    }
    return equal;
}

static FixupId foreign_unwind_dedup_fixup_at(const Link* link, AtomId atom, uint64_t offset)
{
    const FixupId begin = link->atoms.fixup_begin[atom];
    uint32_t low = 0;
    uint32_t high = link->atoms.fixup_count[atom];
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        if (link->fixups.offset[begin + middle] < offset)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }
    return begin + low;
}

static bool foreign_unwind_dedup_identity(const Link* link, AtomId left, const ForeignUnwindRecord* a,
                                         AtomId right, const ForeignUnwindRecord* b, ForeignPath path)
{
    bool equal = a->length == b->length && foreign_unwind_dedup_bytes(link->atoms.bytes[left] + a->old_offset,
                                                                   link->atoms.bytes[right] + b->old_offset, a->length, path);
    const FixupId a_begin = foreign_unwind_dedup_fixup_at(link, left, a->old_offset);
    const FixupId b_begin = foreign_unwind_dedup_fixup_at(link, right, b->old_offset);
    const FixupId a_end = foreign_unwind_dedup_fixup_at(link, left, a->old_offset + a->length);
    const FixupId b_end = foreign_unwind_dedup_fixup_at(link, right, b->old_offset + b->length);
    equal = equal && a_end - a_begin == b_end - b_begin;
    for (uint32_t at = 0; equal && at < a_end - a_begin; at += 8)
    {
        const uint32_t count = a_end - a_begin - at < 8 ? a_end - a_begin - at : 8;
        uint64_t a_fields[8] = { 0 };
        uint64_t b_fields[8] = { 0 };
        int64_t a_addends[8] = { 0 };
        int64_t b_addends[8] = { 0 };
        uint64_t a_offsets[8] = { 0 };
        uint64_t b_offsets[8] = { 0 };
        #pragma clang loop vectorize(disable) interleave(disable)
        for (uint32_t i = 0; i < count; i += 1)
        {
            const FixupId af = a_begin + at + i;
            const FixupId bf = b_begin + at + i;
            // WIDE is storage, not relocation identity. The resolved full addend is compared separately.
            a_fields[i] = ((uint64_t)link->fixups.target[af] << 8) | (link->fixups.kind[af] & BUSTER_LINK_FIXUP_KIND);
            b_fields[i] = ((uint64_t)link->fixups.target[bf] << 8) | (link->fixups.kind[bf] & BUSTER_LINK_FIXUP_KIND);
            a_addends[i] = link_fixup_addend(link, af);
            b_addends[i] = link_fixup_addend(link, bf);
            a_offsets[i] = link->fixups.offset[af] - a->old_offset;
            b_offsets[i] = link->fixups.offset[bf] - b->old_offset;
            if (path == FOREIGN_PATH_EXACT)
            {
                equal = equal && a_fields[i] == b_fields[i] && a_addends[i] == b_addends[i] && a_offsets[i] == b_offsets[i];
            }
        }
        if (path == FOREIGN_PATH_VECTOR)
        {
            const __mmask8 fields = _mm512_cmpeq_epi64_mask(_mm512_loadu_si512(a_fields), _mm512_loadu_si512(b_fields));
            const __mmask8 addends = _mm512_cmpeq_epi64_mask(_mm512_loadu_si512(a_addends), _mm512_loadu_si512(b_addends));
            const __mmask8 offsets = _mm512_cmpeq_epi64_mask(_mm512_loadu_si512(a_offsets), _mm512_loadu_si512(b_offsets));
            equal = (fields & addends & offsets) == UINT8_MAX;
        }
    }
    return equal;
}

static bool foreign_unwind_dedup_offset(const ForeignUnwindDedupAtom* map, uint64_t old_length, uint64_t offset, uint64_t* mapped)
{
    bool valid = offset <= old_length;
    if (valid && offset == old_length)
    {
        *mapped = map->endpoint;
    }
    else if (valid && map->record_count == 0)
    {
        *mapped = offset;
    }
    else if (valid)
    {
        const ForeignUnwind records = {
            .records = map->records,
            .record_count = map->record_count,
        };
        const uint32_t index = foreign_unwind_record_at(&records, offset);
        valid = index < map->record_count && offset >= map->records[index].old_offset &&
                offset - map->records[index].old_offset < map->records[index].length;
        if (valid)
        {
            *mapped = map->records[index].new_offset + offset - map->records[index].old_offset;
        }
    }
    return valid;
}

static ForeignUnwindResult foreign_unwind_dedup_records(ForeignUnwindDedupAtom* map, const Link* link, AtomId atom,
                                                       const ForeignUnwind* frame, ForeignPath path, Arena* arena)
{
    ForeignUnwindResult result = { 0 };
    const uint64_t length = link->atoms.size[atom];
    if (frame == 0 || frame->old_length != length || frame->bytes.length != length ||
        (length != 0 && frame->bytes.pointer != (char*)link->atoms.bytes[atom]) || (frame->record_count != 0 && frame->records == 0))
    {
        result.error = FOREIGN_UNWIND_ERROR_INPUT;
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        map->records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindRecord, frame->record_count);
        map->kept = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, frame->record_count);
        map->record_count = frame->record_count;
        result.error = map->records != 0 && map->kept != 0 ? FOREIGN_UNWIND_ERROR_NONE : FOREIGN_UNWIND_ERROR_MEMORY;
    }
    uint64_t cursor = 0;
    uint32_t fdes = 0;
    map->eligible = length != 0 && length <= BUSTER_EXECUTABLE_MAX_ATOM_BYTES;
    for (uint32_t r = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && r < map->record_count; r += 1)
    {
        ForeignUnwindRecord record = frame->records[r];
        const uint64_t left = length - cursor;
        const bool fits = record.old_offset == cursor && record.length >= 4 && record.length <= left;
        const uint32_t stored = fits ? executable_load32(link->atoms.bytes[atom] + cursor) : 0;
        const uint32_t id = fits && record.length >= 8 ? executable_load32(link->atoms.bytes[atom] + cursor + 4) : 0;
        const bool terminator = record.kind == FOREIGN_UNWIND_TERMINATOR;
        if (!fits || record.kind > FOREIGN_UNWIND_TERMINATOR || (terminator ? stored != 0 || record.length != left :
            stored < 4 || stored == UINT32_MAX || (uint64_t)stored + 4 != record.length || (record.kind == FOREIGN_UNWIND_CIE) != (id == 0)))
        {
            result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, cursor, 0);
        }
        else if (record.kind == FOREIGN_UNWIND_FDE)
        {
            const uint32_t cie = foreign_unwind_record_at(frame, record.cie_offset);
            if (record.length < 17 || id > cursor + 4 || cursor + 4 - id != record.cie_offset || cie >= r ||
                frame->records[cie].kind != FOREIGN_UNWIND_CIE || frame->records[cie].old_offset != record.cie_offset)
            {
                result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, cursor, 0);
            }
            fdes += 1;
        }
        else if (record.kind == FOREIGN_UNWIND_CIE)
        {
            ForeignUnwindState state = {
                .auxiliary = UINT64_MAX,
            };
            const ForeignUnwindError parsed = foreign_unwind_cie(link->atoms.bytes[atom], &record, &state);
            if (parsed == FOREIGN_UNWIND_ERROR_MALFORMED)
            {
                result = foreign_unwind_fail(parsed, cursor, 0);
            }
            map->eligible = map->eligible && parsed == FOREIGN_UNWIND_ERROR_NONE;
            if (r != 0)
            {
                map->eligible = map->eligible && frame->records[0].kind == FOREIGN_UNWIND_CIE &&
                                foreign_unwind_dedup_identity(link, atom, &frame->records[0], atom, &record, path);
            }
        }
        else
        {
            map->eligible = false;
            for (uint64_t i = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && i < record.length; i += 1)
            {
                if (link->atoms.bytes[atom][cursor + i] != 0)
                {
                    result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, cursor + i, 0);
                }
            }
        }
        map->eligible = map->eligible && record.length % 4 == 0;
        record.new_offset = record.old_offset;
        map->records[r] = record;
        map->kept[r] = 1;
        cursor += fits ? record.length : 0;
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE && cursor != length)
    {
        result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, cursor, 0);
    }
    map->eligible = map->eligible && fdes != 0 && map->record_count != 0 && map->records[0].kind == FOREIGN_UNWIND_CIE;
    // Every fixup must remain inside one record; producer validation supplies pointer encodings and ownership.
    const FixupId begin = link->atoms.fixup_begin[atom];
    for (uint32_t i = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && i < link->atoms.fixup_count[atom]; i += 1)
    {
        const FixupId fixup = begin + i;
        const uint64_t offset = link->fixups.offset[fixup];
        const uint32_t r = foreign_unwind_record_at(frame, offset);
        const uint8_t kind = link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_KIND;
        const uint64_t width = link_fixup_kinds[kind].width;
        if (r >= map->record_count || offset < map->records[r].old_offset + 8 || map->records[r].kind == FOREIGN_UNWIND_TERMINATOR ||
            width > map->records[r].length || offset - map->records[r].old_offset > map->records[r].length - width)
        {
            result = foreign_unwind_fail(FOREIGN_UNWIND_ERROR_MALFORMED, offset, i + 1);
        }
    }
    return result;
}

static __attribute__((unused)) ForeignUnwindResult foreign_unwind_dedup_plan(const Link* link, const ForeignUnwind* const* frames,
                                                                           const uint8_t* pinned, const uint8_t* section_symbols, ForeignPath path,
                                                                           Arena* arena, ForeignUnwindDedup* output)
{
    const uint64_t saved = arena != 0 ? arena->length : 0;
    const bool arguments = link != 0 && frames != 0 && pinned != 0 && section_symbols != 0 && arena != 0 && output != 0 &&
                           (path == FOREIGN_PATH_EXACT || path == FOREIGN_PATH_VECTOR);
    ForeignUnwindResult result = { .error = arguments ? FOREIGN_UNWIND_ERROR_NONE : FOREIGN_UNWIND_ERROR_INPUT };
    Link checked = arguments ? *link : (Link){ 0 };
    if (arguments && (link->error != LINK_ERROR_NONE || link->atoms.count == 0 || link->symbols.count == 0 || link->names.count == 0 ||
                      link->fixups.count == 0 || link->priorities.count == 0 || link_check(&checked) != LINK_ERROR_NONE))
    {
        result.error = FOREIGN_UNWIND_ERROR_INPUT;
    }
    ForeignUnwindDedup built = { 0 };
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        built.atom_count = link->atoms.count;
        built.fixup_count = link->fixups.count;
        built.maps = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignUnwindDedupAtom, built.atom_count);
        built.atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, built.atom_count);
        built.fixups = BUSTER_ARENA_PUSH_ARRAY(arena, FixupId, built.fixup_count);
        result.error = built.maps != 0 && built.atoms != 0 && built.fixups != 0 ? FOREIGN_UNWIND_ERROR_NONE : FOREIGN_UNWIND_ERROR_MEMORY;
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        memset(built.maps, 0, (uint64_t)built.atom_count * sizeof(*built.maps));
        memset(built.atoms, 0, (uint64_t)built.atom_count * sizeof(*built.atoms));
        memset(built.fixups, 0, (uint64_t)built.fixup_count * sizeof(*built.fixups));
        built.capacity.names = link->names.count - 1;
        built.capacity.symbols = link->symbols.count - 1;
        built.capacity.priorities = link->priorities.count - 1;
    }
    for (AtomId atom = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && atom < built.atom_count; atom += 1)
    {
        ForeignUnwindDedupAtom* map = &built.maps[atom];
        map->owner = atom;
        map->length = link->atoms.size[atom];
        map->endpoint = map->length;
        map->alignment_log2 = link->atoms.alignment_log2[atom];
        if (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
        {
            result = foreign_unwind_dedup_records(map, link, atom, frames[atom], path, arena);
            map->eligible = map->eligible && pinned[atom] == 0;
        }
    }
    for (SymbolId symbol = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && symbol < link->symbols.count; symbol += 1)
    {
        const AtomId atom = link->symbols.atom[symbol];
        const uint8_t flags = link->symbols.flags[symbol];
        const bool section = section_symbols[symbol] != 0;
        // A named boundary can cover the complete concatenated unwind collection. Spanning symbols are pinned
        // conservatively even when a particular span happens to be affine; a later contract may relax this.
        if (atom != 0 && link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME &&
            (link->symbols.size[symbol] != 0 || (!section && (link->symbols.value[symbol] == 0 || link->symbols.value[symbol] == link->atoms.size[atom] ||
             (flags & BUSTER_LINK_SYMBOL_BINDING) != BUSTER_LINK_SYMBOL_LOCAL))))
        {
            built.maps[atom].eligible = false;
        }
    }
    for (FixupId fixup = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && fixup < link->fixups.count; fixup += 1)
    {
        const SymbolId symbol = link->fixups.target[fixup];
        const AtomId atom = link->symbols.atom[symbol];
        if (atom != 0 && link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME &&
            ((link->symbols.flags[symbol] & BUSTER_LINK_SYMBOL_BINDING) == BUSTER_LINK_SYMBOL_LOCAL || link_fixup_addend(link, fixup) != 0))
        {
            built.maps[atom].eligible = false;
        }
    }
    AtomId pool = 0;
    AtomId tail = 0;
    for (AtomId atom = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && atom < built.atom_count; atom += 1)
    {
        ForeignUnwindDedupAtom* map = &built.maps[atom];
        if (link->atoms.class[atom] == BUSTER_LINK_CLASS_EH_FRAME)
        {
            if (!map->eligible)
            {
                pool = 0;
                tail = 0;
            }
            else
            {
                const uint64_t cie_length = map->records[0].length;
                uint64_t contribution = 0;
                for (uint32_t r = 0; r < map->record_count; r += 1)
                {
                    contribution += map->records[r].kind == FOREIGN_UNWIND_FDE ? map->records[r].length : 0;
                }
                const bool matches = pool != 0 && foreign_unwind_dedup_identity(link, pool, &built.maps[pool].records[0], atom, &map->records[0], path);
                if (!matches || contribution > BUSTER_EXECUTABLE_MAX_ATOM_BYTES - built.maps[pool].length)
                {
                    pool = atom;
                    tail = atom;
                    map->length = cie_length;
                }
                else
                {
                    map->owner = pool;
                    built.maps[tail].next = atom;
                    tail = atom;
                    if (map->alignment_log2 > built.maps[pool].alignment_log2)
                    {
                        built.maps[pool].alignment_log2 = map->alignment_log2;
                    }
                    built.removed_atoms += 1;
                }
                for (uint32_t r = 0; r < map->record_count; r += 1)
                {
                    ForeignUnwindRecord* record = &map->records[r];
                    const bool cie = record->kind == FOREIGN_UNWIND_CIE;
                    map->kept[r] = !cie || (atom == pool && r == 0);
                    record->new_offset = cie ? 0 : built.maps[pool].length;
                    if (!cie)
                    {
                        built.maps[pool].length += record->length;
                    }
                    if (!map->kept[r])
                    {
                        built.removed_cies += 1;
                        built.removed_bytes += record->length;
                    }
                }
                map->endpoint = built.maps[pool].length;
            }
        }
    }
    for (AtomId atom = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && atom < built.atom_count; atom += 1)
    {
        ForeignUnwindDedupAtom* map = &built.maps[atom];
        if (map->owner == atom)
        {
            built.capacity.atoms += 1;
            built.atoms[atom] = built.capacity.atoms;
        }
        else
        {
            built.atoms[atom] = built.atoms[map->owner];
        }
        map->pooled = map->owner != atom || map->next != 0 || map->length != link->atoms.size[atom];
    }
    for (AtomId atom = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && atom < built.atom_count; atom += 1)
    {
        ForeignUnwindDedupAtom* map = &built.maps[atom];
        for (uint32_t r = 0; result.error == FOREIGN_UNWIND_ERROR_NONE && r < map->record_count; r += 1)
        {
            // The executable preserves the maximum atom alignment at the EH section base, but places fragments
            // without padding and checks min(input alignment, 4). Every transformed record has that same residue.
            const uint8_t required = link->atoms.alignment_log2[atom] > 2 ? 2 : link->atoms.alignment_log2[atom];
            const uint64_t mask = (1ull << required) - 1;
            if (map->records[r].atom >= built.atom_count)
            {
                result.error = FOREIGN_UNWIND_ERROR_INPUT;
            }
            else if (((map->records[r].old_offset ^ map->records[r].new_offset) & mask) != 0)
            {
                result.error = FOREIGN_UNWIND_ERROR_UNSUPPORTED;
            }
            else
            {
                map->records[r].atom = built.atoms[map->records[r].atom];
            }
        }
    }
    // Publish fixups grouped by destination atom, preserving each pool's source-record order.
    for (AtomId owner = 1; result.error == FOREIGN_UNWIND_ERROR_NONE && owner < built.atom_count; owner += 1)
    {
        if (built.maps[owner].owner == owner)
        {
            for (AtomId atom = owner; atom != 0; atom = built.maps[atom].next)
            {
                const ForeignUnwindDedupAtom* map = &built.maps[atom];
                const FixupId begin = link->atoms.fixup_begin[atom];
                uint32_t r = 0;
                for (uint32_t i = 0; i < link->atoms.fixup_count[atom]; i += 1)
                {
                    const FixupId fixup = begin + i;
                    while (r + 1 < map->record_count && link->fixups.offset[fixup] >= map->records[r + 1].old_offset)
                    {
                        r += 1;
                    }
                    if (map->record_count == 0 || map->kept[r])
                    {
                        built.capacity.fixups += 1;
                        built.fixups[fixup] = built.capacity.fixups;
                        built.capacity.wide_addends += (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0 ? 1 : 0;
                    }
                }
            }
        }
    }
    if (result.error == FOREIGN_UNWIND_ERROR_NONE)
    {
        *output = built;
    }
    else if (arena != 0)
    {
        arena_pop_to(arena, saved);
    }
    return result;
}
