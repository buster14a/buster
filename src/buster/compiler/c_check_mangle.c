// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright 2026 Buster contributors.
//
// C overloadable symbols use the Itanium function-name grammar, not the source spelling or a compiler-private id.
// check_mangle_name omits the outer function's return type and encodes its adjusted parameter types. Its explicit
// task stack handles arbitrarily nested declarators; completed compound types enter the substitution list in ABI
// order. Aligned typedefs have their canonical identity, while GNU and extended vectors remain distinct types.
// The caller supplies only selected objects, once each, and handles explicit asm labels before asking for a name.
// Unsupported signature forms fail closed: no partially mangled name is published. This is not a C++ mangler.
#ifndef BUSTER_C_CHECK_MANGLE_INCLUDED
#define BUSTER_C_CHECK_MANGLE_INCLUDED

typedef enum CheckMangleStatus
{
    CHECK_MANGLE_OK,
    CHECK_MANGLE_RESOURCE,
    CHECK_MANGLE_UNSUPPORTED,
    CHECK_MANGLE_INVALID,
} CheckMangleStatus;

typedef struct CheckMangleSource CheckMangleSource;
struct CheckMangleSource
{
    const CheckInput* input;
    const Parser* parser;
    const InternTable* identifiers;
    const TypeTable* types;
    Arena* output;
    bool scalar;
    // Built only when a selected signature contains a named tag. Local/anonymous tags need lexical ABI names that
    // the type table alone cannot supply; never confuse one with a file-scope tag of the same spelling.
    uint8_t* global_tags;
};

typedef enum CheckMangleTaskKind
{
    CHECK_MANGLE_TYPE,
    CHECK_MANGLE_REMEMBER,
    CHECK_MANGLE_CHARACTER,
} CheckMangleTaskKind;

typedef struct CheckMangleTask CheckMangleTask;
struct CheckMangleTask
{
    CheckMangleTaskKind kind;
    TypeId type;
};

typedef struct CheckMangle CheckMangle;
struct CheckMangle
{
    CheckMangleSource* source;
    Arena* scratch;
    void* bytes;
    uint64_t byte_capacity;
    uint32_t length;
    void* tasks;
    uint64_t task_capacity;
    uint32_t task_count;
    void* substitutions;
    uint64_t substitution_capacity;
    uint32_t substitution_count;
    bool aligned_substitution;
    CheckMangleStatus status;
};

static void check_mangle_bytes(CheckMangle* writer, const void* bytes, uint64_t length)
{
    if (writer->status == CHECK_MANGLE_OK)
    {
        const uint64_t end = writer->length + length;
        const bool room = end <= UINT32_MAX && type_grow_stack(writer->scratch, &writer->bytes, &writer->byte_capacity, end, 1);
        writer->status = room ? CHECK_MANGLE_OK : CHECK_MANGLE_RESOURCE;
        if (room)
        {
            memcpy((uint8_t*)writer->bytes + writer->length, bytes, length);
            writer->length = (uint32_t)end;
        }
    }
}

static void check_mangle_text(CheckMangle* writer, const char* text)
{
    check_mangle_bytes(writer, text, strlen(text));
}

static void check_mangle_number(CheckMangle* writer, uint64_t number, uint32_t radix)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char bytes[32];
    uint32_t begin = sizeof(bytes);
    do
    {
        bytes[--begin] = digits[number % radix];
        number /= radix;
    } while (number != 0);
    check_mangle_bytes(writer, bytes + begin, sizeof(bytes) - begin);
}

static void check_mangle_push(CheckMangle* writer, CheckMangleTaskKind kind, TypeId type)
{
    if (writer->status == CHECK_MANGLE_OK)
    {
        const bool room = writer->task_count < UINT32_MAX &&
                          type_grow_stack(writer->scratch, &writer->tasks, &writer->task_capacity, (uint64_t)writer->task_count + 1, sizeof(CheckMangleTask));
        writer->status = room ? CHECK_MANGLE_OK : CHECK_MANGLE_RESOURCE;
        if (room)
        {
            ((CheckMangleTask*)writer->tasks)[writer->task_count++] = (CheckMangleTask){ .kind = kind, .type = type };
        }
    }
}

// The first completed occurrence of this canonical type. Exact ids are compared sixteen at a time; only aligned
// typedef wrappers need structural comparison. The scalar path is the executable twin of the id scan.
static uint32_t check_mangle_substitution(CheckMangle* writer, TypeId type)
{
    const TypeTable* types = writer->source->types;
    const TypeId* previous = writer->substitutions;
    uint32_t found = BUSTER_CHECK_NONE;
    for (uint32_t first = 0; found == BUSTER_CHECK_NONE && first < writer->substitution_count; first += 16)
    {
        const uint32_t count = writer->substitution_count - first < 16 ? writer->substitution_count - first : 16;
        uint32_t matches = 0;
        if (BUSTER_SCALAR_PATH(writer->source->scalar))
        {
            for (uint32_t lane = 0; lane < count; lane += 1)
            {
                matches |= (uint32_t)(previous[first + lane] == type) << lane;
            }
        }
        else
        {
            const __mmask16 active = (__mmask16)_bzhi_u32(0xffff, count);
            const __m512i values = _mm512_maskz_loadu_epi32(active, previous + first);
            matches = _mm512_mask_cmpeq_epi32_mask(active, values, _mm512_set1_epi32((int32_t)type));
        }
        found = matches != 0 ? first + (uint32_t)__builtin_ctz(matches) : found;
    }
    const bool aligned = (types->types.flags[type_index(type)] & BUSTER_TYPE_FLAG_ALIGNED_INSIDE) != 0;
    for (uint32_t i = 0; found == BUSTER_CHECK_NONE && writer->status == CHECK_MANGLE_OK &&
                         (aligned || writer->aligned_substitution) && i < writer->substitution_count; i += 1)
    {
        const uint64_t mark = writer->scratch->length;
        TypePairMemo memo = { .arena = writer->scratch };
        const TypeAnswer equal = type_canonical_compare(types, type, previous[i], &memo, writer->scratch);
        writer->status = equal == TYPE_ANSWER_FULL ? CHECK_MANGLE_RESOURCE : writer->status;
        found = equal == TYPE_ANSWER_YES ? i : found;
        arena_pop_to(writer->scratch, mark);
    }
    return found;
}

// A sparse source pass, only for nominal signatures, retains which tags are outside function bodies. It visits the
// same declaration/body events on both paths. A tag reference cannot change the scope of its original declaration.
static bool check_mangle_global_tags(CheckMangleSource* source)
{
    const uint32_t tags = source->types->tags.count;
    bool valid = source->global_tags != NULL;
    if (!valid)
    {
        source->global_tags = BUSTER_ARENA_PUSH_ARRAY(source->output, uint8_t, tags);
        valid = source->global_tags != NULL;
        if (valid)
        {
            memset(source->global_tags, 0, tags);
            const CheckInput* input = source->input;
            const Parser* parser = source->parser;
            uint32_t body_end = 0;
            for (uint32_t first = 0; first < input->token_count; first += 64)
            {
                const uint64_t lanes = BUSTER_SCALAR_PATH(source->scalar) ? check_input_scan_all(first, input->token_count) :
                                       check_input_scan_equal(input->declaration_kinds, first, input->token_count, C_BINDING_TAG_KIND) |
                                       check_input_scan_equal(parser->tree.roles, first, input->token_count, PARSE_ROLE_OPEN_BODY);
                for (uint64_t selected = lanes; selected != 0; selected &= selected - 1)
                {
                    const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
                    if (parser->tree.roles[token] == PARSE_ROLE_OPEN_BODY)
                    {
                        body_end = parser->tree.parents[token];
                    }
                    if (input->declaration_kinds[token] == C_BINDING_TAG_KIND && input->declarations[token] == token && token >= body_end)
                    {
                        const TypeId type = type_strip_aligned(source->types, input->declared_types[token]);
                        const uint32_t tag = source->types->types.index[type_index(type)];
                        if (tag < tags)
                        {
                            source->global_tags[tag] = 1;
                        }
                    }
                }
            }
        }
    }
    return valid;
}

static void check_mangle_parameters(CheckMangle* writer, TypeId function, bool outer)
{
    const TypeTable* types = writer->source->types;
    uint32_t count = 0;
    const TypeId* parameters = type_parameters(types, function, &count);
    const uint8_t flags = types->types.flags[type_index(function)];
    if ((flags & BUSTER_TYPE_FLAG_VARIADIC) != 0)
    {
        check_mangle_push(writer, CHECK_MANGLE_CHARACTER, 'z');
    }
    else if (count == 0 && (outer || (flags & BUSTER_TYPE_FLAG_PROTOTYPE) != 0))
    {
        check_mangle_push(writer, CHECK_MANGLE_CHARACTER, 'v');
    }
    for (uint32_t i = count; i != 0; i -= 1)
    {
        check_mangle_push(writer, CHECK_MANGLE_TYPE, type_unqualified(parameters[i - 1]));
    }
}

static void check_mangle_type(CheckMangle* writer, TypeId original)
{
    const TypeTable* types = writer->source->types;
    if (!type_is_valid(types, original))
    {
        writer->status = CHECK_MANGLE_INVALID;
    }
    else
    {
        const TypeId type = type_strip_aligned(types, original);
        const uint32_t index = type_index(type);
        const uint32_t qualifiers = type_qualifiers(type);
        const uint8_t kind = type_kind(types, type);
        const bool array = type_kind_is_array(kind);
        const bool builtin = qualifiers == 0 && (kind == BUSTER_TYPE_KIND_VOID || kind == BUSTER_TYPE_KIND_INTEGER ||
                                                 kind == BUSTER_TYPE_KIND_FLOATING || kind == BUSTER_TYPE_KIND_NULLPTR);
        const uint32_t substitution = !builtin ? check_mangle_substitution(writer, type) : BUSTER_CHECK_NONE;
        if (substitution != BUSTER_CHECK_NONE)
        {
            check_mangle_text(writer, "S");
            if (substitution != 0)
            {
                check_mangle_number(writer, substitution - 1, 36);
            }
            check_mangle_text(writer, "_");
        }
        else
        {
            if (!builtin)
            {
                check_mangle_push(writer, CHECK_MANGLE_REMEMBER, type);
            }
            if (qualifiers != 0 && !array)
            {
                check_mangle_text(writer, (qualifiers & BUSTER_TYPE_RESTRICT) != 0 ? "r" : "");
                check_mangle_text(writer, (qualifiers & BUSTER_TYPE_VOLATILE) != 0 ? "V" : "");
                check_mangle_text(writer, (qualifiers & BUSTER_TYPE_CONST) != 0 ? "K" : "");
                check_mangle_push(writer, CHECK_MANGLE_TYPE, type_unqualified(type));
            }
            else if (builtin)
            {
                static const char* const codes[BUSTER_TYPE_CODE_NULLPTR + 1] = {
                    NULL, "v", "b", "c", "a", "h", "s", "t", "i", "j", "l", "m", "x", "y", "n", "o",
                    "DF16b", "DF16_", "Dh", "f", "d", "e", "g", "Dn",
                };
                const uint32_t code = types->types.code[index];
                if (code < BUSTER_ARRAY_LENGTH(codes) && codes[code] != NULL)
                {
                    check_mangle_text(writer, codes[code]);
                }
                else
                {
                    writer->status = CHECK_MANGLE_UNSUPPORTED;
                }
            }
            else if (kind == BUSTER_TYPE_KIND_POINTER || kind == BUSTER_TYPE_KIND_COMPLEX || kind == BUSTER_TYPE_KIND_ATOMIC)
            {
                check_mangle_text(writer, kind == BUSTER_TYPE_KIND_POINTER ? "P" : kind == BUSTER_TYPE_KIND_COMPLEX ? "C" : "U7_Atomic");
                check_mangle_push(writer, CHECK_MANGLE_TYPE, type_element(types, type));
            }
            else if (kind == BUSTER_TYPE_KIND_ARRAY || kind == BUSTER_TYPE_KIND_INCOMPLETE_ARRAY)
            {
                check_mangle_text(writer, "A");
                if (kind == BUSTER_TYPE_KIND_ARRAY)
                {
                    check_mangle_number(writer, types->types.extent[index], 10);
                }
                check_mangle_text(writer, "_");
                check_mangle_push(writer, CHECK_MANGLE_TYPE, type_element(types, type));
            }
            else if (kind == BUSTER_TYPE_KIND_FUNCTION)
            {
                const TypeFunctionABI abi = type_function_abi_kind(types, type);
                if (abi != TYPE_FUNCTION_ABI_SYSV && abi != TYPE_FUNCTION_ABI_MICROSOFT)
                {
                    writer->status = CHECK_MANGLE_UNSUPPORTED;
                }
                check_mangle_text(writer, abi == TYPE_FUNCTION_ABI_MICROSOFT ? "U6ms_abiF" : "F");
                check_mangle_push(writer, CHECK_MANGLE_CHARACTER, 'E');
                check_mangle_parameters(writer, type, false);
                check_mangle_push(writer, CHECK_MANGLE_TYPE, type_return(types, type));
            }
            else if (kind == BUSTER_TYPE_KIND_VECTOR || kind == BUSTER_TYPE_KIND_EXT_VECTOR || kind == BUSTER_TYPE_KIND_BIT_INT)
            {
                check_mangle_text(writer, kind != BUSTER_TYPE_KIND_BIT_INT ? "Dv" :
                                           (types->types.flags[index] & BUSTER_TYPE_FLAG_UNSIGNED) != 0 ? "DU" : "DB");
                check_mangle_number(writer, types->types.extent[index], 10);
                check_mangle_text(writer, "_");
                if (kind != BUSTER_TYPE_KIND_BIT_INT)
                {
                    check_mangle_push(writer, CHECK_MANGLE_TYPE, type_element(types, type));
                }
            }
            else if (kind == BUSTER_TYPE_KIND_STRUCT || kind == BUSTER_TYPE_KIND_UNION || kind == BUSTER_TYPE_KIND_ENUM)
            {
                const uint32_t tag = types->types.index[index];
                const uint32_t name = types->tags.name[tag];
                if (!check_mangle_global_tags(writer->source))
                {
                    writer->status = CHECK_MANGLE_RESOURCE;
                }
                else if (name == 0 || !writer->source->global_tags[tag])
                {
                    writer->status = CHECK_MANGLE_UNSUPPORTED;
                }
                else
                {
                    const uint32_t length = intern_length(writer->source->identifiers, name);
                    check_mangle_number(writer, length, 10);
                    check_mangle_bytes(writer, intern_bytes(writer->source->identifiers, name), length);
                }
            }
            else
            {
                writer->status = CHECK_MANGLE_UNSUPPORTED;
            }
        }
    }
}

// Output owns the returned spelling (and the source's lazy nominal catalog), scratch only the worklists. A failure
// leaves spelling empty and frees its temporary work; the emission owner rolls back output together with its link.
static CheckMangleStatus check_mangle_name(CheckMangleSource* source, TypeId function, uint32_t identifier, Arena* scratch, String8* spelling)
{
    *spelling = (String8){ 0 };
    const uint64_t mark = scratch->length;
    CheckMangle writer = { .source = source, .scratch = scratch };
    const TypeTable* types = source->types;
    if (scratch == source->output || !type_is_valid(types, function) || type_kind(types, type_strip_aligned(types, function)) != BUSTER_TYPE_KIND_FUNCTION)
    {
        writer.status = CHECK_MANGLE_INVALID;
    }
    else if (type_function_abi_kind(types, function) != TYPE_FUNCTION_ABI_SYSV)
    {
        writer.status = CHECK_MANGLE_UNSUPPORTED;
    }
    else
    {
        const uint32_t length = intern_length(source->identifiers, identifier);
        writer.status = length == 0 ? CHECK_MANGLE_INVALID : CHECK_MANGLE_OK;
        check_mangle_text(&writer, "_Z");
        check_mangle_number(&writer, length, 10);
        check_mangle_bytes(&writer, intern_bytes(source->identifiers, identifier), length);
        check_mangle_parameters(&writer, type_strip_aligned(types, function), true);
    }
    while (writer.status == CHECK_MANGLE_OK && writer.task_count != 0)
    {
        const CheckMangleTask task = ((CheckMangleTask*)writer.tasks)[--writer.task_count];
        if (task.kind == CHECK_MANGLE_TYPE)
        {
            check_mangle_type(&writer, task.type);
        }
        else if (task.kind == CHECK_MANGLE_CHARACTER)
        {
            const char byte = (char)task.type;
            check_mangle_bytes(&writer, &byte, 1);
        }
        else
        {
            const bool room = writer.substitution_count < UINT32_MAX &&
                              type_grow_stack(scratch, &writer.substitutions, &writer.substitution_capacity,
                                              (uint64_t)writer.substitution_count + 1, sizeof(TypeId));
            writer.status = room ? CHECK_MANGLE_OK : CHECK_MANGLE_RESOURCE;
            if (room)
            {
                ((TypeId*)writer.substitutions)[writer.substitution_count++] = task.type;
                writer.aligned_substitution |= (types->types.flags[type_index(task.type)] & BUSTER_TYPE_FLAG_ALIGNED_INSIDE) != 0;
            }
        }
    }
    if (writer.status == CHECK_MANGLE_OK)
    {
        char* bytes = arena_push(source->output, writer.length, 1);
        writer.status = bytes != NULL ? CHECK_MANGLE_OK : CHECK_MANGLE_RESOURCE;
        if (bytes != NULL)
        {
            memcpy(bytes, writer.bytes, writer.length);
            *spelling = (String8){ .pointer = bytes, .length = writer.length };
        }
    }
    arena_pop_to(scratch, mark);
    return writer.status;
}

#endif
