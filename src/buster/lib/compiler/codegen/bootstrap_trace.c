// Opt-in bootstrap snapshots, not an interchange format. The field walks
// below are deliberately explicit: object padding, pointer addresses, arena
// capacity and lazy ABI caches must never become compiler input evidence.
// bootstrap_trace_ir captures semantic canonical records after validation;
// bootstrap_trace_machine captures actual selected MIR before allocation and
// runs the verifier even for selector-certified functions. Debug source byte
// offsets are excluded because resource-header storage differs by generation;
// line-mark instruction IDs and the final debug-bearing binary are compared.

#include <buster/lib/compiler/codegen/bootstrap_trace.h>
#include <buster/lib/string.h>
#include <stdio.h>

BUSTER_GLOBAL_LOCAL void bootstrap_trace_flush(BootstrapTrace* trace)
{
    if (!trace->failed && trace->used)
    {
        trace->failed = fwrite(trace->buffer, 1, (size_t)trace->used, (FILE*)trace->stream) != trace->used;
        trace->used = 0;
    }
}

BUSTER_GLOBAL_LOCAL void bootstrap_trace_write(BootstrapTrace* trace, String8 value)
{
    u64 offset = 0;
    while (!trace->failed && offset < value.length)
    {
        u64 count = BUSTER_MIN(value.length - offset, BOOTSTRAP_TRACE_BUFFER_SIZE - trace->used);
        memcpy(trace->buffer + trace->used, value.pointer + offset, count);
        trace->used += count;
        offset += count;
        if (trace->used == BOOTSTRAP_TRACE_BUFFER_SIZE)
        {
            bootstrap_trace_flush(trace);
        }
    }
}

void bootstrap_trace_u64(BootstrapTrace* trace, u64 value)
{
    char8 bytes[8];
    for (u32 i = 0; i < 8; i += 1)
    {
        bytes[i] = (char8)(value >> (i * 8));
    }
    bootstrap_trace_write(trace, (String8){bytes, sizeof(bytes)});
}

void bootstrap_trace_string(BootstrapTrace* trace, String8 value)
{
    bootstrap_trace_u64(trace, value.length);
    bootstrap_trace_write(trace, value);
}

BootstrapTrace bootstrap_trace_open(Arena* arena, String8 path, String8 kind)
{
    String8 terminated = string_format_z(arena, S8("{S8}"), path);
    BootstrapTrace result = {.stream = fopen((char const*)terminated.pointer, "wb")};
    result.failed = !result.stream;
    if (!result.failed)
    {
        result.buffer = arena_allocate(arena, u8, BOOTSTRAP_TRACE_BUFFER_SIZE);
        bootstrap_trace_string(&result, S8("BUSTER bootstrap trace v1"));
        bootstrap_trace_string(&result, kind);
    }
    return result;
}

bool bootstrap_trace_close(BootstrapTrace* trace)
{
    bootstrap_trace_u64(trace, BOOTSTRAP_TRACE_END);
    bootstrap_trace_flush(trace);
    if (trace->stream)
    {
        bool closed = fclose((FILE*)trace->stream) == 0;
        trace->failed = trace->failed || !closed;
        trace->stream = 0;
    }
    return !trace->failed;
}

BUSTER_GLOBAL_LOCAL void bootstrap_trace_type(BootstrapTrace* trace, IrType* type)
{
    bootstrap_trace_string(trace, type->name);
    bootstrap_trace_u64(trace, (u64)type->id.value);
    bootstrap_trace_u64(trace, (u64)type->element_type.value);
    bootstrap_trace_u64(trace, (u64)type->return_type.value);
    bootstrap_trace_u64(trace, (u64)type->unqualified_type.value);
    bootstrap_trace_u64(trace, (u64)type->kind);
    bootstrap_trace_u64(trace, (u64)type->calling_convention);
    bootstrap_trace_u64(trace, (u64)type->element_count);
    bootstrap_trace_u64(trace, (u64)type->bit_width);
    bootstrap_trace_u64(trace, (u64)type->is_signed);
    bootstrap_trace_u64(trace, (u64)type->is_variadic);
    bootstrap_trace_u64(trace, (u64)type->is_atomic);
    bootstrap_trace_u64(trace, (u64)type->is_nullptr);
    bootstrap_trace_u64(trace, (u64)type->is_volatile);
    bootstrap_trace_u64(trace, (u64)type->is_transparent_union);
    bootstrap_trace_u64(trace, (u64)type->is_complex);
    bootstrap_trace_u64(trace, (u64)type->is_noreturn);
    bootstrap_trace_u64(trace, (u64)type->is_unprototyped);
    bootstrap_trace_u64(trace, (u64)type->layout.size);
    bootstrap_trace_u64(trace, (u64)type->layout.alignment);
    bootstrap_trace_u64(trace, (u64)type->layout.abi_class);
    bootstrap_trace_u64(trace, (u64)type->layout.resolved);
    bootstrap_trace_u64(trace, (u64)type->layout.natural_alignment);
    bootstrap_trace_u64(trace, type->parameter_count);
    for (u32 i = 0; i < type->parameter_count; i += 1)
    {
        bootstrap_trace_u64(trace, (u64)type->parameter_types[i].value);
    }
    bootstrap_trace_u64(trace, type->field_count);
    for (u32 i = 0; i < type->field_count; i += 1)
    {
        IrField* field = &type->fields[i];
        bootstrap_trace_string(trace, field->name);
        bootstrap_trace_u64(trace, (u64)field->type.value);
        bootstrap_trace_u64(trace, (u64)field->offset);
        bootstrap_trace_u64(trace, (u64)field->bit_offset);
        bootstrap_trace_u64(trace, (u64)field->bit_width);
        bootstrap_trace_u64(trace, (u64)field->is_bit_field);
        bootstrap_trace_u64(trace, (u64)field->access_size);
    }
    bootstrap_trace_u64(trace, type->enum_member_count);
    for (u32 i = 0; i < type->enum_member_count; i += 1)
    {
        bootstrap_trace_string(trace, type->enum_members[i].name);
        bootstrap_trace_u64(trace, type->enum_members[i].value);
    }
}

BUSTER_GLOBAL_LOCAL void bootstrap_trace_ir_function(BootstrapTrace* trace, IrFunction* function)
{
    bootstrap_trace_string(trace, function->name);
    bootstrap_trace_u64(trace, (u64)function->symbol.value);
    bootstrap_trace_u64(trace, (u64)function->canonical_type.value);
    bootstrap_trace_u64(trace, (u64)function->id.value);
    bootstrap_trace_u64(trace, (u64)function->entry.value);
    bootstrap_trace_u64(trace, (u64)function->state);
    bootstrap_trace_u64(trace, (u64)function->block_count);
    for (u32 i = 0; i < function->block_count; i += 1)
    {
        IrBlock* block = &function->blocks[i];
        bootstrap_trace_u64(trace, (u64)block->id.value);
        bootstrap_trace_u64(trace, (u64)block->first_instruction.value);
        bootstrap_trace_u64(trace, (u64)block->last_instruction.value);
        bootstrap_trace_u64(trace, (u64)block->parameter_count);
        bootstrap_trace_u64(trace, (u64)block->predecessor_count);
        bootstrap_trace_u64(trace, (u64)block->terminated);
        bootstrap_trace_u64(trace, (u64)block->sealed);
        for (IrBlockParameter* parameter = block->first_parameter; parameter; parameter = parameter->next)
        {
            bootstrap_trace_u64(trace, (u64)parameter->canonical_type.value);
            bootstrap_trace_u64(trace, (u64)parameter->canonical_local.value);
            bootstrap_trace_u64(trace, (u64)parameter->value.value);
            bootstrap_trace_u64(trace, (u64)parameter->incoming_count);
            for (IrIncoming* incoming = parameter->first_incoming; incoming; incoming = incoming->next)
            {
                bootstrap_trace_u64(trace, (u64)incoming->predecessor.value);
                bootstrap_trace_u64(trace, (u64)incoming->value.value);
            }
        }
        for (IrPredecessor* predecessor = block->first_predecessor; predecessor; predecessor = predecessor->next)
        {
            bootstrap_trace_u64(trace, predecessor->block.value);
        }
    }
    bootstrap_trace_u64(trace, function->instruction_count);
    for (u32 i = 0; i < function->instruction_count; i += 1)
    {
        IrInstruction* instruction = &function->instructions[i];
        bootstrap_trace_u64(trace, (u64)instruction->canonical_type.value);
        bootstrap_trace_u64(trace, (u64)instruction->symbol.value);
        bootstrap_trace_u64(trace, (u64)instruction->canonical_local.value);
        bootstrap_trace_u64(trace, (u64)instruction->next.value);
        bootstrap_trace_u64(trace, (u64)instruction->result.value);
        bootstrap_trace_u64(trace, (u64)instruction->opcode);
        bootstrap_trace_u64(trace, (u64)instruction->conversion_operation);
        bootstrap_trace_u64(trace, (u64)instruction->unary_operation);
        bootstrap_trace_u64(trace, (u64)instruction->binary_operation);
        bootstrap_trace_u64(trace, (u64)instruction->memory_order);
        bootstrap_trace_u64(trace, (u64)instruction->failure_memory_order);
        bootstrap_trace_u64(trace, (u64)instruction->atomic_operation);
        bootstrap_trace_u64(trace, (u64)instruction->immediate_is_negative);
        bootstrap_trace_u64(trace, (u64)instruction->atomic_signal_fence);
        bootstrap_trace_u64(trace, (u64)instruction->volatile_access);
        bootstrap_trace_u64(trace, (u64)instruction->simd_operation);
        bootstrap_trace_u64(trace, instruction->operand_count);
        for (u32 j = 0; j < instruction->operand_count; j += 1)
        {
            bootstrap_trace_u64(trace, (u64)instruction->operands[j].value);
        }
        bootstrap_trace_u64(trace, instruction->target_count);
        for (u32 j = 0; j < instruction->target_count; j += 1)
        {
            bootstrap_trace_u64(trace, (u64)instruction->targets[j].value);
        }
        bootstrap_trace_u64(trace, instruction->immediate_count);
        for (u32 j = 0; j < instruction->immediate_count; j += 1)
        {
            bootstrap_trace_u64(trace, (u64)instruction->immediates[j]);
        }
    }
    bootstrap_trace_u64(trace, function->value_count);
    for (u32 i = 0; i < function->value_count; i += 1)
    {
        IrValue* value = &function->values[i];
        bootstrap_trace_u64(trace, (u64)value->canonical_type.value);
        bootstrap_trace_u64(trace, (u64)value->definition.value);
        bootstrap_trace_u64(trace, (u64)value->alignment);
        bootstrap_trace_u64(trace, (u64)value->category);
        bootstrap_trace_u64(trace, (u64)value->is_read_only);
        bootstrap_trace_u64(trace, (u64)value->points_to_read_only);
        bootstrap_trace_u64(trace, (u64)value->is_volatile);
    }
    bootstrap_trace_u64(trace, function->local_count);
    for (u32 i = 0; i < function->local_count; i += 1)
    {
        bootstrap_trace_u64(trace, function->local_places ? function->local_places[i].value : IR_ID_UNDERLYING_INVALID);
        bootstrap_trace_u64(trace, function->local_uses_memory && function->local_uses_memory[i]);
    }
    bootstrap_trace_u64(trace, function->extra_count);
    for (u32 i = 0; i < function->extra_count; i += 1)
    {
        IrInstructionExtra* extra = &function->extras[i];
        bootstrap_trace_u64(trace, function->extra_instructions[i].value);
        bootstrap_trace_string(trace, extra->literal);
        bootstrap_trace_u64(trace, extra->label_name_count);
        for (u32 j = 0; j < extra->label_name_count; j += 1)
        {
            bootstrap_trace_string(trace, extra->label_names[j]);
        }
        bootstrap_trace_u64(trace, extra->operand_name_count);
        for (u32 j = 0; j < extra->operand_name_count; j += 1)
        {
            bootstrap_trace_string(trace, extra->operand_names[j]);
        }
        bootstrap_trace_u64(trace, extra->clobber_count);
        for (u32 j = 0; j < extra->clobber_count; j += 1)
        {
            bootstrap_trace_string(trace, extra->clobbers[j]);
        }
    }
    bootstrap_trace_u64(trace, function->label_metadata_count);
    for (u32 i = 0; i < function->label_metadata_count; i += 1)
    {
        IrValueLabelMetadata* metadata = &function->label_metadata[i];
        bootstrap_trace_u64(trace, function->label_metadata_values[i].value);
        bootstrap_trace_u64(trace, (u64)metadata->is_label_value);
        bootstrap_trace_u64(trace, (u64)metadata->has_label_provenance);
        bootstrap_trace_u64(trace, (u64)metadata->has_non_label_provenance);
        bootstrap_trace_u64(trace, (u64)metadata->label_block_count);
        for (u32 j = 0; j < metadata->label_block_count; j += 1)
        {
            bootstrap_trace_u64(trace, metadata->label_blocks[j].value);
        }
        bootstrap_trace_u64(trace, metadata->label_path_count);
        for (u32 j = 0; j < metadata->label_path_count; j += 1)
        {
            IrLabelProvenancePath* path = &metadata->label_paths[j];
            bootstrap_trace_u64(trace, (u64)path->offset);
            bootstrap_trace_u64(trace, (u64)path->size);
            bootstrap_trace_u64(trace, (u64)path->is_non_label);
            bootstrap_trace_u64(trace, (u64)path->label_block_count);
            for (u32 k = 0; k < path->label_block_count; k += 1)
            {
                bootstrap_trace_u64(trace, path->label_blocks[k].value);
            }
        }
    }
}

void bootstrap_trace_ir(BootstrapTrace* trace, IrProgram* program, IrModule* module)
{
    bootstrap_trace_string(trace, module->name);
    bootstrap_trace_u64(trace, (u64)program->data_layout.boolean.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.boolean.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.boolean.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.plain_char.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.plain_char.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.plain_char.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.signed_char.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.signed_char.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.signed_char.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_char.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_char.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_char.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.short_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.short_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.short_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_short_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_short_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_short_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_long_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_long_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_long_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_long_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_long_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_long_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_long_long_integer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_long_long_integer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_long_long_integer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.integer128.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.integer128.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.integer128.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_integer128.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_integer128.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.unsigned_integer128.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.float_type.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.float_type.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.float_type.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.double_type.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.double_type.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.double_type.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_double_type.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_double_type.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.long_double_type.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.pointer.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.pointer.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.pointer.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.va_list.size);
    bootstrap_trace_u64(trace, (u64)program->data_layout.va_list.alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.va_list.bit_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.atomic_min_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.atomic_max_width);
    bootstrap_trace_u64(trace, (u64)program->data_layout.atomic_alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.abi_stack_alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.abi_max_alignment);
    bootstrap_trace_u64(trace, (u64)program->data_layout.endianness);
    bootstrap_trace_u64(trace, (u64)program->data_layout.plain_char_is_signed);
    bootstrap_trace_u64(trace, (u64)program->data_layout.has_128_bit_integer);
    bootstrap_trace_u64(trace, program->types.count);
    for (u32 i = 0; i < program->types.count; i += 1)
    {
        bootstrap_trace_type(trace, &program->types.types[i]);
    }
    bootstrap_trace_u64(trace, program->symbols.count);
    for (u32 i = 0; i < program->symbols.count; i += 1)
    {
        IrSymbol* symbol = &program->symbols.symbols[i];
        bootstrap_trace_string(trace, symbol->name);
        bootstrap_trace_string(trace, symbol->link_name);
        bootstrap_trace_string(trace, symbol->section_name);
        bootstrap_trace_u64(trace, (u64)symbol->type.value);
        bootstrap_trace_u64(trace, (u64)symbol->id.value);
        bootstrap_trace_u64(trace, (u64)symbol->kind);
        bootstrap_trace_u64(trace, (u64)symbol->linkage);
        bootstrap_trace_u64(trace, (u64)symbol->is_definition);
        bootstrap_trace_u64(trace, (u64)symbol->is_thread_local);
        bootstrap_trace_u64(trace, (u64)symbol->is_weak);
        bootstrap_trace_u64(trace, (u64)symbol->is_hidden);
    }
    bootstrap_trace_u64(trace, module->global_count);
    for (u32 i = 0; i < module->global_count; i += 1)
    {
        IrGlobal* global = &module->globals[i];
        bootstrap_trace_u64(trace, (u64)global->symbol.value);
        bootstrap_trace_u64(trace, (u64)global->initializer_symbol.value);
        bootstrap_trace_u64(trace, (u64)global->type.value);
        bootstrap_trace_u64(trace, (u64)global->initializer_addend);
        bootstrap_trace_u64(trace, (u64)global->initializer_bits);
        bootstrap_trace_u64(trace, (u64)global->alignment);
        bootstrap_trace_u64(trace, (u64)global->initializer_kind);
        bootstrap_trace_u64(trace, (u64)global->initializer_is_negative);
        bootstrap_trace_u64(trace, (u64)global->is_read_only);
        bootstrap_trace_u64(trace, (u64)global->is_thread_local);
        bootstrap_trace_u64(trace, (u64)global->relocation_count);
        bootstrap_trace_string(trace, BYTE_SLICE_TO_STRING(8, global->bytes));
        for (u32 j = 0; j < global->relocation_count; j += 1)
        {
            IrGlobalRelocation* relocation = &global->relocations[j];
            bootstrap_trace_u64(trace, (u64)relocation->symbol.value);
            bootstrap_trace_u64(trace, (u64)relocation->label_block.value);
            bootstrap_trace_u64(trace, (u64)relocation->addend);
            bootstrap_trace_u64(trace, (u64)relocation->offset);
            bootstrap_trace_u64(trace, (u64)relocation->is_label_address);
        }
    }
    bootstrap_trace_u64(trace, module->alias_count);
    for (u32 i = 0; i < module->alias_count; i += 1)
    {
        bootstrap_trace_u64(trace, module->aliases[i].symbol.value);
        bootstrap_trace_u64(trace, module->aliases[i].target.value);
    }
    bootstrap_trace_u64(trace, module->initializer_count);
    for (u32 i = 0; i < module->initializer_count; i += 1)
    {
        bootstrap_trace_u64(trace, (u64)module->initializers[i].symbol.value);
        bootstrap_trace_u64(trace, (u64)module->initializers[i].priority);
        bootstrap_trace_u64(trace, (u64)module->initializers[i].is_destructor);
    }
    bootstrap_trace_u64(trace, module->assembly_count);
    for (u32 i = 0; i < module->assembly_count; i += 1)
    {
        bootstrap_trace_string(trace, module->assemblies[i].source);
    }
    bootstrap_trace_u64(trace, module->function_count);
    for (u32 i = 0; i < module->function_count; i += 1)
    {
        bootstrap_trace_ir_function(trace, &module->functions[i]);
    }
}

void bootstrap_trace_machine(BootstrapTrace* trace, IrFunction* function, MachineSelectResult* selected)
{
    bootstrap_trace_string(trace, function->name);
    bootstrap_trace_u64(trace, function->id.value);
    bootstrap_trace_u64(trace, selected->supported);
    bootstrap_trace_u64(trace, (u64)selected->failed_opcode);
    if (selected->supported)
    {
        MachineFunction* machine = &selected->function;
        MachineVerifyResult validation = machine_verify_function(machine);
        if (!trace->invalid_mir && validation.error != MACHINE_VERIFY_NONE)
        {
            trace->invalid_mir = true;
            trace->invalid_function = function->name;
            trace->invalid_validation = validation;
        }
        bootstrap_trace_u64(trace, (u64)validation.error);
        bootstrap_trace_u64(trace, (u64)validation.block);
        bootstrap_trace_u64(trace, (u64)validation.instruction);
        bootstrap_trace_u64(trace, (u64)validation.operand);
        bootstrap_trace_u64(trace, (u64)validation.mutable_virtual_register_count);
        // A verifier rejection can mean the tables themselves are absent or
        // inconsistent. Preserve the error, but never dereference rejected
        // storage merely to produce diagnostics about it.
        if (validation.error == MACHINE_VERIFY_NONE)
        {
            bootstrap_trace_u64(trace, (u64)machine->outgoing_bytes);
            bootstrap_trace_u64(trace, (u64)machine->outgoing_slot);
            bootstrap_trace_u64(trace, machine->instruction_count);
            for (u32 i = 0; i < machine->instruction_count; i += 1)
            {
                MachineInstruction* row = &machine->instructions[i];
                bootstrap_trace_u64(trace, (u64)row->opcode);
                bootstrap_trace_u64(trace, (u64)row->flags);
                bootstrap_trace_u64(trace, (u64)row->payload);
                bootstrap_trace_u64(trace, (u64)row->operands[0]);
                bootstrap_trace_u64(trace, (u64)row->operands[1]);
                bootstrap_trace_u64(trace, (u64)row->operands[2]);
                bootstrap_trace_u64(trace, (u64)row->operands[3]);
            }
            bootstrap_trace_u64(trace, machine->virtual_register_count);
            for (u32 i = 0; i < machine->virtual_register_count; i += 1)
            {
                MachineVirtualRegister* row = &machine->virtual_registers[i];
                bootstrap_trace_u64(trace, (u64)row->definition_point);
                bootstrap_trace_u64(trace, (u64)row->register_class);
                bootstrap_trace_u64(trace, (u64)row->flags);
                bootstrap_trace_u64(trace, (u64)row->rematerialization_recipe);
                bootstrap_trace_u64(trace, (u64)row->typed_origin);
                bootstrap_trace_u64(trace, (u64)row->hint);
            }
            bootstrap_trace_u64(trace, machine->block_count);
            for (u32 i = 0; i < machine->block_count; i += 1)
            {
                MachineBlock* row = &machine->blocks[i];
                bootstrap_trace_u64(trace, (u64)row->first_instruction);
                bootstrap_trace_u64(trace, (u64)row->instruction_count);
                bootstrap_trace_u64(trace, (u64)row->predecessor_offset);
                bootstrap_trace_u64(trace, (u64)row->successor_offset);
                bootstrap_trace_u64(trace, (u64)row->parameter_offset);
                bootstrap_trace_u64(trace, (u64)row->predecessor_count);
                bootstrap_trace_u64(trace, (u64)row->successor_count);
                bootstrap_trace_u64(trace, (u64)row->parameter_count);
                bootstrap_trace_u64(trace, (u64)row->frequency_class);
            }
            bootstrap_trace_u64(trace, machine->edge_count);
            for (u32 i = 0; i < machine->edge_count; i += 1)
            {
                MachineEdge* row = &machine->edges[i];
                bootstrap_trace_u64(trace, (u64)row->source_block);
                bootstrap_trace_u64(trace, (u64)row->destination_block);
                bootstrap_trace_u64(trace, (u64)row->copy_offset);
                bootstrap_trace_u64(trace, (u64)row->copy_count);
                bootstrap_trace_u64(trace, (u64)row->flags);
            }
            bootstrap_trace_u64(trace, machine->block_parameter_count);
            for (u32 i = 0; i < machine->block_parameter_count; i += 1)
            {
                MachineBlockParameter* row = &machine->block_parameters[i];
                bootstrap_trace_u64(trace, (u64)row->virtual_register);
                bootstrap_trace_u64(trace, (u64)row->flags);
            }
            bootstrap_trace_u64(trace, machine->switch_case_count);
            for (u32 i = 0; i < machine->switch_case_count; i += 1)
            {
                MachineSwitchCase* row = &machine->switch_cases[i];
                bootstrap_trace_u64(trace, (u64)row->value);
                bootstrap_trace_u64(trace, (u64)row->target_block);
                bootstrap_trace_u64(trace, (u64)row->compare_width);
            }
            bootstrap_trace_u64(trace, machine->line_mark_count);
            for (u32 i = 0; i < machine->line_mark_count; i += 1)
            {
                MachineLineMark* row = &machine->line_marks[i];
                bootstrap_trace_u64(trace, (u64)row->row);
                bootstrap_trace_u64(trace, (u64)row->instruction);
            }
            bootstrap_trace_u64(trace, machine->edge_copy_source_count);
            for (u32 i = 0; i < machine->edge_copy_source_count; i += 1)
            {
                bootstrap_trace_u64(trace, (u64)machine->edge_copy_sources[i]);
            }
            bootstrap_trace_u64(trace, machine->immediate_count);
            for (u32 i = 0; i < machine->immediate_count; i += 1)
            {
                bootstrap_trace_u64(trace, (u64)machine->immediates[i]);
            }
            bootstrap_trace_u64(trace, machine->stack_slot_count);
            for (u32 i = 0; i < machine->stack_slot_count; i += 1)
            {
                bootstrap_trace_u64(trace, (u64)machine->stack_slot_sizes[i]);
            }
            for (u32 i = 0; i < machine->stack_slot_count; i += 1)
            {
                bootstrap_trace_u64(trace, machine->stack_slot_alignments ? machine->stack_slot_alignments[i] : 0);
            }
            bootstrap_trace_u64(trace, machine->call_target_count);
            for (u32 i = 0; i < machine->call_target_count; i += 1)
            {
                bootstrap_trace_u64(trace, machine->call_targets[i].value);
                bootstrap_trace_u64(trace, machine->call_target_references ? machine->call_target_references[i] : 0);
            }
            bootstrap_trace_u64(trace, machine->va_arg_count);
            for (u32 i = 0; i < machine->va_arg_count; i += 1)
            {
                MachineVaArg* row = &machine->va_args[i];
                bootstrap_trace_u64(trace, (u64)row->size);
                bootstrap_trace_u64(trace, (u64)row->alignment);
                bootstrap_trace_u64(trace, (u64)row->stack_size);
                bootstrap_trace_u64(trace, (u64)row->part_count);
                bootstrap_trace_u64(trace, (u64)row->result_slot);
                bootstrap_trace_u64(trace, (u64)row->result_is_frame);
                bootstrap_trace_u64(trace, (u64)row->scalar_size);
                for (u32 j = 0; j < MACHINE_VA_ARG_PART_LIMIT; j += 1)
                {
                    bootstrap_trace_u64(trace, (u64)row->parts[j].value_offset);
                    bootstrap_trace_u64(trace, (u64)row->parts[j].save_offset);
                    bootstrap_trace_u64(trace, (u64)row->parts[j].size);
                    bootstrap_trace_u64(trace, (u64)row->parts[j].is_float);
                    bootstrap_trace_u64(trace, (u64)row->parts[j].is_memory);
                }
            }
        }
    }
}
