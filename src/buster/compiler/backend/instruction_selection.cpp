#pragma once

#include <buster/compiler/backend/instruction_selection.h>
// #include <buster/x86_64_instructions.c>
// #include <buster/x86_64_llvm.c>
#include <buster/assertion.h>

#define X86_SELECTOR_MAX_RULE_COUNT 64

// BUSTER_GLOBAL_LOCAL bool x86_selector_initialized = false;
BUSTER_GLOBAL_LOCAL X86SelectorBucket x86_selector_buckets[X86_SELECTOR_BUCKET_COUNT] = { 0 };
BUSTER_GLOBAL_LOCAL X86SelectorRule x86_selector_rules[X86_SELECTOR_MAX_RULE_COUNT] = { 0 };
// BUSTER_GLOBAL_LOCAL X86SelectorRuleCost x86_selector_rule_costs[X86_SELECTOR_MAX_RULE_COUNT] = { 0 };
BUSTER_GLOBAL_LOCAL u32 x86_selector_rule_count = 0;
// BUSTER_GLOBAL_LOCAL u32 x86_selector_zero_gpr_form_index = 0xffffffffu;

BUSTER_GLOBAL_LOCAL u32 x86_selector_integer_type_width_bits(IrTypeId type_id)
{
    u32 result = 0;
    switch (type_id)
    {
        break; case IR_TYPE_I1: result = 1;
        break; case IR_TYPE_I8: result = 8;
        break; case IR_TYPE_I16: result = 16;
        break; case IR_TYPE_I32: result = 32;
        break; case IR_TYPE_I64: result = 64;
        break; default:
        {
        }
    }
    return result;
}

// static IrTypeId x86_selector_integer_types[] = {
//     IR_TYPE_I8,
//     IR_TYPE_I16,
//     IR_TYPE_I32,
//     IR_TYPE_I64,
// };

// BUSTER_GLOBAL_LOCAL bool x86_selector_form_is_base_xor_zero_candidate(X86InstructionForm* form)
// {
//     bool result = form->iclass == X86_ICLASS_XOR &&
//                   form->extension == X86_EXTENSION_BASE &&
//                   form->prefix_type == X86_PREFIX_TYPE_NONE &&
//                   form->operand_count == 2 &&
//                   form->has_modrm &&
//                   form->opcode_byte_count == 1 &&
//                   form->opcode_bytes[0] == 0x31 &&
//                   form->opcode_masks[0] == 0xff &&
//                   form->operand_count == 2;
//     return result;
// }

// BUSTER_GLOBAL_LOCAL bool x86_selector_form_is_base_mov_immediate_candidate(X86InstructionForm* form)
// {
//     bool result = form->iclass == X86_ICLASS_MOV &&
//                   form->extension == X86_EXTENSION_BASE &&
//                   form->prefix_type == X86_PREFIX_TYPE_NONE &&
//                   !form->has_modrm &&
//                   form->opcode_byte_count == 1 &&
//                   form->opcode_bytes[0] == 0xb8 &&
//                   form->opcode_masks[0] == 0xf8 &&
//                   form->operand_count == 2;
//     return result;
// }

// BUSTER_GLOBAL_LOCAL bool x86_selector_form_is_base_mov_register_candidate(X86InstructionForm* form)
// {
//     bool result = form->iclass == X86_ICLASS_MOV &&
//                   form->iform == X86_IFORM_MOV_GPRV_GPRV_89 &&
//                   form->extension == X86_EXTENSION_BASE &&
//                   form->operand_count == 2 &&
//                   form->has_modrm;
//     return result;
// }

// BUSTER_GLOBAL_LOCAL bool x86_selector_form_is_base_add_register_candidate(X86InstructionForm* form)
// {
//     bool result = form->iclass == X86_ICLASS_ADD &&
//                   form->extension == X86_EXTENSION_BASE &&
//                   form->prefix_type == X86_PREFIX_TYPE_NONE &&
//                   form->operand_count == 2 &&
//                   form->has_modrm &&
//                   form->opcode_byte_count == 1 &&
//                   form->opcode_bytes[0] == 0x01 &&
//                   form->opcode_masks[0] == 0xff &&
//                   form->operand_count == 2;
//     return result;
// }

// BUSTER_GLOBAL_LOCAL bool x86_selector_form_is_base_add_immediate_candidate(X86InstructionForm* form)
// {
//     bool result = form->iclass == X86_ICLASS_ADD &&
//                   form->extension == X86_EXTENSION_BASE &&
//                   form->prefix_type == X86_PREFIX_TYPE_NONE &&
//                   form->operand_count == 2 &&
//                   form->has_modrm &&
//                   form->opcode_byte_count == 1 &&
//                   (form->opcode_bytes[0] == 0x81 || form->opcode_bytes[0] == 0x83) &&
//                   form->modrm_reg_value == 0 &&
//                   form->operand_count == 2;
//     return result;
// }

BUSTER_GLOBAL_LOCAL void x86_selector_push_rule(X86SelectorBucketId bucket_id, IrTypeId type_id, X86SelectorRule rule)
{
    BUSTER_CHECK(x86_selector_rule_count < X86_SELECTOR_MAX_RULE_COUNT);
    X86SelectorBucket* bucket = &x86_selector_buckets[bucket_id];
    if (bucket->rule_count == 0)
    {
        bucket->id = bucket_id;
        bucket->type_id = type_id;
        bucket->rule_start = x86_selector_rule_count;
    }
    else if (bucket->type_id != type_id)
    {
        bucket->type_id = IR_TYPE_VOID;
    }
    x86_selector_rules[x86_selector_rule_count] = rule;
    x86_selector_rule_count += 1;
    bucket->rule_count += 1;
}

BUSTER_GLOBAL_LOCAL bool x86_selector_rule_source_matches(X86SelectorRule rule, X86SelectorSourceKind source_kind)
{
    bool result = false;
    switch (source_kind)
    {
        break; case X86_SELECTOR_SOURCE_KIND_REGISTER:
        {
            result = (rule.flags & (X86_SELECTOR_RULE_FLAG_IMMEDIATE_SOURCE | X86_SELECTOR_RULE_FLAG_MEMORY_SOURCE)) == 0;
        }
        break; case X86_SELECTOR_SOURCE_KIND_IMMEDIATE:
        {
            result = (rule.flags & X86_SELECTOR_RULE_FLAG_IMMEDIATE_SOURCE) != 0;
        }
        break; case X86_SELECTOR_SOURCE_KIND_MEMORY:
        {
            result = (rule.flags & X86_SELECTOR_RULE_FLAG_MEMORY_SOURCE) != 0;
        }
    }
    return result;
}

// BUSTER_GLOBAL_LOCAL bool x86_selector_select_integer_bucket(X86SelectorBucketId bucket_id, IrTypeId type_id, X86SelectorSourceKind source_kind, X86SelectorSelection* out_selection)
// {
//     bool result = out_selection != 0;
//     if (result)
//     {
//         x86_selector_initialize();
//         *out_selection = (X86SelectorSelection){ 0 };
//
//         X86SelectorBucket bucket = x86_selector_bucket(bucket_id);
//         u32 best_total_cost = 0xffffffffu;
//         for (u32 rule_i = bucket.rule_start; rule_i < bucket.rule_start + bucket.rule_count; rule_i += 1)
//         {
//             X86SelectorRule rule = x86_selector_rule(rule_i);
//             X86SelectorRuleCost cost = x86_selector_rule_cost(rule_i);
//             if (rule.type_id == type_id && x86_selector_rule_source_matches(rule, source_kind) && cost.total_cost < best_total_cost)
//             {
//                 best_total_cost = cost.total_cost;
//                 *out_selection = (X86SelectorSelection){
//                     .form_index = rule.form_index,
//                     .lowering_id = rule.lowering_id,
//                     .immediate_width_bits = rule.width_bits,
//                     .is_valid = true,
//                 };
//             }
//         }
//
//         result = out_selection->is_valid;
//     }
//     return result;
// }

// BUSTER_GLOBAL_LOCAL void x86_selector_recompute_rule_costs()
// {
//     for (u32 rule_i = 0; rule_i < x86_selector_rule_count; rule_i += 1)
//     {
//         X86SelectorRule rule = x86_selector_rules[rule_i];
//         X86SelectorLlvmLoweringCost llvm_cost = { 0 };
//         u32 latency_cost = 0;
//         u32 throughput_cost = 0;
//         u32 total_cost = rule.cost;
//
//         if ((u32)rule.lowering_id < X86_SELECTOR_LOWERING_COUNT)
//         {
//             llvm_cost = x86_selector_llvm_lowering_costs[rule.lowering_id];
//         }
//
//         if (llvm_cost.has_metrics)
//         {
//             latency_cost = llvm_cost.latency;
//             throughput_cost = llvm_cost.micro_op_count;
//             total_cost += latency_cost + throughput_cost;
//         }
//
//         x86_selector_rule_costs[rule_i] = (X86SelectorRuleCost){
//             .code_size_cost = rule.cost,
//             .latency_cost = latency_cost,
//             .throughput_cost = throughput_cost,
//             .total_cost = total_cost,
//         };
//     }
// }

// BUSTER_IMPL void x86_selector_initialize()
// {
//     if (!x86_selector_initialized)
//     {
//         memset(x86_selector_buckets, 0, sizeof(x86_selector_buckets));
//         memset(x86_selector_rules, 0, sizeof(x86_selector_rules));
//         memset(x86_selector_rule_costs, 0, sizeof(x86_selector_rule_costs));
//         x86_selector_rule_count = 0;
//         x86_selector_zero_gpr_form_index = 0xffffffffu;
//
//         X86IclassFormRange mov_range = x86_iclass_form_ranges[X86_ICLASS_MOV];
//         X86IclassFormRange add_range = x86_iclass_form_ranges[X86_ICLASS_ADD];
//         X86IclassFormRange xor_range = x86_iclass_form_ranges[X86_ICLASS_XOR];
//
//         for (u32 form_i = mov_range.start; form_i < mov_range.start + mov_range.count; form_i += 1)
//         {
//             X86InstructionForm* form = &x86_instruction_forms[form_i];
//             if (x86_selector_form_is_base_mov_immediate_candidate(form))
//             {
//                 for (u32 type_i = 0; type_i < BUSTER_ARRAY_LENGTH(x86_selector_integer_types); type_i += 1)
//                 {
//                     IrTypeId type_id = x86_selector_integer_types[type_i];
//                     x86_selector_push_rule(
//                         X86_SELECTOR_BUCKET_RETURN_CONSTANT_INTEGER,
//                         type_id,
//                         (X86SelectorRule){
//                             .form_index = form_i,
//                             .lowering_id = X86_SELECTOR_LOWERING_MOV_IMMEDIATE_GPR,
//                             .type_id = type_id,
//                             .width_bits = x86_selector_integer_type_width_bits(type_id),
//                             .flags = X86_SELECTOR_RULE_FLAG_IMMEDIATE_SOURCE,
//                             .operand_count = (u8)form->operand_count,
//                             .cost = type_id == IR_TYPE_I64 ? 2 : 1,
//                         });
//                 }
//             }
//
//             if (x86_selector_form_is_base_mov_register_candidate(form))
//             {
//                 for (u32 type_i = 0; type_i < BUSTER_ARRAY_LENGTH(x86_selector_integer_types); type_i += 1)
//                 {
//                     IrTypeId type_id = x86_selector_integer_types[type_i];
//                     x86_selector_push_rule(
//                         X86_SELECTOR_BUCKET_COPY_INTEGER,
//                         type_id,
//                         (X86SelectorRule){
//                             .form_index = form_i,
//                             .lowering_id = X86_SELECTOR_LOWERING_MOV_REGISTER_GPR,
//                             .type_id = type_id,
//                             .width_bits = x86_selector_integer_type_width_bits(type_id),
//                             .flags = X86_SELECTOR_RULE_FLAG_NONE,
//                             .operand_count = (u8)form->operand_count,
//                             .cost = 1,
//                         });
//                 }
//             }
//         }
//
//         for (u32 form_i = add_range.start; form_i < add_range.start + add_range.count; form_i += 1)
//         {
//             X86InstructionForm* form = &x86_instruction_forms[form_i];
//             if (x86_selector_form_is_base_add_register_candidate(form))
//             {
//                 for (u32 type_i = 0; type_i < BUSTER_ARRAY_LENGTH(x86_selector_integer_types); type_i += 1)
//                 {
//                     IrTypeId type_id = x86_selector_integer_types[type_i];
//                     x86_selector_push_rule(
//                         X86_SELECTOR_BUCKET_ADD_INTEGER,
//                         type_id,
//                         (X86SelectorRule){
//                             .form_index = form_i,
//                             .lowering_id = X86_SELECTOR_LOWERING_ADD_REGISTER_GPR,
//                             .type_id = type_id,
//                             .width_bits = x86_selector_integer_type_width_bits(type_id),
//                             .flags = X86_SELECTOR_RULE_FLAG_COMMUTATIVE,
//                             .operand_count = (u8)form->operand_count,
//                             .cost = 1,
//                         });
//                 }
//             }
//
//             if (x86_selector_form_is_base_add_immediate_candidate(form))
//             {
//                 for (u32 type_i = 0; type_i < BUSTER_ARRAY_LENGTH(x86_selector_integer_types); type_i += 1)
//                 {
//                     IrTypeId type_id = x86_selector_integer_types[type_i];
//                     x86_selector_push_rule(
//                         X86_SELECTOR_BUCKET_ADD_INTEGER,
//                         type_id,
//                         (X86SelectorRule){
//                             .form_index = form_i,
//                             .lowering_id = X86_SELECTOR_LOWERING_ADD_IMMEDIATE_GPR,
//                             .type_id = type_id,
//                             .width_bits = x86_selector_integer_type_width_bits(type_id),
//                             .flags = X86_SELECTOR_RULE_FLAG_IMMEDIATE_SOURCE,
//                             .operand_count = (u8)form->operand_count,
//                             .cost = form->opcode_bytes[0] == 0x83 ? 1 : 2,
//                         });
//                 }
//             }
//         }
//
//         for (u32 form_i = xor_range.start; form_i < xor_range.start + xor_range.count; form_i += 1)
//         {
//             X86InstructionForm* form = &x86_instruction_forms[form_i];
//             if (x86_selector_form_is_base_xor_zero_candidate(form))
//             {
//                 if (x86_selector_zero_gpr_form_index == 0xffffffffu ||
//                     form->iform == X86_IFORM_XOR_GPRV_GPRV_31)
//                 {
//                     x86_selector_zero_gpr_form_index = form_i;
//                 }
//
//                 for (u32 type_i = 0; type_i < BUSTER_ARRAY_LENGTH(x86_selector_integer_types); type_i += 1)
//                 {
//                     IrTypeId type_id = x86_selector_integer_types[type_i];
//                     x86_selector_push_rule(
//                         X86_SELECTOR_BUCKET_RETURN_CONSTANT_INTEGER,
//                         type_id,
//                         (X86SelectorRule){
//                             .form_index = form_i,
//                             .lowering_id = X86_SELECTOR_LOWERING_XOR_ZERO_GPR32,
//                             .type_id = type_id,
//                             .width_bits = type_id == IR_TYPE_I64 ? 32 : x86_selector_integer_type_width_bits(type_id),
//                             .flags = X86_SELECTOR_RULE_FLAG_COMMUTATIVE,
//                             .operand_count = (u8)form->operand_count,
//                             .cost = 0,
//                         });
//
//                     x86_selector_push_rule(
//                         X86_SELECTOR_BUCKET_XOR_INTEGER,
//                         type_id,
//                         (X86SelectorRule){
//                             .form_index = form_i,
//                             .lowering_id = X86_SELECTOR_LOWERING_XOR_ZERO_GPR32,
//                             .type_id = type_id,
//                             .width_bits = type_id == IR_TYPE_I64 ? 32 : x86_selector_integer_type_width_bits(type_id),
//                             .flags = X86_SELECTOR_RULE_FLAG_COMMUTATIVE,
//                             .operand_count = (u8)form->operand_count,
//                             .cost = 1,
//                         });
//                 }
//             }
//         }
//
//         x86_selector_recompute_rule_costs();
//         x86_selector_initialized = true;
//     }
// }

// BUSTER_IMPL X86SelectorBucket x86_selector_bucket(X86SelectorBucketId bucket_id)
// {
//     x86_selector_initialize();
//     return x86_selector_buckets[bucket_id];
// }

// BUSTER_IMPL X86SelectorRule x86_selector_rule(u32 rule_index)
// {
//     x86_selector_initialize();
//     BUSTER_CHECK(rule_index < x86_selector_rule_count);
//     return x86_selector_rules[rule_index];
// }

// BUSTER_IMPL X86SelectorRuleCost x86_selector_rule_cost(u32 rule_index)
// {
//     x86_selector_initialize();
//     BUSTER_CHECK(rule_index < x86_selector_rule_count);
//     return x86_selector_rule_costs[rule_index];
// }

// BUSTER_IMPL bool x86_selector_select_return_constant_integer(IrTypeId type_id, u64 constant_integer, X86SelectorSelection* out_selection)
// {
//     bool result = out_selection != 0;
//     if (result)
//     {
//         x86_selector_initialize();
//         *out_selection = (X86SelectorSelection){ 0 };
//
//         if (constant_integer == 0 && x86_selector_zero_gpr_form_index != 0xffffffffu)
//         {
//             *out_selection = (X86SelectorSelection){
//                 .form_index = x86_selector_zero_gpr_form_index,
//                 .lowering_id = X86_SELECTOR_LOWERING_XOR_ZERO_GPR32,
//                 .immediate_width_bits = 0,
//                 .is_valid = true,
//             };
//         }
//
//         X86SelectorBucket bucket = x86_selector_bucket(X86_SELECTOR_BUCKET_RETURN_CONSTANT_INTEGER);
//         u32 best_total_cost = 0xffffffffu;
//         for (u32 rule_i = bucket.rule_start; rule_i < bucket.rule_start + bucket.rule_count; rule_i += 1)
//         {
//             X86SelectorRule rule = x86_selector_rule(rule_i);
//             X86SelectorRuleCost cost = x86_selector_rule_cost(rule_i);
//             if (rule.type_id == type_id && constant_integer == 0 && rule.lowering_id == X86_SELECTOR_LOWERING_XOR_ZERO_GPR32)
//             {
//                 if (cost.total_cost < best_total_cost)
//                 {
//                     best_total_cost = cost.total_cost;
//                     *out_selection = (X86SelectorSelection){
//                         .form_index = rule.form_index,
//                         .lowering_id = rule.lowering_id,
//                         .immediate_width_bits = 0,
//                         .is_valid = true,
//                     };
//                 }
//             }
//
//             if (rule.type_id == type_id && constant_integer != 0 && rule.lowering_id == X86_SELECTOR_LOWERING_MOV_IMMEDIATE_GPR)
//             {
//                 if (cost.total_cost < best_total_cost)
//                 {
//                     best_total_cost = cost.total_cost;
//                     *out_selection = (X86SelectorSelection){
//                         .form_index = rule.form_index,
//                         .lowering_id = rule.lowering_id,
//                         .immediate_width_bits = rule.width_bits,
//                         .is_valid = true,
//                     };
//                 }
//             }
//         }
//
//         result = out_selection->is_valid;
//     }
//
//     return result;
// }

// BUSTER_IMPL bool x86_selector_select_copy_integer(IrTypeId type_id, X86SelectorSelection* out_selection)
// {
//     return x86_selector_select_integer_bucket(X86_SELECTOR_BUCKET_COPY_INTEGER, type_id, X86_SELECTOR_SOURCE_KIND_REGISTER, out_selection);
// }
//
// BUSTER_IMPL bool x86_selector_select_add_integer(IrTypeId type_id, X86SelectorSourceKind source_kind, X86SelectorSelection* out_selection)
// {
//     return x86_selector_select_integer_bucket(X86_SELECTOR_BUCKET_ADD_INTEGER, type_id, source_kind, out_selection);
// }
//
// BUSTER_IMPL bool x86_selector_select_xor_integer(IrTypeId type_id, X86SelectorSourceKind source_kind, X86SelectorSelection* out_selection)
// {
//     return x86_selector_select_integer_bucket(X86_SELECTOR_BUCKET_XOR_INTEGER, type_id, source_kind, out_selection);
// }

#if BUSTER_INCLUDE_TESTS
// BUSTER_IMPL UnitTestResult instruction_selection_tests(UnitTestArguments* arguments)
// {
//     BUSTER_UNUSED(arguments);
//
//     UnitTestResult result = { 0 };
//     x86_selector_initialize();
//
//     X86SelectorBucket copy_bucket = x86_selector_bucket(X86_SELECTOR_BUCKET_COPY_INTEGER);
//     bool has_copy_rule = copy_bucket.rule_count != 0;
//     result.succeeded_test_count += has_copy_rule;
//     result.test_count += 1;
//     if (!has_copy_rule)
//     {
//         BUSTER_TEST_ERROR(S8("copy bucket should not be empty"), 0);
//     }
//
//     X86SelectorSelection selection = { 0 };
//     bool selected = x86_selector_select_return_constant_integer(IR_TYPE_I32, 0, &selection);
//     bool selected_zero = selected && selection.is_valid && selection.lowering_id == X86_SELECTOR_LOWERING_XOR_ZERO_GPR32;
//     result.succeeded_test_count += selected_zero;
//     result.test_count += 1;
//     if (!selected_zero)
//     {
//         BUSTER_TEST_ERROR(S8("return 0 should select xor zero"), 0);
//     }
//
//     selection = (X86SelectorSelection){ 0 };
//     selected = x86_selector_select_return_constant_integer(IR_TYPE_I64, 7, &selection);
//     bool selected_immediate = selected && selection.is_valid &&
//                               selection.lowering_id == X86_SELECTOR_LOWERING_MOV_IMMEDIATE_GPR &&
//                               selection.immediate_width_bits == 64;
//     result.succeeded_test_count += selected_immediate;
//     result.test_count += 1;
//     if (!selected_immediate)
//     {
//         BUSTER_TEST_ERROR(S8("return immediate should select mov immediate"), 0);
//     }
//
//     selection = (X86SelectorSelection){ 0 };
//     selected = x86_selector_select_copy_integer(IR_TYPE_I16, &selection);
//     bool selected_copy = selected && selection.is_valid && selection.lowering_id == X86_SELECTOR_LOWERING_MOV_REGISTER_GPR;
//     result.succeeded_test_count += selected_copy;
//     result.test_count += 1;
//     if (!selected_copy)
//     {
//         BUSTER_TEST_ERROR(S8("copy should select mov register"), 0);
//     }
//
//     selection = (X86SelectorSelection){ 0 };
//     selected = x86_selector_select_add_integer(IR_TYPE_I64, X86_SELECTOR_SOURCE_KIND_IMMEDIATE, &selection);
//     bool selected_add_immediate = selected && selection.is_valid && selection.lowering_id == X86_SELECTOR_LOWERING_ADD_IMMEDIATE_GPR;
//     result.succeeded_test_count += selected_add_immediate;
//     result.test_count += 1;
//     if (!selected_add_immediate)
//     {
//         BUSTER_TEST_ERROR(S8("add immediate should select add immediate"), 0);
//     }
//
//     if (selected_add_immediate)
//     {
//         X86SelectorBucket add_bucket = x86_selector_bucket(X86_SELECTOR_BUCKET_ADD_INTEGER);
//         u32 selected_rule_index = 0xffffffffu;
//         for (u32 rule_i = add_bucket.rule_start; rule_i < add_bucket.rule_start + add_bucket.rule_count; rule_i += 1)
//         {
//             X86SelectorRule rule = x86_selector_rule(rule_i);
//             if (rule.form_index == selection.form_index && rule.width_bits == selection.immediate_width_bits)
//             {
//                 selected_rule_index = rule_i;
//                 break;
//             }
//         }
//
//         bool found_costed_rule = selected_rule_index != 0xffffffffu;
//         if (found_costed_rule)
//         {
//             X86SelectorRuleCost cost = x86_selector_rule_cost(selected_rule_index);
//             found_costed_rule = cost.total_cost >= cost.code_size_cost;
//         }
//         result.succeeded_test_count += found_costed_rule;
//         result.test_count += 1;
//         if (!found_costed_rule)
//         {
//             BUSTER_TEST_ERROR(S8("selected add rule should have a cost entry"), 0);
//         }
//     }
//     else
//     {
//         result.test_count += 1;
//     }
//
//     selection = (X86SelectorSelection){ 0 };
//     selected = x86_selector_select_xor_integer(IR_TYPE_I32, X86_SELECTOR_SOURCE_KIND_REGISTER, &selection);
//     bool selected_xor = selected && selection.is_valid && selection.lowering_id == X86_SELECTOR_LOWERING_XOR_ZERO_GPR32;
//     result.succeeded_test_count += selected_xor;
//     result.test_count += 1;
//     if (!selected_xor)
//     {
//         BUSTER_TEST_ERROR(S8("xor should select xor rule"), 0);
//     }
//
//     return result;
// }
#endif
