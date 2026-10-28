// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
// Independent values, target bit patterns and protected byte tails for immutable const-object receipts.
#ifndef BUSTER_C_CHECK_CONST_VALUES_TEST_INCLUDED
#define BUSTER_C_CHECK_CONST_VALUES_TEST_INCLUDED

static bool check_const_test_flags(const CheckValue* value)
{
    const uint32_t required = BUSTER_CHECK_GNU_CONSTANT | BUSTER_CHECK_GNU_OBJECT_READ;
    // #705's source literal-zero bit must not survive a receipt read as null-pointer-constant syntax.
    const uint32_t forbidden = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER | UINT32_C(0x20000);
    const bool valid = value->category == CHECK_RVALUE && value->fault == CHECK_FAULT_NONE &&
                       (value->flags & required) == required && (value->flags & forbidden) == 0;
    return valid;
}

static bool check_const_test_append_value(CheckConstBuffer* buffer, const TypeTable* types, uint32_t object, uint32_t completed,
                                          TypeId root, CheckValue value)
{
    const CheckConstObject header = {
        .object = object,
        .declaration = completed - 1,
        .completed = completed,
        .type = root,
        .byte_size = type_size(types, root),
    };
    const CheckConstPart part = {
        .kind = CHECK_CONST_VALUE,
        .type = value.type,
        .bit_count = type_size(types, value.type) * 8,
        .value = value,
    };
    const bool valid = check_const_append(buffer, header, &part, 1, NULL, 0);
    return valid;
}

static bool check_const_test_buffer_unchanged(const CheckConstBuffer* before, const CheckConstBuffer* after)
{
    const CheckConstValues a = check_const_view(before);
    const CheckConstValues b = check_const_view(after);
    const bool same = before->arena == after->arena && before->capacity == after->capacity &&
                      before->part_capacity == after->part_capacity && before->union_capacity == after->union_capacity &&
                      before->hash_capacity == after->hash_capacity && a.objects == b.objects && a.count == b.count &&
                      a.parts == b.parts && a.part_count == b.part_count && a.unions == b.unions && a.union_count == b.union_count &&
                      a.hash == b.hash && a.hash_count == b.hash_count && a.hash_root == b.hash_root && a.prefix == b.prefix;
    return same;
}

static void check_const_test_scalars(CheckTest* test, TypeTable* types, CheckConstBuffer* buffer)
{
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    const CheckValue integer = {
        .type = BUSTER_TYPE_INT,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_ICE | BUSTER_CHECK_GNU_ICE_FORM | BUSTER_CHECK_FLOAT_LITERAL | BUSTER_CHECK_NULL_POINTER | UINT32_C(0x20000),
        .constant_kind = CHECK_CONSTANT_INTEGER,
        .integer = 37,
    };
    bool ready = check_const_test_append_value(buffer, types, 1, 10, ci, integer);
    check_test_expect(test, ready, "const receipt accepts a completed typed integer initializer");
    CheckConstValues view = check_const_view(buffer);
    CheckValue loaded = { 0 };
    const CheckConstValues* owner = NULL;
    check_test_expect(test, ready && check_const_find(&view, 1, 9, &owner) == NULL &&
                           !check_const_load(types, &view, 1, 0, 0, 0, ci, 9, &loaded),
                      "const receipt cannot expose an initializer before its completion point");
    check_test_expect(test, ready && check_const_find(&view, 1, 10, &owner) != NULL && owner == &view &&
                           check_const_load(types, &view, 1, 0, 0, 0, ci, 10, &loaded) &&
                           loaded.type == BUSTER_TYPE_INT && loaded.constant_kind == CHECK_CONSTANT_INTEGER && loaded.integer == 37 &&
                           check_const_test_flags(&loaded),
                      "const integer load is visible at completion and carries GNU object provenance without ICE provenance");
    check_test_expect(test, !check_const_load(types, &view, 1, -1, 0, 0, ci, 100, &loaded) &&
                           !check_const_load(types, &view, 1, 1, 0, 0, ci, 100, &loaded) &&
                           !check_const_load(types, &view, 1, 0, 8, 0, ci, 100, &loaded) &&
                           !check_const_load(types, &view, 1, INT64_MAX, 0, 0, ci, 100, &loaded) &&
                           !check_const_load(types, &view, 9999, 0, 0, 0, ci, 100, &loaded),
                      "const loads reject negative, oversized, invalid subbyte and unknown-object requests");
    const TypeId pointer = type_pointer(types, BUSTER_TYPE_INT);
    const TypeId cp = type_qualify(types, pointer, BUSTER_TYPE_CONST);
    const CheckValue address = {
        .type = pointer,
        .category = CHECK_RVALUE,
        .flags = BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED,
        .constant_kind = CHECK_CONSTANT_ADDRESS,
        .object = 900,
        .offset = 12,
    };
    ready = pointer != 0 && check_const_test_append_value(buffer, types, 2, 20, cp, address);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 2, 0, 0, 0, cp, 20, &loaded) &&
                           loaded.type == pointer && loaded.constant_kind == CHECK_CONSTANT_ADDRESS && loaded.object == 900 && loaded.offset == 12 &&
                           (loaded.flags & (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED)) ==
                           (BUSTER_CHECK_STATIC_OBJECT | BUSTER_CHECK_INACTIVE_UNION_ADDRESS | BUSTER_CHECK_CONST_READ_RESTRICTED) && check_const_test_flags(&loaded),
                      "typed pointer receipt preserves its symbolic target, addend and later-read provenance");
    const TypeId cd = type_qualify(types, BUSTER_TYPE_DOUBLE, BUSTER_TYPE_CONST);
    const CheckValue floating = {
        .type = BUSTER_TYPE_DOUBLE,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_FLOATING,
        .floating = -0.0L,
    };
    ready = check_const_test_append_value(buffer, types, 3, 30, cd, floating);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 3, 0, 0, 0, cd, 30, &loaded) &&
                           loaded.type == BUSTER_TYPE_DOUBLE && loaded.constant_kind == CHECK_CONSTANT_FLOATING &&
                           loaded.floating == 0.0L && signbit(loaded.floating) && check_const_test_flags(&loaded),
                      "typed floating receipt preserves negative zero without inspecting host padding");
    const TypeId complex = type_complex(types, BUSTER_TYPE_DOUBLE);
    const TypeId cc = type_qualify(types, complex, BUSTER_TYPE_CONST);
    const CheckValue pair = {
        .type = complex,
        .category = CHECK_RVALUE,
        .constant_kind = CHECK_CONSTANT_COMPLEX,
        .floating = 1.25L,
        .imaginary = -2.5L,
    };
    ready = complex != 0 && check_const_test_append_value(buffer, types, 4, 40, cc, pair);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 4, 0, 0, 0, cc, 40, &loaded) &&
                           loaded.type == complex && loaded.constant_kind == CHECK_CONSTANT_COMPLEX && loaded.floating == 1.25L &&
                           loaded.imaginary == -2.5L && check_const_test_flags(&loaded),
                      "typed complex receipt retains both components");
    const TypeId atomic = type_atomic(types, BUSTER_TYPE_INT);
    const TypeId ca = type_qualify(types, atomic, BUSTER_TYPE_CONST);
    ready = atomic != 0 && check_const_test_append_value(buffer, types, 5, 50, ca, integer);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 5, 0, 0, 0, ca, 50, &loaded) &&
                           loaded.type == BUSTER_TYPE_INT && loaded.integer == 37 && check_const_test_flags(&loaded),
                      "explicit constant evaluation can read a const atomic receipt as its normalized base type");
    const TypeId aligned = type_aligned(types, ci, 4);
    ready = aligned != 0 && check_const_test_append_value(buffer, types, 6, 60, aligned, integer);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 6, 0, 0, 0, aligned, 60, &loaded) && loaded.type == BUSTER_TYPE_INT,
                      "const load normalizes an aligned lvalue's result type");
    const TypeId cv = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST | BUSTER_TYPE_VOLATILE);
    ready = check_const_test_append_value(buffer, types, 7, 70, cv, integer);
    view = check_const_view(buffer);
    check_test_expect(test, ready && !check_const_load(types, &view, 7, 0, 0, 0, ci, 70, &loaded) &&
                           !check_const_load(types, &view, 1, 0, 0, 0, cv, 70, &loaded),
                      "volatile defining roots and volatile access types cannot acquire constant loads");
}

static void check_const_test_parts(CheckTest* test, TypeTable* types, CheckConstBuffer* buffer)
{
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    const TypeId cu = type_qualify(types, BUSTER_TYPE_UINT, BUSTER_TYPE_CONST);
    const CheckValue number = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 29 };
    const CheckConstObject header = { .object = 20, .declaration = 79, .completed = 80, .type = ci, .byte_size = 4 };
    const TypeId bits = type_tag(types, BUSTER_TYPE_KIND_STRUCT, 170006);
    const TypeMember fields[] = {
        { .type = BUSTER_TYPE_INT, .bit_width = 3 },
        { .name = 170007, .type = BUSTER_TYPE_INT, .bit_width = 5 },
        { .name = 170008, .type = BUSTER_TYPE_UINT, .bit_width = 5 },
    };
    bool ready = bits != 0 && type_complete_record(types, bits, fields, BUSTER_ARRAY_LENGTH(fields), (TypeRecordAttributes){ 0 });
    const uint32_t first = ready ? types->tags.member_begin[types->types.index[type_index(bits)]] : 0;
    const uint64_t signed_at = ready ? types->members.offset[first + 1] : 0;
    const uint64_t unsigned_at = ready ? types->members.offset[first + 2] : 0;
    CheckConstObject bit_header = header;
    bit_header.type = type_qualify(types, bits, BUSTER_TYPE_CONST);
    bit_header.byte_size = ready ? type_size(types, bits) : 0;
    const uint8_t field[] = { 0xe8, 0x1d };
    const CheckConstPart bit_part = {
        .kind = CHECK_CONST_BYTES,
        .type = bits,
        .bit_count = 16,
        .bytes = field,
        .byte_count = sizeof(field),
    };
    ready = ready && signed_at == 3 && unsigned_at == 8 && check_const_append(buffer, bit_header, &bit_part, 1, NULL, 0);
    CheckConstValues view = check_const_view(buffer);
    CheckValue loaded = { 0 };
    check_test_expect(test, ready && check_const_load(types, &view, 20, (int64_t)(signed_at / 8), (uint32_t)(signed_at % 8), 5, ci, 80, &loaded) &&
                           loaded.constant_kind == CHECK_CONSTANT_INTEGER && loaded.integer == UINT32_C(0xfffffffd) &&
                           loaded.bit_width == 5 && (loaded.flags & BUSTER_CHECK_BIT_FIELD) != 0 && check_const_test_flags(&loaded),
                      "signed five-bit const subobject is sign extended independently of its byte offset");
    check_test_expect(test, ready && check_const_load(types, &view, 20, (int64_t)(unsigned_at / 8), (uint32_t)(unsigned_at % 8), 5, cu, 80, &loaded) &&
                           loaded.integer == 29 && !check_const_load(types, &view, 20, 0, 3, 5, cu, 80, &loaded) &&
                           !check_const_load(types, &view, 20, 1, 0, 5, ci, 80, &loaded),
                      "declared unsigned field retains its bits without admitting a signedness reinterpretation");
    check_test_expect(test, ready && !check_const_load(types, &view, 20, 0, 0, 3, ci, 80, &loaded) &&
                           !check_const_load(types, &view, 20, 0, 3, 4, ci, 80, &loaded) &&
                           !check_const_load(types, &view, 20, 0, 3, 6, ci, 80, &loaded) &&
                           !check_const_load(types, &view, 20, 0, 3, 0, ci, 80, &loaded),
                      "unnamed fields and fabricated bit-field widths cannot acquire a typed read witness");
    CheckConstObject typed_bits_header = bit_header;
    typed_bits_header.object = 25;
    const CheckConstPart typed_bits = { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_INT, .bit_offset = 3, .bit_count = 5, .value = number };
    ready = check_const_append(buffer, typed_bits_header, &typed_bits, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 25, 0, 3, 5, ci, 80, &loaded) &&
                           loaded.integer == UINT32_C(0xfffffffd) && loaded.bit_width == 5 &&
                           (loaded.flags & BUSTER_CHECK_BIT_FIELD) != 0 && check_const_test_flags(&loaded),
                      "typed bit-field initialization truncates and sign extends its stored representation on a later read");
    check_test_expect(test, ready && !check_const_load(types, &view, 25, 0, 3, 5, cu, 80, &loaded),
                      "an exact typed signed bit-field receipt does not prove an unsigned reinterpretation");
    CheckConstObject zero_header = header;
    zero_header.object = 21;
    const CheckConstPart clear = { .kind = CHECK_CONST_CLEAR, .type = BUSTER_TYPE_INT, .bit_count = 32 };
    ready = check_const_append(buffer, zero_header, &clear, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 21, 0, 0, 0, ci, 80, &loaded) && loaded.integer == 0 && check_const_test_flags(&loaded),
                      "an explicit clear proves a zero scalar without a dense object image");
    check_test_expect(test, ready && !check_const_load(types, &view, 21, 0, 0, 0, BUSTER_TYPE_FLOAT, 80, &loaded) &&
                           !check_const_load(types, &view, 21, 0, 0, 0, cu, 80, &loaded) &&
                           !check_const_load(types, &view, 21, 0, 3, 5, ci, 80, &loaded),
                      "zero coverage cannot change a declared scalar's type or manufacture a bit-field");
    CheckConstPart overlapping[3] = {
        { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_INT, .bit_count = 32, .value = number },
        { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_UCHAR, .bit_offset = 8, .bit_count = 8,
          .value = { .type = BUSTER_TYPE_UCHAR, .category = CHECK_RVALUE } },
        clear,
    };
    CheckConstObject overlap_header = header;
    overlap_header.object = 22;
    ready = check_const_append(buffer, overlap_header, overlapping, 2, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && !check_const_load(types, &view, 22, 0, 0, 0, ci, 80, &loaded),
                      "a later unknown byte prevents an older complete scalar write from proving the whole value");
    overlap_header.object = 23;
    ready = check_const_append(buffer, overlap_header, overlapping, 3, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 23, 0, 0, 0, ci, 80, &loaded) && loaded.integer == 0,
                      "a later complete clear supersedes both known and unknown earlier writes");
    const TypeId array = type_array(types, ci, 2);
    const CheckConstObject array_header = { .object = 24, .declaration = 79, .completed = 80, .type = array, .byte_size = 8 };
    const CheckConstPart elements[2] = {
        { .kind = CHECK_CONST_CLEAR, .type = array, .bit_count = 64 },
        { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_INT, .bit_offset = 32, .bit_count = 32, .value = number },
    };
    ready = array != 0 && check_const_append(buffer, array_header, elements, 2, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 24, 4, 0, 0, ci, 80, &loaded) && loaded.integer == 29 &&
                           check_const_load(types, &view, 24, 0, 0, 0, ci, 80, &loaded) && loaded.integer == 0,
                      "array element const eligibility and sparse zero coverage survive a designated element write");
}

static void check_const_test_invalid(CheckTest* test, TypeTable* types, CheckConstBuffer* buffer)
{
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    const CheckValue integer = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 7 };
    const uint8_t short_bytes[] = { 1, 2, 3 };
    const CheckConstPart invalid[] = {
        { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_INT, .bit_offset = UINT64_MAX, .bit_count = 32, .value = integer },
        { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_INT, .bit_offset = 32, .bit_count = 1, .value = integer },
        { .kind = CHECK_CONST_BYTES, .type = BUSTER_TYPE_INT, .bit_count = 32, .bytes = short_bytes, .byte_count = sizeof(short_bytes) },
        { .kind = CHECK_CONST_VALUE, .type = BUSTER_TYPE_INT, .bit_count = 32, .value = integer },
    };
    const CheckConstBuffer saved = *buffer;
    const uint64_t mark = buffer->arena->length;
    for (uint32_t i = 0; i < BUSTER_ARRAY_LENGTH(invalid); i += 1)
    {
        const CheckConstObject header = { .object = i == 3 ? 0 : 500 + i, .declaration = 89, .completed = 90, .type = ci, .byte_size = 4 };
        const bool appended = check_const_append(buffer, header, invalid + i, 1, NULL, 0);
        check_test_expect(test, !appended && !buffer->resource && buffer->arena->length == mark &&
                               check_const_test_buffer_unchanged(&saved, buffer),
                          "invalid receipt offsets, byte coverage and zero object identity reject without storage or publication changes");
    }
}

static void check_const_test_records(CheckTest* test, TypeTable* types, CheckConstBuffer* buffer)
{
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    const TypeId record = type_tag(types, BUSTER_TYPE_KIND_STRUCT, 170001);
    const TypeMember member = { .name = 170002, .type = ci, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD };
    bool ready = record != 0 && type_complete_record(types, record, &member, 1, (TypeRecordAttributes){ 0 });
    const CheckConstPart part = {
        .kind = CHECK_CONST_VALUE,
        .type = BUSTER_TYPE_INT,
        .bit_count = 32,
        .value = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 41 },
    };
    CheckConstObject header = { .object = 30, .declaration = 89, .completed = 90, .type = record, .byte_size = type_size(types, record) };
    ready = ready && check_const_append(buffer, header, &part, 1, NULL, 0);
    CheckConstValues view = check_const_view(buffer);
    CheckValue loaded = { 0 };
    check_test_expect(test, ready && !check_const_load(types, &view, 30, 0, 0, 0, ci, 90, &loaded),
                      "a mutable defining struct cannot fold merely because the requested member is const");
    header.object = 31;
    header.type = type_qualify(types, record, BUSTER_TYPE_CONST);
    ready = ready && check_const_append(buffer, header, &part, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 31, 0, 0, 0, ci, 90, &loaded) && loaded.integer == 41,
                      "the same typed member value folds when its defining struct is const");
    const TypeId choice = type_tag(types, BUSTER_TYPE_KIND_UNION, 170003);
    const TypeMember members[] = {
        { .name = 0, .type = BUSTER_TYPE_INT, .bit_width = 3 },
        { .name = 170004, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 170005, .type = BUSTER_TYPE_DOUBLE, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
    };
    ready = choice != 0 && type_complete_record(types, choice, members, BUSTER_ARRAY_LENGTH(members), (TypeRecordAttributes){ 0 });
    CheckConstObject union_header = { .object = 32, .declaration = 89, .completed = 90, .type = type_qualify(types, choice, BUSTER_TYPE_CONST),
                                     .byte_size = type_size(types, choice) };
    const CheckConstPart clear = { .kind = CHECK_CONST_CLEAR, .type = choice, .bit_count = type_size(types, choice) * 8 };
    ready = ready && check_const_append(buffer, union_header, &clear, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_union_allows(types, &view, 32, choice, 0, 2, 90) &&
                           !check_const_union_allows(types, &view, 32, choice, 0, 1, 90) &&
                           !check_const_union_allows(types, &view, 32, choice, 0, 3, 90) &&
                           check_const_union_allows(types, &view, 32, choice, 0, 2, 89),
                      "default union selection skips unnamed bit fields and an unavailable receipt does not restrict a designator");
    check_test_expect(test, ready && check_const_load(types, &view, 32, 0, 0, 0, ci, 90, &loaded) && loaded.integer == 0 &&
                           !check_const_load(types, &view, 32, 0, 0, 0, BUSTER_TYPE_DOUBLE, 90, &loaded),
                      "default union zero coverage proves only the active declared scalar type");
    union_header.object = 33;
    const CheckConstUnion active = { .type = choice, .bit_offset = 0, .member = 3 };
    ready = ready && check_const_append(buffer, union_header, &clear, 1, &active, 1);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_union_allows(types, &view, 33, choice, 0, 3, 90) &&
                           !check_const_union_allows(types, &view, 33, choice, 0, 2, 90),
                      "explicit union activity overrides its default initializable member");
    check_test_expect(test, ready && check_const_load(types, &view, 33, 0, 0, 0, BUSTER_TYPE_DOUBLE, 90, &loaded) &&
                           loaded.constant_kind == CHECK_CONSTANT_FLOATING && loaded.floating == 0.0L &&
                           !check_const_load(types, &view, 33, 0, 0, 0, ci, 90, &loaded),
                      "explicit union selection restricts zero synthesis even when inactive members share its byte offset");
    union_header.object = 34;
    union_header.type = choice;
    ready = ready && check_const_append(buffer, union_header, &clear, 1, &active, 1);
    view = check_const_view(buffer);
    check_test_expect(test, ready && !check_const_load(types, &view, 34, 0, 0, 0, ci, 90, &loaded),
                      "a mutable union root cannot acquire const-member folding");
}

static void check_const_test_subobjects(CheckTest* test, TypeTable* types, CheckConstBuffer* buffer)
{
    const TypeId atomic = type_atomic(types, BUSTER_TYPE_INT);
    const TypeId record = type_tag(types, BUSTER_TYPE_KIND_STRUCT, 170009);
    const TypeMember members[] = {
        { .name = 170010, .type = BUSTER_TYPE_CHAR, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 170011, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 170012, .type = BUSTER_TYPE_INT | BUSTER_TYPE_VOLATILE, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 170013, .type = atomic, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 170014, .type = BUSTER_TYPE_FLOAT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
    };
    bool ready = atomic != 0 && record != 0 && type_complete_record(types, record, members, BUSTER_ARRAY_LENGTH(members), (TypeRecordAttributes){ 0 });
    const uint32_t first = ready ? types->tags.member_begin[types->types.index[type_index(record)]] : 0;
    const uint64_t integer_at = ready ? types->members.offset[first + 1] / 8 : 0;
    const uint64_t volatile_at = ready ? types->members.offset[first + 2] / 8 : 0;
    const uint64_t atomic_at = ready ? types->members.offset[first + 3] / 8 : 0;
    const uint64_t floating_at = ready ? types->members.offset[first + 4] / 8 : 0;
    CheckConstObject object = {
        .object = 40,
        .declaration = 109,
        .completed = 110,
        .type = type_qualify(types, record, BUSTER_TYPE_CONST),
        .byte_size = ready ? type_size(types, record) : 0,
    };
    const CheckConstPart clear = {
        .kind = CHECK_CONST_CLEAR,
        .type = record,
        .bit_count = object.byte_size * 8,
    };
    ready = ready && integer_at > 1 && check_const_append(buffer, object, &clear, 1, NULL, 0);
    CheckConstValues view = check_const_view(buffer);
    CheckValue loaded = { 0 };
    const uint64_t mark = buffer->arena->length;
    const CheckConstBuffer saved = *buffer;
    check_test_expect(test, ready && check_const_load(types, &view, 40, 0, 0, 0, BUSTER_TYPE_CHAR, 110, &loaded) && loaded.integer == 0 &&
                           check_const_load(types, &view, 40, (int64_t)integer_at, 0, 0, BUSTER_TYPE_INT, 110, &loaded) && loaded.integer == 0 &&
                           check_const_load(types, &view, 40, (int64_t)floating_at, 0, 0, BUSTER_TYPE_FLOAT, 110, &loaded) &&
                           loaded.constant_kind == CHECK_CONSTANT_FLOATING && loaded.floating == 0.0L &&
                           check_const_load(types, &view, 40, (int64_t)atomic_at, 0, 0, atomic, 110, &loaded) && loaded.integer == 0,
                      "root zero coverage proves exact declared char, int, float and atomic member values");
    check_test_expect(test, ready && !check_const_load(types, &view, 40, 1, 0, 0, BUSTER_TYPE_UCHAR, 110, &loaded) &&
                           !check_const_load(types, &view, 40, (int64_t)integer_at, 0, 0, BUSTER_TYPE_FLOAT, 110, &loaded) &&
                           !check_const_load(types, &view, 40, (int64_t)volatile_at, 0, 0, BUSTER_TYPE_INT, 110, &loaded) &&
                           buffer->arena->length == mark && check_const_test_buffer_unchanged(&saved, buffer),
                      "typed read witnesses reject padding, type punning and cast-stripped declared volatility without allocating or mutation");
    object.object = 41;
    const CheckConstPart value = {
        .kind = CHECK_CONST_VALUE,
        .type = BUSTER_TYPE_INT,
        .bit_offset = volatile_at * 8,
        .bit_count = 32,
        .value = { .type = BUSTER_TYPE_INT, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 17 },
    };
    ready = ready && check_const_append(buffer, object, &value, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && !check_const_load(types, &view, 41, (int64_t)volatile_at, 0, 0, BUSTER_TYPE_INT, 110, &loaded),
                      "an exact VALUE payload cannot bypass a volatile declaration path");

    const TypeId flex = type_incomplete_array(types, BUSTER_TYPE_INT);
    const TypeId flexible = type_tag(types, BUSTER_TYPE_KIND_STRUCT, 170015);
    const TypeMember flexible_members[] = {
        { .name = 170016, .type = BUSTER_TYPE_INT, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
        { .name = 170017, .type = flex, .bit_width = BUSTER_TYPE_NOT_BIT_FIELD },
    };
    ready = flex != 0 && flexible != 0 && type_complete_record(types, flexible, flexible_members, BUSTER_ARRAY_LENGTH(flexible_members), (TypeRecordAttributes){ 0 });
    const uint32_t flexible_first = ready ? types->tags.member_begin[types->types.index[type_index(flexible)]] : 0;
    const uint64_t tail = ready ? types->members.offset[flexible_first + 1] / 8 : 0;
    const CheckConstObject extended = {
        .object = 42,
        .declaration = 109,
        .completed = 110,
        .type = type_qualify(types, flexible, BUSTER_TYPE_CONST),
        .byte_size = tail + 8,
    };
    const CheckConstPart extended_clear = {
        .kind = CHECK_CONST_CLEAR,
        .type = flexible,
        .bit_count = extended.byte_size * 8,
    };
    ready = ready && check_const_append(buffer, extended, &extended_clear, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && check_const_load(types, &view, 42, 0, 0, 0, BUSTER_TYPE_INT, 110, &loaded) && loaded.integer == 0 &&
                           !check_const_load(types, &view, 42, (int64_t)tail, 0, 0, BUSTER_TYPE_INT, 110, &loaded) &&
                           !check_const_load(types, &view, 42, (int64_t)(tail + 4), 0, 0, BUSTER_TYPE_INT, 110, &loaded) &&
                           !check_const_load(types, &view, 42, (int64_t)(tail + 8), 0, 0, BUSTER_TYPE_INT, 110, &loaded),
                      "extended instance storage preserves fixed members without supplying a flexible-array read bound");
    CheckConstObject incomplete = extended;
    incomplete.object = 45;
    incomplete.type = type_qualify(types, flex, BUSTER_TYPE_CONST);
    const CheckConstPart incomplete_clear = {
        .kind = CHECK_CONST_CLEAR,
        .type = flex,
        .bit_count = incomplete.byte_size * 8,
    };
    ready = ready && check_const_append(buffer, incomplete, &incomplete_clear, 1, NULL, 0);
    view = check_const_view(buffer);
    check_test_expect(test, ready && !check_const_load(types, &view, 45, 0, 0, 0, BUSTER_TYPE_INT, 110, &loaded),
                      "an incomplete root array cannot derive a declared bound from retained storage");
}

static void check_const_test_bytes(CheckTest* test, TypeTable* types, CheckConstBuffer* buffer)
{
    const uint8_t literal[] = { 'a', 'b' };
    const TypeId chars = type_array(types, type_qualify(types, BUSTER_TYPE_CHAR, BUSTER_TYPE_CONST), BUSTER_ARRAY_LENGTH(literal));
    const CheckConstObject characters = { .object = 43, .declaration = 99, .completed = 100, .type = chars, .byte_size = sizeof(literal) };
    const CheckConstPart characters_part = { .kind = CHECK_CONST_BYTES, .type = chars, .bit_count = 16, .bytes = literal, .byte_count = sizeof(literal) };
    const bool retained = chars != 0 && check_const_append(buffer, characters, &characters_part, 1, NULL, 0);
    CheckConstValues literal_view = check_const_view(buffer);
    CheckValue character = { 0 };
    check_test_expect(test, retained && check_const_load(types, &literal_view, 43, 1, 0, 0, BUSTER_TYPE_CHAR, 100, &character) && character.integer == 'b' &&
                           !check_const_load(types, &literal_view, 43, 0, 0, 0, BUSTER_TYPE_USHORT, 100, &character) &&
                           !check_const_load(types, &literal_view, 43, 0, 0, 0, BUSTER_TYPE_UCHAR, 100, &character) &&
                           !check_const_load(types, &literal_view, 43, 0, 1, 5, BUSTER_TYPE_CHAR, 100, &character),
                      "string bytes prove declared char elements without integer, signedness or fabricated field aliases");
    const uint8_t wide_bytes[] = { 0x34, 0x12, 0x78, 0x56 };
    const TypeId wide = type_array(types, type_qualify(types, BUSTER_TYPE_USHORT, BUSTER_TYPE_CONST), 2);
    const CheckConstObject wide_object = { .object = 44, .declaration = 99, .completed = 100, .type = wide, .byte_size = sizeof(wide_bytes) };
    const CheckConstPart wide_part = { .kind = CHECK_CONST_BYTES, .type = wide, .bit_count = 32, .bytes = wide_bytes, .byte_count = sizeof(wide_bytes) };
    const bool wide_retained = wide != 0 && check_const_append(buffer, wide_object, &wide_part, 1, NULL, 0);
    literal_view = check_const_view(buffer);
    check_test_expect(test, wide_retained && check_const_load(types, &literal_view, 44, 2, 0, 0, BUSTER_TYPE_USHORT, 100, &character) &&
                           character.integer == 0x5678 && !check_const_load(types, &literal_view, 44, 1, 0, 0, BUSTER_TYPE_USHORT, 100, &character) &&
                           !check_const_load(types, &literal_view, 44, 0, 0, 0, BUSTER_TYPE_UCHAR, 100, &character),
                      "wide byte payloads retain exact declared element values without crossing or narrowing their subobjects");
    const uint64_t page = (uint64_t)sysconf(_SC_PAGESIZE);
    uint8_t* mapping = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    const bool guarded = mapping != MAP_FAILED && mprotect(mapping + page, page, PROT_NONE) == 0;
    check_test_expect(test, guarded, "const byte receipts have a protected source tail");
    const uint32_t lengths[] = { 1, 2, 3, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129 };
    for (uint32_t l = 0; guarded && l < BUSTER_ARRAY_LENGTH(lengths); l += 1)
    {
        const uint32_t count = lengths[l];
        uint8_t* bytes = mapping + page - count;
        for (uint32_t i = 0; i < count; i += 1)
        {
            bytes[i] = (uint8_t)(i * 29 + count);
        }
        const TypeId array = type_array(types, type_qualify(types, BUSTER_TYPE_UCHAR, BUSTER_TYPE_CONST), count);
        const CheckConstObject header = { .object = 100 + l, .declaration = 99, .completed = 100, .type = array, .byte_size = count };
        const CheckConstPart part = { .kind = CHECK_CONST_BYTES, .type = array, .bit_count = (uint64_t)count * 8, .bytes = bytes, .byte_count = count };
        const bool appended = array != 0 && check_const_append(buffer, header, &part, 1, NULL, 0);
        memset(bytes, 0, count);
        const CheckConstValues view = check_const_view(buffer);
        bool equal = appended;
        for (uint32_t i = 0; equal && i < count; i += 1)
        {
            CheckValue value = { 0 };
            equal = check_const_load(types, &view, header.object, i, 0, 0, BUSTER_TYPE_UCHAR, 100, &value) &&
                    value.constant_kind == CHECK_CONSTANT_INTEGER && value.integer == (uint8_t)(i * 29 + count) && check_const_test_flags(&value);
        }
        CheckValue beyond = { 0 };
        check_test_expect(test, equal && !check_const_load(types, &view, header.object, count, 0, 0, BUSTER_TYPE_UCHAR, 100, &beyond),
                          "const byte receipt copies exactly the guarded source slice and retains every byte independently of its caller");
    }
    if (mapping != MAP_FAILED)
    {
        munmap(mapping, 2 * page);
    }
}

static void check_const_test_history(CheckTest* test, TypeTable* types)
{
    Arena arena = arena_reserve(1u << 24);
    Arena suffix_arena = arena_reserve(1u << 20);
    CheckConstBuffer buffer = { .arena = &arena };
    CheckConstValues snapshots[4] = { 0 };
    const uint32_t stops[] = { 1, 8, 32, 129 };
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    bool grown = arena.base != NULL && suffix_arena.base != NULL;
    for (uint32_t i = 0; grown && i < 257; i += 1)
    {
        const CheckValue value = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = i + 7 };
        grown = check_const_test_append_value(&buffer, types, i + 1, i * 4 + 3, ci, value);
        for (uint32_t s = 0; s < BUSTER_ARRAY_LENGTH(stops); s += 1)
        {
            if (i + 1 == stops[s])
            {
                snapshots[s] = check_const_view(&buffer);
            }
        }
    }
    const CheckConstValues current = check_const_view(&buffer);
    for (uint32_t s = 0; grown && s < BUSTER_ARRAY_LENGTH(stops); s += 1)
    {
        for (uint32_t i = 0; grown && i < 257; i += 1)
        {
            const CheckConstValues* owner = NULL;
            const CheckConstObject* found = check_const_find(snapshots + s, i + 1, UINT32_MAX, &owner);
            CheckValue value = { 0 };
            grown = (found != NULL) == (i < stops[s]) &&
                    check_const_load(types, &current, i + 1, 0, 0, 0, ci, UINT32_MAX, &value) && value.integer == i + 7;
        }
    }
    check_test_expect(test, grown, "receipt growth preserves every old immutable hash view and every current typed payload");
    CheckConstBuffer suffix = { .arena = &suffix_arena, .prefix = &snapshots[1] };
    const CheckValue tail = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 777 };
    bool appended = grown && check_const_test_append_value(&suffix, types, 700, 1200, ci, tail);
    const CheckConstValues view = check_const_view(&suffix);
    const CheckConstValues* owner = NULL;
    CheckValue value = { 0 };
    check_test_expect(test, appended && check_const_find(&view, 1, UINT32_MAX, &owner) != NULL && owner == &snapshots[1] &&
                           check_const_find(&view, 700, UINT32_MAX, &owner) != NULL && owner == &view &&
                           check_const_find(&view, 9, UINT32_MAX, &owner) == NULL &&
                           check_const_load(types, &view, 700, 0, 0, 0, ci, UINT32_MAX, &value) && value.integer == 777,
                      "a lane-local suffix sees its immutable prefix cutoff and identifies each receipt's owning view");
    const uint64_t mark = suffix_arena.length;
    const CheckConstBuffer saved = suffix;
    check_test_expect(test, appended && !check_const_test_append_value(&suffix, types, 700, 1201, ci, tail) &&
                           !check_const_test_append_value(&suffix, types, 1, 1201, ci, tail) && !suffix.resource && suffix_arena.length == mark &&
                           check_const_test_buffer_unchanged(&saved, &suffix),
                      "duplicate object identities in the local view or prefix reject without allocating or claiming exhaustion");
    CheckConstBuffer nested = { .arena = &suffix_arena, .prefix = &view };
    check_test_expect(test, !check_const_test_append_value(&nested, types, 701, 1201, ci, tail) && !nested.resource && suffix_arena.length == mark,
                      "a prefix which already has a prefix is rejected without mutation");
    CheckConstValues cycle = { 0 };
    cycle.prefix = &cycle;
    CheckConstBuffer cyclic = { .arena = &suffix_arena, .prefix = &cycle };
    check_test_expect(test, !check_const_test_append_value(&cyclic, types, 702, 1201, ci, tail) && !cyclic.resource && suffix_arena.length == mark,
                      "a self-referential prefix is rejected without following a cycle");
    arena_release(&suffix_arena);
    arena_release(&arena);
}

static void check_const_test_exhaustion(CheckTest* test, TypeTable* types)
{
    Arena arena = arena_reserve(1u << 20);
    CheckConstBuffer buffer = { .arena = &arena };
    const TypeId ci = type_qualify(types, BUSTER_TYPE_INT, BUSTER_TYPE_CONST);
    const CheckValue integer = { .type = BUSTER_TYPE_INT, .category = CHECK_RVALUE, .constant_kind = CHECK_CONSTANT_INTEGER, .integer = 19 };
    const bool ready = arena.base != NULL && check_const_test_append_value(&buffer, types, 1, 10, ci, integer);
    const CheckConstValues before = check_const_view(&buffer);
    const CheckConstBuffer saved = buffer;
    const uint64_t mark = arena.length;
    const uint64_t capacity = arena.capacity;
    const uint8_t bytes[] = { 7, 11, 13, 17 };
    const CheckConstPart part = { .kind = CHECK_CONST_BYTES, .type = BUSTER_TYPE_INT, .bit_count = 32, .bytes = bytes, .byte_count = sizeof(bytes) };
    const CheckConstObject object = { .object = 2, .declaration = 19, .completed = 20, .type = ci, .byte_size = sizeof(bytes) };
    arena.capacity = mark;
    const bool appended = ready && check_const_append(&buffer, object, &part, 1, NULL, 0);
    arena.capacity = capacity;
    const CheckConstValues after = check_const_view(&buffer);
    const CheckConstValues* owner = NULL;
    CheckValue value = { 0 };
    check_test_expect(test, ready && !appended && buffer.resource && arena.length == mark && check_const_test_buffer_unchanged(&saved, &buffer) &&
                           check_const_find(&after, 2, UINT32_MAX, &owner) == NULL &&
                           check_const_load(types, &after, 1, 0, 0, 0, ci, UINT32_MAX, &value) && value.integer == 19 &&
                           check_const_load(types, &before, 1, 0, 0, 0, ci, UINT32_MAX, &value) && value.integer == 19,
                      "exhausted byte retention rolls back allocation and publication while preserving old and current views");
    buffer.resource = false;
    check_test_expect(test, ready && check_const_append(&buffer, object, &part, 1, NULL, 0),
                      "a rolled-back receipt can be appended after restoring storage");
    arena_release(&arena);
}

static void check_test_const_values(CheckTest* test)
{
    check_test_expect(test, sizeof(CheckValue) == 96, "subbyte object identity occupies existing CheckValue padding");
    CheckTestTypes types = { 0 };
    Arena arena = arena_reserve(1u << 22);
    const bool ready = check_test_types_open(&types) && arena.base != NULL;
    check_test_expect(test, ready, "const-object receipt test storage");
    if (ready)
    {
        CheckConstBuffer buffer = { .arena = &arena };
        check_const_test_scalars(test, &types.table, &buffer);
        check_const_test_parts(test, &types.table, &buffer);
        check_const_test_invalid(test, &types.table, &buffer);
        check_const_test_records(test, &types.table, &buffer);
        check_const_test_subobjects(test, &types.table, &buffer);
        check_const_test_bytes(test, &types.table, &buffer);
        check_const_test_history(test, &types.table);
        check_const_test_exhaustion(test, &types.table);
    }
    arena_release(&arena);
    check_test_types_close(&types);
}

#endif
