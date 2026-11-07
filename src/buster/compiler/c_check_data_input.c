// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Source definitions and retained initializer plans -> one shared Link symbol space. Preparation selects final
// definitions and retains per-initializer references before Link publication. The program consumer merges those
// references with its bodies, reserves code/data together, then publishes both into that Link. The standalone
// wrapper also supports data-only inspection. String candidates are emitted only when their symbols are used.
// Input, typed stream, initializer plans and type table must belong to the same successful source snapshot.

// Shared by standalone data and the program emitter: selected overloads have one ABI spelling.
#include "c_check_mangle.c"

typedef enum CheckDataInputStatus
{
    CHECK_DATA_INPUT_OK,
    CHECK_DATA_INPUT_UNSUPPORTED,
    CHECK_DATA_INPUT_RESOURCE,
    CHECK_DATA_INPUT_INVALID,
} CheckDataInputStatus;

// A constant CFString's call (#148): its object, its `)`'s token plus one (the call's value's), and the object whose
// symbol it takes, the lowest call's of its characters (itself for that one), whose 32-byte record the unit holds, as
// clang's code makes one of each literal (GetAddrOfConstantCFString). CheckDataPrepared.cfstrings is in object order.
typedef struct CheckDataCFString CheckDataCFString;
struct CheckDataCFString
{
    uint32_t object;
    uint32_t canonical;
    // The canonical call's characters' object: its literal's, or its UTF-16 array's.
    uint32_t characters;
};

typedef struct CheckDataPrepared CheckDataPrepared;
struct CheckDataPrepared
{
    CheckDataObject* objects;
    uint32_t count;
    uint32_t* references;
    uint32_t reference_count;
    uint32_t* reference_offsets;
    uint32_t* reference_counts;
    // The static declarations (the tokens that declare an object with static storage: a function, a file-scope object,
    // a static local, an extern declaration, a compound literal at file scope), as keys of the object (a token's id,
    // check_input's `objects`) in the high half and the token in the low, in object order and each object's in token
    // order; and the static objects, in object order, with each one's last declaration: check_data_latest's.
    uint64_t* declaration_keys;
    uint32_t declaration_count;
    uint32_t* declared_objects;
    uint32_t* declared_latest;
    uint32_t declared_count;
    uint32_t object_count;
    // The unit's constant CFStrings (#148, check_data_cfstring_keys): each call's, in token order; the object of the
    // __CFConstantStringClassReference they point to (the unit's declaration's, or one of its own: `cfstring_class_own`).
    CheckDataCFString* cfstrings;
    uint32_t cfstring_count;
    uint32_t cfstring_class;
    bool cfstring_class_own;
    // The first of the records' references (the last of `references`), which no program takes as roots: a record's
    // class and characters are needed only where the record is (check_program_alias_number).
    uint32_t cfstring_reference_first;
    LinkCapacity capacity;
    uint32_t token;
    CheckDataInputStatus status;
};

typedef struct CheckDataInput CheckDataInput;
struct CheckDataInput
{
    Link link;
    SymbolId* object_symbols;
    uint32_t object_count;
    uint32_t token;
    CheckDataInputStatus status;
    CheckDataResult emission;
};

static CheckDataInputStatus check_data_input_status(CheckDataStatus status)
{
    const CheckDataInputStatus result = status == CHECK_DATA_OK ? CHECK_DATA_INPUT_OK :
                                       status == CHECK_DATA_RESOURCE ? CHECK_DATA_INPUT_RESOURCE :
                                       status == CHECK_DATA_INVALID || status == CHECK_DATA_SYMBOL ? CHECK_DATA_INPUT_INVALID : CHECK_DATA_INPUT_UNSUPPORTED;
    return result;
}

// Where `object` is among prepared's declared objects (check_data_prepare's, in object order), or declared_count where it
// isn't one (a string literal's, or a non-object). The search halves its range without a branch, each step a select of
// the half: its callers ask of objects in an order the branch predictor can't learn (the selection's definitions, in
// token order).
static uint32_t check_data_declared_index(const CheckDataPrepared* prepared, uint32_t object)
{
    const uint32_t* objects = prepared->declared_objects;
    uint32_t base = 0;
    uint32_t length = prepared->declared_count;
    while (length > 1)
    {
        const uint32_t half = length / 2;
        base = objects[base + half] < object ? base + half : base;
        length -= half;
    }
    // The first declared object not below `object`.
    const uint32_t lower = length != 0 ? base + (objects[base] < object) : 0;
    const uint32_t index = lower < prepared->declared_count && objects[lower] == object ? lower : prepared->declared_count;
    return index;
}

// A declared object's alignment past its type's, its log2 (CheckDataObject.alignment_log2): its declarations' largest
// alignment attribute's (`attribute`, its log2 plus 1, 0 for none), or without one at least 16 bytes for an array of 16
// bytes or more, as the x86-64 psABI and clang align a global array (type_large_array, #978); a compound literal its
// type's alone.
static uint8_t check_data_declared_alignment(const CheckInput* input, const TypeTable* types, uint32_t object, TypeId type, uint8_t attribute)
{
    uint8_t result = attribute != 0 ? (uint8_t)(attribute - 1) : 0;
    if (attribute == 0 && !check_input_bit(input->declaration_bits, object - 1, BUSTER_CHECK_BITS_COMPOUND) && type_large_array(types, type))
    {
        result = 4;
    }
    return result;
}

// The last static declaration of `object`, or BUSTER_CHECK_NONE where it has none.
static uint32_t check_data_latest(const CheckDataPrepared* prepared, uint32_t object)
{
    const uint32_t index = check_data_declared_index(prepared, object);
    const uint32_t token = index < prepared->declared_count ? prepared->declared_latest[index] : BUSTER_CHECK_NONE;
    return token;
}

// The static data: each static object's last declaration, and for each one the unit defines or tentatively defines, in
// object order, its definition, type, alignment and initializer's references; then the string literals. Few of a unit's
// tokens declare a static object (#592), so the vector path finds them 64 tokens at a time (the scalar path, `scalar`,
// takes every token), as keys of their object and token, which c_bindings_sort puts in object order, each object's
// declarations in token order: no token-sized column is filled or read past them.
// The canonical object of `object` among the `count` calls of `calls` (in object order), or `object` itself where it is no
// call's: a reference to a call's object is its canonical record's (#148).
static uint32_t check_data_cfstring_canonical(const CheckDataCFString* calls, uint32_t count, uint32_t object)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = calls[middle].object < object ? middle + 1 : low;
        high = calls[middle].object < object ? high : middle;
    }
    return low < count && calls[low].object == object ? calls[low].canonical : object;
}

// The index of the call of `object` among the `count` calls of `calls` (in object order), or `count`.
static uint32_t check_data_cfstring_index(const CheckDataCFString* calls, uint32_t count, uint32_t object)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        const uint32_t middle = low + (high - low) / 2;
        low = calls[middle].object < object ? middle + 1 : low;
        high = calls[middle].object < object ? high : middle;
    }
    return low < count && calls[low].object == object ? low : count;
}

// A constant CFString's characters (#148), as clang's GetConstantCFStringEntry keys its objects: an ASCII literal's own
// bytes (none past 0x7f, no NUL), or else the UTF-16 units of LLVM's strict conversion, which stops at the first byte that
// begins no sequence (the units before it kept), and a NUL unit. Of one call: its object (its `)`'s token plus one), its
// name's token, its literal's object, the key's bytes and their hash, the record's length (the literal's bytes, or the
// units but the NUL) and whether it is UTF-16's.
typedef struct CheckDataCFStringKey CheckDataCFStringKey;
struct CheckDataCFStringKey
{
    uint32_t object;
    uint32_t name;
    uint32_t literal;
    const uint8_t* bytes;
    uint32_t byte_count;
    uint64_t length;
    uint32_t hash;
    bool utf16;
};

// The UTF-16 units of `length` bytes at `bytes` into `units` (as many as there are bytes, at least), as LLVM's
// ConvertUTF8toUTF16 strict conversion writes them (pp_utf8_sequence's reading): how many.
static uint32_t check_data_cfstring_utf16(const uint8_t* bytes, uint32_t length, uint16_t* units)
{
    uint32_t count = 0;
    uint32_t at = 0;
    bool converting = true;
    while (converting && at < length)
    {
        uint32_t code_point = 0;
        const uint32_t sequence = pp_utf8_sequence(bytes + at, length - at, &code_point);
        converting = sequence != 0;
        if (converting && code_point >= 0x10000)
        {
            units[count] = (uint16_t)(0xD800 + ((code_point - 0x10000) >> 10));
            units[count + 1] = (uint16_t)(0xDC00 + ((code_point - 0x10000) & 0x3FF));
            count += 2;
        }
        else if (converting)
        {
            units[count] = (uint16_t)code_point;
            count += 1;
        }
        at += sequence;
    }
    return count;
}

// A key's hash: FNV-1a of its bytes and whether they are UTF-16's.
static uint32_t check_data_cfstring_hash(const uint8_t* bytes, uint32_t count, bool utf16)
{
    uint32_t hash = 2166136261u ^ (utf16 ? 1u : 0u);
    for (uint32_t i = 0; i < count; i += 1)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

// The unit's constant CFStrings (#148), as clang's code makes them (GetAddrOfConstantCFString): one 32-byte
// __NSConstantString record of each key (CheckDataCFStringKey) in the section cfstring, writable, its fields the class
// reference's address, its flags (0x7c8, or 0x7d0 for UTF-16), its characters' address and its length; the characters an
// ASCII literal's own (its string pool object), or a UTF-16 array of their own, constant, aligned to 2. Each call
// (a CHECK_OP_BUILTIN_MISC node whose value is its object's address, check_builtin_misc's) takes the symbol of the call of
// its key whose object is lowest (CheckDataPrepared.cfstrings, which check_program_alias_number numbers), and its record
// is that call's alone, published only where it has a symbol. The class reference is the unit's declaration's of
// __CFConstantStringClassReference (CheckGraph.cfstring_class), or else the object of the lowest call's name, which the
// emission names so, an undefined global. `keys` holds `capacity` keys of `*key_count` found; the UTF-16 units go in
// `output`.
static bool check_data_cfstring_keys(const CheckInput* input, const CheckStream* stream, bool scalar, Arena* output, Arena* scratch, CheckDataCFStringKey** keys,
                                     uint32_t* key_count)
{
    const CheckGraph* graph = &input->graph;
    bool valid = true;
    uint32_t count = 0;
    CheckDataCFStringKey* found = graph->builtin_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataCFStringKey, graph->builtin_count) : 0;
    valid = graph->builtin_count == 0 || found != 0;
    for (uint32_t b = 0; valid && b < graph->builtin_count; b += 1)
    {
        const CheckBuiltinCall* call = graph->builtin_calls + b;
        const uint32_t node = call->node;
        const bool builtin = node < graph->count && graph->ops[node] == CHECK_OP_BUILTIN_MISC &&
                             (graph->members[node] == CBUILTIN_CFSTRING_MAKE_CONSTANT_STRING || graph->members[node] == CBUILTIN_NSSTRING_MAKE_CONSTANT_STRING);
        const CheckValue value = builtin ? check_stream_value(stream, node) : (CheckValue){ 0 };
        if (builtin && value.constant_kind == CHECK_CONSTANT_ADDRESS && value.object != 0 && (value.flags & BUSTER_CHECK_STATIC_OBJECT) != 0)
        {
            valid = call->nan_payload_known && call->cfstring_name != 0 && call->cfstring_name + 2 < value.object && value.object <= input->token_count &&
                    (call->nan_payload_length == 0 || call->nan_payload != 0);
            bool ascii = true;
            for (uint32_t i = 0; valid && i < call->nan_payload_length; i += 1)
            {
                ascii = ascii && call->nan_payload[i] != 0 && call->nan_payload[i] < 0x80;
            }
            CheckDataCFStringKey key = {
                .object = value.object,
                .name = call->cfstring_name,
                .literal = call->nan_payload_token + 1,
                .bytes = call->nan_payload,
                .byte_count = call->nan_payload_length,
                .length = call->nan_payload_length,
                .utf16 = !ascii,
            };
            if (valid && !ascii)
            {
                uint16_t* units = BUSTER_ARENA_PUSH_ARRAY(output, uint16_t, (uint64_t)call->nan_payload_length + 1);
                valid = units != 0;
                if (valid)
                {
                    const uint32_t converted = check_data_cfstring_utf16(call->nan_payload, call->nan_payload_length, units);
                    units[converted] = 0;
                    key.bytes = (const uint8_t*)units;
                    key.byte_count = (converted + 1) * 2;
                    key.length = converted;
                }
            }
            key.hash = check_data_cfstring_hash(key.bytes, key.byte_count, key.utf16);
            if (valid)
            {
                found[count] = key;
                count += 1;
            }
        }
    }
    // In object order (the calls' records are looked up by their objects, check_data_cfstring_canonical).
    uint64_t* order = valid && count > 1 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count) : 0;
    uint64_t* temporary = order != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count) : 0;
    CheckDataCFStringKey* sorted = temporary != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataCFStringKey, count) : 0;
    valid = valid && (count <= 1 || sorted != 0);
    for (uint32_t k = 0; valid && count > 1 && k < count; k += 1)
    {
        order[k] = (uint64_t)found[k].object << 32 | k;
    }
    const uint64_t* ordered = valid && count > 1 ? c_bindings_sort(order, temporary, count, !scalar, UINT32_MAX) : 0;
    for (uint32_t k = 0; ordered != 0 && k < count; k += 1)
    {
        sorted[k] = found[(uint32_t)ordered[k]];
    }
    *keys = ordered != 0 ? sorted : found;
    *key_count = count;
    return valid;
}

// Each key's canonical object (CheckDataPrepared.cfstrings): the lowest object of the keys of its bytes. The keys are
// sorted by their hashes, in their order (c_bindings_sort); within one hash, each key is compared with the distinct
// contents seen so far there (one, but for a collision), each of which keeps its keys' lowest object. Writes `count`
// calls into `cfstrings`.
static bool check_data_cfstring_unique(const CheckDataCFStringKey* keys, uint32_t count, bool scalar, Arena* scratch, CheckDataCFString* cfstrings)
{
    uint64_t* order = count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count) : 0;
    uint64_t* temporary = order != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, count) : 0;
    uint32_t* contents = temporary != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* lowest = contents != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    uint32_t* content_of = lowest != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, count) : 0;
    const bool valid = count == 0 || content_of != 0;
    for (uint32_t k = 0; valid && k < count; k += 1)
    {
        order[k] = (uint64_t)keys[k].hash << 32 | k;
    }
    const uint64_t* sorted = valid && count > 1 ? c_bindings_sort(order, temporary, count, !scalar, UINT32_MAX) : order;
    for (uint32_t start = 0; valid && start < count;)
    {
        uint32_t end = start + 1;
        while (end < count && sorted[end] >> 32 == sorted[start] >> 32)
        {
            end += 1;
        }
        uint32_t distinct = 0;
        for (uint32_t a = start; a < end; a += 1)
        {
            const uint32_t k = (uint32_t)sorted[a];
            const CheckDataCFStringKey* key = keys + k;
            uint32_t match = distinct;
            for (uint32_t c = 0; match == distinct && c < distinct; c += 1)
            {
                const CheckDataCFStringKey* other = keys + contents[c];
                const bool same = key->utf16 == other->utf16 && key->byte_count == other->byte_count &&
                                  (key->byte_count == 0 || memcmp(key->bytes, other->bytes, key->byte_count) == 0);
                match = same ? c : match;
            }
            if (match == distinct)
            {
                contents[distinct] = k;
                lowest[distinct] = key->object;
                distinct += 1;
            }
            lowest[match] = key->object < lowest[match] ? key->object : lowest[match];
            content_of[k] = match;
        }
        for (uint32_t a = start; a < end; a += 1)
        {
            const uint32_t k = (uint32_t)sorted[a];
            cfstrings[k] = (CheckDataCFString){ .object = keys[k].object, .canonical = lowest[content_of[k]] };
        }
        start = end;
    }
    return valid;
}

// The CFStrings' objects (check_data_cfstring_keys' rules), appended to `result`'s objects: each canonical key's UTF-16
// array (at its call's `(`'s object) and its record, whose plan is its four fields and whose references are the class
// reference and its characters.
static bool check_data_cfstring_objects(CheckDataPrepared* result, const CheckInput* input, const CheckDataCFStringKey* keys, uint32_t key_count,
                                        TypeTable* types, Arena* output)
{
    const TypeId record = type_unqualified(input->graph.ns_constant_string);
    const TypeId isa = type_pointer(types, type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST));
    const TypeId characters = type_pointer(types, type_qualify(types, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST));
    const TypeId unit = type_qualify(types, BUSTER_TYPE_USHORT, BUSTER_TYPE_CONST);
    bool valid = record != 0 && isa != 0 && characters != 0 && unit != 0 && type_is_complete(types, record) && type_size(types, record) == 32;
    for (uint32_t k = 0; valid && k < key_count; k += 1)
    {
        const CheckDataCFStringKey* key = keys + k;
        const bool canonical = result->cfstrings[k].canonical == key->object;
        const uint32_t characters_object = key->utf16 ? key->name + 2 : key->literal;
        // A canonical call's characters, which its later calls (in object order) take.
        const uint32_t canonical_at = canonical ? k : check_data_cfstring_index(result->cfstrings, k, result->cfstrings[k].canonical);
        result->cfstrings[k].characters = canonical ? characters_object : canonical_at < k ? result->cfstrings[canonical_at].characters : 0;
        if (canonical && key->utf16)
        {
            const TypeId array = type_array(types, unit, key->byte_count / 2);
            valid = array != 0 && check_data_latest(result, characters_object) == BUSTER_CHECK_NONE;
            if (valid)
            {
                const uint32_t at = result->count++;
                result->objects[at] = (CheckDataObject){
                    .type = array,
                    .object = characters_object,
                    .initializer = BUSTER_CHECK_NONE,
                    .token = key->name + 1,
                    .literal_bytes = key->bytes,
                    .literal_size = key->byte_count,
                    .string_pool = true,
                };
                result->reference_offsets[at] = result->reference_count;
                result->reference_counts[at] = 0;
            }
        }
        CheckInitializer* plan = valid && canonical ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitializer, 1) : 0;
        CheckInitPart* parts = plan != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckInitPart, 4) : 0;
        valid = valid && (!canonical || (parts != 0 && check_data_latest(result, key->object) == BUSTER_CHECK_NONE));
        if (valid && canonical)
        {
            const uint32_t token = key->object - 1;
            const CheckValue fields[4] = {
                { .type = isa, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_STATIC_OBJECT, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = result->cfstring_class },
                { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_ICE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = key->utf16 ? 0x7d0 : 0x7c8 },
                { .type = characters, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_STATIC_OBJECT, .constant_kind = CHECK_CONSTANT_ADDRESS, .object = characters_object },
                { .type = BUSTER_TYPE_LONG, .category = CHECK_RVALUE, .flags = BUSTER_CHECK_ICE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = key->length },
            };
            for (uint32_t f = 0; f < 4; f += 1)
            {
                parts[f] = (CheckInitPart){
                    .kind = CHECK_INIT_VALUE,
                    .type = fields[f].type,
                    .node = BUSTER_CHECK_NONE,
                    .token = token,
                    .bit_offset = (uint64_t)f * 64,
                    .value = fields[f],
                };
            }
            *plan = (CheckInitializer){ .type = record, .parts = parts, .count = 4, .status = CHECK_INIT_OK, .failed_token = BUSTER_CHECK_NONE };
            const uint32_t at = result->count++;
            result->objects[at] = (CheckDataObject){
                .type = record,
                .object = key->object,
                .initializer = BUSTER_CHECK_NONE,
                .token = token,
                .initializer_plan = plan,
                .optional = true,
            };
            result->reference_offsets[at] = result->reference_count;
            result->references[result->reference_count++] = result->cfstring_class;
            result->references[result->reference_count++] = characters_object;
            result->reference_counts[at] = 2;
        }
    }
    return valid;
}

static bool check_data_prepare(CheckDataPrepared* result, const CheckInput* input, const CheckStream* stream,
                               const CheckInitializers* initializers, TypeTable* types, bool scalar, Arena* output, Arena* scratch)
{
    *result = (CheckDataPrepared){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    bool valid = output != scratch && count < UINT32_MAX && stream->count == input->graph.count &&
                 stream->status == CHECK_STATUS_OK && stream->report_count == 0 && input->report_count == 0 && !input->resource &&
                 initializers->status == CHECK_INIT_OK && initializers->token_count == count && (initializers->count == 0 || (initializers->plans != 0 && initializers->declaration_tokens != 0));
    result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_INVALID;
    uint64_t* keys = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    uint64_t* sorting = keys != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint64_t, (uint64_t)count + 1) : 0;
    if (valid && sorting == 0)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    uint32_t declared = 0;
    uint32_t varying = 0;
    for (uint32_t first = 0; valid && first < count; first += 64)
    {
        const uint64_t lanes = scalar ? check_input_scan_all(first, count) : check_input_scan_bit(input->declaration_bits, first, count, BUSTER_CHECK_BITS_STATIC);
        for (uint64_t selected = lanes; valid && selected != 0; selected &= selected - 1)
        {
            const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
            if (input->declarations[token] == token && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_STATIC))
            {
                const uint32_t object = input->objects[token];
                valid = object != 0 && object <= count;
                if (valid)
                {
                    keys[declared++] = (uint64_t)object << 32 | token;
                    varying |= object;
                }
                else
                {
                    result->status = CHECK_DATA_INPUT_INVALID;
                    result->token = token;
                }
            }
        }
    }
    const uint64_t* sorted = valid ? c_bindings_sort(keys, sorting, declared, !scalar, varying) : 0;
    result->declaration_keys = valid && declared != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint64_t, declared) : 0;
    if (result->declaration_keys != 0)
    {
        memcpy(result->declaration_keys, sorted, (uint64_t)declared * sizeof(uint64_t));
        result->declaration_count = declared;
    }
    // Each static object's last declaration (the walk's last in token order), its definition (its first declaration that
    // defines it, or else its last tentative definition) and its largest alignment, in object order.
    result->declared_objects = result->declaration_keys != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, declared) : 0;
    result->declared_latest = result->declared_objects != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, declared) : 0;
    uint32_t* definition = result->declared_latest != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, declared) : 0;
    uint8_t* alignments = definition != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, declared) : 0;
    if (valid && declared != 0 && alignments == 0)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    uint32_t objects = 0;
    for (uint32_t i = 0; valid && i < declared; i += 1)
    {
        const uint32_t object = (uint32_t)(sorted[i] >> 32);
        const uint32_t token = (uint32_t)sorted[i];
        if (objects == 0 || result->declared_objects[objects - 1] != object)
        {
            result->declared_objects[objects] = object;
            definition[objects] = BUSTER_CHECK_NONE;
            alignments[objects] = 0;
            objects += 1;
        }
        const uint32_t at = objects - 1;
        result->declared_latest[at] = token;
        const CheckInputAttributes* attributes = check_sparse_wide_present(input->attributes) ? check_input_attributes_at(input, token) : 0;
        if (attributes != 0 && attributes->alignment_log2 > alignments[at])
        {
            alignments[at] = attributes->alignment_log2;
        }
        if (input->declaration_kinds[token] == C_BINDING_OBJECT && (check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_DEFINED) || check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_TENTATIVE)))
        {
            const uint32_t previous = definition[at];
            if (previous == BUSTER_CHECK_NONE || !check_input_bit(input->declaration_bits, previous, BUSTER_CHECK_BITS_DEFINED))
            {
                definition[at] = token;
            }
        }
    }
    result->declared_count = objects;
    // The constant CFStrings' calls (#148), in a unit with one: a record and an array of each, two references of each.
    CheckDataCFStringKey* cfstring_keys = 0;
    uint32_t cfstring_key_count = 0;
    if (valid && input->graph.ns_constant_string != 0)
    {
        valid = check_data_cfstring_keys(input, stream, scalar, output, scratch, &cfstring_keys, &cfstring_key_count);
        result->cfstrings = valid && cfstring_key_count != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDataCFString, cfstring_key_count) : 0;
        valid = valid && (cfstring_key_count == 0 || result->cfstrings != 0) &&
                check_data_cfstring_unique(cfstring_keys, cfstring_key_count, scalar, scratch, result->cfstrings);
        result->cfstring_count = valid ? cfstring_key_count : 0;
        // The class reference: the unit's declaration's, or the lowest call's name's object, an undefined global's.
        uint32_t lowest = 0;
        for (uint32_t k = 0; valid && k < cfstring_key_count; k += 1)
        {
            lowest = lowest == 0 || cfstring_keys[k].object < cfstring_keys[lowest - 1].object ? k + 1 : lowest;
        }
        result->cfstring_class_own = valid && lowest != 0 && input->graph.cfstring_class == 0;
        result->cfstring_class = input->graph.cfstring_class != 0 ? input->graph.cfstring_class : lowest != 0 ? cfstring_keys[lowest - 1].name + 1 : 0;
        result->status = valid ? result->status : CHECK_DATA_INPUT_RESOURCE;
    }
    uint64_t object_capacity = (uint64_t)input->string_count + 2 * (uint64_t)cfstring_key_count;
    uint64_t reference_capacity = 2 * (uint64_t)cfstring_key_count;
    for (uint32_t at = 0; valid && at < objects; at += 1)
    {
        const uint32_t token = definition[at];
        if (token != BUSTER_CHECK_NONE)
        {
            object_capacity += 1;
            const uint32_t plan_index = check_initializers_plan_index(initializers, token);
            valid = plan_index <= initializers->count &&
                    (plan_index == 0 || (initializers->declaration_tokens != 0 && initializers->declaration_tokens[plan_index - 1] == token));
            reference_capacity += valid && plan_index != 0 ? initializers->plans[plan_index - 1].count : 0;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_INVALID;
                result->token = token;
            }
        }
    }
    if (valid && (object_capacity > UINT32_MAX || reference_capacity > UINT32_MAX))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    if (valid)
    {
        result->objects = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, CheckDataObject, object_capacity) : 0;
        result->reference_offsets = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, object_capacity) : 0;
        result->reference_counts = object_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, object_capacity) : 0;
        result->references = reference_capacity != 0 ? BUSTER_ARENA_PUSH_ARRAY(output, uint32_t, reference_capacity) : 0;
        valid = (object_capacity == 0 || (result->objects != 0 && result->reference_offsets != 0 && result->reference_counts != 0)) &&
                (reference_capacity == 0 || result->references != 0);
        result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_RESOURCE;
    }
    for (uint32_t at = 0; valid && at < objects; at += 1)
    {
        const uint32_t selected = definition[at];
        if (selected != BUSTER_CHECK_NONE)
        {
            const uint32_t object = result->declared_objects[at];
            const uint32_t latest = result->declared_latest[at];
            const uint32_t plan_index = check_initializers_plan_index(initializers, selected);
            const CheckInitializer* plan = plan_index != 0 ? initializers->plans + plan_index - 1 : 0;
            TypeId type = input->declared_types[latest];
            const TypeId stripped = type_strip_aligned(types, type);
            if (plan == 0 && check_input_bit(input->declaration_bits, selected, BUSTER_CHECK_BITS_TENTATIVE) && type_kind(types, stripped) == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
            {
                // Tentative arrays acquire one element at translation-unit end; earlier uses retain their
                // historical incomplete TypeIds and cannot gain a retroactive sizeof result.
                type = type_array(types, types->types.base[type_index(stripped)], 1);
                if (type != 0)
                {
                    type |= type_qualifiers(stripped);
                    if (type_kind(types, input->declared_types[latest]) == BUSTER_TYPE_KIND_ALIGNED)
                    {
                        type = type_aligned(types, type, type_alignment_log2(types, input->declared_types[latest]));
                    }
                }
                valid = type != 0;
                if (!valid)
                {
                    result->status = types->last_error == TYPE_ERROR_FULL ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
                    result->token = selected;
                }
            }
            if (valid)
            {
                const uint32_t written = result->count;
                result->objects[written] = (CheckDataObject){
                    .type = type,
                    .object = object,
                    .initializer = check_sparse_get(input->initializer_nodes, selected),
                    .token = selected,
                    .initializer_plan = plan,
                    .alignment_log2 = check_data_declared_alignment(input, types, object, type, alignments[at]),
                };
                result->reference_offsets[written] = result->reference_count;
                const uint64_t reference_mark = scratch->length;
                uint32_t* references = 0;
                uint32_t reference_count = 0;
                if (plan != 0)
                {
                    // Use the opposite arena roles for this temporary result. The helper restores its scratch
                    // suffix in output, and we copy the surviving references before releasing its result suffix.
                    const CheckInitStatus status = check_initializer_references(types, plan, scratch, output, &references, &reference_count);
                    valid = status == CHECK_INIT_OK;
                    if (!valid)
                    {
                        result->status = status == CHECK_INIT_RESOURCE ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
                        result->token = selected;
                    }
                }
                valid = valid && reference_count <= reference_capacity - result->reference_count;
                for (uint32_t reference = 0; valid && reference < reference_count; reference += 1)
                {
                    valid = references[reference] != 0 && references[reference] <= count;
                    if (valid)
                    {
                        // A constant CFString's call is its canonical record's reference (#148).
                        result->references[result->reference_count++] = check_data_cfstring_canonical(result->cfstrings, result->cfstring_count, references[reference]);
                    }
                }
                if (!valid && result->status == CHECK_DATA_INPUT_OK)
                {
                    result->status = CHECK_DATA_INPUT_INVALID;
                    result->token = selected;
                }
                arena_pop_to(scratch, reference_mark);
                result->reference_counts[written] = result->reference_count - result->reference_offsets[written];
                result->count += 1;
            }
        }
    }
    for (uint32_t i = 0; valid && i < input->string_count; i += 1)
    {
        const CheckInitString* string = input->strings + i;
        valid = string->object != 0 && string->object <= count && check_data_latest(result, string->object) == BUSTER_CHECK_NONE;
        if (valid)
        {
            const uint32_t at = result->count++;
            result->objects[at] = (CheckDataObject){
                .type = string->type,
                .object = string->object,
                .initializer = BUSTER_CHECK_NONE,
                .token = string->token,
                .literal_bytes = string->bytes,
                .literal_size = string->byte_count,
                .string_pool = true,
            };
            result->reference_offsets[at] = result->reference_count;
            result->reference_counts[at] = 0;
        }
        else
        {
            result->status = CHECK_DATA_INPUT_INVALID;
            result->token = string->token;
        }
    }
    result->cfstring_reference_first = result->reference_count;
    if (valid && cfstring_key_count != 0)
    {
        valid = check_data_cfstring_objects(result, input, cfstring_keys, cfstring_key_count, types, output);
        result->status = valid ? result->status : types->last_error == TYPE_ERROR_FULL ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID;
    }
    if (valid)
    {
        result->object_count = count + 1;
        result->capacity = (LinkCapacity){
            .atoms = result->count,
            .fixups = result->reference_count,
            .wide_addends = result->reference_count,
        };
    }
    else
    {
        const CheckDataInputStatus status = result->status;
        const uint32_t token = result->token;
        arena_pop_to(output, output_mark);
        *result = (CheckDataPrepared){ .status = status, .token = token };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

static bool check_data_publish(const CheckDataPrepared* prepared, const TypeTable* types, const CheckStream* stream,
                               const SymbolId* object_symbols, uint32_t object_count, Link* link, uint32_t unit,
                               Arena* bytes, Arena* scratch, CheckDataResult* result)
{
    const uint64_t mark = scratch->length;
    bool valid = prepared->status == CHECK_DATA_INPUT_OK && object_count == prepared->object_count && object_symbols != 0;
    *result = (CheckDataResult){ .status = valid ? CHECK_DATA_OK : CHECK_DATA_INVALID, .failed_token = BUSTER_CHECK_NONE };
    CheckDataObject* objects = valid && prepared->count != 0 ? BUSTER_ARENA_PUSH_ARRAY(scratch, CheckDataObject, prepared->count) : 0;
    if (valid && prepared->count != 0 && objects == 0)
    {
        valid = false;
        result->status = CHECK_DATA_RESOURCE;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; valid && i < prepared->count; i += 1)
    {
        const CheckDataObject* object = prepared->objects + i;
        valid = object->object < object_count;
        if (!valid)
        {
            result->status = CHECK_DATA_INVALID;
            result->failed_token = object->token;
        }
        else if ((!object->string_pool && !object->optional) || object_symbols[object->object] != 0)
        {
            objects[count++] = *object;
        }
    }
    if (valid)
    {
        valid = check_data_emit(types, stream, objects, count, object_symbols, object_count, link, unit, bytes, scratch, result);
    }
    arena_pop_to(scratch, mark);
    return valid;
}

static bool check_data_input_prepared(CheckDataInput* result, const CheckInput* input, const CheckStream* stream,
                                      const CheckInitializers* initializers, const Parser* parser, const InternTable* identifiers,
                                      TypeTable* types, Arena* output, Arena* bytes, Arena* scratch)
{
    *result = (CheckDataInput){ .token = BUSTER_CHECK_NONE };
    const uint64_t output_mark = output->length;
    const uint64_t bytes_mark = bytes->length;
    const uint64_t scratch_mark = scratch->length;
    const uint32_t count = input->token_count;
    bool valid = output != scratch && bytes != scratch && output != bytes && count == parser->tokens.count;
    CheckDataPrepared prepared = { .status = CHECK_DATA_INPUT_INVALID, .token = BUSTER_CHECK_NONE };
    if (valid)
    {
        valid = check_data_prepare(&prepared, input, stream, initializers, types, parser->options.scalar, output, scratch);
    }
    result->status = prepared.status;
    result->token = prepared.token;
    uint8_t* needed = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint8_t, (uint64_t)count + 1) : 0;
    uint32_t* definitions = valid ? BUSTER_ARENA_PUSH_ARRAY(scratch, uint32_t, (uint64_t)count + 1) : 0;
    if (valid && (needed == 0 || definitions == 0))
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_RESOURCE;
    }
    if (valid)
    {
        memset(needed, 0, (uint64_t)count + 1);
        memset(definitions, 0, ((uint64_t)count + 1) * sizeof(uint32_t));
        for (uint32_t i = 0; i < prepared.count; i += 1)
        {
            const CheckDataObject* object = prepared.objects + i;
            definitions[object->object] = i + 1;
            needed[object->object] = !object->string_pool;
        }
        for (uint32_t i = 0; i < prepared.reference_count; i += 1)
        {
            needed[prepared.references[i]] = 1;
        }
    }
    uint32_t symbol_count = 0;
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        if (needed[object])
        {
            const uint32_t token = check_data_latest(&prepared, object);
            valid = definitions[object] != 0 || (token != BUSTER_CHECK_NONE && check_input_linkage(input->declaration_bits, token) == 2);
            if (valid)
            {
                symbol_count += 1;
            }
            else
            {
                // Internal function addresses require the program consumer's real code atom.
                result->status = CHECK_DATA_INPUT_UNSUPPORTED;
                result->token = token;
            }
        }
    }
    const CheckGraph* graph = stream->graph;
    const bool overloads = graph != NULL && graph->overload_candidate_count != 0;
    String8* overload_names = valid && overloads ? BUSTER_ARENA_PUSH_ARRAY(scratch, String8, (uint64_t)count + 1) : NULL;
    if (valid && overloads)
    {
        valid = overload_names != NULL;
        result->status = valid ? result->status : CHECK_DATA_INPUT_RESOURCE;
        if (valid)
        {
            memset(overload_names, 0, ((uint64_t)count + 1) * sizeof(String8));
        }
    }
    // Like the program consumer, prefer an explicit asm label, including one on an earlier declaration. The
    // standalone consumer only needs function names when static data refers to them; ordinary data stays unchanged.
    for (uint32_t d = 0; valid && overloads && d < prepared.declaration_count; d += 1)
    {
        const uint32_t token = (uint32_t)prepared.declaration_keys[d];
        const uint32_t object = (uint32_t)(prepared.declaration_keys[d] >> 32);
        const CheckInputAttributes* attributes = check_input_attributes_at(input, token);
        if (needed[object] && input->declaration_kinds[token] == C_BINDING_FUNCTION && attributes != NULL)
        {
            for (uint32_t i = 0; valid && i < attributes->count; i += 1)
            {
                const CheckInputAttribute* attribute = attributes->records + i;
                if (!attribute->ignored && attribute->kind == CHECK_ATTRIBUTE_ASM_LABEL)
                {
                    const String8 previous = overload_names[object];
                    valid = previous.length == 0 || (previous.length == attribute->text.length &&
                            memcmp(previous.pointer, attribute->text.pointer, previous.length) == 0);
                    overload_names[object] = attribute->text;
                    if (!valid)
                    {
                        result->status = CHECK_DATA_INPUT_INVALID;
                        result->token = attribute->name_token;
                    }
                }
            }
        }
    }
    CheckMangleSource mangling = {
        .input = input,
        .parser = parser,
        .identifiers = identifiers,
        .types = types,
        .output = scratch,
        .scalar = parser->options.scalar,
    };
    Scratch mangle_scratch = { 0 };
    for (uint32_t i = 0; valid && overloads && i < graph->overload_candidate_count; i += 1)
    {
        const CheckOverloadCandidate* candidate = graph->overload_candidates + i;
        const uint32_t object = candidate->object;
        if ((candidate->flags & BUSTER_CHECK_OVERLOADABLE) != 0 && object != 0 && object <= count && needed[object] && overload_names[object].length == 0)
        {
            if (mangle_scratch.arena == NULL)
            {
                mangle_scratch = scratch_begin(scratch);
            }
            mangling.object_size_compat = candidate->object_sizes != NULL;
            const CheckMangleStatus status = check_mangle_name(&mangling, candidate->type, parser->tokens.names[candidate->declaration],
                                                               mangle_scratch.arena, &overload_names[object]);
            valid = status == CHECK_MANGLE_OK;
            if (!valid)
            {
                result->status = status == CHECK_MANGLE_RESOURCE ? CHECK_DATA_INPUT_RESOURCE :
                                 status == CHECK_MANGLE_INVALID ? CHECK_DATA_INPUT_INVALID : CHECK_DATA_INPUT_UNSUPPORTED;
                result->token = candidate->declaration;
            }
        }
    }
    if (mangle_scratch.arena != NULL)
    {
        scratch_end(mangle_scratch);
    }
    if (valid)
    {
        LinkCapacity capacity = prepared.capacity;
        capacity.names = symbol_count;
        capacity.symbols = symbol_count;
        result->object_symbols = BUSTER_ARENA_PUSH_ARRAY(output, SymbolId, (uint64_t)count + 1);
        valid = result->object_symbols != 0 && link_initialize(&result->link, output, output, capacity);
        result->status = valid ? CHECK_DATA_INPUT_OK : CHECK_DATA_INPUT_RESOURCE;
        if (valid)
        {
            memset(result->object_symbols, 0, ((uint64_t)count + 1) * sizeof(SymbolId));
            result->object_count = count + 1;
        }
    }
    for (uint32_t object = 1; valid && object <= count; object += 1)
    {
        if (needed[object])
        {
            const uint32_t token = check_data_latest(&prepared, object);
            const bool literal = token == BUSTER_CHECK_NONE;
            const bool compound = !literal && input->declaration_bits != 0 && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_COMPOUND);
            const uint32_t name = !literal ? parser->tokens.names[token] : 0;
            uint8_t generated_name[48];
            const int generated_length = compound ? snprintf((char*)generated_name, sizeof(generated_name), ".Lcompound.%u", object) : 0;
            const String8 renamed = overload_names != NULL ? overload_names[object] : (String8){ 0 };
            const NameId link_name = compound ? link_add_name(&result->link, generated_name, (uint32_t)generated_length) :
                                     renamed.length != 0 ? link_add_name(&result->link, (const uint8_t*)renamed.pointer, (uint32_t)renamed.length) :
                                     !literal ? link_add_name(&result->link, intern_bytes(identifiers, name), intern_length(identifiers, name)) : 0;
            const uint8_t kind = !literal && input->declaration_kinds[token] == C_BINDING_FUNCTION ? BUSTER_LINK_SYMBOL_FUNCTION :
                                 !literal && check_input_bit(input->declaration_bits, token, BUSTER_CHECK_BITS_THREAD_LOCAL) ? BUSTER_LINK_SYMBOL_TLS : BUSTER_LINK_SYMBOL_OBJECT;
            const uint8_t binding = !literal && check_input_linkage(input->declaration_bits, token) == 2 ? BUSTER_LINK_SYMBOL_GLOBAL : BUSTER_LINK_SYMBOL_LOCAL;
            result->object_symbols[object] = literal || link_name != 0 ? link_add_symbol(&result->link, link_name, 0, 0, 0, kind | binding, BUSTER_LINK_SOURCE_NONE) : 0;
            valid = result->object_symbols[object] != 0;
            if (!valid)
            {
                result->status = CHECK_DATA_INPUT_RESOURCE;
                result->token = token;
            }
        }
    }
    if (valid)
    {
        valid = check_data_publish(&prepared, types, stream, result->object_symbols, count + 1, &result->link, 0, bytes, scratch, &result->emission);
        result->status = check_data_input_status(result->emission.status);
        result->token = result->emission.failed_token;
    }
    if (valid && link_check(&result->link) != LINK_ERROR_NONE)
    {
        valid = false;
        result->status = CHECK_DATA_INPUT_INVALID;
    }
    if (!valid)
    {
        const CheckDataInputStatus status = result->status;
        const uint32_t token = result->token;
        const CheckDataResult emission = result->emission;
        arena_pop_to(output, output_mark);
        arena_pop_to(bytes, bytes_mark);
        *result = (CheckDataInput){ .status = status, .token = token, .emission = emission };
    }
    arena_pop_to(scratch, scratch_mark);
    return valid;
}

static bool check_data_input(CheckDataInput* result, const CheckInput* input, const CheckStream* stream, const Parser* parser,
                             const InternTable* identifiers, TypeTable* types, Arena* output, Arena* bytes, Arena* scratch)
{
    const uint64_t mark = output->length;
    CheckInitializers initializers;
    bool valid = check_initializers_run(&initializers, input, stream, parser, types, output, scratch);
    if (valid)
    {
        valid = check_data_input_prepared(result, input, stream, &initializers, parser, identifiers, types, output, bytes, scratch);
    }
    else
    {
        *result = (CheckDataInput){
            .status = initializers.status == CHECK_INIT_RESOURCE ? CHECK_DATA_INPUT_RESOURCE : CHECK_DATA_INPUT_INVALID,
            .token = initializers.token,
        };
    }
    if (!valid)
    {
        arena_pop_to(output, mark);
    }
    return valid;
}
