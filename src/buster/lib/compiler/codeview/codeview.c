// CodeView emission from the canonical debug model: the .debug$S symbol
// and .debug$T type streams a COFF object carries for Windows debuggers,
// built from DebugModule records. codeview_emit_field_list chains bounded
// type records, and codeview_scope_walk_make indexes scopes once per build.
// ByteWriter owns bounded primitive writes; this file owns allocation/layout.
// pdb.c packages these streams into a standalone PDB at link time.

#include <buster/lib/compiler/codeview/codeview.h>
#include <buster/lib/string.h>
#include <buster/lib/byte_writer.h>

#include <string.h>

enum
{
    CV_SIGNATURE_C13 = 4,
    // Match the MSVC/LLVM maximum including the four-byte record prefix.
    CODEVIEW_MAX_RECORD_SIZE = 0xff00,
    CODEVIEW_CONTINUATION_SIZE = 8,
};

enum
{
    DEBUG_S_SYMBOLS = 0xf1,
    DEBUG_S_LINES = 0xf2,
    DEBUG_S_STRINGTABLE = 0xf3,
    DEBUG_S_FILECHKSMS = 0xf4,
};

enum
{
    S_END = 0x0006,
    S_BLOCK32 = 0x1103,
    S_CONSTANT = 0x1107,
    S_GDATA32 = 0x110d,
    S_OBJNAME = 0x1101,
    S_LOCAL = 0x113e,
    S_DEFRANGE_SUBFIELD = 0x1140,
    S_GPROC32 = 0x1110,
    S_DEFRANGE_REGISTER = 0x1141,
    S_DEFRANGE_FRAMEPOINTER_REL = 0x1142,
    S_DEFRANGE_SUBFIELD_REGISTER = 0x1143,
    S_INLINESITE = 0x114d,
    S_INLINESITE_END = 0x114e,
    S_COMPILE3 = 0x113c,
};

enum
{
    CV_LF_MODIFIER = 0x1001,
    CV_LF_POINTER = 0x1002,
    CV_LF_PROCEDURE = 0x1008,
    CV_LF_ARGLIST = 0x1201,
    CV_LF_FIELDLIST = 0x1203,
    CV_LF_INDEX = 0x1404,
    CV_LF_ENUMERATE = 0x1502,
    CV_LF_ARRAY = 0x1503,
    CV_LF_STRUCTURE = 0x1505,
    CV_LF_UNION = 0x1506,
    CV_LF_ENUM = 0x1507,
    CV_LF_ALIAS = 0x150a,
    CV_LF_MEMBER = 0x150d,
    CV_LF_ULONG = 0x8003,
};

#define CODEVIEW_LINE_STATEMENT 0x80000000u
#define CODEVIEW_LINE_NUMBER_MASK 0x00ffffffu

// Opens a subsection and returns the offset of its length field, which
// codeview_subsection_end patches once the payload size is known.
BUSTER_GLOBAL_LOCAL u64 codeview_subsection_begin(ByteWriter* buffer, u32 kind)
{
    byte_writer_align4(buffer);
    byte_writer_emit_u32_le(buffer, kind);
    u64 length_offset = buffer->count;
    byte_writer_emit_u32_le(buffer, 0);
    return length_offset;
}

BUSTER_GLOBAL_LOCAL void codeview_subsection_end(ByteWriter* buffer, u64 length_offset)
{
    byte_writer_patch_u32_le(buffer, length_offset, (u32)(buffer->count - (length_offset + 4)));
    byte_writer_align4(buffer);
}

// Opens a symbol record and returns the offset of its length field; the
// record length excludes the length field itself and includes 4-alignment
// padding, which codeview_record_end appends.
BUSTER_GLOBAL_LOCAL u64 codeview_record_begin(ByteWriter* buffer, u16 record_type)
{
    u64 length_offset = buffer->count;
    byte_writer_emit_u16_le(buffer, 0);
    byte_writer_emit_u16_le(buffer, record_type);
    return length_offset;
}

BUSTER_GLOBAL_LOCAL void codeview_record_end(ByteWriter* buffer, u64 length_offset)
{
    byte_writer_align4(buffer);
    u64 size = buffer->count - length_offset;
    if (size < 4 || size > CODEVIEW_MAX_RECORD_SIZE)
    {
        buffer->overflow = true;
    }
    else
    {
        byte_writer_patch_u16_le(buffer, length_offset, (u16)(size - 2));
    }
}

BUSTER_GLOBAL_LOCAL void codeview_emit_name(ByteWriter* buffer, String8 name)
{
    byte_writer_emit_bytes(buffer, name.pointer, name.length);
    byte_writer_emit_u8(buffer, 0);
}

BUSTER_GLOBAL_LOCAL void codeview_emit_numeric_u32(ByteWriter* buffer, u64 value)
{
    byte_writer_emit_u16_le(buffer, CV_LF_ULONG);
    byte_writer_emit_u32_le(buffer, (u32)BUSTER_MIN(value, UINT32_MAX));
}

BUSTER_GLOBAL_LOCAL u32 codeview_model_type_index(DebugModel* model, DebugTypeId type)
{
    return model && type != DEBUG_ID_INVALID && type < model->type_count ? 0x1000u + type : 0x0074u;
}

BUSTER_GLOBAL_LOCAL u32 codeview_model_simple_type(DebugType* type)
{
    u32 result;
    if (!type)
    {
        result = 0x0003;
    }
    else if (type->kind == DEBUG_TYPE_VOID)
    {
        result = 0x0003;
    }
    else if (type->kind == DEBUG_TYPE_BASE && type->name.length && (type->name.pointer[0] == 'f' || type->name.pointer[0] == 'F'))
    {
        result = type->size > 4 ? 0x0041 : 0x0040;
    }
    else if (type->size <= 1)
    {
        result = type->is_signed ? 0x0010 : 0x0020;
    }
    else if (type->size <= 2)
    {
        result = type->is_signed ? 0x0011 : 0x0021;
    }
    else if (type->size <= 4)
    {
        result = type->is_signed ? 0x0074 : 0x0075;
    }
    else
    {
        result = type->is_signed ? 0x0076 : 0x0077;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL u64 codeview_type_record_begin(ByteWriter* buffer, u16 leaf)
{
    u64 offset = buffer->count;
    byte_writer_emit_u16_le(buffer, 0);
    byte_writer_emit_u16_le(buffer, leaf);
    return offset;
}

BUSTER_GLOBAL_LOCAL void codeview_type_padding(ByteWriter* buffer)
{
    // Type records use CodeView padding leaves rather than the zero padding
    // accepted by the C13 subsection and symbol readers.  The pad bytes are
    // written in descending order: F2 F1 represents two bytes, and F3 F2
    // F1 represents three bytes.  In particular, field lists are parsed as a
    // sequence of variable-size leaves, so zeroes at the end look like a
    // truncated leaf to strict PDB readers.
    u32 padding = (u32)((4 - (buffer->count & 3)) & 3);
    for (u32 index = padding; index != 0; index -= 1)
    {
        byte_writer_emit_u8(buffer, (u8)(0xf0 + index));
    }
}

BUSTER_GLOBAL_LOCAL void codeview_type_record_end(ByteWriter* buffer, u64 offset)
{
    codeview_type_padding(buffer);
    u64 size = buffer->count - offset;
    if (size < 4 || size > CODEVIEW_MAX_RECORD_SIZE)
    {
        buffer->overflow = true;
    }
    else
    {
        byte_writer_patch_u16_le(buffer, offset, (u16)(size - 2));
    }
}

BUSTER_GLOBAL_LOCAL u32 codeview_model_register_target(u16 machine)
{
    return machine == CODEVIEW_MACHINE_ARM64 ? CPU_ARCH_AARCH64 : CPU_ARCH_X86_64;
}

BUSTER_GLOBAL_LOCAL void codeview_emit_location_range(ByteWriter* symbols, DebugLocationRange* range, u32 function_offset, u16 machine)
{
    if (!range || range->end <= range->start || range->location.kind == DEBUG_LOCATION_UNAVAILABLE)
    {
        return;
    }
    u32 start = range->start >= function_offset ? range->start - function_offset : 0;
    u32 length = range->end - range->start;
    if (range->location.kind == DEBUG_LOCATION_REGISTER)
    {
        u64 record = codeview_record_begin(symbols, S_DEFRANGE_REGISTER);
        u32 reg = debug_register_codeview_number((Target){.cpu_arch = (CpuArch)codeview_model_register_target(machine)}, range->location.reg);
        byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(reg, UINT16_MAX));
        byte_writer_emit_u16_le(symbols, 0);
        byte_writer_emit_u32_le(symbols, start);
        byte_writer_emit_u16_le(symbols, 1);
        byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(length, UINT16_MAX));
        codeview_record_end(symbols, record);
    }
    else if (range->location.kind == DEBUG_LOCATION_FRAME)
    {
        u64 record = codeview_record_begin(symbols, S_DEFRANGE_FRAMEPOINTER_REL);
        byte_writer_emit_u32_le(symbols, (u32)range->location.frame_offset);
        byte_writer_emit_u32_le(symbols, start);
        byte_writer_emit_u16_le(symbols, 1);
        byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(length, UINT16_MAX));
        codeview_record_end(symbols, record);
    }
    else if (range->location.kind == DEBUG_LOCATION_PIECEWISE)
    {
        for (u32 piece_index = 0; piece_index < range->location.piece_count; piece_index += 1)
        {
            DebugLocationPiece* piece = range->location.pieces + piece_index;
            u32 piece_start = range->start >= function_offset ? range->start - function_offset : 0;
            u32 piece_length = range->end - range->start;
            if (piece->kind == DEBUG_LOCATION_REGISTER)
            {
                u64 record = codeview_record_begin(symbols, S_DEFRANGE_SUBFIELD_REGISTER);
                u32 reg = debug_register_codeview_number((Target){.cpu_arch = (CpuArch)codeview_model_register_target(machine)}, piece->reg);
                byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(reg, UINT16_MAX));
                byte_writer_emit_u16_le(symbols, 0);
                byte_writer_emit_u32_le(symbols, piece->value_offset);
                byte_writer_emit_u32_le(symbols, piece_start);
                byte_writer_emit_u16_le(symbols, 1);
                byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(piece_length, UINT16_MAX));
                codeview_record_end(symbols, record);
            }
            else if (piece->kind == DEBUG_LOCATION_FRAME)
            {
                u64 record = codeview_record_begin(symbols, S_DEFRANGE_SUBFIELD);
                byte_writer_emit_u32_le(symbols, 0);
                byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(piece->value_offset, UINT16_MAX));
                byte_writer_emit_u32_le(symbols, piece_start);
                byte_writer_emit_u16_le(symbols, 1);
                byte_writer_emit_u16_le(symbols, (u16)BUSTER_MIN(piece_length, UINT16_MAX));
                codeview_record_end(symbols, record);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void codeview_emit_debug_variable(ByteWriter* symbols, DebugModel* model, DebugVariable* variable, u32 function_offset,
                                                      u16 machine)
{
    if (variable)
    {
        // A constant is a whole record on its own; everything else emits a local
        // followed by its location ranges.
        bool has_constant = variable->location_count && variable->locations[0].location.kind == DEBUG_LOCATION_CONSTANT;
        if (has_constant)
        {
            u64 record = codeview_record_begin(symbols, S_CONSTANT);
            byte_writer_emit_u32_le(symbols, codeview_model_type_index(model, variable->type));
            codeview_emit_numeric_u32(symbols, variable->locations[0].location.constant);
            codeview_emit_name(symbols, variable->name);
            codeview_record_end(symbols, record);
        }
        else
        {
            u64 record = codeview_record_begin(symbols, S_LOCAL);
            byte_writer_emit_u32_le(symbols, codeview_model_type_index(model, variable->type));
            byte_writer_emit_u16_le(symbols, variable->kind == DEBUG_VARIABLE_PARAMETER ? 1 : 0);
            codeview_emit_name(symbols, variable->name);
            codeview_record_end(symbols, record);
            for (u32 location_index = 0; location_index < variable->location_count; location_index += 1)
            {
                codeview_emit_location_range(symbols, variable->locations + location_index, function_offset, machine);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void codeview_emit_scope_variables(ByteWriter* symbols, DebugModel* model, DebugScope* scope, u32 function_offset,
                                                       u16 machine)
{
    if (!scope)
    {
        return;
    }
    for (u32 variable_index = 0; variable_index < scope->variable_count; variable_index += 1)
    {
        DebugVariableId id = scope->variables[variable_index];
        if (id < model->variable_count && model->variables[id].kind != DEBUG_VARIABLE_GLOBAL)
        {
            codeview_emit_debug_variable(symbols, model, model->variables + id, function_offset, machine);
        }
    }
}

typedef struct CodeviewScopeFrame CodeviewScopeFrame;
struct CodeviewScopeFrame
{
    DebugScopeId scope;
    u32 next_child;
};

typedef struct CodeviewScopeWalk CodeviewScopeWalk;
struct CodeviewScopeWalk
{
    CodeviewScopeFrame* stack;
    DebugScopeId* first_child;
    DebugScopeId* next_sibling;
};

BUSTER_GLOBAL_LOCAL CodeviewScopeWalk codeview_scope_walk_make(Arena* arena, DebugModel* model)
{
    CodeviewScopeWalk result = {
        .stack = arena_allocate(arena, CodeviewScopeFrame, model->scope_count),
        .first_child = arena_allocate(arena, DebugScopeId, model->scope_count),
        .next_sibling = arena_allocate(arena, DebugScopeId, model->scope_count),
    };
    for (u32 index = 0; index < model->scope_count; index += 1)
    {
        result.first_child[index] = DEBUG_SCOPE_INVALID;
        result.next_sibling[index] = DEBUG_SCOPE_INVALID;
    }
    // Reverse insertion retains the old walk's ascending scope-id order.
    for (u32 index = model->scope_count; index != 0;)
    {
        index -= 1;
        DebugScope* scope = model->scopes + index;
        if (scope->parent < model->scope_count && scope->parent != index && scope->kind != DEBUG_SCOPE_FUNCTION)
        {
            result.next_sibling[index] = result.first_child[scope->parent];
            result.first_child[scope->parent] = index;
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void codeview_emit_scope_tree(ByteWriter* symbols, DebugModel* model, DebugScopeId root, u32 function_offset, u16 machine,
                                                  CodeviewScopeWalk* walk)
{
    if (model && root != DEBUG_SCOPE_INVALID && root < model->scope_count)
    {
        CodeviewScopeFrame* stack = walk->stack;
        u32 stack_count = 1;
        stack[0] = (CodeviewScopeFrame){.scope = root, .next_child = walk->first_child[root]};
        while (!symbols->overflow)
        {
            CodeviewScopeFrame* frame = stack + stack_count - 1;
            DebugScopeId child = frame->next_child;
            if (child != DEBUG_SCOPE_INVALID)
            {
                frame->next_child = walk->next_sibling[child];
                if (stack_count == model->scope_count)
                {
                    symbols->overflow = true;
                    break;
                }
                DebugScope* scope = model->scopes + child;
                u64 block = codeview_record_begin(symbols, S_BLOCK32);
                byte_writer_emit_u32_le(symbols, 0);
                byte_writer_emit_u32_le(symbols, 0);
                byte_writer_emit_u32_le(symbols, scope->end > scope->start ? scope->end - scope->start : 1);
                byte_writer_emit_u32_le(symbols, scope->start >= function_offset ? scope->start - function_offset : 0);
                byte_writer_emit_u16_le(symbols, 1);
                codeview_emit_name(symbols, S8("scope"));
                codeview_record_end(symbols, block);
                codeview_emit_scope_variables(symbols, model, scope, function_offset, machine);
                stack[stack_count++] = (CodeviewScopeFrame){.scope = child, .next_child = walk->first_child[child]};
            }
            else if (stack_count == 1)
            {
                break;
            }
            else
            {
                u64 end = codeview_record_begin(symbols, S_END);
                codeview_record_end(symbols, end);
                stack_count -= 1;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void codeview_emit_global_variable(ByteWriter* symbols, DebugModel* model, DebugVariable* variable,
                                                       CodeviewRelocation* relocations, u32* relocation_count)
{
    if (!variable)
    {
        return;
    }
    u64 record = codeview_record_begin(symbols, S_GDATA32);
    byte_writer_emit_u32_le(symbols, codeview_model_type_index(model, variable->type));
    if (relocations && relocation_count && *relocation_count < UINT32_MAX)
    {
        relocations[*relocation_count] = (CodeviewRelocation){
            .offset = symbols->count,
            .function = UINT32_MAX,
            .kind = CODEVIEW_RELOCATION_SECREL32,
            .symbol_name = variable->linkage_name.length ? variable->linkage_name : variable->name,
        };
        *relocation_count += 1;
    }
    byte_writer_emit_u32_le(symbols, 0);
    if (relocations && relocation_count && *relocation_count < UINT32_MAX)
    {
        relocations[*relocation_count] = (CodeviewRelocation){
            .offset = symbols->count,
            .function = UINT32_MAX,
            .kind = CODEVIEW_RELOCATION_SECTION16,
            .symbol_name = variable->linkage_name.length ? variable->linkage_name : variable->name,
        };
        *relocation_count += 1;
    }
    byte_writer_emit_u16_le(symbols, 1);
    codeview_emit_name(symbols, variable->name);
    codeview_record_end(symbols, record);
}

// Emit the tail segment first, so LF_INDEX always refers to an already
// emitted record. Segments retain ascending member order; only their physical
// order is reversed. A primary type's field-list reference is patched later.
BUSTER_GLOBAL_LOCAL u32 codeview_emit_field_list(ByteWriter* types, DebugModel* model, DebugType* type, u32* next_index)
{
    u32 end = type->kind == DEBUG_TYPE_ENUM ? type->enum_member_count : type->field_count;
    u32 continuation = 0;
    do
    {
        u32 begin = end;
        u64 size = 4 + CODEVIEW_CONTINUATION_SIZE;
        while (begin && !types->overflow)
        {
            String8 name = type->kind == DEBUG_TYPE_ENUM ? type->enum_members[begin - 1].name : type->fields[begin - 1].name;
            // Leaf, attributes, optional type index, LF_ULONG, value, name/NUL.
            u64 prefix = type->kind == DEBUG_TYPE_ENUM ? 11u : 15u;
            if (name.length > CODEVIEW_MAX_RECORD_SIZE - 4 - CODEVIEW_CONTINUATION_SIZE - prefix)
            {
                types->overflow = true;
                break;
            }
            u64 member_size = (prefix + name.length + 3) & ~(u64)3;
            if (member_size > CODEVIEW_MAX_RECORD_SIZE - size)
            {
                break;
            }
            size += member_size;
            begin -= 1;
        }
        if (types->overflow || (begin == end && end))
        {
            types->overflow = true;
            break;
        }
        u64 record = codeview_type_record_begin(types, CV_LF_FIELDLIST);
        for (u32 index = begin; index < end; index += 1)
        {
            if (type->kind == DEBUG_TYPE_ENUM)
            {
                DebugEnumMember* member = type->enum_members + index;
                byte_writer_emit_u16_le(types, CV_LF_ENUMERATE);
                byte_writer_emit_u16_le(types, 0);
                codeview_emit_numeric_u32(types, member->value);
                codeview_emit_name(types, member->name);
            }
            else
            {
                DebugTypeField* field = type->fields + index;
                byte_writer_emit_u16_le(types, CV_LF_MEMBER);
                byte_writer_emit_u16_le(types, 0);
                byte_writer_emit_u32_le(types, codeview_model_type_index(model, field->type));
                codeview_emit_numeric_u32(types, field->offset);
                codeview_emit_name(types, field->name);
            }
            // Padding belongs to every member, not just to the enclosing list.
            codeview_type_padding(types);
        }
        if (continuation)
        {
            byte_writer_emit_u16_le(types, CV_LF_INDEX);
            byte_writer_emit_u16_le(types, 0);
            byte_writer_emit_u32_le(types, continuation);
        }
        codeview_type_record_end(types, record);
        continuation = *next_index;
        *next_index += 1;
        end = begin;
    } while (end && !types->overflow);
    return continuation;
}

BUSTER_GLOBAL_LOCAL void codeview_emit_model_types(ByteWriter* types, DebugModel* model, u64* auxiliary_offsets)
{
    for (u32 type_index = 0; type_index < model->type_count && !types->overflow; type_index += 1)
    {
        DebugType* type = model->types + type_index;
        u64 record = codeview_type_record_begin(types, type->kind == DEBUG_TYPE_POINTER ? CV_LF_POINTER
                                                                                           : type->kind == DEBUG_TYPE_ARRAY || type->kind == DEBUG_TYPE_VECTOR ? CV_LF_ARRAY
                                                                                           : type->kind == DEBUG_TYPE_STRUCT ? CV_LF_STRUCTURE
                                                                                           : type->kind == DEBUG_TYPE_UNION ? CV_LF_UNION
                                                                                           : type->kind == DEBUG_TYPE_ENUM ? CV_LF_ENUM
                                                                                           : type->kind == DEBUG_TYPE_TYPEDEF ? CV_LF_ALIAS
                                                                                           : type->kind == DEBUG_TYPE_FUNCTION ? CV_LF_PROCEDURE
                                                                                                                                  : CV_LF_MODIFIER);
        if (type->kind == DEBUG_TYPE_POINTER)
        {
            byte_writer_emit_u32_le(types, codeview_model_type_index(model, type->element_type));
            // PointerKind 0x0c is the 64-bit near pointer used by both
            // supported native CodeView targets.
            byte_writer_emit_u32_le(types, 0x0c);
        }
        else if (type->kind == DEBUG_TYPE_ARRAY || type->kind == DEBUG_TYPE_VECTOR)
        {
            byte_writer_emit_u32_le(types, codeview_model_type_index(model, type->element_type));
            byte_writer_emit_u32_le(types, 0x0074);
            codeview_emit_numeric_u32(types, type->element_count);
            codeview_emit_name(types, type->name);
        }
        else if (type->kind == DEBUG_TYPE_STRUCT || type->kind == DEBUG_TYPE_UNION)
        {
            types->overflow |= type->field_count > UINT16_MAX;
            byte_writer_emit_u16_le(types, (u16)type->field_count);
            byte_writer_emit_u16_le(types, 0);
            auxiliary_offsets[type_index] = types->count;
            byte_writer_emit_u32_le(types, 0);
            if (type->kind == DEBUG_TYPE_STRUCT)
            {
                byte_writer_emit_u32_le(types, 0);
                byte_writer_emit_u32_le(types, 0);
            }
            codeview_emit_numeric_u32(types, type->size);
            codeview_emit_name(types, type->name);
        }
        else if (type->kind == DEBUG_TYPE_ENUM)
        {
            types->overflow |= type->enum_member_count > UINT16_MAX;
            byte_writer_emit_u16_le(types, (u16)type->enum_member_count);
            byte_writer_emit_u16_le(types, 0);
            byte_writer_emit_u32_le(types, codeview_model_type_index(model, DEBUG_ID_INVALID));
            auxiliary_offsets[type_index] = types->count;
            byte_writer_emit_u32_le(types, 0);
            codeview_emit_name(types, type->name);
        }
        else if (type->kind == DEBUG_TYPE_TYPEDEF)
        {
            byte_writer_emit_u32_le(types, codeview_model_type_index(model, type->element_type));
            codeview_emit_name(types, type->name);
        }
        else if (type->kind == DEBUG_TYPE_FUNCTION)
        {
            byte_writer_emit_u32_le(types, codeview_model_type_index(model, type->return_type));
            byte_writer_emit_u8(types, 0);
            byte_writer_emit_u8(types, type->is_variadic ? 1 : 0);
            types->overflow |= type->parameter_count > UINT16_MAX;
            byte_writer_emit_u16_le(types, (u16)type->parameter_count);
            auxiliary_offsets[type_index] = types->count;
            byte_writer_emit_u32_le(types, 0);
        }
        else
        {
            u32 referent = type->kind == DEBUG_TYPE_QUALIFIED
                               ? codeview_model_type_index(model, type->unqualified_type != DEBUG_ID_INVALID ? type->unqualified_type : type->element_type)
                               : codeview_model_simple_type(type);
            byte_writer_emit_u32_le(types, referent);
            u16 attributes = (u16)((type->is_const ? 1 : 0) | (type->is_volatile ? 2 : 0));
            byte_writer_emit_u16_le(types, attributes);
        }
        codeview_type_record_end(types, record);
    }
    u32 next_index = 0x1000u + model->type_count;
    for (u32 type_index = 0; type_index < model->type_count && !types->overflow; type_index += 1)
    {
        DebugType* type = model->types + type_index;
        if (type->kind == DEBUG_TYPE_STRUCT || type->kind == DEBUG_TYPE_UNION || type->kind == DEBUG_TYPE_ENUM)
        {
            u32 field_index = codeview_emit_field_list(types, model, type, &next_index);
            byte_writer_patch_u32_le(types, auxiliary_offsets[type_index], field_index);
        }
        else if (type->kind == DEBUG_TYPE_FUNCTION)
        {
            byte_writer_patch_u32_le(types, auxiliary_offsets[type_index], next_index++);
            u64 record = codeview_type_record_begin(types, CV_LF_ARGLIST);
            byte_writer_emit_u32_le(types, type->parameter_count);
            for (u32 parameter_index = 0; parameter_index < type->parameter_count; parameter_index += 1)
            {
                byte_writer_emit_u32_le(types, codeview_model_type_index(model, type->parameter_types[parameter_index]));
            }
            codeview_type_record_end(types, record);
        }
    }
}

CodeviewResult codeview_build_legacy(Arena* arena, CodeviewInput input)
{
    CodeviewResult result = {0};
    if (arena && input.file_count && input.file_paths && (!input.function_count || input.functions) && (!input.line_count || input.lines))
    {
        for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
        {
            if (input.functions[function_index].file >= input.file_count)
            {
                return result;
            }
        }
        u32 previous_offset = 0;
        for (u32 line_index = 0; line_index < input.line_count; line_index += 1)
        {
            DwarfLineEntry* entry = input.lines + line_index;
            if (entry->file >= input.file_count || entry->code_offset < previous_offset)
            {
                return result;
            }
            previous_offset = entry->code_offset;
        }
        u64 path_bytes = 0;
        for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
        {
            path_bytes += input.file_paths[file_index].length + 1;
        }
        u64 name_bytes = 0;
        for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
        {
            name_bytes += input.functions[function_index].name.length + 1;
        }
        u64 symbol_capacity = 256 + input.producer.length * 2 + name_bytes + (u64)input.function_count * 96 + (u64)input.line_count * 24 +
                              (u64)input.file_count * 24 + path_bytes;
        if (input.model && input.model->valid)
        {
            symbol_capacity += (u64)input.model->variable_count * 192 + (u64)input.model->scope_count * 96 +
                               (u64)input.model->inline_site_count * 64 + 256;
        }
        ByteWriter symbols = byte_writer_make(arena_allocate(arena, u8, symbol_capacity), symbol_capacity);
        u64 relocation_capacity = (u64)input.function_count * 4;
        if (input.model && input.model->valid)
        {
            relocation_capacity += (u64)input.model->variable_count * 2;
        }
        result.relocations = arena_allocate(arena, CodeviewRelocation, relocation_capacity);
        byte_writer_emit_u32_le(&symbols, CV_SIGNATURE_C13);

        // Translation-unit records: object name and compiler description.
        u64 unit_symbols = codeview_subsection_begin(&symbols, DEBUG_S_SYMBOLS);
        u64 objname = codeview_record_begin(&symbols, S_OBJNAME);
        byte_writer_emit_u32_le(&symbols, 0);
        byte_writer_emit_bytes(&symbols, input.file_paths[0].pointer, input.file_paths[0].length);
        byte_writer_emit_u8(&symbols, 0);
        codeview_record_end(&symbols, objname);
        u64 compile3 = codeview_record_begin(&symbols, S_COMPILE3);
        byte_writer_emit_u32_le(&symbols, 0);
        byte_writer_emit_u16_le(&symbols, input.machine);
        for (u32 version_index = 0; version_index < 8; version_index += 1)
        {
            byte_writer_emit_u16_le(&symbols, 0);
        }
        byte_writer_emit_bytes(&symbols, input.producer.pointer, input.producer.length);
        byte_writer_emit_u8(&symbols, 0);
        codeview_record_end(&symbols, compile3);
        codeview_subsection_end(&symbols, unit_symbols);

        if (input.model && input.model->valid)
        {
            u64 globals = codeview_subsection_begin(&symbols, DEBUG_S_SYMBOLS);
            for (u32 variable_index = 0; variable_index < input.model->variable_count; variable_index += 1)
            {
                DebugVariable* variable = input.model->variables + variable_index;
                if (variable->kind == DEBUG_VARIABLE_GLOBAL)
                {
                    codeview_emit_global_variable(&symbols, input.model, variable, result.relocations, &result.relocation_count);
                }
            }
            codeview_subsection_end(&symbols, globals);
        }

        // One scratch stack and one child index for the whole module, not a
        // scope_count-sized bump allocation (and full-model scan) per function.
        CodeviewScopeWalk scope_walk = {0};
        if (input.model && input.model->valid && input.model->scope_count && input.function_count)
        {
            scope_walk = codeview_scope_walk_make(arena, input.model);
        }

        // One symbols subsection and one lines subsection per function.
        u32 line_cursor = 0;
        for (u32 function_index = 0; function_index < input.function_count; function_index += 1)
        {
            DwarfFunction* function = input.functions + function_index;
            u64 function_symbols = codeview_subsection_begin(&symbols, DEBUG_S_SYMBOLS);
            u64 procedure = codeview_record_begin(&symbols, S_GPROC32);
            byte_writer_emit_u32_le(&symbols, 0);
            u64 end_pointer_offset = symbols.count;
            byte_writer_emit_u32_le(&symbols, 0);
            byte_writer_emit_u32_le(&symbols, 0);
            byte_writer_emit_u32_le(&symbols, function->code_size);
            byte_writer_emit_u32_le(&symbols, 0);
            byte_writer_emit_u32_le(&symbols, function->code_size);
            u32 function_type = 0;
            if (input.model && input.model->valid && function_index < input.model->function_count)
            {
                function_type = codeview_model_type_index(input.model, input.model->functions[function_index].type);
            }
            byte_writer_emit_u32_le(&symbols, function_type);
            result.relocations[result.relocation_count++] = (CodeviewRelocation){
                .offset = symbols.count,
                .function = function_index,
                .kind = CODEVIEW_RELOCATION_SECREL32,
            };
            byte_writer_emit_u32_le(&symbols, 0);
            result.relocations[result.relocation_count++] = (CodeviewRelocation){
                .offset = symbols.count,
                .function = function_index,
                .kind = CODEVIEW_RELOCATION_SECTION16,
            };
            byte_writer_emit_u16_le(&symbols, 0);
            byte_writer_emit_u8(&symbols, 0);
            byte_writer_emit_bytes(&symbols, function->name.pointer, function->name.length);
            byte_writer_emit_u8(&symbols, 0);
            codeview_record_end(&symbols, procedure);

            if (input.model && input.model->valid && function_index < input.model->function_count)
            {
                DebugFunction* debug_function = input.model->functions + function_index;
                if (debug_function->scope < input.model->scope_count)
                {
                    codeview_emit_scope_variables(&symbols, input.model, input.model->scopes + debug_function->scope, function->code_offset, input.machine);
                    codeview_emit_scope_tree(&symbols, input.model, debug_function->scope, function->code_offset, input.machine, &scope_walk);
                }
                for (u32 inline_index = 0; inline_index < input.model->inline_site_count; inline_index += 1)
                {
                    DebugInlineSite* site = input.model->inline_sites + inline_index;
                    if (site->function != debug_function)
                    {
                        continue;
                    }
                    u64 inline_record = codeview_record_begin(&symbols, S_INLINESITE);
                    byte_writer_emit_u32_le(&symbols, 0);
                    byte_writer_emit_u32_le(&symbols, 0);
                    byte_writer_emit_u32_le(&symbols, 0x1000u + (u32)(debug_function - input.model->functions));
                    codeview_record_end(&symbols, inline_record);
                    u64 inline_end = codeview_record_begin(&symbols, S_INLINESITE_END);
                    codeview_record_end(&symbols, inline_end);
                }
            }
            u64 end_record = symbols.count;
            u64 end_marker = codeview_record_begin(&symbols, S_END);
            codeview_record_end(&symbols, end_marker);
            if (end_record <= UINT32_MAX)
            {
                byte_writer_patch_u32_le(&symbols, end_pointer_offset, (u32)end_record);
            }
            codeview_subsection_end(&symbols, function_symbols);

            u64 function_lines = codeview_subsection_begin(&symbols, DEBUG_S_LINES);
            result.relocations[result.relocation_count++] = (CodeviewRelocation){
                .offset = symbols.count,
                .function = function_index,
                .kind = CODEVIEW_RELOCATION_SECREL32,
            };
            byte_writer_emit_u32_le(&symbols, 0);
            result.relocations[result.relocation_count++] = (CodeviewRelocation){
                .offset = symbols.count,
                .function = function_index,
                .kind = CODEVIEW_RELOCATION_SECTION16,
            };
            byte_writer_emit_u16_le(&symbols, 0);
            byte_writer_emit_u16_le(&symbols, 0);
            byte_writer_emit_u32_le(&symbols, function->code_size);
            u32 function_end = function->code_offset + function->code_size;
            while (line_cursor < input.line_count && input.lines[line_cursor].code_offset < function->code_offset)
            {
                line_cursor += 1;
            }
            while (line_cursor < input.line_count && input.lines[line_cursor].code_offset < function_end)
            {
                u32 run_file = input.lines[line_cursor].file;
                u64 block_start = symbols.count;
                byte_writer_emit_u32_le(&symbols, run_file * 8);
                u64 count_offset = symbols.count;
                byte_writer_emit_u32_le(&symbols, 0);
                byte_writer_emit_u32_le(&symbols, 0);
                u32 run_lines = 0;
                while (line_cursor < input.line_count && input.lines[line_cursor].code_offset < function_end && input.lines[line_cursor].file == run_file)
                {
                    DwarfLineEntry* entry = input.lines + line_cursor;
                    line_cursor += 1;
                    if (!entry->line)
                    {
                        continue;
                    }
                    byte_writer_emit_u32_le(&symbols, entry->code_offset - function->code_offset);
                    byte_writer_emit_u32_le(&symbols, (entry->line & CODEVIEW_LINE_NUMBER_MASK) | CODEVIEW_LINE_STATEMENT);
                    run_lines += 1;
                }
                byte_writer_patch_u32_le(&symbols, count_offset, run_lines);
                byte_writer_patch_u32_le(&symbols, count_offset + 4, (u32)(symbols.count - block_start));
            }
            codeview_subsection_end(&symbols, function_lines);
        }

        // File checksum table (checksum kind "none") and the string table it
        // references; line blocks index checksums by byte offset (8 per file).
        u64 checksums = codeview_subsection_begin(&symbols, DEBUG_S_FILECHKSMS);
        for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
        {
            byte_writer_emit_u32_le(&symbols, 1 + (u32)file_index);
            byte_writer_emit_u8(&symbols, 0);
            byte_writer_emit_u8(&symbols, 0);
            byte_writer_emit_u16_le(&symbols, 0);
        }
        codeview_subsection_end(&symbols, checksums);
        u64 string_table = codeview_subsection_begin(&symbols, DEBUG_S_STRINGTABLE);
        u64 string_base = symbols.count;
        byte_writer_emit_u8(&symbols, 0);
        u32* string_offsets = arena_allocate(arena, u32, input.file_count);
        for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
        {
            string_offsets[file_index] = (u32)(symbols.count - string_base);
            byte_writer_emit_bytes(&symbols, input.file_paths[file_index].pointer, input.file_paths[file_index].length);
            byte_writer_emit_u8(&symbols, 0);
        }
        codeview_subsection_end(&symbols, string_table);
        // Patch the checksum entries now that string offsets are known.
        for (u32 file_index = 0; file_index < input.file_count; file_index += 1)
        {
            byte_writer_patch_u32_le(&symbols, checksums + 4 + (u64)file_index * 8, string_offsets[file_index]);
        }

        u64 type_capacity = 4;
        if (input.model && input.model->valid)
        {
            type_capacity += 128 + (u64)input.model->type_count * 96 + (u64)input.model->variable_count * 32;
            for (u32 type_index = 0; type_index < input.model->type_count; type_index += 1)
            {
                DebugType* type = input.model->types + type_index;
                type_capacity += type->name.length + 1 + (u64)type->field_count * 48 + (u64)type->enum_member_count * 32 +
                                 (u64)type->parameter_count * 8;
                for (u32 field = 0; field < type->field_count; field += 1)
                {
                    type_capacity += type->fields[field].name.length + 1;
                }
                for (u32 member = 0; member < type->enum_member_count; member += 1)
                {
                    type_capacity += type->enum_members[member].name.length + 1;
                }
            }
        }
        ByteWriter types = byte_writer_make(arena_allocate(arena, u8, type_capacity), type_capacity);
        byte_writer_emit_u32_le(&types, CV_SIGNATURE_C13);
        if (input.model && input.model->valid)
        {
            u64* auxiliary_offsets = arena_allocate(arena, u64, input.model->type_count ? input.model->type_count : 1);
            codeview_emit_model_types(&types, input.model, auxiliary_offsets);
        }
        if (!symbols.overflow && !types.overflow && symbols.count <= UINT32_MAX)
        {
            result.valid = byte_writer_commit(&symbols, &result.symbols) && byte_writer_commit(&types, &result.types);
        }
    }

    return result;
}

CodeviewResult codeview_build(Arena* arena, CodeviewInput input)
{
    return codeview_build_legacy(arena, input);
}
