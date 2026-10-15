# Work ledger: baseline `base`

| Family | Units | Compiled | Identical ledger/plain | Callgrind units | Callgrind Ir | Preprocessed tokens | Plain minor faults |
|---|---:|---:|---:|---:|---:|---:|---:|
| selfhost | 1 | 1 | 1 | 1 | 26,376,042,187 | 4,376,898 | 271,997 |
| fixtures | 359 | 358 | 359 | 358 | 9,592,177,702 | 316,107 | 689,787 |
| **corpus** | 360 | 359 | 360 | 359 | 35,968,219,889 | 4,693,005 | 961,784 |

## Phase attribution (ledger build)

| Phase | selfhost faults | fixtures faults | corpus faults | corpus arena calls |
|---|---:|---:|---:|---:|
| startup | 428 | 154,377 | 154,805 | 7,966 |
| preprocess | 102,022 | 111,833 | 213,855 | 4,891,119 |
| parse | 4,388 | 644 | 5,032 | 27,619 |
| semantic | 56,342 | 23,661 | 80,003 | 1,680,085 |
| lower | 88,919 | 28,774 | 117,693 | 7,234,399 |
| prepare | 7,388 | 1,122 | 8,510 | 420,913 |
| target_prewarm | 1,076 | 385,434 | 386,510 | 0 |
| codegen | 20,865 | 84,603 | 105,468 | 1,763,412 |
| object | 24,724 | 865 | 25,589 | 358,271 |
| output | 15,496 | 995 | 16,491 | 56,350 |

## Mechanism ledger (exact counts, summed over units)

### ir_construction

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `ir_construction.after_ssa_block_rows` | 203,975 | 14,575 | 218,550 |
| `ir_construction.after_ssa_instruction_rows` | 2,030,511 | 167,847 | 2,198,358 |
| `ir_construction.after_ssa_value_rows` | 1,699,395 | 144,330 | 1,843,725 |
| `ir_construction.before_ssa_block_rows` | 203,975 | 14,575 | 218,550 |
| `ir_construction.before_ssa_instruction_rows` | 1,818,974 | 163,802 | 1,982,776 |
| `ir_construction.before_ssa_value_rows` | 1,993,295 | 164,210 | 2,157,505 |
| `ir_construction.block_appends` | 204,480 | 14,577 | 219,057 |
| `ir_construction.block_grows` | 0 | 3 | 3 |
| `ir_construction.block_rows_copied` | 0 | 39 | 39 |
| `ir_construction.body_tokens` | 2,432,574 | 187,213 | 2,619,787 |
| `ir_construction.c_statement_extent_peels` | 14 | 6 | 20 |
| `ir_construction.c_statement_extent_spans` | 23,312 | 1,541 | 24,853 |
| `ir_construction.cfg_builds` | 6,389 | 2,596 | 8,985 |
| `ir_construction.cfg_copy_sources` | 29,084 | 3,389 | 32,473 |
| `ir_construction.cfg_incoming_visits` | 29,080 | 3,186 | 32,266 |
| `ir_construction.cfg_parameter_visits` | 14,496 | 1,557 | 16,053 |
| `ir_construction.cfg_scratch_slots` | 817,920 | 58,308 | 876,228 |
| `ir_construction.cfg_target_visits` | 289,038 | 15,921 | 304,959 |
| `ir_construction.cfg_terminator_checks` | 2,032,483 | 161,731 | 2,194,214 |
| `ir_construction.cfg_unique_edges` | 289,038 | 15,921 | 304,959 |
| `ir_construction.commit_checks` | 2,063,007 | 170,412 | 2,233,419 |
| `ir_construction.commit_closes` | 203,975 | 14,575 | 218,550 |
| `ir_construction.commit_finalized_blocks` | 203,975 | 14,575 | 218,550 |
| `ir_construction.commit_insertions` | 211,537 | 4,045 | 215,582 |
| `ir_construction.commit_operand_checks` | 2,038,636 | 155,948 | 2,194,584 |
| `ir_construction.commit_result_binds` | 1,717,563 | 145,359 | 1,862,922 |
| `ir_construction.commit_retractions` | 32,033 | 1,933 | 33,966 |
| `ir_construction.commit_target_checks` | 288,346 | 15,945 | 304,291 |
| `ir_construction.commit_truncated_rows` | 463 | 632 | 1,095 |
| `ir_construction.debug_function_index_rows` | 7,595 | 0 | 7,595 |
| `ir_construction.debug_value_blocks` | 11,801 | 0 | 11,801 |
| `ir_construction.debug_value_local_visits` | 12 | 0 | 12 |
| `ir_construction.final_block_slots` | 2,483,686 | 208,020 | 2,691,706 |
| `ir_construction.final_instruction_slots` | 7,481,551 | 622,235 | 8,103,786 |
| `ir_construction.final_value_slots` | 7,481,383 | 621,923 | 8,103,306 |
| `ir_construction.fold_birth_avoidable_bytes` | 742,872 | 63,956 | 806,828 |
| `ir_construction.fold_birth_binary_constant` | 35,314 | 1,120 | 36,434 |
| `ir_construction.fold_birth_binary_left_identity` | 39 | 1 | 40 |
| `ir_construction.fold_birth_binary_right_identity` | 200 | 212 | 412 |
| `ir_construction.fold_birth_cascade_binary_constant` | 3,317 | 1,582 | 4,899 |
| `ir_construction.fold_birth_cascade_binary_left_identity` | 1 | 31 | 32 |
| `ir_construction.fold_birth_cascade_binary_right_identity` | 38 | 123 | 161 |
| `ir_construction.fold_birth_cascade_cast_constant` | 1,078 | 1,162 | 2,240 |
| `ir_construction.fold_birth_cascade_unary_constant` | 248 | 67 | 315 |
| `ir_construction.fold_birth_cast_constant` | 108,617 | 7,600 | 116,217 |
| `ir_construction.fold_birth_cast_identity` | 1 | 7 | 8 |
| `ir_construction.fold_birth_scalar_rows` | 464,139 | 52,171 | 516,310 |
| `ir_construction.fold_birth_unary_constant` | 474 | 656 | 1,130 |
| `ir_construction.fold_form_bool_compare_aggregate` | 11,532 | 34 | 11,566 |
| `ir_construction.fold_form_bool_compare_branch` | 0 | 19 | 19 |
| `ir_construction.fold_form_bool_compare_call` | 1,695 | 5 | 1,700 |
| `ir_construction.fold_form_bool_compare_operator` | 4 | 193 | 197 |
| `ir_construction.fold_form_bool_compare_other` | 0 | 1 | 1 |
| `ir_construction.fold_form_bool_compare_return` | 4,091 | 6 | 4,097 |
| `ir_construction.fold_form_bool_compare_store` | 17,540 | 716 | 18,256 |
| `ir_construction.fold_form_other_aggregate` | 12 | 17 | 29 |
| `ir_construction.fold_form_other_call` | 84 | 79 | 163 |
| `ir_construction.fold_form_other_operator` | 916 | 779 | 1,695 |
| `ir_construction.fold_form_other_other` | 4 | 30 | 34 |
| `ir_construction.fold_form_other_return` | 2 | 9 | 11 |
| `ir_construction.fold_form_other_store` | 131 | 61 | 192 |
| `ir_construction.fold_form_other_unused` | 1 | 9 | 10 |
| `ir_construction.fold_form_reinterpret_aggregate` | 909 | 223 | 1,132 |
| `ir_construction.fold_form_reinterpret_call` | 10,356 | 63 | 10,419 |
| `ir_construction.fold_form_reinterpret_operator` | 35,371 | 874 | 36,245 |
| `ir_construction.fold_form_reinterpret_other` | 0 | 43 | 43 |
| `ir_construction.fold_form_reinterpret_return` | 818 | 14 | 832 |
| `ir_construction.fold_form_reinterpret_store` | 12,438 | 317 | 12,755 |
| `ir_construction.fold_form_sign_extend_aggregate` | 154 | 283 | 437 |
| `ir_construction.fold_form_sign_extend_call` | 1,860 | 912 | 2,772 |
| `ir_construction.fold_form_sign_extend_operator` | 34,841 | 2,713 | 37,554 |
| `ir_construction.fold_form_sign_extend_other` | 0 | 62 | 62 |
| `ir_construction.fold_form_sign_extend_return` | 78 | 10 | 88 |
| `ir_construction.fold_form_sign_extend_store` | 2,926 | 263 | 3,189 |
| `ir_construction.fold_form_sign_extend_unused` | 0 | 1 | 1 |
| `ir_construction.fold_form_truncate_aggregate` | 194 | 1,321 | 1,515 |
| `ir_construction.fold_form_truncate_call` | 1,787 | 78 | 1,865 |
| `ir_construction.fold_form_truncate_operator` | 2,085 | 56 | 2,141 |
| `ir_construction.fold_form_truncate_other` | 0 | 9 | 9 |
| `ir_construction.fold_form_truncate_return` | 262 | 10 | 272 |
| `ir_construction.fold_form_truncate_store` | 3,099 | 325 | 3,424 |
| `ir_construction.fold_form_zero_extend_aggregate` | 0 | 4 | 4 |
| `ir_construction.fold_form_zero_extend_call` | 114 | 1 | 115 |
| `ir_construction.fold_form_zero_extend_operator` | 1,259 | 18 | 1,277 |
| `ir_construction.fold_form_zero_extend_store` | 66 | 0 | 66 |
| `ir_construction.fold_late_binary_constant` | 38,775 | 3,020 | 41,795 |
| `ir_construction.fold_late_binary_left_identity` | 40 | 55 | 95 |
| `ir_construction.fold_late_binary_right_identity` | 190 | 239 | 429 |
| `ir_construction.fold_late_cast_constant` | 109,846 | 8,877 | 118,723 |
| `ir_construction.fold_late_cast_identity` | 1 | 7 | 8 |
| `ir_construction.fold_late_dce_constants` | 202,665 | 19,535 | 222,200 |
| `ir_construction.fold_late_replaced_roots` | 0 | 29 | 29 |
| `ir_construction.fold_late_unary_constant` | 729 | 725 | 1,454 |
| `ir_construction.fold_match_cascade_binary_constant` | 3,317 | 1,581 | 4,898 |
| `ir_construction.fold_match_cascade_binary_left_identity` | 1 | 31 | 32 |
| `ir_construction.fold_match_cascade_binary_right_identity` | 38 | 93 | 131 |
| `ir_construction.fold_match_cascade_cast_constant` | 1,078 | 1,161 | 2,239 |
| `ir_construction.fold_match_cascade_unary_constant` | 248 | 67 | 315 |
| `ir_construction.fold_match_clone_direct` | 218 | 0 | 218 |
| `ir_construction.fold_match_clone_post` | 38 | 1 | 39 |
| `ir_construction.fold_match_direct_binary_constant` | 35,392 | 1,118 | 36,510 |
| `ir_construction.fold_match_direct_binary_left_identity` | 39 | 1 | 40 |
| `ir_construction.fold_match_direct_binary_right_identity` | 152 | 138 | 290 |
| `ir_construction.fold_match_direct_cast_constant` | 108,757 | 7,590 | 116,347 |
| `ir_construction.fold_match_direct_cast_identity` | 1 | 7 | 8 |
| `ir_construction.fold_match_direct_unary_constant` | 474 | 654 | 1,128 |
| `ir_construction.fold_match_post_binary_constant` | 66 | 321 | 387 |
| `ir_construction.fold_match_post_binary_left_identity` | 0 | 23 | 23 |
| `ir_construction.fold_match_post_binary_right_identity` | 0 | 8 | 8 |
| `ir_construction.fold_match_post_cast_constant` | 11 | 126 | 137 |
| `ir_construction.fold_match_post_rule_changed` | 32 | 66 | 98 |
| `ir_construction.fold_match_post_unary_constant` | 7 | 4 | 11 |
| `ir_construction.fold_match_post_unfoldable_at_birth` | 52 | 416 | 468 |
| `ir_construction.fold_net_adjacent_dead_literals` | 143,278 | 9,475 | 152,753 |
| `ir_construction.fold_net_constant_rows` | 144,405 | 9,376 | 153,781 |
| `ir_construction.fold_net_dead_literals` | 179,942 | 10,662 | 190,604 |
| `ir_construction.fold_net_functions` | 6,389 | 2,596 | 8,985 |
| `ir_construction.fold_net_identity_rows` | 224 | 182 | 406 |
| `ir_construction.fold_net_literal_uses` | 179,942 | 10,671 | 190,613 |
| `ir_construction.fold_net_operand_slots` | 180,166 | 10,853 | 191,019 |
| `ir_construction.fold_net_rows` | 1,818,974 | 163,802 | 1,982,776 |
| `ir_construction.fold_net_scalar_rows` | 463,901 | 51,852 | 515,753 |
| `ir_construction.fold_net_ssa_uses` | 326,657 | 21,550 | 348,207 |
| `ir_construction.fold_size_rows_ge_4096_dead_literals` | 38,702 | 609 | 39,311 |
| `ir_construction.fold_size_rows_ge_4096_folds` | 31,245 | 597 | 31,842 |
| `ir_construction.fold_size_rows_ge_4096_functions` | 46 | 1 | 47 |
| `ir_construction.fold_size_rows_ge_4096_rows` | 367,843 | 7,421 | 375,264 |
| `ir_construction.fold_size_rows_lt_1024_dead_literals` | 59,345 | 2,878 | 62,223 |
| `ir_construction.fold_size_rows_lt_1024_folds` | 47,536 | 2,576 | 50,112 |
| `ir_construction.fold_size_rows_lt_1024_functions` | 1,282 | 113 | 1,395 |
| `ir_construction.fold_size_rows_lt_1024_rows` | 609,974 | 50,940 | 660,914 |
| `ir_construction.fold_size_rows_lt_256_dead_literals` | 32,009 | 4,255 | 36,264 |
| `ir_construction.fold_size_rows_lt_256_folds` | 25,475 | 3,707 | 29,182 |
| `ir_construction.fold_size_rows_lt_256_functions` | 2,500 | 430 | 2,930 |
| `ir_construction.fold_size_rows_lt_256_rows` | 337,821 | 52,124 | 389,945 |
| `ir_construction.fold_size_rows_lt_4096_dead_literals` | 42,728 | 952 | 43,680 |
| `ir_construction.fold_size_rows_lt_4096_folds` | 34,666 | 903 | 35,569 |
| `ir_construction.fold_size_rows_lt_4096_functions` | 237 | 11 | 248 |
| `ir_construction.fold_size_rows_lt_4096_rows` | 426,270 | 14,262 | 440,532 |
| `ir_construction.fold_size_rows_lt_64_dead_literals` | 7,158 | 1,968 | 9,126 |
| `ir_construction.fold_size_rows_lt_64_folds` | 5,707 | 1,775 | 7,482 |
| `ir_construction.fold_size_rows_lt_64_functions` | 2,324 | 2,041 | 4,365 |
| `ir_construction.fold_size_rows_lt_64_rows` | 77,066 | 39,055 | 116,121 |
| `ir_construction.fold_survival_reached` | 149,311 | 12,507 | 161,818 |
| `ir_construction.fold_survival_skipped` | 0 | 16 | 16 |
| `ir_construction.function_starts` | 6,389 | 2,596 | 8,985 |
| `ir_construction.global_appends` | 27,852 | 1,288 | 29,140 |
| `ir_construction.initial_block_slots` | 2,483,686 | 207,981 | 2,691,667 |
| `ir_construction.initial_instruction_slots` | 7,478,130 | 620,763 | 8,098,893 |
| `ir_construction.initial_value_slots` | 7,478,130 | 620,763 | 8,098,893 |
| `ir_construction.instruction_appends` | 2,065,403 | 170,414 | 2,235,817 |
| `ir_construction.instruction_grows` | 14 | 13 | 27 |
| `ir_construction.instruction_rows_copied` | 3,421 | 1,472 | 4,893 |
| `ir_construction.operand_slots_appended` | 2,040,845 | 155,950 | 2,196,795 |
| `ir_construction.parameters` | 19,546 | 4,037 | 23,583 |
| `ir_construction.place_atomic_load_retractions` | 11 | 207 | 218 |
| `ir_construction.place_load_retractions` | 32,022 | 1,726 | 33,748 |
| `ir_construction.preparation_calls` | 2 | 718 | 720 |
| `ir_construction.preparation_fast_functions` | 6,389 | 2,596 | 8,985 |
| `ir_construction.preparation_fast_input_validations` | 1 | 359 | 360 |
| `ir_construction.preparation_promotion_functions` | 6,389 | 2,596 | 8,985 |
| `ir_construction.preparation_publication_functions` | 12,778 | 5,192 | 17,970 |
| `ir_construction.program_starts` | 1 | 359 | 360 |
| `ir_construction.source_rows_cleared` | 6,842 | 2,944 | 9,786 |
| `ir_construction.source_rows_copied` | 3,421 | 1,472 | 4,893 |
| `ir_construction.ssa_cfg_target_visits` | 570,668 | 28,130 | 598,798 |
| `ir_construction.ssa_finish_calls` | 6,389 | 2,596 | 8,985 |
| `ir_construction.ssa_finish_slot_grows` | 182 | 71 | 253 |
| `ir_construction.ssa_finish_slot_probes` | 87,378 | 15,712 | 103,090 |
| `ir_construction.ssa_forward_loop_skips` | 125 | 22 | 147 |
| `ir_construction.ssa_forward_steps` | 3,754 | 2,082 | 5,836 |
| `ir_construction.ssa_initialization_work_visits` | 1,499,014 | 66,101 | 1,565,115 |
| `ir_construction.ssa_live_work_visits` | 14,328 | 1,536 | 15,864 |
| `ir_construction.ssa_pending_predecessor_visits` | 34,932 | 4,931 | 39,863 |
| `ir_construction.ssa_pending_visits` | 187,250 | 6,753 | 194,003 |
| `ir_construction.ssa_reachable_target_visits` | 284,738 | 14,034 | 298,772 |
| `ir_construction.ssa_read_retractions` | 4,201 | 367 | 4,568 |
| `ir_construction.ssa_remap_incoming_visits` | 28,744 | 3,144 | 31,888 |
| `ir_construction.ssa_remap_instruction_rows` | 2,006,985 | 138,541 | 2,145,526 |
| `ir_construction.ssa_remap_operand_slots` | 1,983,557 | 127,217 | 2,110,774 |
| `ir_construction.ssa_remap_value_rows` | 3,968,842 | 282,990 | 4,251,832 |
| `ir_construction.ssa_replacement_rows` | 1,984,421 | 141,495 | 2,125,916 |
| `ir_construction.ssa_simplify_block_visits` | 321,430 | 16,791 | 338,221 |
| `ir_construction.ssa_simplify_empty_block_visits` | 82,018 | 7,819 | 89,837 |
| `ir_construction.ssa_simplify_incoming_visits` | 36,282 | 5,080 | 41,362 |
| `ir_construction.ssa_simplify_parameter_visits` | 17,751 | 2,459 | 20,210 |
| `ir_construction.ssa_simplify_passes` | 3,624 | 550 | 4,174 |
| `ir_construction.ssa_slot_clear_bytes` | 17,849,856 | 1,810,944 | 19,660,800 |
| `ir_construction.ssa_slot_grow_visits` | 269,367 | 9,178 | 278,545 |
| `ir_construction.ssa_slot_grows` | 9,775 | 1,940 | 11,715 |
| `ir_construction.ssa_slot_inserts` | 277,604 | 16,409 | 294,013 |
| `ir_construction.ssa_slot_lookups` | 569,566 | 39,514 | 609,080 |
| `ir_construction.ssa_slot_probes` | 840,006 | 47,453 | 887,459 |
| `ir_construction.ssa_slot_rehash_probes` | 304,550 | 10,087 | 314,637 |
| `ir_construction.ssa_value_clear_bytes` | 67,141,569 | 4,738,968 | 71,880,537 |
| `ir_construction.ssa_value_scratch_bytes` | 113,732,149 | 7,886,848 | 121,618,997 |
| `ir_construction.symbol_appends` | 35,756 | 6,312 | 42,068 |
| `ir_construction.type_appends` | 76,275 | 19,139 | 95,414 |
| `ir_construction.validation_aliases` | 0 | 8 | 8 |
| `ir_construction.validation_blocks` | 214,964 | 14,594 | 229,558 |
| `ir_construction.validation_call_arguments` | 146,484 | 9,080 | 155,564 |
| `ir_construction.validation_call_checks` | 53,894 | 6,172 | 60,066 |
| `ir_construction.validation_calls` | 1 | 359 | 360 |
| `ir_construction.validation_conversion_checks` | 236,843 | 24,864 | 261,707 |
| `ir_construction.validation_functions` | 6,487 | 2,599 | 9,086 |
| `ir_construction.validation_global_relocation_pairs` | 9,946 | 46 | 9,992 |
| `ir_construction.validation_global_relocation_sort_rows` | 398 | 2 | 400 |
| `ir_construction.validation_global_relocation_sorts` | 4 | 1 | 5 |
| `ir_construction.validation_global_relocations` | 10,110 | 84 | 10,194 |
| `ir_construction.validation_globals` | 27,852 | 1,288 | 29,140 |
| `ir_construction.validation_incoming_values` | 31,018 | 3,192 | 34,210 |
| `ir_construction.validation_initializers` | 0 | 18 | 18 |
| `ir_construction.validation_instruction_provenance_checks` | 336,970 | 28,902 | 365,872 |
| `ir_construction.validation_instructions` | 2,114,982 | 167,747 | 2,282,729 |
| `ir_construction.validation_label_owner_probes` | 0 | 23 | 23 |
| `ir_construction.validation_label_owner_rows` | 0 | 17 | 17 |
| `ir_construction.validation_label_path_work` | 0 | 333 | 333 |
| `ir_construction.validation_label_scratch_bytes` | 0 | 2,435 | 2,435 |
| `ir_construction.validation_label_set_work` | 0 | 913 | 913 |
| `ir_construction.validation_operand_ids` | 2,087,978 | 153,181 | 2,241,159 |
| `ir_construction.validation_operation_checks` | 2,114,982 | 167,747 | 2,282,729 |
| `ir_construction.validation_ownership_blocks` | 214,964 | 14,594 | 229,558 |
| `ir_construction.validation_ownership_bytes_cleared` | 3,883,477 | 312,059 | 4,195,536 |
| `ir_construction.validation_ownership_function_scans` | 6,487 | 2,599 | 9,086 |
| `ir_construction.validation_ownership_functions` | 6,487 | 2,599 | 9,086 |
| `ir_construction.validation_ownership_instructions` | 2,114,982 | 167,747 | 2,282,729 |
| `ir_construction.validation_parameters` | 15,534 | 1,561 | 17,095 |
| `ir_construction.validation_result_relationships` | 1,752,961 | 142,751 | 1,895,712 |
| `ir_construction.validation_target_ids` | 304,415 | 15,961 | 320,376 |
| `ir_construction.validation_terminator_checks` | 2,114,982 | 167,747 | 2,282,729 |
| `ir_construction.validation_value_blocks` | 214,964 | 14,594 | 229,558 |
| `ir_construction.validation_value_parameters` | 15,534 | 1,561 | 17,095 |
| `ir_construction.validation_value_provenance_blocks` | 0 | 112 | 112 |
| `ir_construction.validation_value_provenance_checks` | 1,768,495 | 144,312 | 1,912,807 |
| `ir_construction.validation_values` | 1,768,495 | 144,312 | 1,912,807 |
| `ir_construction.value_appends` | 2,043,324 | 168,580 | 2,211,904 |
| `ir_construction.value_grows` | 26 | 31 | 57 |
| `ir_construction.value_rows_copied` | 12,450 | 3,026 | 15,476 |

### literal

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.literal.number_conversions` | 697,525 | 43,691 | 741,216 |
| `work.literal.string_count_bytes` | 0 | 19 | 19 |
| `work.literal.string_count_calls` | 0 | 3 | 3 |
| `work.literal.string_decode_bytes` | 12,507,143 | 16,754 | 12,523,897 |
| `work.literal.string_decode_calls` | 55,162 | 2,412 | 57,574 |

### lookup

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.lookup.symbol_intern_bytes_hashed` | 11,090,868 | 6,481,405 | 17,572,273 |
| `work.lookup.symbol_intern_probes` | 1,316,059 | 580,054 | 1,896,113 |
| `work.lookup.symbol_interns` | 1,070,812 | 537,559 | 1,608,371 |

### machine

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.machine.blocks` | 204,619 | 15,104 | 219,723 |
| `work.machine.debug_location_rows` | 227,233 | 0 | 227,233 |
| `work.machine.encoded_bytes` | 15,233,722 | 1,213,870 | 16,447,592 |
| `work.machine.functions_selected` | 6,389 | 2,597 | 8,986 |
| `work.machine.liveness_word_updates` | 1,121,085 | 77,991 | 1,199,076 |
| `work.machine.placement_edits` | 438,987 | 51,377 | 490,364 |
| `work.machine.rows` | 2,355,966 | 183,808 | 2,539,774 |
| `work.machine.virtual_registers` | 1,093,206 | 105,357 | 1,198,563 |

### output

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.output.link_image_bytes` | 42,822,120 | 0 | 42,822,120 |
| `work.output.object_bytes` | 0 | 3,097,704 | 3,097,704 |

### population

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.population.layout_rows_seeded` | 1,378,587 | 36,518 | 1,415,105 |
| `work.population.layout_scratch_bytes` | 203,577,418 | 2,062,557 | 205,639,975 |
| `work.population.layout_solves` | 174 | 507 | 681 |
| `work.population.member_promoted_searches` | 2,918 | 96 | 3,014 |
| `work.population.member_visited_bytes_cleared` | 0 | 11,383 | 11,383 |
| `work.population.types_compatible_calls` | 4,733 | 2,517 | 7,250 |
| `work.population.types_compatible_pairs` | 15,311 | 5,973 | 21,284 |

### rederive

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.rederive.body_scope_share_tokens` | 7,725,840 | 256,531 | 7,982,371 |
| `work.rederive.body_scope_stores` | 2,494,888 | 187,887 | 2,682,775 |
| `work.rederive.call_arity_candidates` | 124,314 | 11,711 | 136,025 |
| `work.rederive.call_arity_keywords_skipped` | 66,518 | 4,264 | 70,782 |
| `work.rederive.initializer_elements` | 608,248 | 6,440 | 614,688 |
| `work.rederive.initializer_literal_elements` | 542,060 | 4,171 | 546,231 |
| `work.rederive.initializer_walk_tokens` | 2,139,588 | 15,353 | 2,154,941 |
| `work.rederive.initializer_walks` | 6,282 | 897 | 7,179 |
| `work.rederive.lower_query_roots` | 220,505 | 10,348 | 230,853 |
| `work.rederive.lower_type_predictions` | 32,725 | 1,381 | 34,106 |
| `work.rederive.type_machine_runs` | 950,576 | 86,592 | 1,037,168 |
| `work.rederive.type_query_cache_hits` | 75,279 | 5,625 | 80,904 |
| `work.rederive.type_query_literal_answers` | 615,495 | 19,168 | 634,663 |
| `work.rederive.type_query_roots` | 1,417,007 | 70,764 | 1,487,771 |
| `work.rederive.type_query_uncached` | 726,233 | 45,971 | 772,204 |
| `work.rederive.type_query_uncached_tokens` | 4,679,459 | 226,537 | 4,905,996 |

### snapshot

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.snapshot.frame_bytes` | 679,704,800 | 62,148,400 | 741,853,200 |
| `work.snapshot.frame_checkpoint_saves` | 10,003 | 2,675 | 12,678 |
| `work.snapshot.frame_pushes` | 1,699,262 | 155,371 | 1,854,633 |
| `work.snapshot.query_checkpoint_bytes` | 424,120,072 | 26,847,064 | 450,967,136 |
| `work.snapshot.query_checkpoints` | 726,233 | 45,971 | 772,204 |
| `work.snapshot.rollbacks` | 49,749 | 4,871 | 54,620 |

### target

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.target.x86_base64_bytes_decoded` | 585,915 | 172,808,187 | 173,394,102 |
| `work.target.x86_form_selections` | 319 | 5,053 | 5,372 |
| `work.target.x86_gpr_table_reads` | 2,027,791 | 169,151 | 2,196,942 |
| `work.target.x86_metadata_encodes` | 4,798 | 235,000 | 239,798 |
| `work.target.x86_nul_distance_entries` | 2,117 | 433,669 | 435,786 |
| `work.target.x86_nul_distance_reads` | 220,643 | 26,997,300 | 27,217,943 |
| `work.target.x86_pool_bytes_copied` | 727,806 | 211,380,414 | 212,108,220 |
| `work.target.x86_records_assembled` | 38,331 | 13,127,172 | 13,165,503 |
| `work.target.x86_variable_memory_cells_built` | 629 | 92,791 | 93,420 |
| `work.target.x86_variable_memory_table_reads` | 93,791 | 3,294 | 97,085 |
| `work.target.x86_variable_memory_tables_built` | 9 | 3,231 | 3,240 |

# `candidate` against `base`

Plain artifacts byte-identical to the baseline: 360/360

| Family | Baseline Ir | Candidate Ir | Change | Baseline faults | Candidate faults | Change |
|---|---:|---:|---:|---:|---:|---:|
| selfhost | 26,376,042,187 | 26,375,944,287 | -0.00% | 271,997 | 271,974 | -0.01% |
| fixtures | 9,592,177,702 | 9,487,669,542 | -1.09% | 689,787 | 689,251 | -0.08% |
| **corpus** | 35,968,219,889 | 35,863,613,829 | -0.29% | 961,784 | 961,225 | -0.06% |

| Counter (corpus) | Baseline | Candidate | Change |
|---|---:|---:|---:|
| `work.phase.codegen.minor_faults` | 105,468 | 105,567 | +0.09% |
| `work.phase.lower.minor_faults` | 117,693 | 117,653 | -0.03% |
| `work.phase.object.minor_faults` | 25,589 | 25,609 | +0.08% |
| `work.phase.output.arena_bytes` | 83,302,980 | 83,304,057 | +0.00% |
| `work.phase.output.minor_faults` | 16,491 | 16,644 | +0.93% |
| `work.phase.parse.minor_faults` | 5,032 | 5,042 | +0.20% |
| `work.phase.prepare.minor_faults` | 8,510 | 8,513 | +0.04% |
| `work.phase.preprocess.arena_bytes` | 1,896,182,704 | 1,856,791,144 | -2.08% |
| `work.phase.preprocess.arena_calls` | 4,891,119 | 4,486,119 | -8.28% |
| `work.phase.preprocess.minor_faults` | 213,855 | 213,148 | -0.33% |
| `work.phase.semantic.minor_faults` | 80,003 | 79,793 | -0.26% |
| `work.phase.startup.minor_faults` | 154,805 | 154,783 | -0.01% |
| `work.phase.target_prewarm.minor_faults` | 386,510 | 386,639 | +0.03% |

