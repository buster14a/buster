// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Shared association validation for the expression and declaration-time _Generic readers.
#define BUSTER_CHECK_GENERIC_GROUP 16u

// Each association belongs to its precise list. Integers/enums and ordinary/exceptional function
// prototypes also publish a list for queries from the other side of their compatibility relation.
typedef struct CheckGenericKeys CheckGenericKeys;
struct CheckGenericKeys
{
    uint32_t precise;
    uint32_t cross;
};

typedef enum CheckGenericRole
{
    CHECK_GENERIC_SINGLE,
    CHECK_GENERIC_ORDINARY,
    CHECK_GENERIC_ALTERNATE,
} CheckGenericRole;

typedef struct CheckGenericRow CheckGenericRow;
struct CheckGenericRow
{
    TypeId type;
    uint32_t next_precise;
    uint32_t next_cross;
};

typedef struct CheckGenericSet CheckGenericSet;
struct CheckGenericSet
{
    uint32_t* keys;
    uint32_t* tails;
    CheckGenericRow* rows;
    TypeId* scan_types;
    TypeId control;
    CheckGenericKeys control_keys;
    uint32_t count;
    uint32_t capacity;
    uint32_t source_position;
    uint64_t duplicate_queries;
    uint64_t control_queries;
    TypeId local_keys[BUSTER_CHECK_GENERIC_GROUP];
    TypeId local_types[BUSTER_CHECK_GENERIC_GROUP];
};

typedef struct CheckGenericPath CheckGenericPath;
struct CheckGenericPath
{
    uint32_t key;
    TypeId leaf;
    TypeId function;
};

// Follow the ordinary compatibility edges. A function's parameters are handled separately. At the
// depth limit the unexamined suffix contributes zero. Alignment, bounds and qualifiers stay exact.
static CheckGenericPath check_generic_path(const TypeTable* types, TypeId type, uint32_t limit)
{
    TypeId function = 0;
    uint32_t path = 0;
    type = type_strip_aligned(types, type);
    uint8_t kind = type_kind(types, type);
    while (limit != 0 && (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_ATOMIC || kind == BUSTER_TYPE_KIND_FUNCTION || type_kind_is_array(kind)))
    {
        if (kind == BUSTER_TYPE_KIND_FUNCTION && function == 0)
        {
            function = type;
        }
        limit -= 1;
        path = path * UINT32_C(0x9e3779b9) + type_merge_class(kind);
        type = type_strip_aligned(types, types->types.base[type_index(type)]);
        kind = type_kind(types, type);
    }
    const TypeId leaf = type_unqualified(type);
    uint32_t key = kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM ? 1 : leaf;
    if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR)
    {
        const uint32_t index = type_index(type);
        const uint64_t extent = types->types.extent[index];
        key = types->types.base[index] * UINT32_C(0x9e3779b9) ^ (uint32_t)extent ^ (uint32_t)(extent >> 32);
    }
    return (CheckGenericPath){
        .key = path * UINT32_C(0x9e3779b9) + (limit != 0 ? key : 0),
        .leaf = leaf,
        .function = function,
    };
}

// A necessary key for every compatible pair, also used by the diagnostic replay on invalid selections.
static uint32_t check_generic_key(const TypeTable* types, TypeId type)
{
    const uint32_t key = check_generic_path(types, type, UINT32_MAX).key;
    return key != 0 ? key : 1;
}

static CheckGenericKeys check_generic_keys(const TypeTable* types, TypeId type)
{
    const CheckGenericPath path = check_generic_path(types, type, UINT32_MAX);
    const uint32_t group = path.key;
    const TypeId function = path.function;
    const uint8_t kind = type_kind(types, path.leaf);
    uint32_t detail = group;
    CheckGenericRole role = CHECK_GENERIC_SINGLE;
    if (function != 0)
    {
        const uint8_t flags = types->types.flags[type_index(function)];
        role = (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0 ? CHECK_GENERIC_ORDINARY : CHECK_GENERIC_ALTERNATE;
        uint32_t count = 0;
        const TypeId* parameters = type_parameters(types, function, &count);
        detail = (detail * 31u + count) * 2u + ((flags & BUSTER_TYPE_FLAG_VARIADIC) != 0);
        // Sample the first and last adjusted parameters, up to 16 wrappers each. Any directly passed union here could acquire a
        // transparent attribute later. Its members, like an old-style function's parameters, need exact lookup.
        for (uint32_t sample = 0; role == CHECK_GENERIC_ORDINARY && sample < count && sample < 2; sample += 1)
        {
            const TypeId parameter = type_strip_aligned(types, parameters[sample == 0 ? 0 : count - 1]);
            if (type_kind(types, parameter) == BUSTER_TYPE_KIND_UNION)
            {
                role = CHECK_GENERIC_ALTERNATE;
            }
            else
            {
                detail = detail * UINT32_C(0x9e3779b9) + check_generic_path(types, parameter, 16).key;
            }
        }
        if (role == CHECK_GENERIC_ALTERNATE)
        {
            detail = group;
        }
    }
    else if (kind == BUSTER_TYPE_KIND_INTEGER || kind == BUSTER_TYPE_KIND_ENUM)
    {
        // Two enums require the same nominal tag. Two integers require the same builtin. Compare across
        // the two lists at query time, so an earlier enum's completion cannot invalidate either index.
        role = kind == BUSTER_TYPE_KIND_INTEGER ? CHECK_GENERIC_ORDINARY : CHECK_GENERIC_ALTERNATE;
        detail = detail * UINT32_C(0x9e3779b9) + path.leaf;
    }
    // The low bits keep the lists disjoint even on a hash collision. Precise lists end in 1, 2 or 3.
    // Cross lists end in 4 or 5, so flipping bit zero looks up the other role's list in the same group.
    return (CheckGenericKeys){
        .precise = (detail << 3) | (role + 1),
        .cross = role != CHECK_GENERIC_SINGLE ? (group << 3) | (role + 3) : 0,
    };
}

// A selection's associations after its control, in [first, end): its top-level commas and one. A group is skipped to its
// closing token, as the readers' punctuation scans skip it, so each association they read ends at a comma counted here.
// The set is sized by these, not by the list's tokens, among which are every nested selection's: sized by its tokens, a
// chain of n selections, one in each other's last association, cleared n^2/2 keys (#2995).
static uint32_t check_generic_associations(const Parser* parser, uint32_t first, uint32_t end)
{
    uint32_t count = 1;
    uint32_t token = first;
    while (token < end)
    {
        const uint8_t kind = parser->tokens.kinds[token];
        const bool group = kind == PARSE_KIND_L_PAREN || kind == PARSE_KIND_L_SQUARE || kind == PARSE_KIND_L_BRACE;
        const uint32_t close = group ? parser->tree.parents[token] : token;
        count += kind == PARSE_KIND_COMMA;
        token = !group ? token + 1 : close > token && close < end ? close + 1 : end;
    }
    return count;
}

static bool check_generic_initialize(CheckInputReader* reader, CheckGenericSet* set, uint32_t associations, TypeId control, uint32_t source_position)
{
    *set = (CheckGenericSet)
    {
        .control = control,
        .source_position = source_position,
    };
    bool valid = true;
    if (associations <= BUSTER_CHECK_GENERIC_GROUP)
    {
        set->keys = set->local_keys;
        set->scan_types = set->local_types;
    }
    else
    {
        // At most two directory keys per association, and at most 2/3 occupancy. Round to whole
        // probe groups. All three columns have 4-byte alignment and fit in one checked allocation.
        const uint64_t capacity = ((uint64_t)associations * 3 + 15) & ~UINT64_C(15);
        const uint64_t bytes = capacity * 2 * sizeof(uint32_t) + (uint64_t)associations * sizeof(CheckGenericRow);
        set->keys = capacity <= UINT32_MAX ? arena_push(reader->scratch, bytes, 4) : NULL;
        if (set->keys != NULL)
        {
            set->capacity = (uint32_t)capacity;
            set->tails = set->keys + capacity;
            set->rows = (CheckGenericRow*)(set->tails + capacity);
            memset(set->keys, 0, capacity * sizeof(uint32_t));
            set->control_keys = check_generic_keys(reader->types, control);
        }
        else
        {
            // If the directory doesn't fit, the packed scan still needs only a key and a type per row.
            const uint64_t padded = ((uint64_t)associations + 15) & ~UINT64_C(15);
            set->keys = arena_push_array(reader->scratch, padded, sizeof(TypeId), 4);
            set->scan_types = arena_push_array(reader->scratch, associations, sizeof(TypeId), 4);
            valid = set->keys != NULL && set->scan_types != NULL;
            if (valid)
            {
                memset(set->keys, 0, padded * sizeof(TypeId));
            }
        }
    }
    if (valid && set->scan_types != NULL)
    {
        set->control_keys.precise = check_generic_key(reader->types, control);
    }
    reader->output->resource |= !valid;
    return valid;
}

static uint32_t check_generic_masks(const TypeId* keys, TypeId key)
{
    const __m512i block = _mm512_loadu_si512(keys);
    const uint32_t matches = _mm512_cmpeq_epi32_mask(block, _mm512_set1_epi32((int)key));
    const uint32_t empty = _mm512_cmpeq_epi32_mask(block, _mm512_setzero_si512());
    return matches | (empty << BUSTER_CHECK_GENERIC_GROUP);
}

// A selection's diagnostics' order at its `)` (#2914), clang's CreateGenericSelectionExpr's: each association in turn, by
// `ordinal` among those of a type, its -Wunreachable-code-generic-assoc (`later` 0) and then its type's errors against later
// associations' (`later` their ordinals plus one); the no-match error after them all (check_generic_suborder_last).
static uint64_t check_generic_suborder(uint32_t ordinal, uint32_t later)
{
    return ((uint64_t)ordinal + 1u) << 32 | later;
}

#define BUSTER_CHECK_GENERIC_SUBORDER_LAST UINT64_C(0xffffffff00000000)

// clang's -Wunreachable-code-generic-assoc (#1447, CreateGenericSelectionExpr): an association whose type name [first, end)
// gives `type` of array type, or qualified (const, volatile, restrict, an address space or objc_gc's, not _Atomic), which
// the controlling expression's lvalue conversion never selects. At the type name's first token that isn't a qualifier
// (its TypeLoc's begin), once however often the selection is read. It completes at the selection's `)`, `close`, in
// clang's CreateGenericSelectionExpr order (#2914, check_generic_suborder): association `ordinal`'s, before its type's
// errors against later ones'.
static void check_generic_unreachable(CheckInputReader* reader, uint32_t first, uint32_t end, TypeId type, uint32_t close, uint32_t ordinal)
{
    const Parser* parser = reader->parser;
    const TypeId plain = type != 0 ? type_strip_aligned(reader->types, type) : 0;
    const bool array = plain != 0 && type_kind_is_array(type_kind(reader->types, plain));
    // (An address space's or objc_gc's qualifier is the QUALIFIED wrapper's, which type_strip_aligned looks through.)
    const bool qualified = plain != 0 && !array && (type_qualifiers(type) != 0 || type_qualifiers(plain) != 0 || type_object_qualifiers(reader->types, type) != 0);
    uint32_t at = first;
    while (at + 1 < end && parser->tokens.kinds[at] == PARSE_KIND_QUALIFIER)
    {
        at += 1;
    }
    if (array || qualified)
    {
        const CheckWarningEvent event =
        {
            .id = CHECK_WARNING_ID_UNREACHABLE_GENERIC_ASSOC,
            .kind = CHECK_WARNING_EVENT_UNREACHABLE_ASSOCIATION,
            .token = at,
            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
            .completion = close + 1,
            .suborder = check_generic_suborder(ordinal, 0),
            // (Its typedef's spelling, as the warnings' types take it: `CI` (aka 'const int').)
            .payload = { .name = check_input_typedef_spelling(reader->output, parser, end), .token = BUSTER_CHECK_WARNING_NO_SOURCE, .first_type = type,
                         .data = array ? 0u : 1u },
        };
        check_input_warning_once(reader, &event);
    }
}

// Find the key or its first empty lane. A read of an absent key doesn't reserve that lane.
static uint32_t check_generic_slot(const CheckGenericSet* set, uint32_t key)
{
    uint32_t hash = key * UINT32_C(0x9e3779b9);
    hash ^= hash >> 16;
    uint32_t group = (uint32_t)(((uint64_t)hash * set->capacity) >> 32) & ~(BUSTER_CHECK_GENERIC_GROUP - 1);
    uint32_t mask = check_generic_masks(set->keys + group, key);
    while (mask == 0)
    {
        group += BUSTER_CHECK_GENERIC_GROUP;
        group = group == set->capacity ? 0 : group;
        mask = check_generic_masks(set->keys + group, key);
    }
    return group + (uint32_t)__builtin_ctz((mask | (mask >> BUSTER_CHECK_GENERIC_GROUP)) & UINT16_MAX);
}

static bool check_generic_distinct(CheckInputReader* reader, CheckGenericSet* set, TypeId incoming, TypeId prior)
{
    set->duplicate_queries += 1;
    const TypeAnswer answer = type_compatible_at(reader->types, incoming, prior, reader->transparent_unions, reader->types->tags.count, set->source_position);
    reader->output->resource |= answer == TYPE_ANSWER_FULL;
    return answer == TYPE_ANSWER_NO;
}

// A circular posting list needs just its tail in the directory. Its successor is the oldest row.
// Distinct key namespaces guarantee that every list always threads the same member of CheckGenericRow.
static void check_generic_append(CheckGenericSet* set, uint32_t slot, uint32_t key, bool cross)
{
    uint32_t first = set->count;
    if (set->keys[slot] != 0)
    {
        CheckGenericRow* tail = set->rows + set->tails[slot];
        uint32_t* next = cross ? &tail->next_cross : &tail->next_precise;
        first = *next;
        *next = set->count;
    }
    uint32_t* next = cross ? &set->rows[set->count].next_cross : &set->rows[set->count].next_precise;
    *next = first;
    set->keys[slot] = key;
    set->tails[slot] = set->count;
}

static bool check_generic_association(CheckInputReader* reader, CheckGenericSet* set, TypeId type, bool* matches)
{
    bool valid = true;
    bool control = false;
    *matches = false;
    if (set->scan_types != NULL)
    {
        const uint32_t key = check_generic_key(reader->types, type);
        for (uint64_t block = 0; valid && block < set->count; block += BUSTER_CHECK_GENERIC_GROUP)
        {
            uint32_t candidates = check_generic_masks(set->keys + block, key) & UINT16_MAX;
            while (valid && candidates != 0)
            {
                const uint32_t lane = (uint32_t)__builtin_ctz(candidates);
                candidates &= candidates - 1;
                valid = check_generic_distinct(reader, set, type, set->scan_types[block + lane]);
            }
        }
        if (valid)
        {
            set->keys[set->count] = key;
            set->scan_types[set->count] = type;
        }
        control = key == set->control_keys.precise;
    }
    else
    {
        const CheckGenericKeys keys = check_generic_keys(reader->types, type);
        const uint32_t slot = check_generic_slot(set, keys.precise);
        const uint32_t tail = set->keys[slot] != 0 ? set->tails[slot] : UINT32_MAX;
        uint32_t same = tail != UINT32_MAX ? set->rows[tail].next_precise : UINT32_MAX;
        uint32_t other_tail = UINT32_MAX;
        if (keys.cross != 0)
        {
            const uint32_t other_slot = check_generic_slot(set, keys.cross ^ 1u);
            other_tail = set->keys[other_slot] != 0 ? set->tails[other_slot] : UINT32_MAX;
        }
        uint32_t other = other_tail != UINT32_MAX ? set->rows[other_tail].next_cross : UINT32_MAX;
        // Merge two disjoint lists by association ordinal. The first YES or FULL still stops the reader.
        while (valid && (same != UINT32_MAX || other != UINT32_MAX))
        {
            const uint32_t row = same < other ? same : other;
            valid = check_generic_distinct(reader, set, type, set->rows[row].type);
            if (row == same)
            {
                same = row == tail ? UINT32_MAX : set->rows[row].next_precise;
            }
            else
            {
                other = row == other_tail ? UINT32_MAX : set->rows[row].next_cross;
            }
        }
        if (valid)
        {
            set->rows[set->count].type = type;
            check_generic_append(set, slot, keys.precise, false);
            if (keys.cross != 0)
            {
                check_generic_append(set, check_generic_slot(set, keys.cross), keys.cross, true);
            }
        }
        control = keys.precise == set->control_keys.precise || (keys.cross != 0 && (keys.cross ^ 1u) == set->control_keys.cross);
    }
    set->count += valid;
    if (valid && control)
    {
        set->control_queries += 1;
        const TypeAnswer answer = type_compatible_at(reader->types, type, set->control, reader->transparent_unions, reader->types->tags.count, set->source_position);
        reader->output->resource |= answer == TYPE_ANSWER_FULL;
        valid = answer != TYPE_ANSWER_FULL;
        *matches = answer == TYPE_ANSWER_YES;
    }
    return valid;
}
