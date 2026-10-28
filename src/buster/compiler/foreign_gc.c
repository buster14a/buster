// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Section reachability for the foreign linker's post-resolution Link. foreign_gc_mark produces stable old-to-new
// atom, symbol and fixup maps; it does not mutate Link or its borrowed section bytes. Include after foreign.c and
// link.c. The caller supplies the entry, exported/forced symbols and retained sections, including SHF_GNU_RETAIN.
// Init/fini fragments and init/fini arrays are roots regardless of whether a symbol names them. Definitions in live
// atoms survive, including locals; atomless symbols survive only when rooted or referred to by a live fixup.
//
// The input is the representation before layout or patching, with each fixup owned by exactly one atom. Undefined
// symbols are allowed: only surviving references can require a diagnostic. Counts include Link's zero records.
// Maps have the original table counts, entry zero is zero, discarded entries are zero, and surviving entries are
// numbered from one in original id order. Kept capacity counts exclude zero, as link_initialize expects. Names are
// retained unchanged; the consumer can reuse the name table and remap every other record and all external id maps.
// Wide addends are counted per retained fixup; a consumer rebuilding with link_add_fixup reads their original values
// with link_fixup_addend. Priorities remain ordered after the atom map is applied.
//
// All entry-point pointer arguments are required, even for an empty graph. The arena owns the maps until reset.
// All temporary bitsets are returned before success, and a failure
// changes neither the result nor the arena's length. Output and input storage must not overlap. The bitsets are
// 64-byte aligned and padded to complete vectors; map columns and root lists need no padding or special alignment.
// Masked loads/stores respect logical tails. Root classification, queue seeding and stable map emission have exact
// scalar twins. Irregular edge target loads, live-bit insertions and queue appends are scalar, without AVX-512
// gathers/scatters. Two compact atom queues hold the current and next frontiers; mark-before-append enqueues each
// atom once. This bounds traversal to O(atoms + fixups), including a chain, at the cost of two u32 queues with
// atoms.count slots apiece. The single-lane walk precedes the planned per-lane frontier exchange.
//
// This base marker does not decode unwind records. Nonempty EH_FRAME atoms fail explicitly: a
// whole .eh_frame section roots otherwise dead functions, while dropping it loses unwind information. Integration
// uses foreign_gc_link.c's FDE/CIE graph and filters their records before producing a compact foreign link.

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "host_isa.h"

#define BUSTER_FOREIGN_GC_VECTOR_WORDS 8
#define BUSTER_FOREIGN_GC_VECTOR_BITS 512
#define BUSTER_FOREIGN_GC_VECTOR_IDS 16
#define BUSTER_FOREIGN_GC_VECTOR_BYTES 64

typedef enum ForeignGcError
{
    FOREIGN_GC_ERROR_NONE,
    FOREIGN_GC_ERROR_INPUT,
    FOREIGN_GC_ERROR_MEMORY,
    FOREIGN_GC_ERROR_UNWIND,
} ForeignGcError;

typedef struct ForeignGcRoots ForeignGcRoots;
struct ForeignGcRoots
{
    // Zero is allowed when the link has no entry (for example a shared library).
    SymbolId entry;
    // The caller's export policy and -u/--undefined roots, not every global definition automatically.
    const SymbolId* exports;
    uint32_t export_count;
    const AtomId* retained;
    uint32_t retained_count;
};

typedef struct ForeignGc ForeignGc;
struct ForeignGc
{
    AtomId* atoms;
    SymbolId* symbols;
    FixupId* fixups;
    LinkCapacity kept;
    uint32_t rounds;
};

// Optional non-relocation dependencies, such as code -> FDE -> CIE. offsets has atoms.count + 1 entries, starts at
// zero and ends at count; targets has count entries. These borrowed columns are immutable and need no padding.
// They contribute atom liveness without manufacturing fixups or symbols. Null means no extra dependencies.
typedef struct ForeignGcEdges ForeignGcEdges;
struct ForeignGcEdges
{
    const uint32_t* offsets;
    const AtomId* targets;
    uint32_t count;
};

static __attribute__((unused)) ForeignGcError foreign_gc_mark(const Link* link, const ForeignGcRoots* roots, ForeignPath path, Arena* arena, ForeignGc* output);
static __attribute__((unused)) ForeignGcError foreign_gc_mark_edges(const Link* link, const ForeignGcRoots* roots, const ForeignGcEdges* edges,
                                                                  ForeignPath path, Arena* arena, ForeignGc* output);

static uint64_t foreign_gc_words(uint32_t count)
{
    return (((uint64_t)count + BUSTER_FOREIGN_GC_VECTOR_BITS - 1) / BUSTER_FOREIGN_GC_VECTOR_BITS) * BUSTER_FOREIGN_GC_VECTOR_WORDS;
}

static bool foreign_gc_bit(const uint64_t* bits, uint32_t id)
{
    return (bits[id / 64] & (1ull << (id % 64))) != 0;
}

static void foreign_gc_set(uint64_t* bits, uint32_t id)
{
    bits[id / 64] |= 1ull << (id % 64);
}

// Basic graph-column checks precede every index use below; the representation's existing checker verifies fixup
// ownership/ranges/fields without diagnosing undefined symbols or mutating Link. owned is reused for live fixups.
static ForeignGcError foreign_gc_validate(const Link* link, const ForeignGcRoots* roots, const ForeignGcEdges* edges, uint64_t* owned)
{
    const LinkAtoms* atoms = &link->atoms;
    const LinkSymbols* symbols = &link->symbols;
    ForeignGcError result = atoms->class[0] == BUSTER_LINK_CLASS_NONE && atoms->fixup_count[0] == 0 && symbols->atom[0] == 0 ?
                            FOREIGN_GC_ERROR_NONE : FOREIGN_GC_ERROR_INPUT;
    uint64_t fixups = 0;
    for (AtomId atom = 1; result == FOREIGN_GC_ERROR_NONE && atom < atoms->count; atom += 1)
    {
        if (atoms->class[atom] == BUSTER_LINK_CLASS_NONE || atoms->class[atom] >= BUSTER_LINK_CLASS_COUNT || !link_check_fixups(link, atom, owned))
        {
            result = FOREIGN_GC_ERROR_INPUT;
        }
        else if (atoms->class[atom] == BUSTER_LINK_CLASS_EH_FRAME && atoms->size[atom] != 0)
        {
            result = FOREIGN_GC_ERROR_UNWIND;
        }
        fixups += atoms->fixup_count[atom];
    }
    if (result == FOREIGN_GC_ERROR_NONE && fixups + 1 != link->fixups.count)
    {
        result = FOREIGN_GC_ERROR_INPUT;
    }
    for (SymbolId symbol = 1; result == FOREIGN_GC_ERROR_NONE && symbol < symbols->count; symbol += 1)
    {
        if (symbols->atom[symbol] >= atoms->count)
        {
            result = FOREIGN_GC_ERROR_INPUT;
        }
    }
    for (uint32_t i = 1; result == FOREIGN_GC_ERROR_NONE && i < link->priorities.count; i += 1)
    {
        const AtomId atom = link->priorities.atom[i];
        if (atom == 0 || atom >= atoms->count || !link_class_is_array(atoms->class[atom]) || (i > 1 && atom <= link->priorities.atom[i - 1]))
        {
            result = FOREIGN_GC_ERROR_INPUT;
        }
    }
    if (result == FOREIGN_GC_ERROR_NONE && roots->entry >= symbols->count)
    {
        result = FOREIGN_GC_ERROR_INPUT;
    }
    for (uint32_t i = 0; result == FOREIGN_GC_ERROR_NONE && i < roots->export_count; i += 1)
    {
        if (roots->exports[i] == 0 || roots->exports[i] >= symbols->count)
        {
            result = FOREIGN_GC_ERROR_INPUT;
        }
    }
    for (uint32_t i = 0; result == FOREIGN_GC_ERROR_NONE && i < roots->retained_count; i += 1)
    {
        if (roots->retained[i] == 0 || roots->retained[i] >= atoms->count)
        {
            result = FOREIGN_GC_ERROR_INPUT;
        }
    }
    if (result == FOREIGN_GC_ERROR_NONE && edges != 0)
    {
        const bool storage = edges->offsets != 0 && (edges->count == 0 || edges->targets != 0);
        result = storage && edges->offsets[0] == 0 && edges->offsets[atoms->count] == edges->count ? FOREIGN_GC_ERROR_NONE : FOREIGN_GC_ERROR_INPUT;
        for (uint32_t atom = 0; result == FOREIGN_GC_ERROR_NONE && atom < atoms->count; atom += 1)
        {
            if (edges->offsets[atom] > edges->offsets[(uint64_t)atom + 1] || edges->offsets[(uint64_t)atom + 1] > edges->count)
            {
                result = FOREIGN_GC_ERROR_INPUT;
            }
        }
        for (uint32_t edge = 0; result == FOREIGN_GC_ERROR_NONE && edge < edges->count; edge += 1)
        {
            if (edges->targets[edge] == 0 || edges->targets[edge] >= atoms->count)
            {
                result = FOREIGN_GC_ERROR_INPUT;
            }
        }
    }
    return result;
}

static void foreign_gc_root_symbol(const Link* link, SymbolId symbol, uint64_t* atoms, uint64_t* symbols)
{
    if (symbol != 0)
    {
        foreign_gc_set(symbols, symbol);
        const AtomId atom = link->symbols.atom[symbol];
        if (atom != 0)
        {
            foreign_gc_set(atoms, atom);
        }
    }
}

static void foreign_gc_root_classes(const Link* link, ForeignPath path, uint64_t* live)
{
    const uint32_t count = link->atoms.count;
    uint32_t atom = 0;
    if (path == FOREIGN_PATH_VECTOR)
    {
        while (atom < count)
        {
            const uint32_t width = count - atom < 64 ? count - atom : 64;
            const __mmask64 tail = width == 64 ? UINT64_MAX : (1ull << width) - 1;
            const __m512i classes = _mm512_maskz_loadu_epi8(tail, link->atoms.class + atom);
            const __mmask64 init = _mm512_cmpeq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_LINK_CLASS_INIT));
            const __mmask64 fini = _mm512_cmpeq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_LINK_CLASS_FINI));
            const __mmask64 init_array = _mm512_cmpeq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_LINK_CLASS_INIT_ARRAY));
            const __mmask64 fini_array = _mm512_cmpeq_epi8_mask(classes, _mm512_set1_epi8(BUSTER_LINK_CLASS_FINI_ARRAY));
            live[atom / 64] |= (init | fini | init_array | fini_array) & tail;
            atom += width;
        }
    }
    else
    {
        for (atom = 1; atom < count; atom += 1)
        {
            const uint8_t class = link->atoms.class[atom];
            if (class == BUSTER_LINK_CLASS_INIT || class == BUSTER_LINK_CLASS_FINI || link_class_is_array(class))
            {
                foreign_gc_set(live, atom);
            }
        }
    }
}

// Compact the initial roots once. live has zero padding beyond count; queue needs popcount(live) writable u32 slots,
// without padding. The mask on each packed store writes only the popcount of its source mask. The caller reserves
// count slots for the walk's maximum frontier, but this kernel also permits an exactly sized output at a page edge.
static uint32_t foreign_gc_queue(const uint64_t* live, uint32_t count, ForeignPath path, AtomId* queue)
{
    uint32_t next = 0;
    if (path == FOREIGN_PATH_VECTOR)
    {
        const __m512i ordinals = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        for (uint64_t id = 0; id < count; id += BUSTER_FOREIGN_GC_VECTOR_IDS)
        {
            const uint32_t width = count - id < BUSTER_FOREIGN_GC_VECTOR_IDS ? (uint32_t)(count - id) : BUSTER_FOREIGN_GC_VECTOR_IDS;
            const __mmask16 tail = (__mmask16)((1u << width) - 1);
            const __mmask16 kept = (__mmask16)(live[id / 64] >> (id % 64)) & tail;
            if (kept != 0)
            {
                const uint32_t added = (uint32_t)__builtin_popcount((uint32_t)kept);
                const __m512i ids = _mm512_add_epi32(ordinals, _mm512_set1_epi32((int32_t)id));
                const __m512i packed = _mm512_maskz_compress_epi32(kept, ids);
                _mm512_mask_storeu_epi32(queue + next, (__mmask16)((1u << added) - 1), packed);
                next += added;
            }
        }
    }
    else
    {
        const uint64_t words = ((uint64_t)count + 63) / 64;
        for (uint64_t word = 0; word < words; word += 1)
        {
            uint64_t active = live[word];
            while (active != 0)
            {
                queue[next] = (AtomId)(word * 64 + (uint32_t)__builtin_ctzll(active));
                next += 1;
                active &= active - 1;
            }
        }
    }
    return next;
}

static uint32_t foreign_gc_map(const uint64_t* live, uint32_t count, ForeignPath path, uint32_t* map)
{
    uint32_t next = 0;
    if (path == FOREIGN_PATH_VECTOR)
    {
        const __m512i ordinals = _mm512_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
        for (uint64_t id = 0; id < count; id += BUSTER_FOREIGN_GC_VECTOR_IDS)
        {
            const uint32_t width = count - id < BUSTER_FOREIGN_GC_VECTOR_IDS ? (uint32_t)(count - id) : BUSTER_FOREIGN_GC_VECTOR_IDS;
            const __mmask16 tail = (__mmask16)((1u << width) - 1);
            const __mmask16 kept = (__mmask16)(live[id / 64] >> (id % 64)) & tail;
            const __m512i ids = _mm512_add_epi32(ordinals, _mm512_set1_epi32((int32_t)next));
            _mm512_mask_storeu_epi32(map + id, tail, _mm512_maskz_expand_epi32(kept, ids));
            next += (uint32_t)__builtin_popcount((uint32_t)kept);
        }
    }
    else
    {
        for (uint32_t id = 0; id < count; id += 1)
        {
            const bool kept = foreign_gc_bit(live, id);
            next += kept ? 1 : 0;
            map[id] = kept ? next : 0;
        }
    }
    return next;
}

static void foreign_gc_walk(const Link* link, const ForeignGcEdges* edges, uint32_t count, uint64_t* live, AtomId* current, AtomId* next,
                            uint64_t* symbols, uint64_t* fixups, ForeignGc* result)
{
    while (count != 0)
    {
        result->rounds += 1;
        uint32_t next_count = 0;
        for (uint32_t at = 0; at < count; at += 1)
        {
            const AtomId atom = current[at];
            const FixupId begin = link->atoms.fixup_begin[atom];
            const uint32_t fixup_count = link->atoms.fixup_count[atom];
            for (uint32_t i = 0; i < fixup_count; i += 1)
            {
                const FixupId fixup = begin + i;
                const SymbolId target = link->fixups.target[fixup];
                const AtomId target_atom = link->symbols.atom[target];
                foreign_gc_set(fixups, fixup);
                foreign_gc_set(symbols, target);
                if (target_atom != 0 && !foreign_gc_bit(live, target_atom))
                {
                    foreign_gc_set(live, target_atom);
                    next[next_count] = target_atom;
                    next_count += 1;
                }
            }
            if (edges != 0)
            {
                for (uint32_t edge = edges->offsets[atom]; edge < edges->offsets[(uint64_t)atom + 1]; edge += 1)
                {
                    const AtomId target = edges->targets[edge];
                    if (!foreign_gc_bit(live, target))
                    {
                        foreign_gc_set(live, target);
                        next[next_count] = target;
                        next_count += 1;
                    }
                }
            }
        }
        AtomId* swap = current;
        current = next;
        next = swap;
        count = next_count;
    }
}

static ForeignGcError foreign_gc_mark_edges(const Link* link, const ForeignGcRoots* roots, const ForeignGcEdges* edges, ForeignPath path, Arena* arena, ForeignGc* output)
{
    const uint64_t saved_length = arena->length;
    const bool tables = link->error == LINK_ERROR_NONE && link->names.count != 0 && link->atoms.count != 0 && link->symbols.count != 0 &&
                        link->fixups.count != 0 && link->fixups.wide_count != 0 && link->priorities.count != 0 && link->names.count <= link->names.capacity &&
                        link->atoms.count <= link->atoms.capacity && link->fixups.wide_count <= link->fixups.wide_capacity &&
                        link->symbols.count <= link->symbols.capacity && link->fixups.count <= link->fixups.capacity &&
                        link->priorities.count <= link->priorities.capacity && link->atoms.class != 0 && link->atoms.size != 0 &&
                        link->atoms.bytes != 0 && link->atoms.fixup_begin != 0 && link->atoms.fixup_count != 0 && link->symbols.atom != 0 &&
                        link->fixups.offset != 0 && link->fixups.target != 0 && link->fixups.addend != 0 && link->fixups.kind != 0 && link->fixups.wide_addends != 0 &&
                        link->priorities.atom != 0;
    ForeignGcError error = tables && (path == FOREIGN_PATH_VECTOR || path == FOREIGN_PATH_EXACT) &&
                           (roots->export_count == 0 || roots->exports != 0) && (roots->retained_count == 0 || roots->retained != 0) ?
                           FOREIGN_GC_ERROR_NONE : FOREIGN_GC_ERROR_INPUT;
    ForeignGc result = { 0 };
    if (error == FOREIGN_GC_ERROR_NONE)
    {
        result.atoms = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, link->atoms.count);
        result.symbols = BUSTER_ARENA_PUSH_ARRAY(arena, SymbolId, link->symbols.count);
        result.fixups = BUSTER_ARENA_PUSH_ARRAY(arena, FixupId, link->fixups.count);
        error = result.atoms != 0 && result.symbols != 0 && result.fixups != 0 ? FOREIGN_GC_ERROR_NONE : FOREIGN_GC_ERROR_MEMORY;
    }
    const uint64_t maps_length = arena->length;
    const uint64_t atom_words = foreign_gc_words(link->atoms.count);
    const uint64_t symbol_words = foreign_gc_words(link->symbols.count);
    const uint64_t fixup_words = foreign_gc_words(link->fixups.count);
    uint64_t* live = 0;
    AtomId* current = 0;
    AtomId* next = 0;
    uint64_t* symbols = 0;
    uint64_t* fixups = 0;
    if (error == FOREIGN_GC_ERROR_NONE)
    {
        live = arena_push(arena, atom_words * sizeof(uint64_t), BUSTER_FOREIGN_GC_VECTOR_BYTES);
        current = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, link->atoms.count);
        next = BUSTER_ARENA_PUSH_ARRAY(arena, AtomId, link->atoms.count);
        symbols = arena_push(arena, symbol_words * sizeof(uint64_t), BUSTER_FOREIGN_GC_VECTOR_BYTES);
        fixups = arena_push(arena, fixup_words * sizeof(uint64_t), BUSTER_FOREIGN_GC_VECTOR_BYTES);
        error = live != 0 && current != 0 && next != 0 && symbols != 0 && fixups != 0 ? FOREIGN_GC_ERROR_NONE : FOREIGN_GC_ERROR_MEMORY;
    }
    if (error == FOREIGN_GC_ERROR_NONE)
    {
        memset(live, 0, atom_words * sizeof(uint64_t));
        memset(symbols, 0, symbol_words * sizeof(uint64_t));
        memset(fixups, 0, fixup_words * sizeof(uint64_t));
        error = foreign_gc_validate(link, roots, edges, fixups);
    }
    if (error == FOREIGN_GC_ERROR_NONE)
    {
        memset(fixups, 0, fixup_words * sizeof(uint64_t));
        foreign_gc_root_classes(link, path, live);
        foreign_gc_root_symbol(link, roots->entry, live, symbols);
        for (uint32_t i = 0; i < roots->export_count; i += 1)
        {
            foreign_gc_root_symbol(link, roots->exports[i], live, symbols);
        }
        for (uint32_t i = 0; i < roots->retained_count; i += 1)
        {
            foreign_gc_set(live, roots->retained[i]);
        }
        const uint32_t count = foreign_gc_queue(live, link->atoms.count, path, current);
        foreign_gc_walk(link, edges, count, live, current, next, symbols, fixups, &result);
        for (SymbolId symbol = 1; symbol < link->symbols.count; symbol += 1)
        {
            const AtomId atom = link->symbols.atom[symbol];
            if (atom != 0 && foreign_gc_bit(live, atom))
            {
                foreign_gc_set(symbols, symbol);
            }
        }
        result.kept.names = link->names.count - 1;
        result.kept.atoms = foreign_gc_map(live, link->atoms.count, path, result.atoms);
        result.kept.symbols = foreign_gc_map(symbols, link->symbols.count, path, result.symbols);
        result.kept.fixups = foreign_gc_map(fixups, link->fixups.count, path, result.fixups);
        for (FixupId fixup = 1; fixup < link->fixups.count; fixup += 1)
        {
            result.kept.wide_addends += result.fixups[fixup] != 0 && (link->fixups.kind[fixup] & BUSTER_LINK_FIXUP_WIDE) != 0 ? 1 : 0;
        }
        for (uint32_t i = 1; i < link->priorities.count; i += 1)
        {
            result.kept.priorities += result.atoms[link->priorities.atom[i]] != 0 ? 1 : 0;
        }
        *output = result;
    }
    arena_pop_to(arena, error == FOREIGN_GC_ERROR_NONE ? maps_length : saved_length);
    return error;
}

static ForeignGcError foreign_gc_mark(const Link* link, const ForeignGcRoots* roots, ForeignPath path, Arena* arena, ForeignGc* output)
{
    return foreign_gc_mark_edges(link, roots, 0, path, arena, output);
}
