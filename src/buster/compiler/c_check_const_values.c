// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Completed initializer receipts. The source reader owns eligibility and evaluation; this module only retains
// converted facts and reads them. VALUE preserves symbolic pointers and floating/complex payloads. Sparse writes
// and CLEAR coverage never materialize an array's extent. Include after CheckValue and the basic type helpers.
#ifndef BUSTER_C_CHECK_CONST_VALUES_INCLUDED
#define BUSTER_C_CHECK_CONST_VALUES_INCLUDED

#ifndef BUSTER_CHECK_GNU_OBJECT_READ
#define BUSTER_CHECK_GNU_OBJECT_READ 0x10000u
#endif

typedef enum CheckConstPartKind
{
    CHECK_CONST_VALUE,
    CHECK_CONST_BYTES,
    CHECK_CONST_CLEAR,
} CheckConstPartKind;

typedef struct CheckConstPart CheckConstPart;
struct CheckConstPart
{
    CheckConstPartKind kind;
    TypeId type;
    uint64_t bit_offset;
    uint64_t bit_count;
    CheckValue value;
    const uint8_t* bytes;
    uint64_t byte_count;
};

typedef struct CheckConstUnion CheckConstUnion;
struct CheckConstUnion
{
    TypeId type;
    uint64_t bit_offset;
    // One-based ordinal in the record's physical member list, including unnamed bit-fields.
    uint32_t member;
};

typedef struct CheckConstObject CheckConstObject;
struct CheckConstObject
{
    uint32_t object;
    uint32_t declaration;
    uint32_t completed;
    TypeId type;
    uint64_t byte_size;
    uint32_t part_first;
    uint32_t part_count;
    uint32_t union_first;
    uint32_t union_count;
};

// A persistent binary trie over an invertible multiplicative hash. Each append copies exactly one 32-node
// path; no published node changes. At depth 31 a child is an object index plus one; earlier children are nodes.
typedef struct CheckConstHash CheckConstHash;
struct CheckConstHash
{
    uint32_t child[2];
};

typedef struct CheckConstValues CheckConstValues;
struct CheckConstValues
{
    const CheckConstObject* objects;
    uint32_t count;
    const CheckConstPart* parts;
    uint32_t part_count;
    const CheckConstUnion* unions;
    uint32_t union_count;
    const CheckConstHash* hash;
    uint32_t hash_count;
    uint32_t hash_root;
    // At most one borrowed, immutable prefix; it must outlive this view and have no prefix of its own.
    const CheckConstValues* prefix;
};

typedef struct CheckConstBuffer CheckConstBuffer;
struct CheckConstBuffer
{
    Arena* arena;
    CheckConstObject* objects;
    uint32_t count;
    uint32_t capacity;
    CheckConstPart* parts;
    uint32_t part_count;
    uint32_t part_capacity;
    CheckConstUnion* unions;
    uint32_t union_count;
    uint32_t union_capacity;
    CheckConstHash* hash;
    uint32_t hash_count;
    uint32_t hash_capacity;
    uint32_t hash_root;
    const CheckConstValues* prefix;
    bool resource;
};

// Views borrow arena storage. Appending preserves every older view, including its trie and owned BYTES data.
static CheckConstValues check_const_view(const CheckConstBuffer* buffer)
{
    const CheckConstValues result = buffer != NULL ? (CheckConstValues){
        .objects = buffer->objects,
        .count = buffer->count,
        .parts = buffer->parts,
        .part_count = buffer->part_count,
        .unions = buffer->unions,
        .union_count = buffer->union_count,
        .hash = buffer->hash,
        .hash_count = buffer->hash_count,
        .hash_root = buffer->hash_root,
        .prefix = buffer->prefix,
    } : (CheckConstValues){ 0 };
    return result;
}

// No traversal follows a caller-created prefix chain. An unavailable or not-yet-completed object has no owner.
static const CheckConstObject* check_const_find(const CheckConstValues* view, uint32_t object, uint32_t source_position,
                                                const CheckConstValues** owner_view_out)
{
    const CheckConstObject* result = NULL;
    const CheckConstValues* owner = NULL;
    const bool valid = view != NULL && object != 0 && (view->prefix == NULL || (view->prefix != view && view->prefix->prefix == NULL));
    const uint32_t key = object * UINT32_C(2654435761);
    for (uint32_t pass = 0; valid && result == NULL && pass < 2; pass += 1)
    {
        const CheckConstValues* part = pass == 0 ? view : view->prefix;
        uint32_t node = part != NULL ? part->hash_root : 0;
        for (uint32_t depth = 0; part != NULL && node != 0 && depth < 32; depth += 1)
        {
            const bool inside = part->hash != NULL && node <= part->hash_count;
            const uint32_t next = inside ? part->hash[node - 1].child[(key >> (31 - depth)) & 1] : 0;
            if (depth == 31)
            {
                if (next != 0 && next <= part->count && part->objects != NULL)
                {
                    const CheckConstObject* candidate = part->objects + next - 1;
                    if (candidate->object == object && candidate->completed <= source_position)
                    {
                        result = candidate;
                        owner = part;
                    }
                }
                node = 0;
            }
            else
            {
                node = next;
            }
        }
    }
    if (owner_view_out != NULL)
    {
        *owner_view_out = owner;
    }
    return result;
}

static void* check_const_storage(Arena* arena, const void* previous, uint32_t count, uint32_t needed,
                                  uint32_t* capacity, uint64_t size, uint64_t alignment)
{
    void* result = (void*)previous;
    if (needed > *capacity)
    {
        uint32_t grown = *capacity != 0 ? *capacity : 8;
        while (grown < needed)
        {
            grown = grown <= UINT32_MAX / 2 ? grown * 2 : needed;
        }
        result = arena_push_array(arena, grown, size, alignment);
        if (result != NULL)
        {
            if (count != 0)
            {
                memcpy(result, previous, (uint64_t)count * size);
            }
            *capacity = grown;
        }
    }
    return result;
}

// Invalid facts/duplicates return false without resource failure. Allocation failure rolls back arena and
// builder publication, setting only resource. The caller plans first; this function never evaluates a source.
static bool check_const_append(CheckConstBuffer* buffer, CheckConstObject header, const CheckConstPart* parts, uint32_t part_count,
                                const CheckConstUnion* unions, uint32_t union_count)
{
    bool valid = buffer != NULL && buffer->arena != NULL && !buffer->resource && header.object != 0 && header.type != 0 &&
                 header.byte_size <= UINT64_MAX / 8 && (part_count == 0 || parts != NULL) && (union_count == 0 || unions != NULL);
    const CheckConstValues before = check_const_view(buffer);
    valid = valid && (before.prefix == NULL || (before.prefix != &before && before.prefix->prefix == NULL)) &&
            check_const_find(&before, header.object, UINT32_MAX, NULL) == NULL;
    const uint64_t bits = valid ? header.byte_size * 8 : 0;
    for (uint32_t i = 0; valid && i < part_count; i += 1)
    {
        const CheckConstPart* part = parts + i;
        valid = (uint32_t)part->kind <= CHECK_CONST_CLEAR && part->bit_count != 0 && part->bit_offset <= bits && part->bit_count <= bits - part->bit_offset;
        if (valid && part->kind == CHECK_CONST_VALUE)
        {
            valid = part->type != 0 && part->value.type != 0;
        }
        else if (valid && part->kind == CHECK_CONST_BYTES)
        {
            valid = part->bytes != NULL && part->byte_count <= UINT64_MAX / 8 && part->bit_count <= part->byte_count * 8;
        }
    }
    for (uint32_t i = 0; valid && i < union_count; i += 1)
    {
        valid = unions[i].type != 0 && unions[i].bit_offset <= bits && unions[i].member != 0;
    }
    const bool fits = valid && buffer->count != UINT32_MAX && part_count <= UINT32_MAX - buffer->part_count &&
                      union_count <= UINT32_MAX - buffer->union_count && buffer->hash_count <= UINT32_MAX - 32;
    if (valid && !fits)
    {
        buffer->resource = true;
    }
    bool result = false;
    if (fits)
    {
        const uint64_t mark = buffer->arena->length;
        CheckConstBuffer next = *buffer;
        next.objects = check_const_storage(next.arena, next.objects, next.count, next.count + 1, &next.capacity, sizeof(CheckConstObject), _Alignof(CheckConstObject));
        next.parts = check_const_storage(next.arena, next.parts, next.part_count, next.part_count + part_count, &next.part_capacity, sizeof(CheckConstPart), _Alignof(CheckConstPart));
        next.unions = check_const_storage(next.arena, next.unions, next.union_count, next.union_count + union_count, &next.union_capacity, sizeof(CheckConstUnion), _Alignof(CheckConstUnion));
        next.hash = check_const_storage(next.arena, next.hash, next.hash_count, next.hash_count + 32, &next.hash_capacity, sizeof(CheckConstHash), _Alignof(CheckConstHash));
        bool ready = next.objects != NULL && next.hash != NULL && (part_count == 0 || next.parts != NULL) && (union_count == 0 || next.unions != NULL);
        for (uint32_t i = 0; ready && i < part_count; i += 1)
        {
            CheckConstPart retained = parts[i];
            if (retained.kind == CHECK_CONST_BYTES)
            {
                uint8_t* bytes = arena_push(next.arena, retained.byte_count, 1);
                ready = bytes != NULL;
                if (ready)
                {
                    memcpy(bytes, retained.bytes, retained.byte_count);
                    retained.bytes = bytes;
                }
            }
            if (ready)
            {
                next.parts[next.part_count + i] = retained;
            }
        }
        if (ready)
        {
            if (union_count != 0)
            {
                memcpy(next.unions + next.union_count, unions, (uint64_t)union_count * sizeof(CheckConstUnion));
            }
            const uint32_t key = header.object * UINT32_C(2654435761);
            uint32_t old = next.hash_root;
            for (uint32_t depth = 0; depth < 32; depth += 1)
            {
                CheckConstHash node = old != 0 ? next.hash[old - 1] : (CheckConstHash){ 0 };
                const uint32_t bit = (key >> (31 - depth)) & 1;
                old = node.child[bit];
                node.child[bit] = depth == 31 ? next.count + 1 : next.hash_count + depth + 2;
                next.hash[next.hash_count + depth] = node;
            }
            header.part_first = next.part_count;
            header.part_count = part_count;
            header.union_first = next.union_count;
            header.union_count = union_count;
            next.objects[next.count++] = header;
            next.part_count += part_count;
            next.union_count += union_count;
            next.hash_root = next.hash_count + 1;
            next.hash_count += 32;
            *buffer = next;
            result = true;
        }
        else
        {
            arena_pop_to(buffer->arena, mark);
            buffer->resource = true;
        }
    }
    return result;
}

static TypeId check_const_plain(const TypeTable* types, TypeId type)
{
    TypeId result = type_unqualified(type);
    bool finished = false;
    for (uint32_t visited = 0; !finished && result != 0 && types != NULL && visited < types->types.count; visited += 1)
    {
        const uint32_t index = type_index(result);
        if (index >= types->types.count || types->types.kind == NULL)
        {
            result = 0;
            finished = true;
        }
        else if (types->types.kind[index] == BUSTER_TYPE_KIND_ALIGNED || types->types.kind[index] == BUSTER_TYPE_KIND_ATOMIC)
        {
            result = types->types.base != NULL ? type_unqualified(types->types.base[index]) : 0;
        }
        else
        {
            finished = true;
        }
    }
    result = finished ? result : 0;
    return result;
}

// Constness belongs to the defining object, not a const member or a cast-qualified access to mutable storage.
static bool check_const_eligible(const TypeTable* types, TypeId type)
{
    bool result = false;
    bool finished = false;
    for (uint32_t visited = 0; !finished && type != 0 && types != NULL && visited < types->types.count; visited += 1)
    {
        const uint32_t index = type_index(type);
        if (index >= types->types.count || types->types.kind == NULL || (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) != 0)
        {
            result = false;
            finished = true;
        }
        else
        {
            result |= (type_qualifiers(type) & BUSTER_TYPE_CONST) != 0;
            const uint8_t kind = types->types.kind[index];
            if (kind == BUSTER_TYPE_KIND_ALIGNED || type_kind_is_array(kind))
            {
                type = types->types.base != NULL ? types->types.base[index] : 0;
            }
            else
            {
                finished = true;
            }
        }
    }
    return result && finished;
}

// A read may normalize an atomic/aligned value, but no cast can erase volatile from its declaration path.
static TypeId check_const_declared(const TypeTable* types, TypeId type)
{
    TypeId result = 0;
    bool finished = false;
    for (uint32_t visited = 0; !finished && types != NULL && visited < types->types.count; visited += 1)
    {
        if (!type_is_valid(types, type) || (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) != 0)
        {
            finished = true;
        }
        else
        {
            const uint8_t kind = type_kind(types, type);
            if (kind == BUSTER_TYPE_KIND_ALIGNED || kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                type = types->types.base != NULL ? types->types.base[type_index(type)] : 0;
            }
            else
            {
                result = type_unqualified(type);
                finished = true;
            }
        }
    }
    return result;
}

// Return one physical ordinal, shared by access-path guards and the typed read witness. Missing metadata
// selects the first initializable member; neither absent selections nor malformed selections admit all arms.
static uint32_t check_const_union_member(const TypeTable* types, const CheckConstValues* owner, const CheckConstObject* receipt,
                                         TypeId record_type, uint64_t bit_offset)
{
    const TypeId plain = check_const_plain(types, record_type);
    uint32_t selected = 0;
    const bool valid = receipt != NULL && owner != NULL && plain != 0 && type_kind(types, plain) == BUSTER_TYPE_KIND_UNION &&
                       type_is_complete(types, plain) && receipt->byte_size <= UINT64_MAX / 8 && bit_offset <= receipt->byte_size * 8 &&
                       type_size(types, plain) <= (receipt->byte_size * 8 - bit_offset) / 8 && receipt->union_first <= owner->union_count &&
                       receipt->union_count <= owner->union_count - receipt->union_first && (receipt->union_count == 0 || owner->unions != NULL);
    if (valid)
    {
        const uint32_t tag = types->types.index[type_index(plain)];
        const bool tagged = tag < types->tags.count && types->tags.member_begin != NULL && types->tags.member_count != NULL;
        const uint32_t first = tagged ? types->tags.member_begin[tag] : 0;
        const uint32_t count = tagged ? types->tags.member_count[tag] : 0;
        const bool bounded = first <= types->members.count && count <= types->members.count - first &&
                             types->members.name != NULL && types->members.flags != NULL;
        for (uint32_t i = 0; bounded && selected == 0 && i < count; i += 1)
        {
            if (types->members.name[first + i] != 0 || (types->members.flags[first + i] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0)
            {
                selected = i + 1;
            }
        }
        for (uint32_t i = 0; bounded && i < receipt->union_count; i += 1)
        {
            const CheckConstUnion* alternative = owner->unions + receipt->union_first + i;
            if (alternative->bit_offset == bit_offset && check_const_plain(types, alternative->type) == plain)
            {
                selected = alternative->member;
            }
        }
        const bool initializable = bounded && selected != 0 && selected <= count &&
            (types->members.name[first + selected - 1] != 0 || (types->members.flags[first + selected - 1] & BUSTER_TYPE_MEMBER_BIT_FIELD) == 0);
        selected = initializable ? selected : 0;
    }
    return selected;
}

// Coverage proves a representation only after this path proves a declared readable subobject. Positive-width
// struct members are disjoint, arrays contribute one computed element, and a union contributes only its active
// physical member. The iterative path allocates nothing and cannot mistake padding or a cast's requested type
// for a declaration. Instance storage does not supply a missing declared array bound for a constant read.
static bool check_const_subobject(const TypeTable* types, const CheckConstValues* owner, const CheckConstObject* receipt,
                                   uint64_t begin, uint64_t width, uint32_t bit_width, TypeId plain)
{
    TypeId current = receipt->type;
    uint64_t offset = 0;
    bool result = false;
    bool finished = false;
    for (uint32_t depth = 0; !finished && depth < types->types.count; depth += 1)
    {
        current = check_const_declared(types, current);
        if (current == 0 || offset > begin)
        {
            finished = true;
        }
        else
        {
            const uint8_t kind = type_kind(types, current);
            if (kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
            {
                finished = true;
            }
            else if (kind == BUSTER_TYPE_KIND_ARRAY)
            {
                const TypeId element = types->types.base != NULL ? type_element(types, current) : 0;
                const TypeId declared = check_const_declared(types, element);
                const uint64_t size = declared != 0 ? type_size(types, element) : 0;
                const uint64_t unit = size <= UINT64_MAX / 8 ? size * 8 : 0;
                const uint64_t delta = begin - offset;
                const uint64_t index = unit != 0 ? delta / unit : 0;
                const uint64_t count = types->types.extent != NULL ? types->types.extent[type_index(current)] : 0;
                finished = declared == 0 || unit == 0 || index >= count || width > unit - delta % unit;
                if (!finished)
                {
                    offset = begin - delta % unit;
                    current = element;
                }
            }
            else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION)
            {
                const uint32_t tag = types->types.index[type_index(current)];
                const bool tagged = type_is_complete(types, current) && tag < types->tags.count &&
                                    types->tags.member_begin != NULL && types->tags.member_count != NULL;
                const uint32_t first = tagged ? types->tags.member_begin[tag] : 0;
                const uint32_t count = tagged ? types->tags.member_count[tag] : 0;
                const bool bounded = first <= types->members.count && count <= types->members.count - first &&
                                     types->members.type != NULL && types->members.offset != NULL && types->members.name != NULL &&
                                     types->members.flags != NULL && types->members.bit_width != NULL;
                const uint32_t active = bounded && kind == BUSTER_TYPE_KIND_UNION ?
                                        check_const_union_member(types, owner, receipt, current, offset) : 0;
                TypeId selected = 0;
                uint64_t selected_offset = 0;
                for (uint32_t i = 0; bounded && !finished && selected == 0 && i < count; i += 1)
                {
                    const uint32_t member = first + i;
                    const bool field = (types->members.flags[member] & BUSTER_TYPE_MEMBER_BIT_FIELD) != 0;
                    const bool initializable = !field || types->members.name[member] != 0;
                    const uint64_t relative = types->members.offset[member];
                    if (initializable && (kind != BUSTER_TYPE_KIND_UNION || active == i + 1) && relative <= UINT64_MAX - offset)
                    {
                        const uint64_t start = offset + relative;
                        const TypeId declared = check_const_declared(types, types->members.type[member]);
                        const uint64_t size = declared != 0 ? type_size(types, types->members.type[member]) : 0;
                        const uint64_t bits = field ? types->members.bit_width[member] : size <= UINT64_MAX / 8 ? size * 8 : 0;
                        if (start <= begin && width <= bits && begin - start <= bits - width)
                        {
                            if (field)
                            {
                                result = declared == plain && begin == start && bit_width != 0 && width == bit_width &&
                                         bit_width == types->members.bit_width[member];
                                finished = true;
                            }
                            else
                            {
                                selected = types->members.type[member];
                                selected_offset = start;
                            }
                        }
                    }
                }
                if (!finished)
                {
                    finished = selected == 0;
                    current = selected;
                    offset = selected_offset;
                }
            }
            else
            {
                const uint64_t size = type_size(types, current);
                result = bit_width == 0 && current == plain && begin == offset && size <= UINT64_MAX / 8 && width == size * 8;
                finished = true;
            }
        }
    }
    return result;
}

static bool check_const_zero(const TypeTable* types, TypeId type, CheckValue* value)
{
    uint32_t width = 0;
    bool sign = false;
    const uint8_t kind = type_kind(types, type);
    CheckConstantKind constant = CHECK_CONSTANT_NONE;
    if (check_action_integer_type(types, type, &width, &sign))
    {
        constant = CHECK_CONSTANT_INTEGER;
    }
    else if (kind == BUSTER_TYPE_KIND_FLOATING)
    {
        constant = CHECK_CONSTANT_FLOATING;
    }
    else if (kind == BUSTER_TYPE_KIND_COMPLEX)
    {
        constant = CHECK_CONSTANT_COMPLEX;
    }
    else if (kind == BUSTER_TYPE_KIND_POINTER)
    {
        constant = CHECK_CONSTANT_ADDRESS;
    }
    if (constant != CHECK_CONSTANT_NONE)
    {
        *value = (CheckValue){ .type = type, .category = CHECK_RVALUE, .constant_kind = constant };
    }
    return constant != CHECK_CONSTANT_NONE;
}

// Last overlapping write wins. A partial write blocks older evidence unless that write itself proves the
// complete requested value. bit_width zero requests the full scalar storage; otherwise it names a bit-field.
static bool check_const_load(const TypeTable* types, const CheckConstValues* view, uint32_t object, int64_t byte_offset,
                              uint32_t subbyte_bit_offset, uint32_t bit_width, TypeId type, uint32_t source_position, CheckValue* value_out)
{
    const CheckConstValues* owner = NULL;
    const CheckConstObject* receipt = check_const_find(view, object, source_position, &owner);
    const TypeId plain = check_const_plain(types, type);
    const uint64_t size = plain != 0 ? type_size(types, plain) : 0;
    const uint64_t width = bit_width != 0 ? bit_width : size <= UINT64_MAX / 8 ? size * 8 : 0;
    bool valid = receipt != NULL && owner != NULL && value_out != NULL && plain != 0 && check_const_eligible(types, receipt->type) &&
                 (type_qualifiers(type) & BUSTER_TYPE_VOLATILE) == 0 && byte_offset >= 0 && (uint64_t)byte_offset <= UINT64_MAX / 8 &&
                 subbyte_bit_offset < 8 && width != 0 && receipt->byte_size <= UINT64_MAX / 8 &&
                 receipt->part_first <= owner->part_count && receipt->part_count <= owner->part_count - receipt->part_first &&
                 (receipt->part_count == 0 || owner->parts != NULL);
    uint64_t begin = valid ? (uint64_t)byte_offset * 8 : 0;
    valid = valid && subbyte_bit_offset <= UINT64_MAX - begin;
    begin += valid ? subbyte_bit_offset : 0;
    valid = valid && begin <= receipt->byte_size * 8 && width <= receipt->byte_size * 8 - begin;
    valid = valid && check_const_subobject(types, owner, receipt, begin, width, bit_width, plain);
    CheckValue value = { 0 };
    bool result = false;
    bool decided = false;
    for (uint32_t remaining = valid ? receipt->part_count : 0; remaining != 0 && !decided;)
    {
        const CheckConstPart* part = owner->parts + receipt->part_first + --remaining;
        const bool bounded = part->bit_offset <= receipt->byte_size * 8 && part->bit_count <= receipt->byte_size * 8 - part->bit_offset;
        const bool overlap = bounded && part->bit_offset < begin + width && begin < part->bit_offset + part->bit_count;
        if (overlap)
        {
            decided = true;
            const bool covered = part->bit_offset <= begin && width <= part->bit_count && begin - part->bit_offset <= part->bit_count - width;
            if (covered && part->kind == CHECK_CONST_CLEAR)
            {
                result = check_const_zero(types, plain, &value);
            }
            else if (covered && part->kind == CHECK_CONST_VALUE && part->bit_offset == begin && part->bit_count == width &&
                     check_const_plain(types, part->type) == plain && check_const_plain(types, part->value.type) == plain)
            {
                value = part->value;
                CheckValue zero = { 0 };
                result = check_const_zero(types, plain, &zero) && value.constant_kind == zero.constant_kind && value.fault == CHECK_FAULT_NONE &&
                         (value.flags & (BUSTER_CHECK_SIDE_EFFECT | BUSTER_CHECK_VOLATILE_PART)) == 0;
                if (result && value.constant_kind == CHECK_CONSTANT_INTEGER)
                {
                    uint32_t integer_width = 0;
                    bool sign = false;
                    result = check_action_integer_type(types, plain, &integer_width, &sign) && integer_width != 0 && integer_width <= 128 &&
                             (bit_width == 0 || bit_width <= integer_width);
                    if (result && bit_width != 0)
                    {
                        value.integer &= check_action_mask(bit_width);
                        if (sign && ((value.integer >> (bit_width - 1)) & 1) != 0)
                        {
                            value.integer |= ~check_action_mask(bit_width);
                        }
                        value.integer &= check_action_mask(integer_width);
                    }
                    else if (result)
                    {
                        result = (value.integer & ~check_action_mask(integer_width)) == 0;
                    }
                }
            }
            else if (covered && part->kind == CHECK_CONST_BYTES && part->bytes != NULL && part->byte_count <= UINT64_MAX / 8)
            {
                uint32_t integer_width = 0;
                bool sign = false;
                const uint64_t relative = begin - part->bit_offset;
                result = check_action_integer_type(types, plain, &integer_width, &sign) && width <= 128 && integer_width <= 128 &&
                         width <= integer_width && relative <= part->byte_count * 8 && width <= part->byte_count * 8 - relative;
                if (result)
                {
                    unsigned __int128 integer = 0;
                    for (uint32_t bit = 0; bit < width; bit += 1)
                    {
                        const uint64_t at = relative + bit;
                        integer |= (unsigned __int128)((part->bytes[at / 8] >> (at % 8)) & 1) << bit;
                    }
                    if (sign && ((integer >> (width - 1)) & 1) != 0)
                    {
                        integer |= ~check_action_mask((uint32_t)width);
                    }
                    value = (CheckValue){ .type = plain, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = integer & check_action_mask(integer_width) };
                }
            }
        }
        else if (!bounded)
        {
            decided = true;
        }
    }
    if (result)
    {
        value.type = plain;
        value.category = CHECK_RVALUE;
        const uint32_t pointer_flags = value.constant_kind == CHECK_CONSTANT_ADDRESS ?
                                       value.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_CAST) : 0;
        value.flags = (value.flags & (BUSTER_CHECK_GNU_DIVISION | BUSTER_CHECK_GNU_FLOAT_RANGE)) | pointer_flags |
                      BUSTER_CHECK_GNU_OBJECT_READ | BUSTER_CHECK_GNU_CONSTANT;
        value.bit_width = bit_width;
        value.flags |= bit_width != 0 ? BUSTER_CHECK_BIT_FIELD : 0;
        *value_out = value;
    }
    return result;
}

// A receipt without an explicit selection uses the first initializable member, never every union alternative.
// No receipt means the caller has no retained const-object evidence with which to restrict a normal designator.
static bool check_const_union_allows(const TypeTable* types, const CheckConstValues* view, uint32_t object, TypeId record_type,
                                      uint64_t bit_offset, uint32_t member_ordinal, uint32_t source_position)
{
    const CheckConstValues* owner = NULL;
    const CheckConstObject* receipt = check_const_find(view, object, source_position, &owner);
    const bool result = receipt == NULL || (member_ordinal != 0 && member_ordinal == check_const_union_member(types, owner, receipt, record_type, bit_offset));
    return result;
}

#endif
