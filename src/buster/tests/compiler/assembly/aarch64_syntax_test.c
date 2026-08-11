#include <buster/tests/compiler/assembly/aarch64_syntax_test.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS

typedef struct Aarch64SyntaxTestAnchorState Aarch64SyntaxTestAnchorState;
struct Aarch64SyntaxTestAnchorState
{
    u32 callback_count;
    u32 failed_branch_count;
    u32 first_failed_occurrence;
    u32 transactional_value;
    u32 restore_count;
    u32 selected_branch;
    u32 optional_present;
    u32 fail_print;
};

BUSTER_GLOBAL_LOCAL u64 aarch64_syntax_test_checkpoint(void* user)
{
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    return state ? state->transactional_value : 0;
}

BUSTER_GLOBAL_LOCAL void aarch64_syntax_test_restore(void* user, u64 token)
{
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (state)
    {
        state->transactional_value = (u32)token;
        state->restore_count += 1;
    }
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_match_anchor(void* user, BusterAarch64SyntaxAnchor anchor, String8 input,
                                                           u64* cursor)
{
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (state) state->callback_count += 1;
    if (!cursor || *cursor >= input.length || input.pointer[*cursor] != '<') return false;
    u64 close = *cursor + 1;
    while (close < input.length && input.pointer[close] != '>') close += 1;
    if (close >= input.length) return false;
    String8 spelling = {.pointer = input.pointer + *cursor + 1, .length = close - *cursor - 1};
    if (spelling.length != anchor.spelling.length || memcmp(spelling.pointer, anchor.spelling.pointer, (size_t)spelling.length) != 0)
        return false;
    *cursor = close + 1;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_match_anchor_branch_rollback(void* user, BusterAarch64SyntaxAnchor anchor,
                                                                            String8 input, u64* cursor)
{
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (state && anchor.spelling.length == 6 && memcmp(anchor.spelling.pointer, "option", 6) == 0)
    {
        state->failed_branch_count += 1;
        state->first_failed_occurrence = anchor.occurrence;
        state->transactional_value = 99;
        return false;
    }
    return aarch64_syntax_test_match_anchor(user, anchor, input, cursor);
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_match_anchor_mutating(void* user, BusterAarch64SyntaxAnchor anchor, String8 input,
                                                                    u64* cursor)
{
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (state) state->transactional_value += 1;
    return aarch64_syntax_test_match_anchor(user, anchor, input, cursor);
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_print_anchor(void* user, BusterAarch64SyntaxAnchor anchor, String8* spelling)
{
    BUSTER_UNUSED(user);
    if (!spelling) return false;
    *spelling = anchor.spelling;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_select_alternative(void* user, BusterAarch64SyntaxNode node, u32 branch_count,
                                                                  u32* branch_index)
{
    BUSTER_UNUSED(node);
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (!branch_index || !branch_count) return false;
    *branch_index = state && state->selected_branch < branch_count ? state->selected_branch : 0;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_select_optional(void* user, BusterAarch64SyntaxNode node, bool* present)
{
    BUSTER_UNUSED(node);
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (!present) return false;
    *present = state && state->optional_present;
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_print_anchor_concrete(void* user, BusterAarch64SyntaxAnchor anchor, String8* spelling)
{
    Aarch64SyntaxTestAnchorState* state = (Aarch64SyntaxTestAnchorState*)user;
    if (!spelling) return false;
    if (state && state->fail_print && anchor.spelling.length == 6 && memcmp(anchor.spelling.pointer, "extend", 6) == 0)
    {
        state->transactional_value = 77;
        return false;
    }
    if (anchor.spelling.length >= 1 && anchor.spelling.pointer[0] == 'W') *spelling = S8("w0");
    else if (anchor.spelling.length >= 1 && anchor.spelling.pointer[0] == 'X') *spelling = S8("x0");
    else if (anchor.spelling.length >= 1 && anchor.spelling.pointer[0] == 'V') *spelling = S8("v0");
    else if (anchor.spelling.length == 1 && anchor.spelling.pointer[0] == 'R') *spelling = S8("x0");
    else if (anchor.spelling.length >= 1 && anchor.spelling.pointer[0] == 'T') *spelling = S8("4s");
    else if (anchor.spelling.length == 5 && memcmp(anchor.spelling.pointer, "prfop", 5) == 0) *spelling = S8("pldl1keep");
    else if (anchor.spelling.length == 6 && memcmp(anchor.spelling.pointer, "option", 6) == 0) *spelling = S8("sy");
    else if (anchor.spelling.length == 6 && memcmp(anchor.spelling.pointer, "extend", 6) == 0) *spelling = S8("lsl");
    else if (anchor.spelling.length == 4 && memcmp(anchor.spelling.pointer, "cond", 4) == 0) *spelling = S8("eq");
    else if (anchor.spelling.length == 5 && memcmp(anchor.spelling.pointer, "label", 5) == 0) *spelling = S8("label");
    else *spelling = S8("0");
    return true;
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_row_contains(String8 assembly, String8 needle, u32* row_index)
{
    for (u32 index = 0; index < buster_aarch64_syntax_counts().row_count; index += 1)
    {
        BusterAarch64SyntaxRow row = {0};
        if (!buster_aarch64_syntax_row(index, &row)) continue;
        if (row.assembly.length == assembly.length && memcmp(row.assembly.pointer, assembly.pointer, (size_t)assembly.length) == 0)
        {
            if (row_index) *row_index = index;
            return true;
        }
    }
    BUSTER_UNUSED(needle);
    return false;
}

UnitTestResult aarch64_syntax_tests(UnitTestArguments* arguments)
{
    BUSTER_UNUSED(arguments);
    UnitTestResult result = {0};
    BusterAarch64SyntaxCounts counts = buster_aarch64_syntax_counts();
    BusterAarch64SyntaxStats stats = buster_aarch64_syntax_stats();
    BUSTER_TEST(arguments, buster_aarch64_syntax_schema_version() == BUSTER_AARCH64_SYNTAX_SCHEMA_VERSION);
    BUSTER_TEST(arguments, counts.row_count == 1695 && counts.canonical_row_count == 1523 && counts.alias_row_count == 172);
    BUSTER_TEST(arguments, counts.optional_node_count == 366 && counts.alt_node_count == 33 && counts.mem_node_count == 625 &&
                         counts.mem_writeback_count == 36 && counts.list_node_count == 158 && counts.lane_node_count == 157);
    BUSTER_TEST(arguments, counts.anchor_occurrence_count == 6213 && counts.anchor_alternative_count == 680 &&
                         counts.range_anchor_count == 28);
    BUSTER_TEST(arguments, counts.mnemonic_optional_suffix_count == 55 && counts.mnemonic_condition_count == 1 &&
                         counts.fixed_numeric_literal_count == 273);
    BUSTER_STRING_TEST(arguments, buster_aarch64_syntax_input_digest(),
                       S8("7dccd8605bfe3f3f738e8e070468625ae364c97c7901b119631b2f54396243ac"));
    BUSTER_TEST(arguments, stats.generic_shape_count == 181 && stats.exact_shape_count == 1635 && stats.max_total_ast_nodes == 29 &&
                         stats.max_non_lit_non_seq_nodes == 14 && stats.max_optional_depth == 2 && stats.max_delimiter_nesting == 3 &&
                         stats.max_top_level_comma_groups == 5 && stats.max_anchor_operands == 10);
    BUSTER_TEST(arguments, buster_aarch64_syntax_validate());

    BusterAarch64SyntaxRow first = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_row(0, &first) && first.assembly.length != 0);
    BUSTER_TEST(arguments, !buster_aarch64_syntax_row(counts.row_count, &first));
    BUSTER_TEST(arguments, !buster_aarch64_syntax_node(counts.node_count, (BusterAarch64SyntaxNode*)&first));
    BUSTER_TEST(arguments, !buster_aarch64_syntax_string(counts.string_pool_bytes, 1, &first.assembly));
    BusterAarch64SyntaxMnemonicRange nop_range = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_mnemonic_lookup(S8("aDd"), &nop_range) && nop_range.candidate_count != 0);
    BUSTER_TEST(arguments, !buster_aarch64_syntax_mnemonic_lookup(S8("not-a-mnemonic"), &nop_range));
    BUSTER_TEST(arguments, !buster_aarch64_syntax_mnemonic_candidate(nop_range, nop_range.candidate_count, 0));
    u32 candidate_row = 0;
    BusterAarch64SyntaxMnemonicRange malformed_range = {.candidate_first = counts.mnemonic_candidate_count, .candidate_count = 1};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_mnemonic_candidate(malformed_range, 0, &candidate_row));
    malformed_range.candidate_first = counts.mnemonic_candidate_count - 1;
    malformed_range.candidate_count = 2;
    BUSTER_TEST(arguments, !buster_aarch64_syntax_mnemonic_candidate(malformed_range, 0, &candidate_row));

    BusterAarch64SyntaxCallbacks callbacks = {
        .match_anchor = &aarch64_syntax_test_match_anchor,
        .print_anchor = &aarch64_syntax_test_print_anchor,
        .checkpoint = &aarch64_syntax_test_checkpoint,
        .restore = &aarch64_syntax_test_restore,
    };
    char8 output_bytes[256] = {0};
    u32 print_failures = 0;
    u32 match_failures = 0;
    for (u32 index = 0; index < counts.row_count; index += 1)
    {
        BusterAarch64SyntaxRow row = {0};
        BusterAarch64SyntaxOutput output = {.pointer = output_bytes, .capacity = sizeof(output_bytes)};
        bool row_ok = buster_aarch64_syntax_row(index, &row);
        bool printed = row_ok && buster_aarch64_syntax_print_row(index, callbacks, &output) &&
                      output.length == row.assembly.length && memcmp(output.pointer, row.assembly.pointer, (size_t)output.length) == 0;
        bool matched = row_ok && buster_aarch64_syntax_match_row(index, row.assembly, callbacks);
        print_failures += !printed;
        match_failures += !matched;
    }
    BUSTER_TEST(arguments, print_failures == 0 && match_failures == 0);
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(0, S8("abs <Vd>.<T>, <Vn>.<T>"), callbacks));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(1, S8("aBs d<d>, D<n>"), callbacks));
    u32 condition_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("B.<cond> <label>"), S8("B"), &condition_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(condition_row, S8("b.<cond> <label>"), callbacks));

    u32 branch_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("DMB (<option>|#<imm>)"), S8("DMB"), &branch_row));
    Aarch64SyntaxTestAnchorState branch_state = {0};
    BusterAarch64SyntaxCallbacks branch_callbacks = {
        .match_anchor = &aarch64_syntax_test_match_anchor_branch_rollback,
        .checkpoint = &aarch64_syntax_test_checkpoint,
        .restore = &aarch64_syntax_test_restore,
        .user = &branch_state,
    };
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(branch_row, S8("DMB #<imm>"), branch_callbacks));
    BUSTER_TEST(arguments, branch_state.failed_branch_count == 1 && branch_state.first_failed_occurrence == 0 &&
                         branch_state.transactional_value == 0 && branch_state.restore_count != 0);
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(branch_row, S8("DMB #<imm> trailing"), branch_callbacks));

    u32 nested_optional_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}"), S8("ADDS"),
                                                            &nested_optional_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(nested_optional_row, S8("ADDS <Wd>, <Wn|WSP>, <Wm>"), callbacks));
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(nested_optional_row, S8("ADDS <Wd>, <Wn|WSP>, <Wm>{"), callbacks));

    Aarch64SyntaxTestAnchorState nested_state = {0};
    BusterAarch64SyntaxCallbacks nested_callbacks = callbacks;
    nested_callbacks.match_anchor = &aarch64_syntax_test_match_anchor_mutating;
    nested_callbacks.user = &nested_state;
    String8 nested_partial = S8("ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>");
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(nested_optional_row, nested_partial, nested_callbacks) &&
                         nested_state.transactional_value == 0);

    u32 list_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("LD1 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]"), S8("LD1"), &list_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(list_row, S8("LD1 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]"), callbacks));
    u32 lane_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("DUP <Vd>.<T>, <Vn>.<Ts>[<index>]"), S8("DUP"), &lane_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(lane_row, S8("DUP <Vd>.<T>, <Vn>.<Ts>[<index>]"), callbacks));
    u32 writeback_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("LDP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!"), S8("LDP"), &writeback_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(writeback_row, S8("LDP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!"), callbacks));

    output_bytes[0] = 'x';
    BusterAarch64SyntaxOutput tiny = {.pointer = output_bytes, .length = 1, .capacity = 1};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_print_row(0, callbacks, &tiny) && tiny.length == 1 && output_bytes[0] == 'x');

    Aarch64SyntaxTestAnchorState concrete_state = {.selected_branch = 0, .optional_present = 0};
    BusterAarch64SyntaxCallbacks concrete_callbacks = {
        .match_anchor = &aarch64_syntax_test_match_anchor,
        .print_anchor = &aarch64_syntax_test_print_anchor_concrete,
        .user = &concrete_state,
        .checkpoint = &aarch64_syntax_test_checkpoint,
        .restore = &aarch64_syntax_test_restore,
        .select_alternative = &aarch64_syntax_test_select_alternative,
        .select_optional = &aarch64_syntax_test_select_optional,
    };
    char8 concrete_bytes[256] = {0};
    BusterAarch64SyntaxOutput concrete_output = {.pointer = concrete_bytes, .capacity = sizeof(concrete_bytes)};
    u32 prfm_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("PRFM (<prfop>|#<imm5>), [<Xn|SP>{, #<pimm>}]"), S8("PRFM"), &prfm_row));
    concrete_state.selected_branch = 1;
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_concrete_row(prfm_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == S8("PRFM #0, [x0]").length &&
                         memcmp(concrete_output.pointer, S8("PRFM #0, [x0]").pointer, (size_t)concrete_output.length) == 0);
    u32 ret_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("RET {<Xn>}"), S8("RET"), &ret_row));
    concrete_output.length = 0;
    concrete_state.selected_branch = 0;
    concrete_state.optional_present = 0;
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_concrete_row(ret_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == S8("RET ").length &&
                         memcmp(concrete_output.pointer, S8("RET ").pointer, (size_t)concrete_output.length) == 0);
    concrete_output.length = 0;
    concrete_state.optional_present = 1;
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_concrete_row(nested_optional_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == S8("ADDS w0, w0, w0, lsl #0").length &&
                         memcmp(concrete_output.pointer, S8("ADDS w0, w0, w0, lsl #0").pointer, (size_t)concrete_output.length) == 0);
    concrete_output.length = 0;
    concrete_state.optional_present = 0;
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_concrete_row(writeback_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == S8("LDP w0, w0, [x0, #0]!").length &&
                         memcmp(concrete_output.pointer, S8("LDP w0, w0, [x0, #0]!").pointer, (size_t)concrete_output.length) == 0);
    concrete_output.length = 0;
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_concrete_row(list_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == S8("LD1 { v0.4s, v0.4s }, [x0]").length &&
                         memcmp(concrete_output.pointer, S8("LD1 { v0.4s, v0.4s }, [x0]").pointer, (size_t)concrete_output.length) == 0);
    concrete_output.length = 0;
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_concrete_row(lane_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == S8("DUP v0.4s, v0.4s[0]").length &&
                         memcmp(concrete_output.pointer, S8("DUP v0.4s, v0.4s[0]").pointer, (size_t)concrete_output.length) == 0);
    concrete_output.length = 1;
    concrete_bytes[0] = 'y';
    concrete_state.fail_print = 1;
    concrete_state.optional_present = 1;
    concrete_state.transactional_value = 0;
    BUSTER_TEST(arguments, !buster_aarch64_syntax_print_concrete_row(nested_optional_row, concrete_callbacks, &concrete_output) &&
                         concrete_output.length == 1 && concrete_bytes[0] == 'y' && concrete_state.transactional_value == 0);
    return result;
}

#endif
