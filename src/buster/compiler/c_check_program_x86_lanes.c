// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// The x86 builtins a target_clones copy reaches without their instructions, computed lane by lane (#2716). Clang checks
// a target_clones body against its first version's features (#2211), so the body's default copy can call an AVX-512
// macro's builtin, which LLVM legalizes with the copy's own instructions. Where the target hasn't AVX-512F and
// AVX-512VL, these builtins are the typed stream's own operations, element by element, as check_lower_vector_lanewise
// computes C's operators no row takes: the arguments set into frame temporaries (a vector's of its bytes, an integer's
// of its class), each element of the result computed from theirs and set at its offset, or each bit of a mask result
// ORed into its integer, then the result read whole. Each element is what clang's legalized code gives, whatever
// instructions that takes. The kinds (CheckX86LanesKind):
//   - COMPARE: an integer compare into a mask (VPCMP's and VPCMPU's), its predicate the immediate's low 3 bits, each
//     lane's bit the compare of its elements as signed or unsigned integers; or a float one (VCMPPS's and VCMPPD's), its
//     predicate clang's fcmp of the immediate's low 4 bits (check_x86_lower_float_predicate), which the typed compares'
//     C semantics combine into: an ordered compare is false of a NaN, != is true of one, and a NaN is the value unequal
//     to itself. The bits are ANDed with the mask argument. A function with FENV_ACCESS keeps its float compares'
//     refusal: their predicates' signaling bit is an exception the typed compares don't raise.
//   - ROTATE: VPROLD's to VPRORQ's rotate of each element by the immediate, modulo its bits.
//   - FUNNEL: VPSHLD's (a's bits shifted left with b's top bits in) and VPSHRD's (a's shifted right with b's low bits
//     in), by the immediate modulo the element's bits.
//   - TERNARY: VPTERNLOG's function of a, b and c's bits (the immediate's bit (a << 2 | b << 1 | c)), each element a's
//     (_mask) or zero (_maskz) where the mask's bit is clear. The 256-bit ones need AVX, and the 512-bit ones aren't
//     taken: clang's backend can't split them ("Do not know how to split the result of this operator") without it, and
//     at every level.
//   - MOVE: the element moves of an immediate (CheckX86LanesMove): VALIGND's and VALIGNQ's, VPALIGNR's and the byte
//     shifts of 64 bytes, the 128-bit lanes' shuffles, SHUFPS's, SHUFPD's, VPERMILPS's, VPERMILPD's, PSHUFD's, PSHUFHW's
//     and PSHUFLW's of 64 bytes, VPERMQ's and VPERMPD's of 64, and the inserts of 128- and 256-bit lanes: each piece of
//     the result an argument's piece, or zeros.
//   - EXTRACT: VEXTRACTF32X4's to VEXTRACTI64X4's lane of a, each element w's where the mask's bit is clear.
//   - GATHER: AVX2's gathers of 16 bytes by a vector mask (VPGATHERDD to VGATHERQPD) without AVX2: each element the one
//     at p + sext(index) * scale where the mask's element has its top bit, w's where it hasn't, and zero past the
//     index's elements (a quadword index's gather of dwords fills half). The load reads the element's own address where
//     its mask is clear, w's element in its temporary, so a masked-off element reads no memory of the program's and
//     takes no branch: the address is w's plus (the gathered one's less w's) ANDed with the mask element's sign. The
//     32-byte ones aren't taken: clang's backend can't split them without AVX2.
//   - ROUND: the round-scales of a constant mask (rndscaless and rndscalesd with SSE4.1, rndscaleps of 32 bytes with
//     AVX), as clang's legalized code computes them: ROUNDSS's, ROUNDSD's or VROUNDPS's of the whole immediate, whose
//     scale bits (7 to 4) those instructions ignore, then each element w's where the mask's bit is clear (and the scalar
//     ones' upper elements a's). Clang's backend selects no blend of a mask that isn't a constant, nor these
//     instructions below SSE4.1 or a 32-byte one without AVX, nor an exceptions' suppression: those stay refused.
//
// Included by c_check_program_lower.c after check_lower_x86_split.

// A lane's code: the typed stream's entries in the order they run (an instruction, or an element's address in a frame
// object, which check_lower_element_address pushes), which check_x86_lanes_push pushes the last first.
#define BUSTER_CHECK_X86_LANE_ADDRESS 0xffu
#define BUSTER_CHECK_X86_LANE_ENTRIES 64u

typedef struct CheckX86LaneEntry CheckX86LaneEntry;
struct CheckX86LaneEntry
{
    uint8_t kind;
    uint8_t class;
    uint32_t object;
    uint64_t value;
};

typedef struct CheckX86LaneCode CheckX86LaneCode;
struct CheckX86LaneCode
{
    uint32_t count;
    bool overflow;
    CheckX86LaneEntry entries[BUSTER_CHECK_X86_LANE_ENTRIES];
};

typedef enum CheckX86LanesKind
{
    CHECK_X86_LANES_NONE,
    CHECK_X86_LANES_COMPARE,
    CHECK_X86_LANES_ROTATE,
    CHECK_X86_LANES_FUNNEL,
    CHECK_X86_LANES_TERNARY,
    CHECK_X86_LANES_MOVE,
    CHECK_X86_LANES_EXTRACT,
    CHECK_X86_LANES_GATHER,
    CHECK_X86_LANES_ROUND,
} CheckX86LanesKind;

// MOVE's moves, by builtin.
typedef enum CheckX86LanesMove
{
    CHECK_X86_MOVE_ALIGN,
    CHECK_X86_MOVE_PALIGNR,
    CHECK_X86_MOVE_BYTES_LEFT,
    CHECK_X86_MOVE_BYTES_RIGHT,
    CHECK_X86_MOVE_LANES,
    CHECK_X86_MOVE_SHUFPS,
    CHECK_X86_MOVE_SHUFPD,
    CHECK_X86_MOVE_PERMILPS,
    CHECK_X86_MOVE_PERMILPD,
    CHECK_X86_MOVE_PSHUFHW,
    CHECK_X86_MOVE_PSHUFLW,
    CHECK_X86_MOVE_PERMQ,
    CHECK_X86_MOVE_INSERT,
} CheckX86LanesMove;

// A builtin's kind, its variant (ROTATE's and FUNNEL's direction, 1 for right; MOVE's CheckX86LanesMove) and the
// arguments it takes but the immediate: the classes of vectors (V) and of an integer mask (K) in order.
typedef struct CheckX86LanesShape CheckX86LanesShape;
struct CheckX86LanesShape
{
    CheckX86LanesKind kind;
    uint32_t variant;
    uint32_t vectors;
    bool mask;
};

typedef struct CheckX86LanesName CheckX86LanesName;
struct CheckX86LanesName
{
    const char* prefix;
    uint32_t length;
    CheckX86LanesKind kind;
    uint32_t variant;
};

// The builtins of each kind but COMPARE (a family of its own), by their names' prefixes past "__builtin_ia32_": each
// family's widths share one prefix (prold128, prold256 and prold512).
static const CheckX86LanesName check_x86_lanes_names[] = {
    { "prol", 4, CHECK_X86_LANES_ROTATE, 0 },
    { "pror", 4, CHECK_X86_LANES_ROTATE, 1 },
    { "vpshld", 6, CHECK_X86_LANES_FUNNEL, 0 },
    { "vpshrd", 6, CHECK_X86_LANES_FUNNEL, 1 },
    { "pternlog", 8, CHECK_X86_LANES_TERNARY, 0 },
    { "alignd", 6, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_ALIGN },
    { "alignq", 6, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_ALIGN },
    { "palignr512", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PALIGNR },
    { "pslldqi512_byteshift", 20, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_BYTES_LEFT },
    { "psrldqi512_byteshift", 20, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_BYTES_RIGHT },
    { "shuf_f32x4", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_LANES },
    { "shuf_f64x2", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_LANES },
    { "shuf_i32x4", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_LANES },
    { "shuf_i64x2", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_LANES },
    { "shufps512", 9, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_SHUFPS },
    { "shufpd512", 9, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_SHUFPD },
    { "vpermilps512", 12, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PERMILPS },
    { "pshufd512", 9, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PERMILPS },
    { "vpermilpd512", 12, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PERMILPD },
    { "pshufhw512", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PSHUFHW },
    { "pshuflw512", 10, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PSHUFLW },
    { "permdi512", 9, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PERMQ },
    { "permdf512", 9, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_PERMQ },
    { "insertf32x4", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "insertf32x8", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "insertf64x2", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "insertf64x4", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "inserti32x4", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "inserti32x8", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "inserti64x2", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "inserti64x4", 11, CHECK_X86_LANES_MOVE, CHECK_X86_MOVE_INSERT },
    { "extractf32x4", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extractf32x8", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extractf64x2", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extractf64x4", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extracti32x4", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extracti32x8", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extracti64x2", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "extracti64x4", 12, CHECK_X86_LANES_EXTRACT, 0 },
    { "gatherd_", 8, CHECK_X86_LANES_GATHER, 4 },
    { "gatherq_", 8, CHECK_X86_LANES_GATHER, 8 },
    { "rndscaless_round_mask", 21, CHECK_X86_LANES_ROUND, 0 },
    { "rndscalesd_round_mask", 21, CHECK_X86_LANES_ROUND, 0 },
    { "rndscaleps_256_mask", 19, CHECK_X86_LANES_ROUND, 1 },
};

// Whether a builtin's name is one check_lower_x86_lanes may take (check_x86_lanes_names', or a compare into a mask:
// "cmp" or "ucmp" then "_mask"), without its rule: check_x86_lanes_room's filter.
static bool check_x86_lanes_named(const char* name, uint32_t length)
{
    const uint32_t prefix = (uint32_t)strlen("__builtin_ia32_");
    const bool ia32 = length > prefix && memcmp(name, "__builtin_ia32_", prefix) == 0;
    const char* rest = name + prefix;
    const uint32_t remaining = ia32 ? length - prefix : 0;
    bool named = remaining > 8 && (memcmp(rest, "cmp", 3) == 0 || memcmp(rest, "ucmp", 4) == 0) && memcmp(name + length - 5, "_mask", 5) == 0;
    for (uint32_t n = 0; !named && ia32 && n < BUSTER_ARRAY_LENGTH(check_x86_lanes_names); n += 1)
    {
        const CheckX86LanesName* entry = &check_x86_lanes_names[n];
        named = remaining >= entry->length && rest[0] == entry->prefix[0] && memcmp(rest, entry->prefix, entry->length) == 0;
    }
    return named;
}

// A rule's shape at a target, or kind NONE where check_lower_x86_lanes doesn't take it (another family, an operand
// shape the kind hasn't, a float compare in a function with FENV_ACCESS, or a builtin clang's backend fails on there).
static CheckX86LanesShape check_x86_lanes_shape(const X86Target* target, const CheckX86LowerRule* rule, bool strict)
{
    CheckX86LanesShape shape = { .kind = CHECK_X86_LANES_NONE };
    const uint32_t prefix = (uint32_t)strlen("__builtin_ia32_");
    const bool vector = (rule->bytes == 16 || rule->bytes == 32 || rule->bytes == 64) &&
                        (rule->element == 1 || rule->element == 2 || rule->element == 4 || rule->element == 8);
    if (rule->family == CHECK_X86_LOWER_COMPARE)
    {
        const bool taken = rule->immediate == 2 && rule->rounding == CHECK_X86_ROUND_NONE &&
                           (rule->form == X86_FORM_NONE || (rule->fixed == CHECK_X86_PREDICATE_FCMP && !strict));
        shape = (CheckX86LanesShape){ .kind = taken ? CHECK_X86_LANES_COMPARE : CHECK_X86_LANES_NONE, .vectors = 2, .mask = true };
    }
    for (uint32_t n = 0; shape.kind == CHECK_X86_LANES_NONE && rule->name_length > prefix && n < BUSTER_ARRAY_LENGTH(check_x86_lanes_names); n += 1)
    {
        const CheckX86LanesName* name = &check_x86_lanes_names[n];
        if (rule->name_length >= prefix + name->length && memcmp(rule->name + prefix, name->prefix, name->length) == 0)
        {
            shape.kind = name->kind;
            shape.variant = name->variant;
        }
    }
    switch (shape.kind)
    {
        case CHECK_X86_LANES_ROTATE: shape.vectors = 1; break;
        case CHECK_X86_LANES_FUNNEL: shape.vectors = 2; break;
        case CHECK_X86_LANES_TERNARY:
        {
            // pternlog*_mask and _maskz: a, b, c and the mask; the 256-bit ones with AVX, as clang's backend splits them.
            const bool legal = rule->bytes == 16 || (rule->bytes == 32 && (target->features & BUSTER_X86_FEATURE(AVX)) != 0);
            shape = (CheckX86LanesShape){ .kind = legal ? CHECK_X86_LANES_TERNARY : CHECK_X86_LANES_NONE, .vectors = 3, .mask = true };
            break;
        }
        case CHECK_X86_LANES_MOVE: shape.vectors = rule->family == CHECK_X86_LOWER_UNARY_IMMEDIATE ? 1 : 2; break;
        case CHECK_X86_LANES_EXTRACT: shape.vectors = 2; shape.mask = true; break;
        case CHECK_X86_LANES_GATHER:
        {
            // w, p, the index and the mask, of 16 bytes: the 32-byte ones need AVX2, which has them.
            const bool legal = rule->bytes == 16 && rule->family == CHECK_X86_LOWER_GATHER_VECTOR;
            shape = (CheckX86LanesShape){ .kind = legal ? CHECK_X86_LANES_GATHER : CHECK_X86_LANES_NONE, .variant = shape.variant, .vectors = 3 };
            break;
        }
        case CHECK_X86_LANES_ROUND:
        {
            // The scalar ones' a, b, w and the mask, with SSE4.1; the packed one's a, w and the mask, with AVX.
            const bool packed = shape.variant != 0;
            const uint64_t feature = packed ? BUSTER_X86_FEATURE(AVX) : BUSTER_X86_FEATURE(SSE41);
            const bool legal = (target->features & feature) != 0;
            shape = (CheckX86LanesShape){ .kind = legal ? CHECK_X86_LANES_ROUND : CHECK_X86_LANES_NONE, .variant = shape.variant, .vectors = packed ? 2 : 3, .mask = true };
            break;
        }
        default: break;
    }
    const bool rounded = shape.kind == CHECK_X86_LANES_ROUND || rule->rounding == CHECK_X86_ROUND_NONE;
    const bool immediate = shape.kind == CHECK_X86_LANES_COMPARE || (rule->immediate != BUSTER_CHECK_X86_NO_IMMEDIATE && rounded);
    if (!vector || !immediate)
    {
        shape.kind = CHECK_X86_LANES_NONE;
    }
    return shape;
}

// The entries a call of `rule` computed lane by lane may take (check_x86_lanes_room's bound): the elements its code
// computes (a MOVE's granules, an EXTRACT's lane's elements), each at most a kind's entries, besides 32 for its
// arguments' sets and its result's read. A lane's entries count an element's address as its 3 (LOCAL_ADDRESS, CONST and
// PTR_ADD): an integer compare's 17 (the mask read, two elements' addresses, loads and conversions, the compare, its
// conversion, shift and OR, and the mask's set), a float compare's EQ_UQ 35 (two more operands' pairs), a move's 11, a
// rotate's or funnel shift's 21, an extract's 33, a gather's 29, a round-scale's 9, and a ternary logic's element 85
// (its three loads into locals, four minterms of nine entries, and the mask's merge).
static uint32_t check_x86_lanes_granule(const CheckX86LowerRule* rule, uint32_t move);

static uint64_t check_x86_lanes_entries(const CheckX86LowerRule* rule, CheckX86LanesShape shape)
{
    uint32_t element = rule->element;
    uint32_t bytes = rule->bytes;
    uint32_t each = 0;
    switch (shape.kind)
    {
        case CHECK_X86_LANES_COMPARE: each = rule->form != X86_FORM_NONE ? 40 : 20; break;
        case CHECK_X86_LANES_MOVE: each = 14; element = check_x86_lanes_granule(rule, shape.variant); break;
        case CHECK_X86_LANES_ROTATE:
        case CHECK_X86_LANES_FUNNEL: each = 24; break;
        case CHECK_X86_LANES_TERNARY: each = 96; break;
        case CHECK_X86_LANES_EXTRACT: each = 36; bytes = rule->other; break;
        case CHECK_X86_LANES_GATHER: each = 36; break;
        case CHECK_X86_LANES_ROUND: each = 12; break;
        default: break;
    }
    return element != 0 ? (uint64_t)(bytes / element) * each + 32 : 0;
}

// The entries a program's calls computed lane by lane may take past the per-node bounds (check_program_lower's room):
// check_x86_lanes_entries' of each x86 builtin call check_x86_lanes_shape takes at the program's target, which has none
// with AVX-512F and AVX-512VL. The calls' rules are looked up by name only where the target hasn't those.
static uint64_t check_x86_lanes_room(const CheckProgram* program)
{
    const uint64_t evex = BUSTER_X86_FEATURE(AVX512F) | BUSTER_X86_FEATURE(AVX512VL);
    uint64_t room = 0;
    const uint32_t count = program->status == CHECK_PROGRAM_OK && program->stream != 0 && (program->target.features & evex) != evex ? program->stream->x86_count : 0;
    for (uint32_t i = 0; i < count; i += 1)
    {
        const BuiltinIntrinsicInfo* info = program->stream->x86[i].info;
        const CheckX86LowerRule* rule = info != NULL && check_x86_lanes_named(info->name, info->name_length) ? check_x86_lower_rule(info->name, info->name_length) : NULL;
        room += rule != NULL ? check_x86_lanes_entries(rule, check_x86_lanes_shape(&program->target, rule, false)) : 0;
    }
    return room;
}

static void check_x86_lane_entry(CheckX86LaneCode* code, uint8_t kind, uint8_t class, uint32_t object, uint64_t value)
{
    if (code->count < BUSTER_CHECK_X86_LANE_ENTRIES)
    {
        code->entries[code->count] = (CheckX86LaneEntry){ .kind = kind, .class = class, .object = object, .value = value };
        code->count += 1;
    }
    else
    {
        code->overflow = true;
    }
}

static void check_x86_lane_op(CheckX86LaneCode* code, uint8_t kind, uint8_t class, uint64_t value)
{
    check_x86_lane_entry(code, kind, class, 0, value);
}

static void check_x86_lane_address(CheckX86LaneCode* code, uint32_t object, uint32_t offset)
{
    check_x86_lane_entry(code, BUSTER_CHECK_X86_LANE_ADDRESS, 0, object, offset);
}

// An element of `class` read at `offset` bytes into frame object `object`, converted to `computed` where that differs.
static void check_x86_lane_load(CheckX86LaneCode* code, uint32_t object, uint32_t offset, uint8_t class, uint8_t computed)
{
    check_x86_lane_address(code, object, offset);
    check_x86_lane_op(code, BUSTER_TYPED_LOAD, class, 0);
    if (computed != class)
    {
        check_x86_lane_op(code, BUSTER_TYPED_CONVERT, computed, 0);
    }
}

// Pushes a lane's code, the last entry first (the tasks run in the reverse of their pushes), and empties it.
static void check_x86_lanes_push(CheckLowerWriter* writer, CheckX86LaneCode* code)
{
    if (code->overflow)
    {
        check_lower_fail(writer, CHECK_PROGRAM_INVALID);
    }
    for (uint32_t e = code->count; e-- > 0 && !code->overflow;)
    {
        const CheckX86LaneEntry* entry = &code->entries[e];
        if (entry->kind == BUSTER_CHECK_X86_LANE_ADDRESS)
        {
            check_lower_element_address(writer, entry->object, (uint32_t)entry->value);
        }
        else
        {
            check_lower_instruction(writer, entry->kind, entry->class, entry->value);
        }
    }
    code->count = 0;
}

// The classes of an integer element of `bytes` bytes: as it is stored, and as it is computed (int or unsigned int for
// one of 4 bytes or fewer, as LLVM's narrower compares order them, long long or unsigned long long for 8).
static uint8_t check_x86_lane_stored_class(uint32_t bytes, bool is_signed)
{
    const uint8_t classes[4][2] = {
        { BUSTER_TYPED_CLASS_U8, BUSTER_TYPED_CLASS_I8 },
        { BUSTER_TYPED_CLASS_U16, BUSTER_TYPED_CLASS_I16 },
        { BUSTER_TYPED_CLASS_U32, BUSTER_TYPED_CLASS_I32 },
        { BUSTER_TYPED_CLASS_U64, BUSTER_TYPED_CLASS_I64 },
    };
    const uint32_t size = bytes == 1 ? 0 : bytes == 2 ? 1 : bytes == 4 ? 2 : 3;
    return classes[size][is_signed ? 1 : 0];
}

static uint8_t check_x86_lane_computed_class(uint32_t bytes, bool is_signed)
{
    return bytes == 8 ? (is_signed ? BUSTER_TYPED_CLASS_I64 : BUSTER_TYPED_CLASS_U64) : (is_signed ? BUSTER_TYPED_CLASS_I32 : BUSTER_TYPED_CLASS_U32);
}

// A float compare's value of lane elements `a` and `b` (read at `offset` of their temporaries), an int 0 or 1, by
// check_x86_lower_float_predicate's predicate: those of 0 to 7 (EQ, LT, LE, UNORD, NEQ, NLT, NLE, ORD) and EQ_UQ (8),
// NEQ_OQ (0x0c); FALSE and TRUE are the mask's constants, which take no lane.
static void check_x86_lane_float_compare(CheckX86LaneCode* code, uint32_t predicate, uint32_t a, uint32_t b, uint32_t offset, uint8_t class)
{
    // UNORD's (a != a) | (b != b), or ORD's (a == a) & (b == b), of the two operands.
    const bool unordered = predicate == 3 || predicate == 8;
    const bool ordered = predicate == 7 || predicate == 0x0c;
    if (predicate == 0 || predicate == 1 || predicate == 2 || predicate == 4 || predicate == 5 || predicate == 6 || predicate == 8 || predicate == 0x0c)
    {
        static const uint8_t kinds[16] = {
            [0] = BUSTER_TYPED_EQ,
            [1] = BUSTER_TYPED_LT,
            [2] = BUSTER_TYPED_LE,
            [4] = BUSTER_TYPED_NE,
            [5] = BUSTER_TYPED_LT,
            [6] = BUSTER_TYPED_LE,
            [8] = BUSTER_TYPED_EQ,
            [12] = BUSTER_TYPED_NE,
        };
        check_x86_lane_load(code, a, offset, class, class);
        check_x86_lane_load(code, b, offset, class, class);
        check_x86_lane_op(code, kinds[predicate], BUSTER_TYPED_CLASS_I32, 0);
        if (predicate == 5 || predicate == 6)
        {
            // NLT and NLE: the ordered compare's negation, true of a NaN.
            check_x86_lane_op(code, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_I32, 1);
            check_x86_lane_op(code, BUSTER_TYPED_XOR, BUSTER_TYPED_CLASS_I32, 0);
        }
    }
    if (unordered || ordered)
    {
        const uint32_t operands[2] = { a, b };
        for (uint32_t j = 0; j < 2; j += 1)
        {
            check_x86_lane_load(code, operands[j], offset, class, class);
            check_x86_lane_load(code, operands[j], offset, class, class);
            check_x86_lane_op(code, unordered ? BUSTER_TYPED_NE : BUSTER_TYPED_EQ, BUSTER_TYPED_CLASS_I32, 0);
            // The first operand's test combines with the compare before it, where there is one (EQ_UQ's, NEQ_OQ's).
            if (j == 1 || predicate == 8 || predicate == 0x0c)
            {
                check_x86_lane_op(code, unordered ? BUSTER_TYPED_OR : BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_I32, 0);
            }
        }
    }
}

// The mask's bit `i` as an element of `class`, all ones where it is set and zero where it isn't: 0 - ((k >> i) & 1), of
// the mask's temporary `local` of `mask_class`.
static void check_x86_lane_bit(CheckX86LaneCode* code, uint32_t local, uint8_t mask_class, uint32_t i, uint8_t class)
{
    check_x86_lane_op(code, BUSTER_TYPED_LOCAL, mask_class, local);
    if (mask_class != class)
    {
        check_x86_lane_op(code, BUSTER_TYPED_CONVERT, class, 0);
    }
    if (i != 0)
    {
        check_x86_lane_op(code, BUSTER_TYPED_CONST, class, i);
        check_x86_lane_op(code, BUSTER_TYPED_SHR, class, 0);
    }
    check_x86_lane_op(code, BUSTER_TYPED_CONST, class, 1);
    check_x86_lane_op(code, BUSTER_TYPED_AND, class, 0);
    check_x86_lane_op(code, BUSTER_TYPED_NEG, class, 0);
}

// MOVE's source of the result's piece of `size` bytes at `offset`: an argument (0 or 1) and its offset there, or false
// for zeros. Pieces are the move's granule (check_x86_lanes_granule): no move splits one.
static bool check_x86_lanes_source(const CheckX86LowerRule* rule, uint32_t move, uint64_t immediate, uint32_t offset, uint32_t* argument, uint32_t* from)
{
    const uint32_t lane = offset & ~15u;
    const uint32_t within = offset & 15u;
    const uint32_t bytes = rule->bytes;
    bool taken = true;
    *argument = 0;
    *from = offset;
    switch (move)
    {
        case CHECK_X86_MOVE_ALIGN:
        {
            // (a:b) shifted right by the immediate's elements, its count modulo the elements': b is the low half.
            const uint32_t count = bytes / rule->element;
            const uint32_t shifted = offset + (uint32_t)(immediate & (count - 1)) * rule->element;
            *argument = shifted < bytes ? 1 : 0;
            *from = shifted < bytes ? shifted : shifted - bytes;
            break;
        }
        case CHECK_X86_MOVE_PALIGNR:
        {
            // Each 16-byte lane's (a:b) shifted right by the immediate's bytes, zeros past 31.
            const uint32_t shifted = within + (uint32_t)(immediate & 0xff);
            taken = shifted < 32;
            *argument = shifted < 16 ? 1 : 0;
            *from = lane + (shifted & 15u);
            break;
        }
        case CHECK_X86_MOVE_BYTES_LEFT:
        {
            const uint32_t count = (uint32_t)(immediate & 0xff);
            taken = count < 16 && within >= count;
            *from = lane + within - (taken ? count : 0);
            break;
        }
        case CHECK_X86_MOVE_BYTES_RIGHT:
        {
            const uint32_t count = (uint32_t)(immediate & 0xff);
            taken = count < 16 && within + count < 16;
            *from = lane + within + (taken ? count : 0);
            break;
        }
        case CHECK_X86_MOVE_LANES:
        {
            // The low half's lanes a's and the high half's b's, each by its field of the immediate: 2 bits of 4 lanes, 1 of 2.
            const uint32_t lanes = bytes / 16;
            const uint32_t index = offset / 16;
            const uint32_t bits = lanes == 4 ? 2 : 1;
            *argument = index < lanes / 2 ? 0 : 1;
            *from = (uint32_t)((immediate >> (bits * index)) & (lanes - 1)) * 16 + within;
            break;
        }
        case CHECK_X86_MOVE_SHUFPS:
        {
            const uint32_t i = within / 4;
            *argument = i < 2 ? 0 : 1;
            *from = lane + (uint32_t)((immediate >> (2 * i)) & 3) * 4;
            break;
        }
        case CHECK_X86_MOVE_SHUFPD:
        {
            const uint32_t i = within / 8;
            *argument = i;
            *from = lane + (uint32_t)((immediate >> (2 * (lane / 16) + i)) & 1) * 8;
            break;
        }
        case CHECK_X86_MOVE_PERMILPS: *from = lane + (uint32_t)((immediate >> (2 * (within / 4))) & 3) * 4; break;
        case CHECK_X86_MOVE_PERMILPD: *from = lane + (uint32_t)((immediate >> (2 * (lane / 16) + within / 8)) & 1) * 8; break;
        case CHECK_X86_MOVE_PSHUFHW:
        {
            const uint32_t i = within / 2;
            *from = i < 4 ? offset : lane + 8 + (uint32_t)((immediate >> (2 * (i - 4))) & 3) * 2;
            break;
        }
        case CHECK_X86_MOVE_PSHUFLW:
        {
            const uint32_t i = within / 2;
            *from = i >= 4 ? offset : lane + (uint32_t)((immediate >> (2 * i)) & 3) * 2;
            break;
        }
        case CHECK_X86_MOVE_PERMQ:
        {
            // Each 32-byte half's quadwords, each by its 2 bits.
            const uint32_t half = offset & ~31u;
            *from = half + (uint32_t)((immediate >> (2 * ((offset & 31u) / 8))) & 3) * 8;
            break;
        }
        case CHECK_X86_MOVE_INSERT:
        {
            // a with b over its lane of `other` bytes at the immediate's low bits.
            const uint32_t size = rule->other;
            const uint32_t index = (uint32_t)(immediate & (bytes / size - 1));
            *argument = offset / size == index ? 1 : 0;
            *from = *argument != 0 ? offset % size : offset;
            break;
        }
        default: break;
    }
    return taken;
}

// MOVE's granule, the bytes of the pieces it moves whole: a byte's for the byte moves, a word's for PSHUFHW's and
// PSHUFLW's, a dword's for the dword moves, and a qword's for the others.
static uint32_t check_x86_lanes_granule(const CheckX86LowerRule* rule, uint32_t move)
{
    uint32_t granule = 8;
    switch (move)
    {
        case CHECK_X86_MOVE_PALIGNR:
        case CHECK_X86_MOVE_BYTES_LEFT:
        case CHECK_X86_MOVE_BYTES_RIGHT: granule = 1; break;
        case CHECK_X86_MOVE_PSHUFHW:
        case CHECK_X86_MOVE_PSHUFLW: granule = 2; break;
        case CHECK_X86_MOVE_SHUFPS:
        case CHECK_X86_MOVE_PERMILPS: granule = 4; break;
        case CHECK_X86_MOVE_ALIGN: granule = rule->element; break;
        default: break;
    }
    return granule;
}

// Pushes a vector result's elements, the last first: each `size` bytes at its offset into `out`, by `kind`.
static void check_x86_lanes_elements(CheckLowerWriter* writer, const CheckX86LowerRule* rule, CheckX86LanesShape shape, uint64_t immediate,
                                     const uint32_t* temporaries, uint8_t mask_class, uint32_t out, const uint32_t* locals)
{
    CheckX86LaneCode code = { 0 };
    const uint32_t element = shape.kind == CHECK_X86_LANES_MOVE ? check_x86_lanes_granule(rule, shape.variant) : rule->element;
    const uint32_t bytes = shape.kind == CHECK_X86_LANES_EXTRACT ? rule->other : rule->bytes;
    const uint8_t stored = check_x86_lane_stored_class(element, false);
    const uint8_t computed = check_x86_lane_computed_class(element, false);
    const uint32_t bits = 8 * element;
    for (uint32_t i = bytes / element; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        const uint32_t offset = i * element;
        if (shape.kind == CHECK_X86_LANES_TERNARY)
        {
            // The elements into locals A, B and C, the function into R, then the result: R where the mask's bit is set, and
            // A (_mask) or zero (_maskz) where it isn't. The function is the OR of its minterms, or the complement of
            // its absent ones' where those are fewer.
            const uint32_t function = (uint32_t)(immediate & 0xff);
            const bool complement = __builtin_popcount(function) > 4;
            const uint32_t terms = complement ? ~function & 0xff : function;
            check_x86_lane_address(&code, out, offset);
            check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, computed, locals[3]);
            check_x86_lane_bit(&code, temporaries[3], mask_class, i, computed);
            check_x86_lane_op(&code, BUSTER_TYPED_AND, computed, 0);
            if (rule->fixed == CHECK_X86_INTO_A)
            {
                check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, computed, locals[0]);
                check_x86_lane_bit(&code, temporaries[3], mask_class, i, computed);
                check_x86_lane_op(&code, BUSTER_TYPED_NOT, computed, 0);
                check_x86_lane_op(&code, BUSTER_TYPED_AND, computed, 0);
                check_x86_lane_op(&code, BUSTER_TYPED_OR, computed, 0);
            }
            check_x86_lane_op(&code, BUSTER_TYPED_SET, stored, 0);
            check_x86_lanes_push(writer, &code);
            check_x86_lane_op(&code, BUSTER_TYPED_CONST, computed, 0);
            for (uint32_t t = 0; t < 8; t += 1)
            {
                if ((terms >> t & 1) != 0)
                {
                    for (uint32_t v = 0; v < 3; v += 1)
                    {
                        check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, computed, locals[v]);
                        if ((t >> (2 - v) & 1) == 0)
                        {
                            check_x86_lane_op(&code, BUSTER_TYPED_NOT, computed, 0);
                        }
                        if (v != 0)
                        {
                            check_x86_lane_op(&code, BUSTER_TYPED_AND, computed, 0);
                        }
                    }
                    check_x86_lane_op(&code, BUSTER_TYPED_OR, computed, 0);
                }
            }
            if (complement)
            {
                check_x86_lane_op(&code, BUSTER_TYPED_NOT, computed, 0);
            }
            check_x86_lane_op(&code, BUSTER_TYPED_SET_LOCAL, computed, locals[3]);
            check_x86_lanes_push(writer, &code);
            for (uint32_t v = 3; v-- > 0;)
            {
                check_x86_lane_load(&code, temporaries[v], offset, stored, computed);
                check_x86_lane_op(&code, BUSTER_TYPED_SET_LOCAL, computed, locals[v]);
            }
            check_x86_lanes_push(writer, &code);
        }
        else
        {
            check_x86_lane_address(&code, out, offset);
            if (shape.kind == CHECK_X86_LANES_ROTATE || shape.kind == CHECK_X86_LANES_FUNNEL)
            {
                // A rotate is a funnel shift of a and a: left, (a << n) | (b >> (bits - n)); right, (a >> n) |
                // (b << (bits - n)); n of 0 is a.
                const uint32_t n = (uint32_t)(immediate & (bits - 1));
                const uint32_t second = shape.kind == CHECK_X86_LANES_FUNNEL ? temporaries[1] : temporaries[0];
                const bool right = shape.variant != 0;
                check_x86_lane_load(&code, temporaries[0], offset, stored, computed);
                if (n != 0)
                {
                    check_x86_lane_op(&code, BUSTER_TYPED_CONST, computed, n);
                    check_x86_lane_op(&code, right ? BUSTER_TYPED_SHR : BUSTER_TYPED_SHL, computed, 0);
                    check_x86_lane_load(&code, second, offset, stored, computed);
                    check_x86_lane_op(&code, BUSTER_TYPED_CONST, computed, bits - n);
                    check_x86_lane_op(&code, right ? BUSTER_TYPED_SHL : BUSTER_TYPED_SHR, computed, 0);
                    check_x86_lane_op(&code, BUSTER_TYPED_OR, computed, 0);
                }
            }
            else if (shape.kind == CHECK_X86_LANES_MOVE)
            {
                uint32_t argument = 0;
                uint32_t from = 0;
                if (check_x86_lanes_source(rule, shape.variant, immediate, offset, &argument, &from))
                {
                    check_x86_lane_load(&code, temporaries[argument], from, stored, computed);
                }
                else
                {
                    check_x86_lane_op(&code, BUSTER_TYPED_CONST, computed, 0);
                }
            }
            else
            {
                // EXTRACT: a's element in its lane where the mask's bit is set, w's where it isn't.
                const uint32_t lane = (uint32_t)(immediate & (rule->bytes / rule->other - 1)) * rule->other;
                check_x86_lane_load(&code, temporaries[0], lane + offset, stored, computed);
                check_x86_lane_bit(&code, temporaries[2], mask_class, i, computed);
                check_x86_lane_op(&code, BUSTER_TYPED_AND, computed, 0);
                check_x86_lane_load(&code, temporaries[1], offset, stored, computed);
                check_x86_lane_bit(&code, temporaries[2], mask_class, i, computed);
                check_x86_lane_op(&code, BUSTER_TYPED_NOT, computed, 0);
                check_x86_lane_op(&code, BUSTER_TYPED_AND, computed, 0);
                check_x86_lane_op(&code, BUSTER_TYPED_OR, computed, 0);
            }
            if (computed != stored)
            {
                check_x86_lane_op(&code, BUSTER_TYPED_CONVERT, stored, 0);
            }
            check_x86_lane_op(&code, BUSTER_TYPED_SET, stored, 0);
            check_x86_lanes_push(writer, &code);
        }
    }
}

// Pushes a compare's mask, the last lane first: the bits ORed into local `mask` of class `bits`, then ANDed with the mask
// argument (temporaries[2]).
static void check_x86_lanes_compare(CheckLowerWriter* writer, const CheckX86LowerRule* rule, uint64_t immediate, bool strict, const uint32_t* temporaries,
                                    uint8_t mask_class, uint32_t mask, uint8_t bits)
{
    const uint32_t lanes = (uint32_t)rule->bytes / rule->element;
    const uint64_t all = lanes == 64 ? UINT64_MAX : (UINT64_C(1) << lanes) - 1;
    const bool floating = rule->form != X86_FORM_NONE;
    bool swap = false;
    const uint32_t predicate = floating ? check_x86_lower_float_predicate(immediate, strict, &swap) : (uint32_t)(immediate & 7);
    // FALSE and TRUE: a constant mask, which takes no lane.
    const bool constant = floating ? predicate == 0x0b || predicate == 0x0f : predicate == 3 || predicate == 7;
    const uint64_t initial = constant && (floating ? predicate == 0x0f : predicate == 7) ? all : 0;
    CheckX86LaneCode code = { 0 };
    check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, bits, mask);
    check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, mask_class, temporaries[2]);
    if (mask_class != bits)
    {
        check_x86_lane_op(&code, BUSTER_TYPED_CONVERT, bits, 0);
    }
    check_x86_lane_op(&code, BUSTER_TYPED_AND, bits, 0);
    check_x86_lanes_push(writer, &code);
    for (uint32_t i = constant ? 0 : lanes; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        // mask |= compare(a[i], b[i]) << i.
        const uint32_t offset = i * rule->element;
        const uint32_t left = temporaries[swap ? 1 : 0];
        const uint32_t right = temporaries[swap ? 0 : 1];
        check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, bits, mask);
        if (floating)
        {
            const uint8_t class = rule->element == 8 ? BUSTER_TYPED_CLASS_F64 : BUSTER_TYPED_CLASS_F32;
            check_x86_lane_float_compare(&code, predicate, left, right, offset, class);
        }
        else
        {
            static const uint8_t kinds[8] = { BUSTER_TYPED_EQ, BUSTER_TYPED_LT, BUSTER_TYPED_LE, 0, BUSTER_TYPED_NE, BUSTER_TYPED_GE, BUSTER_TYPED_GT, 0 };
            const uint8_t stored = check_x86_lane_stored_class(rule->element, rule->is_signed);
            const uint8_t computed = check_x86_lane_computed_class(rule->element, rule->is_signed);
            check_x86_lane_load(&code, left, offset, stored, computed);
            check_x86_lane_load(&code, right, offset, stored, computed);
            check_x86_lane_op(&code, kinds[predicate], BUSTER_TYPED_CLASS_I32, 0);
        }
        check_x86_lane_op(&code, BUSTER_TYPED_CONVERT, bits, 0);
        if (i != 0)
        {
            check_x86_lane_op(&code, BUSTER_TYPED_CONST, bits, i);
            check_x86_lane_op(&code, BUSTER_TYPED_SHL, bits, 0);
        }
        check_x86_lane_op(&code, BUSTER_TYPED_OR, bits, 0);
        check_x86_lane_op(&code, BUSTER_TYPED_SET_LOCAL, bits, mask);
        check_x86_lanes_push(writer, &code);
    }
    check_x86_lane_op(&code, BUSTER_TYPED_CONST, bits, initial);
    check_x86_lane_op(&code, BUSTER_TYPED_SET_LOCAL, bits, mask);
    check_x86_lanes_push(writer, &code);
}

// Pushes GATHER's elements, the last first: each of `element` bytes loaded from its address (the kind's comment above)
// or zero past the index's elements; `scale` the immediate, temporaries w, p, the index and the mask.
static void check_x86_lanes_gather(CheckLowerWriter* writer, const CheckX86LowerRule* rule, uint32_t index_bytes, uint64_t scale, const uint32_t* temporaries,
                                   uint32_t out)
{
    CheckX86LaneCode code = { 0 };
    const uint32_t element = rule->element;
    const uint32_t lanes = rule->bytes / element;
    const uint32_t indices = rule->other / index_bytes;
    const uint8_t stored = check_x86_lane_stored_class(element, false);
    const uint8_t index_class = check_x86_lane_stored_class(index_bytes, true);
    const uint8_t sign_class = check_x86_lane_computed_class(element, true);
    for (uint32_t i = lanes; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        const uint32_t offset = i * element;
        check_x86_lane_address(&code, out, offset);
        if (i < indices)
        {
            // w's element's address, plus (p + sext(index) * scale less it) ANDed with the mask element's sign.
            check_x86_lane_address(&code, temporaries[0], offset);
            check_x86_lane_op(&code, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_U64, temporaries[1]);
            check_x86_lane_load(&code, temporaries[2], i * index_bytes, index_class, BUSTER_TYPED_CLASS_I64);
            check_x86_lane_op(&code, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_op(&code, BUSTER_TYPED_CONST, BUSTER_TYPED_CLASS_U64, scale);
            check_x86_lane_op(&code, BUSTER_TYPED_MUL, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_op(&code, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_address(&code, temporaries[0], offset);
            check_x86_lane_op(&code, BUSTER_TYPED_SUB, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_load(&code, temporaries[3], offset, check_x86_lane_stored_class(element, true), sign_class);
            check_x86_lane_op(&code, BUSTER_TYPED_CONST, sign_class, 8 * element - 1);
            check_x86_lane_op(&code, BUSTER_TYPED_SHR, sign_class, 0);
            check_x86_lane_op(&code, BUSTER_TYPED_CONVERT, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_op(&code, BUSTER_TYPED_AND, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_op(&code, BUSTER_TYPED_ADD, BUSTER_TYPED_CLASS_U64, 0);
            check_x86_lane_op(&code, BUSTER_TYPED_LOAD, stored, 0);
        }
        else
        {
            check_x86_lane_op(&code, BUSTER_TYPED_CONST, stored, 0);
        }
        check_x86_lane_op(&code, BUSTER_TYPED_SET, stored, 0);
        check_x86_lanes_push(writer, &code);
    }
}

// Pushes ROUND's result, the last first: its rounding row's result of the source (the scalar ones' b, the packed one's
// a) set into `rounded`, then each element: the rounded one where the constant mask's bit is set and w's where it
// isn't, and the scalar ones' upper elements a's. Temporaries a, [b,] w and the mask.
static void check_x86_lanes_round(CheckLowerWriter* writer, const CheckX86LowerRule* rule, bool packed, uint64_t immediate, uint64_t mask,
                                  const uint32_t* temporaries, uint32_t rounded, uint32_t out)
{
    CheckX86LaneCode code = { 0 };
    const uint32_t element = rule->element;
    const uint32_t bytes = rule->bytes;
    const uint8_t stored = check_x86_lane_stored_class(element, false);
    const uint32_t w = temporaries[packed ? 1 : 2];
    Tier0Operation row;
    memset(&row, 0, sizeof(row));
    const uint32_t form = packed ? X86_FORM_VROUNDPS_Y_YM256_IMM8 : element == 8 ? X86_FORM_ROUNDSD_X_XM64_IMM8 : X86_FORM_ROUNDSS_X_XM32_IMM8;
    row.records[0] = check_x86_lower_record(form, BUSTER_TIER0_ROLE_VECTOR(0), 0, BUSTER_TIER0_ROLE_VECTOR(0));
    row.records[0].immediate = (int32_t)(immediate & 0xff);
    row.record_count = 1;
    row.operand_count = 1;
    row.operand_classes[0] = BUSTER_TYPED_CLASS_VECTOR;
    row.operand_bytes[0] = (uint8_t)bytes;
    row.result_class = BUSTER_TYPED_CLASS_VECTOR;
    row.result_bytes = (uint8_t)bytes;
    const uint32_t index = check_lower_operation_add(writer, &row);
    for (uint32_t i = bytes / element; i-- > 0 && writer->output->status == CHECK_PROGRAM_OK;)
    {
        const uint32_t offset = i * element;
        const uint32_t source = !packed && i != 0 ? temporaries[0] : (mask >> i & 1) != 0 ? rounded : w;
        check_x86_lane_address(&code, out, offset);
        check_x86_lane_load(&code, source, offset, stored, stored);
        check_x86_lane_op(&code, BUSTER_TYPED_SET, stored, 0);
        check_x86_lanes_push(writer, &code);
    }
    check_x86_lane_address(&code, rounded, 0);
    check_x86_lane_address(&code, temporaries[packed ? 0 : 1], 0);
    check_x86_lane_op(&code, BUSTER_TYPED_LOAD, BUSTER_TYPED_CLASS_VECTOR, bytes);
    check_x86_lane_op(&code, BUSTER_TYPED_X86, BUSTER_TYPED_CLASS_VECTOR, index);
    check_x86_lane_op(&code, BUSTER_TYPED_SET, BUSTER_TYPED_CLASS_VECTOR, bytes);
    check_x86_lanes_push(writer, &code);
}

// An x86 builtin whose row the target can't run, lane by lane (the kinds above): or false, with nothing pushed, where
// check_x86_lanes_shape refuses its rule or its arguments aren't its kind's. `rounding` is its rounding control's
// argument (BUSTER_CHECK_X86_NO_IMMEDIATE for none) and `round` that control's value, which ROUND takes as 4
// (_MM_FROUND_CUR_DIRECTION) alone. Its arguments but the immediate and the rounding control are set into frame
// temporaries, the last first (a vector's of its bytes, an integer's of its class widened to 32 bits), then the result
// is computed into its own temporary and read. Pushed last first.
static bool check_lower_x86_lanes(CheckLowerWriter* writer, uint32_t node, const CheckX86LowerRule* rule, uint64_t immediate, uint32_t rounding,
                                  uint64_t round, bool strict)
{
    const CheckProgram* program = writer->program;
    const CheckGraph* graph = &program->input->graph;
    const CheckStream* stream = program->stream;
    const uint32_t first = graph->argument_offsets[node];
    const uint32_t count = graph->argument_counts[node];
    const CheckX86LanesShape shape = check_x86_lanes_shape(&writer->target, rule, strict);
    const bool round_kind = shape.kind == CHECK_X86_LANES_ROUND;
    bool valid = shape.kind != CHECK_X86_LANES_NONE && count <= BUSTER_TIER0_OPERATION_OPERANDS && rule->immediate < count &&
                 (rounding == BUSTER_CHECK_X86_NO_IMMEDIATE || (round_kind && round == 4));
    uint8_t classes[BUSTER_TIER0_OPERATION_OPERANDS] = { 0 };
    uint32_t taken = 0;
    uint32_t mask_argument = BUSTER_CHECK_X86_NO_IMMEDIATE;
    for (uint32_t a = 0; valid && a < count; a += 1)
    {
        if (a != rule->immediate && a != rounding)
        {
            const TypeId type = stream->argument_conversions[first + a].type;
            classes[taken] = check_lower_x86_operand_class(check_lower_x86_bits_class(writer, type));
            mask_argument = a;
            taken += 1;
        }
    }
    // The vectors first, then the mask where the kind takes one; GATHER's w, p, the index and the mask (vectors but p).
    const bool gather = shape.kind == CHECK_X86_LANES_GATHER;
    valid = valid && taken == (gather ? 4u : shape.vectors + (shape.mask ? 1u : 0u));
    for (uint32_t j = 0; valid && j < taken; j += 1)
    {
        const bool vector = gather ? j != 1 : j < shape.vectors;
        valid = vector ? classes[j] == BUSTER_TYPED_CLASS_VECTOR : tier0_class_integer(classes[j]);
    }
    // ROUND's mask, a constant (clang's backend selects no blend of another).
    uint64_t mask = 0;
    if (valid && round_kind)
    {
        CheckValue value = check_stream_value(stream, graph->argument_nodes[first + mask_argument]);
        check_action_convert_value(program->types, &value, stream->argument_conversions[first + mask_argument].type, node);
        valid = value.constant_kind == CHECK_CONSTANT_INTEGER;
        mask = (uint64_t)value.integer;
    }
    if (valid)
    {
        const TypeId result = check_lower_x86_bits_type(writer, check_stream_type(stream, node));
        uint32_t temporaries[BUSTER_TIER0_OPERATION_OPERANDS] = { 0 };
        for (uint32_t a = 0, j = 0; a < count; a += 1)
        {
            if (a != rule->immediate && a != rounding)
            {
                const TypeId type = stream->argument_conversions[first + a].type;
                temporaries[j] = classes[j] == BUSTER_TYPED_CLASS_VECTOR ? check_lower_temporary_of(writer, type) : check_lower_temporary(writer, classes[j]);
                j += 1;
            }
        }
        const uint8_t mask_class = shape.mask ? classes[taken - 1] : BUSTER_TYPED_CLASS_VOID;
        if (shape.kind == CHECK_X86_LANES_COMPARE)
        {
            // The mask's integer as it is computed: 32 bits up to 32 lanes, 64 past them; then the call's type.
            const uint8_t bits = rule->bytes / rule->element == 64 ? BUSTER_TYPED_CLASS_U64 : BUSTER_TYPED_CLASS_U32;
            const uint32_t local = check_lower_temporary(writer, bits);
            check_lower_convert(writer, check_lower_class_type(bits), result);
            check_x86_lanes_compare(writer, rule, immediate, strict, temporaries, mask_class, local, bits);
        }
        else
        {
            // The result's temporary, read whole; TERNARY's elements in locals of their computed class, ROUND's rounded
            // source in a temporary of the result's type.
            const uint8_t computed = check_x86_lane_computed_class(rule->element, false);
            uint32_t locals[4] = { 0 };
            for (uint32_t l = 0; shape.kind == CHECK_X86_LANES_TERNARY && l < 4; l += 1)
            {
                locals[l] = check_lower_temporary(writer, computed);
            }
            const uint32_t rounded = round_kind ? check_lower_temporary_of(writer, result) : 0;
            const uint32_t out = check_lower_temporary_of(writer, result);
            check_lower_instruction(writer, BUSTER_TYPED_LOCAL, BUSTER_TYPED_CLASS_VECTOR, out);
            if (gather)
            {
                check_x86_lanes_gather(writer, rule, shape.variant, immediate, temporaries, out);
            }
            else if (round_kind)
            {
                check_x86_lanes_round(writer, rule, shape.variant != 0, immediate, mask, temporaries, rounded, out);
            }
            else
            {
                check_x86_lanes_elements(writer, rule, shape, immediate, temporaries, mask_class, out, locals);
            }
        }
        // The arguments into their temporaries, the last first (as check_lower_x86_split's).
        for (uint32_t j = 0; j < taken; j += 1)
        {
            check_lower_instruction(writer, BUSTER_TYPED_SET_LOCAL, classes[j], temporaries[j]);
            if (j + 1 < taken && classes[j + 1] != classes[j])
            {
                check_lower_instruction(writer, BUSTER_TYPED_DISCARD, classes[j], 0);
                check_lower_instruction(writer, BUSTER_TYPED_LOCAL, classes[j], temporaries[j]);
            }
        }
        writer->vectors = true;
        for (uint32_t a = count; a-- > 0;)
        {
            if (a != rule->immediate && a != rounding)
            {
                const TypeId type = stream->argument_conversions[first + a].type;
                const uint8_t class = check_lower_x86_bits_class(writer, type);
                const uint8_t widened = check_lower_x86_operand_class(class);
                if (widened != class)
                {
                    check_lower_convert(writer, check_lower_x86_bits_type(writer, type), check_lower_class_type(widened));
                }
                check_lower_operand(writer, node, a + 3, false, type);
            }
        }
    }
    return valid;
}
