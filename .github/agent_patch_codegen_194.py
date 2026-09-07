from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
codegen_path = root / "src/buster/lib/compiler/codegen/codegen.c"
test_path = root / "tests/basic_c_i128_float.c"
text = codegen_path.read_text(encoding="utf-8")


def replace_once(source: str, old: str, new: str, label: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return source.replace(old, new, 1)


text = replace_once(
    text,
    "#define CODEGEN_X64_X87_SCRATCH_TRUNCATE_OFFSET 26\n",
    "#define CODEGEN_X64_X87_SCRATCH_TRUNCATE_OFFSET 26\n"
    "// i128 conversions additionally need one f80 quotient/remainder image\n"
    "// and an outer control-word pair.  The inner pair above remains free for\n"
    "// the unsigned-u64 truncation helper used twice by float-to-i128.\n"
    "#define CODEGEN_X64_X87_I128_SCRATCH_SIZE 64\n"
    "#define CODEGEN_X64_X87_I128_TEMP_OFFSET 32\n"
    "#define CODEGEN_X64_X87_I128_CONTROL_OFFSET 48\n"
    "#define CODEGEN_X64_X87_I128_ACTIVE_CONTROL_OFFSET 50\n"
    "#define CODEGEN_X64_X87_CONTROL_PRECISION_CLEAR UINT32_C(0xfffffcff)\n"
    "#define CODEGEN_X64_X87_CONTROL_PRECISION_DOUBLE UINT32_C(0x0200)\n"
    "#define CODEGEN_X64_X87_CONTROL_PRECISION_EXTENDED UINT32_C(0x0300)\n",
    "i128 x87 scratch constants",
)

helpers = r'''
// Replace only the x87 precision-control field, preserving the caller's
// rounding mode and exception masks.  Saving into a second control-word pair
// lets float-to-i128 nest the existing truncate-to-u64 helper without losing
// the control word that was active on entry.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_x87_i128_precision(CodegenBuffer* buffer, s32 scratch_displacement,
                                                                  u16 width, bool save)
{
    u32 precision = width == 32   ? 0
                    : width == 64 ? CODEGEN_X64_X87_CONTROL_PRECISION_DOUBLE
                    : width == 80 ? CODEGEN_X64_X87_CONTROL_PRECISION_EXTENDED
                                  : UINT32_MAX;
    if (precision == UINT32_MAX)
    {
        return false;
    }
    BusterX86MetadataPhysicalOperand saved_control = codegen_canonical_x64_metadata_memory(
        X64_REGISTER_RBP, 16, scratch_displacement + CODEGEN_X64_X87_I128_CONTROL_OFFSET);
    BusterX86MetadataPhysicalOperand active_control = codegen_canonical_x64_metadata_memory(
        X64_REGISTER_RBP, 16, scratch_displacement + CODEGEN_X64_X87_I128_ACTIVE_CONTROL_OFFSET);
    BusterX86MetadataPhysicalOperand eax = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 32);
    BusterX86MetadataPhysicalOperand ax = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 16);
    BusterX86MetadataPhysicalOperand read_control[2] = {eax, saved_control};
    BusterX86MetadataPhysicalOperand clear_precision[2] = {
        eax, codegen_canonical_x64_metadata_unsigned_immediate(CODEGEN_X64_X87_CONTROL_PRECISION_CLEAR, 32)};
    BusterX86MetadataPhysicalOperand set_precision[2] = {
        eax, codegen_canonical_x64_metadata_unsigned_immediate(precision, 32)};
    BusterX86MetadataPhysicalOperand write_control[2] = {active_control, ax};
    bool result = !save || codegen_canonical_x64_x87_features(buffer, S8("FNSTCW"), &saved_control, 1);
    result = result && codegen_canonical_x64_metadata_emit(buffer, S8("MOVZX"), read_control, BUSTER_ARRAY_LENGTH(read_control)) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("AND"), clear_precision, BUSTER_ARRAY_LENGTH(clear_precision));
    if (result && precision)
    {
        result = codegen_canonical_x64_metadata_emit(buffer, S8("OR"), set_precision, BUSTER_ARRAY_LENGTH(set_precision));
    }
    return result && codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), write_control, BUSTER_ARRAY_LENGTH(write_control)) &&
           codegen_canonical_x64_x87_features(buffer, S8("FLDCW"), &active_control, 1);
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_x87_i128_precision_end(CodegenBuffer* buffer, s32 scratch_displacement)
{
    BusterX86MetadataPhysicalOperand saved_control = codegen_canonical_x64_metadata_memory(
        X64_REGISTER_RBP, 16, scratch_displacement + CODEGEN_X64_X87_I128_CONTROL_OFFSET);
    return codegen_canonical_x64_x87_features(buffer, S8("FLDCW"), &saved_control, 1);
}

// Push one GPR as an exact integer-valued f80.  FILD is signed; an unsigned
// value with its top bit set is corrected by adding the exact 2^64 constant
// already installed in the scratch area.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_x87_push_gpr_integer(CodegenBuffer* buffer, X64Register reg, bool unsigned_value,
                                                                    s32 scratch_displacement, u32* x87_depth)
{
    s32 integer_displacement = scratch_displacement + CODEGEN_X64_X87_SCRATCH_INTEGER_OFFSET;
    s32 constant_displacement = scratch_displacement + CODEGEN_X64_X87_SCRATCH_FLOAT_OFFSET;
    BusterX86MetadataPhysicalOperand register_operand = codegen_canonical_x64_metadata_gpr(reg, 64);
    BusterX86MetadataPhysicalOperand store_operands[2] = {
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, integer_displacement), register_operand};
    bool result = codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), store_operands, BUSTER_ARRAY_LENGTH(store_operands)) &&
                  codegen_canonical_x64_x87_push(buffer, true, X64_REGISTER_RBP, integer_displacement, 64, x87_depth);
    if (result && unsigned_value)
    {
        BusterX86MetadataPhysicalOperand test_operands[2] = {register_operand, register_operand};
        BusterX86MetadataPhysicalOperand nonnegative_branch = codegen_canonical_x64_metadata_relative(0, 8);
        result = codegen_canonical_x64_metadata_emit(buffer, S8("TEST"), test_operands, BUSTER_ARRAY_LENGTH(test_operands));
        u32 branch_offset = (u32)buffer->count;
        result = result && codegen_canonical_x64_metadata_emit(buffer, S8("JNS"), &nonnegative_branch, 1) &&
                 codegen_canonical_x64_x87_push(buffer, false, X64_REGISTER_RBP, constant_displacement, 80, x87_depth) &&
                 codegen_canonical_x64_x87_pair(buffer, S8("FADDP"), 1, 0, true, x87_depth);
        s64 delta = (s64)buffer->count - (s64)(branch_offset + 2);
        if (!result || !buffer->bytes || branch_offset + 2 > buffer->count || delta < INT8_MIN || delta > INT8_MAX)
        {
            result = false;
        }
        else
        {
            s8 short_delta = (s8)delta;
            memcpy(buffer->bytes + branch_offset + 1, &short_delta, sizeof(short_delta));
        }
    }
    return result;
}

// Exact decomposition: signed values are high(s64)*2^64 + low(u64), unsigned
// values are high(u64)*2^64 + low(u64).  The two terms stay exact in f80; only
// their final addition runs at the destination precision, avoiding the
// half-way double rounding caused by converting the halves independently.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_i128_to_float(CodegenBuffer* buffer, s32 source_displacement,
                                                              s32 result_displacement, s32 scratch_displacement,
                                                              u16 target_width, bool signed_value, u32* x87_depth)
{
    if (!buffer || (target_width != 32 && target_width != 64 && target_width != 80))
    {
        return false;
    }
    s32 constant_displacement = scratch_displacement + CODEGEN_X64_X87_SCRATCH_FLOAT_OFFSET;
    BusterX86MetadataPhysicalOperand load_low[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, source_displacement)};
    BusterX86MetadataPhysicalOperand load_high[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, source_displacement + 8)};
    bool result = codegen_canonical_x64_x87_i128_precision(buffer, scratch_displacement, 80, true) &&
                  codegen_canonical_x64_store_f80_constant(buffer, constant_displacement,
                                                           CODEGEN_X64_F80_TWO_POWER_64_SIGNIFICAND,
                                                           CODEGEN_X64_F80_TWO_POWER_64_SIGN_EXPONENT) &&
                  codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), load_low, BUSTER_ARRAY_LENGTH(load_low)) &&
                  codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), load_high, BUSTER_ARRAY_LENGTH(load_high)) &&
                  codegen_canonical_x64_x87_push_gpr_integer(buffer, X64_REGISTER_RDX, !signed_value,
                                                              scratch_displacement, x87_depth) &&
                  codegen_canonical_x64_x87_push(buffer, false, X64_REGISTER_RBP, constant_displacement, 80, x87_depth) &&
                  codegen_canonical_x64_x87_pair(buffer, S8("FMULP"), 1, 0, true, x87_depth) &&
                  codegen_canonical_x64_x87_push_gpr_integer(buffer, X64_REGISTER_RAX, true,
                                                              scratch_displacement, x87_depth) &&
                  codegen_canonical_x64_x87_i128_precision(buffer, scratch_displacement, target_width, false) &&
                  codegen_canonical_x64_x87_pair(buffer, S8("FADDP"), 1, 0, true, x87_depth) &&
                  codegen_canonical_x64_x87_pop_store(buffer, false, X64_REGISTER_RBP, result_displacement,
                                                       target_width, x87_depth) &&
                  codegen_canonical_x64_x87_i128_precision_end(buffer, scratch_displacement);
    return result && buffer->error == CODEGEN_ERROR_NONE;
}

BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_float_sign(CodegenBuffer* buffer, s32 source_displacement,
                                                           u16 source_width, bool signed_value)
{
    BusterX86MetadataPhysicalOperand r8_32 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 32);
    BusterX86MetadataPhysicalOperand r8_64 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64);
    if (!signed_value)
    {
        BusterX86MetadataPhysicalOperand clear[2] = {r8_32, r8_32};
        return codegen_canonical_x64_metadata_emit(buffer, S8("XOR"), clear, BUSTER_ARRAY_LENGTH(clear));
    }
    BusterX86MetadataPhysicalOperand load[2] = {0};
    String8 load_mnemonic = S8("MOV");
    u16 shift = 0;
    if (source_width == 32)
    {
        load[0] = r8_32;
        load[1] = codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 32, source_displacement);
        shift = 31;
    }
    else if (source_width == 64)
    {
        load[0] = r8_64;
        load[1] = codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, source_displacement);
        shift = 63;
    }
    else if (source_width == 80)
    {
        load_mnemonic = S8("MOVZX");
        load[0] = r8_32;
        load[1] = codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 16, source_displacement + 8);
        shift = 15;
    }
    else
    {
        return false;
    }
    BusterX86MetadataPhysicalOperand shift_operands[2] = {
        source_width == 64 ? r8_64 : r8_32, codegen_canonical_x64_metadata_immediate(shift, 8)};
    return codegen_canonical_x64_metadata_emit(buffer, load_mnemonic, load, BUSTER_ARRAY_LENGTH(load)) &&
           codegen_canonical_x64_metadata_emit(buffer, S8("SHR"), shift_operands, BUSTER_ARRAY_LENGTH(shift_operands));
}

// Split |source| at 2^64.  Each quotient/remainder fits the existing exact
// f80-to-u64 truncation path; signed destinations conditionally negate the
// resulting pair with a 0/-1 mask, including INT128_MIN without overflow.
BUSTER_GLOBAL_LOCAL bool codegen_canonical_x64_float_to_i128(CodegenBuffer* buffer, s32 source_displacement,
                                                              s32 result_displacement, s32 scratch_displacement,
                                                              u16 source_width, bool signed_value, u32* x87_depth)
{
    if (!buffer || (source_width != 32 && source_width != 64 && source_width != 80))
    {
        return false;
    }
    s32 constant_displacement = scratch_displacement + CODEGEN_X64_X87_SCRATCH_FLOAT_OFFSET;
    s32 temporary_displacement = scratch_displacement + CODEGEN_X64_X87_I128_TEMP_OFFSET;
    BusterX86MetadataPhysicalOperand high_store[2] = {
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement + 8),
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64)};
    BusterX86MetadataPhysicalOperand high_load[2] = {
        codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64),
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement + 8)};
    bool result = codegen_canonical_x64_x87_i128_precision(buffer, scratch_displacement, 80, true) &&
                  codegen_canonical_x64_float_sign(buffer, source_displacement, source_width, signed_value) &&
                  codegen_canonical_x64_store_f80_constant(buffer, constant_displacement,
                                                           CODEGEN_X64_F80_TWO_POWER_64_SIGNIFICAND,
                                                           CODEGEN_X64_F80_TWO_POWER_64_SIGN_EXPONENT) &&
                  codegen_canonical_x64_x87_push(buffer, false, X64_REGISTER_RBP, source_displacement,
                                                  source_width, x87_depth) &&
                  codegen_canonical_x64_x87_features(buffer, S8("FABS"), 0, 0) &&
                  codegen_canonical_x64_x87_push(buffer, false, X64_REGISTER_RBP, constant_displacement, 80, x87_depth) &&
                  codegen_canonical_x64_x87_pair(buffer, S8("FDIVP"), 1, 0, true, x87_depth) &&
                  codegen_canonical_x64_x87_pop_store(buffer, false, X64_REGISTER_RBP, temporary_displacement, 80, x87_depth) &&
                  codegen_canonical_x64_emit_f80_to_unsigned_64(buffer, temporary_displacement, scratch_displacement, x87_depth) &&
                  codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), high_store, BUSTER_ARRAY_LENGTH(high_store)) &&
                  codegen_canonical_x64_store_f80_constant(buffer, constant_displacement,
                                                           CODEGEN_X64_F80_TWO_POWER_64_SIGNIFICAND,
                                                           CODEGEN_X64_F80_TWO_POWER_64_SIGN_EXPONENT) &&
                  codegen_canonical_x64_x87_push(buffer, false, X64_REGISTER_RBP, source_displacement,
                                                  source_width, x87_depth) &&
                  codegen_canonical_x64_x87_features(buffer, S8("FABS"), 0, 0) &&
                  codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), high_load, BUSTER_ARRAY_LENGTH(high_load)) &&
                  codegen_canonical_x64_x87_push_gpr_integer(buffer, X64_REGISTER_RDX, true,
                                                              scratch_displacement, x87_depth) &&
                  codegen_canonical_x64_x87_push(buffer, false, X64_REGISTER_RBP, constant_displacement, 80, x87_depth) &&
                  codegen_canonical_x64_x87_pair(buffer, S8("FMULP"), 1, 0, true, x87_depth) &&
                  codegen_canonical_x64_x87_pair(buffer, S8("FSUBP"), 1, 0, true, x87_depth) &&
                  codegen_canonical_x64_x87_pop_store(buffer, false, X64_REGISTER_RBP, temporary_displacement, 80, x87_depth) &&
                  codegen_canonical_x64_emit_f80_to_unsigned_64(buffer, temporary_displacement, scratch_displacement, x87_depth) &&
                  codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), high_load, BUSTER_ARRAY_LENGTH(high_load));
    BusterX86MetadataPhysicalOperand r8_64 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_R8, 64);
    BusterX86MetadataPhysicalOperand rax_64 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RAX, 64);
    BusterX86MetadataPhysicalOperand rdx_64 = codegen_canonical_x64_metadata_gpr(X64_REGISTER_RDX, 64);
    BusterX86MetadataPhysicalOperand negate_mask[1] = {r8_64};
    BusterX86MetadataPhysicalOperand xor_low[2] = {rax_64, r8_64};
    BusterX86MetadataPhysicalOperand xor_high[2] = {rdx_64, r8_64};
    BusterX86MetadataPhysicalOperand sub_low[2] = {rax_64, r8_64};
    BusterX86MetadataPhysicalOperand sub_high[2] = {rdx_64, r8_64};
    BusterX86MetadataPhysicalOperand low_store[2] = {
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement), rax_64};
    BusterX86MetadataPhysicalOperand final_high_store[2] = {
        codegen_canonical_x64_metadata_memory(X64_REGISTER_RBP, 64, result_displacement + 8), rdx_64};
    result = result && codegen_canonical_x64_metadata_emit(buffer, S8("NEG"), negate_mask, 1) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("XOR"), xor_low, BUSTER_ARRAY_LENGTH(xor_low)) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("XOR"), xor_high, BUSTER_ARRAY_LENGTH(xor_high)) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("SUB"), sub_low, BUSTER_ARRAY_LENGTH(sub_low)) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("SBB"), sub_high, BUSTER_ARRAY_LENGTH(sub_high)) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), low_store, BUSTER_ARRAY_LENGTH(low_store)) &&
             codegen_canonical_x64_metadata_emit(buffer, S8("MOV"), final_high_store, BUSTER_ARRAY_LENGTH(final_high_store)) &&
             codegen_canonical_x64_x87_i128_precision_end(buffer, scratch_displacement);
    return result && buffer->error == CODEGEN_ERROR_NONE;
}

'''
text = replace_once(text, "\nBUSTER_GLOBAL_LOCAL String8 codegen_global_assembly_trim(String8 value)\n", "\n" + helpers + "BUSTER_GLOBAL_LOCAL String8 codegen_global_assembly_trim(String8 value)\n", "insert x64 i128 float helpers")

scan = r'''
        bool canonical_function_has_i128_float_cast = false;
        if (target.cpu_arch == CPU_ARCH_X86_64)
        {
            for (u32 instruction_index = 0; instruction_index < function->instruction_count && !canonical_function_has_i128_float_cast;
                 instruction_index += 1)
            {
                IrInstruction* cast = function->instructions + instruction_index;
                if (cast->opcode != IR_OPCODE_CAST || !cast->operand_count || cast->operands[0].value >= function->value_count)
                {
                    continue;
                }
                IrType* cast_source = ir_type_from_id(&program->types, function->values[cast->operands[0].value].canonical_type);
                IrType* cast_target = ir_type_from_id(&program->types, cast->canonical_type);
                bool source_i128 = cast_source && cast_source->kind == IR_TYPE_INTEGER && cast_source->bit_width == 128;
                bool target_i128 = cast_target && cast_target->kind == IR_TYPE_INTEGER && cast_target->bit_width == 128;
                bool source_float = cast_source && cast_source->kind == IR_TYPE_FLOAT &&
                                    (cast_source->bit_width == 32 || cast_source->bit_width == 64 || cast_source->bit_width == 80);
                bool target_float = cast_target && cast_target->kind == IR_TYPE_FLOAT &&
                                    (cast_target->bit_width == 32 || cast_target->bit_width == 64 || cast_target->bit_width == 80);
                canonical_function_has_i128_float_cast =
                    (source_i128 && target_float &&
                     (cast->conversion_operation == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT ||
                      cast->conversion_operation == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)) ||
                    (target_i128 && source_float &&
                     (cast->conversion_operation == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER ||
                      cast->conversion_operation == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER));
            }
        }
'''
text = replace_once(text, "        CodegenCanonicalAbiValue canonical_return_abi = codegen_canonical_x64_windows_vector_result(\n", scan + "        CodegenCanonicalAbiValue canonical_return_abi = codegen_canonical_x64_windows_vector_result(\n", "scan i128 float casts")

old_scratch = r'''        s32 canonical_x87_scratch_displacement = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 && result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && canonical_function_has_f80)
        {
            canonical_x87_scratch_displacement = -(s32)(frame_size_64 + CODEGEN_X64_X87_SCRATCH_SIZE);
            frame_size_64 += CODEGEN_X64_X87_SCRATCH_SIZE;
        }
'''
new_scratch = r'''        s32 canonical_x87_scratch_displacement = 0;
        if (target.cpu_arch == CPU_ARCH_X86_64 &&
            ((result.abi == CODEGEN_ABI_X86_64_SYSTEM_V && canonical_function_has_f80) || canonical_function_has_i128_float_cast))
        {
            u32 x87_scratch_size = canonical_function_has_i128_float_cast ? CODEGEN_X64_X87_I128_SCRATCH_SIZE
                                                                          : CODEGEN_X64_X87_SCRATCH_SIZE;
            canonical_x87_scratch_displacement = -(s32)(frame_size_64 + x87_scratch_size);
            frame_size_64 += x87_scratch_size;
        }
'''
text = replace_once(text, old_scratch, new_scratch, "allocate i128 x87 scratch")

cast_dispatch = r'''                        bool source_integer128 = source_type->kind == IR_TYPE_INTEGER && source_type->bit_width == 128;
                        bool target_integer128 = target_type->kind == IR_TYPE_INTEGER && target_type->bit_width == 128;
                        bool source_float_scalar = source_type->kind == IR_TYPE_FLOAT &&
                                                   (source_type->bit_width == 32 || source_type->bit_width == 64 || source_type->bit_width == 80);
                        bool target_float_scalar = target_type->kind == IR_TYPE_FLOAT &&
                                                   (target_type->bit_width == 32 || target_type->bit_width == 64 || target_type->bit_width == 80);
                        if ((source_integer128 && target_float_scalar &&
                             (conversion == IR_CONVERSION_SIGNED_INTEGER_TO_FLOAT || conversion == IR_CONVERSION_UNSIGNED_INTEGER_TO_FLOAT)) ||
                            (target_integer128 && source_float_scalar &&
                             (conversion == IR_CONVERSION_FLOAT_TO_SIGNED_INTEGER || conversion == IR_CONVERSION_FLOAT_TO_UNSIGNED_INTEGER)))
                        {
                            s32 cast_source_displacement =
                                c_x64_frame_displacement(&emitter, value_offsets[instruction->operands[0].value]);
                            s32 cast_scratch_displacement = codegen_canonical_x64_rebase_frame_displacement(
                                &buffer, canonical_x87_scratch_displacement, canonical_x64_frame_base_offset);
                            bool cast_emitted = source_integer128
                                                    ? codegen_canonical_x64_i128_to_float(
                                                          &buffer, cast_source_displacement, result_displacement, cast_scratch_displacement,
                                                          (u16)target_type->bit_width, source_type->is_signed, &x87_stack_depth)
                                                    : codegen_canonical_x64_float_to_i128(
                                                          &buffer, cast_source_displacement, result_displacement, cast_scratch_displacement,
                                                          (u16)source_type->bit_width, target_type->is_signed, &x87_stack_depth);
                            if (!cast_emitted)
                            {
                                result.error = buffer.error != CODEGEN_ERROR_NONE ? buffer.error : CODEGEN_ERROR_UNSUPPORTED_INSTRUCTION;
                                return result;
                            }
                            instruction_id = instruction->next;
                            continue;
                        }
'''
text = replace_once(text, "                        bool source_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(\n", cast_dispatch + "                        bool source_contains_f80 = codegen_canonical_x64_type_contains_f80_cached(\n", "dispatch i128 float casts")
text = replace_once(
    text,
    "                        bool source_integer128 = source_type->kind == IR_TYPE_INTEGER && source_type->bit_width == 128;\n"
    "                        bool target_integer128 = target_type->kind == IR_TYPE_INTEGER && target_type->bit_width == 128;\n"
    "                        if (source_integer128 || target_integer128)\n",
    "                        if (source_integer128 || target_integer128)\n",
    "remove duplicate i128 declarations",
)

codegen_path.write_text(text, encoding="utf-8")

test_path.write_text(r'''typedef unsigned __int128 U128;
typedef __int128 S128;
typedef unsigned long long U64;
typedef unsigned int U32;

static void signed_to_double(S128 value, double *result) { *result = (double)value; }
static void unsigned_to_double(U128 value, double *result) { *result = (double)value; }
static void signed_to_float(S128 value, float *result) { *result = (float)value; }
static void unsigned_to_float(U128 value, float *result) { *result = (float)value; }
static void signed_to_long_double(S128 value, long double *result) { *result = (long double)value; }
static void unsigned_to_long_double(U128 value, long double *result) { *result = (long double)value; }
static void double_to_signed(double value, S128 *result) { *result = (S128)value; }
static void double_to_unsigned(double value, U128 *result) { *result = (U128)value; }
static void float_to_signed(float value, S128 *result) { *result = (S128)value; }
static void float_to_unsigned(float value, U128 *result) { *result = (U128)value; }
static void long_double_to_signed(long double value, S128 *result) { *result = (S128)value; }
static void long_double_to_unsigned(long double value, U128 *result) { *result = (U128)value; }

static U64 double_bits(double value)
{
    union { double f; U64 u; } bits = {value};
    return bits.u;
}

static U32 float_bits(float value)
{
    union { float f; U32 u; } bits = {value};
    return bits.u;
}

int main(void)
{
    double d = 0.0;
    float f = 0.0f;
    long double ld = 0.0L;
    S128 s = 0;
    U128 u = 0;
    S128 minimum = -((S128)1 << 126) - ((S128)1 << 126);

    signed_to_double(-((S128)1 << 100), &d);
    if (d != -0x1p100) return 1;
    unsigned_to_double((U128)1 << 127, &d);
    if (d != 0x1p127) return 2;
    U128 double_tie = ((U128)1 << 127) + ((U128)1 << 74);
    unsigned_to_double(double_tie - 1, &d);
    if (double_bits(d) != 0x47e0000000000000ULL) return 3;
    unsigned_to_double(double_tie, &d);
    if (double_bits(d) != 0x47e0000000000000ULL) return 4;
    unsigned_to_double(double_tie + 1, &d);
    if (double_bits(d) != 0x47e0000000000001ULL) return 5;
    S128 signed_double_tie = ((S128)1 << 126) + ((S128)1 << 73);
    signed_to_double(-(signed_double_tie + 1), &d);
    if (double_bits(d) != 0xc7d0000000000001ULL) return 6;

    U128 float_tie = ((U128)1 << 127) + ((U128)1 << 103);
    unsigned_to_float(float_tie - 1, &f);
    if (float_bits(f) != 0x7f000000U) return 7;
    unsigned_to_float(float_tie, &f);
    if (float_bits(f) != 0x7f000000U) return 8;
    unsigned_to_float(float_tie + 1, &f);
    if (float_bits(f) != 0x7f000001U) return 9;
    signed_to_float(-((S128)1 << 126), &f);
    if (f != -0x1p126f) return 10;

    signed_to_long_double(-((S128)1 << 100), &ld);
    if (ld != -0x1p100L) return 11;
    unsigned_to_long_double((U128)1 << 127, &ld);
    if (ld != 0x1p127L) return 12;
    if (sizeof(long double) > sizeof(double))
    {
        U128 long_double_tie = ((U128)1 << 126) + ((U128)1 << 62);
        unsigned_to_long_double(long_double_tie, &ld);
        if (ld != 0x1p126L) return 13;
        unsigned_to_long_double(long_double_tie + 1, &ld);
        if (ld != 0x1.0000000000000002p126L) return 14;
    }

    double_to_signed(-0x1p127, &s);
    if (s != minimum) return 15;
    double_to_unsigned(0x1p127, &u);
    if (u != ((U128)1 << 127)) return 16;
    double_to_unsigned(0x1.8p64, &u);
    if (u != (((U128)1 << 64) | ((U128)1 << 63))) return 17;
    float_to_signed(-0x1p127f, &s);
    if (s != minimum) return 18;
    float_to_unsigned(0x1p127f, &u);
    if (u != ((U128)1 << 127)) return 19;
    long_double_to_signed(-0x1p127L, &s);
    if (s != minimum) return 20;
    long_double_to_unsigned(0x1.8p64L, &u);
    if (u != (((U128)1 << 64) | ((U128)1 << 63))) return 21;
    long_double_to_signed(-12345.875L, &s);
    if (s != -12345) return 22;
    return 0;
}
''', encoding="utf-8")

print("patched issue 194 and added cross-width runtime regression")
