from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    source_path = Path(path)
    text = source_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}: {old[:120]!r}")
    source_path.write_text(text.replace(old, new, 1))


# Preserve each completed enumerator's C type and the evaluator's unsigned result.
replace_once(
    "src/buster/lib/compiler/frontend/c/c.h",
    """    u32 symbol;
    u64 value;
    bool is_negative;
    u8 reserved[7];
""",
    """    u32 symbol;
    // The declaration-point type for C23, or the completed compatible type
    // selected for a GNU enum. This fills the former alignment hole.
    CTypeId type;
    u64 value;
    bool is_negative;
    bool is_unsigned;
    u8 reserved[6];
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_internal.h",
    """BUSTER_C_EXTERN bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                     CPreprocessResult* result, u64* value_out);
""",
    """BUSTER_C_EXTERN bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                     CPreprocessResult* result, u64* value_out);
BUSTER_C_EXTERN bool c_integer_expression_evaluate_typed(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count,
                                                           u32 expansion_limit, CPreprocessResult* result, u64* value_out,
                                                           bool* is_unsigned_out);
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_source.c",
    """                                                                      CIncludeSearchOrigin including_origin, u64* value_out)
""",
    """                                                                      CIncludeSearchOrigin including_origin, u64* value_out,
                                                                      bool* is_unsigned_out)
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_source.c",
    """        if (valid)
        {
            *value_out = values[0];
        }
""",
    """        if (valid)
        {
            *value_out = values[0];
            if (is_unsigned_out)
            {
                *is_unsigned_out = (value_flags[0] & C_CONDITIONAL_VALUE_UNSIGNED) != 0;
            }
        }
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_source.c",
    """BUSTER_C_SHARED bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                       CPreprocessResult* result, u64* value_out)
{
    CSpellingSpace view = {
        .base = (char8*)spelling_base,
    };
    CPpToken* wrapped = arena_allocate(arena, CPpToken, token_count);
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        wrapped[token_index] = (CPpToken){
            .token = tokens[token_index],
        };
    }
    return c_integer_expression_evaluate_with_features(arena, &view, 0, 0, 0, wrapped, token_count, expansion_limit, result, 0, (String8){0},
                                                       (CIncludeSearchOrigin){0}, value_out);
}
""",
    """BUSTER_C_SHARED bool c_integer_expression_evaluate_typed(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count,
                                                             u32 expansion_limit, CPreprocessResult* result, u64* value_out,
                                                             bool* is_unsigned_out)
{
    CSpellingSpace view = {
        .base = (char8*)spelling_base,
    };
    CPpToken* wrapped = arena_allocate(arena, CPpToken, token_count);
    for (u32 token_index = 0; token_index < token_count; token_index += 1)
    {
        wrapped[token_index] = (CPpToken){
            .token = tokens[token_index],
        };
    }
    return c_integer_expression_evaluate_with_features(arena, &view, 0, 0, 0, wrapped, token_count, expansion_limit, result, 0, (String8){0},
                                                       (CIncludeSearchOrigin){0}, value_out, is_unsigned_out);
}

BUSTER_C_SHARED bool c_integer_expression_evaluate(Arena* arena, char8 const* spelling_base, CToken* tokens, u32 token_count, u32 expansion_limit,
                                                       CPreprocessResult* result, u64* value_out)
{
    return c_integer_expression_evaluate_typed(arena, spelling_base, tokens, token_count, expansion_limit, result, value_out, 0);
}
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_source.c",
    """                                                    including_origin, &value);
""",
    """                                                    including_origin, &value, 0);
""",
)

# Keep enum values as sign/magnitude plus the evaluator's unsigned result, so
# prior enumerators can be retokenized without changing their arithmetic type.
replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """        u32 enum_start = open + 1;
        s64 previous_value = -1;
""",
    """        u32 enum_start = open + 1;
        u64 previous_value = 1;
        bool previous_is_negative = true;
        bool previous_is_unsigned = false;
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """            u32 enum_value_index = c_parse_skip_attributes(preprocess, enum_start + 1, token_index);
            s64 value = previous_value + 1;
""",
    """            u32 enum_value_index = c_parse_skip_attributes(preprocess, enum_start + 1, token_index);
            u64 value = 0;
            bool value_is_negative = false;
            bool value_is_unsigned = previous_is_unsigned;
            if (previous_is_negative)
            {
                BUSTER_VALIDATE(previous_value != 0);
                value = previous_value - 1;
                value_is_negative = value != 0;
                value_is_unsigned = false;
            }
            else
            {
                if (previous_value == UINT64_MAX)
                {
                    c_parse_diagnostic(result, c_preprocess_token_location(&preprocess, name), C_DIAGNOSTIC_INVALID_CONSTEXPR,
                                       S8("enumerator value is not representable"));
                    return C_TYPE_ID_INVALID;
                }
                value = previous_value + 1;
            }
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """                            evaluation_tokens[evaluation_token_count++] = c_space_token(&enum_space, string_format(temporary.arena, S8("{u64}"), member->value),
                                                                                        C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
""",
    """                            String8 member_spelling = member->is_unsigned
                                                          ? string_format(temporary.arena, S8("{u64}U"), member->value)
                                                          : string_format(temporary.arena, S8("{u64}"), member->value);
                            evaluation_tokens[evaluation_token_count++] =
                                c_space_token(&enum_space, member_spelling, C_TOKEN_PREPROCESSING_NUMBER, C_PUNCTUATOR_NONE);
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """                    u64 evaluated = 0;
                    bool valid = c_integer_expression_evaluate(temporary.arena, enum_space.base, evaluation_tokens,
                                                               evaluation_token_count, 65536, &evaluation, &evaluated);
                    if (!valid || evaluation.diagnostic_count)
                    {
                        c_parse_diagnostic(
                            result, c_preprocess_token_location(&preprocess, name), C_DIAGNOSTIC_INVALID_CONSTEXPR,
                            string_format(result->arena, S8("enumerator '{S8}' is not an integer constant expression"),
                                          c_token_spelling(preprocess.spelling_base, name)));
                    }
                    else if (evaluated <= INT64_MAX)
                    {
                        value = (s64)evaluated;
                    }
                    else
                    {
                        value = -1 - (s64)(UINT64_MAX - evaluated);
                    }
""",
    """                    u64 evaluated = 0;
                    bool evaluated_is_unsigned = false;
                    bool valid = c_integer_expression_evaluate_typed(temporary.arena, enum_space.base, evaluation_tokens,
                                                                     evaluation_token_count, 65536, &evaluation, &evaluated,
                                                                     &evaluated_is_unsigned);
                    if (!valid || evaluation.diagnostic_count)
                    {
                        c_parse_diagnostic(
                            result, c_preprocess_token_location(&preprocess, name), C_DIAGNOSTIC_INVALID_CONSTEXPR,
                            string_format(result->arena, S8("enumerator '{S8}' is not an integer constant expression"),
                                          c_token_spelling(preprocess.spelling_base, name)));
                    }
                    else if (evaluated_is_unsigned || evaluated <= INT64_MAX)
                    {
                        value = evaluated;
                        value_is_negative = false;
                        value_is_unsigned = evaluated_is_unsigned;
                    }
                    else
                    {
                        value = 0 - evaluated;
                        value_is_negative = true;
                        value_is_unsigned = false;
                    }
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """                .symbol = name.symbol,
                .value = value < 0 ? (u64)(-value) : (u64)value,
                .is_negative = value < 0,
            };
            previous_value = value;
""",
    """                .symbol = name.symbol,
                .value = value,
                .is_negative = value_is_negative,
                .is_unsigned = value_is_unsigned,
            };
            previous_value = value;
            previous_is_negative = value_is_negative;
            previous_is_unsigned = value_is_unsigned;
""",
)

# GNU pre-C23 enums use one completed compatible type. Select it from target
# widths, then attach it to both the enum object type and every enumerator.
replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """            enum_start = token_index + 1;
        }
    }
    if (kind != C_TYPE_ENUM)
""",
    """            enum_start = token_index + 1;
        }
        u32 enum_member_count = result->enum_member_count - aggregate->enum_member_start;
        CTypeId enumerator_type = enum_underlying_type;
        bool completed_gnu_type = c_preprocess_dialect_is_gnu(preprocess.dialect) &&
                                  !c_preprocess_dialect_is_c23(preprocess.dialect) &&
                                  enum_underlying_type.value == C_ID_UNDERLYING_INVALID;
        if (completed_gnu_type)
        {
            CTypeKind candidates[] = {
                C_TYPE_INT,
                C_TYPE_UNSIGNED_INT,
                C_TYPE_LONG,
                C_TYPE_UNSIGNED_LONG,
                C_TYPE_LONG_LONG,
                C_TYPE_UNSIGNED_LONG_LONG,
            };
            enumerator_type = C_TYPE_ID_INVALID;
            for (u32 candidate_index = 0; candidate_index < BUSTER_ARRAY_LENGTH(candidates); candidate_index += 1)
            {
                CTypeKind candidate = candidates[candidate_index];
                bool candidate_is_unsigned = candidate == C_TYPE_UNSIGNED_INT || candidate == C_TYPE_UNSIGNED_LONG ||
                                             candidate == C_TYPE_UNSIGNED_LONG_LONG;
                u64 candidate_size = 0;
                u32 candidate_alignment = 0;
                bool fits = c_parse_builtin_type_layout(preprocess.target, candidate, &candidate_size, &candidate_alignment) &&
                            candidate_size && candidate_size <= 8;
                u32 candidate_bits = (u32)(candidate_size * 8);
                for (u32 member_index = 0; fits && member_index < enum_member_count; member_index += 1)
                {
                    CEnumMember* member = &result->enum_members[aggregate->enum_member_start + member_index];
                    if (member->is_negative)
                    {
                        fits = !candidate_is_unsigned &&
                               (candidate_bits >= 64 || member->value <= (UINT64_C(1) << (candidate_bits - 1)));
                    }
                    else if (candidate_is_unsigned)
                    {
                        fits = candidate_bits >= 64 || member->value < (UINT64_C(1) << candidate_bits);
                    }
                    else
                    {
                        fits = candidate_bits >= 64 ? member->value <= INT64_MAX
                                                    : member->value < (UINT64_C(1) << (candidate_bits - 1));
                    }
                }
                if (fits)
                {
                    enumerator_type = c_parse_add_type(result, (CType){
                                                                        .element_type = C_TYPE_ID_INVALID,
                                                                        .return_type = C_TYPE_ID_INVALID,
                                                                        .array_bound = C_ARRAY_BOUND_INVALID,
                                                                        .kind = candidate,
                                                                        .is_complete = true,
                                                                    });
                    break;
                }
            }
            BUSTER_VALIDATE(enumerator_type.value != C_ID_UNDERLYING_INVALID);
            aggregate = &result->types[type.value];
            aggregate->element_type = enumerator_type;
        }
        else if (enumerator_type.value == C_ID_UNDERLYING_INVALID)
        {
            enumerator_type = c_parse_add_type(result, (CType){
                                                                .element_type = C_TYPE_ID_INVALID,
                                                                .return_type = C_TYPE_ID_INVALID,
                                                                .array_bound = C_ARRAY_BOUND_INVALID,
                                                                .kind = C_TYPE_INT,
                                                                .is_complete = true,
                                                            });
        }
        for (u32 member_index = 0; member_index < enum_member_count; member_index += 1)
        {
            result->enum_members[aggregate->enum_member_start + member_index].type = enumerator_type;
        }
    }
    if (kind != C_TYPE_ENUM)
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_parse.c",
    """                .type = enum_integer_type,
""",
    """                .type = member->type.value < result.type_count ? member->type : enum_integer_type,
""",
)

# Consume the enumerator entity type instead of forcing int in prediction,
# lowering, and conservative static-expression scans.
replace_once(
    "src/buster/lib/compiler/frontend/c/c_gen.c",
    """    else if (entity.value < builder->parse.entity_count && builder->parse.entities[entity.value].kind == C_ENTITY_ENUMERATOR)
    {
        type = builder->s32_type;
    }
""",
    """    else if (entity.value < builder->parse.entity_count && builder->parse.entities[entity.value].kind == C_ENTITY_ENUMERATOR)
    {
        CTypeId enumerator_type = builder->parse.entities[entity.value].type;
        type = enumerator_type.value < builder->parse.type_count ? builder->c_type_ir_map[enumerator_type.value] : builder->s32_type;
        if (type.value == IR_ID_UNDERLYING_INVALID)
        {
            type = builder->s32_type;
        }
    }
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_gen.c",
    """                else if (entity.value < builder->parse.entity_count && builder->parse.entities[entity.value].kind == C_ENTITY_ENUMERATOR)
                {
                    CEntity* enumerator = &builder->parse.entities[entity.value];
                    value = c_ir_emit_integer_value(builder, enumerator->constant_value, enumerator->constant_is_negative, token);
                }
""",
    """                else if (entity.value < builder->parse.entity_count && builder->parse.entities[entity.value].kind == C_ENTITY_ENUMERATOR)
                {
                    CEntity* enumerator = &builder->parse.entities[entity.value];
                    IrTypeId enumerator_type = enumerator->type.value < builder->parse.type_count
                                                   ? builder->c_type_ir_map[enumerator->type.value]
                                                   : builder->s32_type;
                    if (enumerator_type.value == IR_ID_UNDERLYING_INVALID)
                    {
                        enumerator_type = builder->s32_type;
                    }
                    value = c_ir_emit_integer_value_typed(builder, enumerator->constant_value, enumerator->constant_is_negative, token,
                                                          enumerator_type);
                }
""",
)

replace_once(
    "src/buster/lib/compiler/frontend/c/c_gen.c",
    """            else if (entity.value < builder->parse.entity_count && builder->parse.entities[entity.value].kind == C_ENTITY_ENUMERATOR)
            {
                candidate = builder->s32_type;
            }
""",
    """            else if (entity.value < builder->parse.entity_count && builder->parse.entities[entity.value].kind == C_ENTITY_ENUMERATOR)
            {
                CTypeId enumerator_type = builder->parse.entities[entity.value].type;
                candidate = enumerator_type.value < builder->parse.type_count ? builder->c_type_ir_map[enumerator_type.value] : builder->s32_type;
                if (candidate.value == IR_ID_UNDERLYING_INVALID)
                {
                    candidate = builder->s32_type;
                }
            }
""",
)

Path("tests/basic_c_enum_underlying_type.c").write_text(
    """// GNU C completes an enum with the first target integer type that can
// represent every value, and its enumerator constants have that compatible
// type after the closing brace. Exercise both type queries and emitted values.
#define IS_UNSIGNED_INT(value) _Generic((value), unsigned int: 1, default: 0)

enum Issue183Wide
{
    ISSUE183_ZERO = 0,
    ISSUE183_MID = 70000,
    ISSUE183_HIGH = 0x80000000u,
};

enum Issue183Chained
{
    ISSUE183_CHAIN_BASE = 0x80000000u,
    ISSUE183_CHAIN_EXPRESSION = ISSUE183_CHAIN_BASE + 1,
    ISSUE183_CHAIN_IMPLICIT,
};

_Static_assert(IS_UNSIGNED_INT(ISSUE183_ZERO), "completed enumerator type");
_Static_assert(IS_UNSIGNED_INT(ISSUE183_MID), "completed enumerator type");
_Static_assert(IS_UNSIGNED_INT(ISSUE183_HIGH), "unsigned boundary enumerator type");
_Static_assert(IS_UNSIGNED_INT((enum Issue183Wide)0), "enum compatible type");
_Static_assert(IS_UNSIGNED_INT(ISSUE183_CHAIN_BASE), "prior unsigned enumerator type");
_Static_assert(IS_UNSIGNED_INT(ISSUE183_CHAIN_EXPRESSION), "folded unsigned enumerator type");
_Static_assert(IS_UNSIGNED_INT(ISSUE183_CHAIN_IMPLICIT), "implicit unsigned enumerator type");
_Static_assert(IS_UNSIGNED_INT(1 ? ISSUE183_HIGH : ISSUE183_ZERO), "conditional enum type");
_Static_assert(_Generic(0x80000000, unsigned int: 1, default: 0), "hex literal boundary type");

static unsigned int runtime_value(enum Issue183Wide value)
{
    return value + 1u;
}

int main(void)
{
    volatile enum Issue183Wide value = ISSUE183_HIGH;
    if (sizeof(value) != sizeof(unsigned int))
    {
        return 1;
    }
    if (runtime_value(value) != 0x80000001u)
    {
        return 2;
    }
    if (ISSUE183_CHAIN_EXPRESSION != 0x80000001u || ISSUE183_CHAIN_IMPLICIT != 0x80000002u)
    {
        return 3;
    }
    return 0;
}
"""
)
