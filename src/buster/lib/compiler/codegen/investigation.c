// One-function lowering captures: investigation_record snapshots retained MIR
// emission intervals and canonical selection anchors, without frontend pointers.
// investigation_bind_object resolves the serialized ELF section; serialize/read
// own the bounded little-endian v1 format; investigation_command validates the
// complete artifact identity before reporting offsets. Anchors describe selection
// context, never ownership of every byte between them.
#include <buster/lib/compiler/codegen/investigation.h>
#include <buster/lib/byte_writer.h>
#include <buster/lib/file.h>
#include <buster/lib/string.h>
#include <buster/lib/time.h>

#define BUSTER_INVESTIGATION_MAGIC S8("BSTRINV1")
#define BUSTER_INVESTIGATION_END 0x31444e45u
#define BUSTER_INVESTIGATION_ROW_BYTES 12u
#define BUSTER_INVESTIGATION_MARK_BYTES 64u
#define BUSTER_INVESTIGATION_FIXED_BYTES (8u + 4u + 7u * 4u + 3u * 64u + 3u * 8u + 10u * 4u + 4u)
#define BUSTER_INVESTIGATION_HEX_BYTES 64u
#define BUSTER_INVESTIGATION_DISASSEMBLY_BYTES 256u
#define BUSTER_INVESTIGATION_REPORT_ROW_LIMIT 64u

typedef struct InvestigationReader InvestigationReader;
struct InvestigationReader
{
    ByteSlice bytes;
    u64 offset;
    bool invalid;
};

typedef struct InvestigationElfSection InvestigationElfSection;
struct InvestigationElfSection
{
    u64 offset;
    u64 size;
    bool valid;
};

String8 investigation_compiler_revision(void)
{
#if defined(BUSTER_INVESTIGATION_REVISION)
    String8 result = S8(BUSTER_INVESTIGATION_REVISION);
#else
    String8 result = {0};
#endif
    return result;
}

void investigation_digest(ByteSlice bytes, char8 result[SHA256_HEX_CAPACITY])
{
    Sha256 hash;
    sha256_init(&hash);
    sha256_add(&hash, bytes.pointer, bytes.length);
    sha256_finish_hex(&hash, result);
}

BUSTER_GLOBAL_LOCAL bool investigation_span(ByteSlice bytes, u64 offset, u64 size)
{
    bool result = (!bytes.length || bytes.pointer) && offset <= bytes.length && size <= bytes.length - offset;
    return result;
}

BUSTER_GLOBAL_LOCAL bool investigation_text_valid(String8 text)
{
    bool result = text.length <= INVESTIGATION_TEXT_LIMIT && (!text.length || text.pointer);
    for (u64 index = 0; result && index < text.length; index += 1)
    {
        result = text.pointer[index] != 0;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool investigation_hex_valid(String8 text, u32 length, bool lower_only)
{
    bool result = text.length == length && text.pointer;
    for (u64 index = 0; result && index < text.length; index += 1)
    {
        char8 byte = text.pointer[index];
        result = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') ||
                 (!lower_only && byte >= 'A' && byte <= 'F');
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool investigation_revision_equal(String8 first, String8 second)
{
    bool result = investigation_hex_valid(first, 40, false) && investigation_hex_valid(second, 40, false);
    for (u64 index = 0; result && index < first.length; index += 1)
    {
        char8 left = first.pointer[index];
        char8 right = second.pointer[index];
        left = left >= 'A' && left <= 'F' ? (char8)(left + ('a' - 'A')) : left;
        right = right >= 'A' && right <= 'F' ? (char8)(right + ('a' - 'A')) : right;
        result = left == right;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool investigation_size_add(u64* size, u64 count)
{
    bool result = *size <= INVESTIGATION_CAPTURE_BYTE_LIMIT && count <= INVESTIGATION_CAPTURE_BYTE_LIMIT - *size;
    if (result)
    {
        *size += count;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 investigation_source_path(IrProgram* program, IrSourcePosition position)
{
    String8 result = {0};
    if (position.line && position.source < program->sources.count && program->sources.sources)
    {
        result = program->sources.sources[position.source].path;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool investigation_capture_shape(InvestigationCapture const* capture, u64* size_out, bool manifest)
{
    String8 texts[] = {capture->function_name, capture->revision, capture->configuration, capture->input_path,
                       capture->artifact_path, capture->section_name, capture->target};
    u64 size = BUSTER_INVESTIGATION_FIXED_BYTES;
    bool result = capture->found && capture->code.length <= INVESTIGATION_FUNCTION_BYTE_LIMIT &&
                  (!capture->code.length || capture->code.pointer) && capture->row_count <= INVESTIGATION_ROW_LIMIT &&
                  capture->mark_count <= INVESTIGATION_MARK_LIMIT && (!capture->row_count || capture->rows) &&
                  (!capture->mark_count || capture->marks);
    for (u32 index = 0; result && index < BUSTER_ARRAY_LENGTH(texts); index += 1)
    {
        result = investigation_text_valid(texts[index]) && investigation_size_add(&size, texts[index].length);
    }
    result = result && (!capture->revision.length || investigation_hex_valid(capture->revision, 40, false));
    if (result && manifest)
    {
        result = capture->function_name.length && capture->configuration.length && capture->input_path.length &&
                 capture->artifact_path.length && capture->section_name.length && capture->target.length &&
                 investigation_hex_valid((String8){(char8*)capture->input_sha256, 64}, 64, true) &&
                 investigation_hex_valid((String8){(char8*)capture->translation_sha256, 64}, 64, true) &&
                 investigation_hex_valid((String8){(char8*)capture->artifact_sha256, 64}, 64, true);
    }
    result = result && investigation_size_add(&size, capture->code.length) &&
             investigation_size_add(&size, (u64)capture->row_count * BUSTER_INVESTIGATION_ROW_BYTES) &&
             investigation_size_add(&size, (u64)capture->mark_count * BUSTER_INVESTIGATION_MARK_BYTES);
    for (u32 index = 0; result && index < capture->row_count; index += 1)
    {
        InvestigationRow const* row = capture->rows + index;
        result = row->offset <= row->end && row->end <= capture->code.length &&
                 (!index || row->offset == capture->rows[index - 1u].end) &&
                 (index + 1u != capture->row_count || row->end == capture->code.length);
    }
    for (u32 index = 0; result && index < capture->mark_count; index += 1)
    {
        InvestigationMark const* mark = capture->marks + index;
        result = mark->row <= capture->row_count && (!index || mark->row >= capture->marks[index - 1u].row) &&
                 investigation_text_valid(mark->path) && investigation_text_valid(mark->original_path) &&
                 investigation_size_add(&size, mark->path.length) && investigation_size_add(&size, mark->original_path.length);
    }
    if (result && size_out)
    {
        *size_out = size;
    }
    return result;
}

void investigation_record(Arena* arena, InvestigationCapture* capture, IrProgram* program, IrFunction* ir_function,
                          MachineFunction const* machine, u32 const* row_offsets, u8 const* bytes, u32 byte_count, u32 code_base)
{
    if (capture && ir_function && string_equal(capture->function_name, ir_function->name))
    {
        TimeDataType start = timestamp_take();
        capture->found = false;
        bool valid = arena && program && machine && byte_count <= INVESTIGATION_FUNCTION_BYTE_LIMIT && (!byte_count || bytes) &&
                     machine->instruction_count <= INVESTIGATION_ROW_LIMIT && machine->line_mark_count <= INVESTIGATION_MARK_LIMIT &&
                     (!machine->instruction_count || (row_offsets && machine->instructions)) &&
                     (!machine->line_mark_count || machine->line_marks);
        if (valid)
        {
            capture->function = ir_function->id.value;
            capture->code_base = code_base;
            // These IDs are published after canonical preparation and selection;
            // the capture does not retain the history of individual transforms.
            capture->transformed = true;
            capture->row_count = machine->instruction_count;
            capture->mark_count = machine->line_mark_count;
            capture->rows = arena_allocate(arena, InvestigationRow, capture->row_count);
            capture->marks = arena_allocate_zeroed(arena, InvestigationMark, capture->mark_count);
            capture->code = (ByteSlice){.pointer = (u8*)bytes, .length = byte_count};
            for (u32 index = 0; valid && index < capture->row_count; index += 1)
            {
                u32 end = index + 1u < capture->row_count ? row_offsets[index + 1u] : byte_count;
                valid = row_offsets[index] <= end && end <= byte_count;
                capture->rows[index] = (InvestigationRow){row_offsets[index], end, machine->instructions[index].opcode};
            }
            for (u32 index = 0; valid && index < capture->mark_count; index += 1)
            {
                MachineLineMark anchor = machine->line_marks[index];
                valid = anchor.row <= capture->row_count && anchor.instruction < ir_function->instruction_count &&
                        ir_function->instructions && (!index || anchor.row >= machine->line_marks[index - 1u].row);
                if (valid)
                {
                    InvestigationMark* mark = capture->marks + index;
                    mark->row = anchor.row;
                    mark->instruction = anchor.instruction;
                    mark->opcode = ir_function->instructions[anchor.instruction].opcode;
                    mark->range.source = IR_SOURCE_ID_INVALID;
                    if (ir_function->instruction_canonical_sources)
                    {
                        mark->range = ir_instruction_canonical_source(ir_function, (IrInstructionId){.value = anchor.instruction});
                        if (mark->range.source.value != IR_ID_UNDERLYING_INVALID)
                        {
                            mark->position = ir_source_position(program, mark->range);
                            mark->original_position = program->source_map.count
                                                          ? ir_source_map_original_position(&program->source_map, mark->range.offset)
                                                          : mark->position;
                            mark->path = investigation_source_path(program, mark->position);
                            mark->original_path = investigation_source_path(program, mark->original_position);
                        }
                    }
                }
            }
            capture->found = valid;
            valid = valid && investigation_capture_shape(capture, 0, false);
            if (valid)
            {
                for (u32 index = 0; index < capture->mark_count; index += 1)
                {
                    InvestigationMark* mark = capture->marks + index;
                    mark->path = string_duplicate_arena(arena, mark->path, false);
                    mark->original_path = string_duplicate_arena(arena, mark->original_path, false);
                }
                u8* owned = arena_allocate(arena, u8, byte_count);
                if (byte_count)
                {
                    memcpy(owned, bytes, byte_count);
                }
                capture->code.pointer = owned;
                capture->diagnostic = (String8){0};
            }
        }
        if (!valid)
        {
            capture->found = false;
            capture->diagnostic = S8("investigation capture exceeds its limits or has invalid MIR anchors/offsets");
        }
        TimeDataType end = timestamp_take();
        capture->capture_ns += timestamp_ns_between(start, end);
    }
}

BUSTER_GLOBAL_LOCAL u64 investigation_read_scalar(InvestigationReader* reader, u32 width)
{
    u64 result = 0;
    if (!reader->invalid && investigation_span(reader->bytes, reader->offset, width))
    {
        for (u32 index = 0; index < width; index += 1)
        {
            result |= (u64)reader->bytes.pointer[reader->offset + index] << (index * 8u);
        }
        reader->offset += width;
    }
    else
    {
        reader->invalid = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 investigation_read_text(InvestigationReader* reader)
{
    String8 result = {0};
    u64 length = investigation_read_scalar(reader, 4);
    if (!reader->invalid && length <= INVESTIGATION_TEXT_LIMIT && investigation_span(reader->bytes, reader->offset, length))
    {
        result = (String8){(char8*)reader->bytes.pointer + reader->offset, length};
        reader->offset += length;
    }
    else
    {
        reader->invalid = true;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool investigation_read_digest(InvestigationReader* reader, char8 digest[SHA256_HEX_CAPACITY])
{
    bool result = !reader->invalid && investigation_span(reader->bytes, reader->offset, 64);
    if (result)
    {
        memcpy(digest, reader->bytes.pointer + reader->offset, 64);
        digest[64] = 0;
        reader->offset += 64;
        result = investigation_hex_valid((String8){digest, 64}, 64, true);
    }
    reader->invalid = reader->invalid || !result;
    return result;
}

BUSTER_GLOBAL_LOCAL IrSourcePosition investigation_read_position(InvestigationReader* reader)
{
    IrSourcePosition result;
    result.source = (u32)investigation_read_scalar(reader, 4);
    result.offset = (u32)investigation_read_scalar(reader, 4);
    result.line = (u32)investigation_read_scalar(reader, 4);
    result.column = (u32)investigation_read_scalar(reader, 4);
    return result;
}

BUSTER_GLOBAL_LOCAL void investigation_emit_u64(ByteWriter* writer, u64 value)
{
    byte_writer_emit_u32_le(writer, (u32)value);
    byte_writer_emit_u32_le(writer, (u32)(value >> 32u));
}

BUSTER_GLOBAL_LOCAL void investigation_emit_text(ByteWriter* writer, String8 value)
{
    byte_writer_emit_u32_le(writer, (u32)value.length);
    byte_writer_emit_bytes(writer, value.pointer, value.length);
}

BUSTER_GLOBAL_LOCAL void investigation_emit_position(ByteWriter* writer, IrSourcePosition position)
{
    byte_writer_emit_u32_le(writer, position.source);
    byte_writer_emit_u32_le(writer, position.offset);
    byte_writer_emit_u32_le(writer, position.line);
    byte_writer_emit_u32_le(writer, position.column);
}

ByteSlice investigation_serialize(Arena* arena, InvestigationCapture const* capture)
{
    ByteSlice result = {0};
    u64 size = 0;
    if (arena && capture && investigation_capture_shape(capture, &size, true))
    {
        ByteWriter writer = byte_writer_make(arena_allocate(arena, u8, size), size);
        String8 magic = BUSTER_INVESTIGATION_MAGIC;
        byte_writer_emit_bytes(&writer, magic.pointer, magic.length);
        byte_writer_emit_u32_le(&writer, INVESTIGATION_SCHEMA_VERSION);
        investigation_emit_text(&writer, capture->function_name);
        investigation_emit_text(&writer, capture->revision);
        investigation_emit_text(&writer, capture->configuration);
        investigation_emit_text(&writer, capture->input_path);
        investigation_emit_text(&writer, capture->artifact_path);
        investigation_emit_text(&writer, capture->section_name);
        investigation_emit_text(&writer, capture->target);
        byte_writer_emit_bytes(&writer, capture->input_sha256, 64);
        byte_writer_emit_bytes(&writer, capture->translation_sha256, 64);
        byte_writer_emit_bytes(&writer, capture->artifact_sha256, 64);
        investigation_emit_u64(&writer, capture->file_offset);
        investigation_emit_u64(&writer, capture->section_offset);
        investigation_emit_u64(&writer, capture->capture_ns);
        byte_writer_emit_u32_le(&writer, capture->function);
        byte_writer_emit_u32_le(&writer, capture->code_base);
        byte_writer_emit_u32_le(&writer, capture->row_count);
        byte_writer_emit_u32_le(&writer, capture->mark_count);
        byte_writer_emit_u32_le(&writer, capture->allocator);
        byte_writer_emit_u32_le(&writer, capture->cpu);
        byte_writer_emit_u32_le(&writer, capture->os);
        byte_writer_emit_u32_le(&writer, capture->features);
        byte_writer_emit_u32_le(&writer, (u32)capture->transformed);
        byte_writer_emit_u32_le(&writer, (u32)capture->code.length);
        for (u32 index = 0; index < capture->row_count; index += 1)
        {
            InvestigationRow row = capture->rows[index];
            byte_writer_emit_u32_le(&writer, row.offset);
            byte_writer_emit_u32_le(&writer, row.end);
            byte_writer_emit_u32_le(&writer, row.opcode);
        }
        for (u32 index = 0; index < capture->mark_count; index += 1)
        {
            InvestigationMark const* mark = capture->marks + index;
            byte_writer_emit_u32_le(&writer, mark->row);
            byte_writer_emit_u32_le(&writer, mark->instruction);
            byte_writer_emit_u32_le(&writer, mark->opcode);
            byte_writer_emit_u32_le(&writer, mark->range.source.value);
            byte_writer_emit_u32_le(&writer, mark->range.offset);
            byte_writer_emit_u32_le(&writer, mark->range.length);
            investigation_emit_position(&writer, mark->position);
            investigation_emit_position(&writer, mark->original_position);
            investigation_emit_text(&writer, mark->path);
            investigation_emit_text(&writer, mark->original_path);
        }
        byte_writer_emit_bytes(&writer, capture->code.pointer, capture->code.length);
        byte_writer_emit_u32_le(&writer, BUSTER_INVESTIGATION_END);
        bool committed = byte_writer_commit(&writer, &result);
        if (!committed || result.length != size)
        {
            result = (ByteSlice){0};
        }
    }
    return result;
}

InvestigationCapture investigation_read(Arena* arena, ByteSlice bytes)
{
    InvestigationCapture result = {0};
    String8 magic = BUSTER_INVESTIGATION_MAGIC;
    InvestigationReader reader = {.bytes = bytes};
    reader.invalid = !arena || !bytes.pointer || bytes.length > INVESTIGATION_CAPTURE_BYTE_LIMIT ||
                     !investigation_span(bytes, 0, magic.length) || memcmp(bytes.pointer, magic.pointer, magic.length) != 0;
    reader.offset = magic.length;
    if (!reader.invalid)
    {
        reader.invalid = investigation_read_scalar(&reader, 4) != INVESTIGATION_SCHEMA_VERSION;
        result.function_name = investigation_read_text(&reader);
        result.revision = investigation_read_text(&reader);
        result.configuration = investigation_read_text(&reader);
        result.input_path = investigation_read_text(&reader);
        result.artifact_path = investigation_read_text(&reader);
        result.section_name = investigation_read_text(&reader);
        result.target = investigation_read_text(&reader);
        investigation_read_digest(&reader, result.input_sha256);
        investigation_read_digest(&reader, result.translation_sha256);
        investigation_read_digest(&reader, result.artifact_sha256);
        result.file_offset = investigation_read_scalar(&reader, 8);
        result.section_offset = investigation_read_scalar(&reader, 8);
        result.capture_ns = investigation_read_scalar(&reader, 8);
        result.function = (u32)investigation_read_scalar(&reader, 4);
        result.code_base = (u32)investigation_read_scalar(&reader, 4);
        result.row_count = (u32)investigation_read_scalar(&reader, 4);
        result.mark_count = (u32)investigation_read_scalar(&reader, 4);
        result.allocator = (u32)investigation_read_scalar(&reader, 4);
        result.cpu = (u32)investigation_read_scalar(&reader, 4);
        result.os = (u32)investigation_read_scalar(&reader, 4);
        result.features = (u32)investigation_read_scalar(&reader, 4);
        u64 transformed = investigation_read_scalar(&reader, 4);
        result.transformed = transformed == 1;
        result.code.length = investigation_read_scalar(&reader, 4);
        u64 minimum = (u64)result.row_count * BUSTER_INVESTIGATION_ROW_BYTES +
                      (u64)result.mark_count * BUSTER_INVESTIGATION_MARK_BYTES + result.code.length + 4u;
        reader.invalid = reader.invalid || transformed > 1 || result.row_count > INVESTIGATION_ROW_LIMIT ||
                         result.mark_count > INVESTIGATION_MARK_LIMIT || result.code.length > INVESTIGATION_FUNCTION_BYTE_LIMIT ||
                         !investigation_span(bytes, reader.offset, minimum);
        if (!reader.invalid)
        {
            result.rows = arena_allocate(arena, InvestigationRow, result.row_count);
            result.marks = arena_allocate_zeroed(arena, InvestigationMark, result.mark_count);
            for (u32 index = 0; !reader.invalid && index < result.row_count; index += 1)
            {
                InvestigationRow* row = result.rows + index;
                row->offset = (u32)investigation_read_scalar(&reader, 4);
                row->end = (u32)investigation_read_scalar(&reader, 4);
                row->opcode = (u32)investigation_read_scalar(&reader, 4);
            }
            for (u32 index = 0; !reader.invalid && index < result.mark_count; index += 1)
            {
                InvestigationMark* mark = result.marks + index;
                mark->row = (u32)investigation_read_scalar(&reader, 4);
                mark->instruction = (u32)investigation_read_scalar(&reader, 4);
                mark->opcode = (u32)investigation_read_scalar(&reader, 4);
                mark->range.source.value = (u32)investigation_read_scalar(&reader, 4);
                mark->range.offset = (u32)investigation_read_scalar(&reader, 4);
                mark->range.length = (u32)investigation_read_scalar(&reader, 4);
                mark->position = investigation_read_position(&reader);
                mark->original_position = investigation_read_position(&reader);
                mark->path = investigation_read_text(&reader);
                mark->original_path = investigation_read_text(&reader);
            }
            if (!reader.invalid && investigation_span(bytes, reader.offset, result.code.length))
            {
                result.code.pointer = bytes.pointer + reader.offset;
                reader.offset += result.code.length;
                u64 terminator = investigation_read_scalar(&reader, 4);
                result.found = !reader.invalid && terminator == BUSTER_INVESTIGATION_END && reader.offset == bytes.length;
                result.found = result.found && investigation_capture_shape(&result, 0, true);
            }
        }
    }
    if (result.found)
    {
        result.function_name = string_duplicate_arena(arena, result.function_name, false);
        result.revision = string_duplicate_arena(arena, result.revision, false);
        result.configuration = string_duplicate_arena(arena, result.configuration, false);
        result.input_path = string_duplicate_arena(arena, result.input_path, false);
        result.artifact_path = string_duplicate_arena(arena, result.artifact_path, false);
        result.section_name = string_duplicate_arena(arena, result.section_name, false);
        result.target = string_duplicate_arena(arena, result.target, false);
        for (u32 index = 0; index < result.mark_count; index += 1)
        {
            result.marks[index].path = string_duplicate_arena(arena, result.marks[index].path, false);
            result.marks[index].original_path = string_duplicate_arena(arena, result.marks[index].original_path, false);
        }
        u8* owned = arena_allocate(arena, u8, result.code.length);
        if (result.code.length)
        {
            memcpy(owned, result.code.pointer, result.code.length);
        }
        result.code.pointer = owned;
    }
    else
    {
        result.diagnostic = S8("invalid, truncated, unsupported, or over-limit investigation capture");
    }
    return result;
}

BUSTER_GLOBAL_LOCAL u64 investigation_elf_scalar(ByteSlice bytes, u64 offset, u32 width, bool* valid)
{
    InvestigationReader reader = {.bytes = bytes, .offset = offset, .invalid = !*valid};
    u64 result = investigation_read_scalar(&reader, width);
    *valid = *valid && !reader.invalid;
    return result;
}

BUSTER_GLOBAL_LOCAL InvestigationElfSection investigation_elf_section(ByteSlice bytes, String8 name)
{
    InvestigationElfSection result = {0};
    bool valid = bytes.length <= INVESTIGATION_ARTIFACT_BYTE_LIMIT && investigation_span(bytes, 0, 64) &&
                 investigation_text_valid(name) && name.length;
    if (valid)
    {
        valid = bytes.pointer[0] == 0x7f && bytes.pointer[1] == 'E' && bytes.pointer[2] == 'L' && bytes.pointer[3] == 'F' &&
                bytes.pointer[4] == 2 && bytes.pointer[5] == 1 && bytes.pointer[6] == 1;
        u64 kind = investigation_elf_scalar(bytes, 16, 2, &valid);
        u64 machine = investigation_elf_scalar(bytes, 18, 2, &valid);
        u64 version = investigation_elf_scalar(bytes, 20, 4, &valid);
        u64 headers = investigation_elf_scalar(bytes, 40, 8, &valid);
        u64 header_size = investigation_elf_scalar(bytes, 52, 2, &valid);
        u64 stride = investigation_elf_scalar(bytes, 58, 2, &valid);
        u64 count = investigation_elf_scalar(bytes, 60, 2, &valid);
        u64 names = investigation_elf_scalar(bytes, 62, 2, &valid);
        // v1 supports ordinary ELF64 ET_REL/x86-64, not extended section numbering.
        valid = valid && kind == 1 && machine == 62 && version == 1 && header_size == 64 && stride == 64 &&
                count && names && names < count && investigation_span(bytes, headers, count * stride);
        u64 string_header = valid ? headers + names * stride : 0;
        u64 string_kind = investigation_elf_scalar(bytes, string_header + 4, 4, &valid);
        u64 string_offset = investigation_elf_scalar(bytes, string_header + 24, 8, &valid);
        u64 string_size = investigation_elf_scalar(bytes, string_header + 32, 8, &valid);
        valid = valid && string_kind == 3 && string_size && investigation_span(bytes, string_offset, string_size);
        u32 matches = 0;
        for (u64 index = 0; valid && index < count; index += 1)
        {
            u64 header = headers + index * stride;
            u64 name_offset = investigation_elf_scalar(bytes, header, 4, &valid);
            valid = valid && name_offset < string_size;
            u64 length = 0;
            while (valid && length < string_size - name_offset && length <= INVESTIGATION_TEXT_LIMIT &&
                   bytes.pointer[string_offset + name_offset + length])
            {
                length += 1;
            }
            valid = valid && length < string_size - name_offset && length <= INVESTIGATION_TEXT_LIMIT;
            if (valid)
            {
                String8 section_name = {(char8*)bytes.pointer + string_offset + name_offset, length};
                if (string_equal(name, section_name))
                {
                    u64 section_kind = investigation_elf_scalar(bytes, header + 4, 4, &valid);
                    u64 flags = investigation_elf_scalar(bytes, header + 8, 8, &valid);
                    result.offset = investigation_elf_scalar(bytes, header + 24, 8, &valid);
                    result.size = investigation_elf_scalar(bytes, header + 32, 8, &valid);
                    valid = valid && section_kind == 1 && (flags & 4u) && investigation_span(bytes, result.offset, result.size);
                    matches += 1;
                }
            }
        }
        result.valid = valid && matches == 1;
    }
    return result;
}

bool investigation_bind_object(Arena* arena, InvestigationCapture* capture, ObjectFile const* object, ByteSlice artifact)
{
    bool result = arena && capture && object && investigation_capture_shape(capture, 0, false) && !capture->diagnostic.length &&
                  object->error == OBJECT_ERROR_NONE && object->target.cpu_arch == CPU_ARCH_X86_64 &&
                  object->target.os == OPERATING_SYSTEM_LINUX && object->sections && (!object->symbol_count || object->symbols) &&
                  artifact.length <= INVESTIGATION_ARTIFACT_BYTE_LIMIT && (!artifact.length || artifact.pointer);
    ObjectSymbol const* function = 0;
    u32 matches = 0;
    if (result)
    {
        for (u32 index = 0; index < object->symbol_count; index += 1)
        {
            ObjectSymbol const* symbol = object->symbols + index;
            if (symbol->kind == OBJECT_SYMBOL_FUNCTION && string_equal(symbol->name, capture->function_name))
            {
                function = symbol;
                matches += 1;
            }
        }
        result = matches == 1 && function && function->section < object->section_count && function->size == capture->code.length;
    }
    if (result)
    {
        ObjectSection const* section = object->sections + function->section;
        InvestigationElfSection serialized = investigation_elf_section(artifact, section->name);
        result = section->kind == OBJECT_SECTION_TEXT && serialized.valid && serialized.size == section->data.length &&
                 investigation_span(section->data, function->value, capture->code.length) &&
                 function->value <= serialized.size && capture->code.length <= serialized.size - function->value;
        if (result && capture->code.length)
        {
            result = memcmp(section->data.pointer + function->value, capture->code.pointer, capture->code.length) == 0 &&
                     memcmp(artifact.pointer + serialized.offset + function->value, capture->code.pointer, capture->code.length) == 0;
        }
        if (result)
        {
            capture->section_name = string_duplicate_arena(arena, section->name, false);
            capture->section_offset = function->value;
            capture->file_offset = serialized.offset + function->value;
            investigation_digest(artifact, capture->artifact_sha256);
        }
    }
    if (!result && capture)
    {
        capture->found = false;
        capture->diagnostic = S8("cannot bind investigation: missing/ambiguous ELF function or section, over-limit object, or changed encoded bytes");
    }
    return result;
}

bool investigation_matches(InvestigationCapture const* capture, ByteSlice artifact, String8 expected_revision)
{
    bool result = capture && artifact.length <= INVESTIGATION_ARTIFACT_BYTE_LIMIT &&
                  investigation_capture_shape(capture, 0, true) && investigation_span(artifact, capture->file_offset, capture->code.length) &&
                  (!expected_revision.length || investigation_revision_equal(capture->revision, expected_revision));
    if (result)
    {
        char8 digest[SHA256_HEX_CAPACITY];
        investigation_digest(artifact, digest);
        result = memcmp(digest, capture->artifact_sha256, 64) == 0 &&
                 (!capture->code.length || memcmp(artifact.pointer + capture->file_offset, capture->code.pointer, capture->code.length) == 0);
    }
    return result;
}

BUSTER_GLOBAL_LOCAL String8 investigation_mir_label(u32 opcode, bool interpret)
{
    String8 result = S8("numeric ID");
    if (interpret)
    {
        switch (opcode)
        {
            break; case MACHINE_X64_CVT_F32_TO_U64: result = S8("MACHINE_X64_CVT_F32_TO_U64");
            break; case MACHINE_X64_CVT_F64_TO_U64: result = S8("MACHINE_X64_CVT_F64_TO_U64");
            break; default: result = S8("numeric ID");
        }
    }
    return result;
}

BUSTER_GLOBAL_LOCAL void investigation_report_mark(InvestigationMark const* mark, bool interpret)
{
    String8 label = interpret && mark->opcode == IR_OPCODE_CAST ? S8("CAST") : S8("numeric ID");
    string_print(S8("  canonical instruction={u32} opcode={u32} ({S8}) selection_start_row={u32}\n"),
                 mark->instruction, mark->opcode, label, mark->row);
    bool available = mark->range.source.value != IR_ID_UNDERLYING_INVALID && mark->position.line && mark->path.length;
    if (available)
    {
        string_print(S8("    source anchor {S8}:{u32}:{u32} source_offset={u32}; expanded_offset={u32} expanded_length={u32}\n"),
                     mark->path, mark->position.line, mark->position.column, mark->position.offset, mark->range.offset, mark->range.length);
    }
    else
    {
        string_print(S8("    source anchor unavailable\n"));
    }
    if (mark->range.source.value != IR_ID_UNDERLYING_INVALID && mark->original_position.line && mark->original_path.length)
    {
        string_print(S8("    original anchor {S8}:{u32}:{u32} physical_offset={u32}\n"), mark->original_path,
                     mark->original_position.line, mark->original_position.column, mark->original_position.offset);
    }
}

BUSTER_GLOBAL_LOCAL void investigation_report_context(InvestigationCapture const* capture, u32 row, bool interpret)
{
    u32 nearest = UINT32_MAX;
    for (u32 index = 0; index < capture->mark_count; index += 1)
    {
        u32 anchor = capture->marks[index].row;
        if (anchor <= row && (nearest == UINT32_MAX || anchor > nearest))
        {
            nearest = anchor;
        }
    }
    if (nearest == UINT32_MAX)
    {
        string_print(S8("  canonical selection context unavailable\n"));
    }
    else
    {
        string_print(S8("  {S8} canonical selection anchors (context, not byte ownership):\n"),
                     nearest == row ? S8("exact-start") : S8("nearest preceding"));
        for (u32 index = 0; index < capture->mark_count; index += 1)
        {
            if (capture->marks[index].row == nearest)
            {
                investigation_report_mark(capture->marks + index, interpret);
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL void investigation_report_row(InvestigationCapture const* capture, u32 index, bool interpret)
{
    InvestigationRow row = capture->rows[index];
    string_print(S8("MIR row={u32} opcode={u32} ({S8}) function_bytes=[{u32},{u32}) file_bytes=[{u64},{u64})\n"),
                 index, row.opcode, investigation_mir_label(row.opcode, interpret), row.offset, row.end,
                 capture->file_offset + row.offset, capture->file_offset + row.end);
    char8 hex[BUSTER_INVESTIGATION_HEX_BYTES * 2u];
    u32 length = BUSTER_MIN(row.end - row.offset, BUSTER_INVESTIGATION_HEX_BYTES);
    String8 digits = S8("0123456789abcdef");
    for (u32 byte = 0; byte < length; byte += 1)
    {
        u8 value = capture->code.pointer[row.offset + byte];
        hex[2u * byte] = digits.pointer[value >> 4u];
        hex[2u * byte + 1u] = digits.pointer[value & 15u];
    }
    string_print(S8("  encoded bytes {S8}{S8}\n"), (String8){hex, (u64)length * 2u}, length < row.end - row.offset ? S8(" (truncated)") : S8(""));
    if (interpret && (row.opcode == MACHINE_X64_CVT_F32_TO_U64 || row.opcode == MACHINE_X64_CVT_F64_TO_U64))
    {
        string_print(S8("  floating -> u64: signed64 conversion with compare/subtract/correction around 2^63; "
                        "the extra comparison/branch alone does not establish redundant lowering. Defined source domain: 0 <= x < 2^64.\n"));
    }
}

BUSTER_GLOBAL_LOCAL void investigation_report_native(Arena* arena, InvestigationCapture const* capture, u32 start, u32 end)
{
    if (start < end)
    {
        u32 length = BUSTER_MIN(end - start, BUSTER_INVESTIGATION_DISASSEMBLY_BYTES);
        ObjectSection section = {.name = S8(".text"), .data = {.pointer = capture->code.pointer + start, .length = length},
                                 .kind = OBJECT_SECTION_TEXT, .alignment = 1};
        // ELF identity was checked independently of capture-local enum IDs.
        ObjectFile object = {.sections = &section, .section_count = 1,
                             .target = {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX}};
        String8 assembly = object_print_assembly(arena, &object);
        string_print(S8("Native byte view for function offset={u32} file_offset={u64}, length={u32}{S8}:\n"
                        "  ISA decoding is partial; .byte preserves unsupported forms. Branch operands are slice-relative; "
                        "relocations are unresolved and source ownership is unavailable.\n{S8}"),
                     start, capture->file_offset + start, length, length < end - start ? S8(" (truncated)") : S8(""), assembly);
        if (!assembly.length)
        {
            string_print(S8("  native decoding unavailable; use the encoded bytes above\n"));
        }
    }
}

BUSTER_GLOBAL_LOCAL void investigation_report_manifest(InvestigationCapture const* capture, bool interpret)
{
    string_print(S8("INVESTIGATION schema={u32} function={S8} canonical_function={u32}\n"
                    "revision={S8}; numeric opcode interpretation={S8}\n"
                    "input={S8} sha256={S8}\ntranslation_sha256={S8}\n"
                    "artifact={S8} sha256={S8}\ntarget={S8}\nconfiguration={S8}\n"
                    "section={S8} section_offset={u64} file_offset={u64} bytes={u64} rows={u32} anchors={u32} capture_ns={u64}\n"),
                 INVESTIGATION_SCHEMA_VERSION, capture->function_name, capture->function,
                 capture->revision.length ? capture->revision : S8("unavailable"), interpret ? S8("matching compiler revision") : S8("unverified capture-local IDs"),
                 capture->input_path, (String8){(char8*)capture->input_sha256, 64}, (String8){(char8*)capture->translation_sha256, 64},
                 capture->artifact_path, (String8){(char8*)capture->artifact_sha256, 64}, capture->target, capture->configuration,
                 capture->section_name, capture->section_offset, capture->file_offset, capture->code.length,
                 capture->row_count, capture->mark_count, capture->capture_ns);
    string_print(S8("Mappings are canonical selection anchors after preparation; transformation history is unavailable. "
                    "Synthetic rows, fused operations, allocator edits, and multiple source constructs can share emission intervals. "
                    "Expanded lengths are not physical source extents; paths/positions are snapshots, not current-source verification.\n"));
}

ProcessResult investigation_command(Arena* arena, SliceString8 arguments)
{
    ProcessResult result = PROCESS_RESULT_FAILED;
    bool valid = arena && arguments.pointer && arguments.length >= 2 && arguments.length <= 4;
    bool query = false;
    u64 file_offset = 0;
    String8 expected_revision = {0};
    FileMapRead capture_map = {0};
    FileMapRead artifact_map = {0};
    if (valid)
    {
        valid = investigation_text_valid(arguments.pointer[0]) && arguments.pointer[0].length &&
                investigation_text_valid(arguments.pointer[1]) && arguments.pointer[1].length;
        for (u64 index = 2; valid && index < arguments.length; index += 1)
        {
            String8 argument = arguments.pointer[index];
            String8 offset_prefix = S8("--offset=");
            String8 revision_prefix = S8("--expect-revision=");
            if (string_starts_with_sequence(argument, offset_prefix))
            {
                String8 value = string_slice(argument, offset_prefix.length, argument.length);
                IntegerParsingU64 parsed = string8_parse_u64_decimal(value);
                valid = !query && value.length && parsed.status == INTEGER_PARSING_SUCCESS && parsed.length == value.length;
                query = true;
                file_offset = parsed.value;
            }
            else if (string_starts_with_sequence(argument, revision_prefix))
            {
                String8 value = string_slice(argument, revision_prefix.length, argument.length);
                valid = !expected_revision.length && investigation_hex_valid(value, 40, false);
                expected_revision = value;
            }
            else
            {
                valid = false;
            }
        }
    }
    if (!valid)
    {
        string_print(S8("usage: ide investigate <capture> <object> [--offset=<decimal-file-offset>] [--expect-revision=<40hex>]\n"));
    }
    else
    {
        capture_map = file_map_read(arena, arguments.pointer[0], (FileReadOptions){.map_required = 1});
        valid = capture_map.bytes.pointer && capture_map.bytes.length <= INVESTIGATION_CAPTURE_BYTE_LIMIT;
        if (!valid)
        {
            string_print(S8("investigate: could not map bounded capture {S8}\n"), arguments.pointer[0]);
        }
        InvestigationCapture capture = valid ? investigation_read(arena, capture_map.bytes) : (InvestigationCapture){0};
        if (valid && !capture.found)
        {
            valid = false;
            string_print(S8("investigate: {S8}\n"), capture.diagnostic);
        }
        if (valid)
        {
            artifact_map = file_map_read(arena, arguments.pointer[1], (FileReadOptions){.map_required = 1});
            valid = artifact_map.bytes.pointer && artifact_map.bytes.length <= INVESTIGATION_ARTIFACT_BYTE_LIMIT;
            if (!valid)
            {
                string_print(S8("investigate: could not map bounded artifact {S8}\n"), arguments.pointer[1]);
            }
        }
        if (valid && !investigation_matches(&capture, artifact_map.bytes, expected_revision))
        {
            valid = false;
            string_print(S8("investigate: artifact bytes/digest or expected compiler revision mismatch (unavailable revision cannot satisfy an expectation)\n"));
        }
        if (valid)
        {
            InvestigationElfSection section = investigation_elf_section(artifact_map.bytes, capture.section_name);
            valid = section.valid && capture.section_offset <= section.size && capture.code.length <= section.size - capture.section_offset &&
                    section.offset + capture.section_offset == capture.file_offset;
            if (!valid)
            {
                string_print(S8("investigate: captured location does not match a unique executable x86-64 ELF64 object section\n"));
            }
        }
        if (valid && query && (file_offset < capture.file_offset || file_offset - capture.file_offset > capture.code.length))
        {
            valid = false;
            string_print(S8("investigate: queried file offset is outside the captured function\n"));
        }
        if (valid)
        {
            bool interpret = investigation_revision_equal(capture.revision, investigation_compiler_revision());
            investigation_report_manifest(&capture, interpret);
            if (query)
            {
                u32 relative = (u32)(file_offset - capture.file_offset);
                u32 selected = UINT32_MAX;
                for (u32 index = 0; index < capture.row_count; index += 1)
                {
                    InvestigationRow row = capture.rows[index];
                    if (row.offset <= relative && relative < row.end)
                    {
                        selected = index;
                    }
                    if (row.offset == relative && row.offset == row.end)
                    {
                        investigation_report_row(&capture, index, interpret);
                    }
                }
                if (selected != UINT32_MAX)
                {
                    investigation_report_row(&capture, selected, interpret);
                    investigation_report_context(&capture, selected, interpret);
                    investigation_report_native(arena, &capture, capture.rows[selected].offset, capture.rows[selected].end);
                }
                else
                {
                    string_print(S8("No MIR emission interval at file offset={u64}; prologue, terminal boundary, or mapping unavailable.\n"), file_offset);
                    if (relative == capture.code.length)
                    {
                        investigation_report_context(&capture, capture.row_count, interpret);
                    }
                }
            }
            else
            {
                u32 count = BUSTER_MIN(capture.row_count, BUSTER_INVESTIGATION_REPORT_ROW_LIMIT);
                for (u32 index = 0; index < count; index += 1)
                {
                    investigation_report_row(&capture, index, interpret);
                }
                if (count < capture.row_count)
                {
                    string_print(S8("Row listing limited to {u32}; query remaining rows using --offset=<file-offset>.\n"), count);
                }
                string_print(S8("Canonical selection anchors (duplicate and terminal starts retained):\n"));
                for (u32 index = 0; index < capture.mark_count; index += 1)
                {
                    investigation_report_mark(capture.marks + index, interpret);
                }
                if (capture.code.length <= BUSTER_INVESTIGATION_DISASSEMBLY_BYTES)
                {
                    investigation_report_native(arena, &capture, 0, (u32)capture.code.length);
                }
                else
                {
                    string_print(S8("Use --offset to view a bounded native emission sequence.\n"));
                }
            }
            result = PROCESS_RESULT_SUCCESS;
        }
    }
    file_map_unmap(artifact_map);
    file_map_unmap(capture_map);
    return result;
}
