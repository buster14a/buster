#include <buster/tests/compiler/assembly/aarch64_syntax_test.h>
#include <buster/lib/string.h>

#if BUSTER_INCLUDE_TESTS

typedef struct Aarch64SyntaxTestRows Aarch64SyntaxTestRows;
struct Aarch64SyntaxTestRows
{
    u32 index;
    String8 assembly;
};

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_row_contains(String8 assembly, u32* row_index)
{
    BusterAarch64SyntaxCounts counts = buster_aarch64_syntax_counts();
    for (u32 index = 0; index < counts.row_count; index += 1)
    {
        BusterAarch64SyntaxRow row = {0};
        if (buster_aarch64_syntax_row(index, &row) && row.assembly.length == assembly.length &&
            memcmp(row.assembly.pointer, assembly.pointer, (size_t)assembly.length) == 0)
        {
            if (row_index) *row_index = index;
            return true;
        }
    }
    return false;
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_string_equal(String8 left, String8 right)
{
    return left.length == right.length && (!left.length || (left.pointer && right.pointer &&
                                                               memcmp(left.pointer, right.pointer, (size_t)left.length) == 0));
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_print_display(u32 row_index, String8 expected, char8* bytes, u64 capacity)
{
    BusterAarch64SyntaxOutput output = {.pointer = bytes, .capacity = capacity};
    if (!buster_aarch64_syntax_print_row(row_index, (BusterAarch64SyntaxPrintRequest){0}, &output)) return false;
    return aarch64_syntax_test_string_equal((String8){.pointer = output.pointer, .length = output.length}, expected);
}

BUSTER_GLOBAL_LOCAL bool aarch64_syntax_test_match_and_print_concrete(u32 row_index, String8 spelling)
{
    BusterAarch64SyntaxCapture captures[10] = {0};
    BusterAarch64SyntaxChoice choices[4] = {0};
    BusterAarch64SyntaxMatchResult result = {
        .captures = captures,
        .capture_capacity = 10,
        .choices = choices,
        .choice_capacity = 4,
    };
    if (!buster_aarch64_syntax_match_row(row_index, spelling, &result)) return false;
    char8 output_bytes[256] = {0};
    BusterAarch64SyntaxPrintRequest request = {
        .captures = captures,
        .capture_count = result.capture_count,
        .choices = choices,
        .choice_count = result.choice_count,
    };
    BusterAarch64SyntaxOutput output = {.pointer = output_bytes, .capacity = sizeof(output_bytes)};
    if (!buster_aarch64_syntax_print_concrete_row(row_index, request, &output)) return false;
    return aarch64_syntax_test_string_equal((String8){.pointer = output.pointer, .length = output.length}, spelling);
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
                       S8("eea16d7f094badc65614aed988621f48aca5495890847294bb29decc4be1c31c"));
    BUSTER_STRING_TEST(arguments, buster_aarch64_syntax_source_digest(),
                       S8("8485c5c61835d5394d325757ab2964890e8bdfea304c6faa8fd4c23e4c7aabec"));
    BUSTER_TEST(arguments, stats.generic_shape_count == 1165 && stats.exact_shape_count == 1635 &&
                         stats.max_total_ast_nodes == 29 && stats.max_non_lit_non_seq_nodes == 13 &&
                         stats.max_optional_depth == 2 && stats.max_delimiter_nesting == 3 &&
                         stats.max_top_level_comma_groups == 5 && stats.max_anchor_operands == 10 &&
                         stats.max_choice_count == 4 && stats.max_assembly_bytes == 74 &&
                         stats.max_work_items == 132 && stats.max_backtrack_frames == 8);
    BUSTER_TEST(arguments, buster_aarch64_syntax_validate());

    BusterAarch64SyntaxRow first = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_row(0, &first) && first.assembly.length != 0 && first.encoding_name.length != 0);
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
    BUSTER_TEST(arguments, counts.mnemonic_range_count == 695);
    BusterAarch64SyntaxMnemonicRange fixed_prefix_range = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_mnemonic_lookup(S8("ABS"), &fixed_prefix_range) &&
                         buster_aarch64_syntax_mnemonic_lookup(S8("BRK"), &fixed_prefix_range) &&
                         buster_aarch64_syntax_mnemonic_lookup(S8("CFP"), &fixed_prefix_range) &&
                         buster_aarch64_syntax_mnemonic_lookup(S8("SYS"), &fixed_prefix_range) &&
                         buster_aarch64_syntax_mnemonic_lookup(S8("TSB"), &fixed_prefix_range));
    BusterAarch64SyntaxNode mnemonic_node = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_node(first.node_first + 1, &mnemonic_node) &&
                         mnemonic_node.kind == BUSTER_AARCH64_SYNTAX_MNEMONIC &&
                         mnemonic_node.text.length == S8("ABS ").length &&
                         memcmp(mnemonic_node.text.pointer, S8("ABS ").pointer, (size_t)mnemonic_node.text.length) == 0);

    /* Every canonical source template must survive the direct flat matcher and
     * printer.  This exercises all alternatives, optionals, memory/list/lane
     * groups, punctuation and the maximum-depth rows without user dispatch. */
    u32 match_failures = 0;
    u32 print_failures = 0;
    u32 max_assembly_row = 0;
    u32 max_assembly_length = 0;
    u32 max_anchor_row = 0;
    u32 max_anchor_count = 0;
    char8 display_bytes[256] = {0};
    BusterAarch64SyntaxCapture captures[10] = {0};
    BusterAarch64SyntaxChoice choices[4] = {0};
    for (u32 index = 0; index < counts.row_count; index += 1)
    {
        BusterAarch64SyntaxRow row = {0};
        BusterAarch64SyntaxMatchResult match = {
            .captures = captures,
            .capture_capacity = 10,
            .choices = choices,
            .choice_capacity = 4,
        };
        bool matched = buster_aarch64_syntax_row(index, &row) && buster_aarch64_syntax_match_row(index, row.assembly, &match) &&
                       match.consumed == row.assembly.length;
        bool printed = aarch64_syntax_test_print_display(index, row.assembly, display_bytes, sizeof(display_bytes));
        match_failures += !matched;
        print_failures += !printed;
        if (row.assembly.length > max_assembly_length)
        {
            max_assembly_length = (u32)row.assembly.length;
            max_assembly_row = index;
        }
        if (row.anchor_count > max_anchor_count)
        {
            max_anchor_count = row.anchor_count;
            max_anchor_row = index;
        }
    }
    BUSTER_TEST(arguments, match_failures == 0 && print_failures == 0);
    BusterAarch64SyntaxMatchResult direct_match = {
        .captures = captures, .capture_capacity = 10, .choices = choices, .choice_capacity = 4,
    };
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(0, S8("abs <Vd>.<T>, <Vn>.<T>"), &direct_match));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(1, S8("aBs d<d>, D<n>"), &direct_match));

    /* Exercise the generated maxima explicitly, including one-byte-short
     * transactional output and one-slot-short capture storage. */
    BusterAarch64SyntaxRow max_assembly = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_row(max_assembly_row, &max_assembly) &&
                         max_assembly.assembly.length == max_assembly_length && max_assembly_length == stats.max_assembly_bytes);
    char8 max_output_bytes[128] = {0};
    BusterAarch64SyntaxOutput max_output = {.pointer = max_output_bytes, .capacity = sizeof(max_output_bytes)};
    BUSTER_TEST(arguments, buster_aarch64_syntax_print_row(max_assembly_row, (BusterAarch64SyntaxPrintRequest){0}, &max_output) &&
                         max_output.length == max_assembly_length);
    char8 short_output_bytes[128];
    memset(short_output_bytes, 0xa5, sizeof(short_output_bytes));
    char8 short_output_before[128];
    memcpy(short_output_before, short_output_bytes, sizeof(short_output_before));
    BusterAarch64SyntaxOutput short_output = {
        .pointer = short_output_bytes, .length = 3, .capacity = 3 + max_assembly_length - 1,
    };
    BUSTER_TEST(arguments, !buster_aarch64_syntax_print_row(max_assembly_row, (BusterAarch64SyntaxPrintRequest){0}, &short_output) &&
                         short_output.length == 3 && memcmp(short_output_bytes, short_output_before,
                                                             sizeof(short_output_bytes)) == 0);

    BusterAarch64SyntaxRow max_anchor = {0};
    BUSTER_TEST(arguments, buster_aarch64_syntax_row(max_anchor_row, &max_anchor) && max_anchor.anchor_count == max_anchor_count &&
                         max_anchor_count == stats.max_anchor_operands);
    BusterAarch64SyntaxCapture max_capture_storage[10] = {0};
    BusterAarch64SyntaxChoice max_choice_storage[4] = {0};
    BusterAarch64SyntaxMatchResult short_match = {
        .captures = max_capture_storage,
        .capture_capacity = max_anchor_count - 1,
        .capture_count = 2,
        .choices = max_choice_storage,
        .choice_capacity = 4,
        .choice_count = 1,
        .consumed = 123,
    };
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(max_anchor_row, max_anchor.assembly, &short_match) &&
                         short_match.capture_count == 2 && short_match.choice_count == 1 && short_match.consumed == 123);

    u32 condition_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("B.<cond> <label>"), &condition_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(condition_row, S8("B.eq label")));

    u32 branch_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("DMB (<option>|#<imm>)"), &branch_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(branch_row, S8("DMB #0")));
    BusterAarch64SyntaxCapture rollback_captures[2] = {{.spelling = S8("old")}};
    BusterAarch64SyntaxChoice rollback_choices[2] = {{.node_index = 77, .value = 88}};
    BusterAarch64SyntaxMatchResult rollback = {
        .captures = rollback_captures, .capture_capacity = 2, .capture_count = 1,
        .choices = rollback_choices, .choice_capacity = 2, .choice_count = 1, .consumed = 91,
    };
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(branch_row, S8("DMB #0 trailing"), &rollback) &&
                         rollback.capture_count == 1 && rollback.choice_count == 1 && rollback.consumed == 91 &&
                         aarch64_syntax_test_string_equal(rollback_captures[0].spelling, S8("old")) &&
                         rollback_choices[0].node_index == 77 && rollback_choices[0].value == 88);

    u32 nested_optional_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}"),
                                                            &nested_optional_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(nested_optional_row,
                                                            S8("ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>}}"),
                                                            &direct_match));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(nested_optional_row, S8("ADDS w0, w0, w0"), 0));
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(nested_optional_row, S8("ADDS <Wd>, <Wn|WSP>, <Wm>{"), 0));
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(
                         nested_optional_row, S8("ADDS <Wd>, <Wn|WSP>, <Wm>{, <extend> {#<amount>"), 0));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(nested_optional_row,
                                                                          S8("ADDS w0, w0, w0, lsl #0")));
    BusterAarch64SyntaxCapture nested_rollback_captures[10] = {{.spelling = S8("keep")}};
    BusterAarch64SyntaxChoice nested_rollback_choices[4] = {{.node_index = 31, .value = 41}};
    BusterAarch64SyntaxMatchResult nested_rollback = {
        .captures = nested_rollback_captures, .capture_capacity = 10, .capture_count = 1,
        .choices = nested_rollback_choices, .choice_capacity = 4, .choice_count = 1, .consumed = 79,
    };
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(nested_optional_row,
                                                              S8("ADDS w0, w0, w0, lsl #0 trailing"), &nested_rollback) &&
                         nested_rollback.capture_count == 1 && nested_rollback.choice_count == 1 &&
                         nested_rollback.consumed == 79 &&
                         aarch64_syntax_test_string_equal(nested_rollback_captures[0].spelling, S8("keep")) &&
                         nested_rollback_choices[0].node_index == 31 && nested_rollback_choices[0].value == 41);
    u32 modifier_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("BIC <Vd>.<T>, #<imm8>{, LSL #<amount>}"), &modifier_row));
    BUSTER_TEST(arguments, buster_aarch64_syntax_match_row(modifier_row,
                                                            S8("bic <Vd>.<T>, #<imm8>{, lSl #<amount>}"), &direct_match));

    u32 list_row = 0;
    u32 lane_row = 0;
    u32 writeback_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("LD1 { <Vt>.<T>, <Vt2>.<T> }, [<Xn|SP>]"), &list_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("DUP <Vd>.<T>, <Vn>.<Ts>[<index>]"), &lane_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("LDP <Wt1>, <Wt2>, [<Xn|SP>, #<imm>]!"), &writeback_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(list_row, S8("LD1 { v0.4s, v1.4s }, [x2]")));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(lane_row, S8("DUP v0.4s, v1.s[0]")));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(writeback_row, S8("LDP w0, w1, [x2, #0]!")));
    u32 prfm_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("PRFM (<prfop>|#<imm5>), [<Xn|SP>{, #<pimm>}]"), &prfm_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(prfm_row, S8("PRFM #0, [x0]")));
    u32 ret_row = 0;
    BUSTER_TEST(arguments, aarch64_syntax_test_row_contains(S8("RET {<Xn>}"), &ret_row));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(ret_row, S8("RET ")));
    BUSTER_TEST(arguments, aarch64_syntax_test_match_and_print_concrete(ret_row, S8("RET x0")));

    /* Undersized arrays, malformed spans and output overflow are transactional:
     * all pre-existing caller state and bytes remain untouched. */
    BusterAarch64SyntaxCapture tiny_capture = {.spelling = S8("sentinel")};
    BusterAarch64SyntaxChoice tiny_choice = {.node_index = 19, .value = 23};
    BusterAarch64SyntaxMatchResult tiny_result = {
        .captures = &tiny_capture, .capture_capacity = 0, .capture_count = 7,
        .choices = &tiny_choice, .choice_capacity = 0, .choice_count = 9, .consumed = 17,
    };
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(0, first.assembly, &tiny_result) && tiny_result.capture_count == 7 &&
                         tiny_result.choice_count == 9 && tiny_result.consumed == 17 &&
                         aarch64_syntax_test_string_equal(tiny_capture.spelling, S8("sentinel")));
    BusterAarch64SyntaxMatchResult malformed = {.capture_capacity = 1, .captures = 0};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(0, first.assembly, &malformed));
    String8 malformed_input = {.pointer = 0, .length = 1};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_match_row(0, malformed_input, 0));
    char8 output_bytes[256] = {'z'};
    BusterAarch64SyntaxOutput tiny_output = {.pointer = output_bytes, .length = 1, .capacity = 1};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_print_row(0, (BusterAarch64SyntaxPrintRequest){0}, &tiny_output) &&
                         tiny_output.length == 1 && output_bytes[0] == 'z');
    BusterAarch64SyntaxOutput malformed_output = {.pointer = 0, .length = 0, .capacity = 1};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_print_row(0, (BusterAarch64SyntaxPrintRequest){0}, &malformed_output) &&
                         malformed_output.length == 0);
    BusterAarch64SyntaxPrintRequest malformed_request = {.captures = 0, .capture_count = 1};
    BusterAarch64SyntaxOutput valid_output = {.pointer = output_bytes, .capacity = sizeof(output_bytes)};
    BUSTER_TEST(arguments, !buster_aarch64_syntax_print_concrete_row(writeback_row, malformed_request, &valid_output) &&
                         valid_output.length == 0);
    return result;
}

#endif
