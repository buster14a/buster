// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
//
// Exact catalogue declarations and original source call operands. Include after CheckInputReader, the ordinary
// binding API, check_input_predefined_type and check_constant_punctuation. Initialization uses the catalogue's
// scalar/vector batch twins; the source-ordered prepare step consults the current ordinary scope before its cache.
// It creates no CBinding and changes no graph, ignored-token or inferred-type column. A prepared record supplies
// the exact ordinary prototype; call conversions, source ICE witnesses, target features, custom x86 constraints,
// reachability, effects, constant folding and lowering remain separate consumer obligations.
//
// A catalogue hit is not a capability grant. In particular no prefix establishes membership in the separately
// attested frozen x86 constraint inventory. All immutable attributes/features/language/header/shard provenance is
// retained through info. Custom checking, ignored signatures and unsupported descriptors never acquire an ordinary
// call record. Failed descriptor facts may be cached; a resource failure publishes neither that declaration nor a
// partial call. Type interning remains monotonic as documented by builtin_intrinsic_types_decode.
//
// Source argument slices are half-open parser-token intervals, excluding commas and parentheses. Their actual
// count is retained even when it disagrees with the prototype. No argument is evaluated here. Call ids are index+1
// at the original CLOSE_CALL only, with zero everywhere else and at the token sentinel. Name-cache entries are
// UINT32_MAX for a name the intern table's memo hasn't looked up (none of the ids the table has when the unit's set-up
// runs, which fills them), zero for a miss, the high bit plus a catalogue index for a hit not yet decoded, otherwise a dense
// declaration index+1. Initialization requires fresh output/cache fields; a TU with no exact operand candidates
// allocates no intrinsic storage.
#ifndef BUSTER_C_CHECK_INTRINSIC_INPUT_INCLUDED
#define BUSTER_C_CHECK_INTRINSIC_INPUT_INCLUDED

typedef enum CheckInputIntrinsicStatus
{
    CHECK_INPUT_INTRINSIC_NOT_FOUND,
    CHECK_INPUT_INTRINSIC_PREPARED,
    CHECK_INPUT_INTRINSIC_FAILED,
} CheckInputIntrinsicStatus;

typedef struct CheckInputIntrinsicDeclaration CheckInputIntrinsicDeclaration;
struct CheckInputIntrinsicDeclaration
{
    const BuiltinIntrinsicInfo* info;
    BuiltinIntrinsicTypeResult types;
};

typedef struct CheckInputIntrinsicArgument CheckInputIntrinsicArgument;
struct CheckInputIntrinsicArgument
{
    uint32_t first;
    uint32_t end;
};

typedef struct CheckInputIntrinsicCall CheckInputIntrinsicCall;
struct CheckInputIntrinsicCall
{
    uint32_t declaration;
    uint32_t keyword;
    uint32_t opening;
    uint32_t closing;
    uint32_t function_body;
    uint32_t argument_count;
    const CheckInputIntrinsicArgument* arguments;
};

static bool check_input_intrinsic_call_shape(const Parser* parser, uint32_t keyword)
{
    const uint32_t count = parser->tokens.count;
    const uint32_t closing = keyword < count ? parser->tree.parents[keyword] : count;
    const bool call = keyword < count && keyword + 1 < closing && closing < count &&
                      parser->tree.roles[keyword] == PARSE_ROLE_OPERAND &&
                      parser->tree.roles[keyword + 1] == PARSE_ROLE_OPEN_CALL &&
                      parser->tree.roles[closing] == PARSE_ROLE_CLOSE_CALL &&
                      parser->tree.parents[keyword + 1] == closing;
    return call;
}

// A call's source-position identity survives declaration-query suspension and binding replay. Do not look up
// its spelling in the current scope when a type-only consumer resumes after that scope has changed.
static TypeId check_input_intrinsic_type(const CheckInputReader* reader, uint32_t keyword)
{
    TypeId type = 0;
    if (reader->output->intrinsic_ids != 0 && check_input_intrinsic_call_shape(reader->parser, keyword))
    {
        const uint32_t close = reader->parser->tree.parents[keyword];
        const uint32_t id = reader->output->intrinsic_ids[close];
        if (id != 0)
        {
            const CheckInputIntrinsicCall* call = reader->output->intrinsic_calls + id - 1;
            type = reader->output->intrinsic_declarations[call->declaration].types.function_type;
        }
    }
    return type;
}

// The tokens from `first` (a multiple of 64) of the next 64 (a bit each) that are identifier operands outside the deferred
// bodies and the declarations attested declarations skip (CheckInputReader.skips, which the walk never reads), which the
// intrinsics' tables are sized for. The vector path tests 64 at once; the scalar path, its twin, takes every token, and
// the caller tests each.
static uint64_t check_input_intrinsic_operands(const CheckInputReader* reader, uint32_t first)
{
    const Parser* parser = reader->parser;
    const uint32_t count = parser->tokens.count;
    const uint8_t* deferred_tokens = reader->output->deferred_tokens;
    uint64_t result = check_input_scan_all(first, count);
    if (!BUSTER_SCALAR_PATH(parser->options.scalar))
    {
        result = check_input_scan_equal(parser->tokens.kinds, first, count, PARSE_KIND_IDENTIFIER) &
                 check_input_scan_equal(parser->tree.roles, first, count, PARSE_ROLE_OPERAND);
        if (deferred_tokens != 0 && reader->deferred == 0)
        {
            result &= ~check_input_scan_nonzero(deferred_tokens, first, count);
        }
        result &= reader->skips != NULL ? ~reader->skips[first / 64] : ~UINT64_C(0);
    }
    return result;
}

static bool check_input_intrinsic_operand(const CheckInputReader* reader, uint32_t token)
{
    const Parser* parser = reader->parser;
    const uint8_t* deferred_tokens = reader->output->deferred_tokens;
    const bool skipped = reader->skips != NULL && (reader->skips[token / 64] >> (token % 64) & 1) != 0;
    return parser->tokens.kinds[token] == PARSE_KIND_IDENTIFIER && parser->tree.roles[token] == PARSE_ROLE_OPERAND &&
           (deferred_tokens == 0 || reader->deferred != 0 || !deferred_tokens[token]) && !skipped;
}

// The codes of the intern table's memo of each name's catalogue answer (InternTable.intrinsic_rows, #893): a name not
// looked up yet, a name no row has, or the row plus BUSTER_CHECK_INTRINSIC_ROW.
#define BUSTER_CHECK_INTRINSIC_UNKNOWN 0u
#define BUSTER_CHECK_INTRINSIC_MISS 1u
#define BUSTER_CHECK_INTRINSIC_ROW 2u
_Static_assert(BUSTER_BUILTIN_INTRINSIC_COUNT < UINT16_MAX - BUSTER_CHECK_INTRINSIC_ROW, "a code for each catalogue row");

// The code of a name that the catalogue's filter passed: its row's by the exact table, or a miss's.
static uint16_t check_input_intrinsic_code(const InternTable* table, uint32_t name)
{
    const InternSpan span = table->spans[name];
    const BuiltinIntrinsicInfo* info = builtin_intrinsic_table_lookup((const char*)table->records + span.offset, span.length);
    const uint32_t row = info != NULL ? (uint32_t)(info - builtin_intrinsic_infos) : 0;
    return (uint16_t)(info != NULL ? BUSTER_CHECK_INTRINSIC_ROW + row : BUSTER_CHECK_INTRINSIC_MISS);
}

// Whether the catalogue's filter passes `name` (c_builtin_intrinsic_filter.c): its spelling's first and last four bytes
// and length hashed to a bit of the filter. The scalar twin of check_input_intrinsic_candidates' lanes.
static bool check_input_intrinsic_candidate(const InternTable* table, uint32_t name)
{
    const InternSpan span = table->spans[name];
    bool result = false;
    if (span.length != 0)
    {
        const uint32_t hash = builtin_intrinsic_filter_hash(builtin_intrinsic_filter_key((const char*)table->records + span.offset, span.length));
        result = ((builtin_intrinsic_filter_bits[hash / 64] >> (hash % 64)) & 1) != 0;
    }
    return result;
}

// Of the 16 ids from `first` (all below the table's count, `lanes` of them), those the catalogue's filter passes, a bit
// each: their spans read 16 at once, each spelling's first and last four bytes gathered from the records, the key and
// hash of builtin_intrinsic_filter_key and builtin_intrinsic_filter_hash computed in the lanes, and the filter's bits
// gathered. A spelling of under four bytes is gathered from its first byte and masked to its length, where four bytes from
// it lie within the records written (`written`); the others, the table's last records, take the scalar test.
static uint32_t check_input_intrinsic_candidates(const InternTable* table, uint32_t first, __mmask16 lanes, uint64_t written)
{
    const __m512i low = _mm512_maskz_loadu_epi64((__mmask8)lanes, table->spans + first);
    const __m512i high = _mm512_maskz_loadu_epi64((__mmask8)(lanes >> 8), table->spans + first + 8);
    const __m512i evens = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    const __m512i offsets = _mm512_permutex2var_epi32(low, evens, high);
    const __m512i lengths = _mm512_permutex2var_epi32(low, _mm512_add_epi32(evens, _mm512_set1_epi32(1)), high);
    const __mmask16 named = _mm512_mask_test_epi32_mask(lanes, lengths, lengths);
    const __mmask16 whole = _mm512_mask_cmpge_epu32_mask(named, lengths, _mm512_set1_epi32(4));
    // The caller keeps the records within INT32_MAX bytes, so an offset is a gather's index and four past it fits.
    const __mmask16 near = _mm512_mask_cmple_epu32_mask(named, _mm512_add_epi32(offsets, _mm512_set1_epi32(4)), _mm512_set1_epi32((int32_t)written));
    const __mmask16 gathered = whole | (named & ~whole & near);
    const __m512i heads = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), gathered, offsets, table->records, 1);
    const __m512i last_offsets = _mm512_sub_epi32(_mm512_add_epi32(offsets, lengths), _mm512_set1_epi32(4));
    const __m512i tails = _mm512_mask_i32gather_epi32(heads, whole, last_offsets, table->records, 1);
    // Under four bytes: the bytes past the length cleared, and the last four the same as the first.
    const __m512i keep = _mm512_srlv_epi32(_mm512_set1_epi32(-1), _mm512_sub_epi32(_mm512_set1_epi32(32), _mm512_slli_epi32(lengths, 3)));
    const __m512i first_bytes = _mm512_mask_mov_epi32(heads, named & ~whole, _mm512_and_si512(heads, keep));
    const __m512i last_bytes = _mm512_mask_mov_epi32(tails, named & ~whole, first_bytes);
    const __m512i key = _mm512_xor_si512(_mm512_xor_si512(first_bytes, _mm512_rol_epi32(last_bytes, 13)), _mm512_mullo_epi32(lengths, _mm512_set1_epi32((int32_t)0x9e3779b9u)));
    const __m512i mixed = _mm512_mullo_epi32(_mm512_xor_si512(key, _mm512_srli_epi32(key, 16)), _mm512_set1_epi32(0x7feb352d));
    const __m512i hash = _mm512_and_si512(_mm512_xor_si512(mixed, _mm512_srli_epi32(mixed, 15)), _mm512_set1_epi32((int32_t)(BUSTER_BUILTIN_INTRINSIC_FILTER_BITS - 1u)));
    const __m512i words = _mm512_mask_i32gather_epi32(_mm512_setzero_si512(), gathered, _mm512_srli_epi32(hash, 5), builtin_intrinsic_filter_bits, 4);
    const __m512i bits = _mm512_srlv_epi32(words, _mm512_and_si512(hash, _mm512_set1_epi32(31)));
    uint32_t result = _mm512_mask_test_epi32_mask(gathered, bits, _mm512_set1_epi32(1));
    for (uint32_t rest = named & ~gathered; rest != 0; rest &= rest - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(rest);
        result |= (uint32_t)check_input_intrinsic_candidate(table, first + lane) << lane;
    }
    return result;
}

// Gives every id of `table` from `rows`' first unknown one (InternTable.intrinsic_known) to its count its code, in id
// order, which is its records' order: each id the catalogue's filter passes is looked up in the exact table, and every
// other is a miss's. The vector path filters 16 ids at once (check_input_intrinsic_candidates); the scalar path, its twin,
// tests each (check_input_intrinsic_candidate). A command's later units fill only the ids that came since.
static void check_input_intrinsic_fill(InternTable* table, uint16_t* rows, bool scalar)
{
    (void)scalar;
    const uint64_t written = table->record_arena.length - (uint64_t)(table->records - table->record_arena.base);
    const bool lanes_reach = written <= INT32_MAX;
    uint32_t name = table->intrinsic_known;
    if (!BUSTER_SCALAR_PATH(scalar) && lanes_reach)
    {
        for (; name < table->count; name += 16)
        {
            const __mmask16 lanes = (__mmask16)_bzhi_u32(UINT32_MAX, table->count - name < 16 ? table->count - name : 16);
            _mm512_mask_storeu_epi16(rows + name, (__mmask32)lanes, _mm512_set1_epi16((short)BUSTER_CHECK_INTRINSIC_MISS));
            for (uint32_t passed = check_input_intrinsic_candidates(table, name, lanes, written); passed != 0; passed &= passed - 1)
            {
                const uint32_t id = name + (uint32_t)__builtin_ctz(passed);
                rows[id] = check_input_intrinsic_code(table, id);
            }
        }
    }
    else
    {
        for (; name < table->count; name += 1)
        {
            rows[name] = check_input_intrinsic_candidate(table, name) ? check_input_intrinsic_code(table, name) : (uint16_t)BUSTER_CHECK_INTRINSIC_MISS;
        }
    }
    table->intrinsic_known = table->count;
}

// What the operands pass finds: the distinct rows the unit's operands name, a bit each, and how many, and their operands
// that a call's shape follows, which the tables are sized for.
typedef struct CheckInputIntrinsicScan CheckInputIntrinsicScan;
struct CheckInputIntrinsicScan
{
    uint64_t seen[(BUSTER_BUILTIN_INTRINSIC_COUNT + 63) / 64];
    uint32_t declarations;
    uint32_t calls;
};

// The operand `token`, whose name has `code`: a row's counts toward the tables, another nothing.
static void check_input_intrinsic_count(CheckInputIntrinsicScan* scan, const Parser* parser, uint32_t token, uint32_t code)
{
    if (code >= BUSTER_CHECK_INTRINSIC_ROW)
    {
        const uint32_t row = code - BUSTER_CHECK_INTRINSIC_ROW;
        const uint64_t bit = UINT64_C(1) << (row % 64);
        scan->declarations += (scan->seen[row / 64] & bit) == 0;
        scan->seen[row / 64] |= bit;
        scan->calls += check_input_intrinsic_call_shape(parser, token);
    }
}

// The unit's name cache from the memo: UINT32_MAX for a name not looked up, 0 for one no row has, and the high bit and
// the row for a row's. The vector path converts 32 codes at once; the scalar path, its twin, one.
static void check_input_intrinsic_cache(uint32_t* cache, const uint16_t* rows, uint32_t count, bool scalar)
{
    (void)scalar;
    if (BUSTER_SCALAR_PATH(scalar))
    {
        for (uint32_t name = 0; name < count; name += 1)
        {
            const uint32_t code = rows[name];
            cache[name] = code == BUSTER_CHECK_INTRINSIC_UNKNOWN ? UINT32_MAX : code == BUSTER_CHECK_INTRINSIC_MISS ? 0 :
                                                                               UINT32_C(0x80000000) | (code - BUSTER_CHECK_INTRINSIC_ROW);
        }
    }
    else
    {
        for (uint32_t name = 0; name < count; name += 32)
        {
            const __mmask32 lanes = _bzhi_u32(UINT32_MAX, count - name < 32 ? count - name : 32);
            const __m512i codes = _mm512_maskz_loadu_epi16(lanes, rows + name);
            for (uint32_t half = 0; half < 2; half += 1)
            {
                const __m256i part = half == 0 ? _mm512_castsi512_si256(codes) : _mm512_extracti64x4_epi64(codes, 1);
                const __m512i code = _mm512_cvtepu16_epi32(part);
                const __m512i found = _mm512_or_si512(_mm512_sub_epi32(code, _mm512_set1_epi32((int32_t)BUSTER_CHECK_INTRINSIC_ROW)),
                                                      _mm512_set1_epi32(INT32_MIN));
                const __mmask16 unknown = _mm512_cmpeq_epi32_mask(code, _mm512_set1_epi32((int32_t)BUSTER_CHECK_INTRINSIC_UNKNOWN));
                const __mmask16 miss = _mm512_cmpeq_epi32_mask(code, _mm512_set1_epi32((int32_t)BUSTER_CHECK_INTRINSIC_MISS));
                const __m512i value = _mm512_mask_mov_epi32(_mm512_mask_mov_epi32(found, unknown, _mm512_set1_epi32(-1)), miss, _mm512_setzero_si512());
                _mm512_mask_storeu_epi32(cache + name + half * 16, (__mmask16)(lanes >> (half * 16)), value);
            }
        }
    }
}

// Of the operands `selected` of the 64 tokens from `first`, those whose name's code isn't a miss's, which most operands'
// is: a load of each one's code, branch-free. It stays scalar by measurement (#2654): operands are about a fifth of the
// tokens, so 16-lane gathers of the codes ran mostly empty, and on the 9700X the set-up's functions took 252 samples
// of 20 compiles of sqlite3.c and 159 of 3 of musl's 1,336-unit command with them, against 216 and 105 with this loop.
static uint64_t check_input_intrinsic_unresolved(const Parser* parser, const uint16_t* rows, uint32_t first, uint64_t selected)
{
    uint64_t result = 0;
    for (uint64_t bits = selected; bits != 0; bits &= bits - 1)
    {
        const uint32_t lane = (uint32_t)__builtin_ctzll(bits);
        result |= (uint64_t)(rows[parser->tokens.names[first + lane]] != BUSTER_CHECK_INTRINSIC_MISS) << lane;
    }
    return result;
}

// The operands `selected` of the 64 tokens from `first` that need more than a miss's code, a row's, counted: on the
// scalar path every token, each tested as an operand. Out of the operands' loop, since few take it.
static __attribute__((noinline)) void check_input_intrinsic_take(CheckInputIntrinsicScan* scan, const CheckInputReader* reader, const uint16_t* rows, uint32_t first,
                                                                 uint64_t selected, bool scalar)
{
    const Parser* parser = reader->parser;
    for (; selected != 0; selected &= selected - 1)
    {
        const uint32_t token = first + (uint32_t)__builtin_ctzll(selected);
        if (!scalar || check_input_intrinsic_operand(reader, token))
        {
            check_input_intrinsic_count(scan, parser, token, rows[parser->tokens.names[token]]);
        }
    }
}

// The tables for the unit's intrinsics. The memo first gets the codes of the table's ids it hasn't (check_input_intrinsic_fill,
// in id order, once for the table's every unit); then one pass over the unit's operands (check_input_intrinsic_operands)
// counts the distinct rows they name and their call-shaped operands. The vector path finds the operands 64 tokens at once,
// without the scalar test of each, and reads their codes (check_input_intrinsic_unresolved); only a row's go on
// (check_input_intrinsic_take). The scalar path, its twin, tests each token. Both give each name its exact code and the
// same counts. Only a unit with a row's name takes storage: then the name cache, from the memo, in scratch, and the tables.
static bool check_input_intrinsic_initialize(CheckInputReader* reader)
{
    const Parser* parser = reader->parser;
    InternTable* table = reader->pp->table;
    CheckInput* output = reader->output;
    const bool scalar = BUSTER_SCALAR_PATH(parser->options.scalar);
    const uint64_t mark = reader->arena->length;
    const uint64_t scratch_mark = reader->scratch->length;
    uint16_t* rows = (builtin_intrinsic_table_ready || builtin_intrinsic_table_initialize()) ? intern_intrinsic_rows(table) : NULL;
    CheckInputIntrinsicScan scan = { 0 };
    bool valid = rows != NULL;
    if (valid)
    {
        check_input_intrinsic_fill(table, rows, scalar);
    }
    // #113's deferred bodies are read after this pass, which sizes the tables for them too, as source order does.
    for (uint32_t first = 0; valid && first < parser->tokens.count; first += 64)
    {
        const uint64_t operands = check_input_intrinsic_operands(reader, first);
        const uint64_t taken = scalar ? operands : check_input_intrinsic_unresolved(parser, rows, first, operands);
        if (taken != 0)
        {
            check_input_intrinsic_take(&scan, reader, rows, first, taken, scalar);
        }
    }
    if (valid && scan.declarations != 0)
    {
        uint32_t* cache = arena_push_array(reader->scratch, table->count, sizeof(uint32_t), 64);
        uint32_t* ids = arena_push_array(reader->arena, (uint64_t)parser->tokens.count + 1, sizeof(uint32_t), 64);
        uint8_t* source = arena_push_array(reader->arena, BUSTER_BUILTIN_INTRINSIC_COUNT, 1, 64);
        CheckInputIntrinsicDeclaration* declaration_records = arena_push_array(reader->arena, scan.declarations, sizeof(CheckInputIntrinsicDeclaration), 8);
        CheckInputIntrinsicCall* call_records = scan.calls != 0 ? arena_push_array(reader->arena, scan.calls, sizeof(CheckInputIntrinsicCall), 8) : 0;
        valid = cache != 0 && ids != 0 && declaration_records != 0 && (scan.calls == 0 || call_records != 0);
        if (valid)
        {
            check_input_intrinsic_cache(cache, rows, table->count, scalar);
            memset(ids, 0, ((uint64_t)parser->tokens.count + 1) * sizeof(uint32_t));
            if (source != NULL)
            {
                memset(source, 0, BUSTER_BUILTIN_INTRINSIC_COUNT);
            }
            reader->intrinsic_source = source;
            reader->intrinsic_names = cache;
            output->intrinsic_ids = ids;
            output->intrinsic_declarations = declaration_records;
            output->intrinsic_declaration_count = 0;
            output->intrinsic_declaration_capacity = scan.declarations;
            output->intrinsic_calls = call_records;
            output->intrinsic_call_count = 0;
            output->intrinsic_call_capacity = scan.calls;
        }
    }
    if (!valid)
    {
        reader->arena->length = mark;
        reader->scratch->length = scratch_mark;
        output->resource = true;
    }
    return valid;
}

// The unit's FILE for clang's prototype code P (BuiltinIntrinsicTypeInput.file_type), and its jmp_buf, sigjmp_buf and
// ucontext_t for J, SJ and K: the type of the file-scope typedef of `spelling` that the call sees, past any block-scope
// declaration of the name that hides it (clang's ASTContext keeps the last file-scope typedef of the name, which a local
// `int FILE` doesn't change, and no block-scope one), or 0. A deferred body's lane, whose bindings hold only the names
// the body spells, leaves it to source order.
static TypeId check_input_file_type(CheckInputReader* reader, const char* spelling)
{
    check_input_lane_refuse(reader, reader->lane, "a library builtin's prototype reads the unit's FILE, jmp_buf or ucontext_t");
    const uint32_t name = intern_find(reader->pp->table, (const uint8_t*)spelling, (uint32_t)strlen(spelling));
    uint32_t handle = name != 0 && !reader->lane ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) : 0;
    while (handle != 0 && reader->bindings.depth[handle - 1] != 0)
    {
        handle = reader->bindings.previous[handle - 1];
    }
    return handle != 0 && reader->bindings.kind[handle - 1] == C_BINDING_TYPEDEF ? reader->declared_types[reader->bindings.declaration[handle - 1]] : 0;
}

static CheckInputIntrinsicDeclaration check_input_intrinsic_decode(CheckInputReader* reader, const BuiltinIntrinsicInfo* info)
{
    CheckInputIntrinsicDeclaration declaration = { .info = info };
    BuiltinIntrinsicTypeInput input =
    {
        .encoding = info->encoding,
        .encoding_length = info->encoding_length,
        .attributes = info->attributes,
        .attributes_length = info->attributes_length,
        .custom_typechecking = info->custom_typechecking,
        .strict_prototypes = reader->parser->language.standard >= 2023,
        .half_prototypes = cbuiltin_library_candidate(info->name, info->name_length) &&
                           cbuiltin_library_half(cbuiltin_library_id(info->name, info->name_length)),
    };
    const bool va_list = memchr(info->encoding, 'a', info->encoding_length) != 0 || memchr(info->encoding, 'A', info->encoding_length) != 0;
    if (va_list && !info->custom_typechecking)
    {
        input.va_list_type = check_input_predefined_type(reader, reader->predefined_names[4]);
    }
    if (memchr(info->encoding, 'P', info->encoding_length) != 0 && !info->custom_typechecking)
    {
        input.file_type = check_input_file_type(reader, "FILE");
    }
    const char* jump = memchr(info->encoding, 'J', info->encoding_length);
    if (jump != 0 && !info->custom_typechecking)
    {
        const bool signal = jump != info->encoding && jump[-1] == 'S';
        *(signal ? &input.sigjmp_buf_type : &input.jmp_buf_type) = check_input_file_type(reader, signal ? "sigjmp_buf" : "jmp_buf");
    }
    if (memchr(info->encoding, 'K', info->encoding_length) != 0 && !info->custom_typechecking)
    {
        input.ucontext_type = check_input_file_type(reader, "ucontext_t");
    }
    builtin_intrinsic_types_decode(reader->types, &input, &declaration.types);
    return declaration;
}

// Whether clang's table row is a builtin in the unit's language mode: of its non-library rows, __builtin_c23_va_start's
// C23_LANG alone is narrower than every language this checker reads (#2121). Before C23 its name is an unknown builtin's.
static bool check_input_builtin_language(const BuiltinIntrinsicInfo* info, const PPOptions* options)
{
    return strcmp(info->language_token, "C23_LANG") != 0 || options->language.standard >= 2023;
}

static CheckInputIntrinsicStatus check_input_intrinsic_prepare(CheckInputReader* reader, uint32_t keyword)
{
    const Parser* parser = reader->parser;
    CheckInput* output = reader->output;
    CheckInputIntrinsicStatus status = CHECK_INPUT_INTRINSIC_NOT_FOUND;
    const bool operand = keyword < parser->tokens.count && parser->tokens.kinds[keyword] == PARSE_KIND_IDENTIFIER &&
                         parser->tree.roles[keyword] == PARSE_ROLE_OPERAND && (output->deferred_tokens == 0 || !output->deferred_tokens[keyword]);
    const uint32_t name = operand ? parser->tokens.names[keyword] : 0;
    // An x86 builtin of the intrinsic catalogue's rows and leaves (#148, cbuiltin_source_intrinsic) stays the builtin under
    // a declaration of it with external linkage, as clang keeps it: the names <xmmintrin.h>, <emmintrin.h> and
    // <prfchwintrin.h> declare (_mm_sfence, _m_prefetch), and a block-scope declaration's. A definition with internal
    // linkage (static) is the program's function, and a local object of the name hides it. Each row's verdict is
    // remembered (CheckInputReader.intrinsic_source, the top level's reader's alone: a deferred lane has none and works
    // it out each time), so that a declared call site costs a lookup of its row alone. A unit without an intrinsic's
    // name (reader->intrinsic_names null, nearly every unit) reads no binding at all; in one with them, every operand's
    // binding is read, as before, and a bound name of no catalogue row (its cache entry 0) needs nothing more.
    const bool intrinsics = operand && reader->intrinsic_names != 0;
    const uint32_t bound = intrinsics ? c_bindings_lookup(&reader->bindings, C_BINDING_ORDINARY, name) : 0;
    const uint32_t entry = bound != 0 ? reader->intrinsic_names[name] : 0;
    const bool declared = entry != 0 && reader->bindings.kind[bound - 1] == C_BINDING_FUNCTION &&
                          check_input_linkage(reader->declaration_bits, reader->bindings.declaration[bound - 1]) == 2;
    const uint32_t hinted = declared ? entry : 0;
    const BuiltinIntrinsicInfo* header = NULL;
    if (hinted != 0)
    {
        const InternSpan span = reader->pp->table->spans[name];
        const uint8_t* spelling = reader->pp->table->records + span.offset;
        const BuiltinIntrinsicInfo* info = (hinted & UINT32_C(0x80000000)) == 0 ? output->intrinsic_declarations[hinted - 1].info :
                                           hinted == UINT32_MAX ? builtin_intrinsic_lookup((const char*)spelling, span.length) :
                                                                  builtin_intrinsic_at(hinted & UINT32_C(0x7fffffff));
        // A name no row has is no intrinsic's anywhere, which the cache remembers, as the lookup below does.
        reader->intrinsic_names[name] = info == NULL ? 0 : hinted;
        const uint32_t row = info != NULL ? (uint32_t)(info - builtin_intrinsic_infos) : 0;
        uint8_t verdict = info != NULL && reader->intrinsic_source != NULL ? reader->intrinsic_source[row] : 0;
        if (info != NULL && verdict == 0)
        {
            verdict = memchr(info->attributes, 'f', info->attributes_length) == NULL && cbuiltin_source_intrinsic(spelling, span.length) ? 1 : 2;
            if (reader->intrinsic_source != NULL)
            {
                reader->intrinsic_source[row] = verdict;
            }
        }
        header = verdict == 1 ? info : NULL;
    }
    if (intrinsics && (bound == 0 || header != NULL))
    {
        const uint32_t cached = reader->intrinsic_names[name];
        const bool fresh = (cached & UINT32_C(0x80000000)) != 0;
        const BuiltinIntrinsicInfo* info = 0;
        if (fresh)
        {
            if (header != NULL)
            {
                info = header;
            }
            else if (cached == UINT32_MAX)
            {
                const InternSpan span = reader->pp->table->spans[name];
                info = builtin_intrinsic_lookup((const char*)reader->pp->table->records + span.offset, span.length);
            }
            else
            {
                info = builtin_intrinsic_at(cached & UINT32_C(0x7fffffff));
            }
            if (info == 0)
            {
                reader->intrinsic_names[name] = 0;
            }
        }
        else if (cached != 0)
        {
            info = output->intrinsic_declarations[cached - 1].info;
        }
        // Plain library names belong to C's declaration path, including disabled or language-ineligible ones.
        if (info != 0 && memchr(info->attributes, 'f', info->attributes_length) == NULL && check_input_builtin_language(info, &reader->pp->options))
        {
            status = CHECK_INPUT_INTRINSIC_FAILED;
            const uint32_t index = fresh ? output->intrinsic_declaration_count : cached - 1;
            const bool capacity = !fresh || index < output->intrinsic_declaration_capacity;
            output->resource |= !capacity;
            CheckInputIntrinsicDeclaration declaration = { 0 };
            if (capacity)
            {
                declaration = fresh ? check_input_intrinsic_decode(reader, info) : output->intrinsic_declarations[index];
                output->resource |= declaration.types.status == BUILTIN_INTRINSIC_TYPES_CAPACITY;
            }
            const bool ordinary = capacity && !output->resource && declaration.types.status == BUILTIN_INTRINSIC_TYPES_SUCCESS &&
                                  !declaration.types.signature_ignored;
            const bool call = check_input_intrinsic_call_shape(parser, keyword);
            const uint32_t closing = call ? parser->tree.parents[keyword] : parser->tokens.count;
            const uint32_t existing = call ? output->intrinsic_ids[closing] : 0;
            uint32_t count = 0;
            CheckInputIntrinsicArgument* arguments = 0;
            const uint64_t mark = reader->arena->length;
            if (ordinary && call && existing == 0)
            {
                for (uint32_t first = keyword + 2; first < closing;)
                {
                    const uint32_t separator = check_constant_punctuation(parser, first, closing, PARSE_KIND_COMMA);
                    count += 1;
                    first = separator + 1;
                }
                const bool room = output->intrinsic_call_count < output->intrinsic_call_capacity;
                arguments = room && count != 0 ? arena_push_array(reader->arena, count, sizeof(CheckInputIntrinsicArgument), 4) : 0;
                output->resource |= !room || (count != 0 && arguments == 0);
                uint32_t argument = 0;
                for (uint32_t first = keyword + 2; !output->resource && first < closing;)
                {
                    const uint32_t separator = check_constant_punctuation(parser, first, closing, PARSE_KIND_COMMA);
                    arguments[argument] = (CheckInputIntrinsicArgument){ first, separator };
                    argument += 1;
                    first = separator + 1;
                }
            }
            if (!output->resource)
            {
                if (fresh)
                {
                    output->intrinsic_declarations[index] = declaration;
                    output->intrinsic_declaration_count += 1;
                    reader->intrinsic_names[name] = index + 1;
                }
                if (ordinary && call)
                {
                    if (existing == 0)
                    {
                        const uint32_t call_index = output->intrinsic_call_count;
                        output->intrinsic_calls[call_index] = (CheckInputIntrinsicCall)
                        {
                            .declaration = index,
                            .keyword = keyword,
                            .opening = keyword + 1,
                            .closing = closing,
                            .function_body = reader->function_body,
                            .argument_count = count,
                            .arguments = arguments,
                        };
                        output->intrinsic_call_count += 1;
                        output->intrinsic_ids[closing] = call_index + 1;
                    }
                    status = CHECK_INPUT_INTRINSIC_PREPARED;
                }
                else
                {
                    // A FILE builtin without the unit's FILE (check_input_file_type) is clang's error, with its warning
                    // that the builtin needs its header; any other prototype that doesn't decode is refused by name.
                    const bool file = capacity && declaration.types.status == BUILTIN_INTRINSIC_TYPES_UNSUPPORTED &&
                                      (declaration.types.unsupported & BUSTER_INTRINSIC_TYPE_UNSUPPORTED_CONTEXT) != 0 &&
                                      declaration.types.error_offset < info->encoding_length && info->encoding[declaration.types.error_offset] == 'P';
                    if (file)
                    {
                        const CheckWarningEvent event =
                        {
                            .id = CHECK_WARNING_ID_BUILTIN_HEADER,
                            .kind = CHECK_WARNING_EVENT_BUILTIN_HEADER,
                            .token = keyword,
                            .source = BUSTER_CHECK_WARNING_NO_SOURCE,
                            .completion_kind = CHECK_WARNING_COMPLETE_AT_TOKEN,
                            .completion = keyword + 1,
                            .payload = { .name = name, .token = BUSTER_CHECK_WARNING_NO_SOURCE },
                        };
                        check_input_warning_event(reader, &event, NULL, 0);
                    }
                    check_input_report(reader, keyword, file ? CHECK_INPUT_UNKNOWN_BUILTIN : CHECK_INPUT_UNSUPPORTED);
                }
            }
            else
            {
                reader->arena->length = mark;
            }
        }
    }
    return status;
}

#endif
