# Work ledger: baseline `base`

| Family | Units | Compiled | Identical ledger/plain | Callgrind units | Callgrind Ir | Preprocessed tokens | Plain minor faults |
|---|---:|---:|---:|---:|---:|---:|---:|
| selfhost | 1 | 1 | 1 | 1 | 26,416,655,443 | 4,373,064 | 271,729 |
| fixtures | 359 | 358 | 359 | 30 | 791,165,257 | 316,107 | 789,312 |
| **corpus** | 360 | 359 | 360 | 31 | 27,207,820,700 | 4,689,171 | 1,061,041 |

## Phase attribution (ledger build)

| Phase | selfhost faults | fixtures faults | corpus faults | corpus arena calls |
|---|---:|---:|---:|---:|
| startup | 425 | 151,794 | 152,219 | 7,966 |
| preprocess | 101,754 | 111,091 | 212,845 | 4,884,430 |
| parse | 4,386 | 637 | 5,023 | 27,563 |
| semantic | 56,213 | 23,733 | 79,946 | 1,673,487 |
| lower | 88,789 | 28,046 | 116,835 | 7,199,817 |
| prepare | 7,392 | 1,098 | 8,490 | 420,845 |
| target_prewarm | 1,393 | 500,369 | 501,762 | 0 |
| codegen | 20,723 | 71,342 | 92,065 | 1,769,049 |
| object | 24,702 | 865 | 25,567 | 357,771 |
| output | 15,467 | 968 | 16,435 | 56,360 |

## Mechanism ledger (exact counts, summed over units)

### ir_construction

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `ir_construction.after_ssa_block_rows` | 203,924 | 14,575 | 218,499 |
| `ir_construction.after_ssa_instruction_rows` | 2,028,346 | 167,847 | 2,196,193 |
| `ir_construction.after_ssa_value_rows` | 1,697,380 | 144,330 | 1,841,710 |
| `ir_construction.before_ssa_block_rows` | 203,924 | 14,575 | 218,499 |
| `ir_construction.before_ssa_instruction_rows` | 1,817,323 | 163,802 | 1,981,125 |
| `ir_construction.before_ssa_value_rows` | 1,990,325 | 164,210 | 2,154,535 |
| `ir_construction.block_appends` | 204,439 | 14,577 | 219,016 |
| `ir_construction.block_grows` | 0 | 3 | 3 |
| `ir_construction.block_rows_copied` | 0 | 39 | 39 |
| `ir_construction.body_tokens` | 2,430,947 | 187,213 | 2,618,160 |
| `ir_construction.c_statement_extent_peels` | 14 | 6 | 20 |
| `ir_construction.c_statement_extent_spans` | 23,305 | 1,541 | 24,846 |
| `ir_construction.cfg_builds` | 6,387 | 2,596 | 8,983 |
| `ir_construction.cfg_copy_sources` | 28,890 | 3,389 | 32,279 |
| `ir_construction.cfg_incoming_visits` | 28,886 | 3,186 | 32,072 |
| `ir_construction.cfg_parameter_visits` | 14,399 | 1,557 | 15,956 |
| `ir_construction.cfg_scratch_slots` | 817,756 | 58,308 | 876,064 |
| `ir_construction.cfg_target_visits` | 288,728 | 15,921 | 304,649 |
| `ir_construction.cfg_terminator_checks` | 2,030,651 | 161,731 | 2,192,382 |
| `ir_construction.cfg_unique_edges` | 288,728 | 15,921 | 304,649 |
| `ir_construction.commit_checks` | 2,060,729 | 170,412 | 2,231,141 |
| `ir_construction.commit_closes` | 203,924 | 14,575 | 218,499 |
| `ir_construction.commit_finalized_blocks` | 203,924 | 14,575 | 218,499 |
| `ir_construction.commit_insertions` | 211,023 | 4,045 | 215,068 |
| `ir_construction.commit_operand_checks` | 2,035,906 | 155,948 | 2,191,854 |
| `ir_construction.commit_result_binds` | 1,715,532 | 145,359 | 1,860,891 |
| `ir_construction.commit_retractions` | 31,920 | 1,933 | 33,853 |
| `ir_construction.commit_target_checks` | 288,025 | 15,945 | 303,970 |
| `ir_construction.commit_truncated_rows` | 463 | 632 | 1,095 |
| `ir_construction.debug_function_index_rows` | 7,593 | 0 | 7,593 |
| `ir_construction.debug_value_blocks` | 11,743 | 0 | 11,743 |
| `ir_construction.debug_value_local_visits` | 12 | 0 | 12 |
| `ir_construction.final_block_slots` | 2,482,043 | 208,020 | 2,690,063 |
| `ir_construction.final_instruction_slots` | 7,476,442 | 622,235 | 8,098,677 |
| `ir_construction.final_value_slots` | 7,476,274 | 621,923 | 8,098,197 |
| `ir_construction.fold_birth_avoidable_bytes` | 741,428 | 63,956 | 805,384 |
| `ir_construction.fold_birth_binary_constant` | 35,161 | 1,120 | 36,281 |
| `ir_construction.fold_birth_binary_left_identity` | 39 | 1 | 40 |
| `ir_construction.fold_birth_binary_right_identity` | 200 | 212 | 412 |
| `ir_construction.fold_birth_cast_constant` | 108,564 | 7,600 | 116,164 |
| `ir_construction.fold_birth_cast_identity` | 1 | 7 | 8 |
| `ir_construction.fold_birth_scalar_rows` | 464,092 | 52,171 | 516,263 |
| `ir_construction.fold_birth_unary_constant` | 472 | 656 | 1,128 |
| `ir_construction.fold_late_binary_constant` | 38,610 | 3,020 | 41,630 |
| `ir_construction.fold_late_binary_left_identity` | 40 | 55 | 95 |
| `ir_construction.fold_late_binary_right_identity` | 191 | 239 | 430 |
| `ir_construction.fold_late_cast_constant` | 109,793 | 8,877 | 118,670 |
| `ir_construction.fold_late_cast_identity` | 1 | 7 | 8 |
| `ir_construction.fold_late_replaced_roots` | 0 | 29 | 29 |
| `ir_construction.fold_late_unary_constant` | 727 | 725 | 1,452 |
| `ir_construction.function_starts` | 6,387 | 2,596 | 8,983 |
| `ir_construction.global_appends` | 27,848 | 1,288 | 29,136 |
| `ir_construction.initial_block_slots` | 2,482,043 | 207,981 | 2,690,024 |
| `ir_construction.initial_instruction_slots` | 7,473,021 | 620,763 | 8,093,784 |
| `ir_construction.initial_value_slots` | 7,473,021 | 620,763 | 8,093,784 |
| `ir_construction.instruction_appends` | 2,063,138 | 170,414 | 2,233,552 |
| `ir_construction.instruction_grows` | 14 | 13 | 27 |
| `ir_construction.instruction_rows_copied` | 3,421 | 1,472 | 4,893 |
| `ir_construction.operand_slots_appended` | 2,038,109 | 155,950 | 2,194,059 |
| `ir_construction.parameters` | 19,497 | 4,037 | 23,534 |
| `ir_construction.place_atomic_load_retractions` | 11 | 207 | 218 |
| `ir_construction.place_load_retractions` | 31,909 | 1,726 | 33,635 |
| `ir_construction.preparation_calls` | 2 | 718 | 720 |
| `ir_construction.preparation_fast_functions` | 6,387 | 2,596 | 8,983 |
| `ir_construction.preparation_fast_input_validations` | 1 | 359 | 360 |
| `ir_construction.preparation_promotion_functions` | 6,387 | 2,596 | 8,983 |
| `ir_construction.preparation_publication_functions` | 12,774 | 5,192 | 17,966 |
| `ir_construction.program_starts` | 1 | 359 | 360 |
| `ir_construction.source_rows_cleared` | 6,842 | 2,944 | 9,786 |
| `ir_construction.source_rows_copied` | 3,421 | 1,472 | 4,893 |
| `ir_construction.ssa_cfg_target_visits` | 570,032 | 28,130 | 598,162 |
| `ir_construction.ssa_finish_calls` | 6,387 | 2,596 | 8,983 |
| `ir_construction.ssa_finish_slot_grows` | 181 | 71 | 252 |
| `ir_construction.ssa_finish_slot_probes` | 87,705 | 15,774 | 103,479 |
| `ir_construction.ssa_forward_steps` | 3,953 | 2,104 | 6,057 |
| `ir_construction.ssa_initialization_work_visits` | 1,494,521 | 66,101 | 1,560,622 |
| `ir_construction.ssa_live_work_visits` | 14,231 | 1,536 | 15,767 |
| `ir_construction.ssa_pending_predecessor_visits` | 34,788 | 4,931 | 39,719 |
| `ir_construction.ssa_pending_visits` | 186,737 | 6,753 | 193,490 |
| `ir_construction.ssa_reachable_target_visits` | 284,420 | 14,034 | 298,454 |
| `ir_construction.ssa_read_retractions` | 4,189 | 367 | 4,556 |
| `ir_construction.ssa_remap_incoming_visits` | 28,550 | 3,144 | 31,694 |
| `ir_construction.ssa_remap_instruction_rows` | 2,004,778 | 138,541 | 2,143,319 |
| `ir_construction.ssa_remap_operand_slots` | 1,980,911 | 127,217 | 2,108,128 |
| `ir_construction.ssa_remap_value_rows` | 3,962,816 | 282,990 | 4,245,806 |
| `ir_construction.ssa_replacement_rows` | 1,981,408 | 141,495 | 2,122,903 |
| `ir_construction.ssa_simplify_block_visits` | 321,107 | 16,791 | 337,898 |
| `ir_construction.ssa_simplify_empty_block_visits` | 82,241 | 7,819 | 90,060 |
| `ir_construction.ssa_simplify_incoming_visits` | 36,138 | 5,080 | 41,218 |
| `ir_construction.ssa_simplify_parameter_visits` | 17,679 | 2,459 | 20,138 |
| `ir_construction.ssa_simplify_passes` | 3,612 | 550 | 4,162 |
| `ir_construction.ssa_slot_clear_bytes` | 17,793,792 | 1,810,944 | 19,604,736 |
| `ir_construction.ssa_slot_grow_visits` | 268,264 | 9,178 | 277,442 |
| `ir_construction.ssa_slot_grows` | 9,755 | 1,940 | 11,695 |
| `ir_construction.ssa_slot_inserts` | 276,916 | 16,431 | 293,347 |
| `ir_construction.ssa_slot_lookups` | 568,215 | 39,558 | 607,773 |
| `ir_construction.ssa_slot_probes` | 837,087 | 47,515 | 884,602 |
| `ir_construction.ssa_slot_rehash_probes` | 303,105 | 10,086 | 313,191 |
| `ir_construction.ssa_value_clear_bytes` | 67,037,031 | 4,738,968 | 71,775,999 |
| `ir_construction.ssa_value_scratch_bytes` | 113,545,763 | 7,886,848 | 121,432,611 |
| `ir_construction.symbol_appends` | 35,738 | 6,312 | 42,050 |
| `ir_construction.type_appends` | 76,227 | 19,138 | 95,365 |
| `ir_construction.validation_aliases` | 0 | 8 | 8 |
| `ir_construction.validation_blocks` | 215,024 | 14,594 | 229,618 |
| `ir_construction.validation_call_arguments` | 145,872 | 9,080 | 154,952 |
| `ir_construction.validation_call_checks` | 53,714 | 6,172 | 59,886 |
| `ir_construction.validation_calls` | 1 | 359 | 360 |
| `ir_construction.validation_conversion_checks` | 236,822 | 24,864 | 261,686 |
| `ir_construction.validation_functions` | 6,492 | 2,599 | 9,091 |
| `ir_construction.validation_global_relocation_pairs` | 9,918 | 46 | 9,964 |
| `ir_construction.validation_global_relocation_sort_rows` | 398 | 2 | 400 |
| `ir_construction.validation_global_relocation_sorts` | 4 | 1 | 5 |
| `ir_construction.validation_global_relocations` | 10,079 | 84 | 10,163 |
| `ir_construction.validation_globals` | 27,848 | 1,288 | 29,136 |
| `ir_construction.validation_incoming_values` | 30,832 | 3,192 | 34,024 |
| `ir_construction.validation_initializers` | 0 | 18 | 18 |
| `ir_construction.validation_instruction_provenance_checks` | 336,622 | 28,902 | 365,524 |
| `ir_construction.validation_instructions` | 2,113,691 | 167,747 | 2,281,438 |
| `ir_construction.validation_label_owner_probes` | 0 | 23 | 23 |
| `ir_construction.validation_label_owner_rows` | 0 | 17 | 17 |
| `ir_construction.validation_label_path_work` | 0 | 333 | 333 |
| `ir_construction.validation_label_scratch_bytes` | 0 | 2,435 | 2,435 |
| `ir_construction.validation_label_set_work` | 0 | 913 | 913 |
| `ir_construction.validation_operand_ids` | 2,086,048 | 153,181 | 2,239,229 |
| `ir_construction.validation_operation_checks` | 2,113,691 | 167,747 | 2,281,438 |
| `ir_construction.validation_ownership_blocks` | 215,024 | 14,594 | 229,618 |
| `ir_construction.validation_ownership_bytes_cleared` | 3,880,875 | 312,059 | 4,192,934 |
| `ir_construction.validation_ownership_function_scans` | 6,492 | 2,599 | 9,091 |
| `ir_construction.validation_ownership_functions` | 6,492 | 2,599 | 9,091 |
| `ir_construction.validation_ownership_instructions` | 2,113,691 | 167,747 | 2,281,438 |
| `ir_construction.validation_parameters` | 15,441 | 1,561 | 17,002 |
| `ir_construction.validation_result_relationships` | 1,751,743 | 142,751 | 1,894,494 |
| `ir_construction.validation_target_ids` | 304,215 | 15,961 | 320,176 |
| `ir_construction.validation_terminator_checks` | 2,113,691 | 167,747 | 2,281,438 |
| `ir_construction.validation_value_blocks` | 215,024 | 14,594 | 229,618 |
| `ir_construction.validation_value_parameters` | 15,441 | 1,561 | 17,002 |
| `ir_construction.validation_value_provenance_blocks` | 0 | 112 | 112 |
| `ir_construction.validation_value_provenance_checks` | 1,767,184 | 144,312 | 1,911,496 |
| `ir_construction.validation_values` | 1,767,184 | 144,312 | 1,911,496 |
| `ir_construction.value_appends` | 2,040,220 | 168,580 | 2,208,800 |
| `ir_construction.value_grows` | 26 | 31 | 57 |
| `ir_construction.value_rows_copied` | 12,450 | 3,026 | 15,476 |

### literal

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.literal.number_conversions` | 697,435 | 43,691 | 741,126 |
| `work.literal.string_count_bytes` | 0 | 19 | 19 |
| `work.literal.string_count_calls` | 0 | 3 | 3 |
| `work.literal.string_decode_bytes` | 12,509,645 | 16,754 | 12,526,399 |
| `work.literal.string_decode_calls` | 55,166 | 2,412 | 57,578 |

### lookup

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.lookup.symbol_intern_bytes_hashed` | 11,057,551 | 6,464,891 | 17,522,442 |
| `work.lookup.symbol_intern_probes` | 1,304,257 | 578,927 | 1,883,184 |
| `work.lookup.symbol_interns` | 1,066,357 | 536,482 | 1,602,839 |

### machine

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.machine.blocks` | 204,578 | 15,104 | 219,682 |
| `work.machine.debug_location_rows` | 225,161 | 0 | 225,161 |
| `work.machine.encoded_bytes` | 15,222,712 | 1,228,458 | 16,451,170 |
| `work.machine.functions_selected` | 6,387 | 2,597 | 8,984 |
| `work.machine.liveness_word_updates` | 1,190,487 | 83,504 | 1,273,991 |
| `work.machine.placement_edits` | 443,631 | 52,463 | 496,094 |
| `work.machine.rows` | 2,351,137 | 183,808 | 2,534,945 |
| `work.machine.virtual_registers` | 1,091,416 | 105,357 | 1,196,773 |

### output

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.output.link_image_bytes` | 42,725,248 | 0 | 42,725,248 |
| `work.output.object_bytes` | 0 | 3,111,728 | 3,111,728 |

### population

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.population.layout_rows_seeded` | 1,371,539 | 36,151 | 1,407,690 |
| `work.population.layout_scratch_bytes` | 200,594,633 | 2,019,046 | 202,613,679 |
| `work.population.layout_solves` | 172 | 507 | 679 |
| `work.population.member_promoted_searches` | 2,903 | 96 | 2,999 |
| `work.population.member_visited_bytes_cleared` | 0 | 11,383 | 11,383 |
| `work.population.types_compatible_calls` | 4,687 | 2,493 | 7,180 |
| `work.population.types_compatible_pairs` | 15,037 | 5,949 | 20,986 |

### rederive

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.rederive.body_scope_share_tokens` | 7,722,295 | 256,531 | 7,978,826 |
| `work.rederive.body_scope_stores` | 2,493,204 | 187,887 | 2,681,091 |
| `work.rederive.call_arity_candidates` | 124,177 | 11,711 | 135,888 |
| `work.rederive.call_arity_keywords_skipped` | 66,649 | 4,264 | 70,913 |
| `work.rederive.initializer_elements` | 608,053 | 6,440 | 614,493 |
| `work.rederive.initializer_literal_elements` | 542,047 | 4,171 | 546,218 |
| `work.rederive.initializer_walk_tokens` | 2,922,866 | 17,296 | 2,940,162 |
| `work.rederive.initializer_walks` | 6,423 | 975 | 7,398 |
| `work.rederive.lower_query_roots` | 221,147 | 10,371 | 231,518 |
| `work.rederive.lower_type_predictions` | 32,683 | 1,381 | 34,064 |
| `work.rederive.type_machine_runs` | 948,598 | 86,841 | 1,035,439 |
| `work.rederive.type_query_cache_hits` | 74,931 | 5,515 | 80,446 |
| `work.rederive.type_query_literal_answers` | 615,618 | 19,168 | 634,786 |
| `work.rederive.type_query_roots` | 1,415,313 | 70,903 | 1,486,216 |
| `work.rederive.type_query_uncached` | 724,764 | 46,220 | 770,984 |
| `work.rederive.type_query_uncached_tokens` | 4,667,588 | 229,023 | 4,896,611 |

### snapshot

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.snapshot.frame_bytes` | 678,269,600 | 62,303,600 | 740,573,200 |
| `work.snapshot.frame_checkpoint_saves` | 9,967 | 2,675 | 12,642 |
| `work.snapshot.frame_pushes` | 1,695,674 | 155,759 | 1,851,433 |
| `work.snapshot.query_checkpoint_bytes` | 411,665,952 | 26,252,960 | 437,918,912 |
| `work.snapshot.query_checkpoints` | 724,764 | 46,220 | 770,984 |
| `work.snapshot.rollbacks` | 50,192 | 5,536 | 55,728 |

### target

| Counter | selfhost | fixtures | corpus |
|---|---:|---:|---:|
| `work.target.x86_base64_bytes_decoded` | 754,267 | 233,246,555 | 234,000,822 |
| `work.target.x86_form_selections` | 312 | 5,047 | 5,359 |
| `work.target.x86_gpr_table_reads` | 2,017,396 | 166,082 | 2,183,478 |
| `work.target.x86_gpr_tables_built` | 59 | 5,021 | 5,080 |
| `work.target.x86_metadata_encodes` | 11,911 | 1,141,845 | 1,153,756 |
| `work.target.x86_nul_distance_entries` | 2,117 | 433,669 | 435,786 |
| `work.target.x86_nul_distance_reads` | 244,884 | 29,616,964 | 29,861,848 |
| `work.target.x86_pool_bytes_copied` | 1,726,254 | 619,725,186 | 621,451,440 |
| `work.target.x86_records_assembled` | 64,719 | 22,600,464 | 22,665,183 |
| `work.target.x86_variable_memory_cells_built` | 622 | 92,776 | 93,398 |
| `work.target.x86_variable_memory_table_reads` | 93,535 | 3,294 | 96,829 |
| `work.target.x86_variable_memory_tables_built` | 9 | 3,231 | 3,240 |

# `cand3` against `base`

Plain artifacts byte-identical to the baseline: 360/360

| Family | Baseline Ir | Candidate Ir | Change | Baseline faults | Candidate faults | Change |
|---|---:|---:|---:|---:|---:|---:|
| selfhost | 26,416,655,443 | 26,417,015,468 | +0.00% | 271,729 | 271,337 | -0.14% |
| fixtures | 791,165,257 | 772,096,279 | -2.41% | 789,312 | 690,953 | -12.46% |
| **corpus** | 27,207,820,700 | 27,189,111,747 | -0.07% | 1,061,041 | 962,290 | -9.31% |

| Counter (corpus) | Baseline | Candidate | Change |
|---|---:|---:|---:|
| `work.phase.codegen.minor_faults` | 92,065 | 103,124 | +12.01% |
| `work.phase.lower.minor_faults` | 116,835 | 116,739 | -0.08% |
| `work.phase.object.minor_faults` | 25,567 | 25,568 | +0.00% |
| `work.phase.output.arena_bytes` | 83,174,528 | 83,177,759 | +0.00% |
| `work.phase.output.minor_faults` | 16,435 | 16,414 | -0.13% |
| `work.phase.parse.minor_faults` | 5,023 | 5,034 | +0.22% |
| `work.phase.prepare.minor_faults` | 8,490 | 8,502 | +0.14% |
| `work.phase.preprocess.minor_faults` | 212,845 | 212,764 | -0.04% |
| `work.phase.semantic.minor_faults` | 79,946 | 79,976 | +0.04% |
| `work.phase.startup.arena_bytes` | 6,075,911 | 6,089,991 | +0.23% |
| `work.phase.startup.minor_faults` | 152,219 | 152,136 | -0.05% |
| `work.phase.target_prewarm.minor_faults` | 501,762 | 388,990 | -22.48% |
| `work.target.x86_base64_bytes_decoded` | 234,000,822 | 173,394,102 | -25.90% |
| `work.target.x86_pool_bytes_copied` | 621,451,440 | 212,108,220 | -65.87% |
| `work.target.x86_records_assembled` | 22,665,183 | 13,165,503 | -41.91% |

