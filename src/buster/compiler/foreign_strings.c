// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Stable exact string interning for foreign SHF_MERGE | SHF_STRINGS sections with one-, two- or four-byte code units. This producer
// builds a pool and a per-entry offset map without changing its input. The consumer must translate section-symbol
// references at S+A and named symbols at S (retaining their addend), before publishing the changed Link. It supplies
// group ids for compatible section names/types/flags; strings in different groups never share storage. Empty input
// sections are valid. Relocatable string contents and other code-unit widths are outside this interface.
//
// Terminator scans use bounded masked 64-byte loads; hashing uses the existing eight-string kernel, both with exact
// scalar twins. Hash-table probes and offset lookups are irregular and scalar. Entries are interned in input order. A
// duplicate keeps the first suitable owner's bytes (an earlier entry with storage of its own), provided its output
// offset meets the requested effective alignment: its section's alignment reduced by its input offset. A one-byte
// string that equals no suitable owner tries one more place (#3044), the end of its block head, where that offset
// meets its alignment: its head is the earliest one-byte entry of its group, owner or not, whose content ends with its
// own (equal or longer). Otherwise it becomes an owner. Wider units share whole entries only. Owners are placed in
// input order and a later string never moves an earlier one. The output pool is zeroed before the owners are copied,
// making padding deterministic.
//
// Block heads (foreign_strings_block_heads). (a) A head is always an owner: nothing earlier equals it or ends with it,
// so neither the equal probe nor a head places it. (b) At effective alignment 1 the rule equals the earlier one, which
// walked every earlier, longer owner for the first whose end met the alignment: that first owner is then the head.
// (c) The two differ only at alignment 2 or more, when the head's end misses. (d) Heads depend only on contents and
// input order: they are minima, so they need neither offsets nor a stable sort. Groups whose one-byte entries all have
// one length are skipped, since an entry's head is then the first equal one, which the equal probe tries
// (foreign_strings_groups). In the others a prefilter (foreign_strings_flag) flags every content with a proper
// extension in its group together with its whole block; flags are a function of (group, content), so an unflagged
// entry's head has its own content, and an empty string's is its group's first one-byte entry. The flagged entries are
// sorted by reversed content (foreign_strings_sort), so a content's block follows it, and a stack pass
// (foreign_strings_heads) keeps each class's earliest block entry, writing a head only where that is not the class's
// own first entry. The interning loop checks a head in O(1).
//
// What stays scalar, with counts in place of the 9700X measurements it still owes: one key load per considered entry
// and prefilter pass (8 bytes, or masked for a shorter tail); at most 2 (long), 7 (donor levels) and 1 (receiver)
// bitmap bit operations an entry, as AVX-512 has no bit scatter; the radix sort's count and scatter, once for each
// partition a record takes, which is one per key digit that varies in its range and at most 8 a sort (zmm vpscatterdd
// is 88 uops, vpgatherdd 64), then an insertion sort of its slice or bucket of at most 32 records, at most 31 key
// comparisons and moves a record a sort; per member of a tie of more than 32 and level, one key load and at most four
// 64-byte comparison steps with the tie's first member, where every level starts at least 8 bytes deeper than the last
// and splits its tie unless all its members share the 256 bytes it compares; comparisons bounded by the common tail /
// 64 in ties of at most 32; one push and one pop per distinct content; one head write per member of a class headed by
// an extension; the interning loop, which was scalar before. The flags are a bitset compacted 16 entries a step, and
// the prefilter's bitmaps are popped before the sort reuses their pages.
//
// Borrowed inputs remain live through build. The caller owns result arrays and pool in its arena until reset. A
// failure leaves both the result and arena length unchanged. Offsets and counts are 32-bit, with checked totals;
// neither input buffers nor output columns require vector padding. Include after foreign.c and link.c.

typedef enum ForeignStringsError
{
    FOREIGN_STRINGS_ERROR_NONE,
    FOREIGN_STRINGS_ERROR_INPUT,
    FOREIGN_STRINGS_ERROR_MEMORY,
    FOREIGN_STRINGS_ERROR_CAPACITY,
    FOREIGN_STRINGS_ERROR_UNSUPPORTED,
} ForeignStringsError;

typedef struct ForeignStringInput ForeignStringInput;
struct ForeignStringInput
{
    const uint8_t* bytes;
    uint32_t size;
    uint32_t group;
    uint8_t alignment_log2;
    uint8_t unit_log2;
};

typedef struct ForeignStrings ForeignStrings;
struct ForeignStrings
{
    uint8_t* bytes;
    uint32_t size;
    uint32_t count;
    uint32_t input_count;
    // Entry range per input section, including the final sentinel; entry offsets are ordered within each range.
    uint32_t* section_begin;
    uint32_t* input_offset;
    uint32_t* output_offset;
    uint32_t* lengths;
};

static __attribute__((unused)) ForeignStringsError foreign_strings_build(const ForeignStringInput* inputs, uint32_t input_count,
                                                                        ForeignPath path, Arena* arena, ForeignStrings* output);
static __attribute__((unused)) bool foreign_strings_offset(const ForeignStrings* strings, uint32_t input, uint64_t offset, uint32_t* mapped);

// Zero-byte positions in this bounded block. The mask excludes inactive lanes even though a masked load zeros them.
static uint64_t foreign_strings_zeros(const uint8_t* bytes, uint32_t count, uint8_t unit_log2, ForeignPath path)
{
    const uint32_t units = count >> unit_log2;
    const uint64_t valid = units >= 64 ? ~0ull : (1ull << units) - 1;
    uint64_t zeros = 0;
    if (path == FOREIGN_PATH_VECTOR && unit_log2 == 0)
    {
        zeros = _mm512_mask_cmpeq_epi8_mask(valid, _mm512_maskz_loadu_epi8(valid, bytes), _mm512_setzero_si512());
    }
    else if (path == FOREIGN_PATH_VECTOR && unit_log2 == 1)
    {
        zeros = _mm512_mask_cmpeq_epi16_mask((__mmask32)valid, _mm512_maskz_loadu_epi16((__mmask32)valid, bytes), _mm512_setzero_si512());
    }
    else if (path == FOREIGN_PATH_VECTOR)
    {
        zeros = _mm512_mask_cmpeq_epi32_mask((__mmask16)valid, _mm512_maskz_loadu_epi32((__mmask16)valid, bytes), _mm512_setzero_si512());
    }
    else
    {
        #pragma clang loop vectorize(disable) interleave(disable)
        for (uint32_t i = 0; i < units; i += 1)
        {
            uint32_t value = 0;
            memcpy(&value, bytes + (i << unit_log2), 1u << unit_log2);
            zeros |= value == 0 ? 1ull << i : 0;
        }
    }
    return zeros;
}

static ForeignStringsError foreign_strings_count(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, uint32_t* count)
{
    ForeignStringsError error = FOREIGN_STRINGS_ERROR_NONE;
    uint64_t total = 0;
    for (uint32_t section = 0; error == FOREIGN_STRINGS_ERROR_NONE && section < input_count; section += 1)
    {
        const ForeignStringInput* input = &inputs[section];
        const uint32_t unit = input->unit_log2 <= 2 ? 1u << input->unit_log2 : 0;
        uint32_t last = 0;
        const bool bounded = unit != 0 && input->size % unit == 0 && (input->size == 0 || input->bytes != 0);
        if (bounded && input->size != 0)
        {
            memcpy(&last, input->bytes + input->size - unit, unit);
        }
        if (input->alignment_log2 >= 32 || !bounded || last != 0)
        {
            error = FOREIGN_STRINGS_ERROR_INPUT;
        }
        for (uint64_t at = 0; error == FOREIGN_STRINGS_ERROR_NONE && at < input->size; at += 64)
        {
            const uint32_t left = input->size - (uint32_t)at;
            total += (uint64_t)__builtin_popcountll(foreign_strings_zeros(input->bytes + at, left < 64 ? left : 64, input->unit_log2, path));
            if (total >= UINT32_MAX)
            {
                error = FOREIGN_STRINGS_ERROR_CAPACITY;
            }
        }
    }
    *count = error == FOREIGN_STRINGS_ERROR_NONE ? (uint32_t)total : 0;
    return error;
}

// Fill the byte-pointer/length hash input columns and the consumer's ordered offset map in the same scan.
static void foreign_strings_entries(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, ForeignStrings* strings,
                                    const uint8_t** pointers, uint32_t* sections)
{
    uint32_t next = 0;
    for (uint32_t section = 0; section < input_count; section += 1)
    {
        const ForeignStringInput* input = &inputs[section];
        uint32_t begin = 0;
        strings->section_begin[section] = next;
        for (uint64_t at = 0; at < input->size; at += 64)
        {
            const uint32_t left = input->size - (uint32_t)at;
            for (uint64_t zeros = foreign_strings_zeros(input->bytes + at, left < 64 ? left : 64, input->unit_log2, path); zeros != 0; zeros &= zeros - 1)
            {
                const uint32_t end = (uint32_t)at + (((uint32_t)__builtin_ctzll(zeros) + 1) << input->unit_log2);
                pointers[next] = input->bytes + begin;
                sections[next] = section;
                strings->input_offset[next] = begin;
                strings->lengths[next] = end - begin;
                begin = end;
                next += 1;
            }
        }
    }
    strings->section_begin[input_count] = next;
}

// No group, entry or head.
#define BUSTER_FOREIGN_STRINGS_NONE UINT32_MAX
// The prefilter's hash (#3044): H(g, j, t, b) = ((t + g * GROUP + j * LEVEL) * GOLDEN) >> (64 - b), for the sorted group
// g, the j last content bytes t and a bitmap of 2^b bits. The same constants hash a group value into its map's slot.
#define BUSTER_FOREIGN_STRINGS_FILTER_GROUP 0xd6e8feb86659fd93ull
#define BUSTER_FOREIGN_STRINGS_FILTER_LEVEL 0xa0761d6478bd642full
#define BUSTER_FOREIGN_STRINGS_FILTER_GOLDEN 0x9e3779b97f4a7c15ull
// Bitmap bits an entry counted for it, and the smallest bitmap, 2^9 bits.
#define BUSTER_FOREIGN_STRINGS_FILTER_DENSITY 16
#define BUSTER_FOREIGN_STRINGS_FILTER_MINIMUM_LOG2 9
// Content bytes a sort key holds, the most records an insertion sort orders, and the most content bytes past its depth
// that a tie's common-tail pass compares a level (four 64-byte steps).
#define BUSTER_FOREIGN_STRINGS_KEY_BYTES 8
#define BUSTER_FOREIGN_STRINGS_INSERTION 32
#define BUSTER_FOREIGN_STRINGS_COMMON_SPAN 256

// A flagged entry being sorted: a key of BUSTER_FOREIGN_STRINGS_KEY_BYTES content bytes, its entry and its content
// bytes (its length less the terminator).
typedef struct ForeignStringsRecord ForeignStringsRecord;
struct ForeignStringsRecord
{
    uint64_t key;
    uint32_t entry;
    uint32_t content;
};

// An open class of the head pass: equal contents at consecutive sorted positions from `begin`, with the earliest entry
// of its block seen so far and of its own members.
typedef struct ForeignStringsClass ForeignStringsClass;
struct ForeignStringsClass
{
    uint32_t content;
    uint32_t best;
    uint32_t own;
    uint32_t begin;
};

_Static_assert(sizeof(ForeignStringsClass) <= sizeof(ForeignStringsRecord), "the head pass's stack reuses the records' spare buffer");

// Sorted positions [begin, end) whose last `depth` content bytes are equal, still to be ordered beyond them.
typedef struct ForeignStringsRun ForeignStringsRun;
struct ForeignStringsRun
{
    uint32_t begin;
    uint32_t end;
    uint32_t depth;
};

// Sorted positions [begin, end) of more than BUSTER_FOREIGN_STRINGS_INSERTION records whose keys are equal above the
// digit the radix sort orders next.
typedef struct ForeignStringsRange ForeignStringsRange;
struct ForeignStringsRange
{
    uint32_t begin;
    uint32_t end;
    // Where its records are: in the radix sort's spare buffer, or in its records.
    bool spare;
};

// A dense group of one-byte sections with entries: its first entry, its shortest and longest lengths (terminators
// included), its sorted group's number, and its entries with at least BUSTER_FOREIGN_STRINGS_KEY_BYTES content bytes
// and with one to seven.
typedef struct ForeignStringsGroup ForeignStringsGroup;
struct ForeignStringsGroup
{
    uint32_t first;
    uint32_t shortest;
    uint32_t longest;
    uint32_t sorted;
    uint32_t entries;
    uint32_t long_count;
    uint32_t short_count;
};

typedef struct ForeignStringsBlocks ForeignStringsBlocks;
struct ForeignStringsBlocks
{
    Arena* arena;
    uint32_t count;
    // Per section: its sorted group, or BUSTER_FOREIGN_STRINGS_NONE outside every sorted group.
    uint32_t* section_groups;
    // Per sorted group: its first one-byte entry, its empty strings' head.
    uint32_t* group_first;
    uint32_t sorted_groups;
    // The one-byte entries of sorted groups, and those with at least BUSTER_FOREIGN_STRINGS_KEY_BYTES content bytes and
    // with one to seven: the prefilter's long and short bitmaps' sizes.
    uint32_t considered;
    uint32_t long_count;
    uint32_t short_count;
    // The flagged entries in input order.
    uint32_t* flagged;
    uint32_t flagged_count;
    // Per entry: its head, or BUSTER_FOREIGN_STRINGS_NONE; null while no head is written.
    uint32_t* heads;
    uint32_t heads_written;
};

// One section's one-byte lengths into its group's bounds and counts, 16 at a time.
static void foreign_strings_group_lengths(const uint32_t* lengths, uint32_t begin, uint32_t end, ForeignStringsGroup* group)
{
    const __m512i long_length = _mm512_set1_epi32(BUSTER_FOREIGN_STRINGS_KEY_BYTES + 1);
    const __m512i short_length = _mm512_set1_epi32(2);
    __m512i shortest = _mm512_set1_epi32(-1);
    __m512i longest = _mm512_setzero_si512();
    uint32_t long_count = 0;
    uint32_t short_count = 0;
    for (uint32_t at = begin; at < end; at += 16)
    {
        const uint32_t left = end - at;
        const __mmask16 valid = (__mmask16)(left >= 16 ? 0xffffu : (1u << left) - 1);
        const __m512i length = _mm512_maskz_loadu_epi32(valid, lengths + at);
        shortest = _mm512_mask_min_epu32(shortest, valid, shortest, length);
        longest = _mm512_mask_max_epu32(longest, valid, longest, length);
        const __mmask16 is_long = _mm512_mask_cmpge_epu32_mask(valid, length, long_length);
        long_count += (uint32_t)__builtin_popcount(is_long);
        short_count += (uint32_t)__builtin_popcount(_mm512_mask_cmpge_epu32_mask((__mmask16)(valid & ~is_long), length, short_length));
    }
    const uint32_t section_shortest = _mm512_reduce_min_epu32(shortest);
    const uint32_t section_longest = _mm512_reduce_max_epu32(longest);
    group->shortest = section_shortest < group->shortest ? section_shortest : group->shortest;
    group->longest = section_longest > group->longest ? section_longest : group->longest;
    group->entries += end - begin;
    group->long_count += long_count;
    group->short_count += short_count;
}

// Dense groups of the one-byte sections that hold entries, through an open-addressed map of their group values (any
// u32, the map storing a dense id + 1), and their length bounds. A group is sorted when its one-byte entries have
// more than one length; the sorted groups are numbered in their first section's order. O(sections), and nothing
// more when no section is one-byte.
static bool foreign_strings_groups(const ForeignStringInput* inputs, uint32_t input_count, const ForeignStrings* strings, ForeignStringsBlocks* blocks)
{
    Arena* arena = blocks->arena;
    const uint32_t* section_begin = strings->section_begin;
    uint32_t narrow = 0;
    for (uint32_t section = 0; section < input_count; section += 1)
    {
        narrow += inputs[section].unit_log2 == 0 && section_begin[section + 1] != section_begin[section] ? 1 : 0;
    }
    uint64_t slot_count = 2;
    while (slot_count < 2 * (uint64_t)narrow)
    {
        slot_count *= 2;
    }
    uint32_t* section_groups = narrow != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, input_count) : 0;
    uint32_t* values = narrow != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slot_count) : 0;
    uint32_t* ids = narrow != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, slot_count) : 0;
    ForeignStringsGroup* groups = narrow != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, ForeignStringsGroup, narrow) : 0;
    uint32_t* group_first = narrow != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, narrow) : 0;
    const bool valid = narrow == 0 || (section_groups != 0 && values != 0 && ids != 0 && groups != 0 && group_first != 0);
    if (narrow != 0 && valid)
    {
        memset(ids, 0, slot_count * sizeof(uint32_t));
        uint32_t group_count = 0;
        for (uint32_t section = 0; section < input_count; section += 1)
        {
            const uint32_t begin = section_begin[section];
            const uint32_t end = section_begin[section + 1];
            uint32_t id = BUSTER_FOREIGN_STRINGS_NONE;
            if (inputs[section].unit_log2 == 0 && end != begin)
            {
                const uint32_t value = inputs[section].group;
                uint64_t slot = (((uint64_t)value * BUSTER_FOREIGN_STRINGS_FILTER_GOLDEN) >> 32) & (slot_count - 1);
                while (ids[slot] != 0 && values[slot] != value)
                {
                    slot = (slot + 1) & (slot_count - 1);
                }
                if (ids[slot] == 0)
                {
                    values[slot] = value;
                    ids[slot] = group_count + 1;
                    groups[group_count] = (ForeignStringsGroup){
                        .first = begin,
                        .shortest = UINT32_MAX,
                    };
                    group_count += 1;
                }
                id = ids[slot] - 1;
                foreign_strings_group_lengths(strings->lengths, begin, end, &groups[id]);
            }
            section_groups[section] = id;
        }
        uint32_t sorted = 0;
        for (uint32_t id = 0; id < group_count; id += 1)
        {
            ForeignStringsGroup* group = &groups[id];
            group->sorted = group->shortest != group->longest ? sorted : BUSTER_FOREIGN_STRINGS_NONE;
            if (group->sorted != BUSTER_FOREIGN_STRINGS_NONE)
            {
                group_first[sorted] = group->first;
                blocks->considered += group->entries;
                blocks->long_count += group->long_count;
                blocks->short_count += group->short_count;
                sorted += 1;
            }
        }
        for (uint32_t section = 0; section < input_count; section += 1)
        {
            const uint32_t id = section_groups[section];
            section_groups[section] = id != BUSTER_FOREIGN_STRINGS_NONE ? groups[id].sorted : BUSTER_FOREIGN_STRINGS_NONE;
        }
        blocks->section_groups = section_groups;
        blocks->group_first = group_first;
        blocks->sorted_groups = sorted;
    }
    return valid;
}

// The BUSTER_FOREIGN_STRINGS_KEY_BYTES content bytes that end `depth` bytes before the terminator, read by one load
// that starts at or after the entry's first byte, the one nearest the terminator most significant and zeros below a
// shorter tail: a plain 8-byte load when that many bytes remain (cheaper than the masked form, which the prefilter's
// long level reads once an entry and pass), else a masked load of the rest, shifted up. Content bytes are nonzero, so
// unsigned key order is reversed content order, a suffix first; past the entry's start the key is zero, as the empty
// load's zero kept by the shift of (64 - 0) & 63.
static uint64_t foreign_strings_key(const uint8_t* bytes, uint32_t content, uint32_t depth)
{
    const uint32_t left = content > depth ? content - depth : 0;
    uint64_t key = 0;
    if (left >= BUSTER_FOREIGN_STRINGS_KEY_BYTES)
    {
        memcpy(&key, bytes + left - BUSTER_FOREIGN_STRINGS_KEY_BYTES, BUSTER_FOREIGN_STRINGS_KEY_BYTES);
    }
    else
    {
        const __m128i loaded = _mm_maskz_loadu_epi8((__mmask16)((1u << left) - 1), bytes);
        key = (uint64_t)_mm_cvtsi128_si64(loaded) << ((64 - 8 * left) & 63);
    }
    return key;
}

static uint64_t foreign_strings_filter_slot(uint64_t tail, uint64_t mix, uint32_t bits_log2)
{
    return ((tail + mix) * BUSTER_FOREIGN_STRINGS_FILTER_GOLDEN) >> (64 - bits_log2);
}

static uint32_t foreign_strings_filter_log2(uint32_t count)
{
    uint32_t bits_log2 = BUSTER_FOREIGN_STRINGS_FILTER_MINIMUM_LOG2;
    while ((1ull << bits_log2) < (uint64_t)BUSTER_FOREIGN_STRINGS_FILTER_DENSITY * count)
    {
        bits_log2 += 1;
    }
    return bits_log2;
}

// Allocates the heads, every one BUSTER_FOREIGN_STRINGS_NONE (all bytes 0xff), when the first is about to be written.
static bool foreign_strings_heads_ready(ForeignStringsBlocks* blocks)
{
    if (blocks->heads == 0)
    {
        blocks->heads = BUSTER_ARENA_PUSH_ARRAY(blocks->arena, uint32_t, blocks->count);
        if (blocks->heads != 0)
        {
            memset(blocks->heads, 0xff, (uint64_t)blocks->count * sizeof(uint32_t));
        }
    }
    return blocks->heads != 0;
}

// The prefilter over the one-byte entries of sorted groups, in input order, with k content bytes, its key's last j
// bytes T_j and its sorted group g. Long level: P1 marks seen and twice-seen (g, T_8) for k >= 8, and P2 flags an
// entry whose (g, T_8) was seen twice. Short levels: P1 marks each receiver's (g, k, T_k) for 1 <= k <= 7; P2 flags a
// donor whose (g, j, T_j) is marked, for every receiver length j < k (j <= 7), marking it hit; P3 flags a receiver
// whose mark was hit. Every content with a proper extension in its group is flagged with its whole block, since its
// block shares its last min(k, 8) bytes. Empty strings are never flagged; those after their group's first one-byte
// entry take it as head. The flags are a bitset, gathered a register word at a time and compacted into the
// input-ordered list 16 entries a step; the empty strings fill the list from its back. The list comes first, so popping
// the bitmaps and the bitset leaves their pages to the heads and the sort.
static bool foreign_strings_flag(const ForeignStrings* strings, const uint8_t* const* pointers, const uint32_t* sections, ForeignStringsBlocks* blocks)
{
    Arena* arena = blocks->arena;
    const uint32_t input_count = strings->input_count;
    const uint32_t* section_begin = strings->section_begin;
    const uint32_t* lengths = strings->lengths;
    const uint32_t* section_groups = blocks->section_groups;
    const uint32_t* group_first = blocks->group_first;
    const uint32_t considered = blocks->considered;
    const uint32_t long_log2 = foreign_strings_filter_log2(blocks->long_count);
    const uint32_t short_log2 = foreign_strings_filter_log2(blocks->short_count);
    const uint64_t long_words = blocks->long_count != 0 ? (1ull << long_log2) / 64 : 0;
    const uint64_t short_words = blocks->short_count != 0 ? (1ull << short_log2) / 64 : 0;
    // A word past the last entry's, which a 16-entry step may read.
    const uint64_t flag_words = (uint64_t)blocks->count / 64 + 2;
    uint32_t* flagged = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, considered);
    const uint64_t mark = arena->length;
    uint64_t* seen = long_words != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, long_words) : 0;
    uint64_t* twice = long_words != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, long_words) : 0;
    uint64_t* receivers = short_words != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, short_words) : 0;
    uint64_t* hit = short_words != 0 ? BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, short_words) : 0;
    uint64_t* flags = BUSTER_ARENA_PUSH_ARRAY(arena, uint64_t, flag_words);
    bool valid = flagged != 0 && (long_words == 0 || (seen != 0 && twice != 0)) && (short_words == 0 || (receivers != 0 && hit != 0)) && flags != 0;
    if (valid && long_words != 0)
    {
        memset(seen, 0, long_words * sizeof(uint64_t));
        memset(twice, 0, long_words * sizeof(uint64_t));
    }
    if (valid && short_words != 0)
    {
        memset(receivers, 0, short_words * sizeof(uint64_t));
        memset(hit, 0, short_words * sizeof(uint64_t));
    }
    if (valid)
    {
        memset(flags, 0, flag_words * sizeof(uint64_t));
    }
    const uint64_t long_level = BUSTER_FOREIGN_STRINGS_KEY_BYTES * BUSTER_FOREIGN_STRINGS_FILTER_LEVEL;
    uint32_t present = 0;
    for (uint32_t section = 0; valid && section < input_count; section += 1)
    {
        const uint32_t group = section_groups[section];
        const uint64_t mix = (uint64_t)group * BUSTER_FOREIGN_STRINGS_FILTER_GROUP;
        const uint32_t end = group != BUSTER_FOREIGN_STRINGS_NONE ? section_begin[section + 1] : 0;
        for (uint32_t entry = section_begin[section]; entry < end; entry += 1)
        {
            const uint32_t content = lengths[entry] - 1;
            const uint64_t key = foreign_strings_key(pointers[entry], content, 0);
            if (content >= BUSTER_FOREIGN_STRINGS_KEY_BYTES)
            {
                const uint64_t slot = foreign_strings_filter_slot(key, mix + long_level, long_log2);
                const uint64_t bit = 1ull << (slot % 64);
                twice[slot / 64] |= seen[slot / 64] & bit;
                seen[slot / 64] |= bit;
            }
            else if (content != 0)
            {
                const uint64_t slot = foreign_strings_filter_slot(key >> (64 - 8 * content), mix + content * BUSTER_FOREIGN_STRINGS_FILTER_LEVEL, short_log2);
                receivers[slot / 64] |= 1ull << (slot % 64);
                present |= 1u << content;
            }
        }
    }
    uint32_t empties = 0;
    uint64_t word = 0;
    uint32_t word_index = 0;
    for (uint32_t section = 0; valid && section < input_count; section += 1)
    {
        const uint32_t group = section_groups[section];
        const uint64_t mix = (uint64_t)group * BUSTER_FOREIGN_STRINGS_FILTER_GROUP;
        const uint32_t first = group != BUSTER_FOREIGN_STRINGS_NONE ? group_first[group] : 0;
        const uint32_t end = group != BUSTER_FOREIGN_STRINGS_NONE ? section_begin[section + 1] : 0;
        for (uint32_t entry = section_begin[section]; entry < end; entry += 1)
        {
            const uint32_t content = lengths[entry] - 1;
            const uint64_t key = foreign_strings_key(pointers[entry], content, 0);
            uint64_t flag = 0;
            uint32_t levels = present & ((1u << (content < BUSTER_FOREIGN_STRINGS_KEY_BYTES ? content : BUSTER_FOREIGN_STRINGS_KEY_BYTES)) - 1);
            while (levels != 0)
            {
                const uint32_t level = (uint32_t)__builtin_ctz(levels);
                const uint64_t slot = foreign_strings_filter_slot(key >> (64 - 8 * level), mix + level * BUSTER_FOREIGN_STRINGS_FILTER_LEVEL, short_log2);
                const uint64_t found = receivers[slot / 64] & (1ull << (slot % 64));
                hit[slot / 64] |= found;
                flag |= found != 0 ? 1 : 0;
                levels &= levels - 1;
            }
            if (content >= BUSTER_FOREIGN_STRINGS_KEY_BYTES)
            {
                const uint64_t slot = foreign_strings_filter_slot(key, mix + long_level, long_log2);
                flag |= (twice[slot / 64] >> (slot % 64)) & 1;
            }
            if (entry / 64 != word_index)
            {
                flags[word_index] |= word;
                word = 0;
                word_index = entry / 64;
            }
            word |= flag << (entry % 64);
            if (content == 0 && first < entry)
            {
                empties += 1;
                flagged[considered - empties] = entry;
            }
        }
    }
    if (valid)
    {
        flags[word_index] |= word;
    }
    word = 0;
    word_index = 0;
    for (uint32_t section = 0; valid && present != 0 && section < input_count; section += 1)
    {
        const uint32_t group = section_groups[section];
        const uint64_t mix = (uint64_t)group * BUSTER_FOREIGN_STRINGS_FILTER_GROUP;
        const uint32_t end = group != BUSTER_FOREIGN_STRINGS_NONE ? section_begin[section + 1] : 0;
        for (uint32_t entry = section_begin[section]; entry < end; entry += 1)
        {
            const uint32_t content = lengths[entry] - 1;
            if (content != 0 && content < BUSTER_FOREIGN_STRINGS_KEY_BYTES)
            {
                const uint64_t key = foreign_strings_key(pointers[entry], content, 0);
                const uint64_t slot = foreign_strings_filter_slot(key >> (64 - 8 * content), mix + content * BUSTER_FOREIGN_STRINGS_FILTER_LEVEL, short_log2);
                if (entry / 64 != word_index)
                {
                    flags[word_index] |= word;
                    word = 0;
                    word_index = entry / 64;
                }
                word |= ((hit[slot / 64] >> (slot % 64)) & 1) << (entry % 64);
            }
        }
    }
    if (valid && present != 0)
    {
        flags[word_index] |= word;
    }
    const __m512i lanes = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    uint32_t flagged_count = 0;
    for (uint32_t section = 0; valid && section < input_count; section += 1)
    {
        const uint32_t end = section_groups[section] != BUSTER_FOREIGN_STRINGS_NONE ? section_begin[section + 1] : 0;
        for (uint32_t at = section_begin[section]; at < end; at += 16)
        {
            const uint32_t left = end - at;
            const uint32_t shift = at % 64;
            uint64_t bits = flags[at / 64] >> shift;
            bits |= shift > 48 ? flags[at / 64 + 1] << (64 - shift) : 0;
            const __mmask16 sorted = (__mmask16)(bits & (left >= 16 ? 0xffffu : (1u << left) - 1));
            const uint32_t taken = (uint32_t)__builtin_popcount(sorted);
            const __m512i entries = _mm512_maskz_compress_epi32(sorted, _mm512_add_epi32(lanes, _mm512_set1_epi32((int)at)));
            _mm512_mask_storeu_epi32(flagged + flagged_count, (__mmask16)((1u << taken) - 1), entries);
            flagged_count += taken;
        }
    }
    if (valid)
    {
        arena_pop_to(arena, mark);
    }
    valid = valid && (empties == 0 || foreign_strings_heads_ready(blocks));
    for (uint32_t index = 0; valid && index < empties; index += 1)
    {
        const uint32_t entry = flagged[considered - 1 - index];
        blocks->heads[entry] = group_first[section_groups[sections[entry]]];
        blocks->heads_written += 1;
    }
    blocks->flagged = flagged;
    blocks->flagged_count = valid ? flagged_count : 0;
    return valid;
}

// Stable insertion sort of `count` records by key.
static void foreign_strings_insert_keys(ForeignStringsRecord* records, uint32_t count)
{
    for (uint32_t index = 1; index < count; index += 1)
    {
        const ForeignStringsRecord moving = records[index];
        uint32_t at = index;
        while (at > 0 && records[at - 1].key > moving.key)
        {
            records[at] = records[at - 1];
            at -= 1;
        }
        records[at] = moving;
    }
}

// A line of zeros, which a load masked to no lanes reads in place of records that nothing may have written
// (docs/coding-style.md: such a load at an untouched page still costs as if it read it).
static const uint64_t foreign_strings_zero_line[8] __attribute__((aligned(64)));

// `words` where `mask` keeps a lane, else foreign_strings_zero_line, by a mask rather than a branch.
static const uint64_t* foreign_strings_line(const uint64_t* words, __mmask8 mask)
{
    const uintptr_t keep = (uintptr_t)0 - (uintptr_t)(mask != 0);
    return (const uint64_t*)(((uintptr_t)words & keep) | ((uintptr_t)foreign_strings_zero_line & ~keep));
}

// The masks of a step of 8 records read as two 8-word loads: 16 bytes a record, `count` records from the first.
static __mmask8 foreign_strings_low_words(uint32_t count)
{
    return (__mmask8)(count >= 4 ? 0xffu : (1u << (2 * count)) - 1);
}

static __mmask8 foreign_strings_high_words(uint32_t count)
{
    return (__mmask8)(count > 4 ? (1u << (2 * (count - 4))) - 1 : 0);
}

// The key bits in which any of `count` records differs from the first, 8 records a step: their nonzero bytes are the
// digits a radix pass must order.
static uint64_t foreign_strings_varying(const ForeignStringsRecord* records, uint32_t count)
{
    const __m512i keys_of = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
    const __m512i first = _mm512_set1_epi64((long long)records[0].key);
    __m512i varying = _mm512_setzero_si512();
    for (uint32_t at = 0; at < count; at += 8)
    {
        const uint32_t left = count - at < 8 ? count - at : 8;
        const __mmask8 high = foreign_strings_high_words(left);
        const uint64_t* words = (const uint64_t*)(const void*)(records + at);
        const __m512i low_words = _mm512_maskz_loadu_epi64(foreign_strings_low_words(left), words);
        const __m512i high_words = _mm512_maskz_loadu_epi64(high, foreign_strings_line(words + 8, high));
        const __m512i keys = _mm512_permutex2var_epi64(low_words, keys_of, high_words);
        varying = _mm512_or_si512(varying, _mm512_maskz_xor_epi64((__mmask8)((1u << left) - 1), keys, first));
    }
    return (uint64_t)_mm512_reduce_or_epi64(varying);
}

// Stable MSD radix sort of `count` records by key, ping-ponging between them and `spare`, from an explicit work list
// of disjoint ranges of more than BUSTER_FOREIGN_STRINGS_INSERTION records (so at most count / 33 at once). A vector
// pass finds the digits in which a range's keys vary. A range with none is done; any other is partitioned by its
// highest varying digit, counted and scattered into the other buffer, its larger buckets listed and the others sorted
// by insertion, each back in `records`. A record takes at most 8 partitions, one per digit that varies in its range.
static void foreign_strings_radix(ForeignStringsRecord* records, ForeignStringsRecord* spare, uint32_t count, ForeignStringsRange* ranges)
{
    uint32_t range_count = 0;
    if (count > BUSTER_FOREIGN_STRINGS_INSERTION)
    {
        ranges[0] = (ForeignStringsRange){
            .begin = 0,
            .end = count,
            .spare = false,
        };
        range_count = 1;
    }
    else
    {
        foreign_strings_insert_keys(records, count);
    }
    while (range_count != 0)
    {
        range_count -= 1;
        const ForeignStringsRange range = ranges[range_count];
        const uint32_t size = range.end - range.begin;
        ForeignStringsRecord* from = (range.spare ? spare : records) + range.begin;
        ForeignStringsRecord* to = (range.spare ? records : spare) + range.begin;
        const uint64_t varying = foreign_strings_varying(from, size);
        if (varying == 0 && range.spare)
        {
            memcpy(to, from, (uint64_t)size * sizeof(ForeignStringsRecord));
        }
        if (varying != 0)
        {
            const uint32_t shift = 56 - 8 * ((uint32_t)_lzcnt_u64(varying) / 8);
            uint32_t histogram[256];
            memset(histogram, 0, sizeof(histogram));
            for (uint32_t index = 0; index < size; index += 1)
            {
                histogram[(from[index].key >> shift) & 255] += 1;
            }
            uint32_t offsets[256];
            uint32_t sum = 0;
            for (uint32_t bucket = 0; bucket < 256; bucket += 1)
            {
                offsets[bucket] = sum;
                sum += histogram[bucket];
            }
            for (uint32_t index = 0; index < size; index += 1)
            {
                const uint32_t bucket = (uint32_t)(from[index].key >> shift) & 255;
                to[offsets[bucket]] = from[index];
                offsets[bucket] += 1;
            }
            uint32_t begin = 0;
            for (uint32_t bucket = 0; bucket < 256; bucket += 1)
            {
                const uint32_t members = histogram[bucket];
                if (members > BUSTER_FOREIGN_STRINGS_INSERTION)
                {
                    ranges[range_count] = (ForeignStringsRange){
                        .begin = range.begin + begin,
                        .end = range.begin + begin + members,
                        .spare = !range.spare,
                    };
                    range_count += 1;
                }
                else
                {
                    // A bucket that landed in `spare` returns before its insertion sort.
                    if (!range.spare)
                    {
                        memcpy(from + begin, to + begin, (uint64_t)members * sizeof(ForeignStringsRecord));
                    }
                    foreign_strings_insert_keys(records + range.begin + begin, members);
                }
                begin += members;
            }
        }
    }
}

// The common suffix of each sorted neighbour pair whose keys hold the bytes after the `depth` both share: for p in
// (begin, end), min(depth + the leading equal key bytes, both contents), 8 records a step.
static void foreign_strings_lcs(const ForeignStringsRecord* records, uint32_t* lcs, uint32_t begin, uint32_t end, uint32_t depth)
{
    const __m512i keys_of = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
    const __m512i entries_of = _mm512_setr_epi64(1, 3, 5, 7, 9, 11, 13, 15);
    const __m512i known = _mm512_set1_epi64(depth);
    for (uint32_t at = begin + 1; at < end; at += 8)
    {
        const uint32_t count = end - at < 8 ? end - at : 8;
        const __mmask8 low = foreign_strings_low_words(count);
        const __mmask8 high = foreign_strings_high_words(count);
        const uint64_t* current = (const uint64_t*)(const void*)(records + at);
        const uint64_t* previous = (const uint64_t*)(const void*)(records + at - 1);
        const __m512i current_low = _mm512_maskz_loadu_epi64(low, current);
        const __m512i current_high = _mm512_maskz_loadu_epi64(high, foreign_strings_line(current + 8, high));
        const __m512i previous_low = _mm512_maskz_loadu_epi64(low, previous);
        const __m512i previous_high = _mm512_maskz_loadu_epi64(high, foreign_strings_line(previous + 8, high));
        const __m512i keys = _mm512_permutex2var_epi64(current_low, keys_of, current_high);
        const __m512i previous_keys = _mm512_permutex2var_epi64(previous_low, keys_of, previous_high);
        const __m512i contents = _mm512_srli_epi64(_mm512_permutex2var_epi64(current_low, entries_of, current_high), 32);
        const __m512i previous_contents = _mm512_srli_epi64(_mm512_permutex2var_epi64(previous_low, entries_of, previous_high), 32);
        const __m512i equal = _mm512_srli_epi64(_mm512_lzcnt_epi64(_mm512_xor_si512(keys, previous_keys)), 3);
        __m512i common = _mm512_add_epi64(known, equal);
        common = _mm512_min_epu64(common, contents);
        common = _mm512_min_epu64(common, previous_contents);
        _mm256_mask_storeu_epi32(lcs + at, (__mmask8)((1u << count) - 1), _mm512_cvtepi64_epi32(common));
    }
}

// The reversed order of two contents whose last `depth` bytes are equal (-1, 0 or 1; a proper suffix first) and their
// common suffix's length, 64 bytes a step from the terminator outward. Every load starts at or after its entry's
// first byte.
static int32_t foreign_strings_compare(const uint8_t* a, uint32_t a_content, const uint8_t* b, uint32_t b_content, uint32_t depth, uint32_t* common)
{
    uint32_t known = depth;
    int32_t order = 2;
    while (order == 2)
    {
        const uint32_t a_left = a_content - known;
        const uint32_t b_left = b_content - known;
        const uint32_t shorter = a_left < b_left ? a_left : b_left;
        const uint32_t width = shorter < 64 ? shorter : 64;
        if (width == 0)
        {
            order = a_left == b_left ? 0 : a_left < b_left ? -1 : 1;
        }
        else
        {
            const __mmask64 valid = width == 64 ? ~0ull : (1ull << width) - 1;
            const __m512i a_bytes = _mm512_maskz_loadu_epi8(valid, a + a_left - width);
            const __m512i b_bytes = _mm512_maskz_loadu_epi8(valid, b + b_left - width);
            const uint64_t differ = _mm512_mask_cmpneq_epi8_mask(valid, a_bytes, b_bytes);
            if (differ != 0)
            {
                const uint32_t lane = 63 - (uint32_t)__builtin_clzll(differ);
                known += width - 1 - lane;
                order = a[a_left - width + lane] < b[b_left - width + lane] ? -1 : 1;
            }
            else
            {
                known += width;
            }
        }
    }
    *common = known;
    return order;
}

// A tie of at most BUSTER_FOREIGN_STRINGS_INSERTION records: a stable insertion sort by the comparison past `depth`,
// then its neighbours' common suffixes.
static void foreign_strings_insert_contents(ForeignStringsRecord* records, uint32_t* lcs, uint32_t begin, uint32_t end, uint32_t depth, const uint8_t* const* pointers)
{
    for (uint32_t index = begin + 1; index < end; index += 1)
    {
        const ForeignStringsRecord moving = records[index];
        uint32_t at = index;
        uint32_t common = 0;
        while (at > begin && foreign_strings_compare(pointers[records[at - 1].entry], records[at - 1].content, pointers[moving.entry], moving.content, depth, &common) > 0)
        {
            records[at] = records[at - 1];
            at -= 1;
        }
        records[at] = moving;
    }
    for (uint32_t index = begin + 1; index < end; index += 1)
    {
        uint32_t common = 0;
        foreign_strings_compare(pointers[records[index - 1].entry], records[index - 1].content, pointers[records[index].entry], records[index].content, depth, &common);
        lcs[index] = common;
    }
}

// The common suffix of two contents whose last `depth` bytes are equal, counted up to `limit` (at least `depth`, at
// most the shorter content), 64 bytes a step from the terminator outward. Every load starts at or after its entry's
// first byte.
static uint32_t foreign_strings_common(const uint8_t* a, uint32_t a_content, const uint8_t* b, uint32_t b_content, uint32_t depth, uint32_t limit)
{
    uint32_t known = depth;
    uint32_t width = limit - known < 64 ? limit - known : 64;
    while (width != 0)
    {
        const __mmask64 valid = width == 64 ? ~0ull : (1ull << width) - 1;
        const __m512i a_bytes = _mm512_maskz_loadu_epi8(valid, a + a_content - known - width);
        const __m512i b_bytes = _mm512_maskz_loadu_epi8(valid, b + b_content - known - width);
        const uint64_t differ = _mm512_mask_cmpneq_epi8_mask(valid, a_bytes, b_bytes);
        // The equal bytes nearest the known tail: the window's top lanes, up to its highest differing one.
        const uint32_t equal = (uint32_t)_lzcnt_u64(differ << (64 - width));
        known += equal < width ? equal : width;
        width = equal < width ? 0 : limit - known < 64 ? limit - known : 64;
    }
    return known;
}

// Lists a tie [begin, end) when a member is longer than the `known` bytes its members share; otherwise they are all
// one content, whose neighbours' common suffixes are already their contents.
static uint32_t foreign_strings_run(const ForeignStringsRecord* records, uint32_t begin, uint32_t end, uint32_t known, ForeignStringsRun* runs, uint32_t run_count)
{
    uint32_t longest = 0;
    for (uint32_t index = begin; index < end; index += 1)
    {
        longest = records[index].content > longest ? records[index].content : longest;
    }
    if (longest > known)
    {
        runs[run_count] = (ForeignStringsRun){
            .begin = begin,
            .end = end,
            .depth = known,
        };
        run_count += 1;
    }
    return run_count;
}

// Lists the maximal ties among sorted positions [begin, end) that the keys past `depth` leave: neighbours whose common
// suffix is depth + BUSTER_FOREIGN_STRINGS_KEY_BYTES, 16 positions a step.
static uint32_t foreign_strings_runs(const ForeignStringsRecord* records, const uint32_t* lcs, uint32_t begin, uint32_t end, uint32_t depth, ForeignStringsRun* runs,
                                     uint32_t run_count)
{
    const uint32_t known = depth + BUSTER_FOREIGN_STRINGS_KEY_BYTES;
    const __m512i tied = _mm512_set1_epi32((int)known);
    uint32_t open = BUSTER_FOREIGN_STRINGS_NONE;
    uint32_t last = 0;
    for (uint32_t at = begin + 1; at < end; at += 16)
    {
        const uint32_t left = end - at;
        const __mmask16 valid = (__mmask16)(left >= 16 ? 0xffffu : (1u << left) - 1);
        uint32_t ties = _mm512_mask_cmpeq_epi32_mask(valid, _mm512_maskz_loadu_epi32(valid, lcs + at), tied);
        while (ties != 0)
        {
            const uint32_t position = at + (uint32_t)__builtin_ctz(ties);
            if (open != BUSTER_FOREIGN_STRINGS_NONE && last + 1 != position)
            {
                run_count = foreign_strings_run(records, open, last + 1, known, runs, run_count);
                open = BUSTER_FOREIGN_STRINGS_NONE;
            }
            open = open == BUSTER_FOREIGN_STRINGS_NONE ? position - 1 : open;
            last = position;
            ties &= ties - 1;
        }
    }
    if (open != BUSTER_FOREIGN_STRINGS_NONE)
    {
        run_count = foreign_strings_run(records, open, last + 1, known, runs, run_count);
    }
    return run_count;
}

// Orders the listed ties beyond the bytes their members share, from an explicit work list (its runs are disjoint, so
// it holds at most half the records). A tie of at most BUSTER_FOREIGN_STRINGS_INSERTION sorts by comparison. A larger
// one first finds its members' common tail, each compared with its first member up to
// BUSTER_FOREIGN_STRINGS_COMMON_SPAN bytes past its depth, independently of the others: a tie of one content only sets
// its common suffixes; any other takes the key bytes past that tail, sorts by them and lists the ties they leave. A
// member that ends at the tail or differs in the next byte splits the tie, so a level splits it unless all its members
// share the whole span, and the next level starts at least 8 bytes deeper.
static void foreign_strings_refine(ForeignStringsRecord* records, ForeignStringsRecord* spare, uint32_t* lcs, ForeignStringsRun* runs, uint32_t run_count,
                                   ForeignStringsRange* ranges, const uint8_t* const* pointers)
{
    while (run_count != 0)
    {
        run_count -= 1;
        const ForeignStringsRun run = runs[run_count];
        const uint32_t count = run.end - run.begin;
        const ForeignStringsRecord first = records[run.begin];
        const uint32_t bound = first.content - run.depth < BUSTER_FOREIGN_STRINGS_COMMON_SPAN ? first.content : run.depth + BUSTER_FOREIGN_STRINGS_COMMON_SPAN;
        uint32_t common = first.content;
        bool one_length = true;
        for (uint32_t index = run.begin + 1; count > BUSTER_FOREIGN_STRINGS_INSERTION && index < run.end; index += 1)
        {
            const uint32_t content = records[index].content;
            const uint32_t shared = foreign_strings_common(pointers[first.entry], first.content, pointers[records[index].entry], content, run.depth, content < bound ? content : bound);
            common = shared < common ? shared : common;
            one_length = one_length && content == first.content;
        }
        if (count <= BUSTER_FOREIGN_STRINGS_INSERTION)
        {
            foreign_strings_insert_contents(records, lcs, run.begin, run.end, run.depth, pointers);
        }
        else if (one_length && common == first.content)
        {
            for (uint32_t index = run.begin + 1; index < run.end; index += 1)
            {
                lcs[index] = records[index].content;
            }
        }
        else
        {
            for (uint32_t index = run.begin; index < run.end; index += 1)
            {
                records[index].key = foreign_strings_key(pointers[records[index].entry], records[index].content, common);
            }
            foreign_strings_radix(records + run.begin, spare + run.begin, count, ranges);
            foreign_strings_lcs(records, lcs, run.begin, run.end, common);
            run_count = foreign_strings_runs(records, lcs, run.begin, run.end, common, runs, run_count);
        }
    }
}

// The stack pass over the sorted records, whose open classes have strictly increasing contents: a neighbour's common
// suffix below a class's content closes it, giving the class below its block's earliest entry; an equal neighbour
// joins its class; any other opens one. A closed class whose block's earliest entry is not its own first writes it
// as every member's head. A slice's first position has a common suffix of zero, which closes every class at a group
// boundary (contents are at least one byte), as the end does.
static bool foreign_strings_heads(ForeignStringsBlocks* blocks, const ForeignStringsRecord* records, const uint32_t* lcs, uint32_t total, ForeignStringsClass* stack)
{
    bool valid = true;
    uint32_t depth = 0;
    for (uint32_t position = 0; valid && position <= total; position += 1)
    {
        const uint32_t boundary = position < total ? lcs[position] : 0;
        while (valid && depth != 0 && stack[depth - 1].content > boundary)
        {
            depth -= 1;
            const ForeignStringsClass closed = stack[depth];
            // Its best is an extension's only when one closed into it, which sorts after its members, so the first
            // longer content ends them before the slice does.
            if (closed.best < closed.own)
            {
                valid = foreign_strings_heads_ready(blocks);
                for (uint32_t at = closed.begin; valid && records[at].content == closed.content; at += 1)
                {
                    blocks->heads[records[at].entry] = closed.best;
                    blocks->heads_written += 1;
                }
            }
            if (depth != 0 && closed.best < stack[depth - 1].best)
            {
                stack[depth - 1].best = closed.best;
            }
        }
        if (position < total)
        {
            const uint32_t entry = records[position].entry;
            const uint32_t content = records[position].content;
            if (depth != 0 && stack[depth - 1].content == boundary && boundary == content)
            {
                ForeignStringsClass* top = &stack[depth - 1];
                top->best = entry < top->best ? entry : top->best;
                top->own = entry < top->own ? entry : top->own;
            }
            else
            {
                stack[depth] = (ForeignStringsClass){
                    .content = content,
                    .best = entry,
                    .own = entry,
                    .begin = position,
                };
                depth += 1;
            }
        }
    }
    return valid;
}

// Sorts the flagged entries by reversed content within their sorted groups (a stable counting partition, skipped for
// one group), then runs the head pass. A slice of at most BUSTER_FOREIGN_STRINGS_INSERTION sorts by insertion, a
// larger one by radix; ties past the first key bytes go to foreign_strings_refine.
static bool foreign_strings_sort(const ForeignStrings* strings, const uint8_t* const* pointers, const uint32_t* sections, ForeignStringsBlocks* blocks)
{
    Arena* arena = blocks->arena;
    const uint32_t total = blocks->flagged_count;
    const uint32_t group_count = blocks->sorted_groups;
    const uint32_t* flagged = blocks->flagged;
    const uint32_t* section_groups = blocks->section_groups;
    ForeignStringsRecord* records = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignStringsRecord, total);
    ForeignStringsRecord* spare = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignStringsRecord, total);
    uint32_t* lcs = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, total);
    ForeignStringsRun* runs = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignStringsRun, (uint64_t)total / 2 + 1);
    ForeignStringsRange* ranges = BUSTER_ARENA_PUSH_ARRAY(arena, ForeignStringsRange, (uint64_t)total / (BUSTER_FOREIGN_STRINGS_INSERTION + 1) + 1);
    uint32_t* slices = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, 2 * (uint64_t)group_count + 1);
    bool valid = records != 0 && spare != 0 && lcs != 0 && runs != 0 && ranges != 0 && slices != 0;
    if (valid)
    {
        uint32_t* next = slices + group_count + 1;
        if (group_count == 1)
        {
            slices[0] = 0;
            slices[1] = total;
        }
        else
        {
            memset(slices, 0, ((uint64_t)group_count + 1) * sizeof(uint32_t));
            for (uint32_t index = 0; index < total; index += 1)
            {
                slices[section_groups[sections[flagged[index]]] + 1] += 1;
            }
            for (uint32_t group = 0; group < group_count; group += 1)
            {
                slices[group + 1] += slices[group];
            }
        }
        memcpy(next, slices, (uint64_t)group_count * sizeof(uint32_t));
        for (uint32_t index = 0; index < total; index += 1)
        {
            const uint32_t entry = flagged[index];
            const uint32_t group = group_count == 1 ? 0 : section_groups[sections[entry]];
            const uint32_t content = strings->lengths[entry] - 1;
            records[next[group]] = (ForeignStringsRecord){
                .key = foreign_strings_key(pointers[entry], content, 0),
                .entry = entry,
                .content = content,
            };
            next[group] += 1;
        }
        for (uint32_t group = 0; group < group_count; group += 1)
        {
            const uint32_t begin = slices[group];
            const uint32_t end = slices[group + 1];
            foreign_strings_radix(records + begin, spare + begin, end - begin, ranges);
            if (end != begin)
            {
                lcs[begin] = 0;
            }
            foreign_strings_lcs(records, lcs, begin, end, 0);
            const uint32_t run_count = foreign_strings_runs(records, lcs, begin, end, 0, runs, 0);
            foreign_strings_refine(records, spare, lcs, runs, run_count, ranges, pointers);
        }
        valid = foreign_strings_heads(blocks, records, lcs, total, (ForeignStringsClass*)(void*)spare);
    }
    return valid;
}

// Block heads of the one-byte entries (#3044), in scratch: none when no group is sorted, and blocks->heads stays null
// unless a head is written.
static bool foreign_strings_block_heads(const ForeignStringInput* inputs, const ForeignStrings* strings, const uint8_t* const* pointers, const uint32_t* sections,
                                        ForeignStringsBlocks* blocks)
{
    bool valid = foreign_strings_groups(inputs, strings->input_count, strings, blocks);
    if (valid && blocks->sorted_groups != 0)
    {
        valid = foreign_strings_flag(strings, pointers, sections, blocks);
    }
    if (valid && blocks->flagged_count > 1)
    {
        valid = foreign_strings_sort(strings, pointers, sections, blocks);
    }
    return valid;
}

static ForeignStringsError foreign_strings_intern(const ForeignStringInput* inputs, ForeignStrings* strings, const uint8_t* const* pointers,
                                                  const uint32_t* sections, const uint64_t* hashes, uint32_t* table, uint64_t slots, const uint32_t* heads)
{
    ForeignStringsError error = FOREIGN_STRINGS_ERROR_NONE;
    uint64_t size = 0;
    for (uint32_t entry = 0; error == FOREIGN_STRINGS_ERROR_NONE && entry < strings->count; entry += 1)
    {
        const ForeignStringInput* input = &inputs[sections[entry]];
        const uint32_t offset = strings->input_offset[entry];
        const uint32_t trailing = offset != 0 ? (uint32_t)__builtin_ctz(offset) : 32;
        const uint32_t alignment = 1u << (trailing < input->alignment_log2 ? trailing : input->alignment_log2);
        uint64_t slot = (hashes[entry] ^ ((uint64_t)input->group * 0x9e3779b97f4a7c15ull)) & (slots - 1);
        bool same = false;
        while (table[slot] != 0 && !same)
        {
            const uint32_t prior = table[slot] - 1;
            same = hashes[prior] == hashes[entry] && inputs[sections[prior]].group == input->group && inputs[sections[prior]].unit_log2 == input->unit_log2 &&
                   strings->lengths[prior] == strings->lengths[entry] && (strings->output_offset[prior] & (alignment - 1)) == 0 &&
                   memcmp(pointers[prior], pointers[entry], strings->lengths[entry]) == 0;
            if (!same)
            {
                slot = (slot + 1) & (slots - 1);
            }
        }
        // A one-byte entry that no suitable owner equals takes its block head's end where that meets its alignment
        // (#3044). A head is an earlier owner; every other entry's is BUSTER_FOREIGN_STRINGS_NONE.
        const uint32_t head = !same && heads != 0 ? heads[entry] : BUSTER_FOREIGN_STRINGS_NONE;
        const uint32_t shared = head < entry ? strings->output_offset[head] + strings->lengths[head] - strings->lengths[entry] : 0;
        const bool suffix = head < entry && (shared & (alignment - 1)) == 0;
        if (same)
        {
            strings->output_offset[entry] = strings->output_offset[table[slot] - 1];
        }
        else if (suffix)
        {
            strings->output_offset[entry] = shared;
        }
        else
        {
            const uint64_t aligned = (size + alignment - 1) & ~(uint64_t)(alignment - 1);
            const uint64_t end = aligned + strings->lengths[entry];
            if (end > UINT32_MAX)
            {
                error = FOREIGN_STRINGS_ERROR_CAPACITY;
            }
            else
            {
                strings->output_offset[entry] = (uint32_t)aligned;
                size = end;
                table[slot] = entry + 1;
            }
        }
    }
    strings->size = error == FOREIGN_STRINGS_ERROR_NONE ? (uint32_t)size : 0;
    return error;
}

static bool foreign_strings_path_valid(ForeignPath path)
{
    return path == FOREIGN_PATH_VECTOR || path == FOREIGN_PATH_EXACT;
}

// The final linker needs the pool only below its original byte count. Finish interning first: alignment can grow
// the pool and offset overflow is an error even if the linker would discard it. An empty result reports no saving.
// Without `suffixes` entries share whole entries only (no block heads): the consumer passes false when it refuses
// every pool (foreign_strings_refused), so that it pays for no head it would discard (#3044).
static ForeignStringsError foreign_strings_build_below(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, Arena* arena, ForeignStrings* output, uint64_t limit,
                                                       bool suffixes)
{
    ForeignStringsError error = arena == 0 || output == 0 || (input_count != 0 && inputs == 0) || !foreign_strings_path_valid(path) ?
                                FOREIGN_STRINGS_ERROR_INPUT : FOREIGN_STRINGS_ERROR_NONE;
    const uint64_t start = arena != 0 ? arena->length : 0;
    ForeignStrings strings = { .input_count = input_count };
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        error = foreign_strings_count(inputs, input_count, path, &strings.count);
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        strings.section_begin = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)input_count + 1);
        strings.input_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
        strings.output_offset = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
        strings.lengths = BUSTER_ARENA_PUSH_ARRAY(arena, uint32_t, (uint64_t)strings.count + 1);
        error = strings.section_begin == 0 || strings.input_offset == 0 || strings.output_offset == 0 || strings.lengths == 0 ?
                FOREIGN_STRINGS_ERROR_MEMORY : FOREIGN_STRINGS_ERROR_NONE;
    }
    Scratch scratch = scratch_begin(arena);
    const uint8_t** pointers = 0;
    uint32_t* sections = 0;
    uint64_t* hashes = 0;
    uint32_t* table = 0;
    uint64_t slots = 1;
    while (slots < 2 * (uint64_t)strings.count)
    {
        slots *= 2;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        pointers = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, const uint8_t*, (uint64_t)strings.count + 1);
        sections = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, (uint64_t)strings.count + 1);
        hashes = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint64_t, (uint64_t)strings.count + 1);
        table = BUSTER_ARENA_PUSH_ARRAY(scratch.arena, uint32_t, slots);
        error = pointers == 0 || sections == 0 || hashes == 0 || table == 0 ? FOREIGN_STRINGS_ERROR_MEMORY : FOREIGN_STRINGS_ERROR_NONE;
    }
    ForeignStringsBlocks blocks = {
        .arena = scratch.arena,
        .count = strings.count,
    };
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        foreign_strings_entries(inputs, input_count, path, &strings, pointers, sections);
        if (path == FOREIGN_PATH_VECTOR)
        {
            link_hash_names(pointers, strings.lengths, strings.count, hashes);
        }
        else
        {
            #pragma clang loop vectorize(disable) interleave(disable)
            for (uint32_t entry = 0; entry < strings.count; entry += 1)
            {
                hashes[entry] = link_hash(pointers[entry], strings.lengths[entry]);
            }
        }
        memset(table, 0, slots * sizeof(uint32_t));
        error = !suffixes || foreign_strings_block_heads(inputs, &strings, pointers, sections, &blocks) ? FOREIGN_STRINGS_ERROR_NONE : FOREIGN_STRINGS_ERROR_MEMORY;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        error = foreign_strings_intern(inputs, &strings, pointers, sections, hashes, table, slots, blocks.heads);
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE && strings.size < limit)
    {
        strings.bytes = BUSTER_ARENA_PUSH_ARRAY(arena, uint8_t, (uint64_t)strings.size + 1);
        error = strings.bytes == 0 ? FOREIGN_STRINGS_ERROR_MEMORY : FOREIGN_STRINGS_ERROR_NONE;
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE && strings.bytes != 0)
    {
        memset(strings.bytes, 0, strings.size);
        // Only table residents own bytes; a suffix's are its owner's. Copies are disjoint, including when equal strings
        // need different alignment.
        for (uint64_t slot = 0; slot < slots; slot += 1)
        {
            if (table[slot] != 0)
            {
                const uint32_t entry = table[slot] - 1;
                memcpy(strings.bytes + strings.output_offset[entry], pointers[entry], strings.lengths[entry]);
            }
        }
    }
    scratch_end(scratch);
    if (arena != 0 && (error != FOREIGN_STRINGS_ERROR_NONE || strings.bytes == 0))
    {
        arena_pop_to(arena, start);
    }
    if (error == FOREIGN_STRINGS_ERROR_NONE)
    {
        *output = strings.bytes != 0 ? strings : (ForeignStrings){ 0 };
    }
    return error;
}

static ForeignStringsError foreign_strings_build(const ForeignStringInput* inputs, uint32_t input_count, ForeignPath path, Arena* arena, ForeignStrings* output)
{
    return foreign_strings_build_below(inputs, input_count, path, arena, output, UINT64_MAX, true);
}

static bool foreign_strings_entry(const ForeignStrings* strings, uint32_t input, uint64_t offset, uint32_t* entry)
{
    bool found = strings != 0 && entry != 0 && input < strings->input_count && offset <= UINT32_MAX;
    uint32_t first = found ? strings->section_begin[input] : 0;
    uint32_t last = found ? strings->section_begin[input + 1] : 0;
    const uint32_t begin = first;
    while (first < last)
    {
        const uint32_t middle = first + (last - first) / 2;
        if (strings->input_offset[middle] <= offset)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    found = found && first > begin;
    if (found)
    {
        const uint32_t at = first - 1;
        const uint64_t within = offset - strings->input_offset[at];
        found = within < strings->lengths[at];
        if (found)
        {
            *entry = at;
        }
    }
    return found;
}

static bool foreign_strings_offset(const ForeignStrings* strings, uint32_t input, uint64_t offset, uint32_t* mapped)
{
    uint32_t entry = 0;
    const bool found = mapped != 0 && foreign_strings_entry(strings, input, offset, &entry);
    if (found)
    {
        *mapped = strings->output_offset[entry] + (uint32_t)(offset - strings->input_offset[entry]);
    }
    return found;
}
