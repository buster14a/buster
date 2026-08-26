#include <buster/tests/compiler/codegen/machine_test.h>
#if BUSTER_INCLUDE_TESTS

#include <buster/lib/compiler/assembly/aarch64_encoding.h>
#include <buster/lib/compiler/codegen/codegen.h>
#include <buster/lib/compiler/codegen/machine_x86_64_emit_registry.h>
#include <buster/lib/compiler/frontend/c/c.h>
#include <buster/lib/file.h>
#include <buster/lib/compiler/ir/ir.h>
#include <buster/lib/string.h>
#include <buster/lib/x86_64.h>

// Size census for the hot records the register-allocator project depends
// on. The machine rows are all-integer and hold everywhere; the typed IR and
// codegen records carry pointers, so their checks apply to 64-bit builds
// only. A designed size change must update these checks in the same change —
// they exist so design documents can never drift from the code the way the
// historical IrInstructionExtra comment did.
BUSTER_CT_CHECK(sizeof(MachineInstruction) == 24);
BUSTER_CT_CHECK(sizeof(MachineVirtualRegister) == 16);
BUSTER_CT_CHECK(sizeof(MachineBlock) == 32);
BUSTER_CT_CHECK(sizeof(MachineEdge) == 16);
BUSTER_CT_CHECK(sizeof(MachineAddress) == 16);
BUSTER_CT_CHECK(sizeof(MachineSegment) == 8);
BUSTER_CT_CHECK(sizeof(MachineUse) == 8);
BUSTER_CT_CHECK(sizeof(MachineEdit) == 16);
BUSTER_CT_CHECK(sizeof(MachineLocationSegment) == 16);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrInstruction) == 64);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrValue) == 16);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrBlock) == 64);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrBlockParameter) == 40);
BUSTER_CT_CHECK(sizeof(void*) != 8 || sizeof(IrIncoming) == 16);
BUSTER_CT_CHECK(sizeof(CodegenModuleOptions) == 4);

// Compiles one C source through the C frontend into a canonical IrProgram
// for machine-selection tests. Diagnostics fail the caller's assertions.
BUSTER_GLOBAL_LOCAL IrProgram* machine_test_compile_c(Arena* arena, String8 name, String8 source, Target target)
{
    CPreprocessResult tokens = c_preprocess(arena, source, (CPreprocessOptions){0});
    if (tokens.error_count)
    {
        return 0;
    }
    CParseResult parse = c_parse(arena, tokens);
    if (parse.diagnostic_count)
    {
        return 0;
    }
    CIRLowerResult lowered = c_lower_to_ir(arena, name, tokens, parse, target);
    IrProgram* result;
    if (lowered.diagnostic_count)
    {
        result = 0;
    }
    else
    {
        result = lowered.program;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL IrFunction* machine_test_ir_function_find(IrModule* module, String8 name)
{
    for (u32 function_index = 0; function_index < module->function_count; function_index += 1)
    {
        if (string_equal(module->functions[function_index].name, name))
        {
            return module->functions + function_index;
        }
    }
    return 0;
}

// Selects, verifies, places, and encodes one function; returns encoded
// bytes with valid=false at the first failing pipeline step.
BUSTER_GLOBAL_LOCAL MachineEncodeResult machine_test_encode(Arena* arena, IrProgram* program, IrFunction* function, Target target,
                                                            MachineSelectResult* select_out)
{
    MachineEncodeResult encoded = {0};
    MachineSelectResult selected = machine_select_canonical_function(arena, program, function, target);
    if (select_out)
    {
        *select_out = selected;
    }
    if (!selected.supported || !selected.selector_certified || machine_verify_function(&selected.function).error != MACHINE_VERIFY_NONE)
    {
        return encoded;
    }
    MachineStackPlacement placement = machine_stack_placement_build(arena, &selected.function);
    if (!placement.valid)
    {
        return encoded;
    }
    return target.cpu_arch == CPU_ARCH_AARCH64 ? machine_encode_aarch64(arena, &selected.function, &placement)
                                               : machine_encode_x86_64(arena, &selected.function, &placement);
}

typedef struct MachineX64SourceSpan MachineX64SourceSpan;
struct MachineX64SourceSpan
{
    u8 const* bytes;
    u64 length;
    u64 start;
    u64 end;
};

typedef enum MachineX64SourceArch
{
    MACHINE_X64_SOURCE_ARCH_UNKNOWN,
    MACHINE_X64_SOURCE_ARCH_X86,
    MACHINE_X64_SOURCE_ARCH_AARCH64,
} MachineX64SourceArch;

typedef struct MachineX64SourceAudit MachineX64SourceAudit;
struct MachineX64SourceAudit
{
    bool files_readable;
    bool owners_found;
    u32 owner_count;
    u32 forbidden_count;
    u32 forbidden_constructor_count;
    u32 neutral_patch_count;
    u32 aarch64_write_count;
    u32 data_directive_count;
};

BUSTER_GLOBAL_LOCAL bool machine_test_source_identifier_start(u8 byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';
}

BUSTER_GLOBAL_LOCAL bool machine_test_source_identifier_continue(u8 byte)
{
    return machine_test_source_identifier_start(byte) || (byte >= '0' && byte <= '9');
}

BUSTER_GLOBAL_LOCAL bool machine_test_source_token_at(MachineX64SourceSpan source, u64 offset, String8 token)
{
    if (!source.bytes || source.start > source.end || source.end > source.length || offset < source.start || offset > source.end || !token.length ||
        token.length > source.end - offset)
        return false;
    if (memcmp(source.bytes + offset, token.pointer, token.length) != 0) return false;
    bool left_boundary = offset == source.start || !machine_test_source_identifier_continue(source.bytes[offset - 1]);
    u64 end = offset + token.length;
    bool right_boundary = end == source.end || !machine_test_source_identifier_continue(source.bytes[end]);
    return left_boundary && right_boundary;
}

BUSTER_GLOBAL_LOCAL bool machine_test_source_span_contains_sequence(MachineX64SourceSpan source, String8 sequence)
{
    if (source.bytes && source.start <= source.end && source.end <= source.length && sequence.length && sequence.length <= source.end - source.start)
    {
        u8 const* bytes = source.bytes + source.start;
        u64 length = source.end - source.start;
        for (u64 offset = 0; offset + sequence.length <= length; offset += 1)
            if (memcmp(bytes + offset, sequence.pointer, sequence.length) == 0) return true;
    }

    return false;
}

BUSTER_GLOBAL_LOCAL u8* machine_test_source_sanitize(Arena* arena, ByteSlice input)
{
    u8* result = arena_allocate(arena, u8, input.length + 1);
    u64 index = 0;
    while (index < input.length)
    {
        u8 byte = input.pointer[index];
        if (byte == '/' && index + 1 < input.length && input.pointer[index + 1] == '/')
        {
            while (index < input.length && input.pointer[index] != '\n')
            {
                result[index] = ' ';
                index += 1;
            }
            continue;
        }
        if (byte == '/' && index + 1 < input.length && input.pointer[index + 1] == '*')
        {
            result[index++] = ' ';
            result[index++] = ' ';
            while (index < input.length)
            {
                if (input.pointer[index] == '*' && index + 1 < input.length && input.pointer[index + 1] == '/')
                {
                    result[index++] = ' ';
                    result[index++] = ' ';
                    break;
                }
                result[index] = input.pointer[index] == '\n' ? '\n' : ' ';
                index += 1;
            }
            continue;
        }
        if (byte == '"' || byte == '\'')
        {
            u8 quote = byte;
            result[index++] = ' ';
            while (index < input.length)
            {
                byte = input.pointer[index];
                if (byte == '\\' && index + 1 < input.length)
                {
                    result[index++] = ' ';
                    u8 escaped = input.pointer[index];
                    result[index++] = escaped == '\n' ? '\n' : ' ';
                    continue;
                }
                result[index] = byte == '\n' ? '\n' : ' ';
                index += 1;
                if (byte == quote) break;
            }
            continue;
        }
        result[index] = byte;
        index += 1;
    }
    result[input.length] = 0;
    return result;
}

BUSTER_GLOBAL_LOCAL bool machine_test_source_function_body_at(MachineX64SourceSpan source, String8 owner, u64 offset,
                                                               MachineX64SourceSpan* body)
{
    if (!owner.length || !source.bytes || source.start > source.end || source.end > source.length || offset < source.start ||
        offset + owner.length > source.end || !machine_test_source_token_at(source, offset, owner))
        return false;
    u64 cursor = offset + owner.length;
    while (cursor < source.end && (source.bytes[cursor] == ' ' || source.bytes[cursor] == '\t' || source.bytes[cursor] == '\r' ||
                                   source.bytes[cursor] == '\n'))
        cursor += 1;
    if (cursor < source.end && source.bytes[cursor] == '(')
    {
        u64 parentheses = 0;
        for (; cursor < source.end; cursor += 1)
        {
            if (source.bytes[cursor] == '(') parentheses += 1;
            else if (source.bytes[cursor] == ')' && parentheses && --parentheses == 0)
            {
                cursor += 1;
                break;
            }
        }
        if (!parentheses && cursor < source.end)
        {
            // Definitions in this codebase put the opening brace immediately after
            // the parameter list (modulo whitespace).  Requiring that shape rejects
            // calls such as x64_emit_foo(...) followed by a block and keeps prefix
            // discovery independent of line numbers or call-site lists.
            while (cursor < source.end && (source.bytes[cursor] == ' ' || source.bytes[cursor] == '\t' || source.bytes[cursor] == '\r' ||
                                           source.bytes[cursor] == '\n'))
                cursor += 1;
            if (cursor < source.end && source.bytes[cursor] == '{')
            {
                u64 depth = 0;
                for (u64 index = cursor; index < source.end; index += 1)
                {
                    if (source.bytes[index] == '{') depth += 1;
                    else if (source.bytes[index] == '}')
                    {
                        if (!depth) break;
                        depth -= 1;
                        if (!depth)
                        {
                            if (body)
                            {
                                *body = (MachineX64SourceSpan){
                                    .bytes = source.bytes,
                                    .length = source.length,
                                    .start = cursor,
                                    .end = index + 1,
                                };
                            }
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool machine_test_source_function_body_from(MachineX64SourceSpan source, String8 owner, u64 search_start,
                                                                 MachineX64SourceSpan* body)
{
    if (owner.length && source.bytes && source.start <= source.end && source.end <= source.length)
    {
        u64 first_offset = BUSTER_MAX(search_start, source.start);
        for (u64 offset = first_offset; offset + owner.length <= source.end; offset += 1)
        {
            if (machine_test_source_function_body_at(source, owner, offset, body)) return true;
        }
    }

    return false;
}

BUSTER_GLOBAL_LOCAL bool machine_test_source_function_body(MachineX64SourceSpan source, String8 owner, MachineX64SourceSpan* body)
{
    return machine_test_source_function_body_from(source, owner, source.start, body);
}

BUSTER_GLOBAL_LOCAL MachineX64SourceArch machine_test_source_arch_inverse(MachineX64SourceArch arch)
{
    return arch == MACHINE_X64_SOURCE_ARCH_X86   ? MACHINE_X64_SOURCE_ARCH_AARCH64
           : arch == MACHINE_X64_SOURCE_ARCH_AARCH64 ? MACHINE_X64_SOURCE_ARCH_X86
                                                     : MACHINE_X64_SOURCE_ARCH_UNKNOWN;
}

BUSTER_GLOBAL_LOCAL MachineX64SourceArch machine_test_source_arch_markers(MachineX64SourceSpan source, u64 start, u64 end)
{
    if (source.bytes && start <= end && end <= source.end)
    {
        String8 x86_name = S8("CPU_ARCH_X86_64");
        String8 aarch64_name = S8("CPU_ARCH_AARCH64");
        bool x86_found = false;
        bool aarch64_found = false;
        for (u64 offset = start; offset + x86_name.length <= end; offset += 1)
        {
            x86_found |= machine_test_source_token_at(source, offset, x86_name);
        }
        for (u64 offset = start; offset + aarch64_name.length <= end; offset += 1)
        {
            aarch64_found |= machine_test_source_token_at(source, offset, aarch64_name);
        }
        if (x86_found && !aarch64_found) return MACHINE_X64_SOURCE_ARCH_X86;
        if (aarch64_found && !x86_found) return MACHINE_X64_SOURCE_ARCH_AARCH64;
    }

    return MACHINE_X64_SOURCE_ARCH_UNKNOWN;
}

BUSTER_GLOBAL_LOCAL MachineX64SourceArch machine_test_source_arch_for_brace(MachineX64SourceSpan source, MachineX64SourceSpan body, u64 brace,
                                                                              MachineX64SourceArch parent, MachineX64SourceArch else_parent)
{
    u64 segment_start = brace;
    while (segment_start > body.start)
    {
        u8 byte = source.bytes[segment_start - 1];
        if (byte == ';' || byte == '{' || byte == '}') break;
        segment_start -= 1;
    }
    MachineX64SourceArch marker_arch = machine_test_source_arch_markers(source, segment_start, brace);
    if (marker_arch != MACHINE_X64_SOURCE_ARCH_UNKNOWN) return marker_arch;
    String8 else_name = S8("else");
    for (u64 offset = segment_start; offset + else_name.length <= brace; offset += 1)
    {
        if (machine_test_source_token_at(source, offset, else_name))
            return parent == MACHINE_X64_SOURCE_ARCH_UNKNOWN && else_parent != MACHINE_X64_SOURCE_ARCH_UNKNOWN
                       ? machine_test_source_arch_inverse(else_parent)
                       : parent;
    }
    return parent;
}

BUSTER_GLOBAL_LOCAL MachineX64SourceArch machine_test_source_arch_for_writer(MachineX64SourceSpan source, MachineX64SourceSpan body,
                                                                                u64 writer_offset, MachineX64SourceArch default_arch)
{
    MachineX64SourceArch stack[256] = {0};
    u32 depth = 0;
    MachineX64SourceArch current = default_arch;
    MachineX64SourceArch last_closed = MACHINE_X64_SOURCE_ARCH_UNKNOWN;
    for (u64 offset = body.start; offset < writer_offset && offset < body.end; offset += 1)
    {
        if (source.bytes[offset] == '{')
        {
            if (depth < BUSTER_ARRAY_LENGTH(stack))
            {
                stack[depth++] = current;
                current = machine_test_source_arch_for_brace(source, body, offset, current, last_closed);
                last_closed = MACHINE_X64_SOURCE_ARCH_UNKNOWN;
            }
        }
        else if (source.bytes[offset] == '}' && depth)
        {
            last_closed = current;
            current = stack[--depth];
        }
    }
    if (current != MACHINE_X64_SOURCE_ARCH_UNKNOWN) return current;

    // A ternary architecture guard (the code-buffer alignment path is the
    // important example) has no enclosing brace.  Restrict the look-back to
    // the current statement so a previous branch cannot classify a .byte/data
    // write as an instruction.
    u64 statement_start = writer_offset;
    while (statement_start > body.start)
    {
        u8 byte = source.bytes[statement_start - 1];
        if (byte == ';' || byte == '{' || byte == '}') break;
        statement_start -= 1;
    }
    u64 question = UINT64_MAX;
    u64 colon = UINT64_MAX;
    for (u64 offset = statement_start; offset < writer_offset; offset += 1)
    {
        if (source.bytes[offset] == '?') question = offset;
    }
    if (question != UINT64_MAX)
    {
        for (u64 offset = question + 1; offset < body.end; offset += 1)
        {
            if (source.bytes[offset] == ':')
            {
                colon = offset;
                break;
            }
        }
        MachineX64SourceArch condition_arch = machine_test_source_arch_markers(source, statement_start, question);
        if (condition_arch != MACHINE_X64_SOURCE_ARCH_UNKNOWN)
        {
            if (writer_offset < colon) return condition_arch;
            if (colon != UINT64_MAX && writer_offset > colon) return machine_test_source_arch_inverse(condition_arch);
        }
    }
    MachineX64SourceArch marker_arch = machine_test_source_arch_markers(source, statement_start, writer_offset);
    MachineX64SourceArch result;
    if (marker_arch != MACHINE_X64_SOURCE_ARCH_UNKNOWN)
    {
        result = marker_arch;
    }
    else
    {
        result = default_arch;
    }

    return result;
}

BUSTER_GLOBAL_LOCAL void machine_test_source_scan_writers(MachineX64SourceSpan source, MachineX64SourceSpan body,
                                                            MachineX64SourceArch default_arch, String8 const* writers, u32 writer_count,
                                                            bool* has_x86, bool* has_aarch64, bool* has_unknown)
{
    for (u32 writer_index = 0; writer_index < writer_count; writer_index += 1)
    {
        String8 writer = writers[writer_index];
        for (u64 offset = body.start; offset + writer.length <= body.end; offset += 1)
        {
            if (!machine_test_source_token_at(source, offset, writer)) continue;
            MachineX64SourceArch arch = machine_test_source_arch_for_writer(source, body, offset, default_arch);
            if (arch == MACHINE_X64_SOURCE_ARCH_X86)
            {
                if (has_x86) *has_x86 = true;
            }
            else if (arch == MACHINE_X64_SOURCE_ARCH_AARCH64)
            {
                if (has_aarch64) *has_aarch64 = true;
            }
            else if (has_unknown)
            {
                *has_unknown = true;
            }
        }
    }
}

BUSTER_GLOBAL_LOCAL MachineX64SourceAudit machine_test_x86_source_authority_audit(Arena* arena)
{
    MachineX64SourceAudit audit = {.files_readable = true, .owners_found = true};
    typedef struct MachineX64SourceFile MachineX64SourceFile;
    struct MachineX64SourceFile
    {
        String8 path;
        MachineX64SourceArch default_arch;
    };
    typedef struct MachineX64ConsumerSite MachineX64ConsumerSite;
    struct MachineX64ConsumerSite
    {
        String8 source_file;
        String8 owner_symbol;
    };
    typedef struct MachineX64NeutralSite MachineX64NeutralSite;
    struct MachineX64NeutralSite
    {
        String8 source_file;
        String8 owner_symbol;
    };
    // Consumers are kept as migration anchors for the source audit, but are
    // not counted as authorities: their bodies must route instruction
    // construction through one of the metadata entry points.
    static MachineX64ConsumerSite const consumers[] = {
        {S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"), S8_INITIALIZER("codegen_canonical_x64_metadata_emit")},
        {S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"), S8_INITIALIZER("codegen_generate_canonical_module_attempt")},
        {S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"), S8_INITIALIZER("codegen_emit_global_assembly")},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/assembly.c"), S8_INITIALIZER("assembly_x86_metadata_emit")},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/assembly.c"), S8_INITIALIZER("assembly_instructions_emit")},
        {S8_INITIALIZER("src/buster/lib/x86_64.c"), S8_INITIALIZER("x86_64_encode_register_operation")},
        {S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"), S8_INITIALIZER("jit_emit_thunks")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_x86_emit")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_x86_build_elf_entry_stub")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_x86_build_pe_entry_stub")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_elf64_x86_64")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_elf64_x86_64_dynamic")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_pe64")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_mach_o64")},
    };
    static MachineX64NeutralSite const neutral_sites[] = {
        {S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"), S8_INITIALIZER("codegen_emit_global_assembly")},
        {S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"), S8_INITIALIZER("codegen_generate_canonical_module_attempt")},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/assembly.c"), S8_INITIALIZER("assembly_x86_metadata_local_relocation")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_address_difference")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_write_u16")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_write_u32")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_write_u32_be")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_write_u64")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_elf64_x86_64")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_elf64_x86_64_dynamic")},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), S8_INITIALIZER("link_native_executable_mach_o64")},
        {S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"), S8_INITIALIZER("jit_emit_thunks")},
        {S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"), S8_INITIALIZER("jit_apply_relocations")},
        {S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"), S8_INITIALIZER("jit_apply_aarch64_mach_page_relocation")},
    };
    static MachineX64SourceFile const files[] = {
        {S8_INITIALIZER("src/buster/lib/compiler/codegen/codegen.c"), MACHINE_X64_SOURCE_ARCH_UNKNOWN},
        {S8_INITIALIZER("src/buster/lib/compiler/assembly/assembly.c"), MACHINE_X64_SOURCE_ARCH_UNKNOWN},
        {S8_INITIALIZER("src/buster/lib/x86_64.c"), MACHINE_X64_SOURCE_ARCH_X86},
        {S8_INITIALIZER("src/buster/lib/compiler/jit/jit.c"), MACHINE_X64_SOURCE_ARCH_UNKNOWN},
        {S8_INITIALIZER("src/buster/lib/compiler/link/link.c"), MACHINE_X64_SOURCE_ARCH_UNKNOWN},
    };
    static String8 const codegen_writers[] = {
        S8_INITIALIZER("codegen_emit_u8"),
        S8_INITIALIZER("codegen_emit_u32"),
        S8_INITIALIZER("codegen_emit_u64"),
    };
    static String8 const assembly_writers[] = {
        S8_INITIALIZER("assembly_emit_byte"),
        S8_INITIALIZER("assembly_emit_immediate"),
    };
    u8 neutral_site_seen[BUSTER_ARRAY_LENGTH(neutral_sites)] = {0};
    for (u32 file_index = 0; file_index < BUSTER_ARRAY_LENGTH(files); file_index += 1)
    {
        MachineX64SourceFile file = files[file_index];
        ByteSlice input = file_read(arena, file.path, (FileReadOptions){0});
        if (!input.pointer || !input.length)
        {
            audit.files_readable = false;
            continue;
        }
        u8* sanitized = machine_test_source_sanitize(arena, input);
        MachineX64SourceSpan source = {.bytes = sanitized, .length = input.length, .start = 0, .end = input.length};

        // Neutral patch owners are audited independently of metadata
        // consumers.  A patch helper may legitimately write bytes, but the
        // owner must be named in the registry so a new writer cannot be
        // smuggled in as an unreviewed exception.
        for (u32 neutral_index = 0; neutral_index < BUSTER_ARRAY_LENGTH(neutral_sites); neutral_index += 1)
        {
            MachineX64NeutralSite site = neutral_sites[neutral_index];
            if (neutral_site_seen[neutral_index] || !string_equal(site.source_file, file.path)) continue;
            MachineX64SourceSpan body = {0};
            if (!machine_test_source_function_body(source, site.owner_symbol, &body))
            {
                audit.owners_found = false;
                continue;
            }
            neutral_site_seen[neutral_index] = 1;
            audit.neutral_patch_count += 1;
        }

        // First audit the explicitly named metadata consumers.  They are
        // stable migration anchors; byte writes in a consumer are judged by
        // the active architecture guard, not by a source line allowlist.
        for (u32 site_index = 0; site_index < BUSTER_ARRAY_LENGTH(consumers); site_index += 1)
        {
            MachineX64ConsumerSite site = consumers[site_index];
            if (!string_equal(site.source_file, file.path)) continue;
            MachineX64SourceSpan body = {0};
            if (!machine_test_source_function_body(source, site.owner_symbol, &body))
            {
                audit.owners_found = false;
                continue;
            }
            audit.owner_count += 1;
            bool has_x86 = false;
            bool has_aarch64 = false;
            bool has_unknown = false;
            bool assembly_file = string_equal(file.path, S8("src/buster/lib/compiler/assembly/assembly.c"));
            String8 const* writers = assembly_file ? assembly_writers : codegen_writers;
            u32 writer_count = assembly_file ? BUSTER_ARRAY_LENGTH(assembly_writers) : BUSTER_ARRAY_LENGTH(codegen_writers);
            machine_test_source_scan_writers(source, body, file.default_arch, writers, writer_count, &has_x86, &has_aarch64, &has_unknown);
            if (assembly_file)
            {
                // The AArch64 fixed-word/semantic branches use assembly_emit_u32
                // and never call an assembly_x86_emit_* helper.  A call to an
                // x86 helper is therefore the architecture-independent marker
                // for a handwritten x86 constructor in the mixed emitter.
                bool has_handwritten_x86 = string_equal(site.owner_symbol, S8("assembly_instructions_emit")) &&
                                           machine_test_source_span_contains_sequence(body, S8("assembly_x86_emit_"));
                if (has_handwritten_x86 || has_unknown)
                {
                    audit.forbidden_count += 1;
                    audit.forbidden_constructor_count += 1;
                }
            }
            else if (string_equal(file.path, S8("src/buster/lib/compiler/codegen/codegen.c")))
            {
                bool has_data_directive = has_unknown && machine_test_source_span_contains_sequence(body, S8(".byte"));
                if (has_x86)
                {
                    // `.byte` is data, while a target-guarded 0x90 alignment
                    // byte is an x86 instruction and remains forbidden until
                    // it is routed through metadata.
                    audit.forbidden_count += 1;
                    audit.forbidden_constructor_count += 1;
                }
                else if (has_unknown && !has_data_directive)
                {
                    // An unguarded byte writer in a mixed codegen owner is
                    // not safely attributable to AArch64.  Treat unknown as
                    // a handwritten x86 producer until an architecture guard
                    // (or a recognized .byte data parser) makes its intent
                    // explicit.
                    audit.forbidden_count += 1;
                    audit.forbidden_constructor_count += 1;
                }
                if (has_aarch64) audit.aarch64_write_count += 1;
                if (has_data_directive) audit.data_directive_count += 1;
            }
        }

        // Discover x86-named helper definitions instead of keeping a brittle
        // line-number or hand-maintained helper allowlist.  x64 helpers are
        // x86 by default; a nested AArch64 guard is handled by the brace
        // tracker above.  The assembly metadata adapter is a consumer and has
        // no assembly_emit_byte call, so it naturally remains clean.
        String8 prefixes[2] = {0};
        MachineX64SourceArch prefix_arch[2] = {0};
        u32 prefix_count = 0;
        if (string_equal(file.path, S8("src/buster/lib/compiler/codegen/codegen.c")))
        {
            prefixes[prefix_count] = S8("codegen_canonical_x64_");
            prefix_arch[prefix_count++] = MACHINE_X64_SOURCE_ARCH_X86;
            prefixes[prefix_count] = S8("x64_emit_");
            prefix_arch[prefix_count++] = MACHINE_X64_SOURCE_ARCH_X86;
        }
        else if (string_equal(file.path, S8("src/buster/lib/compiler/assembly/assembly.c")))
        {
            prefixes[prefix_count] = S8("assembly_x86_");
            prefix_arch[prefix_count++] = MACHINE_X64_SOURCE_ARCH_X86;
        }
        for (u32 prefix_index = 0; prefix_index < prefix_count; prefix_index += 1)
        {
            String8 prefix = prefixes[prefix_index];
            for (u64 offset = source.start; offset + prefix.length <= source.end; offset += 1)
            {
                if (memcmp(source.bytes + offset, prefix.pointer, prefix.length) != 0 ||
                    (offset && machine_test_source_identifier_continue(source.bytes[offset - 1])))
                    continue;
                u64 end = offset + prefix.length;
                while (end < source.end && machine_test_source_identifier_continue(source.bytes[end])) end += 1;
                String8 owner = {.pointer = (char8*)(source.bytes + offset), .length = end - offset};
                MachineX64SourceSpan body = {0};
                // The name starts at `offset`, so ask whether a definition
                // begins *here* rather than searching forward for one.  The
                // forward search made every call site cost a scan to the end
                // of the file -- half the module's time on codegen.c alone --
                // and it could also step over an intervening definition,
                // because a call site that resolved to a later definition
                // advanced the cursor past that definition's body.
                if (!machine_test_source_function_body_at(source, owner, offset, &body)) continue;
                audit.owner_count += 1;
                bool has_x86 = false;
                bool has_aarch64 = false;
                bool has_unknown = false;
                if (prefix_arch[prefix_index] == MACHINE_X64_SOURCE_ARCH_X86)
                {
                    machine_test_source_scan_writers(source, body, prefix_arch[prefix_index],
                                                      string_equal(file.path, S8("src/buster/lib/compiler/assembly/assembly.c"))
                                                          ? assembly_writers
                                                          : codegen_writers,
                                                      string_equal(file.path, S8("src/buster/lib/compiler/assembly/assembly.c"))
                                                          ? BUSTER_ARRAY_LENGTH(assembly_writers)
                                                          : BUSTER_ARRAY_LENGTH(codegen_writers),
                                                      &has_x86, &has_aarch64, &has_unknown);
                }
                if (has_x86 || has_unknown)
                {
                    audit.forbidden_count += 1;
                    audit.forbidden_constructor_count += 1;
                }
                if (has_aarch64) audit.aarch64_write_count += 1;
                offset = end - 1;
            }
        }
    }
    return audit;
}

// Every caller sits inside the executing-differential sections below, so
// the definition carries their guard: configurations that compile those
// out (non-x86-64, or the sanitized and fuzzing builds) do not pass
// -Wno-unused-function and would reject an unreferenced helper.
//
// Those sections call the emitted bytes through a native function pointer,
// and the bytes are generated for the System V ABI regardless of host,
// because the corpus fixes a Linux target so the machine path runs
// everywhere. A Microsoft-ABI host therefore passes arguments in the wrong
// registers and both paths read whatever the callee-side registers happen
// to hold, so Windows is excluded from executing — it still selects,
// verifies, places, encodes and checks fallback accounting above.
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
BUSTER_GLOBAL_LOCAL u32 machine_test_module_offset(CodegenModule* module, IrModule* ir_module, String8 name)
{
    IrFunction* ir_function = machine_test_ir_function_find(ir_module, name);
    for (u32 entry_index = 0; ir_function && entry_index < module->entry_count; entry_index += 1)
    {
        if (module->entries[entry_index].symbol.value == ir_function->symbol.value)
        {
            return module->entries[entry_index].offset;
        }
    }
    return UINT32_MAX;
}
#endif

BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_function(Arena* arena)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    u32 source = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                                .definition_point = MACHINE_POINT_INVALID,
                                                                .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                            });
    u32 destination = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                                     .definition_point = machine_point_make(0, MACHINE_POINT_AFTER),
                                                                     .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                     .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                 });
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, destination),
                                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, source)},
                                              .opcode = MACHINE_OPCODE_SKELETON_COPY,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                          });
    machine_builder_block_end(&builder, (MachineBlock){0});
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_NOP,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                          });
    machine_builder_block_end(&builder, (MachineBlock){0});
    return machine_function_builder_finish(arena, &builder);
}

BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_cmpxchg16_function(Arena* arena)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    u32 address = machine_builder_virtual_register(&builder, (MachineVirtualRegister){
                                                               .definition_point = machine_point_make(0, MACHINE_POINT_AFTER),
                                                               .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                           });
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .operands = {machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address),
                                                           machine_ref_make(MACHINE_REF_IMMEDIATE, 0)},
                                              .opcode = MACHINE_X64_MOV_RI,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){
                                              .operands = {machine_ref_make(MACHINE_REF_STACK_SLOT, 0),
                                                           machine_ref_make(MACHINE_REF_STACK_SLOT, 1),
                                                           machine_ref_make(MACHINE_REF_STACK_SLOT, 2),
                                                           machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, address)},
                                              .opcode = MACHINE_X64_ATOMIC_CMPXCHG16,
                                          });
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
    function.immediates = arena_allocate(arena, u64, 1);
    function.immediates[0] = 0;
    function.immediate_count = 1;
    function.stack_slot_sizes = arena_allocate(arena, u32, 3);
    function.stack_slot_alignments = arena_allocate(arena, u32, 3);
    for (u32 slot = 0; slot < 3; slot += 1)
    {
        function.stack_slot_sizes[slot] = 16;
        function.stack_slot_alignments[slot] = 16;
    }
    function.stack_slot_count = 3;
    return function;
}

// A zero-operand DIRECT pair used by the x86 exact-form migration tests.  The
// rows deliberately sit before RET so the encoder must emit both metadata
// forms in one ordinary placed function; no executable call is attempted
// because INT3 is intentionally a trap.
BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_exact_barrier_function(Arena* arena)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_MFENCE});
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_INT3});
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
    return function;
}

// One physical-register fixture covering every migrated register-only DIRECT
// row.  Alternating RAX/R9 makes the REX.R/REX.B projections observable while
// the paired 32/64 and narrow-extension rows keep width projections exercised
// in one encode; ALU/IMUL rows use their tied destination in slot 1.
BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_exact_register_function(Arena* arena)
{
#define MACHINE_TEST_EXACT_PHYSICAL(reg) machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, (reg))
#define MACHINE_TEST_EXACT_UNARY(op_value, reg) \
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = (op_value), .operands = {MACHINE_TEST_EXACT_PHYSICAL(reg)}})
#define MACHINE_TEST_EXACT_BINARY(op_value, destination, source) \
    machine_builder_instruction(&builder,                                                                                 \
                                (MachineInstruction){.opcode = (op_value),                                             \
                                                     .operands = {MACHINE_TEST_EXACT_PHYSICAL(destination),            \
                                                                  MACHINE_TEST_EXACT_PHYSICAL(source)}})
#define MACHINE_TEST_EXACT_TIED(op_value, destination, source) \
    machine_builder_instruction(&builder,                                                                                 \
                                (MachineInstruction){.opcode = (op_value),                                             \
                                                     .operands = {MACHINE_TEST_EXACT_PHYSICAL(destination),            \
                                                                  MACHINE_TEST_EXACT_PHYSICAL(destination),            \
                                                                  MACHINE_TEST_EXACT_PHYSICAL(source)}})
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    machine_builder_block_begin(&builder);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOV_RR, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOV32_RR, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOVSX8_RR, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOVSX16_RR, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOVSX32_RR, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOVZX8_RR, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_MOVZX16_RR, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_ADD32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_ADD64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_SUB32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_SUB64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_AND32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_AND64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_OR32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_OR64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_XOR32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_XOR64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_IMUL32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_TIED(MACHINE_X64_IMUL64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_UNARY(MACHINE_X64_NEG32, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_UNARY(MACHINE_X64_NEG64, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_UNARY(MACHINE_X64_NOT32, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_UNARY(MACHINE_X64_NOT64, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_BSF32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_BSF64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_BSR32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_BSR64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_CMP32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_CMP64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_BINARY(MACHINE_X64_TEST_RR, MACHINE_X64_RAX, MACHINE_X64_R9);
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
#undef MACHINE_TEST_EXACT_TIED
#undef MACHINE_TEST_EXACT_BINARY
#undef MACHINE_TEST_EXACT_UNARY
#undef MACHINE_TEST_EXACT_PHYSICAL
    return function;
}

// The second exact DIRECT cohort exercises feature-gated POPCNT, the
// implicit-CL shifts, XMM/GPR payload projections, and synthesized fixed-RSP
// immediate operands. Physical RAX/RCX constraints mirror the selector rows;
// MOVQ and the stack rows carry their non-machine-visible payloads directly.
BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_exact_second_register_function(Arena* arena)
{
#define MACHINE_TEST_EXACT_SECOND_PHYSICAL(reg) machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, (reg))
#define MACHINE_TEST_EXACT_SECOND_BINARY(op_value, destination, source) \
    machine_builder_instruction(&builder,                                                                                 \
                                (MachineInstruction){.opcode = (op_value),                                             \
                                                     .operands = {MACHINE_TEST_EXACT_SECOND_PHYSICAL(destination),     \
                                                                  MACHINE_TEST_EXACT_SECOND_PHYSICAL(source)}})
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    machine_builder_block_begin(&builder);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_POPCNT32, MACHINE_X64_RAX, MACHINE_X64_R9);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_POPCNT64, MACHINE_X64_R9, MACHINE_X64_RAX);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_SHL32, MACHINE_X64_RAX, MACHINE_X64_RCX);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_SHL64, MACHINE_X64_RAX, MACHINE_X64_RCX);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_SAR32, MACHINE_X64_RAX, MACHINE_X64_RCX);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_SAR64, MACHINE_X64_RAX, MACHINE_X64_RCX);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_SHR32, MACHINE_X64_RAX, MACHINE_X64_RCX);
    MACHINE_TEST_EXACT_SECOND_BINARY(MACHINE_X64_SHR64, MACHINE_X64_RAX, MACHINE_X64_RCX);
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .opcode = MACHINE_X64_MOVQ_TO_XMM,
                                                      .payload = 0,
                                                      .operands = {MACHINE_TEST_EXACT_SECOND_PHYSICAL(MACHINE_X64_R9)},
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .opcode = MACHINE_X64_MOVQ_FROM_XMM,
                                                      .payload = 1,
                                                      .operands = {MACHINE_TEST_EXACT_SECOND_PHYSICAL(MACHINE_X64_RAX)},
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .opcode = MACHINE_X64_PUSH_REGISTER,
                                                      .operands = {MACHINE_TEST_EXACT_SECOND_PHYSICAL(MACHINE_X64_R9)},
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_ADD_RSP, .payload = 16});
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
#undef MACHINE_TEST_EXACT_SECOND_BINARY
#undef MACHINE_TEST_EXACT_SECOND_PHYSICAL
    return function;
}

// LOAD_INCOMING is the final direct recipe migrated to metadata.  Keep the
// incoming offsets at the legacy producer's boundary values: payload 0 gives
// the ordinary positive disp32, 112 reaches the disp8/disp32 boundary after
// the fixed frame prefix, and 128 exercises a larger displacement.  R9/R10
// make the REX.R destination projection observable alongside RAX.
BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_exact_load_incoming_function(Arena* arena)
{
#define MACHINE_TEST_LOAD_INCOMING_PHYSICAL(reg) machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, (reg))
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .opcode = MACHINE_X64_LOAD_INCOMING,
                                                      .payload = 0,
                                                      .operands = {MACHINE_TEST_LOAD_INCOMING_PHYSICAL(MACHINE_X64_RAX)},
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .opcode = MACHINE_X64_LOAD_INCOMING,
                                                      .payload = 112,
                                                      .operands = {MACHINE_TEST_LOAD_INCOMING_PHYSICAL(MACHINE_X64_R9)},
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .opcode = MACHINE_X64_LOAD_INCOMING,
                                                      .payload = 128,
                                                      .operands = {MACHINE_TEST_LOAD_INCOMING_PHYSICAL(MACHINE_X64_R10)},
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
#undef MACHINE_TEST_LOAD_INCOMING_PHYSICAL
    return function;
}

// Relative/symbolic DIRECT rows keep their canonical fixup streams: JMP is
// queried with a neutral rel32 and patched to its block, while LEA_SYMBOL is
// queried with a neutral RIP-relative AGEN and retained as a call-site row.
// The block order gives one forward and one backward branch, and the two
// destination registers make both low and extended REX.R projections visible.
BUSTER_GLOBAL_LOCAL MachineFunction machine_test_build_exact_relative_function(Arena* arena)
{
    MachineFunctionBuilder builder = machine_function_builder_begin(arena);
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .operands = {machine_ref_make(MACHINE_REF_BLOCK, 2)},
                                                      .opcode = MACHINE_X64_JMP,
                                                  });
    machine_builder_block_end(&builder, (MachineBlock){0});
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_R8)},
                                                      .payload = 1,
                                                      .opcode = MACHINE_X64_LEA_SYMBOL,
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){.opcode = MACHINE_X64_RET});
    machine_builder_block_end(&builder, (MachineBlock){0});
    machine_builder_block_begin(&builder);
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .operands = {machine_ref_make(MACHINE_REF_PHYSICAL_REGISTER, MACHINE_X64_RAX)},
                                                      .payload = 0,
                                                      .opcode = MACHINE_X64_LEA_SYMBOL,
                                                  });
    machine_builder_instruction(&builder, (MachineInstruction){
                                                      .operands = {machine_ref_make(MACHINE_REF_BLOCK, 1)},
                                                      .opcode = MACHINE_X64_JMP,
                                                  });
    machine_builder_block_end(&builder, (MachineBlock){0});
    MachineFunction function = machine_function_builder_finish(arena, &builder);
    function.target = machine_target_x86_64();
    return function;
}

UnitTestResult machine_tests(UnitTestArguments* arguments)
{
    UnitTestResult result = {0};

    // Exercise the stateless FAST picker directly on both target register
    // files: preferred free, lowest nonpreferred free, dead-first eviction,
    // stable LRU tie order, and the nonzero-mask ctz guard (the no-free cases
    // also prove the picker never asks ctz to decode zero).
    u32 fast_picker_test_cases = machine_fast_picker_test_cases();
    BUSTER_TEST(arguments, fast_picker_test_cases & MACHINE_FAST_PICK_TEST_PREFERRED_FREE);
    BUSTER_TEST(arguments, fast_picker_test_cases & MACHINE_FAST_PICK_TEST_LOWEST_OTHER_FREE);
    BUSTER_TEST(arguments, fast_picker_test_cases & MACHINE_FAST_PICK_TEST_DEAD_FIRST);
    BUSTER_TEST(arguments, fast_picker_test_cases & MACHINE_FAST_PICK_TEST_LRU_ORDER);
    BUSTER_TEST(arguments, fast_picker_test_cases & MACHINE_FAST_PICK_TEST_CTZ_GUARD);

    MachineRef ref = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, MACHINE_REF_PAYLOAD_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_ref_kind(ref) == MACHINE_REF_VIRTUAL_REGISTER);
    BUSTER_TEST(arguments, machine_ref_payload(ref) == MACHINE_REF_PAYLOAD_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_ref_kind(MACHINE_REF_NONE_VALUE) == MACHINE_REF_NONE);
    BUSTER_TEST(arguments, machine_ref_payload(MACHINE_REF_NONE_VALUE) == 0);

    MachinePoint point = machine_point_make(MACHINE_POINT_INSTRUCTION_LIMIT - 1u, MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_point_instruction(point) == MACHINE_POINT_INSTRUCTION_LIMIT - 1u);
    BUSTER_TEST(arguments, machine_point_phase(point) == MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_point_phase(machine_point_make(7, MACHINE_POINT_BEFORE)) == MACHINE_POINT_BEFORE);
    // Phase order is the overlap contract: BEFORE < EARLY < NORMAL < AFTER
    // within one instruction, and the next instruction's BEFORE follows.
    BUSTER_TEST(arguments, machine_point_make(3, MACHINE_POINT_BEFORE) < machine_point_make(3, MACHINE_POINT_EARLY));
    BUSTER_TEST(arguments, machine_point_make(3, MACHINE_POINT_AFTER) < machine_point_make(4, MACHINE_POINT_BEFORE));

    MachineOpcodeInfo const* copy_info = machine_opcode_info(MACHINE_OPCODE_SKELETON_COPY);
    BUSTER_TEST(arguments, copy_info && copy_info->operand_count == 2);
    BUSTER_TEST(arguments, copy_info && (copy_info->operand_info[0] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_DEFINE);
    BUSTER_TEST(arguments, copy_info && (copy_info->operand_info[1] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_USE);
    MachineOpcodeInfo const* return_info = machine_opcode_info(MACHINE_OPCODE_SKELETON_RETURN);
    BUSTER_TEST(arguments, return_info && (return_info->attributes & MACHINE_OPCODE_ATTRIBUTE_TERMINATOR));
    MachineOpcodeInfo const* stack_allocate_info = machine_opcode_info(MACHINE_X64_STACK_ALLOCATE);
    BUSTER_TEST(arguments, stack_allocate_info && (stack_allocate_info->attributes & MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE));
    MachineOpcodeInfo const* cmpxchg16_info = machine_opcode_info(MACHINE_X64_ATOMIC_CMPXCHG16);
    u64 cmpxchg16_fixed = (1ull << MACHINE_X64_RAX) | (1ull << MACHINE_X64_RDX) | (1ull << MACHINE_X64_RBX) | (1ull << MACHINE_X64_RCX);
    BUSTER_TEST(arguments, cmpxchg16_info && cmpxchg16_info->operand_count == 4);
    BUSTER_TEST(arguments, cmpxchg16_info && cmpxchg16_info->operand_info[0] == 0 && cmpxchg16_info->operand_info[1] == 0 &&
                                   cmpxchg16_info->operand_info[2] == 0 &&
                                   (cmpxchg16_info->operand_info[3] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u)) == MACHINE_OPERAND_ROLE_USE);
    BUSTER_TEST(arguments, cmpxchg16_info && (cmpxchg16_info->attributes & MACHINE_OPCODE_ATTRIBUTE_CONSTRAINED) &&
                                   (cmpxchg16_info->attributes & MACHINE_OPCODE_ATTRIBUTE_FLAGS_DEFINE) &&
                                   cmpxchg16_info->clobber_mask == cmpxchg16_fixed);
    BUSTER_TEST(arguments, machine_opcode_info(MACHINE_OPCODE_COUNT) == 0);

    // A metadata-only CMPXCHG16B clobber must preserve RBX in every
    // placement mode, even though this fixture has no virtual value bound to
    // RBX. Its address is constrained to RSI and the encoder must retain the
    // old value from RAX:RDX in the result slot after the instruction.
    MachineFunction cmpxchg16_function = machine_test_build_cmpxchg16_function(arguments->arena);
    BUSTER_TEST(arguments, machine_verify_function(&cmpxchg16_function).error == MACHINE_VERIFY_NONE);
    MachineStackPlacement cmpxchg16_mir = machine_stack_placement_build(arguments->arena, &cmpxchg16_function);
    MachineStackPlacement cmpxchg16_fast = machine_fast_placement_build(arguments->arena, &cmpxchg16_function);
    MachineStackPlacement cmpxchg16_quality = machine_quality_placement_build(arguments->arena, &cmpxchg16_function);
    u64 rbx_mask = 1ull << MACHINE_X64_RBX;
    BUSTER_TEST(arguments, cmpxchg16_mir.valid && cmpxchg16_fast.valid && cmpxchg16_quality.valid);
    BUSTER_TEST(arguments, (cmpxchg16_mir.callee_saved_mask & rbx_mask) && (cmpxchg16_fast.callee_saved_mask & rbx_mask) &&
                                   (cmpxchg16_quality.callee_saved_mask & rbx_mask));
    BUSTER_TEST(arguments, (cmpxchg16_mir.frame_size + 8 * ((cmpxchg16_mir.callee_saved_mask >> MACHINE_X64_RBX) & 1u)) % 16 == 0);
    BUSTER_TEST(arguments, (cmpxchg16_fast.frame_size + 8 * ((cmpxchg16_fast.callee_saved_mask >> MACHINE_X64_RBX) & 1u)) % 16 == 0);
    BUSTER_TEST(arguments, (cmpxchg16_quality.frame_size + 8 * ((cmpxchg16_quality.callee_saved_mask >> MACHINE_X64_RBX) & 1u)) % 16 == 0);
    // MIR_STACK must put every home below the callee-saved save area.  The
    // CMPXCHG16B fixture has no virtual registers, so check its selector
    // stack slots directly: slot zero must not alias [RBP-8], where the
    // generated prologue saves RBX.
    u32 cmpxchg16_push_area = 8;
    BUSTER_TEST(arguments, cmpxchg16_mir.stack_slot_offsets[0] > cmpxchg16_push_area &&
                               cmpxchg16_mir.stack_slot_offsets[1] > cmpxchg16_push_area &&
                               cmpxchg16_mir.stack_slot_offsets[2] > cmpxchg16_push_area);
    u32 cmpxchg16_row = 1;
    BUSTER_TEST(arguments, cmpxchg16_mir.operand_registers[cmpxchg16_row * 4 + 3] == MACHINE_X64_RSI &&
                                   cmpxchg16_fast.operand_registers[cmpxchg16_row * 4 + 3] == MACHINE_X64_RSI &&
                                   cmpxchg16_quality.operand_registers[cmpxchg16_row * 4 + 3] == MACHINE_X64_RSI);
    MachineEncodeResult cmpxchg16_encoded = machine_encode_x86_64(arguments->arena, &cmpxchg16_function, &cmpxchg16_fast);
    bool saw_cmpxchg16 = false;
    bool saw_result_store = false;
    for (u32 byte = 0; cmpxchg16_encoded.valid && byte + 5 <= cmpxchg16_encoded.byte_count; byte += 1)
    {
        if (cmpxchg16_encoded.bytes[byte] == 0xf0 && cmpxchg16_encoded.bytes[byte + 1] == 0x48 &&
            cmpxchg16_encoded.bytes[byte + 2] == 0x0f && cmpxchg16_encoded.bytes[byte + 3] == 0xc7 && cmpxchg16_encoded.bytes[byte + 4] == 0x0e)
        {
            saw_cmpxchg16 = true;
        }
        if (saw_cmpxchg16 && byte + 3 <= cmpxchg16_encoded.byte_count && cmpxchg16_encoded.bytes[byte] == 0x48 &&
            cmpxchg16_encoded.bytes[byte + 1] == 0x89 && cmpxchg16_encoded.bytes[byte + 2] == 0x85)
        {
            saw_result_store = true;
        }
    }
    BUSTER_TEST(arguments, cmpxchg16_encoded.valid && saw_cmpxchg16 && saw_result_store);
    // Recipe IDs are a packed, target-local policy namespace.  Keep this
    // contract independent from exact encoding, selection, and scheduling
    // tables, and audit every current opcode so a newly added row cannot
    // silently inherit NONE.
    MachineEmitRecipeId sample_recipe = MACHINE_EMIT_RECIPE_FAMILY_BASE + 37;
    BUSTER_TEST(arguments, machine_emit_recipe_is_valid(sample_recipe));
    BUSTER_TEST(arguments, machine_emit_recipe_category(sample_recipe) == MACHINE_EMIT_RECIPE_CATEGORY_FAMILY);
    BUSTER_TEST(arguments, machine_emit_recipe_index(sample_recipe) == 37);
    BUSTER_TEST(arguments, machine_emit_recipe_category(MACHINE_EMIT_RECIPE_INVALID) == MACHINE_EMIT_RECIPE_CATEGORY_COUNT);
    BUSTER_TEST(arguments, !machine_emit_recipe_is_valid(MACHINE_EMIT_RECIPE_INVALID));
    BUSTER_TEST(arguments, SELECTION_PATTERN_ID_INVALID == (SelectionPatternId)UINT16_MAX);
    BUSTER_TEST(arguments, SCHEDULING_CLASS_ID_INVALID == (SchedulingClassId)UINT16_MAX);

    u32 recipe_counts[MACHINE_EMIT_RECIPE_CATEGORY_COUNT] = {0};
    bool recipe_indices_in_range = true;
    for (u16 opcode = 0; opcode < MACHINE_OPCODE_COUNT; opcode += 1)
    {
        MachineEmitRecipeId recipe = machine_opcode_emit_recipe(opcode);
        MachineEmitRecipeCategory category = machine_emit_recipe_category(recipe);
        MachineOpcodeInfo const* info = machine_opcode_info(opcode);
        recipe_indices_in_range &= info && machine_emit_recipe_is_valid(recipe);
        if (category < MACHINE_EMIT_RECIPE_CATEGORY_COUNT)
        {
            recipe_counts[category] += 1;
        }
    }
    BUSTER_TEST(arguments, recipe_indices_in_range);
    BUSTER_TEST(arguments, recipe_counts[MACHINE_EMIT_RECIPE_CATEGORY_NONE] == 4);
    BUSTER_TEST(arguments, recipe_counts[MACHINE_EMIT_RECIPE_CATEGORY_DIRECT] == 98);
    BUSTER_TEST(arguments, recipe_counts[MACHINE_EMIT_RECIPE_CATEGORY_FAMILY] == 53);
    BUSTER_TEST(arguments, recipe_counts[MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION] == 77);
    BUSTER_TEST(arguments, machine_opcode_emit_recipe(MACHINE_OPCODE_COUNT) == MACHINE_EMIT_RECIPE_INVALID);

    u32 x64_counts[MACHINE_EMIT_RECIPE_CATEGORY_COUNT] = {0};
    for (u16 opcode = MACHINE_X64_MOV_RI; opcode <= MACHINE_X64_VBINARY; opcode += 1)
    {
        x64_counts[machine_emit_recipe_category(machine_opcode_emit_recipe(opcode))] += 1;
    }
    BUSTER_TEST(arguments, x64_counts[MACHINE_EMIT_RECIPE_CATEGORY_DIRECT] == 47);
    BUSTER_TEST(arguments, x64_counts[MACHINE_EMIT_RECIPE_CATEGORY_FAMILY] == 50);
    BUSTER_TEST(arguments, x64_counts[MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION] == 26);

    // The x86-64 producer registry is the Phase-0 census used to keep the
    // encoder switch and recipe projection from drifting independently.  It
    // is indexed by the contiguous opcode span, while rows remain grouped by
    // recipe category in the source registry for reviewability.
    u32 registry_count = machine_x86_64_emit_registry_count();
    BUSTER_TEST(arguments, registry_count == MACHINE_X86_64_EMIT_REGISTRY_COUNT);
    BUSTER_TEST(arguments, (u32)(MACHINE_X64_VBINARY - MACHINE_X64_MOV_RI + 1) == registry_count);
    u32 registry_counts[MACHINE_EMIT_RECIPE_CATEGORY_COUNT] = {0};
    u32 registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_COUNT] = {0};
    bool registry_entries_are_complete = true;
    bool registry_recipes_match = true;
    bool registry_statuses_are_valid = true;
    bool exact_rows_are_explicit = true;
    bool exact_rows_are_not_legacy = true;
    bool exact_rows_have_direct_indices = true;
    bool expansion_rows_are_policy = true;
    bool remaining_rows_are_not_legacy = true;
    for (u32 ordinal = 0; ordinal < registry_count; ordinal += 1)
    {
        MachineX64EmitRegistryEntry const* entry = machine_x86_64_emit_registry_entry(ordinal);
        MachineOpcode expected_opcode = (MachineOpcode)(MACHINE_X64_MOV_RI + ordinal);
        bool row_is_complete = entry && entry->opcode == expected_opcode && entry->producer_ordinal == ordinal;
        registry_entries_are_complete &= row_is_complete;
        if (row_is_complete)
        {
            MachineEmitRecipeCategory category = machine_emit_recipe_category(entry->recipe);
            MachineX64EmitProducerStatus status = (MachineX64EmitProducerStatus)entry->producer_status;
            bool status_is_valid = status < MACHINE_X64_EMIT_PRODUCER_STATUS_COUNT;
            // The direct cohort already migrated to exact-form recipes is
            // explicit here rather than inferred from the opcode span: the
            // x86 enum interleaves family and expansion rows between direct
            // rows.  Keeping the expected recipe index beside each cohort
            // catches a reordered row as well as a stale status token.
            // LOAD_INCOMING (direct index 42) uses an explicit machine-only
            // disp32 policy so its metadata projection preserves the legacy
            // spelling at every incoming-argument offset.
            bool expected_exact = false;
            bool expected_exact_sequence = false;
            MachineEmitRecipeCategory expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_NONE;
            u16 expected_exact_index = 0;
            if (entry->opcode == MACHINE_X64_MOV_RI || entry->opcode == MACHINE_X64_LEA_OFFSET || entry->opcode == MACHINE_X64_ADD64_IMM ||
                entry->opcode == MACHINE_X64_IMUL64_RRI || entry->opcode == MACHINE_X64_LOAD_FRAME ||
                (entry->opcode >= MACHINE_X64_STORE_FRAME8 && entry->opcode <= MACHINE_X64_STORE_FRAME64) ||
                (entry->opcode >= MACHINE_X64_LOAD_PTR8 && entry->opcode <= MACHINE_X64_LOAD_PTR64) ||
                (entry->opcode >= MACHINE_X64_STORE_PTR8 && entry->opcode <= MACHINE_X64_STORE_PTR64) ||
                entry->opcode == MACHINE_X64_LEA_FRAME)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_FAMILY;
                if (entry->opcode == MACHINE_X64_MOV_RI) expected_exact_index = 0;
                else if (entry->opcode == MACHINE_X64_LEA_OFFSET) expected_exact_index = 1;
                else if (entry->opcode == MACHINE_X64_ADD64_IMM) expected_exact_index = 2;
                else if (entry->opcode == MACHINE_X64_IMUL64_RRI) expected_exact_index = 3;
                else if (entry->opcode == MACHINE_X64_LOAD_FRAME) expected_exact_index = 4;
                else if (entry->opcode >= MACHINE_X64_STORE_FRAME8 && entry->opcode <= MACHINE_X64_STORE_FRAME64)
                    expected_exact_index = (u16)(5 + entry->opcode - MACHINE_X64_STORE_FRAME8);
                else if (entry->opcode >= MACHINE_X64_LOAD_PTR8 && entry->opcode <= MACHINE_X64_LOAD_PTR64)
                    expected_exact_index = (u16)(9 + entry->opcode - MACHINE_X64_LOAD_PTR8);
                else if (entry->opcode >= MACHINE_X64_STORE_PTR8 && entry->opcode <= MACHINE_X64_STORE_PTR64)
                    expected_exact_index = (u16)(13 + entry->opcode - MACHINE_X64_STORE_PTR8);
                else expected_exact_index = 17;
            }
            if (entry->opcode >= MACHINE_X64_MOV_RR && entry->opcode <= MACHINE_X64_NOT64)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = (u16)(entry->opcode - MACHINE_X64_MOV_RR);
            }
            else if (entry->opcode >= MACHINE_X64_BSF32 && entry->opcode <= MACHINE_X64_BSR64)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = (u16)(23 + (entry->opcode - MACHINE_X64_BSF32));
            }
            else if (entry->opcode >= MACHINE_X64_POPCNT32 && entry->opcode <= MACHINE_X64_POPCNT64)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = (u16)(27 + (entry->opcode - MACHINE_X64_POPCNT32));
            }
            else if (entry->opcode >= MACHINE_X64_CMP32 && entry->opcode <= MACHINE_X64_TEST_RR)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = (u16)(29 + (entry->opcode - MACHINE_X64_CMP32));
            }
            else if (entry->opcode == MACHINE_X64_JMP)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = 32;
            }
            else if (entry->opcode >= MACHINE_X64_SHL32 && entry->opcode <= MACHINE_X64_SHR64)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = (u16)(33 + (entry->opcode - MACHINE_X64_SHL32));
            }
            else if (entry->opcode == MACHINE_X64_LEA_SYMBOL)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = 39;
            }
            else if (entry->opcode >= MACHINE_X64_MOVQ_TO_XMM && entry->opcode <= MACHINE_X64_MOVQ_FROM_XMM)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = (u16)(40 + (entry->opcode - MACHINE_X64_MOVQ_TO_XMM));
            }
            else if (entry->opcode == MACHINE_X64_LOAD_INCOMING)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = 42;
            }
            else if (entry->opcode == MACHINE_X64_PUSH_REGISTER || entry->opcode == MACHINE_X64_ADD_RSP)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = entry->opcode == MACHINE_X64_PUSH_REGISTER ? 43 : 44;
            }
            else if (entry->opcode == MACHINE_X64_MFENCE || entry->opcode == MACHINE_X64_INT3)
            {
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_DIRECT;
                expected_exact_index = entry->opcode == MACHINE_X64_MFENCE ? 45 : 46;
            }
            else if (category == MACHINE_EMIT_RECIPE_CATEGORY_FAMILY)
            {
                u16 family_index = machine_emit_recipe_index(entry->recipe);
                expected_exact = true;
                expected_exact_category = MACHINE_EMIT_RECIPE_CATEGORY_FAMILY;
                expected_exact_index = family_index;
                expected_exact_sequence = (family_index >= 26 && family_index <= 31) || family_index == 35 ||
                                          (family_index >= 36 && family_index <= 45) || family_index == 47 || family_index == 48;
            }
            registry_statuses_are_valid &= status_is_valid;
            registry_recipes_match &= machine_emit_recipe_is_valid(entry->recipe);
            registry_recipes_match &= machine_opcode_emit_recipe((u16)entry->opcode) == entry->recipe;
            registry_recipes_match &= machine_x86_64_emit_registry_find(entry->opcode) == entry;
            bool status_is_exact = status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM || status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE;
            exact_rows_are_explicit &= expected_exact ? status_is_exact : !status_is_exact;
            if (expected_exact)
            {
                exact_rows_are_not_legacy &= status != MACHINE_X64_EMIT_PRODUCER_STATUS_LEGACY_RAW;
                exact_rows_have_direct_indices &= category == expected_exact_category && machine_emit_recipe_index(entry->recipe) == expected_exact_index;
                exact_rows_have_direct_indices &= expected_exact_sequence ? status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE
                                                                           : status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM;
            }
            if (category == MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION)
            {
                expansion_rows_are_policy &= status == MACHINE_X64_EMIT_PRODUCER_STATUS_EXPANSION_POLICY;
            }
            else if (category != MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION)
            {
                remaining_rows_are_not_legacy &= status != MACHINE_X64_EMIT_PRODUCER_STATUS_LEGACY_RAW;
            }
            if (status_is_valid)
            {
                registry_status_counts[status] += 1;
            }
            if (category < MACHINE_EMIT_RECIPE_CATEGORY_COUNT)
            {
                registry_counts[category] += 1;
            }
        }
    }
    BUSTER_TEST(arguments, registry_entries_are_complete);
    BUSTER_TEST(arguments, registry_recipes_match);
    BUSTER_TEST(arguments, registry_statuses_are_valid);
    BUSTER_TEST(arguments, exact_rows_are_explicit);
    BUSTER_TEST(arguments, exact_rows_are_not_legacy);
    BUSTER_TEST(arguments, exact_rows_have_direct_indices);
    BUSTER_TEST(arguments, expansion_rows_are_policy);
    BUSTER_TEST(arguments, remaining_rows_are_not_legacy);
    BUSTER_TEST(arguments, registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_LEGACY_RAW] == MACHINE_X86_64_EMIT_REGISTRY_LEGACY_RAW_COUNT);
    BUSTER_TEST(arguments, registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM] == MACHINE_X86_64_EMIT_REGISTRY_EXACT_FORM_COUNT);
    BUSTER_TEST(arguments, registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE] == MACHINE_X86_64_EMIT_REGISTRY_EXACT_SEQUENCE_COUNT);
    BUSTER_TEST(arguments, registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_FORM] +
                             registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_EXACT_SEQUENCE] == MACHINE_X86_64_EMIT_REGISTRY_EXACT_COUNT);
    BUSTER_TEST(arguments, registry_status_counts[MACHINE_X64_EMIT_PRODUCER_STATUS_EXPANSION_POLICY] == MACHINE_X86_64_EMIT_REGISTRY_EXPANSION_POLICY_COUNT);
    BUSTER_TEST(arguments, registry_counts[MACHINE_EMIT_RECIPE_CATEGORY_DIRECT] == MACHINE_X86_64_EMIT_REGISTRY_DIRECT_COUNT);
    BUSTER_TEST(arguments, registry_counts[MACHINE_EMIT_RECIPE_CATEGORY_FAMILY] == MACHINE_X86_64_EMIT_REGISTRY_FAMILY_COUNT);
    BUSTER_TEST(arguments, registry_counts[MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION] == MACHINE_X86_64_EMIT_REGISTRY_EXPANSION_COUNT);
    BUSTER_TEST(arguments, machine_x86_64_emit_registry_entry(registry_count) == 0);
    BUSTER_TEST(arguments, machine_x86_64_emit_registry_find(MACHINE_X64_MOV_RI - 1) == 0);
    BUSTER_TEST(arguments, machine_x86_64_emit_registry_find(MACHINE_X64_VBINARY + 1) == 0);

    // Serial exact prewarm publishes one immutable row map for all workers.
    // Audit every row after publication: exact forms and sequences must have
    // every variant token valid, while expansion-policy rows must remain
    // deliberately non-exact so the machine layer cannot accidentally route
    // them through a stale one-form plan.
    MachineX64ExactMapAudit exact_map = machine_x86_64_exact_map_audit();
    BUSTER_TEST(arguments, exact_map.valid);
    BUSTER_TEST(arguments, exact_map.registry_rows == registry_count);
    BUSTER_TEST(arguments, exact_map.exact_rows == MACHINE_X86_64_EMIT_REGISTRY_EXACT_COUNT);
    BUSTER_TEST(arguments, exact_map.exact_plan_valid_rows == exact_map.exact_rows);
    BUSTER_TEST(arguments, exact_map.sequence_rows == MACHINE_X86_64_EMIT_REGISTRY_EXACT_SEQUENCE_COUNT);
    BUSTER_TEST(arguments, exact_map.sequence_variant_valid_rows == exact_map.sequence_rows);
    BUSTER_TEST(arguments, exact_map.expansion_rows == MACHINE_X86_64_EMIT_REGISTRY_EXPANSION_COUNT);
    BUSTER_TEST(arguments, exact_map.expansion_nonexact_rows == exact_map.expansion_rows);
    BUSTER_TEST(arguments, exact_map.dense_encoding_tables == 64);
    BUSTER_TEST(arguments, exact_map.immediate_patch_tables == 9);
    BUSTER_TEST(arguments, exact_map.memory_base_tables == 8);
    BUSTER_TEST(arguments, exact_map.displacement_patch_tables == 8);
    BUSTER_TEST(arguments, exact_map.variable_memory_encoding_tables == 9);
    MachineX64MetadataShapeCacheAudit metadata_shape_cache = machine_x86_64_metadata_shape_cache_audit();
    BUSTER_TEST(arguments, metadata_shape_cache.valid);
    BUSTER_TEST(arguments, metadata_shape_cache.prepared_rows == 170);
    BUSTER_TEST(arguments, metadata_shape_cache.invalid_rows == 0);

    // Canonical metadata authorities and neutral patch helpers are separate
    // records.  The source audit below validates their shape and ownership;
    // the strict zero-manual-producer assertion is enabled once the remaining
    // codegen/assembly migration lands.
    u32 authority_count = machine_x86_64_canonical_authority_site_count();
    u32 patch_count = machine_x86_64_neutral_patch_site_count();
    BUSTER_TEST(arguments, authority_count == MACHINE_X86_64_CANONICAL_AUTHORITY_SITE_COUNT);
    BUSTER_TEST(arguments, patch_count == MACHINE_X86_64_NEUTRAL_PATCH_SITE_COUNT);
    u32 authority_kind_counts[MACHINE_X64_CANONICAL_AUTHORITY_KIND_COUNT] = {0};
    u32 patch_class_counts[MACHINE_X64_NEUTRAL_PATCH_CLASS_COUNT] = {0};
    bool authority_records_are_well_formed = true;
    bool authority_records_are_unique = true;
    bool patch_records_are_well_formed = true;
    bool patch_records_are_unique = true;
    for (u32 site_index = 0; site_index < authority_count; site_index += 1)
    {
        MachineX64CanonicalAuthoritySite const* site = machine_x86_64_canonical_authority_site(site_index);
        bool site_is_well_formed = site && site->authority_kind < MACHINE_X64_CANONICAL_AUTHORITY_KIND_COUNT && site->source_file.length != 0 &&
                                   site->owner_symbol.length != 0 && string_ends_with_sequence(site->source_file, S8(".c"));
        authority_records_are_well_formed &= site_is_well_formed;
        if (site_is_well_formed)
        {
            authority_kind_counts[site->authority_kind] += 1;
            for (u32 previous_index = 0; previous_index < site_index; previous_index += 1)
            {
                MachineX64CanonicalAuthoritySite const* previous = machine_x86_64_canonical_authority_site(previous_index);
                authority_records_are_unique &= !previous || !string_equal(site->source_file, previous->source_file) ||
                                               !string_equal(site->owner_symbol, previous->owner_symbol);
            }
        }
    }
    for (u32 site_index = 0; site_index < patch_count; site_index += 1)
    {
        MachineX64NeutralPatchSite const* site = machine_x86_64_neutral_patch_site(site_index);
        bool site_is_well_formed = site && site->patch_class < MACHINE_X64_NEUTRAL_PATCH_CLASS_COUNT && site->source_file.length != 0 &&
                                   site->owner_symbol.length != 0 && string_ends_with_sequence(site->source_file, S8(".c"));
        patch_records_are_well_formed &= site_is_well_formed;
        if (site_is_well_formed)
        {
            patch_class_counts[site->patch_class] += 1;
            for (u32 previous_index = 0; previous_index < site_index; previous_index += 1)
            {
                MachineX64NeutralPatchSite const* previous = machine_x86_64_neutral_patch_site(previous_index);
                patch_records_are_unique &= !previous || !string_equal(site->source_file, previous->source_file) ||
                                            !string_equal(site->owner_symbol, previous->owner_symbol);
            }
        }
    }
    BUSTER_TEST(arguments, authority_records_are_well_formed && authority_records_are_unique);
    BUSTER_TEST(arguments, patch_records_are_well_formed && patch_records_are_unique);
    BUSTER_TEST(arguments, authority_kind_counts[MACHINE_X64_CANONICAL_AUTHORITY_METADATA_CHECKED] != 0);
    BUSTER_TEST(arguments, authority_kind_counts[MACHINE_X64_CANONICAL_AUTHORITY_METADATA_EXACT] != 0);
    BUSTER_TEST(arguments, patch_class_counts[MACHINE_X64_NEUTRAL_PATCH_RELOCATION] != 0);
    BUSTER_TEST(arguments, patch_class_counts[MACHINE_X64_NEUTRAL_PATCH_DISPLACEMENT] != 0);
    BUSTER_TEST(arguments, patch_class_counts[MACHINE_X64_NEUTRAL_PATCH_DATA] != 0);
    BUSTER_TEST(arguments, patch_class_counts[MACHINE_X64_NEUTRAL_PATCH_TARGET_PAYLOAD] != 0);
    MachineX64SourceAudit source_audit = machine_test_x86_source_authority_audit(arguments->arena);
    // Packaged runtimes (notably Android) do not carry the repository source
    // tree, so the scanner cannot discover its five audit files there.  Keep
    // the authority gate strict whenever all sources are readable, while
    // treating an unavailable source tree as an inapplicable audit rather
    // than a producer violation.
    bool source_audit_available = source_audit.files_readable;
    BUSTER_TEST(arguments, !source_audit_available || source_audit.owners_found);
    BUSTER_TEST(arguments, !source_audit_available || source_audit.neutral_patch_count == patch_count);
    // The source audit is the final authority gate: every handwritten x86
    // constructor must route through metadata, while AArch64 words, .byte
    // data, and registered neutral patches remain explicitly classified.
    BUSTER_TEST(arguments, !source_audit_available || source_audit.forbidden_count == 0);

    u32 a64_counts[MACHINE_EMIT_RECIPE_CATEGORY_COUNT] = {0};
    for (u16 opcode = MACHINE_A64_MOV_RI; opcode <= MACHINE_A64_LEA_SYMBOL; opcode += 1)
    {
        a64_counts[machine_emit_recipe_category(machine_opcode_emit_recipe(opcode))] += 1;
    }
    BUSTER_TEST(arguments, a64_counts[MACHINE_EMIT_RECIPE_CATEGORY_DIRECT] == 51);
    BUSTER_TEST(arguments, a64_counts[MACHINE_EMIT_RECIPE_CATEGORY_FAMILY] == 3);
    BUSTER_TEST(arguments, a64_counts[MACHINE_EMIT_RECIPE_CATEGORY_EXPANSION] == 19);

    MachineFunction function = machine_test_build_function(arguments->arena);
    BUSTER_TEST(arguments, function.instruction_count == 4);
    BUSTER_TEST(arguments, function.virtual_register_count == 2);
    BUSTER_TEST(arguments, function.block_count == 2);
    BUSTER_TEST(arguments, function.blocks[0].first_instruction == 0 && function.blocks[0].instruction_count == 2);
    BUSTER_TEST(arguments, function.blocks[1].first_instruction == 2 && function.blocks[1].instruction_count == 2);
    BUSTER_TEST(arguments, function.instructions[0].opcode == MACHINE_OPCODE_SKELETON_COPY);
    BUSTER_TEST(arguments, machine_verify_function(&function).error == MACHINE_VERIFY_NONE);

    // A one-chunk stream is already contiguous in the builder arena. Finish
    // must publish its live payload directly rather than copying it, while
    // retaining the ordinary row values and block layout.
    MachineFunctionBuilder alias_builder = machine_function_builder_begin(arguments->arena);
    machine_builder_virtual_register(&alias_builder, (MachineVirtualRegister){
                                                            .definition_point = MACHINE_POINT_INVALID,
                                                            .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                        });
    machine_builder_block_begin(&alias_builder);
    machine_builder_instruction(&alias_builder, (MachineInstruction){.opcode = MACHINE_OPCODE_SKELETON_RETURN});
    machine_builder_block_end(&alias_builder, (MachineBlock){0});
    MachineBuilderChunk* alias_instruction_chunk = alias_builder.instructions.first;
    MachineBuilderChunk* alias_virtual_register_chunk = alias_builder.virtual_registers.first;
    MachineBuilderChunk* alias_block_chunk = alias_builder.blocks.first;
    MachineFunction alias_function = machine_function_builder_finish(arguments->arena, &alias_builder);
    BUSTER_TEST(arguments, alias_function.instructions == (MachineInstruction*)(alias_instruction_chunk + 1));
    BUSTER_TEST(arguments, alias_function.virtual_registers == (MachineVirtualRegister*)(alias_virtual_register_chunk + 1));
    BUSTER_TEST(arguments, alias_function.blocks == (MachineBlock*)(alias_block_chunk + 1));
    BUSTER_TEST(arguments, alias_function.instruction_count == 1 && alias_function.instructions[0].opcode == MACHINE_OPCODE_SKELETON_RETURN);
    BUSTER_TEST(arguments, machine_verify_function(&alias_function).error == MACHINE_VERIFY_NONE);

    // The chunked builder must survive chunk boundaries: enough rows to
    // spill across several 16 KiB chunks, flattened back in exact order.
    u32 large_count = 3000;
    MachineFunctionBuilder large_builder = machine_function_builder_begin(arguments->arena);
    machine_builder_block_begin(&large_builder);
    for (u32 index = 0; index < large_count - 1; index += 1)
    {
        machine_builder_instruction(&large_builder, (MachineInstruction){
                                                        .payload = index,
                                                        .opcode = MACHINE_OPCODE_SKELETON_NOP,
                                                    });
    }
    machine_builder_instruction(&large_builder, (MachineInstruction){
                                                    .payload = large_count - 1,
                                                    .opcode = MACHINE_OPCODE_SKELETON_RETURN,
                                                });
    machine_builder_block_end(&large_builder, (MachineBlock){0});
    MachineBuilderChunk* large_instruction_chunk = large_builder.instructions.first;
    MachineFunction large = machine_function_builder_finish(arguments->arena, &large_builder);
    BUSTER_TEST(arguments, large.instruction_count == large_count);
    BUSTER_TEST(arguments, large.instructions != (MachineInstruction*)(large_instruction_chunk + 1));
    bool payloads_ordered = true;
    for (u32 index = 0; index < large.instruction_count; index += 1)
    {
        payloads_ordered &= large.instructions[index].payload == index;
    }
    BUSTER_TEST(arguments, payloads_ordered);
    BUSTER_TEST(arguments, machine_verify_function(&large).error == MACHINE_VERIFY_NONE);

    // A caller may deliberately finish into a different arena and release
    // the builder arena immediately. That path must copy even a one-chunk
    // stream so the returned function owns only the finish arena.
    Arena* cross_builder_arena = arena_create((ArenaCreation){0});
    Arena* cross_function_arena = arena_create((ArenaCreation){0});
    MachineFunctionBuilder cross_builder = machine_function_builder_begin(cross_builder_arena);
    machine_builder_block_begin(&cross_builder);
    machine_builder_instruction(&cross_builder, (MachineInstruction){.payload = 17, .opcode = MACHINE_OPCODE_SKELETON_RETURN});
    machine_builder_block_end(&cross_builder, (MachineBlock){0});
    MachineBuilderChunk* cross_instruction_chunk = cross_builder.instructions.first;
    MachineFunction cross_function = machine_function_builder_finish(cross_function_arena, &cross_builder);
    MachineInstruction* cross_payload = (MachineInstruction*)(cross_instruction_chunk + 1);
    BUSTER_TEST(arguments, cross_function.instructions != cross_payload && cross_function.instructions[0].payload == 17);
    BUSTER_TEST(arguments, arena_destroy(cross_builder_arena, 1));
    BUSTER_TEST(arguments, cross_function.instructions[0].opcode == MACHINE_OPCODE_SKELETON_RETURN && cross_function.instructions[0].payload == 17);
    BUSTER_TEST(arguments, arena_destroy(cross_function_arena, 1));

    // Verifier rejections. Each case mutates a fresh valid function so a
    // single detected defect cannot mask another.
    MachineFunction bad_opcode = machine_test_build_function(arguments->arena);
    bad_opcode.instructions[0].opcode = MACHINE_OPCODE_INVALID;
    BUSTER_TEST(arguments, machine_verify_function(&bad_opcode).error == MACHINE_VERIFY_OPCODE);
    MachineFunction bad_opcode_range = machine_test_build_function(arguments->arena);
    bad_opcode_range.instructions[0].opcode = MACHINE_OPCODE_COUNT;
    BUSTER_TEST(arguments, machine_verify_function(&bad_opcode_range).error == MACHINE_VERIFY_OPCODE);

    MachineFunction early_terminator = machine_test_build_function(arguments->arena);
    early_terminator.instructions[0].opcode = MACHINE_OPCODE_SKELETON_RETURN;
    early_terminator.instructions[0].operands[0] = MACHINE_REF_NONE_VALUE;
    early_terminator.instructions[0].operands[1] = MACHINE_REF_NONE_VALUE;
    BUSTER_TEST(arguments, machine_verify_function(&early_terminator).error == MACHINE_VERIFY_TERMINATOR);
    MachineFunction missing_terminator = machine_test_build_function(arguments->arena);
    missing_terminator.instructions[3].opcode = MACHINE_OPCODE_SKELETON_NOP;
    BUSTER_TEST(arguments, machine_verify_function(&missing_terminator).error == MACHINE_VERIFY_TERMINATOR);

    MachineFunction bad_reference = machine_test_build_function(arguments->arena);
    bad_reference.instructions[0].operands[1] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, bad_reference.virtual_register_count);
    MachineVerifyResult reference_result = machine_verify_function(&bad_reference);
    BUSTER_TEST(arguments, reference_result.error == MACHINE_VERIFY_OPERAND_REFERENCE && reference_result.operand == 1);

    MachineFunction dirty_slot = machine_test_build_function(arguments->arena);
    dirty_slot.instructions[0].operands[2] = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, 0);
    BUSTER_TEST(arguments, machine_verify_function(&dirty_slot).error == MACHINE_VERIFY_OPERAND_SLOT);

    MachineFunction bad_range = machine_test_build_function(arguments->arena);
    bad_range.blocks[1].first_instruction = 3;
    BUSTER_TEST(arguments, machine_verify_function(&bad_range).error == MACHINE_VERIFY_BLOCK_RANGE);
    MachineFunction uncovered = machine_test_build_function(arguments->arena);
    uncovered.blocks[1].instruction_count = 1;
    BUSTER_TEST(arguments, machine_verify_function(&uncovered).error != MACHINE_VERIFY_NONE);

    MachineFunction bad_definition = machine_test_build_function(arguments->arena);
    bad_definition.virtual_registers[1].definition_point = machine_point_make(function.instruction_count, MACHINE_POINT_AFTER);
    BUSTER_TEST(arguments, machine_verify_function(&bad_definition).error == MACHINE_VERIFY_VIRTUAL_REGISTER_DEFINITION);

    // Replay round-trip: byte-exact reconstruction, then hard rejection of
    // corrupted headers and truncated payloads.
    ByteSlice replay = machine_replay_serialize(arguments->arena, &function);
    MachineFunction replayed = {0};
    BUSTER_TEST(arguments, machine_replay_deserialize(arguments->arena, replay, &replayed));
    BUSTER_TEST(arguments, replayed.instruction_count == function.instruction_count);
    BUSTER_TEST(arguments, replayed.virtual_register_count == function.virtual_register_count);
    BUSTER_TEST(arguments, replayed.block_count == function.block_count);
    BUSTER_TEST(arguments,
                memcmp(replayed.instructions, function.instructions, function.instruction_count * sizeof(MachineInstruction)) == 0);
    BUSTER_TEST(arguments,
                memcmp(replayed.virtual_registers, function.virtual_registers, function.virtual_register_count * sizeof(MachineVirtualRegister)) == 0);
    BUSTER_TEST(arguments, memcmp(replayed.blocks, function.blocks, function.block_count * sizeof(MachineBlock)) == 0);
    BUSTER_TEST(arguments, machine_verify_function(&replayed).error == MACHINE_VERIFY_NONE);

    // CFG edge contracts and their parallel-copy source/parameter slices are
    // owned by the function and must survive the same replay round trip.
    MachineFunctionBuilder edge_builder = machine_function_builder_begin(arguments->arena);
    u32 edge_source_register = machine_builder_virtual_register(&edge_builder, (MachineVirtualRegister){
                                                                    .definition_point = MACHINE_POINT_INVALID,
                                                                    .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                    .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                });
    u32 edge_parameter_register = machine_builder_virtual_register(&edge_builder, (MachineVirtualRegister){
                                                                        .definition_point = MACHINE_POINT_INVALID,
                                                                        .register_class = MACHINE_REGISTER_CLASS_GENERAL,
                                                                        .typed_origin = IR_ID_UNDERLYING_INVALID,
                                                                    });
    machine_builder_block_parameter(&edge_builder, (MachineBlockParameter){.virtual_register = edge_parameter_register});
    machine_builder_block_begin(&edge_builder);
    machine_builder_instruction(&edge_builder, (MachineInstruction){.opcode = MACHINE_OPCODE_SKELETON_RETURN});
    machine_builder_block_end(&edge_builder, (MachineBlock){.successor_offset = 0, .successor_count = 1});
    machine_builder_block_begin(&edge_builder);
    machine_builder_instruction(&edge_builder, (MachineInstruction){.opcode = MACHINE_OPCODE_SKELETON_RETURN});
    machine_builder_block_end(&edge_builder, (MachineBlock){.predecessor_offset = 0, .predecessor_count = 1, .parameter_offset = 0, .parameter_count = 1});
    MachineRef edge_source_ref = machine_ref_make(MACHINE_REF_VIRTUAL_REGISTER, edge_source_register);
    u32 edge_copy_offset = machine_builder_edge_copy_source(&edge_builder, edge_source_ref);
    machine_builder_edge(&edge_builder, (MachineEdge){.source_block = 0, .destination_block = 1, .copy_offset = edge_copy_offset, .copy_count = 1});
    MachineFunction edge_function = machine_function_builder_finish(arguments->arena, &edge_builder);
    BUSTER_TEST(arguments, edge_function.edge_count == 1 && edge_function.block_parameter_count == 1 && edge_function.edge_copy_source_count == 1);
    BUSTER_TEST(arguments, machine_verify_function(&edge_function).error == MACHINE_VERIFY_NONE);
    ByteSlice edge_replay = machine_replay_serialize(arguments->arena, &edge_function);
    MachineFunction edge_replayed = {0};
    BUSTER_TEST(arguments, machine_replay_deserialize(arguments->arena, edge_replay, &edge_replayed));
    BUSTER_TEST(arguments, edge_replayed.edge_count == edge_function.edge_count && edge_replayed.block_parameter_count == edge_function.block_parameter_count &&
                              edge_replayed.edge_copy_source_count == edge_function.edge_copy_source_count);
    BUSTER_TEST(arguments, memcmp(edge_replayed.edges, edge_function.edges, sizeof(MachineEdge)) == 0 &&
                              memcmp(edge_replayed.block_parameters, edge_function.block_parameters, sizeof(MachineBlockParameter)) == 0 &&
                              memcmp(edge_replayed.edge_copy_sources, edge_function.edge_copy_sources, sizeof(MachineRef)) == 0);
    BUSTER_TEST(arguments, machine_verify_function(&edge_replayed).error == MACHINE_VERIFY_NONE);

    MachineFunction rejected = {0};
    ByteSlice truncated = replay;
    truncated.length -= 1;
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, truncated, &rejected));
    u8* corrupt_bytes = arena_allocate(arguments->arena, u8, replay.length);
    memcpy(corrupt_bytes, replay.pointer, replay.length);
    corrupt_bytes[0] ^= 0xff;
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, (ByteSlice){.pointer = corrupt_bytes, .length = replay.length}, &rejected));
    BUSTER_TEST(arguments, !machine_replay_deserialize(arguments->arena, (ByteSlice){0}, &rejected));

    // Mode plumbing: the driver-facing enum and its report spelling.
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_NONE), S8("none"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_MIR_STACK), S8("mir-stack"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_FAST), S8("fast"));
    BUSTER_STRING_TEST(arguments, codegen_register_allocator_mode_string(CODEGEN_REGISTER_ALLOCATOR_QUALITY), S8("quality"));

    // Stage-9 scheduling. Thirty-two independent products all combined at
    // the end hold every intermediate live to the combine in source order —
    // more than the x86-64 register file — and sinking each product to its
    // first use removes the excess. The pass must keep the block partition
    // and the SSA def-before-use order, its output must satisfy the
    // structural verifier, and on this shape the scheduled FAST placement
    // must model strictly cheaper — that modeled improvement is the
    // acceptance discipline's whole basis for keeping a schedule. The
    // low-pressure function must come back unmoved: its excess is zero and
    // the pass gates itself out before building anything. Both selectors
    // promote the thirty-two locals into virtual registers, so the same
    // pressure shape schedules on AArch64 too — its file is 25 allocatable
    // registers against the 32 live products — and the acceptance
    // discipline must hold on both targets.
    {
        String8 schedule_source = S8("unsigned long tree32(unsigned long s) {\n"
                                     "    unsigned long c0 = (s + 1) * 3; unsigned long c1 = (s + 2) * 5;\n"
                                     "    unsigned long c2 = (s + 3) * 7; unsigned long c3 = (s + 4) * 11;\n"
                                     "    unsigned long c4 = (s + 5) * 13; unsigned long c5 = (s + 6) * 17;\n"
                                     "    unsigned long c6 = (s + 7) * 19; unsigned long c7 = (s + 8) * 23;\n"
                                     "    unsigned long c8 = (s + 9) * 29; unsigned long c9 = (s + 10) * 31;\n"
                                     "    unsigned long c10 = (s + 11) * 37; unsigned long c11 = (s + 12) * 41;\n"
                                     "    unsigned long c12 = (s + 13) * 43; unsigned long c13 = (s + 14) * 47;\n"
                                     "    unsigned long c14 = (s + 15) * 53; unsigned long c15 = (s + 16) * 59;\n"
                                     "    unsigned long c16 = (s + 17) * 61; unsigned long c17 = (s + 18) * 67;\n"
                                     "    unsigned long c18 = (s + 19) * 71; unsigned long c19 = (s + 20) * 73;\n"
                                     "    unsigned long c20 = (s + 21) * 79; unsigned long c21 = (s + 22) * 83;\n"
                                     "    unsigned long c22 = (s + 23) * 89; unsigned long c23 = (s + 24) * 97;\n"
                                     "    unsigned long c24 = (s + 25) * 101; unsigned long c25 = (s + 26) * 103;\n"
                                     "    unsigned long c26 = (s + 27) * 107; unsigned long c27 = (s + 28) * 109;\n"
                                     "    unsigned long c28 = (s + 29) * 113; unsigned long c29 = (s + 30) * 127;\n"
                                     "    unsigned long c30 = (s + 31) * 131; unsigned long c31 = (s + 32) * 137;\n"
                                     "    return (((c0 ^ c1) + (c2 ^ c3)) * ((c4 ^ c5) + (c6 ^ c7))) ^\n"
                                     "           (((c8 ^ c9) + (c10 ^ c11)) * ((c12 ^ c13) + (c14 ^ c15))) ^\n"
                                     "           (((c16 ^ c17) + (c18 ^ c19)) * ((c20 ^ c21) + (c22 ^ c23))) ^\n"
                                     "           (((c24 ^ c25) + (c26 ^ c27)) * ((c28 ^ c29) + (c30 ^ c31)));\n"
                                     "}\n"
                                     "unsigned long lean(unsigned long a, unsigned long b) { return a * 3 + b; }\n");
        Target schedule_targets[] = {
            {.cpu_arch = CPU_ARCH_X86_64, .os = OPERATING_SYSTEM_LINUX},
            {.cpu_arch = CPU_ARCH_AARCH64, .os = OPERATING_SYSTEM_LINUX},
        };
        for (u32 target_index = 0; target_index < BUSTER_ARRAY_LENGTH(schedule_targets); target_index += 1)
        {
            IrProgram* schedule_program =
                machine_test_compile_c(arguments->arena, S8("machine-schedule.c"), schedule_source, schedule_targets[target_index]);
            BUSTER_TEST(arguments, schedule_program && schedule_program->module_count);
            if (!schedule_program || !schedule_program->module_count)
            {
                continue;
            }
            IrFunction* tree_function = machine_test_ir_function_find(schedule_program->modules, S8("tree32"));
            IrFunction* lean_function = machine_test_ir_function_find(schedule_program->modules, S8("lean"));
            BUSTER_TEST(arguments, tree_function && lean_function);
            if (!tree_function || !lean_function)
            {
                continue;
            }
            MachineSelectResult tree_selected =
                machine_select_canonical_function(arguments->arena, schedule_program, tree_function, schedule_targets[target_index]);
            MachineSelectResult lean_selected =
                machine_select_canonical_function(arguments->arena, schedule_program, lean_function, schedule_targets[target_index]);
            BUSTER_TEST(arguments, tree_selected.supported && lean_selected.supported);
            BUSTER_TEST(arguments, tree_selected.selector_certified && lean_selected.selector_certified);
            if (!tree_selected.supported || !lean_selected.supported)
            {
                continue;
            }
            MachineScheduleResult lean_scheduled = machine_schedule_function(arguments->arena, &lean_selected.function);
            BUSTER_TEST(arguments, !lean_scheduled.moved);
            MachineScheduleResult tree_scheduled = machine_schedule_function(arguments->arena, &tree_selected.function);
            BUSTER_TEST(arguments, tree_scheduled.moved);
            if (!tree_scheduled.moved)
            {
                continue;
            }
            BUSTER_TEST(arguments, machine_verify_function(&tree_scheduled.function).error == MACHINE_VERIFY_NONE);
            BUSTER_TEST(arguments, tree_scheduled.function.instruction_count == tree_selected.function.instruction_count);
            BUSTER_TEST(arguments, tree_scheduled.function.blocks == tree_selected.function.blocks);
            // Single-definition values must still define above every use.
            u32 schedule_register_count = tree_scheduled.function.virtual_register_count;
            u32* schedule_definition_rows = arena_allocate(arguments->arena, u32, schedule_register_count);
            u32* schedule_definition_counts = arena_allocate(arguments->arena, u32, schedule_register_count);
            for (u32 register_index = 0; register_index < schedule_register_count; register_index += 1)
            {
                schedule_definition_rows[register_index] = 0;
                schedule_definition_counts[register_index] = 0;
            }
            for (u32 row = 0; row < tree_scheduled.function.instruction_count; row += 1)
            {
                MachineInstruction* instruction = tree_scheduled.function.instructions + row;
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                for (u32 slot = 0; info && slot < info->operand_count; slot += 1)
                {
                    if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
                    {
                        continue;
                    }
                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                    if (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE)
                    {
                        schedule_definition_rows[machine_ref_payload(instruction->operands[slot])] = row;
                        schedule_definition_counts[machine_ref_payload(instruction->operands[slot])] += 1;
                    }
                }
            }
            bool uses_follow_definitions = true;
            for (u32 row = 0; row < tree_scheduled.function.instruction_count; row += 1)
            {
                MachineInstruction* instruction = tree_scheduled.function.instructions + row;
                MachineOpcodeInfo const* info = machine_opcode_info(instruction->opcode);
                for (u32 slot = 0; info && slot < info->operand_count; slot += 1)
                {
                    if (machine_ref_kind(instruction->operands[slot]) != MACHINE_REF_VIRTUAL_REGISTER)
                    {
                        continue;
                    }
                    u32 virtual_register = machine_ref_payload(instruction->operands[slot]);
                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                    if (role == MACHINE_OPERAND_ROLE_USE && schedule_definition_counts[virtual_register] == 1)
                    {
                        uses_follow_definitions &= schedule_definition_rows[virtual_register] <= row;
                    }
                }
            }
            BUSTER_TEST(arguments, uses_follow_definitions);
            MachineStackPlacement tree_base_placement = machine_fast_placement_build(arguments->arena, &tree_selected.function);
            MachineStackPlacement tree_scheduled_placement = machine_fast_placement_build(arguments->arena, &tree_scheduled.function);
            BUSTER_TEST(arguments, tree_base_placement.valid && tree_scheduled_placement.valid);
            BUSTER_TEST(arguments, tree_scheduled_placement.reload_count + tree_scheduled_placement.spill_count <
                                       tree_base_placement.reload_count + tree_base_placement.spill_count);
        }
    }

    // Stage 2: x86-64 selection, MIR_STACK placement, and encoding over the
    // scalar subset. Selection and encoding are host-independent; execution
    // requires a non-sanitized x86-64 host and runs the same functions
    // through the canonical NONE path as the differential oracle.
    Target machine_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    machine_target.cpu_features_explicit = true;
    machine_target.cpu_features = target_cpu_features_add(target_cpu_features_effective(machine_target), TARGET_CPU_FEATURE_X86_CX16);
    String8 machine_c_source_head = S8("int add(int a, int b) { return a + b; }\n"
                                  "int mul(int a, int b) { return a * b; }\n"
                                  "long widen(int a, unsigned b) { return (long)a + (long)b; }\n"
                                  "int narrow(long v) { return (int)v; }\n"
                                  "int negate(int a) { return -a; }\n"
                                  "long bitnot(long a) { return ~a; }\n"
                                  "int lnot(int a) { return !a; }\n"
                                  "int less(int a, int b) { return a < b; }\n"
                                  "int uless(unsigned a, unsigned b) { return a < b; }\n"
                                  "int six(int a, int b, int c, int d, int e, int f) { return a + b + c + d + e + f; }\n"
                                  "long arr_lit(long a, long b) { long t[4] = {a, b, a + b, 5}; return t[0] * 1000 + t[1] * 100 + t[2] * 10 + t[3]; }\n"
                                  "struct KPair { long low; long high; };\n"
                                  "struct BF3 { unsigned r : 1; unsigned w : 1; unsigned x : 3; unsigned rest : 27; };\n"
                                  "long bits(long a, long b) { struct BF3 f = {.r = (unsigned)a & 1u, .w = (unsigned)b & 1u, .x = (unsigned)(a + b) & 7u, .rest = 0u}; return (long)(f.r * 100u + f.w * 10u + f.x); }\n"
                                                                    "static long kagg_take(struct KPair pair, long salt) { return pair.low * 3 + pair.high + salt; }\n"
                                  "long kagg(long a, long b) { struct KPair pair = {.low = a + 1, .high = b}; return kagg_take(pair, a) + pair.high; }\n"
                                  "int sum_to(int n) { int s = 0; int i = 1; while (i <= n) { s = s + i; i = i + 1; } return s; }\n"
                                  "long readp(long* p) { return *p; }\n"
                                  "void writep(int* p, int v) { *p = v; }\n"
                                  "int divide(int a, int b) { return a / b; }\n"
                                  "int srem(int a, int b) { return a % b; }\n"
                                  "unsigned long udiv(unsigned long a, unsigned long b) { return a / b; }\n"
                                  "int shl(int a, int b) { return a << b; }\n"
                                  "long sar(long a, int b) { return a >> b; }\n"
                                  "unsigned shr(unsigned a, int b) { return a >> b; }\n"
                                  "int with_call(int a, int b) { return divide(a, 2) + srem(b, 3); }\n"
                                  "int fadd(float a, float b) { return a + b > 1.0f; }\n"
                                  "int seven(int a, int b, int c, int d, int e, int f, int g) { return a + g; }\n"
                                  "long stack_mix(int a, int b, int c, int d, int e, int f, int g, long h) { return a + g * h; }\n"
                                  "long call_stack(long h) { return stack_mix(1, 2, 3, 4, 5, 6, 7, h); }\n"
                                  "double nine(double a, double b, double c, double d, double e, double f, double g, double h, double i) {\n"
                                  "    return a + i * b; }\n"
                                  "int indirect(int (*callee)(int, int), int a) { return callee(a, 2); }\n"
                                  "int call_indirect(int a) { int (*f)(int, int) = divide; return f(a, 3); }\n"
                                  "int aligned_local(int x) { _Alignas(16) long buffer[4]; buffer[0] = x; buffer[3] = x * 2; return (int)(buffer[0] + buffer[3]); }\n"
                                  "_Atomic int atomic_cell;\n"
                                  "int atomic_probe(int v) { return __c11_atomic_fetch_add(&atomic_cell, v, 5); }\n"
                                  "int atomic_ops(int v) { _Atomic int cell; _Atomic long wide; __c11_atomic_store(&cell, v, 5);\n"
                                  "    __c11_atomic_store(&wide, (long)v * 7, 5);\n"
                                  "    int old = __c11_atomic_fetch_add(&cell, 3, 5); old += __c11_atomic_fetch_and(&cell, 6, 5);\n"
                                  "    old += __c11_atomic_exchange(&cell, v * 2, 5); int expected = v * 2;\n"
                                  "    __c11_atomic_compare_exchange_strong(&cell, &expected, 9, 5, 5); __c11_atomic_thread_fence(5);\n"
                                  "    return old + __c11_atomic_load(&cell, 5) + expected + (int)__c11_atomic_fetch_sub(&wide, 2, 5); }\n"
                                  "int goto_probe(int v) { void* t = v ? &&a : &&b; goto *t; a: return 1; b: return 2; }\n"
                                  "typedef struct Big { long a; long b; long c; } Big;\n"
                                  "Big big_make(long a) { Big b; b.a = a; b.b = a * 2; b.c = a ^ 5; return b; }\n"
                                  "long big_sum(Big b) { return b.a + b.b + b.c; }\n"
                                  "long big_round(long a) { Big b = big_make(a); return big_sum(b) + b.c; }\n"
                                  "int counter;\n"
                                  "int bump(int by) { counter = counter + by; return counter; }\n"
                                  "int table[8];\n"
                                  "int table_get(int i) { return table[i]; }\n"
                                  "void table_set(int i, int v) { table[i] = v; }\n"
                                  "typedef struct Pair { int first; long second; } Pair;\n"
                                  "Pair pair;\n"
                                  "long pair_sum(void) { return pair.first + pair.second; }\n"
                                  "int locals_array(int n) { int a[4]; a[0] = n; a[1] = n + 1; a[2] = a[0] * a[1]; a[3] = a[2] - n; return a[3]; }\n"
                                  "int local_pair(int x) { Pair p; p.first = x; p.second = x * 2; return p.first + (int)p.second; }\n"
                                  "int pick(int k) { switch (k) { case 1: return 10; case 3: return 30; case 7: return 70; default: return -k; } }\n"
                                  // The union's zero literal covers one byte of forty-eight, so
                                  // a selection that skips the aggregate zero-fill returns
                                  // whatever the probe stack held (the state_push regression).
                                  "typedef union UTail { char head; long words[6]; } UTail;\n"
                                  "long union_tail(long a, long b) { UTail u; u = (UTail){0};\n"
                                  "    return u.words[1] + u.words[2] + u.words[3] + u.words[4] + u.words[5] + (a & 0) + (b & 0); }\n");
    String8 machine_c_source_tail = S8(
                                  "int nest2(int n) { int total = 0; for (int i = 0; i < n; i += 1) { for (int j = 0; j < n; j += 1) { total = total + i * j; } } return total; }\n"
                                  "long ucvt(long a, long b) { unsigned long u = ((unsigned long)a << 32) | 5u; double d = (double)u; unsigned long r = (unsigned long)d; float f = (float)(((unsigned long)b << 31) | 1u); return (long)(r >> 33) + (long)(f * 0.25f) + (long)(unsigned long)(double)((unsigned long)b | 3u); }\n"
                                  "int printf(const char* format, ...);\n"
                                  "int call_variadic(int a, long b) { return printf(\"%d %ld\", a, b); }\n"
                                  "typedef struct Span { char* data; unsigned long length; } Span;\n"
                                  "unsigned long span_length(Span s) { return s.length; }\n"
                                  "Span span_make(char* data, unsigned long length) { Span s; s.data = data; s.length = length; return s; }\n"
                                  "long span_round_trip(char* data, unsigned long length) {\n"
                                  "    Span s = span_make(data, length);\n"
                                  "    return (long)span_length(s) + (s.data == data ? 1 : 0);\n"
                                  "}\n"
                                  "typedef struct Single { long only; } Single;\n"
                                  "long single_round_trip(int a) { Single o; o.only = a * 3; Single copy = o; return copy.only; }\n"
                                  "int fmath(int a, int b) { double x = a; double y = b; double z = (x + y) * 0.5 - x / (y + 3.0); return (int)z; }\n"
                                  "int f32math(int a) { float x = (float)a; float y = x * 2.0f + 1.5f; return (int)(y - x); }\n"
                                  "int fcompare(int a, int b) { double x = a; double y = b;\n"
                                  "    return (x < y) + (x <= y) * 2 + (x == y) * 4 + (x != y) * 8 + (x > y) * 16 + (x >= y) * 32; }\n"
                                  "int fnegate(int a) { double x = a; return (int)-x; }\n"
                                  "int fnan(int a) { double n = a * 0.0; n = n / n; return (n == n) ? 1 : 0; }\n"
                                  "unsigned fuconv(unsigned a) { double x = a; return (unsigned)(x + 1.5); }\n"
                                  "double dadd(double a, double b) { return a + b; }\n"
                                  "double dmix(int a, double b, long c, double d) { return (double)a + b * (double)c - d; }\n"
                                  "float fhalf(float a, float b) { return (a - b) * 0.5f; }\n"
                                  "typedef struct DPair { double x; double y; } DPair;\n"
                                  "double dpair_sum(DPair p) { return p.x + p.y; }\n"
                                  "DPair dpair_make(double x, double y) { DPair p; p.x = x; p.y = y; return p; }\n"
                                  "typedef struct Tagged { long tag; double v; } Tagged;\n"
                                  "double tagged_get(Tagged t) { return t.tag ? t.v : -t.v; }\n"
                                  "double dcall(double a, double b) { DPair p = dpair_make(a, b); return dpair_sum(p) * dadd(a, b); }\n"
                                  "_Thread_local long tls_cell;\n"
                                  "long tls_bump(long by) { tls_cell = tls_cell + by; return tls_cell; }\n"
                                  "static long rv_take(const long* values, int count) { long s = 0; int i = 0; while (i < count) { s = s + values[i]; i = i + 1; } return s; }\n"
                                  "long rv_lit(long a, long b) { return rv_take((const long[]){a, b, a * b, 7}, 4); }\n"
                                  "static unsigned __int128 u128_idem(unsigned __int128 v) { return v; }\n"
                                  "unsigned __int128 u128_ferry(unsigned __int128 v) { return u128_idem(v); }\n"
                                  "int call_seventeen(int a) { return printf(\"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\",\n"
                                  "    a, a + 1, a + 2, a + 3, a + 4, a + 5, a + 6, a + 7, a + 8, a + 9, a + 10, a + 11, a + 12, a + 13, a + 14, a + 15); }\n");
    // A third fixture segment: `machine_c_source_tail` is already at the
    // C99 4095-byte string-literal limit, so new fixture text lands here
    // instead of growing it further.
    String8 machine_c_source_extra = S8(
                                  // The live-range-splitting shape: twenty-four values live from before
                                  // a call-free hot loop across a later call loop — past both
                                  // targets' register files. The calls foreclose the
                                  // caller-saved file over every whole interval and the
                                  // callee-saved file cannot hold them all, so the only
                                  // register residency left for the rest is a split to the
                                  // first loop.
                                  "static unsigned long sp_mix(unsigned long v) { v ^= v >> 31; v *= 0x9e3779b97f4a7c15UL; v ^= v >> 27; return v; }\n"
                                  "unsigned long split_phase(unsigned long seed, unsigned long rounds) {\n"
                                  "    unsigned long k0 = seed + 1; unsigned long k1 = seed + 2; unsigned long k2 = seed + 3; unsigned long k3 = seed + 4;\n"
                                  "    unsigned long k4 = seed + 5; unsigned long k5 = seed + 6; unsigned long k6 = seed + 7; unsigned long k7 = seed + 8;\n"
                                  "    unsigned long k8 = seed + 9; unsigned long k9 = seed + 10; unsigned long k10 = seed + 11; unsigned long k11 = seed + 12;\n"
                                  "    unsigned long k12 = seed + 13; unsigned long k13 = seed + 14; unsigned long k14 = seed + 15; unsigned long k15 = seed + 16;\n"
                                  "    unsigned long k16 = seed + 17; unsigned long k17 = seed + 18; unsigned long k18 = seed + 19; unsigned long k19 = seed + 20;\n"
                                  "    unsigned long k20 = seed + 21; unsigned long k21 = seed + 22; unsigned long k22 = seed + 23; unsigned long k23 = seed + 24;\n"
                                  "    unsigned long acc = seed;\n"
                                  "    for (unsigned long round = 0; round < rounds; round += 1) {\n"
                                  "        acc += k0 ^ (acc << 1); acc += k1 ^ (acc << 2); acc += k2 ^ (acc << 3); acc += k3 ^ (acc << 4);\n"
                                  "        acc += k4 ^ (acc << 5); acc += k5 ^ (acc << 6); acc += k6 ^ (acc << 7); acc += k7 ^ (acc << 1);\n"
                                  "        acc += k8 ^ (acc << 2); acc += k9 ^ (acc << 3); acc += k10 ^ (acc << 4); acc += k11 ^ (acc << 5);\n"
                                  "        acc += k12 ^ (acc << 6); acc += k13 ^ (acc << 7); acc += k14 ^ (acc << 1); acc += k15 ^ (acc << 2);\n"
                                  "        acc += k16 ^ (acc << 3); acc += k17 ^ (acc << 4); acc += k18 ^ (acc << 5); acc += k19 ^ (acc << 6);\n"
                                  "        acc += k20 ^ (acc << 7); acc += k21 ^ (acc << 1); acc += k22 ^ (acc << 2); acc += k23 ^ (acc << 3);\n"
                                  "    }\n"
                                  "    for (unsigned long round = 0; round < rounds; round += 1) { acc += sp_mix(acc ^ k0 ^ k23); }\n"
                                  "    return acc ^ k1 ^ k2 ^ k3 ^ k4 ^ k5 ^ k6 ^ k7 ^ k8 ^ k9 ^ k10 ^ k11 ^ k12 ^ k13 ^ k14 ^ k15 ^ k16 ^ k17 ^ k18 ^ k19 ^\n"
                                  "           k20 ^ k21 ^ k22;\n"
                                  "}\n"
                                  "static int vla_consume(const int* values, int count, int a, int b, int c, int d, int e) { return values[0] + values[count - 1] + a + e; }\n"
                                  "int vla_sum(int seed, int count) { int n = (count & 2047) + 1; int total = 0; for (int i = 0; i < 2; i += 1) { int values[n]; values[0] = seed + i; values[n - 1] = seed + count + i; total += vla_consume(values, n, seed, i, 1, 2, 3); } return total; }\n"
                                  "typedef __int128 MachineWideSigned;\n"
                                  "typedef unsigned __int128 MachineWideUnsigned;\n"
                                  "unsigned long u128_to_u64(MachineWideUnsigned v) { return (unsigned long)v; }\n"
                                  "long i128_to_i64(MachineWideSigned v) { return (long)v; }\n"
                                  "MachineWideUnsigned u64_to_u128(unsigned long v) { return (MachineWideUnsigned)v; }\n"
                                  "MachineWideSigned i64_to_i128(long v) { return (MachineWideSigned)v; }\n"
                                  "int variadic_named(int first, ...) { return first; }\n"
                                  "int variadic_named_caller(int first) { return variadic_named(first, 22, 3.5); }\n"
                                  "long stack_tail(long a, long b, long c, long d, long e, long f, long g, long h, long i, long j) { return a * 2 + b + c + d + e + f + g + h + i * 3 + j * 5; }\n"
                                  "long call_stack_tail(long x) { return stack_tail(x, 2, 3, 4, 5, 6, 7, 8, x + 9, 10); }\n"
                                  // A parameter written with array syntax carries a bound whose
                                  // arithmetic is emitted between the parameter homes, so the entry
                                  // block is not a contiguous run of them. Both shapes take a fourth
                                  // integer argument, which System V passes in RCX -- the register
                                  // that bound multiply uses. `vla_param` is the same shape with a
                                  // genuine runtime bound, where the arithmetic cannot be folded away.
                                  "long arr_param(unsigned char slots[2], long a, long b, long c) {\n"
                                  "    return slots[0] * 1000 + a * 100 + b * 10 + c; }\n"
                                  "long vla_param(long n, unsigned char slots[n], long b, long c) {\n"
                                  "    return slots[n - 1] * 1000 + n * 100 + b * 10 + c; }\n");
    String8 machine_c_source_i128 = S8(
                                  "MachineWideSigned i8_to_i128(signed char v) { return (MachineWideSigned)v; }\n"
                                  "MachineWideUnsigned u8_to_u128(unsigned char v) { return (MachineWideUnsigned)v; }\n"
                                  "MachineWideSigned i16_to_i128(short v) { return (MachineWideSigned)v; }\n"
                                  "MachineWideUnsigned u16_to_u128(unsigned short v) { return (MachineWideUnsigned)v; }\n"
                                  "MachineWideSigned i32_to_i128(int v) { return (MachineWideSigned)v; }\n"
                                  "MachineWideUnsigned u32_to_u128(unsigned int v) { return (MachineWideUnsigned)v; }\n"
                                  "MachineWideUnsigned i128_reinterpret(MachineWideSigned v) { return (MachineWideUnsigned)v; }\n"
                                  "MachineWideUnsigned u128_shr0(MachineWideUnsigned v) { return v >> 0; }\n"
                                  "MachineWideUnsigned u128_shr1(MachineWideUnsigned v) { return v >> 1; }\n"
                                  "MachineWideUnsigned u128_shr63(MachineWideUnsigned v) { return v >> 63; }\n"
                                  "MachineWideUnsigned u128_shr64(MachineWideUnsigned v) { return v >> 64; }\n"
                                  "MachineWideUnsigned u128_shr65(MachineWideUnsigned v) { return v >> 65; }\n"
                                  "MachineWideUnsigned u128_shr127(MachineWideUnsigned v) { return v >> 127; }\n");
    String8 machine_c_source_variadic = S8(
                                  "#if defined(__x86_64__)\n"
                                  "typedef void *va_list;\n"
                                  "long variadic_observe(int first, ...) { va_list arguments; long total = first; __builtin_va_start(arguments, first);\n"
                                  "    total += __builtin_va_arg(arguments, int); total += __builtin_va_arg(arguments, int);\n"
                                  "    total += __builtin_va_arg(arguments, int); total += __builtin_va_arg(arguments, int);\n"
                                  "    total += __builtin_va_arg(arguments, int); total += __builtin_va_arg(arguments, int);\n"
                                  "    total += __builtin_va_arg(arguments, int);\n"
                                  "    total += (long)__builtin_va_arg(arguments, double); total += (long)__builtin_va_arg(arguments, double);\n"
                                  "    total += (long)__builtin_va_arg(arguments, double); total += (long)__builtin_va_arg(arguments, double);\n"
                                  "    total += (long)__builtin_va_arg(arguments, double); total += (long)__builtin_va_arg(arguments, double);\n"
                                  "    total += (long)__builtin_va_arg(arguments, double); total += (long)__builtin_va_arg(arguments, double);\n"
                                  "    total += (long)__builtin_va_arg(arguments, double); __builtin_va_end(arguments); return total; }\n"
                                  "long variadic_observe_caller(int first) { return variadic_observe(first, 1, 2, 3, 4, 5, 6, 7,\n"
                                  "    1.25, 2.25, 3.25, 4.25, 5.25, 6.25, 7.25, 8.25, 9.25); }\n"
                                  "int atomic_cmpxchg16(_Atomic unsigned __int128* cell, unsigned __int128 expected, unsigned __int128 desired) {\n"
                                  "    int result = __c11_atomic_compare_exchange_strong(cell, &expected, desired, 5, 5);\n"
                                  "    return result;\n"
                                  "}\n"
                                  "int atomic_cmpxchg16_after_call(_Atomic unsigned __int128* cell, unsigned __int128 expected, unsigned __int128 desired) {\n"
                                  "    int result = __c11_atomic_compare_exchange_strong(cell, &expected, desired, 5, 5);\n"
                                  "    return result + variadic_named(0);\n"
                                  "}\n"
                                  "int variadic_unsupported_first(int first, ...) { __asm__ volatile(\"\");\n"
                                  "    va_list arguments; __builtin_va_start(arguments, first); return __builtin_va_arg(arguments, int); }\n"
                                  "#endif\n");
    // The AArch64 variadic segment, appended only to the stage-11 source:
    // the typedef's name alone selects the va_list type, exactly like the
    // x86-64 segment's. Eleven anonymous parts against eight registers
    // exercise both the register path and the overflow tail of the
    // canonical four-word va_list model the machine subset mirrors.
    String8 machine_c_source_a64_variadic = S8(
                                  "typedef void *va_list;\n"
                                  "long vsum(int count, ...) { va_list arguments; long total = 0; __builtin_va_start(arguments, count);\n"
                                  "    for (int index = 0; index < count; index += 1) { total += __builtin_va_arg(arguments, long); }\n"
                                  "    __builtin_va_end(arguments); return total; }\n"
                                  "long vsum_caller(long a, long b) { return vsum(11, a, b, a + b, (long)1, (long)2, (long)3, (long)4,\n"
                                  "    (long)5, (long)6, (long)7, a - b); }\n"
                                  "typedef float MachineV4 __attribute__((vector_size(16)));\n"
                                  "MachineV4 vec_pass(MachineV4 value) { return value; }\n"
                                  "typedef struct MachineBig3 { long a; long b; long c; } MachineBig3;\n"
                                  "long big_take(MachineBig3 value, long salt) { return value.a * 3 + value.c + salt; }\n"
                                  "typedef struct MachineFPair { double left; double right; } MachineFPair;\n"
                                  "double fpair_tail(double a, double b, double c, double d, double e, double f, double g, double h,\n"
                                  "    MachineFPair tail) { return a + b + c + d + e + f + g + h + tail.left * 2 + tail.right; }\n"
                                  "typedef struct MachinePair2 { long low; long high; } MachinePair2;\n"
                                  "long pair_spill(long a, long b, long c, long d, long e, long f, long g, MachinePair2 tail) {\n"
                                  "    return a + b + c + d + e + f + g + tail.low * 5 + tail.high; }\n"
                                  "long aligned_spot(long x) { _Alignas(64) long cells[8]; cells[0] = x; cells[7] = x * 3;\n"
                                  "    return cells[0] + cells[7] + (((long)(unsigned long)&cells[0]) & 63); }\n"
                                  "typedef int MachineVLit __attribute__((vector_size(16)));\n"
                                  "MachineVLit vlit_make(int a, int b) { return (MachineVLit){ a, b, a + b, a - b }; }\n"
                                  "MachineVLit vquad_add(MachineVLit left, MachineVLit right) { return left + right; }\n"
                                  "typedef float MachineVF4 __attribute__((vector_size(16)));\n"
                                  "MachineVF4 vf4_scale(MachineVF4 value, MachineVF4 scale) { return value * scale; }\n"
                                  "unsigned long amix(unsigned long start, unsigned long delta) { _Atomic unsigned long cell = start;\n"
                                  "    unsigned long added = __c11_atomic_fetch_add(&cell, delta, __ATOMIC_ACQ_REL);\n"
                                  "    unsigned long swapped = __c11_atomic_exchange(&cell, added ^ delta, __ATOMIC_SEQ_CST);\n"
                                  "    unsigned long expected = added ^ delta;\n"
                                  "    unsigned long won = (unsigned long)__c11_atomic_compare_exchange_strong(&cell, &expected, swapped + 3, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);\n"
                                  "    __c11_atomic_thread_fence(__ATOMIC_SEQ_CST);\n"
                                  "    __c11_atomic_store(&cell, swapped - added, __ATOMIC_RELEASE);\n"
                                  "    return added * 3 + swapped + won + __c11_atomic_load(&cell, __ATOMIC_ACQUIRE); }\n"
                                  "long vla_fill(long count) { long lanes = (count & 7) + 1; long cells[lanes];\n"
                                  "    for (long i = 0; i < lanes; i += 1) { cells[i] = i * 3 + count; }\n"
                                  "    long total = 0;\n"
                                  "    for (long i = 0; i < lanes; i += 1) { total += cells[i]; }\n"
                                  "    return total; }\n"
                                  "long pick4(long value) { switch (value & 3) {\n"
                                  "    case 0: return value * 3;\n"
                                  "    case 1: return value - 7;\n"
                                  "    case 2: return value ^ 0x55;\n"
                                  "    default: return value + 11; } }\n"
                                  "typedef unsigned char MachineV8 __attribute__((vector_size(8)));\n"
                                  "MachineV8 v8_pass(MachineV8 value) { return value; }\n"
                                  "typedef unsigned char MachineV2 __attribute__((vector_size(2)));\n"
                                  "MachineV2 v2_make(int a) { return (MachineV2){ (unsigned char)a, (unsigned char)(a + 3) }; }\n");
    String8 machine_c_source_base =
        string_format(arguments->arena, S8("{S8}{S8}{S8}"), machine_c_source_head, machine_c_source_tail, machine_c_source_extra);
    String8 machine_c_source_stage11 =
        string_format(arguments->arena, S8("{S8}{S8}"), machine_c_source_base, machine_c_source_a64_variadic);
    String8 machine_c_source = string_format(arguments->arena, S8("{S8}{S8}{S8}{S8}{S8}"), machine_c_source_head, machine_c_source_tail,
                                              machine_c_source_extra, machine_c_source_i128, machine_c_source_variadic);
    IrProgram* machine_program = machine_test_compile_c(arguments->arena, S8("machine-stage2.c"), machine_c_source, machine_target);
    BUSTER_TEST(arguments, machine_program != 0);
    if (machine_program && machine_program->module_count)
    {
        IrModule* machine_module = machine_program->modules;
        // The whole-module NONE oracle: identical IR through the canonical
        // path, executed at each function's entry offset.
        CodegenModule none_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                      (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, none_module.error == CODEGEN_ERROR_NONE);
        String8 supported_names[] = {
            S8_INITIALIZER("add"), S8_INITIALIZER("mul"), S8_INITIALIZER("widen"), S8_INITIALIZER("narrow"),
            S8_INITIALIZER("negate"), S8_INITIALIZER("bitnot"), S8_INITIALIZER("lnot"), S8_INITIALIZER("less"),
            S8_INITIALIZER("uless"), S8_INITIALIZER("six"), S8_INITIALIZER("kagg"), S8_INITIALIZER("arr_lit"), S8_INITIALIZER("bits"), S8_INITIALIZER("ucvt"), S8_INITIALIZER("sum_to"), S8_INITIALIZER("readp"),
            S8_INITIALIZER("writep"), S8_INITIALIZER("divide"), S8_INITIALIZER("srem"), S8_INITIALIZER("udiv"),
            S8_INITIALIZER("shl"), S8_INITIALIZER("sar"), S8_INITIALIZER("shr"), S8_INITIALIZER("bump"),
            S8_INITIALIZER("table_get"), S8_INITIALIZER("table_set"), S8_INITIALIZER("pair_sum"),
            S8_INITIALIZER("locals_array"), S8_INITIALIZER("local_pair"), S8_INITIALIZER("pick"),
            S8_INITIALIZER("aligned_local"), S8_INITIALIZER("span_length"), S8_INITIALIZER("span_make"), S8_INITIALIZER("span_round_trip"),
            S8_INITIALIZER("single_round_trip"), S8_INITIALIZER("fmath"), S8_INITIALIZER("f32math"),
            S8_INITIALIZER("fcompare"), S8_INITIALIZER("fnegate"), S8_INITIALIZER("fnan"), S8_INITIALIZER("fuconv"),
            S8_INITIALIZER("union_tail"),
        };
        MachineEncodeResult machine_encoded[BUSTER_ARRAY_LENGTH(supported_names)] = {0};
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(supported_names); name_index += 1)
        {
            IrFunction* ir_function = machine_test_ir_function_find(machine_module, supported_names[name_index]);
            BUSTER_TEST(arguments, ir_function != 0);
            if (!ir_function)
            {
                continue;
            }
            MachineSelectResult selected = {0};
            machine_encoded[name_index] = machine_test_encode(arguments->arena, machine_program, ir_function, machine_target, &selected);
            BUSTER_TEST_RAW(arguments, selected.supported,
                            string_format(arguments->arena, S8("select {S8} failed at opcode {u32}"), supported_names[name_index],
                                          (u32)selected.failed_opcode));
            BUSTER_TEST(arguments, selected.machine_instructions >= selected.selected_typed_instructions / 4);
            BUSTER_TEST_RAW(arguments, machine_encoded[name_index].valid, supported_names[name_index]);
            BUSTER_TEST_RAW(arguments, machine_encoded[name_index].byte_count > 8, supported_names[name_index]);
            // The prologue shape is fixed: push rbp; mov rbp, rsp.
            BUSTER_TEST(arguments, !machine_encoded[name_index].valid ||
                                       (machine_encoded[name_index].bytes[0] == 0x55 && machine_encoded[name_index].bytes[1] == 0x48 &&
                                        machine_encoded[name_index].bytes[2] == 0x89 && machine_encoded[name_index].bytes[3] == 0xe5));
        }
        // Placement statistics: MIR_STACK round-trips every operand, so a
        // selected function with arithmetic must report both reloads and
        // spills, bounded by four per instruction.
        IrFunction* add_function = machine_test_ir_function_find(machine_module, S8("add"));
        if (add_function)
        {
            MachineSelectResult add_selected = machine_select_canonical_function(arguments->arena, machine_program, add_function, machine_target);
            BUSTER_TEST(arguments, add_selected.supported);
            bool saw_ssa_alu = false;
            bool ssa_alu_well_formed = true;
            for (u32 row_index = 0; add_selected.supported && row_index < add_selected.function.instruction_count; row_index += 1)
            {
                MachineInstruction* row = add_selected.function.instructions + row_index;
                bool is_ssa_alu = row->opcode == MACHINE_X64_ADD32 || row->opcode == MACHINE_X64_ADD64 || row->opcode == MACHINE_X64_SUB32 ||
                                  row->opcode == MACHINE_X64_SUB64 || row->opcode == MACHINE_X64_AND32 || row->opcode == MACHINE_X64_AND64 ||
                                  row->opcode == MACHINE_X64_OR32 || row->opcode == MACHINE_X64_OR64 || row->opcode == MACHINE_X64_XOR32 ||
                                  row->opcode == MACHINE_X64_XOR64 || row->opcode == MACHINE_X64_IMUL32 || row->opcode == MACHINE_X64_IMUL64;
                if (!is_ssa_alu)
                {
                    continue;
                }
                saw_ssa_alu = true;
                MachineOpcodeInfo const* info = machine_opcode_info(row->opcode);
                ssa_alu_well_formed &= info && info->operand_count == 3 && machine_opcode_operand_is_tied(info, 0, 1);
                ssa_alu_well_formed &= machine_ref_kind(row->operands[0]) == MACHINE_REF_VIRTUAL_REGISTER &&
                                       machine_ref_kind(row->operands[1]) == MACHINE_REF_VIRTUAL_REGISTER &&
                                       machine_ref_kind(row->operands[2]) == MACHINE_REF_VIRTUAL_REGISTER;
            }
            BUSTER_TEST(arguments, saw_ssa_alu && ssa_alu_well_formed);
            MachineStackPlacement add_placement = machine_stack_placement_build(arguments->arena, &add_selected.function);
            BUSTER_TEST(arguments, add_placement.valid);
            BUSTER_TEST(arguments, add_placement.reload_count > 0 && add_placement.spill_count > 0);
            BUSTER_TEST(arguments, add_placement.reload_count + add_placement.spill_count <= add_selected.function.instruction_count * 4);
            BUSTER_TEST(arguments, add_placement.frame_size % 16 == 0);
        }
        // Direct calls select into fixed-register argument copies plus a
        // relocated call row; float signatures are the current explicit
        // unsupported representative.
        IrFunction* call_function = machine_test_ir_function_find(machine_module, S8("with_call"));
        BUSTER_TEST(arguments, call_function != 0);
        if (call_function)
        {
            MachineSelectResult call_selected = machine_select_canonical_function(arguments->arena, machine_program, call_function, machine_target);
            BUSTER_TEST(arguments, call_selected.supported);
            BUSTER_TEST(arguments, call_selected.function.call_target_count >= 2);
        }
        IrFunction* float_function = machine_test_ir_function_find(machine_module, S8("fadd"));
        BUSTER_TEST(arguments, float_function != 0);
        if (float_function)
        {
            MachineSelectResult float_selected = machine_select_canonical_function(arguments->arena, machine_program, float_function, machine_target);
            BUSTER_TEST(arguments, float_selected.supported);
        }
        // Stack arguments now select; dynamic stack allocation stays the
        // explicit unsupported representative.
        IrFunction* seven_function = machine_test_ir_function_find(machine_module, S8("seven"));
        BUSTER_TEST(arguments, seven_function != 0);
        if (seven_function)
        {
            MachineSelectResult seven_selected = machine_select_canonical_function(arguments->arena, machine_program, seven_function, machine_target);
            BUSTER_TEST(arguments, seven_selected.supported);
        }
        // A variable-length local must stay on the machine path: the
        // dynamic allocation row pins its runtime size in RCX, probes the
        // pages, and returns the aligned pointer in RAX. The call while the
        // VLA is live proves the stack checkpoint and outgoing argument rows
        // remain ordered by the scheduler barriers.
        IrFunction* vla_function = machine_test_ir_function_find(machine_module, S8("vla_sum"));
        BUSTER_TEST(arguments, vla_function != 0);
        if (vla_function)
        {
            MachineSelectResult vla_selected = machine_select_canonical_function(arguments->arena, machine_program, vla_function, machine_target);
            BUSTER_TEST_RAW(arguments, vla_selected.supported,
                            string_format(arguments->arena, S8("vla select failed at opcode {u32}"), (u32)vla_selected.failed_opcode));
            if (vla_selected.supported)
            {
                bool saw_allocate = false;
                bool saw_save = false;
                bool saw_restore = false;
                for (u32 row_index = 0; row_index < vla_selected.function.instruction_count; row_index += 1)
                {
                    MachineInstruction* row = vla_selected.function.instructions + row_index;
                    saw_allocate |= row->opcode == MACHINE_X64_STACK_ALLOCATE;
                    saw_save |= row->opcode == MACHINE_X64_MOV_RR && machine_ref_kind(row->operands[1]) == MACHINE_REF_PHYSICAL_REGISTER &&
                                machine_ref_payload(row->operands[1]) == MACHINE_X64_RSP;
                    saw_restore |= row->opcode == MACHINE_X64_MOV_RR && machine_ref_kind(row->operands[0]) == MACHINE_REF_PHYSICAL_REGISTER &&
                                   machine_ref_payload(row->operands[0]) == MACHINE_X64_RSP;
                }
                BUSTER_TEST(arguments, saw_allocate && saw_save && saw_restore);
                BUSTER_TEST(arguments, machine_verify_function(&vla_selected.function).error == MACHINE_VERIFY_NONE);
                MachineStackPlacement vla_placement = machine_stack_placement_build(arguments->arena, &vla_selected.function);
                BUSTER_TEST(arguments, vla_placement.valid);
                MachineEncodeResult vla_encoded = machine_encode_x86_64(arguments->arena, &vla_selected.function, &vla_placement);
                BUSTER_TEST(arguments, vla_encoded.valid && vla_encoded.byte_count > 32);
            }
        }
        IrFunction* indirect_function = machine_test_ir_function_find(machine_module, S8("indirect"));
        BUSTER_TEST(arguments, indirect_function != 0);
        if (indirect_function)
        {
            MachineSelectResult indirect_selected = machine_select_canonical_function(arguments->arena, machine_program, indirect_function, machine_target);
            BUSTER_TEST(arguments, indirect_selected.supported);
        }
        // Atomics now select; computed goto stays the explicit unsupported
        // representative.
        IrFunction* atomic_function = machine_test_ir_function_find(machine_module, S8("atomic_probe"));
        BUSTER_TEST(arguments, atomic_function != 0);
        if (atomic_function)
        {
            MachineSelectResult atomic_selected = machine_select_canonical_function(arguments->arena, machine_program, atomic_function, machine_target);
            BUSTER_TEST(arguments, atomic_selected.supported);
        }
        IrFunction* atomic_cmpxchg16_function = machine_test_ir_function_find(machine_module, S8("atomic_cmpxchg16"));
        BUSTER_TEST(arguments, atomic_cmpxchg16_function != 0);
        if (atomic_cmpxchg16_function)
        {
            MachineSelectResult atomic_cmpxchg16_selected =
                machine_select_canonical_function(arguments->arena, machine_program, atomic_cmpxchg16_function, machine_target);
            BUSTER_TEST_RAW(arguments, atomic_cmpxchg16_selected.supported,
                            string_format(arguments->arena, S8("select atomic_cmpxchg16 failed at opcode {u32}"),
                                          (u32)atomic_cmpxchg16_selected.failed_opcode));
            if (atomic_cmpxchg16_selected.supported)
            {
                bool saw_cmpxchg16_row = false;
                for (u32 row_index = 0; row_index < atomic_cmpxchg16_selected.function.instruction_count; row_index += 1)
                {
                    saw_cmpxchg16_row |= atomic_cmpxchg16_selected.function.instructions[row_index].opcode == MACHINE_X64_ATOMIC_CMPXCHG16;
                }
                BUSTER_TEST(arguments, saw_cmpxchg16_row);
            }
            // The boolean shortcut is valid only for the frontend's
            // observed == expected comparison.  Mutate that RHS to the
            // unrelated desired i128 value and require the generic selector
            // to reject the unsupported i128 equality instead of reusing the
            // atomic success flag.
            IrInstruction* atomic_ir = 0;
            IrInstruction* atomic_observed_compare = 0;
            for (u32 instruction_index = 0; instruction_index < atomic_cmpxchg16_function->instruction_count; instruction_index += 1)
            {
                IrInstruction* instruction = atomic_cmpxchg16_function->instructions + instruction_index;
                if (!atomic_ir && instruction->opcode == IR_OPCODE_ATOMIC_COMPARE_EXCHANGE && instruction->operand_count >= 3)
                {
                    atomic_ir = instruction;
                }
            }
            if (atomic_ir)
            {
                for (u32 instruction_index = 0; instruction_index < atomic_cmpxchg16_function->instruction_count; instruction_index += 1)
                {
                    IrInstruction* instruction = atomic_cmpxchg16_function->instructions + instruction_index;
                    if (instruction->opcode == IR_OPCODE_BINARY && instruction->operand_count >= 2 &&
                        instruction->binary_operation == IR_BINARY_INTEGER_EQUAL &&
                        instruction->operands[0].value == atomic_ir->result.value)
                    {
                        atomic_observed_compare = instruction;
                        break;
                    }
                }
            }
            BUSTER_TEST(arguments, atomic_ir && atomic_observed_compare && atomic_ir->operands[1].value != atomic_ir->operands[2].value);
            if (atomic_ir && atomic_observed_compare && atomic_ir->operands[1].value != atomic_ir->operands[2].value)
            {
                IrValueId expected_operand = atomic_observed_compare->operands[1];
                atomic_observed_compare->operands[1] = atomic_ir->operands[2];
                MachineSelectResult unrelated_i128_selected =
                    machine_select_canonical_function(arguments->arena, machine_program, atomic_cmpxchg16_function, machine_target);
                BUSTER_TEST(arguments, !unrelated_i128_selected.supported);
                atomic_observed_compare->operands[1] = expected_operand;
            }
            Target no_cx16_target = machine_target;
            no_cx16_target.cpu_features_explicit = true;
            no_cx16_target.cpu_features = target_cpu_features_remove(target_cpu_features_effective(machine_target), TARGET_CPU_FEATURE_X86_CX16);
            MachineSelectResult no_cx16_selected =
                machine_select_canonical_function(arguments->arena, machine_program, atomic_cmpxchg16_function, no_cx16_target);
            BUSTER_TEST(arguments, !no_cx16_selected.supported);
        }
        IrFunction* atomic_cmpxchg16_call_function = machine_test_ir_function_find(machine_module, S8("atomic_cmpxchg16_after_call"));
        BUSTER_TEST(arguments, atomic_cmpxchg16_call_function != 0);
        if (atomic_cmpxchg16_call_function)
        {
            MachineSelectResult atomic_cmpxchg16_call_selected =
                machine_select_canonical_function(arguments->arena, machine_program, atomic_cmpxchg16_call_function, machine_target);
            BUSTER_TEST_RAW(arguments, atomic_cmpxchg16_call_selected.supported,
                            string_format(arguments->arena, S8("select atomic_cmpxchg16_after_call failed at opcode {u32}"),
                                          (u32)atomic_cmpxchg16_call_selected.failed_opcode));
            if (atomic_cmpxchg16_call_selected.supported)
            {
                MachineStackPlacement call_placement = machine_stack_placement_build(arguments->arena, &atomic_cmpxchg16_call_selected.function);
                u32 call_push_count = 0;
                for (u32 physical_register = 0; physical_register < machine_target_x86_64()->register_count; physical_register += 1)
                {
                    call_push_count += (call_placement.callee_saved_mask >> physical_register) & 1u;
                }
                BUSTER_TEST(arguments, call_placement.valid && (call_placement.callee_saved_mask & rbx_mask) &&
                                               (call_placement.frame_size + 8 * call_push_count) % 16 == 0);
            }
        }
        // Execute the canonical and machine forms against a deliberately
        // sixteen-byte-aligned object.  The first call succeeds and returns
        // true; the second observes the changed old value and returns false,
        // exercising both CMPXCHG16B result paths and its RBX save/restore.
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
        if (atomic_cmpxchg16_function)
        {
            typedef int MachineTestAtomicCmpxchg16(_Atomic unsigned __int128*, unsigned __int128, unsigned __int128);
            u32 atomic_none_offset = machine_test_module_offset(&none_module, machine_module, S8("atomic_cmpxchg16"));
            MachineSelectResult atomic_machine_selected =
                machine_select_canonical_function(arguments->arena, machine_program, atomic_cmpxchg16_function, machine_target);
            MachineStackPlacement atomic_machine_placement = machine_stack_placement_build(arguments->arena, &atomic_machine_selected.function);
            MachineEncodeResult atomic_machine_encoded = atomic_machine_selected.supported
                                                             ? machine_encode_x86_64(arguments->arena, &atomic_machine_selected.function,
                                                                                     &atomic_machine_placement)
                                                             : (MachineEncodeResult){0};
            CodegenExecutable atomic_none_executable = codegen_make_executable((CodegenFunction){.code = none_module.code});
            CodegenExecutable atomic_machine_executable = codegen_make_executable((CodegenFunction){
                .code = {.pointer = atomic_machine_encoded.bytes, .length = atomic_machine_encoded.byte_count}});
            BUSTER_TEST(arguments, atomic_none_offset != UINT32_MAX && atomic_machine_encoded.valid &&
                                       atomic_none_executable.error == CODEGEN_ERROR_NONE && atomic_machine_executable.error == CODEGEN_ERROR_NONE);
            if (atomic_none_offset != UINT32_MAX && atomic_machine_executable.address && atomic_none_executable.address)
            {
                MachineTestAtomicCmpxchg16* atomic_none_call = 0;
                MachineTestAtomicCmpxchg16* atomic_machine_call = 0;
                void* atomic_none_address = (u8*)atomic_none_executable.address + atomic_none_offset;
                void* atomic_machine_address = atomic_machine_executable.address;
                memcpy(&atomic_none_call, &atomic_none_address, sizeof(atomic_none_call));
                memcpy(&atomic_machine_call, &atomic_machine_address, sizeof(atomic_machine_call));
                _Alignas(16) unsigned __int128 atomic_cell_none = ((unsigned __int128)UINT64_C(0x1111222233334444) << 64) | UINT64_C(0x5555666677778888);
                _Alignas(16) unsigned __int128 atomic_cell_machine = atomic_cell_none;
                unsigned __int128 atomic_expected = atomic_cell_none;
                unsigned __int128 atomic_desired = ((unsigned __int128)UINT64_C(0xaaaabbbbccccdddd) << 64) | UINT64_C(0xeeeeffff00001111);
                int atomic_none_first = atomic_none_call((_Atomic unsigned __int128*)&atomic_cell_none, atomic_expected, atomic_desired);
                int atomic_machine_first = atomic_machine_call((_Atomic unsigned __int128*)&atomic_cell_machine, atomic_expected, atomic_desired);
                int atomic_none_second = atomic_none_call((_Atomic unsigned __int128*)&atomic_cell_none, atomic_expected, atomic_expected);
                int atomic_machine_second = atomic_machine_call((_Atomic unsigned __int128*)&atomic_cell_machine, atomic_expected, atomic_expected);
                BUSTER_TEST(arguments, atomic_none_first == atomic_machine_first && atomic_none_second == atomic_machine_second && atomic_none_first && !atomic_none_second &&
                                           atomic_cell_none == atomic_cell_machine);
            }
            codegen_release_executable(atomic_machine_executable);
            codegen_release_executable(atomic_none_executable);
        }
#endif
        IrFunction* goto_function = machine_test_ir_function_find(machine_module, S8("goto_probe"));
        BUSTER_TEST(arguments, goto_function != 0);
        if (goto_function)
        {
            MachineSelectResult goto_selected = machine_select_canonical_function(arguments->arena, machine_program, goto_function, machine_target);
            BUSTER_TEST(arguments, goto_selected.supported);
            if (goto_selected.supported)
            {
                MachineStackPlacement goto_placement = machine_stack_placement_build(arguments->arena, &goto_selected.function);
                MachineEncodeResult goto_encoded = machine_encode_x86_64(arguments->arena, &goto_selected.function, &goto_placement);
                BUSTER_TEST(arguments, goto_placement.valid);
                BUSTER_TEST(arguments, goto_encoded.valid && goto_encoded.byte_count != 0);
            }
        }
        // Computed-goto census: each shape below keeps the complete IR target
        // set on the machine terminator and exercises a different label-value
        // provenance path (forward/backward, conditional, array storage,
        // switch selection, and a label address without an indirect branch).
        // The same fixture is selected, verified, placed, and encoded on both
        // backends; the module then runs through NONE/MIR_STACK/FAST/QUALITY
        // so every allocator sees the same successor population.
        String8 label_fixture_source = S8(
            "int labels_one(int v) { void* p = v ? &&one : &&two; goto *p; one: return 1; two: return 2; }\n"
            "int labels_three(int v) { void* p; if (v < 0) p = &&neg; else if (v) p = &&pos; else p = &&zero; goto *p; neg: return -1; pos: return 1; zero: return 0; }\n"
            "int labels_four(int v) { void* p = &&a; if (v == 1) p = &&b; if (v == 2) p = &&c; if (v == 3) p = &&d; goto *p; a: return 10; b: return 11; c: return 12; d: return 13; }\n"
            "int labels_loop(int v) { void* p = &&loop; loop: if (v-- > 0) goto *p; return v; }\n"
            "int labels_forward(int v) { void* p = &&done; if (v) goto *p; return 9; done: return 7; }\n"
            "int labels_array(int v) { void* p[2] = {&&zero, &&one}; goto *p[v & 1]; zero: return 0; one: return 1; }\n"
            "int labels_switch(int v) { void* p; switch (v & 3) { case 0: p = &&a; break; case 1: p = &&b; break; case 2: p = &&c; break; default: p = &&d; break; } goto *p; a: return 20; b: return 21; c: return 22; d: return 23; }\n"
            "int labels_conditional(int v) { goto *(v ? &&yes : &&no); yes: return 1; no: return 0; }\n"
            "int labels_address_only(int v) { void* p = &&target; return p == &&target ? v : 0; target: return 1; }\n"
            "int labels_compare(int v) { void* p = &&target; if (p == &&target) goto *p; target: return v; }\n");
        IrProgram* label_fixture_program = machine_test_compile_c(arguments->arena, S8("machine-label-fixtures.c"), label_fixture_source, machine_target);
        BUSTER_TEST(arguments, label_fixture_program && label_fixture_program->module_count);
        if (label_fixture_program && label_fixture_program->module_count)
        {
            IrModule* label_fixture_module = label_fixture_program->modules;
            String8 label_fixture_names[] = {
                S8_INITIALIZER("labels_one"), S8_INITIALIZER("labels_three"), S8_INITIALIZER("labels_four"),
                S8_INITIALIZER("labels_loop"), S8_INITIALIZER("labels_forward"), S8_INITIALIZER("labels_array"),
                S8_INITIALIZER("labels_switch"), S8_INITIALIZER("labels_conditional"), S8_INITIALIZER("labels_address_only"),
                S8_INITIALIZER("labels_compare"),
            };
            u32 fixture_label_rows = 0;
            u32 fixture_indirect_rows = 0;
            for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(label_fixture_names); fixture_index += 1)
            {
                IrFunction* fixture_function = machine_test_ir_function_find(label_fixture_module, label_fixture_names[fixture_index]);
                BUSTER_TEST(arguments, fixture_function != 0);
                if (!fixture_function)
                {
                    continue;
                }
                MachineSelectResult fixture_selected =
                    machine_select_canonical_function(arguments->arena, label_fixture_program, fixture_function, machine_target);
                BUSTER_TEST_RAW(arguments, fixture_selected.supported && fixture_selected.selector_certified,
                                string_format(arguments->arena, S8("label fixture {S8} select opcode {u32}"), label_fixture_names[fixture_index],
                                              (u32)fixture_selected.failed_opcode));
                if (!fixture_selected.supported)
                {
                    continue;
                }
                BUSTER_TEST(arguments, machine_verify_function(&fixture_selected.function).error == MACHINE_VERIFY_NONE);
                MachineOpcode fixture_label_opcode = machine_target.cpu_arch == CPU_ARCH_AARCH64 ? MACHINE_A64_LEA_BLOCK : MACHINE_X64_LEA_BLOCK;
                MachineOpcode fixture_indirect_opcode =
                    machine_target.cpu_arch == CPU_ARCH_AARCH64 ? MACHINE_A64_INDIRECT_BRANCH : MACHINE_X64_INDIRECT_BRANCH;
                for (u32 row_index = 0; row_index < fixture_selected.function.instruction_count; row_index += 1)
                {
                    MachineInstruction* row = fixture_selected.function.instructions + row_index;
                    if (row->opcode == fixture_label_opcode)
                    {
                        fixture_label_rows += 1;
                        BUSTER_TEST(arguments, row->payload < fixture_selected.function.block_count);
                    }
                    if (row->opcode != fixture_indirect_opcode)
                    {
                        continue;
                    }
                    fixture_indirect_rows += 1;
                    BUSTER_TEST(arguments, row->flags != 0 && row->payload <= fixture_selected.function.switch_case_count &&
                                               row->flags <= fixture_selected.function.switch_case_count - row->payload);
                    u32 owner_block = UINT32_MAX;
                    for (u32 block_index = 0; block_index < fixture_selected.function.block_count; block_index += 1)
                    {
                        MachineBlock* block = fixture_selected.function.blocks + block_index;
                        if (row_index >= block->first_instruction && row_index < block->first_instruction + block->instruction_count)
                        {
                            owner_block = block_index;
                            break;
                        }
                    }
                    BUSTER_TEST(arguments, owner_block != UINT32_MAX);
                    for (u32 case_index = 0; owner_block != UINT32_MAX && case_index < row->flags; case_index += 1)
                    {
                        u32 target_block = fixture_selected.function.switch_cases[row->payload + case_index].target_block;
                        bool edge_found = false;
                        for (u32 edge_index = 0; edge_index < fixture_selected.function.edge_count; edge_index += 1)
                        {
                            MachineEdge* edge = fixture_selected.function.edges + edge_index;
                            edge_found |= edge->source_block == owner_block && edge->destination_block == target_block;
                        }
                        BUSTER_TEST(arguments, edge_found);
                    }
                }
                MachineStackPlacement fixture_stack = machine_stack_placement_build(arguments->arena, &fixture_selected.function);
                BUSTER_TEST(arguments, fixture_stack.valid);
                MachineEncodeResult fixture_encoded = machine_target.cpu_arch == CPU_ARCH_AARCH64
                                                           ? machine_encode_aarch64(arguments->arena, &fixture_selected.function, &fixture_stack)
                                                           : machine_encode_x86_64(arguments->arena, &fixture_selected.function, &fixture_stack);
                BUSTER_TEST_RAW(arguments, fixture_encoded.valid && fixture_encoded.byte_count != 0,
                                string_format(arguments->arena, S8("label fixture {S8} encode"), label_fixture_names[fixture_index]));
            }
            BUSTER_TEST(arguments, fixture_label_rows >= BUSTER_ARRAY_LENGTH(label_fixture_names));
            BUSTER_TEST(arguments, fixture_indirect_rows >= BUSTER_ARRAY_LENGTH(label_fixture_names) - 1);
            CodegenRegisterAllocatorMode fixture_modes[] = {
                CODEGEN_REGISTER_ALLOCATOR_NONE,
                CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                CODEGEN_REGISTER_ALLOCATOR_FAST,
                CODEGEN_REGISTER_ALLOCATOR_QUALITY,
            };
            for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(fixture_modes); mode_index += 1)
            {
                CodegenModule fixture_module = codegen_generate_canonical_module(
                    arguments->arena, label_fixture_program, label_fixture_module, machine_target,
                    (CodegenModuleOptions){.register_allocator = (u8)fixture_modes[mode_index]});
                BUSTER_TEST_RAW(arguments, fixture_module.error == CODEGEN_ERROR_NONE && fixture_module.statistics.fallback_function_count == 0,
                                string_format(arguments->arena, S8("label fixture mode {S8} fallback {u32}"),
                                              codegen_register_allocator_mode_string(fixture_modes[mode_index]),
                                              fixture_module.statistics.fallback_function_count));
                BUSTER_TEST(arguments, fixture_module.relocation_count == 0);
            }
        }
        Target label_fixture_a64_target = {
            .cpu_arch = CPU_ARCH_AARCH64,
            .os = OPERATING_SYSTEM_LINUX,
        };
        IrProgram* label_fixture_a64_program =
            machine_test_compile_c(arguments->arena, S8("machine-label-fixtures-a64.c"), label_fixture_source, label_fixture_a64_target);
        BUSTER_TEST(arguments, label_fixture_a64_program && label_fixture_a64_program->module_count);
        if (label_fixture_a64_program && label_fixture_a64_program->module_count)
        {
            IrModule* label_fixture_a64_module = label_fixture_a64_program->modules;
            String8 label_fixture_a64_names[] = {
                S8_INITIALIZER("labels_one"), S8_INITIALIZER("labels_three"), S8_INITIALIZER("labels_four"),
                S8_INITIALIZER("labels_loop"), S8_INITIALIZER("labels_forward"), S8_INITIALIZER("labels_array"),
                S8_INITIALIZER("labels_switch"), S8_INITIALIZER("labels_conditional"), S8_INITIALIZER("labels_address_only"),
                S8_INITIALIZER("labels_compare"),
            };
            u32 a64_label_rows = 0;
            u32 a64_indirect_rows = 0;
            for (u32 fixture_index = 0; fixture_index < BUSTER_ARRAY_LENGTH(label_fixture_a64_names); fixture_index += 1)
            {
                IrFunction* fixture_function = machine_test_ir_function_find(label_fixture_a64_module, label_fixture_a64_names[fixture_index]);
                BUSTER_TEST(arguments, fixture_function != 0);
                if (!fixture_function)
                {
                    continue;
                }
                MachineSelectResult fixture_selected =
                    machine_select_canonical_function(arguments->arena, label_fixture_a64_program, fixture_function, label_fixture_a64_target);
                BUSTER_TEST_RAW(arguments, fixture_selected.supported && fixture_selected.selector_certified,
                                string_format(arguments->arena, S8("a64 label fixture {S8} select opcode {u32}"),
                                              label_fixture_a64_names[fixture_index], (u32)fixture_selected.failed_opcode));
                if (!fixture_selected.supported)
                {
                    continue;
                }
                BUSTER_TEST(arguments, machine_verify_function(&fixture_selected.function).error == MACHINE_VERIFY_NONE);
                for (u32 row_index = 0; row_index < fixture_selected.function.instruction_count; row_index += 1)
                {
                    MachineInstruction* row = fixture_selected.function.instructions + row_index;
                    if (row->opcode == MACHINE_A64_LEA_BLOCK)
                    {
                        a64_label_rows += 1;
                        BUSTER_TEST(arguments, row->payload < fixture_selected.function.block_count);
                    }
                    if (row->opcode != MACHINE_A64_INDIRECT_BRANCH)
                    {
                        continue;
                    }
                    a64_indirect_rows += 1;
                    BUSTER_TEST(arguments, row->flags != 0 && row->payload <= fixture_selected.function.switch_case_count &&
                                               row->flags <= fixture_selected.function.switch_case_count - row->payload);
                    u32 owner_block = UINT32_MAX;
                    for (u32 block_index = 0; block_index < fixture_selected.function.block_count; block_index += 1)
                    {
                        MachineBlock* block = fixture_selected.function.blocks + block_index;
                        if (row_index >= block->first_instruction && row_index < block->first_instruction + block->instruction_count)
                        {
                            owner_block = block_index;
                            break;
                        }
                    }
                    for (u32 case_index = 0; owner_block != UINT32_MAX && case_index < row->flags; case_index += 1)
                    {
                        u32 target_block = fixture_selected.function.switch_cases[row->payload + case_index].target_block;
                        bool edge_found = false;
                        for (u32 edge_index = 0; edge_index < fixture_selected.function.edge_count; edge_index += 1)
                        {
                            MachineEdge* edge = fixture_selected.function.edges + edge_index;
                            edge_found |= edge->source_block == owner_block && edge->destination_block == target_block;
                        }
                        BUSTER_TEST(arguments, edge_found);
                    }
                }
                MachineStackPlacement fixture_stack = machine_stack_placement_build(arguments->arena, &fixture_selected.function);
                BUSTER_TEST(arguments, fixture_stack.valid);
                MachineEncodeResult fixture_encoded = machine_encode_aarch64(arguments->arena, &fixture_selected.function, &fixture_stack);
                BUSTER_TEST_RAW(arguments, fixture_encoded.valid && fixture_encoded.byte_count != 0,
                                string_format(arguments->arena, S8("a64 label fixture {S8} encode"), label_fixture_a64_names[fixture_index]));
            }
            BUSTER_TEST(arguments, a64_label_rows >= BUSTER_ARRAY_LENGTH(label_fixture_a64_names));
            BUSTER_TEST(arguments, a64_indirect_rows >= BUSTER_ARRAY_LENGTH(label_fixture_a64_names) - 1);
            CodegenRegisterAllocatorMode fixture_modes[] = {
                CODEGEN_REGISTER_ALLOCATOR_NONE,
                CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                CODEGEN_REGISTER_ALLOCATOR_FAST,
                CODEGEN_REGISTER_ALLOCATOR_QUALITY,
            };
            for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(fixture_modes); mode_index += 1)
            {
                CodegenModule fixture_module = codegen_generate_canonical_module(
                    arguments->arena, label_fixture_a64_program, label_fixture_a64_module, label_fixture_a64_target,
                    (CodegenModuleOptions){.register_allocator = (u8)fixture_modes[mode_index]});
                BUSTER_TEST_RAW(arguments, fixture_module.error == CODEGEN_ERROR_NONE && fixture_module.statistics.fallback_function_count == 0,
                                string_format(arguments->arena, S8("a64 label fixture mode {S8} fallback {u32}"),
                                              codegen_register_allocator_mode_string(fixture_modes[mode_index]),
                                              fixture_module.statistics.fallback_function_count));
                BUSTER_TEST(arguments, fixture_module.relocation_count == 0);
            }
        }
        // Variadic direct calls select (AL zero, register-only integer
        // arguments); execution is proven by the linked soak because the
        // extern callee cannot resolve in a raw code copy.
        IrFunction* variadic_function = machine_test_ir_function_find(machine_module, S8("call_variadic"));
        BUSTER_TEST(arguments, variadic_function != 0);
        if (variadic_function)
        {
            MachineSelectResult variadic_selected = machine_select_canonical_function(arguments->arena, machine_program, variadic_function, machine_target);
            BUSTER_TEST(arguments, variadic_selected.supported);
        }
        IrFunction* variadic_named_function = machine_test_ir_function_find(machine_module, S8("variadic_named"));
        IrFunction* variadic_named_caller_function = machine_test_ir_function_find(machine_module, S8("variadic_named_caller"));
        IrFunction* variadic_observe_function = machine_test_ir_function_find(machine_module, S8("variadic_observe"));
        IrFunction* variadic_observe_caller_function = machine_test_ir_function_find(machine_module, S8("variadic_observe_caller"));
        IrFunction* variadic_unsupported_first_function = machine_test_ir_function_find(machine_module, S8("variadic_unsupported_first"));
        BUSTER_TEST(arguments, variadic_named_function && variadic_named_caller_function && variadic_observe_function &&
                                   variadic_observe_caller_function && variadic_unsupported_first_function);
        if (variadic_named_function && variadic_named_caller_function && variadic_observe_function && variadic_observe_caller_function &&
            variadic_unsupported_first_function)
        {
            MachineSelectResult variadic_named_selected =
                machine_select_canonical_function(arguments->arena, machine_program, variadic_named_function, machine_target);
            MachineSelectResult variadic_named_caller_selected =
                machine_select_canonical_function(arguments->arena, machine_program, variadic_named_caller_function, machine_target);
            MachineSelectResult variadic_observe_selected =
                machine_select_canonical_function(arguments->arena, machine_program, variadic_observe_function, machine_target);
            MachineSelectResult variadic_observe_caller_selected =
                machine_select_canonical_function(arguments->arena, machine_program, variadic_observe_caller_function, machine_target);
            MachineSelectResult variadic_unsupported_first_selected =
                machine_select_canonical_function(arguments->arena, machine_program, variadic_unsupported_first_function, machine_target);
            BUSTER_TEST(arguments, variadic_named_selected.supported && variadic_named_caller_selected.supported &&
                                       variadic_observe_caller_selected.supported);
            // SysV variadic bodies are now selected by the machine path.  The
            // selector emits one entry save row and carries every VA_ARG's
            // ABI classification in the function side table; VA_START itself
            // is lowered to ordinary frame stores and therefore is no longer
            // the first unsupported opcode.
            BUSTER_TEST(arguments, variadic_observe_selected.supported);
            if (variadic_observe_selected.supported)
            {
                u32 va_save_rows = 0;
                u32 va_arg_rows = 0;
                for (u32 instruction_index = 0; instruction_index < variadic_observe_selected.function.instruction_count; instruction_index += 1)
                {
                    MachineInstruction* machine_instruction = variadic_observe_selected.function.instructions + instruction_index;
                    if (machine_instruction->opcode == MACHINE_X64_VA_SAVE)
                    {
                        va_save_rows += 1;
                    }
                    else if (machine_instruction->opcode == MACHINE_X64_VA_ARG)
                    {
                        va_arg_rows += 1;
                        BUSTER_TEST(arguments, machine_instruction->payload < variadic_observe_selected.function.va_arg_count);
                        if (machine_instruction->payload < variadic_observe_selected.function.va_arg_count)
                        {
                            MachineVaArg* metadata = variadic_observe_selected.function.va_args + machine_instruction->payload;
                            BUSTER_TEST(arguments, metadata->part_count == 1 && !metadata->result_is_frame);
                            BUSTER_TEST(arguments, metadata->parts[0].is_memory == 0);
                            // The scalar result is the constrained fixed RCX
                            // operand, while the list pointer is fixed RAX.
                            BUSTER_TEST(arguments, machine_ref_kind(machine_instruction->operands[0]) == MACHINE_REF_VIRTUAL_REGISTER);
                            BUSTER_TEST(arguments, machine_ref_kind(machine_instruction->operands[1]) == MACHINE_REF_VIRTUAL_REGISTER);
                        }
                    }
                }
                BUSTER_TEST(arguments, va_save_rows == 1);
                BUSTER_TEST(arguments, va_arg_rows == variadic_observe_selected.function.va_arg_count && va_arg_rows == 16);
            }
            // The Windows x86-64 variadic definition ABI is intentionally
            // outside this SysV machine subset and must stay on fallback.
            Target windows_machine_target = machine_target;
            windows_machine_target.os = OPERATING_SYSTEM_WINDOWS;
            MachineSelectResult windows_variadic_selected =
                machine_select_canonical_function(arguments->arena, machine_program, variadic_observe_function, windows_machine_target);
            BUSTER_TEST(arguments, !windows_variadic_selected.supported);
            BUSTER_TEST(arguments, !variadic_unsupported_first_selected.supported &&
                                       variadic_unsupported_first_selected.failed_opcode == IR_OPCODE_INLINE_ASSEMBLY);
        }
        // The lifted non-vector gaps: a thread-local address (ELF local-exec
        // fs sequence), an rvalue compound-literal array base, a variadic
        // call past sixteen arguments, and 128-bit integers ferried through
        // calls as the two-eightbyte pair. Their relocations or link-only
        // callees keep execution with the full-unity soak, where every one
        // of these shapes runs in the compiler's own hot path.
        String8 lifted_gap_names[] = {
            S8_INITIALIZER("tls_bump"),
            S8_INITIALIZER("rv_lit"),
            S8_INITIALIZER("u128_ferry"),
            S8_INITIALIZER("call_seventeen"),
            S8_INITIALIZER("i8_to_i128"),
            S8_INITIALIZER("u8_to_u128"),
            S8_INITIALIZER("i16_to_i128"),
            S8_INITIALIZER("u16_to_u128"),
            S8_INITIALIZER("i32_to_i128"),
            S8_INITIALIZER("u32_to_u128"),
            S8_INITIALIZER("i128_reinterpret"),
            S8_INITIALIZER("u128_shr0"),
            S8_INITIALIZER("u128_shr1"),
            S8_INITIALIZER("u128_shr63"),
            S8_INITIALIZER("u128_shr64"),
            S8_INITIALIZER("u128_shr65"),
            S8_INITIALIZER("u128_shr127"),
        };
        for (u32 lifted_index = 0; lifted_index < BUSTER_ARRAY_LENGTH(lifted_gap_names); lifted_index += 1)
        {
            IrFunction* lifted_function = machine_test_ir_function_find(machine_module, lifted_gap_names[lifted_index]);
            BUSTER_TEST(arguments, lifted_function != 0);
            if (lifted_function)
            {
                MachineSelectResult lifted_selected = machine_select_canonical_function(arguments->arena, machine_program, lifted_function, machine_target);
                BUSTER_TEST_RAW(arguments, lifted_selected.supported,
                                string_format(arguments->arena, S8("select {S8} failed at opcode {u32}"), lifted_gap_names[lifted_index],
                                              (u32)lifted_selected.failed_opcode));
            }
        }
        String8 i128_machine_names[] = {
            S8_INITIALIZER("i8_to_i128"), S8_INITIALIZER("u8_to_u128"), S8_INITIALIZER("i16_to_i128"), S8_INITIALIZER("u16_to_u128"),
            S8_INITIALIZER("i32_to_i128"), S8_INITIALIZER("u32_to_u128"), S8_INITIALIZER("i128_reinterpret"), S8_INITIALIZER("u128_shr0"),
            S8_INITIALIZER("u128_shr1"), S8_INITIALIZER("u128_shr63"), S8_INITIALIZER("u128_shr64"), S8_INITIALIZER("u128_shr65"),
            S8_INITIALIZER("u128_shr127"),
        };
        for (u32 i128_index = 0; i128_index < BUSTER_ARRAY_LENGTH(i128_machine_names); i128_index += 1)
        {
            IrFunction* i128_function = machine_test_ir_function_find(machine_module, i128_machine_names[i128_index]);
            BUSTER_TEST(arguments, i128_function != 0);
            if (!i128_function)
            {
                continue;
            }
            MachineSelectResult i128_selected = machine_select_canonical_function(arguments->arena, machine_program, i128_function, machine_target);
            BUSTER_TEST_RAW(arguments, i128_selected.supported,
                            string_format(arguments->arena, S8("select {S8} failed at opcode {u32}"), i128_machine_names[i128_index],
                                          (u32)i128_selected.failed_opcode));
            if (i128_selected.supported)
            {
                bool saw_copy16 = false;
                bool saw_shift = false;
                bool saw_or = false;
                bool saw_high_load = false;
                bool saw_low_store = false;
                bool saw_high_store = false;
                bool saw_zero = false;
                for (u32 row_index = 0; row_index < i128_selected.function.instruction_count; row_index += 1)
                {
                    MachineInstruction* row = i128_selected.function.instructions + row_index;
                    saw_copy16 |= row->opcode == MACHINE_X64_COPY_FRAME_FROM_FRAME && row->payload == 16;
                    saw_shift |= row->opcode == MACHINE_X64_SHR64;
                    saw_or |= row->opcode == MACHINE_X64_OR64;
                    saw_high_load |= row->opcode == MACHINE_X64_LOAD_FRAME && row->payload == 8;
                    saw_low_store |= row->opcode == MACHINE_X64_STORE_FRAME64 && row->payload == 0;
                    saw_high_store |= row->opcode == MACHINE_X64_STORE_FRAME64 && row->payload == 8;
                    saw_zero |= row->opcode == MACHINE_X64_MOV_RI && machine_ref_kind(row->operands[1]) == MACHINE_REF_IMMEDIATE &&
                                machine_ref_payload(row->operands[1]) < i128_selected.function.immediate_count &&
                                i128_selected.function.immediates[machine_ref_payload(row->operands[1])] == 0;
                }
                if (i128_index == 6 || i128_index == 7)
                {
                    BUSTER_TEST(arguments, saw_copy16);
                }
                if (i128_index >= 8 && i128_index != 10)
                {
                    BUSTER_TEST_RAW(arguments, saw_shift,
                                    string_format(arguments->arena, S8("{S8} missing shift"), i128_machine_names[i128_index]));
                }
                if (i128_index == 10)
                {
                    BUSTER_TEST_RAW(arguments, saw_high_load, S8("u128_shr64 missing high load"));
                    BUSTER_TEST_RAW(arguments, saw_low_store, S8("u128_shr64 missing low store"));
                    BUSTER_TEST_RAW(arguments, saw_high_store, S8("u128_shr64 missing high store"));
                    BUSTER_TEST_RAW(arguments, saw_zero, S8("u128_shr64 missing zero"));
                }
                if (i128_index == 8 || i128_index == 9)
                {
                    BUSTER_TEST(arguments, saw_or);
                }
                MachineStackPlacement i128_placement = machine_stack_placement_build(arguments->arena, &i128_selected.function);
                BUSTER_TEST(arguments, i128_placement.valid);
                MachineEncodeResult i128_encoded = machine_encode_x86_64(arguments->arena, &i128_selected.function, &i128_placement);
                BUSTER_TEST(arguments, i128_encoded.valid && i128_encoded.byte_count > 8);
            }
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
        // i128/u128 casts use the same two-eightbyte frame representation as
        // the ABI's integer aggregate pair.  Exercise every signedness
        // direction through the machine bytes and compare against the
        // canonical NONE entry, including a negative signed widening and an
        // unsigned value whose low half has its top bit set.
        {
            typedef unsigned __int128 MachineTestCallU128(u64);
            typedef __int128 MachineTestCallI128(s64);
            typedef u64 MachineTestCallU64(unsigned __int128);
            typedef s64 MachineTestCallI64(__int128);
            String8 cast_names[] = {
                S8_INITIALIZER("u128_to_u64"), S8_INITIALIZER("i128_to_i64"), S8_INITIALIZER("u64_to_u128"), S8_INITIALIZER("i64_to_i128"),
            };
            CodegenExecutable cast_none_module_executable = codegen_make_executable((CodegenFunction){
                .code = none_module.code,
            });
            BUSTER_TEST(arguments, cast_none_module_executable.error == CODEGEN_ERROR_NONE);
            for (u32 cast_index = 0; cast_index < BUSTER_ARRAY_LENGTH(cast_names); cast_index += 1)
            {
                IrFunction* cast_function = machine_test_ir_function_find(machine_module, cast_names[cast_index]);
                BUSTER_TEST(arguments, cast_function != 0);
                if (!cast_function)
                {
                    continue;
                }
                MachineSelectResult cast_selected = {0};
                MachineEncodeResult cast_machine = machine_test_encode(arguments->arena, machine_program, cast_function, machine_target, &cast_selected);
                BUSTER_TEST_RAW(arguments, cast_selected.supported,
                                string_format(arguments->arena, S8("select {S8} failed at opcode {u32}"), cast_names[cast_index],
                                              (u32)cast_selected.failed_opcode));
                BUSTER_TEST(arguments, cast_machine.valid);
                u32 cast_none_offset = machine_test_module_offset(&none_module, machine_module, cast_names[cast_index]);
                BUSTER_TEST(arguments, cast_none_offset != UINT32_MAX);
                CodegenExecutable cast_machine_executable = codegen_make_executable((CodegenFunction){
                    .code = {.pointer = cast_machine.bytes, .length = cast_machine.byte_count},
                });
                BUSTER_TEST(arguments, cast_machine_executable.error == CODEGEN_ERROR_NONE);
                if (cast_none_offset == UINT32_MAX || !cast_machine_executable.address || !cast_none_module_executable.address)
                {
                    codegen_release_executable(cast_machine_executable);
                    continue;
                }
                void* cast_none_address = (u8*)cast_none_module_executable.address + cast_none_offset;
                void* cast_machine_address = cast_machine_executable.address;
                bool cast_equal = true;
                if (cast_index == 0)
                {
                    MachineTestCallU64* none_call = 0;
                    MachineTestCallU64* machine_call = 0;
                    memcpy(&none_call, &cast_none_address, sizeof(none_call));
                    memcpy(&machine_call, &cast_machine_address, sizeof(machine_call));
                    unsigned __int128 value = ((unsigned __int128)UINT64_C(0x8000000000000001) << 64) | UINT64_C(0xdeadbeefcafebabe);
                    cast_equal = none_call(value) == machine_call(value);
                }
                else if (cast_index == 1)
                {
                    MachineTestCallI64* none_call = 0;
                    MachineTestCallI64* machine_call = 0;
                    memcpy(&none_call, &cast_none_address, sizeof(none_call));
                    memcpy(&machine_call, &cast_machine_address, sizeof(machine_call));
                    __int128 value = -((__int128)1 << 100) - 5;
                    cast_equal = none_call(value) == machine_call(value);
                }
                else if (cast_index == 2)
                {
                    MachineTestCallU128* none_call = 0;
                    MachineTestCallU128* machine_call = 0;
                    memcpy(&none_call, &cast_none_address, sizeof(none_call));
                    memcpy(&machine_call, &cast_machine_address, sizeof(machine_call));
                    u64 value = UINT64_C(0x8000000000000001);
                    unsigned __int128 none_value = none_call(value);
                    unsigned __int128 machine_value = machine_call(value);
                    cast_equal = none_value == machine_value;
                    BUSTER_TEST(arguments, (u64)(machine_value >> 64) == 0);
                }
                else
                {
                    MachineTestCallI128* none_call = 0;
                    MachineTestCallI128* machine_call = 0;
                    memcpy(&none_call, &cast_none_address, sizeof(none_call));
                    memcpy(&machine_call, &cast_machine_address, sizeof(machine_call));
                    s64 value = -1;
                    __int128 none_value = none_call(value);
                    __int128 machine_value = machine_call(value);
                    cast_equal = none_value == machine_value;
                    BUSTER_TEST(arguments, (u64)((unsigned __int128)machine_value >> 64) == UINT64_MAX);
                    value = INT64_MIN;
                    none_value = none_call(value);
                    machine_value = machine_call(value);
                    cast_equal &= none_value == machine_value;
                    BUSTER_TEST(arguments, (u64)((unsigned __int128)machine_value >> 64) == UINT64_MAX);
                }
                BUSTER_TEST_RAW(arguments, cast_equal, cast_names[cast_index]);
                codegen_release_executable(cast_machine_executable);
            }
            codegen_release_executable(cast_none_module_executable);
        }
        {
            typedef __int128 MachineTestCallI8ToI128(signed char);
            typedef unsigned __int128 MachineTestCallU8ToU128(unsigned char);
            typedef __int128 MachineTestCallI16ToI128(short);
            typedef unsigned __int128 MachineTestCallU16ToU128(unsigned short);
            typedef __int128 MachineTestCallI32ToI128(int);
            typedef unsigned __int128 MachineTestCallU32ToU128(unsigned int);
            typedef unsigned __int128 MachineTestCallI128Reinterpret(__int128);
            typedef unsigned __int128 MachineTestCallU128Shift(unsigned __int128);
            String8 widening_names[] = {
                S8_INITIALIZER("i8_to_i128"), S8_INITIALIZER("u8_to_u128"), S8_INITIALIZER("i16_to_i128"), S8_INITIALIZER("u16_to_u128"),
                S8_INITIALIZER("i32_to_i128"), S8_INITIALIZER("u32_to_u128"), S8_INITIALIZER("i128_reinterpret"),
            };
            CodegenExecutable widening_none_executable = codegen_make_executable((CodegenFunction){.code = none_module.code});
            BUSTER_TEST(arguments, widening_none_executable.error == CODEGEN_ERROR_NONE);
            for (u32 widening_index = 0; widening_index < BUSTER_ARRAY_LENGTH(widening_names); widening_index += 1)
            {
                IrFunction* widening_function = machine_test_ir_function_find(machine_module, widening_names[widening_index]);
                BUSTER_TEST(arguments, widening_function != 0);
                if (!widening_function)
                {
                    continue;
                }
                MachineSelectResult widening_selected = {0};
                MachineEncodeResult widening_machine = machine_test_encode(arguments->arena, machine_program, widening_function, machine_target, &widening_selected);
                BUSTER_TEST(arguments, widening_selected.supported && widening_machine.valid);
                u32 none_offset = machine_test_module_offset(&none_module, machine_module, widening_names[widening_index]);
                BUSTER_TEST(arguments, none_offset != UINT32_MAX);
                CodegenExecutable widening_machine_executable = codegen_make_executable((CodegenFunction){
                    .code = {.pointer = widening_machine.bytes, .length = widening_machine.byte_count},
                });
                BUSTER_TEST(arguments, widening_machine_executable.error == CODEGEN_ERROR_NONE);
                if (none_offset == UINT32_MAX || !widening_machine_executable.address || !widening_none_executable.address)
                {
                    codegen_release_executable(widening_machine_executable);
                    continue;
                }
                void* none_address = (u8*)widening_none_executable.address + none_offset;
                void* machine_address = widening_machine_executable.address;
                bool equal = true;
                if (widening_index == 0)
                {
                    MachineTestCallI8ToI128* none_call = 0;
                    MachineTestCallI8ToI128* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    signed char values[] = {INT8_MIN, -1, 0, 1, INT8_MAX};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                else if (widening_index == 1)
                {
                    MachineTestCallU8ToU128* none_call = 0;
                    MachineTestCallU8ToU128* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    unsigned char values[] = {0, 1, 127, 128, UINT8_MAX};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                else if (widening_index == 2)
                {
                    MachineTestCallI16ToI128* none_call = 0;
                    MachineTestCallI16ToI128* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    short values[] = {INT16_MIN, -1, 0, 1, INT16_MAX};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                else if (widening_index == 3)
                {
                    MachineTestCallU16ToU128* none_call = 0;
                    MachineTestCallU16ToU128* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    unsigned short values[] = {0, 1, 32767, 32768, UINT16_MAX};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                else if (widening_index == 4)
                {
                    MachineTestCallI32ToI128* none_call = 0;
                    MachineTestCallI32ToI128* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    int values[] = {INT32_MIN, -1, 0, 1, INT32_MAX};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                else if (widening_index == 5)
                {
                    MachineTestCallU32ToU128* none_call = 0;
                    MachineTestCallU32ToU128* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    unsigned int values[] = {0, 1, UINT32_C(0x7fffffff), UINT32_C(0x80000000), UINT32_MAX};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                else
                {
                    MachineTestCallI128Reinterpret* none_call = 0;
                    MachineTestCallI128Reinterpret* machine_call = 0;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    __int128 values[] = {0, -1, -((__int128)1 << 126) - ((__int128)1 << 125)};
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                }
                BUSTER_TEST_RAW(arguments, equal, widening_names[widening_index]);
                codegen_release_executable(widening_machine_executable);
            }
            codegen_release_executable(widening_none_executable);

            String8 shift_names[] = {
                S8_INITIALIZER("u128_shr0"), S8_INITIALIZER("u128_shr1"), S8_INITIALIZER("u128_shr63"), S8_INITIALIZER("u128_shr64"),
                S8_INITIALIZER("u128_shr65"), S8_INITIALIZER("u128_shr127"),
            };
            for (u32 shift_index = 0; shift_index < BUSTER_ARRAY_LENGTH(shift_names); shift_index += 1)
            {
                IrFunction* shift_function = machine_test_ir_function_find(machine_module, shift_names[shift_index]);
                BUSTER_TEST(arguments, shift_function != 0);
                if (!shift_function)
                {
                    continue;
                }
                MachineSelectResult shift_selected = {0};
                MachineEncodeResult shift_machine = machine_test_encode(arguments->arena, machine_program, shift_function, machine_target, &shift_selected);
                BUSTER_TEST(arguments, shift_selected.supported && shift_machine.valid);
                u32 none_offset = machine_test_module_offset(&none_module, machine_module, shift_names[shift_index]);
                BUSTER_TEST(arguments, none_offset != UINT32_MAX);
                CodegenExecutable shift_none_executable = codegen_make_executable((CodegenFunction){.code = none_module.code});
                CodegenExecutable shift_machine_executable = codegen_make_executable((CodegenFunction){
                    .code = {.pointer = shift_machine.bytes, .length = shift_machine.byte_count},
                });
                BUSTER_TEST(arguments, shift_none_executable.error == CODEGEN_ERROR_NONE && shift_machine_executable.error == CODEGEN_ERROR_NONE);
                if (none_offset != UINT32_MAX && shift_none_executable.address && shift_machine_executable.address)
                {
                    MachineTestCallU128Shift* none_call = 0;
                    MachineTestCallU128Shift* machine_call = 0;
                    void* none_address = (u8*)shift_none_executable.address + none_offset;
                    void* machine_address = shift_machine_executable.address;
                    memcpy(&none_call, &none_address, sizeof(none_call));
                    memcpy(&machine_call, &machine_address, sizeof(machine_call));
                    unsigned __int128 values[] = {
                        0,
                        1,
                        UINT64_MAX,
                        ((unsigned __int128)UINT64_C(0x8000000000000001) << 64) | UINT64_C(0xdeadbeefcafebabe),
                        (unsigned __int128)UINT64_MAX << 64,
                    };
                    bool equal = true;
                    for (u32 value_index = 0; value_index < BUSTER_ARRAY_LENGTH(values); value_index += 1) equal &= none_call(values[value_index]) == machine_call(values[value_index]);
                    BUSTER_TEST_RAW(arguments, equal, shift_names[shift_index]);
                }
                codegen_release_executable(shift_machine_executable);
                codegen_release_executable(shift_none_executable);
            }
        }
#endif
        // Live-range splitting: split_phase must take at least one split — a value
        // register-resident for the first loop only, installed on the
        // entering edges and stored back at the landing pad — and the
        // placement must survive the pin verifier, since a degraded
        // placement reports zero splits. The traffic bound holds the
        // whole-placement acceptance to its meaning.
        IrFunction* split_function = machine_test_ir_function_find(machine_module, S8("split_phase"));
        BUSTER_TEST(arguments, split_function != 0);
        if (split_function)
        {
            MachineSelectResult split_selected = machine_select_canonical_function(arguments->arena, machine_program, split_function, machine_target);
            BUSTER_TEST(arguments, split_selected.supported);
            if (split_selected.supported)
            {
                MachineStackPlacement split_quality = machine_quality_placement_build(arguments->arena, &split_selected.function);
                MachineStackPlacement split_fast = machine_fast_placement_build(arguments->arena, &split_selected.function);
                BUSTER_TEST(arguments, split_quality.valid && split_fast.valid);
                BUSTER_TEST_RAW(arguments, split_quality.split_register_count + split_quality.pinned_register_count >= 1,
                                string_format(arguments->arena, S8("split_phase splits {u32} pins {u32}"), split_quality.split_register_count,
                                              split_quality.pinned_register_count));
                BUSTER_TEST(arguments, split_quality.reload_count + split_quality.spill_count < split_fast.reload_count + split_fast.spill_count);
            }
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
        CodegenExecutable none_executable = codegen_make_executable((CodegenFunction){
            .code = none_module.code,
        });
        BUSTER_TEST(arguments, none_executable.error == CODEGEN_ERROR_NONE);
        typedef s64 MachineTestCall2(s64, s64);
        typedef s64 MachineTestCall6(s64, s64, s64, s64, s64, s64);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(supported_names) && none_executable.address; name_index += 1)
        {
            if (!machine_encoded[name_index].valid)
            {
                continue;
            }
            // Functions touching global storage cannot execute from a raw
            // code copy: their data relocations only resolve at link time.
            // The full-unity soak is their execution proof.
            bool touches_globals = string_equal(supported_names[name_index], S8("bump")) ||
                                   string_equal(supported_names[name_index], S8("table_get")) ||
                                   string_equal(supported_names[name_index], S8("table_set")) ||
                                   string_equal(supported_names[name_index], S8("pair_sum"));
            // Call-containing functions execute only through the module
            // differential, where their call relocations resolve.
            bool contains_calls = string_equal(supported_names[name_index], S8("span_round_trip")) ||
                                  string_equal(supported_names[name_index], S8("kagg")) ||
                                  string_equal(supported_names[name_index], S8("vla_sum"));
            if (touches_globals || contains_calls)
            {
                continue;
            }
            IrFunction* ir_function = machine_test_ir_function_find(machine_module, supported_names[name_index]);
            u32 none_offset = UINT32_MAX;
            for (u32 entry_index = 0; entry_index < none_module.entry_count; entry_index += 1)
            {
                if (ir_function && none_module.entries[entry_index].symbol.value == ir_function->symbol.value)
                {
                    none_offset = none_module.entries[entry_index].offset;
                    break;
                }
            }
            BUSTER_TEST(arguments, none_offset != UINT32_MAX);
            CodegenExecutable machine_executable = codegen_make_executable((CodegenFunction){
                .code = {.pointer = machine_encoded[name_index].bytes, .length = machine_encoded[name_index].byte_count},
            });
            BUSTER_TEST(arguments, machine_executable.error == CODEGEN_ERROR_NONE);
            if (none_offset == UINT32_MAX || !machine_executable.address)
            {
                continue;
            }
            bool is_writep = string_equal(supported_names[name_index], S8("writep"));
            bool is_readp = string_equal(supported_names[name_index], S8("readp"));
            bool is_six = string_equal(supported_names[name_index], S8("six"));
            bool is_loop = string_equal(supported_names[name_index], S8("sum_to"));
            // Functions declared to return `long` promise all sixty-four
            // result bits; `int` returns compare only the low thirty-two,
            // because the canonical and machine paths may legitimately leave
            // different stale upper bits in RAX.
            bool wide_result = string_equal(supported_names[name_index], S8("widen")) ||
                               string_equal(supported_names[name_index], S8("bitnot")) ||
                               string_equal(supported_names[name_index], S8("sar")) ||
                               string_equal(supported_names[name_index], S8("udiv")) ||
                               string_equal(supported_names[name_index], S8("union_tail")) || is_readp;
            bool is_division = string_equal(supported_names[name_index], S8("divide")) ||
                               string_equal(supported_names[name_index], S8("srem")) ||
                               string_equal(supported_names[name_index], S8("udiv"));
            s64 probe_arguments[][2] = {
                {0, 0}, {1, 2}, {-1, 5}, {123456789, -987654321}, {-2147483647, 2147483647}, {40, 2}, {7, -7},
            };
            bool all_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(probe_arguments); probe_index += 1)
            {
                s64 left = probe_arguments[probe_index][0];
                s64 right = probe_arguments[probe_index][1];
                if (is_loop)
                {
                    // Keep iteration counts test-sized.
                    left &= 63;
                }
                if (is_division)
                {
                    // Never probe a zero divisor; odd divisors also keep the
                    // signed INT_MIN/-1 overflow case out of the grid.
                    right |= 1;
                }
                void* none_address = (u8*)none_executable.address + none_offset;
                void* machine_address = machine_executable.address;
                MachineTestCall2* none_call = 0;
                MachineTestCall2* machine_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&machine_call, &machine_address, sizeof(machine_call));
                if (is_writep)
                {
                    s32 none_cell = 0;
                    s32 machine_cell = 0;
                    none_call((s64)(u64)&none_cell, right);
                    machine_call((s64)(u64)&machine_cell, right);
                    all_equal &= none_cell == machine_cell;
                }
                else if (is_readp)
                {
                    s64 cell = left * 3 + right;
                    all_equal &= none_call((s64)(u64)&cell, 0) == machine_call((s64)(u64)&cell, 0);
                }
                else if (is_six)
                {
                    MachineTestCall6* none_call6 = 0;
                    MachineTestCall6* machine_call6 = 0;
                    memcpy(&none_call6, &none_address, sizeof(none_call6));
                    memcpy(&machine_call6, &machine_address, sizeof(machine_call6));
                    s32 none_result = (s32)none_call6(left, right, left + 1, right + 1, left - 2, right - 2);
                    s32 machine_result = (s32)machine_call6(left, right, left + 1, right + 1, left - 2, right - 2);
                    BUSTER_TEST_RAW(arguments, none_result == machine_result,
                                    string_format(arguments->arena, S8("six none={u64} machine={u64}"), (u64)(u32)none_result, (u64)(u32)machine_result));
                    all_equal &= none_result == machine_result;
                }
                else if (wide_result)
                {
                    all_equal &= none_call(left, right) == machine_call(left, right);
                }
                else
                {
                    s64 none_value = none_call(left, right);
                    s64 machine_value = machine_call(left, right);
                    all_equal &= (s32)none_value == (s32)machine_value;
                }
            }
            BUSTER_TEST_RAW(arguments, all_equal, supported_names[name_index]);
            codegen_release_executable(machine_executable);
        }
        codegen_release_executable(none_executable);
#endif
        // Stage 3 wiring: the same module generated under MIR_STACK routes
        // every eligible function through the machine path and counts the
        // rest as explicit fallbacks; the canonical NONE module is the
        // execution oracle for both kinds.
        CodegenModule mir_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                     (CodegenModuleOptions){
                                                                         .register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                                                                     });
        BUSTER_TEST(arguments, mir_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, mir_module.statistics.exact_attempts >= 1);
        BUSTER_TEST(arguments, mir_module.statistics.exact_successes == mir_module.statistics.exact_attempts);
        BUSTER_TEST(arguments, mir_module.statistics.exact_failures == 0);
        BUSTER_TEST(arguments, none_module.statistics.fallback_function_count == 0);
        BUSTER_TEST_RAW(arguments, mir_module.statistics.fallback_function_count == 1,
                        string_format(arguments->arena, S8("mir fallbacks {u32}"), mir_module.statistics.fallback_function_count));
        IrFunction* mir_add_function = machine_test_ir_function_find(machine_module, S8("add"));
        if (mir_add_function)
        {
            CodegenFunctionDescriptor* mir_add_descriptor = 0;
            for (u32 descriptor_index = 0; descriptor_index < mir_module.function_count; descriptor_index += 1)
            {
                if (mir_module.functions[descriptor_index].symbol.value == mir_add_function->symbol.value)
                {
                    mir_add_descriptor = mir_module.functions + descriptor_index;
                    break;
                }
            }
            BUSTER_TEST(arguments, mir_add_descriptor != 0);
            BUSTER_TEST(arguments, mir_add_descriptor && mir_add_descriptor->code_size > 8);
            BUSTER_TEST(arguments, mir_add_descriptor && mir_add_descriptor->unwind_action_count >= 2 &&
                                       mir_add_descriptor->unwind_actions[0].kind == CODEGEN_UNWIND_ACTION_PUSH_REGISTER);
            // Frameless prologues stop after the frame-pointer setup; framed
            // ones add a chunked subtract (imm8 or imm32) and a probe touch.
            BUSTER_TEST(arguments, mir_add_descriptor &&
                                       (mir_add_descriptor->prolog_size == 4 || mir_add_descriptor->prolog_size == 12 || mir_add_descriptor->prolog_size == 15));
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
        // Both modules resolve their internal direct-call relocations the
        // way the linker would, so machine-to-machine calls execute; this
        // must happen before the executable copies are taken.
        for (u32 relocation_index = 0; relocation_index < none_module.relocation_count + mir_module.relocation_count; relocation_index += 1)
        {
            CodegenModule* patched = relocation_index < none_module.relocation_count ? &none_module : &mir_module;
            u32 local_index = relocation_index < none_module.relocation_count ? relocation_index : relocation_index - none_module.relocation_count;
            CodegenModuleRelocation* relocation = patched->relocations + local_index;
            BUSTER_TEST(arguments, codegen_module_relocation_valid(relocation));
            if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute)
            {
                continue;
            }
            for (u32 entry_index = 0; entry_index < patched->entry_count; entry_index += 1)
            {
                if (patched->entries[entry_index].symbol.value == relocation->symbol.value)
                {
                    u32 displacement = patched->entries[entry_index].offset - (relocation->offset + 4);
                    memcpy(patched->code.pointer + relocation->offset, &displacement, sizeof(displacement));
                    break;
                }
            }
        }
        CodegenExecutable none_module_executable = codegen_make_executable((CodegenFunction){
            .code = none_module.code,
        });
        CodegenExecutable mir_module_executable = codegen_make_executable((CodegenFunction){
            .code = mir_module.code,
        });
        BUSTER_TEST(arguments, none_module_executable.error == CODEGEN_ERROR_NONE && mir_module_executable.error == CODEGEN_ERROR_NONE);
        String8 module_names[] = {
            S8_INITIALIZER("add"), S8_INITIALIZER("mul"), S8_INITIALIZER("widen"), S8_INITIALIZER("narrow"),
            S8_INITIALIZER("negate"), S8_INITIALIZER("bitnot"), S8_INITIALIZER("lnot"), S8_INITIALIZER("less"),
            S8_INITIALIZER("uless"), S8_INITIALIZER("sum_to"), S8_INITIALIZER("divide"), S8_INITIALIZER("with_call"),
            S8_INITIALIZER("locals_array"), S8_INITIALIZER("local_pair"), S8_INITIALIZER("pick"),
            S8_INITIALIZER("aligned_local"), S8_INITIALIZER("span_round_trip"), S8_INITIALIZER("single_round_trip"), S8_INITIALIZER("fmath"),
            S8_INITIALIZER("fcompare"), S8_INITIALIZER("fnan"), S8_INITIALIZER("call_stack"), S8_INITIALIZER("big_round"),
            S8_INITIALIZER("call_indirect"), S8_INITIALIZER("atomic_ops"), S8_INITIALIZER("kagg"), S8_INITIALIZER("vla_sum"),
            S8_INITIALIZER("variadic_named"), S8_INITIALIZER("variadic_named_caller"), S8_INITIALIZER("variadic_observe_caller"),
        };
        typedef s64 MachineTestModuleCall2(s64, s64);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(module_names) && none_module_executable.address && mir_module_executable.address;
             name_index += 1)
        {
            IrFunction* module_function = machine_test_ir_function_find(machine_module, module_names[name_index]);
            u32 none_offset = UINT32_MAX;
            u32 mir_offset = UINT32_MAX;
            for (u32 entry_index = 0; module_function && entry_index < none_module.entry_count; entry_index += 1)
            {
                if (none_module.entries[entry_index].symbol.value == module_function->symbol.value)
                {
                    none_offset = none_module.entries[entry_index].offset;
                }
            }
            for (u32 entry_index = 0; module_function && entry_index < mir_module.entry_count; entry_index += 1)
            {
                if (mir_module.entries[entry_index].symbol.value == module_function->symbol.value)
                {
                    mir_offset = mir_module.entries[entry_index].offset;
                }
            }
            BUSTER_TEST(arguments, none_offset != UINT32_MAX && mir_offset != UINT32_MAX);
            if (none_offset == UINT32_MAX || mir_offset == UINT32_MAX)
            {
                continue;
            }
            bool module_wide = string_equal(module_names[name_index], S8("widen")) || string_equal(module_names[name_index], S8("bitnot"));
            bool module_loop = string_equal(module_names[name_index], S8("sum_to"));
            s64 module_probes[][2] = {
                {5, 3}, {-7, 9}, {0, 1}, {2147483646, -2},
            };
            bool module_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(module_probes); probe_index += 1)
            {
                s64 left = module_probes[probe_index][0];
                s64 right = module_probes[probe_index][1];
                if (module_loop)
                {
                    left &= 63;
                }
                void* none_address = (u8*)none_module_executable.address + none_offset;
                void* mir_address = (u8*)mir_module_executable.address + mir_offset;
                MachineTestModuleCall2* none_call = 0;
                MachineTestModuleCall2* mir_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&mir_call, &mir_address, sizeof(mir_call));
                if (module_wide)
                {
                    module_equal &= none_call(left, right) == mir_call(left, right);
                }
                else
                {
                    module_equal &= (s32)none_call(left, right) == (s32)mir_call(left, right);
                }
            }
            BUSTER_TEST_RAW(arguments, module_equal, module_names[name_index]);
        }
        // Splitting executing differential: the QUALITY module carries the
        // split placement of split_phase — the entry installs, the span
        // itself, and the landing-pad stores — against the canonical
        // oracle, including a zero-round probe where the installs and
        // stores run but neither loop body does.
        CodegenModule quality_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                         (CodegenModuleOptions){
                                                                             .register_allocator = CODEGEN_REGISTER_ALLOCATOR_QUALITY,
                                                                         });
        BUSTER_TEST(arguments, quality_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, quality_module.statistics.exact_attempts >= 1);
        BUSTER_TEST(arguments, quality_module.statistics.exact_successes == quality_module.statistics.exact_attempts);
        BUSTER_TEST(arguments, quality_module.statistics.exact_failures == 0);
        for (u32 relocation_index = 0; relocation_index < quality_module.relocation_count; relocation_index += 1)
        {
            CodegenModuleRelocation* relocation = quality_module.relocations + relocation_index;
            BUSTER_TEST(arguments, codegen_module_relocation_valid(relocation));
            if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute)
            {
                continue;
            }
            for (u32 entry_index = 0; entry_index < quality_module.entry_count; entry_index += 1)
            {
                if (quality_module.entries[entry_index].symbol.value == relocation->symbol.value)
                {
                    u32 displacement = quality_module.entries[entry_index].offset - (relocation->offset + 4);
                    memcpy(quality_module.code.pointer + relocation->offset, &displacement, sizeof(displacement));
                    break;
                }
            }
        }
        CodegenExecutable quality_module_executable = codegen_make_executable((CodegenFunction){
            .code = quality_module.code,
        });
        BUSTER_TEST(arguments, quality_module_executable.error == CODEGEN_ERROR_NONE);
        u32 split_none_offset = machine_test_module_offset(&none_module, machine_module, S8("split_phase"));
        u32 split_quality_offset = machine_test_module_offset(&quality_module, machine_module, S8("split_phase"));
        BUSTER_TEST(arguments, split_none_offset != UINT32_MAX && split_quality_offset != UINT32_MAX);
        if (none_module_executable.address && quality_module_executable.address && split_none_offset != UINT32_MAX &&
            split_quality_offset != UINT32_MAX)
        {
            typedef u64 MachineTestSplitCall(u64, u64);
            void* split_none_address = (u8*)none_module_executable.address + split_none_offset;
            void* split_quality_address = (u8*)quality_module_executable.address + split_quality_offset;
            MachineTestSplitCall* split_none_call = 0;
            MachineTestSplitCall* split_quality_call = 0;
            memcpy(&split_none_call, &split_none_address, sizeof(split_none_call));
            memcpy(&split_quality_call, &split_quality_address, sizeof(split_quality_call));
            u64 split_probes[][2] = {
                {0x9e3779b97f4a7c15ull, 12}, {5, 7}, {0xffffffffffffffffull, 3}, {123456789, 0}, {0, 1},
            };
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(split_probes); probe_index += 1)
            {
                u64 none_result = split_none_call(split_probes[probe_index][0], split_probes[probe_index][1]);
                u64 quality_result = split_quality_call(split_probes[probe_index][0], split_probes[probe_index][1]);
                BUSTER_TEST_RAW(arguments, none_result == quality_result,
                                string_format(arguments->arena, S8("split_phase probe {u64} none {u64} quality {u64}"), split_probes[probe_index][1],
                                              none_result, quality_result));
            }
        }
        codegen_release_executable(quality_module_executable);
        // Float-signature shapes need typed callers: XMM scalars, mixed
        // integer/float argument sequences, all-float and mixed aggregates,
        // aggregate float returns, and machine-to-machine float calls.
        if (none_module_executable.address && mir_module_executable.address)
        {
            typedef double MachineTestCallD2(double, double);
            typedef double MachineTestCallDMix(int, double, long, double);
            typedef float MachineTestCallF2(float, float);
            typedef struct MachineTestDPair
            {
                double x;
                double y;
            } MachineTestDPair;
            typedef double MachineTestCallDPairSum(MachineTestDPair);
            typedef MachineTestDPair MachineTestCallDPairMake(double, double);
            typedef struct MachineTestTagged
            {
                s64 tag;
                double v;
            } MachineTestTagged;
            typedef double MachineTestCallTagged(MachineTestTagged);
            typedef int MachineTestCallI7(int, int, int, int, int, int, int);
            typedef s64 MachineTestCallStackMix(int, int, int, int, int, int, int, s64);
            typedef double MachineTestCallD9(double, double, double, double, double, double, double, double, double);
            String8 float_names[] = {
                S8_INITIALIZER("dadd"), S8_INITIALIZER("dmix"), S8_INITIALIZER("fhalf"), S8_INITIALIZER("dpair_sum"),
                S8_INITIALIZER("dpair_make"), S8_INITIALIZER("tagged_get"), S8_INITIALIZER("dcall"),
                S8_INITIALIZER("seven"), S8_INITIALIZER("stack_mix"), S8_INITIALIZER("nine"),
            };
            void* none_addresses[BUSTER_ARRAY_LENGTH(float_names)];
            void* mir_addresses[BUSTER_ARRAY_LENGTH(float_names)];
            bool float_offsets_found = true;
            for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(float_names); name_index += 1)
            {
                u32 none_offset = machine_test_module_offset(&none_module, machine_module, float_names[name_index]);
                u32 mir_offset = machine_test_module_offset(&mir_module, machine_module, float_names[name_index]);
                float_offsets_found &= none_offset != UINT32_MAX && mir_offset != UINT32_MAX;
                none_addresses[name_index] = none_offset != UINT32_MAX ? (u8*)none_module_executable.address + none_offset : 0;
                mir_addresses[name_index] = mir_offset != UINT32_MAX ? (u8*)mir_module_executable.address + mir_offset : 0;
            }
            BUSTER_TEST(arguments, float_offsets_found);
            if (float_offsets_found)
            {
                MachineTestCallD2* none_dadd;
                MachineTestCallD2* mir_dadd;
                MachineTestCallDMix* none_dmix;
                MachineTestCallDMix* mir_dmix;
                MachineTestCallF2* none_fhalf;
                MachineTestCallF2* mir_fhalf;
                MachineTestCallDPairSum* none_dpair_sum;
                MachineTestCallDPairSum* mir_dpair_sum;
                MachineTestCallDPairMake* none_dpair_make;
                MachineTestCallDPairMake* mir_dpair_make;
                MachineTestCallTagged* none_tagged;
                MachineTestCallTagged* mir_tagged;
                MachineTestCallD2* none_dcall;
                MachineTestCallD2* mir_dcall;
                memcpy(&none_dadd, none_addresses + 0, sizeof(none_dadd));
                memcpy(&mir_dadd, mir_addresses + 0, sizeof(mir_dadd));
                memcpy(&none_dmix, none_addresses + 1, sizeof(none_dmix));
                memcpy(&mir_dmix, mir_addresses + 1, sizeof(mir_dmix));
                memcpy(&none_fhalf, none_addresses + 2, sizeof(none_fhalf));
                memcpy(&mir_fhalf, mir_addresses + 2, sizeof(mir_fhalf));
                memcpy(&none_dpair_sum, none_addresses + 3, sizeof(none_dpair_sum));
                memcpy(&mir_dpair_sum, mir_addresses + 3, sizeof(mir_dpair_sum));
                memcpy(&none_dpair_make, none_addresses + 4, sizeof(none_dpair_make));
                memcpy(&mir_dpair_make, mir_addresses + 4, sizeof(mir_dpair_make));
                memcpy(&none_tagged, none_addresses + 5, sizeof(none_tagged));
                memcpy(&mir_tagged, mir_addresses + 5, sizeof(mir_tagged));
                memcpy(&none_dcall, none_addresses + 6, sizeof(none_dcall));
                memcpy(&mir_dcall, mir_addresses + 6, sizeof(mir_dcall));
                BUSTER_TEST(arguments, none_dadd(1.5, 2.25) == mir_dadd(1.5, 2.25));
                BUSTER_TEST(arguments, none_dadd(-0.125, 1e100) == mir_dadd(-0.125, 1e100));
                BUSTER_TEST(arguments, none_dmix(3, 1.5, -2, 0.25) == mir_dmix(3, 1.5, -2, 0.25));
                BUSTER_TEST(arguments, none_fhalf(7.5f, 2.5f) == mir_fhalf(7.5f, 2.5f));
                MachineTestDPair pair_probe = {3.5, -4.25};
                BUSTER_TEST(arguments, none_dpair_sum(pair_probe) == mir_dpair_sum(pair_probe));
                MachineTestDPair none_made = none_dpair_make(1.25, -8.5);
                MachineTestDPair mir_made = mir_dpair_make(1.25, -8.5);
                BUSTER_TEST(arguments, none_made.x == mir_made.x && none_made.y == mir_made.y);
                MachineTestTagged tagged_probe = {7, 9.5};
                MachineTestTagged tagged_zero = {0, 2.5};
                BUSTER_TEST(arguments, none_tagged(tagged_probe) == mir_tagged(tagged_probe));
                BUSTER_TEST(arguments, none_tagged(tagged_zero) == mir_tagged(tagged_zero));
                BUSTER_TEST(arguments, none_dcall(2.5, 4.0) == mir_dcall(2.5, 4.0));
                MachineTestCallI7* none_seven;
                MachineTestCallI7* mir_seven;
                MachineTestCallStackMix* none_stack_mix;
                MachineTestCallStackMix* mir_stack_mix;
                MachineTestCallD9* none_nine;
                MachineTestCallD9* mir_nine;
                memcpy(&none_seven, none_addresses + 7, sizeof(none_seven));
                memcpy(&mir_seven, mir_addresses + 7, sizeof(mir_seven));
                memcpy(&none_stack_mix, none_addresses + 8, sizeof(none_stack_mix));
                memcpy(&mir_stack_mix, mir_addresses + 8, sizeof(mir_stack_mix));
                memcpy(&none_nine, none_addresses + 9, sizeof(none_nine));
                memcpy(&mir_nine, mir_addresses + 9, sizeof(mir_nine));
                BUSTER_TEST(arguments, none_seven(1, 2, 3, 4, 5, 6, 70) == mir_seven(1, 2, 3, 4, 5, 6, 70));
                BUSTER_TEST(arguments, none_stack_mix(1, 2, 3, 4, 5, 6, 7, -11) == mir_stack_mix(1, 2, 3, 4, 5, 6, 7, -11));
                BUSTER_TEST(arguments,
                            none_nine(1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, -0.25) == mir_nine(1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, -0.25));
                typedef struct MachineTestBig
                {
                    s64 a;
                    s64 b;
                    s64 c;
                } MachineTestBig;
                typedef MachineTestBig MachineTestCallBigMake(s64);
                typedef s64 MachineTestCallBigSum(MachineTestBig);
                u32 none_big_make_offset = machine_test_module_offset(&none_module, machine_module, S8("big_make"));
                u32 mir_big_make_offset = machine_test_module_offset(&mir_module, machine_module, S8("big_make"));
                u32 none_big_sum_offset = machine_test_module_offset(&none_module, machine_module, S8("big_sum"));
                u32 mir_big_sum_offset = machine_test_module_offset(&mir_module, machine_module, S8("big_sum"));
                BUSTER_TEST(arguments, none_big_make_offset != UINT32_MAX && mir_big_make_offset != UINT32_MAX &&
                                           none_big_sum_offset != UINT32_MAX && mir_big_sum_offset != UINT32_MAX);
                if (none_big_make_offset != UINT32_MAX && mir_big_make_offset != UINT32_MAX && none_big_sum_offset != UINT32_MAX &&
                    mir_big_sum_offset != UINT32_MAX)
                {
                    MachineTestCallBigMake* none_big_make;
                    MachineTestCallBigMake* mir_big_make;
                    MachineTestCallBigSum* none_big_sum;
                    MachineTestCallBigSum* mir_big_sum;
                    void* none_big_make_address = (u8*)none_module_executable.address + none_big_make_offset;
                    void* mir_big_make_address = (u8*)mir_module_executable.address + mir_big_make_offset;
                    void* none_big_sum_address = (u8*)none_module_executable.address + none_big_sum_offset;
                    void* mir_big_sum_address = (u8*)mir_module_executable.address + mir_big_sum_offset;
                    memcpy(&none_big_make, &none_big_make_address, sizeof(none_big_make));
                    memcpy(&mir_big_make, &mir_big_make_address, sizeof(mir_big_make));
                    memcpy(&none_big_sum, &none_big_sum_address, sizeof(none_big_sum));
                    memcpy(&mir_big_sum, &mir_big_sum_address, sizeof(mir_big_sum));
                    MachineTestBig none_big = none_big_make(37);
                    MachineTestBig mir_big = mir_big_make(37);
                    BUSTER_TEST(arguments, none_big.a == mir_big.a && none_big.b == mir_big.b && none_big.c == mir_big.c);
                    BUSTER_TEST(arguments, none_big_sum(none_big) == mir_big_sum(mir_big));
                }
            }
        }
        // The fast allocator is a drop-in placement replacement: the same
        // module through FAST must behave identically to the oracle while
        // producing strictly less slot traffic than MIR_STACK.
        CodegenModule fast_module = codegen_generate_canonical_module(arguments->arena, machine_program, machine_module, machine_target,
                                                                      (CodegenModuleOptions){
                                                                          .register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST,
                                                                      });
        BUSTER_TEST(arguments, fast_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, fast_module.statistics.exact_attempts >= 1);
        BUSTER_TEST(arguments, fast_module.statistics.exact_successes == fast_module.statistics.exact_attempts);
        BUSTER_TEST(arguments, fast_module.statistics.exact_failures == 0);
        BUSTER_TEST(arguments, fast_module.statistics.fallback_function_count == mir_module.statistics.fallback_function_count);
        for (u32 relocation_index = 0; relocation_index < fast_module.relocation_count; relocation_index += 1)
        {
            CodegenModuleRelocation* relocation = fast_module.relocations + relocation_index;
            BUSTER_TEST(arguments, codegen_module_relocation_valid(relocation));
            if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute)
            {
                continue;
            }
            for (u32 entry_index = 0; entry_index < fast_module.entry_count; entry_index += 1)
            {
                if (fast_module.entries[entry_index].symbol.value == relocation->symbol.value)
                {
                    u32 displacement = fast_module.entries[entry_index].offset - (relocation->offset + 4);
                    memcpy(fast_module.code.pointer + relocation->offset, &displacement, sizeof(displacement));
                    break;
                }
            }
        }
        CodegenExecutable fast_module_executable = codegen_make_executable((CodegenFunction){
            .code = fast_module.code,
        });
        BUSTER_TEST(arguments, fast_module_executable.error == CODEGEN_ERROR_NONE);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(module_names) && none_module_executable.address && fast_module_executable.address;
             name_index += 1)
        {
            u32 none_offset = machine_test_module_offset(&none_module, machine_module, module_names[name_index]);
            u32 fast_offset = machine_test_module_offset(&fast_module, machine_module, module_names[name_index]);
            BUSTER_TEST(arguments, none_offset != UINT32_MAX && fast_offset != UINT32_MAX);
            if (none_offset == UINT32_MAX || fast_offset == UINT32_MAX)
            {
                continue;
            }
            bool fast_wide = string_equal(module_names[name_index], S8("widen")) || string_equal(module_names[name_index], S8("bitnot"));
            bool fast_loop = string_equal(module_names[name_index], S8("sum_to"));
            s64 fast_probes[][2] = {
                {6, 2}, {-9, 13}, {0, 1},
            };
            bool fast_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(fast_probes); probe_index += 1)
            {
                s64 left = fast_probes[probe_index][0];
                s64 right = fast_probes[probe_index][1];
                if (fast_loop)
                {
                    left &= 63;
                }
                void* none_address = (u8*)none_module_executable.address + none_offset;
                void* fast_address = (u8*)fast_module_executable.address + fast_offset;
                MachineTestModuleCall2* none_call = 0;
                MachineTestModuleCall2* fast_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&fast_call, &fast_address, sizeof(fast_call));
                if (fast_wide)
                {
                    fast_equal &= none_call(left, right) == fast_call(left, right);
                }
                else
                {
                    fast_equal &= (s32)none_call(left, right) == (s32)fast_call(left, right);
                }
            }
            BUSTER_TEST_RAW(arguments, fast_equal, module_names[name_index]);
        }
        // Six register arguments exercise the entry captures: an eager pick
        // must not clobber an incoming argument register before its own
        // capture reads it.
        if (none_module_executable.address && fast_module_executable.address)
        {
            u32 none_offset = machine_test_module_offset(&none_module, machine_module, S8("six"));
            u32 fast_offset = machine_test_module_offset(&fast_module, machine_module, S8("six"));
            BUSTER_TEST(arguments, none_offset != UINT32_MAX && fast_offset != UINT32_MAX);
            if (none_offset != UINT32_MAX && fast_offset != UINT32_MAX)
            {
                void* none_address = (u8*)none_module_executable.address + none_offset;
                void* fast_address = (u8*)fast_module_executable.address + fast_offset;
                MachineTestCall6* none_call6 = 0;
                MachineTestCall6* fast_call6 = 0;
                memcpy(&none_call6, &none_address, sizeof(none_call6));
                memcpy(&fast_call6, &fast_address, sizeof(fast_call6));
                s32 none_result = (s32)none_call6(1, 20, 300, 4000, 50000, 600000);
                s32 fast_result = (s32)fast_call6(1, 20, 300, 4000, 50000, 600000);
                BUSTER_TEST_RAW(arguments, none_result == fast_result,
                                string_format(arguments->arena, S8("fast six none={u64} fast={u64}"), (u64)(u32)none_result, (u64)(u32)fast_result));
            }
        }
        // The canonical emitter's own entry captures. An array-syntax
        // parameter puts its bound arithmetic between the parameter homes,
        // and that multiply takes RAX and RCX, so a NONE-emitted body that
        // homes RCX afterwards reads the multiplier instead of its fourth
        // argument. Both shapes are checked against the value the C says, not
        // only against each other: NONE is the differential oracle everywhere
        // else here, so a wrong oracle has to be caught absolutely.
        if (none_module_executable.address && fast_module_executable.address)
        {
            typedef s64 MachineTestArrayParamCall(unsigned char const*, s64, s64, s64);
            typedef s64 MachineTestVlaParamCall(s64, unsigned char const*, s64, s64);
            unsigned char entry_capture_slots[2] = {7, 8};
            u32 none_array_offset = machine_test_module_offset(&none_module, machine_module, S8("arr_param"));
            u32 fast_array_offset = machine_test_module_offset(&fast_module, machine_module, S8("arr_param"));
            u32 none_vla_offset = machine_test_module_offset(&none_module, machine_module, S8("vla_param"));
            u32 fast_vla_offset = machine_test_module_offset(&fast_module, machine_module, S8("vla_param"));
            BUSTER_TEST(arguments, none_array_offset != UINT32_MAX && fast_array_offset != UINT32_MAX && none_vla_offset != UINT32_MAX &&
                                       fast_vla_offset != UINT32_MAX);
            if (none_array_offset != UINT32_MAX && fast_array_offset != UINT32_MAX)
            {
                void* none_address = (u8*)none_module_executable.address + none_array_offset;
                void* fast_address = (u8*)fast_module_executable.address + fast_array_offset;
                MachineTestArrayParamCall* none_array_call = 0;
                MachineTestArrayParamCall* fast_array_call = 0;
                memcpy(&none_array_call, &none_address, sizeof(none_array_call));
                memcpy(&fast_array_call, &fast_address, sizeof(fast_array_call));
                s64 none_array_result = none_array_call(entry_capture_slots, 2, 3, 4);
                s64 fast_array_result = fast_array_call(entry_capture_slots, 2, 3, 4);
                BUSTER_TEST_RAW(arguments, none_array_result == 7234 && fast_array_result == 7234,
                                string_format(arguments->arena, S8("arr_param none={s64} fast={s64} want 7234"), none_array_result,
                                              fast_array_result));
            }
            if (none_vla_offset != UINT32_MAX && fast_vla_offset != UINT32_MAX)
            {
                void* none_address = (u8*)none_module_executable.address + none_vla_offset;
                void* fast_address = (u8*)fast_module_executable.address + fast_vla_offset;
                MachineTestVlaParamCall* none_vla_call = 0;
                MachineTestVlaParamCall* fast_vla_call = 0;
                memcpy(&none_vla_call, &none_address, sizeof(none_vla_call));
                memcpy(&fast_vla_call, &fast_address, sizeof(fast_vla_call));
                s64 none_vla_result = none_vla_call(2, entry_capture_slots, 3, 4);
                s64 fast_vla_result = fast_vla_call(2, entry_capture_slots, 3, 4);
                BUSTER_TEST_RAW(arguments, none_vla_result == 8234 && fast_vla_result == 8234,
                                string_format(arguments->arena, S8("vla_param none={s64} fast={s64} want 8234"), none_vla_result, fast_vla_result));
            }
        }
        codegen_release_executable(fast_module_executable);
        // The loop-heavy body must see strictly fewer reloads and spills
        // under the fast allocator than under the everything-in-slots mode.
        IrFunction* traffic_function = machine_test_ir_function_find(machine_module, S8("sum_to"));
        if (traffic_function)
        {
            MachineSelectResult traffic_selected = machine_select_canonical_function(arguments->arena, machine_program, traffic_function, machine_target);
            BUSTER_TEST(arguments, traffic_selected.supported);
            if (traffic_selected.supported)
            {
                MachineStackPlacement stack_placement = machine_stack_placement_build(arguments->arena, &traffic_selected.function);
                MachineStackPlacement fast_placement = machine_fast_placement_build(arguments->arena, &traffic_selected.function);
                BUSTER_TEST(arguments, stack_placement.valid && fast_placement.valid);
                BUSTER_TEST_RAW(arguments,
                                fast_placement.reload_count + fast_placement.spill_count <
                                    stack_placement.reload_count + stack_placement.spill_count,
                                string_format(arguments->arena, S8("fast traffic {u32}+{u32} vs stack {u32}+{u32}"), fast_placement.reload_count,
                                              fast_placement.spill_count, stack_placement.reload_count, stack_placement.spill_count));
                // Coalescing must survive: copies whose source dies land on
                // the source register and encode to nothing, so the fast
                // encoding stays strictly smaller than the stack one.
                // QUALITY must be at least as correct and never spill more
                // than the local scan it is built on.
                MachineStackPlacement quality_placement = machine_quality_placement_build(arguments->arena, &traffic_selected.function);
                BUSTER_TEST(arguments, quality_placement.valid);
                BUSTER_TEST_RAW(arguments,
                                quality_placement.reload_count + quality_placement.spill_count <=
                                    stack_placement.reload_count + stack_placement.spill_count,
                                string_format(arguments->arena, S8("quality traffic {u32}+{u32} vs stack {u32}+{u32}"), quality_placement.reload_count,
                                              quality_placement.spill_count, stack_placement.reload_count, stack_placement.spill_count));
                MachineEncodeResult stack_encoded = machine_encode_x86_64(arguments->arena, &traffic_selected.function, &stack_placement);
                MachineEncodeResult fast_encoded = machine_encode_x86_64(arguments->arena, &traffic_selected.function, &fast_placement);
                BUSTER_TEST(arguments, stack_encoded.valid && fast_encoded.valid);
                BUSTER_TEST_RAW(arguments, fast_encoded.byte_count < stack_encoded.byte_count,
                                string_format(arguments->arena, S8("fast bytes {u32} vs stack {u32}"), fast_encoded.byte_count, stack_encoded.byte_count));
                // Frequency classes, stamped on QUALITY's way into the pin
                // economics: sum_to's single while loop is one depth-1
                // region below a depth-0 entry.
                machine_function_stamp_frequency_classes(&traffic_selected.function);
                u16 traffic_maximum_class = 0;
                for (u32 block_index = 0; block_index < traffic_selected.function.block_count; block_index += 1)
                {
                    traffic_maximum_class = BUSTER_MAX(traffic_maximum_class, traffic_selected.function.blocks[block_index].frequency_class);
                }
                BUSTER_TEST(arguments, traffic_selected.function.blocks[0].frequency_class == 0);
                BUSTER_TEST_RAW(arguments, traffic_maximum_class == 1,
                                string_format(arguments->arena, S8("sum_to maximum frequency class {u64}"), (u64)traffic_maximum_class));
            }
        }
        // The nested pair reaches depth 2 — the classes count covering
        // backward-edge spans, not merely loop membership.
        IrFunction* nest_function = machine_test_ir_function_find(machine_module, S8("nest2"));
        BUSTER_TEST(arguments, nest_function != 0);
        if (nest_function)
        {
            MachineSelectResult nest_selected = machine_select_canonical_function(arguments->arena, machine_program, nest_function, machine_target);
            BUSTER_TEST(arguments, nest_selected.supported);
            if (nest_selected.supported)
            {
                machine_function_stamp_frequency_classes(&nest_selected.function);
                u16 nest_maximum_class = 0;
                for (u32 block_index = 0; block_index < nest_selected.function.block_count; block_index += 1)
                {
                    nest_maximum_class = BUSTER_MAX(nest_maximum_class, nest_selected.function.blocks[block_index].frequency_class);
                }
                BUSTER_TEST(arguments, nest_selected.function.blocks[0].frequency_class == 0);
                BUSTER_TEST_RAW(arguments, nest_maximum_class == 2,
                                string_format(arguments->arena, S8("nest2 maximum frequency class {u64}"), (u64)nest_maximum_class));
            }
        }
        codegen_release_executable(none_module_executable);
        codegen_release_executable(mir_module_executable);
#endif
    }

    // DIRECT exact-form smoke: MFENCE and INT3 have no visible operands, so
    // one tiny function exercises selection-independent placement and the
    // metadata-backed encoder path.  Keep this separate from the recipe
    // census above; the census owns registry counts, while this checks bytes.
    MachineFunction exact_barrier_function = machine_test_build_exact_barrier_function(arguments->arena);
    BUSTER_TEST(arguments, machine_verify_function(&exact_barrier_function).error == MACHINE_VERIFY_NONE);
    MachineStackPlacement exact_barrier_placement = machine_fast_placement_build(arguments->arena, &exact_barrier_function);
    BUSTER_TEST(arguments, exact_barrier_placement.valid);
    MachineEncodeResult exact_barrier_encoded = machine_encode_x86_64(arguments->arena, &exact_barrier_function, &exact_barrier_placement);
    u32 exact_barrier_mfence_count = 0;
    u32 exact_barrier_int3_count = 0;
    for (u32 byte_index = 0; exact_barrier_encoded.valid && byte_index < exact_barrier_encoded.byte_count; byte_index += 1)
    {
        if (byte_index + 3 <= exact_barrier_encoded.byte_count && exact_barrier_encoded.bytes[byte_index] == 0x0f &&
            exact_barrier_encoded.bytes[byte_index + 1] == 0xae && exact_barrier_encoded.bytes[byte_index + 2] == 0xf0)
        {
            exact_barrier_mfence_count += 1;
        }
        if (exact_barrier_encoded.bytes[byte_index] == 0xcc)
        {
            exact_barrier_int3_count += 1;
        }
    }
    BUSTER_TEST(arguments, exact_barrier_encoded.valid && exact_barrier_mfence_count == 1 && exact_barrier_int3_count == 1);
    BUSTER_TEST(arguments, exact_barrier_encoded.exact_attempts == 2);
    BUSTER_TEST(arguments, exact_barrier_encoded.exact_successes == 2);
    BUSTER_TEST(arguments, exact_barrier_encoded.exact_failures == 0);
    BUSTER_TEST(arguments, exact_barrier_encoded.exact_attempts ==
                                   exact_barrier_encoded.exact_successes + exact_barrier_encoded.exact_failures);

    MachineFunction exact_register_function = machine_test_build_exact_register_function(arguments->arena);
    BUSTER_TEST(arguments, machine_verify_function(&exact_register_function).error == MACHINE_VERIFY_NONE);
    MachineStackPlacement exact_register_placement = machine_stack_placement_build(arguments->arena, &exact_register_function);
    BUSTER_TEST(arguments, exact_register_placement.valid && exact_register_placement.edit_count == 0);
    MachineEncodeResult exact_register_encoded =
        machine_encode_x86_64(arguments->arena, &exact_register_function, &exact_register_placement);
    bool exact_register_offsets_monotonic = exact_register_encoded.valid && exact_register_encoded.row_offsets[0] == 4;
    for (u32 row_index = 1; row_index < exact_register_function.instruction_count && exact_register_offsets_monotonic; row_index += 1)
    {
        exact_register_offsets_monotonic &= exact_register_encoded.row_offsets[row_index] > exact_register_encoded.row_offsets[row_index - 1];
    }
    // Check asymmetric low/extended encodings at representative widths and
    // operand orientations. These bytes are the old switch's canonical
    // spellings, so this catches a projection that happens to succeed in
    // metadata but reverses RM/REG or drops a REX bit.
    bool exact_register_asymmetric_bytes = exact_register_encoded.valid;
    bool exact_register_mov64_bytes = false;
    bool exact_register_add32_bytes = false;
    bool exact_register_add64_bytes = false;
    bool exact_register_bsf32_bytes = false;
    bool exact_register_bsf64_bytes = false;
    bool exact_register_test64_bytes = false;
    if (exact_register_asymmetric_bytes)
    {
        u32 mov64 = exact_register_encoded.row_offsets[0];
        u32 add32 = exact_register_encoded.row_offsets[7];
        u32 add64 = exact_register_encoded.row_offsets[8];
        u32 bsf32 = exact_register_encoded.row_offsets[23];
        u32 bsf64 = exact_register_encoded.row_offsets[24];
        u32 test64 = exact_register_encoded.row_offsets[29];
        exact_register_mov64_bytes = exact_register_encoded.bytes[mov64 + 0] == 0x4c && exact_register_encoded.bytes[mov64 + 1] == 0x89 &&
                                     exact_register_encoded.bytes[mov64 + 2] == 0xc8;
        exact_register_add32_bytes = exact_register_encoded.bytes[add32 + 0] == 0x44 && exact_register_encoded.bytes[add32 + 1] == 0x01 &&
                                     exact_register_encoded.bytes[add32 + 2] == 0xc8;
        exact_register_add64_bytes = exact_register_encoded.bytes[add64 + 0] == 0x49 && exact_register_encoded.bytes[add64 + 1] == 0x01 &&
                                     exact_register_encoded.bytes[add64 + 2] == 0xc1;
        exact_register_bsf32_bytes = exact_register_encoded.bytes[bsf32 + 0] == 0x41 && exact_register_encoded.bytes[bsf32 + 1] == 0x0f &&
                                     exact_register_encoded.bytes[bsf32 + 2] == 0xbc && exact_register_encoded.bytes[bsf32 + 3] == 0xc1;
        exact_register_bsf64_bytes = exact_register_encoded.bytes[bsf64 + 0] == 0x4c && exact_register_encoded.bytes[bsf64 + 1] == 0x0f &&
                                     exact_register_encoded.bytes[bsf64 + 2] == 0xbc && exact_register_encoded.bytes[bsf64 + 3] == 0xc8;
        exact_register_test64_bytes = exact_register_encoded.bytes[test64 + 0] == 0x4c && exact_register_encoded.bytes[test64 + 1] == 0x85 &&
                                      exact_register_encoded.bytes[test64 + 2] == 0xc8;
        exact_register_asymmetric_bytes = exact_register_mov64_bytes && exact_register_add32_bytes && exact_register_add64_bytes &&
                                          exact_register_bsf32_bytes && exact_register_bsf64_bytes && exact_register_test64_bytes;
    }
    BUSTER_TEST(arguments, exact_register_encoded.valid && exact_register_offsets_monotonic);
    BUSTER_TEST(arguments, exact_register_mov64_bytes);
    BUSTER_TEST(arguments, exact_register_add32_bytes);
    BUSTER_TEST(arguments, exact_register_add64_bytes);
    BUSTER_TEST(arguments, exact_register_bsf32_bytes);
    BUSTER_TEST(arguments, exact_register_bsf64_bytes);
    BUSTER_TEST(arguments, exact_register_test64_bytes);
    BUSTER_TEST(arguments, exact_register_asymmetric_bytes);
    BUSTER_TEST(arguments, exact_register_encoded.exact_attempts == 30);
    BUSTER_TEST(arguments, exact_register_encoded.exact_successes == 30);
    BUSTER_TEST(arguments, exact_register_encoded.exact_failures == 0);
    BUSTER_TEST(arguments, exact_register_encoded.exact_attempts ==
                                   exact_register_encoded.exact_successes + exact_register_encoded.exact_failures);

    MachineFunction exact_second_function = machine_test_build_exact_second_register_function(arguments->arena);
    BUSTER_TEST(arguments, machine_verify_function(&exact_second_function).error == MACHINE_VERIFY_NONE);
    MachineStackPlacement exact_second_placement = machine_stack_placement_build(arguments->arena, &exact_second_function);
    BUSTER_TEST(arguments, exact_second_placement.valid && exact_second_placement.edit_count == 0);
    MachineEncodeResult exact_second_encoded = machine_encode_x86_64(arguments->arena, &exact_second_function, &exact_second_placement);
    bool exact_second_offsets_monotonic = exact_second_encoded.valid && exact_second_encoded.row_offsets[0] == 4;
    for (u32 row_index = 1; row_index < exact_second_function.instruction_count && exact_second_offsets_monotonic; row_index += 1)
    {
        exact_second_offsets_monotonic &= exact_second_encoded.row_offsets[row_index] > exact_second_encoded.row_offsets[row_index - 1];
    }
    bool exact_second_bytes = exact_second_encoded.valid;
    if (exact_second_bytes)
    {
        u32 popcnt32 = exact_second_encoded.row_offsets[0];
        u32 popcnt64 = exact_second_encoded.row_offsets[1];
        u32 shl32 = exact_second_encoded.row_offsets[2];
        u32 shl64 = exact_second_encoded.row_offsets[3];
        u32 movq_to = exact_second_encoded.row_offsets[8];
        u32 movq_from = exact_second_encoded.row_offsets[9];
        u32 push = exact_second_encoded.row_offsets[10];
        u32 add_rsp = exact_second_encoded.row_offsets[11];
        exact_second_bytes &= exact_second_encoded.bytes[popcnt32 + 0] == 0xf3 && exact_second_encoded.bytes[popcnt32 + 1] == 0x41 &&
                              exact_second_encoded.bytes[popcnt32 + 2] == 0x0f && exact_second_encoded.bytes[popcnt32 + 3] == 0xb8 &&
                              exact_second_encoded.bytes[popcnt32 + 4] == 0xc1;
        exact_second_bytes &= exact_second_encoded.bytes[popcnt64 + 0] == 0xf3 && exact_second_encoded.bytes[popcnt64 + 1] == 0x4c &&
                              exact_second_encoded.bytes[popcnt64 + 2] == 0x0f && exact_second_encoded.bytes[popcnt64 + 3] == 0xb8 &&
                              exact_second_encoded.bytes[popcnt64 + 4] == 0xc8;
        exact_second_bytes &= exact_second_encoded.bytes[shl32 + 0] == 0xd3 && exact_second_encoded.bytes[shl32 + 1] == 0xe0;
        exact_second_bytes &= exact_second_encoded.bytes[shl64 + 0] == 0x48 && exact_second_encoded.bytes[shl64 + 1] == 0xd3 &&
                              exact_second_encoded.bytes[shl64 + 2] == 0xe0;
        exact_second_bytes &= exact_second_encoded.bytes[movq_to + 0] == 0x66 && exact_second_encoded.bytes[movq_to + 1] == 0x49 &&
                              exact_second_encoded.bytes[movq_to + 2] == 0x0f && exact_second_encoded.bytes[movq_to + 3] == 0x6e &&
                              exact_second_encoded.bytes[movq_to + 4] == 0xc1;
        exact_second_bytes &= exact_second_encoded.bytes[movq_from + 0] == 0x66 && exact_second_encoded.bytes[movq_from + 1] == 0x48 &&
                              exact_second_encoded.bytes[movq_from + 2] == 0x0f && exact_second_encoded.bytes[movq_from + 3] == 0x7e &&
                              exact_second_encoded.bytes[movq_from + 4] == 0xc8;
        exact_second_bytes &= exact_second_encoded.bytes[push + 0] == 0x41 && exact_second_encoded.bytes[push + 1] == 0x51;
        exact_second_bytes &= exact_second_encoded.bytes[add_rsp + 0] == 0x48 && exact_second_encoded.bytes[add_rsp + 1] == 0x81 &&
                              exact_second_encoded.bytes[add_rsp + 2] == 0xc4 && exact_second_encoded.bytes[add_rsp + 3] == 0x10 &&
                              exact_second_encoded.bytes[add_rsp + 4] == 0x00 && exact_second_encoded.bytes[add_rsp + 5] == 0x00 &&
                              exact_second_encoded.bytes[add_rsp + 6] == 0x00;
    }
    BUSTER_TEST(arguments, exact_second_encoded.valid && exact_second_offsets_monotonic);
    BUSTER_TEST(arguments, exact_second_bytes);
    BUSTER_TEST(arguments, exact_second_encoded.exact_attempts == 12);
    BUSTER_TEST(arguments, exact_second_encoded.exact_successes == 12);
    BUSTER_TEST(arguments, exact_second_encoded.exact_failures == 0);
    BUSTER_TEST(arguments, exact_second_encoded.exact_attempts ==
                                   exact_second_encoded.exact_successes + exact_second_encoded.exact_failures);

    MachineFunction exact_load_incoming_function = machine_test_build_exact_load_incoming_function(arguments->arena);
    BUSTER_TEST(arguments, machine_verify_function(&exact_load_incoming_function).error == MACHINE_VERIFY_NONE);
    MachineStackPlacement exact_load_incoming_placement =
        machine_stack_placement_build(arguments->arena, &exact_load_incoming_function);
    BUSTER_TEST(arguments, exact_load_incoming_placement.valid && exact_load_incoming_placement.edit_count == 0);
    MachineEncodeResult exact_load_incoming_encoded =
        machine_encode_x86_64(arguments->arena, &exact_load_incoming_function, &exact_load_incoming_placement);
    bool exact_load_incoming_bytes = exact_load_incoming_encoded.valid;
    if (exact_load_incoming_bytes)
    {
        u32 rax_load = exact_load_incoming_encoded.row_offsets[0];
        u32 r9_load = exact_load_incoming_encoded.row_offsets[1];
        u32 r10_load = exact_load_incoming_encoded.row_offsets[2];
        // All three rows retain the old seven-byte [RBP+disp32] spelling,
        // including the offsets that metadata would otherwise relax to
        // disp8.  The final two rows also verify REX.R for extended regs.
        exact_load_incoming_bytes &= exact_load_incoming_encoded.bytes[rax_load + 0] == 0x48 &&
                                     exact_load_incoming_encoded.bytes[rax_load + 1] == 0x8b &&
                                     exact_load_incoming_encoded.bytes[rax_load + 2] == 0x85 &&
                                     exact_load_incoming_encoded.bytes[rax_load + 3] == 0x10 &&
                                     exact_load_incoming_encoded.bytes[rax_load + 4] == 0x00 &&
                                     exact_load_incoming_encoded.bytes[rax_load + 5] == 0x00 &&
                                     exact_load_incoming_encoded.bytes[rax_load + 6] == 0x00;
        exact_load_incoming_bytes &= exact_load_incoming_encoded.bytes[r9_load + 0] == 0x4c &&
                                     exact_load_incoming_encoded.bytes[r9_load + 1] == 0x8b &&
                                     exact_load_incoming_encoded.bytes[r9_load + 2] == 0x8d &&
                                     exact_load_incoming_encoded.bytes[r9_load + 3] == 0x80 &&
                                     exact_load_incoming_encoded.bytes[r9_load + 4] == 0x00 &&
                                     exact_load_incoming_encoded.bytes[r9_load + 5] == 0x00 &&
                                     exact_load_incoming_encoded.bytes[r9_load + 6] == 0x00;
        exact_load_incoming_bytes &= exact_load_incoming_encoded.bytes[r10_load + 0] == 0x4c &&
                                     exact_load_incoming_encoded.bytes[r10_load + 1] == 0x8b &&
                                     exact_load_incoming_encoded.bytes[r10_load + 2] == 0x95 &&
                                     exact_load_incoming_encoded.bytes[r10_load + 3] == 0x90 &&
                                     exact_load_incoming_encoded.bytes[r10_load + 4] == 0x00 &&
                                     exact_load_incoming_encoded.bytes[r10_load + 5] == 0x00 &&
                                     exact_load_incoming_encoded.bytes[r10_load + 6] == 0x00;
    }
    BUSTER_TEST(arguments, exact_load_incoming_encoded.valid && exact_load_incoming_bytes);
    BUSTER_TEST(arguments, exact_load_incoming_encoded.exact_attempts == 3);
    BUSTER_TEST(arguments, exact_load_incoming_encoded.exact_successes == 3);
    BUSTER_TEST(arguments, exact_load_incoming_encoded.exact_failures == 0);
    BUSTER_TEST(arguments, exact_load_incoming_encoded.exact_attempts ==
                                   exact_load_incoming_encoded.exact_successes + exact_load_incoming_encoded.exact_failures);

    MachineFunction exact_relative_function = machine_test_build_exact_relative_function(arguments->arena);
    BUSTER_TEST(arguments, machine_verify_function(&exact_relative_function).error == MACHINE_VERIFY_NONE);
    MachineStackPlacement exact_relative_placement = machine_stack_placement_build(arguments->arena, &exact_relative_function);
    BUSTER_TEST(arguments, exact_relative_placement.valid && exact_relative_placement.edit_count == 0);
    MachineEncodeResult exact_relative_encoded =
        machine_encode_x86_64(arguments->arena, &exact_relative_function, &exact_relative_placement);
    bool exact_relative_bytes = exact_relative_encoded.valid && exact_relative_encoded.byte_count >= 29;
    bool exact_relative_sites = exact_relative_encoded.valid && exact_relative_encoded.call_site_count == 2;
    if (exact_relative_bytes)
    {
        u32 forward_jmp = exact_relative_encoded.row_offsets[0];
        u32 extended_lea = exact_relative_encoded.row_offsets[1];
        u32 low_lea = exact_relative_encoded.row_offsets[3];
        u32 backward_jmp = exact_relative_encoded.row_offsets[4];
        // The branch fields are patched from the neutral E9+rel32 bytes;
        // derive both displacements from the final block offsets so the test
        // remains valid if RET's epilogue policy changes.
        u32 forward_displacement = exact_relative_encoded.block_offsets[2] - (forward_jmp + 5);
        u32 backward_displacement = exact_relative_encoded.block_offsets[1] - (backward_jmp + 5);
        exact_relative_bytes &= exact_relative_encoded.bytes[forward_jmp + 0] == 0xe9 &&
                               memcmp(exact_relative_encoded.bytes + forward_jmp + 1, &forward_displacement, sizeof(forward_displacement)) == 0;
        exact_relative_bytes &= exact_relative_encoded.bytes[extended_lea + 0] == 0x4c &&
                               exact_relative_encoded.bytes[extended_lea + 1] == 0x8d &&
                               exact_relative_encoded.bytes[extended_lea + 2] == 0x05 &&
                               exact_relative_encoded.bytes[extended_lea + 3] == 0x00 &&
                               exact_relative_encoded.bytes[extended_lea + 4] == 0x00 &&
                               exact_relative_encoded.bytes[extended_lea + 5] == 0x00 &&
                               exact_relative_encoded.bytes[extended_lea + 6] == 0x00;
        exact_relative_bytes &= exact_relative_encoded.bytes[low_lea + 0] == 0x48 &&
                               exact_relative_encoded.bytes[low_lea + 1] == 0x8d &&
                               exact_relative_encoded.bytes[low_lea + 2] == 0x05 &&
                               exact_relative_encoded.bytes[low_lea + 3] == 0x00 &&
                               exact_relative_encoded.bytes[low_lea + 4] == 0x00 &&
                               exact_relative_encoded.bytes[low_lea + 5] == 0x00 &&
                               exact_relative_encoded.bytes[low_lea + 6] == 0x00;
        exact_relative_bytes &= exact_relative_encoded.bytes[backward_jmp + 0] == 0xe9 &&
                               memcmp(exact_relative_encoded.bytes + backward_jmp + 1, &backward_displacement, sizeof(backward_displacement)) == 0;
    }
    if (exact_relative_sites)
    {
        exact_relative_sites &= exact_relative_encoded.call_sites[0].code_offset == exact_relative_encoded.row_offsets[1] + 3 &&
                               exact_relative_encoded.call_sites[0].target == 1 &&
                               exact_relative_encoded.call_sites[1].code_offset == exact_relative_encoded.row_offsets[3] + 3 &&
                               exact_relative_encoded.call_sites[1].target == 0;
    }
    BUSTER_TEST(arguments, exact_relative_encoded.valid && exact_relative_bytes);
    BUSTER_TEST(arguments, exact_relative_sites);
    BUSTER_TEST(arguments, exact_relative_encoded.exact_attempts == 4);
    BUSTER_TEST(arguments, exact_relative_encoded.exact_successes == 4);
    BUSTER_TEST(arguments, exact_relative_encoded.exact_failures == 0);
    BUSTER_TEST(arguments, exact_relative_encoded.exact_attempts ==
                                   exact_relative_encoded.exact_successes + exact_relative_encoded.exact_failures);

    // A vector load that the target cannot select must reject the function at
    // that load and let module generation use the canonical path. Keeping the
    // signature scalar makes the unsupported instruction internal to the
    // function instead of letting the vector ABI reject it first.
    Target machine_vector_fallback_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_BASELINE,
        .os = OPERATING_SYSTEM_LINUX,
    };
    String8 machine_vector_fallback_source =
        S8("typedef unsigned char V64 __attribute__((vector_size(64)));\n"
           "unsigned char load_first(const V64* value) {\n"
           "    V64 loaded = *value;\n"
           "    return ((const unsigned char*)&loaded)[0];\n"
           "}\n");
    IrProgram* machine_vector_fallback_program =
        machine_test_compile_c(arguments->arena, S8("machine-vector-load-fallback.c"), machine_vector_fallback_source,
                               machine_vector_fallback_target);
    BUSTER_TEST(arguments, machine_vector_fallback_program != 0);
    if (machine_vector_fallback_program && machine_vector_fallback_program->module_count)
    {
        IrModule* machine_vector_fallback_ir_module = machine_vector_fallback_program->modules;
        IrFunction* machine_vector_fallback_function =
            machine_test_ir_function_find(machine_vector_fallback_ir_module, S8("load_first"));
        BUSTER_TEST(arguments, machine_vector_fallback_function != 0);
        if (machine_vector_fallback_function)
        {
            MachineSelectResult machine_vector_fallback_selected = machine_select_canonical_function(
                arguments->arena, machine_vector_fallback_program, machine_vector_fallback_function, machine_vector_fallback_target);
            BUSTER_TEST(arguments, !machine_vector_fallback_selected.supported);
            BUSTER_TEST(arguments, machine_vector_fallback_selected.failed_opcode == IR_OPCODE_LOAD);
        }
        CodegenModule machine_vector_fallback_module = codegen_generate_canonical_module(
            arguments->arena, machine_vector_fallback_program, machine_vector_fallback_ir_module, machine_vector_fallback_target,
            (CodegenModuleOptions){
                .register_allocator = CODEGEN_REGISTER_ALLOCATOR_FAST,
            });
        BUSTER_TEST(arguments, machine_vector_fallback_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, machine_vector_fallback_module.statistics.fallback_function_count == 1);
        BUSTER_TEST(arguments, machine_vector_fallback_module.statistics.fallback_opcode_counts[IR_OPCODE_LOAD] == 1);
    }

    // Stage 10: the 512-bit vector subset. The corpus fixes a znver5 Linux
    // target, which carries every feature the vocabulary needs, so
    // selection, verification, placement, and encoding run on every host;
    // the executing differential additionally requires the host's own
    // cpuid to carry the features, since the canonical NONE oracle emits
    // the same AVX-512 instructions the machine rows do.
    Target machine_vector_target = {
        .cpu_arch = CPU_ARCH_X86_64,
        .cpu_model = CPU_MODEL_AMD_ZEN_5,
        .os = OPERATING_SYSTEM_LINUX,
    };
    String8 machine_vector_source_head =
        S8("typedef unsigned char u8;\n"
           "typedef unsigned long long u64;\n"
           "typedef u8 V64 __attribute__((vector_size(64)));\n"
           "u64 vclassify(const u8* data, u64 valid) {\n"
           "    V64 chunk = __builtin_buster_simd_load_masked(data, valid);\n"
           "    V64 lower = chunk | __builtin_buster_simd_splat_byte(0x20);\n"
           "    V64 shifted = lower - __builtin_buster_simd_splat_byte(97);\n"
           "    u64 alpha = __builtin_buster_simd_less_byte(shifted, __builtin_buster_simd_splat_byte(26));\n"
           "    u64 under = __builtin_buster_simd_equal_byte(chunk, __builtin_buster_simd_splat_byte(95));\n"
           "    u64 high = __builtin_buster_simd_sign_byte(chunk);\n"
           "    u64 bits = __builtin_buster_simd_test_byte(chunk, __builtin_buster_simd_splat_byte(0x40));\n"
           "    return (alpha | under) ^ (high * 3) ^ bits ^ (u64)__builtin_popcountll(alpha);\n"
           "}\n"
           "u64 vtable(const u8* low, const u8* high, const u8* indices, u64 mask) {\n"
           "    V64 low_v = __builtin_buster_simd_load(low);\n"
           "    V64 high_v = __builtin_buster_simd_load(high);\n"
           "    V64 index_v = __builtin_buster_simd_load(indices);\n"
           "    V64 permuted = __builtin_buster_simd_permute2_byte(mask, low_v, index_v, high_v);\n"
           "    V64 packed = __builtin_buster_simd_compress_byte(mask, permuted);\n"
           "    V64 wide = __builtin_buster_simd_widen_byte(packed, 1);\n"
           "    V64 shifted = __builtin_buster_simd_shift_left_word(wide, 5);\n"
           "    V64 mixed = __builtin_buster_simd_ternary_word(packed, shifted, wide, 0xd8);\n"
           "    return __builtin_buster_simd_sign_byte(mixed) ^ __builtin_buster_simd_test_byte(shifted, __builtin_buster_simd_splat_byte(0x80)) ^\n"
           "           __builtin_buster_simd_equal_byte(packed, __builtin_buster_simd_splat_byte(0));\n"
           "}\n"
           "void vstores(u8* out, const u8* in, u64 mask) {\n"
           "    V64 first = __builtin_buster_simd_load(in);\n"
           "    __builtin_buster_simd_store(out, first + __builtin_buster_simd_splat_byte(1));\n"
           "    __builtin_buster_simd_store_masked(out + 64, mask, first ^ __builtin_buster_simd_load(in + 64));\n"
           "    __builtin_buster_simd_compress_store_byte(out + 128, mask, first);\n"
           "}\n");
    String8 machine_vector_source_tail =
        S8("u64 vspill(const u8* data, u64 rounds) {\n"
           "    V64 a0 = __builtin_buster_simd_load(data + 64 * 0);\n"
           "    V64 a1 = __builtin_buster_simd_load(data + 64 * 1);\n"
           "    V64 a2 = __builtin_buster_simd_load(data + 64 * 2);\n"
           "    V64 a3 = __builtin_buster_simd_load(data + 64 * 3);\n"
           "    V64 a4 = __builtin_buster_simd_load(data + 64 * 4);\n"
           "    V64 a5 = __builtin_buster_simd_load(data + 64 * 5);\n"
           "    V64 a6 = __builtin_buster_simd_load(data + 64 * 6);\n"
           "    V64 a7 = __builtin_buster_simd_load(data + 64 * 7);\n"
           "    V64 a8 = __builtin_buster_simd_load(data + 64 * 8);\n"
           "    V64 a9 = __builtin_buster_simd_load(data + 64 * 9);\n"
           "    V64 a10 = __builtin_buster_simd_load(data + 64 * 10);\n"
           "    V64 a11 = __builtin_buster_simd_load(data + 64 * 11);\n"
           "    V64 a12 = __builtin_buster_simd_load(data + 64 * 12);\n"
           "    V64 a13 = __builtin_buster_simd_load(data + 64 * 13);\n"
           "    V64 a14 = __builtin_buster_simd_load(data + 64 * 14);\n"
           "    V64 a15 = __builtin_buster_simd_load(data + 64 * 15);\n"
           "    V64 a16 = __builtin_buster_simd_load(data + 64 * 16);\n"
           "    V64 a17 = __builtin_buster_simd_load(data + 64 * 17);\n"
           "    for (u64 round = 0; round < rounds; round += 1) {\n"
           "        a0 = a0 + (a1 ^ a17);\n"
           "        a1 = a1 + (a2 ^ a0);\n"
           "        a2 = a2 + (a3 ^ a1);\n"
           "        a3 = a3 + (a4 ^ a2);\n"
           "        a4 = a4 + (a5 ^ a3);\n"
           "        a5 = a5 + (a6 ^ a4);\n"
           "        a6 = a6 + (a7 ^ a5);\n"
           "        a7 = a7 + (a8 ^ a6);\n"
           "        a8 = a8 + (a9 ^ a7);\n"
           "        a9 = a9 + (a10 ^ a8);\n"
           "        a10 = a10 + (a11 ^ a9);\n"
           "        a11 = a11 + (a12 ^ a10);\n"
           "        a12 = a12 + (a13 ^ a11);\n"
           "        a13 = a13 + (a14 ^ a12);\n"
           "        a14 = a14 + (a15 ^ a13);\n"
           "        a15 = a15 + (a16 ^ a14);\n"
           "        a16 = a16 + (a17 ^ a15);\n"
           "        a17 = a17 + (a0 ^ a16);\n"
           "    }\n"
           "    V64 combined = a0 ^ a1 ^ a2 ^ a3 ^ a4 ^ a5 ^ a6 ^ a7 ^ a8 ^ a9 ^ a10 ^ a11 ^ a12 ^ a13 ^ a14 ^ a15 ^ a16 ^ a17;\n"
           "    return __builtin_buster_simd_sign_byte(combined) ^ __builtin_buster_simd_test_byte(a0 & a9, __builtin_buster_simd_splat_byte(0x11));\n"
           "}\n"
           "u64 vhelp(u64 x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdUL; return x ^ (x >> 29); }\n"
           "u64 vcalls(const u8* data, u64 rounds) {\n"
           "    V64 b0 = __builtin_buster_simd_load(data + 64 * 0);\n"
           "    V64 b1 = __builtin_buster_simd_load(data + 64 * 1);\n"
           "    V64 b2 = __builtin_buster_simd_load(data + 64 * 2);\n"
           "    V64 b3 = __builtin_buster_simd_load(data + 64 * 3);\n"
           "    u64 total = 0;\n"
           "    for (u64 round = 0; round < rounds; round += 1) {\n"
           "        total += vhelp(total ^ round);\n"
           "        V64 salt = __builtin_buster_simd_splat_byte((u8)total);\n"
           "        b0 = b0 + (salt ^ b3);\n"
           "        b1 = b1 + (salt ^ b0);\n"
           "        b2 = b2 + (salt ^ b1);\n"
           "        b3 = b3 + (salt ^ b2);\n"
           "    }\n"
           "    return total ^ __builtin_buster_simd_sign_byte(b0 ^ b1 ^ b2 ^ b3);\n"
           "}\n"
           // 64-bit lanes: vpaddq/vpsubq are the vocabulary's only EVEX.W1
           // rows, and their W0 encodings #UD on real hardware, so this body
           // is the differential that keeps both the machine wide bit and
           // the canonical oracle's W1 forms honest.
           "typedef u64 Q8 __attribute__((vector_size(64)));\n"
           "u64 vqarith(const u8* data, u64 rounds) {\n"
           "    const Q8* row = (const Q8*)(const void*)data;\n"
           "    Q8 accumulator = row[0];\n"
           "    Q8 step = row[1];\n"
           "    Q8 bias = row[2];\n"
           "    for (u64 round = 0; round < rounds; round += 1) {\n"
           "        accumulator = (accumulator + step) - bias;\n"
           "        accumulator = accumulator + (step ^ bias);\n"
           "        accumulator = accumulator - (step & bias);\n"
           "    }\n"
           "    u64 out[8];\n"
           "    *(Q8*)(void*)out = accumulator;\n"
           "    return out[0] ^ out[1] ^ out[2] ^ out[3] ^ out[4] ^ out[5] ^ out[6] ^ out[7];\n"
           "}\n"
           // A union with a vector member is a 64-byte-aligned local: the
           // machine path mirrors the canonical over-aligned frame shape
           // (padded slot, runtime-aligned pointer), and the returned
           // low address bits assert the alignment actually holds.
           "typedef union Lanes { V64 vector; u8 bytes[64]; u64 words[8]; } Lanes;\n"
           "u64 vunion(const u8* data, u64 salt) {\n"
           "    Lanes lanes;\n"
           "    lanes.vector = __builtin_buster_simd_load(data);\n"
           "    lanes.words[0] += salt;\n"
           "    lanes.bytes[63] = (u8)salt;\n"
           "    u64 misalignment = (u64)&lanes & 63;\n"
           "    return lanes.words[0] ^ lanes.words[7] ^ __builtin_buster_simd_sign_byte(lanes.vector) ^ misalignment;\n"
           "}\n");
    // The System V vector ABI: 64-byte values crossing call boundaries in
    // ZMM registers — parameters, returns, the mixed integer/vector
    // signature, and the ninth argument that overflows to the caller's
    // 64-aligned stack eightbytes. vabi drives them all from one
    // u64-returning wrapper so the differential below can execute it
    // through the scalar calling shape it already uses.
    String8 machine_vector_source_abi =
        S8("V64 vident(V64 v) { return v; }\n"
           "V64 vmix(u64 salt, V64 a, u64 salt2, V64 b) {\n"
           "    return a + (b ^ __builtin_buster_simd_splat_byte((u8)(salt + salt2)));\n"
           "}\n"
           "V64 vninth(V64 a, V64 b, V64 c, V64 d, V64 e, V64 f, V64 g, V64 h, V64 i) {\n"
           "    return a + (h ^ i);\n"
           "}\n"
           "u64 vabi(const u8* data, u64 mask) {\n"
           "    V64 first = __builtin_buster_simd_load(data);\n"
           "    V64 second = __builtin_buster_simd_load(data + 64);\n"
           "    V64 ident = vident(first);\n"
           "    V64 mixed = vmix(mask, ident, mask >> 7, second);\n"
           "    V64 nine = vninth(first, second, ident, mixed, first, second, ident, mixed, first ^ second);\n"
           "    return __builtin_buster_simd_sign_byte(nine) ^\n"
           "           __builtin_buster_simd_test_byte(mixed, __builtin_buster_simd_splat_byte(0x21)) ^\n"
           "           __builtin_buster_simd_equal_byte(vident(nine), nine);\n"
           "}\n");
    String8 machine_vector_source =
        string_format(arguments->arena, S8("{S8}{S8}{S8}"), machine_vector_source_head, machine_vector_source_tail, machine_vector_source_abi);
    IrProgram* machine_vector_program = machine_test_compile_c(arguments->arena, S8("machine-stage10.c"), machine_vector_source, machine_vector_target);
    BUSTER_TEST(arguments, machine_vector_program != 0);
    if (machine_vector_program && machine_vector_program->module_count)
    {
        IrModule* machine_vector_module = machine_vector_program->modules;
        String8 vector_names[] = {
            S8_INITIALIZER("vclassify"), S8_INITIALIZER("vtable"), S8_INITIALIZER("vstores"),
            S8_INITIALIZER("vspill"),    S8_INITIALIZER("vcalls"), S8_INITIALIZER("vhelp"),
            S8_INITIALIZER("vident"),    S8_INITIALIZER("vmix"),   S8_INITIALIZER("vninth"),
            S8_INITIALIZER("vabi"),      S8_INITIALIZER("vqarith"), S8_INITIALIZER("vunion"),
        };
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(vector_names); name_index += 1)
        {
            IrFunction* ir_function = machine_test_ir_function_find(machine_vector_module, vector_names[name_index]);
            BUSTER_TEST(arguments, ir_function != 0);
            if (!ir_function)
            {
                continue;
            }
            MachineSelectResult selected = machine_select_canonical_function(arguments->arena, machine_vector_program, ir_function, machine_vector_target);
            BUSTER_TEST_RAW(arguments, selected.supported,
                            string_format(arguments->arena, S8("vector select {S8} failed at opcode {u32}"), vector_names[name_index],
                                          (u32)selected.failed_opcode));
            if (!selected.supported)
            {
                continue;
            }
            BUSTER_TEST(arguments, machine_verify_function(&selected.function).error == MACHINE_VERIFY_NONE);
            MachineStackPlacement vector_stack = machine_stack_placement_build(arguments->arena, &selected.function);
            MachineStackPlacement vector_fast = machine_fast_placement_build(arguments->arena, &selected.function);
            MachineStackPlacement vector_quality = machine_quality_placement_build(arguments->arena, &selected.function);
            BUSTER_TEST(arguments, vector_stack.valid && vector_fast.valid && vector_quality.valid);
            MachineEncodeResult stack_encoded = machine_encode_x86_64(arguments->arena, &selected.function, &vector_stack);
            MachineEncodeResult fast_encoded = machine_encode_x86_64(arguments->arena, &selected.function, &vector_fast);
            MachineEncodeResult quality_encoded = machine_encode_x86_64(arguments->arena, &selected.function, &vector_quality);
            BUSTER_TEST(arguments, stack_encoded.valid && fast_encoded.valid && quality_encoded.valid);
            // Eighteen live vectors against the thirty-two-register file:
            // every accumulator fits, so the scan must keep the vector
            // working set fully register-resident — any vector-class edit
            // here means the widened file is not actually being allocated.
            // The scalar traffic stays: the loop-carried counters conform
            // at the loop head and the frontend's stack save/restore
            // bracket round-trips through RSP exactly as before.
            if (string_equal(vector_names[name_index], S8("vspill")))
            {
                u32 fast_vector_edits = 0;
                for (u32 edit_index = 0; edit_index < vector_fast.edit_count; edit_index += 1)
                {
                    MachineEdit* edit = vector_fast.edits + edit_index;
                    fast_vector_edits += (edit->kind == MACHINE_EDIT_SPILL || edit->kind == MACHINE_EDIT_RELOAD) &&
                                         selected.function.virtual_registers[edit->subject].register_class == MACHINE_REGISTER_CLASS_VECTOR;
                }
                BUSTER_TEST_RAW(arguments, fast_vector_edits == 0,
                                string_format(arguments->arena, S8("vector-class edits fast {u32} (spills {u32} reloads {u32} stack {u32})"),
                                              fast_vector_edits, vector_fast.spill_count, vector_fast.reload_count, vector_stack.spill_count));
                // Eighteen accumulators cannot fit in ZMM0-15 alone, so a
                // fully resident placement is proof the scan handed out the
                // high file; the executing differential below is what then
                // proves the encoder's EVEX extension bits reproduce the
                // canonical results on an AVX-512 host.
                bool fast_reaches_high_file = false;
                for (u64 register_scan = 0; register_scan < (u64)selected.function.instruction_count * 4; register_scan += 1)
                {
                    fast_reaches_high_file |=
                        vector_fast.operand_registers[register_scan] != UINT8_MAX && vector_fast.operand_registers[register_scan] >= MACHINE_X64_ZMM16;
                }
                BUSTER_TEST(arguments, fast_reaches_high_file);
            }
        }
#if BUSTER_CPU_ARCH_X86_64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
        // Executing differential: all four modes over the same module, on
        // hosts whose cpuid carries the vocabulary.
        TargetCpuFeatures machine_vector_host = cpu_detect_features_x86_64();
        bool machine_vector_host_ready =
            target_cpu_features_contains(machine_vector_host, TARGET_CPU_FEATURE_X86_AVX512F) &&
            target_cpu_features_contains(machine_vector_host, TARGET_CPU_FEATURE_X86_AVX512BW) &&
            target_cpu_features_contains(machine_vector_host, TARGET_CPU_FEATURE_X86_AVX512VBMI) &&
            target_cpu_features_contains(machine_vector_host, TARGET_CPU_FEATURE_X86_AVX512VBMI2);
        if (machine_vector_host_ready)
        {
            CodegenRegisterAllocatorMode vector_modes[] = {
                CODEGEN_REGISTER_ALLOCATOR_NONE,
                CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                CODEGEN_REGISTER_ALLOCATOR_FAST,
                CODEGEN_REGISTER_ALLOCATOR_QUALITY,
            };
            CodegenModule vector_modules[BUSTER_ARRAY_LENGTH(vector_modes)] = {0};
            CodegenExecutable vector_executables[BUSTER_ARRAY_LENGTH(vector_modes)] = {0};
            bool vector_modules_ready = true;
            for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(vector_modes); mode_index += 1)
            {
                vector_modules[mode_index] = codegen_generate_canonical_module(arguments->arena, machine_vector_program, machine_vector_module,
                                                                              machine_vector_target,
                                                                              (CodegenModuleOptions){
                                                                                  .register_allocator = (u8)vector_modes[mode_index],
                                                                              });
                BUSTER_TEST(arguments, vector_modules[mode_index].error == CODEGEN_ERROR_NONE);
                for (u32 relocation_index = 0; relocation_index < vector_modules[mode_index].relocation_count; relocation_index += 1)
                {
                    CodegenModuleRelocation* relocation = vector_modules[mode_index].relocations + relocation_index;
                    BUSTER_TEST(arguments, codegen_module_relocation_valid(relocation));
                    if (relocation->source != CODEGEN_MODULE_RELOCATION_CODE || relocation->absolute)
                    {
                        continue;
                    }
                    for (u32 entry_index = 0; entry_index < vector_modules[mode_index].entry_count; entry_index += 1)
                    {
                        if (vector_modules[mode_index].entries[entry_index].symbol.value == relocation->symbol.value)
                        {
                            u32 displacement = vector_modules[mode_index].entries[entry_index].offset - (relocation->offset + 4);
                            memcpy(vector_modules[mode_index].code.pointer + relocation->offset, &displacement, sizeof(displacement));
                            break;
                        }
                    }
                }
                vector_executables[mode_index] = codegen_make_executable((CodegenFunction){
                    .code = vector_modules[mode_index].code,
                });
                BUSTER_TEST(arguments, vector_executables[mode_index].error == CODEGEN_ERROR_NONE);
                vector_modules_ready &= vector_executables[mode_index].error == CODEGEN_ERROR_NONE && vector_executables[mode_index].address != 0;
            }
            u8 vector_probe_bytes[64 * 18];
            u64 vector_probe_state = 0x9e3779b97f4a7c15ull;
            for (u32 byte_index = 0; byte_index < BUSTER_ARRAY_LENGTH(vector_probe_bytes); byte_index += 1)
            {
                vector_probe_state = vector_probe_state * 6364136223846793005ull + 1442695040888963407ull;
                vector_probe_bytes[byte_index] = (u8)(vector_probe_state >> 56);
            }
            typedef u64 MachineTestVectorCall(u8 const*, u64);
            typedef u64 MachineTestVectorTable(u8 const*, u8 const*, u8 const*, u64);
            typedef void MachineTestVectorStores(u8*, u8 const*, u64);
            u64 vector_mask_probes[] = {~0ull, 0x00ffff0000ffff00ull, 1ull, 0x8000000000000001ull};
            for (u32 mode_index = 1; mode_index < BUSTER_ARRAY_LENGTH(vector_modes) && vector_modules_ready; mode_index += 1)
            {
                for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(vector_mask_probes); probe_index += 1)
                {
                    u64 probe_mask = vector_mask_probes[probe_index];
                    String8 call_names[] = {S8_INITIALIZER("vclassify"), S8_INITIALIZER("vspill"),   S8_INITIALIZER("vcalls"),
                                            S8_INITIALIZER("vabi"),      S8_INITIALIZER("vqarith"), S8_INITIALIZER("vunion")};
                    for (u32 call_index = 0; call_index < BUSTER_ARRAY_LENGTH(call_names); call_index += 1)
                    {
                        u32 none_offset = machine_test_module_offset(&vector_modules[0], machine_vector_module, call_names[call_index]);
                        u32 mode_offset = machine_test_module_offset(&vector_modules[mode_index], machine_vector_module, call_names[call_index]);
                        BUSTER_TEST(arguments, none_offset != UINT32_MAX && mode_offset != UINT32_MAX);
                        if (none_offset == UINT32_MAX || mode_offset == UINT32_MAX)
                        {
                            continue;
                        }
                        void* none_address = (u8*)vector_executables[0].address + none_offset;
                        void* mode_address = (u8*)vector_executables[mode_index].address + mode_offset;
                        MachineTestVectorCall* none_call = 0;
                        MachineTestVectorCall* mode_call = 0;
                        memcpy(&none_call, &none_address, sizeof(none_call));
                        memcpy(&mode_call, &mode_address, sizeof(mode_call));
                        // vclassify consumes the mask as lane validity and
                        // vabi as a salt, so both take it raw; the loop
                        // bodies consume it as a round count, clamped so the
                        // differential stays fast.
                        bool mask_shaped = call_index == 0 || call_index == 3;
                        u64 argument = mask_shaped ? probe_mask : (probe_mask & 15ull) + 3ull;
                        u64 none_result = none_call(vector_probe_bytes, argument);
                        u64 mode_result = mode_call(vector_probe_bytes, argument);
                        BUSTER_TEST_RAW(arguments, none_result == mode_result,
                                        string_format(arguments->arena, S8("vector {S8} mode {u32} mask {u64} none {u64} got {u64}"),
                                                      call_names[call_index], (u32)vector_modes[mode_index], probe_mask, none_result, mode_result));
                    }
                    u32 none_table_offset = machine_test_module_offset(&vector_modules[0], machine_vector_module, S8("vtable"));
                    u32 mode_table_offset = machine_test_module_offset(&vector_modules[mode_index], machine_vector_module, S8("vtable"));
                    if (none_table_offset != UINT32_MAX && mode_table_offset != UINT32_MAX)
                    {
                        void* none_address = (u8*)vector_executables[0].address + none_table_offset;
                        void* mode_address = (u8*)vector_executables[mode_index].address + mode_table_offset;
                        MachineTestVectorTable* none_table = 0;
                        MachineTestVectorTable* mode_table = 0;
                        memcpy(&none_table, &none_address, sizeof(none_table));
                        memcpy(&mode_table, &mode_address, sizeof(mode_table));
                        u64 none_result = none_table(vector_probe_bytes, vector_probe_bytes + 64, vector_probe_bytes + 128, probe_mask);
                        u64 mode_result = mode_table(vector_probe_bytes, vector_probe_bytes + 64, vector_probe_bytes + 128, probe_mask);
                        BUSTER_TEST_RAW(arguments, none_result == mode_result,
                                        string_format(arguments->arena, S8("vector vtable mode {u32} mask {u64} none {u64} got {u64}"),
                                                      (u32)vector_modes[mode_index], probe_mask, none_result, mode_result));
                    }
                    u32 none_store_offset = machine_test_module_offset(&vector_modules[0], machine_vector_module, S8("vstores"));
                    u32 mode_store_offset = machine_test_module_offset(&vector_modules[mode_index], machine_vector_module, S8("vstores"));
                    if (none_store_offset != UINT32_MAX && mode_store_offset != UINT32_MAX)
                    {
                        u8 none_out[192];
                        u8 mode_out[192];
                        memset(none_out, 0xa5, sizeof(none_out));
                        memset(mode_out, 0xa5, sizeof(mode_out));
                        void* none_address = (u8*)vector_executables[0].address + none_store_offset;
                        void* mode_address = (u8*)vector_executables[mode_index].address + mode_store_offset;
                        MachineTestVectorStores* none_store = 0;
                        MachineTestVectorStores* mode_store = 0;
                        memcpy(&none_store, &none_address, sizeof(none_store));
                        memcpy(&mode_store, &mode_address, sizeof(mode_store));
                        none_store(none_out, vector_probe_bytes, probe_mask);
                        mode_store(mode_out, vector_probe_bytes, probe_mask);
                        BUSTER_TEST_RAW(arguments, memcmp(none_out, mode_out, sizeof(none_out)) == 0,
                                        string_format(arguments->arena, S8("vector vstores mode {u32} mask {u64}"), (u32)vector_modes[mode_index],
                                                      probe_mask));
                    }
                }
            }
            for (u32 mode_index = 0; mode_index < BUSTER_ARRAY_LENGTH(vector_modes); mode_index += 1)
            {
                codegen_release_executable(vector_executables[mode_index]);
            }
        }
#endif
    }

    // Stage 11: AArch64 selection, MIR_STACK placement, and encoding over
    // the scalar integer subset — plus the AAPCS64 shape coverage: a
    // two-part register aggregate parameter (kagg_take) and an indirect
    // result through X8 (big_make) — on the same C corpus with a fixed
    // aarch64-linux target so the machine path exercises on every host.
    // Execution requires a non-sanitized AArch64 host; the subset's
    // register-argument scalar signatures place identically under AAPCS64
    // and the Darwin convention, so the Linux-target bytes execute
    // natively on both.
    Target machine_a64_target = {
        .cpu_arch = CPU_ARCH_AARCH64,
        .os = OPERATING_SYSTEM_LINUX,
    };
    // The following memory and scalar cases enter through the exact helpers
    // used by the machine encoder. None may fall back to packed audit data.
    buster_aarch64_metadata_test_reset_packed_access_counter();

    // Differential coverage for the generated unsigned memory wrapper used
    // by frame and aggregate-copy emission. Each form is checked at offset
    // zero and at the architectural imm12 limit, with the old hand-encoded
    // words retained as an independent byte oracle. The frame helper's large
    // offset path is also checked: its materialize/add prefix stays intact
    // and only the final unsigned memory word comes from metadata.
    static struct
    {
        u32 size;
        bool store;
        char const* name;
    } const a64_memory_forms[] = {
        {1, false, "LDRBBui"},
        {2, false, "LDRHHui"},
        {4, false, "LDRWui"},
        {8, false, "LDRXui"},
        {1, true, "STRBBui"},
        {2, true, "STRHHui"},
        {4, true, "STRWui"},
        {8, true, "STRXui"},
    };
    for (u32 form_index = 0; form_index < BUSTER_ARRAY_LENGTH(a64_memory_forms); form_index += 1)
    {
        u32 size = a64_memory_forms[form_index].size;
        bool store = a64_memory_forms[form_index].store;
        u32 base = store ? (size == 1   ? UINT32_C(0x39000000)
                              : size == 2 ? UINT32_C(0x79000000)
                              : size == 4 ? UINT32_C(0xb9000000)
                                          : UINT32_C(0xf9000000))
                         : (size == 1   ? UINT32_C(0x39400000)
                              : size == 2 ? UINT32_C(0x79400000)
                              : size == 4 ? UINT32_C(0xb9400000)
                                          : UINT32_C(0xf9400000));
        u32 offsets[] = {0, 4095u * size};
        for (u32 offset_index = 0; offset_index < BUSTER_ARRAY_LENGTH(offsets); offset_index += 1)
        {
            u32 bytes[4] = {0};
            u32 byte_count = 0;
            bool error = false;
            bool emitted = machine_a64_test_emit_unsigned_memory((u8*)bytes, sizeof(bytes), 17, 28, offsets[offset_index], size, store, false,
                                                                   &byte_count, &error);
            u32 expected = base | (17u) | (28u << 5) | ((offsets[offset_index] / size) << 10);
            BUSTER_TEST_RAW(arguments, emitted && !error && byte_count == 4 && bytes[0] == expected,
                            string_format(arguments->arena, S8("a64 generated {S8} offset {u32}"), a64_memory_forms[form_index].name,
                                          offsets[offset_index]));
        }
        if (size > 1)
        {
            u32 bytes[4] = {0};
            u32 byte_count = 0;
            bool error = false;
            BUSTER_TEST(arguments, !machine_a64_test_emit_unsigned_memory((u8*)bytes, sizeof(bytes), 17, 28, 1, size, store, false, &byte_count,
                                                                            &error) &&
                                      error && byte_count == 0);
        }
        u32 bytes[4] = {0};
        u32 byte_count = 0;
        bool error = false;
        BUSTER_TEST(arguments, !machine_a64_test_emit_unsigned_memory((u8*)bytes, sizeof(bytes), 17, 28, 4096u * size, size, store, false, &byte_count,
                                                                        &error) &&
                                  error && byte_count == 0);
    }
    {
        u32 bytes[16] = {0};
        u32 byte_count = 0;
        bool error = false;
        BUSTER_TEST(arguments, machine_a64_test_emit_unsigned_memory((u8*)bytes, sizeof(bytes), 17, 0, 32768, 8, true, true, &byte_count, &error) &&
                                  !error && byte_count == 12);
        u32 words[3] = {0};
        memcpy(words, bytes, sizeof(words));
        BUSTER_TEST(arguments, words[0] == UINT32_C(0xd2900010) && words[1] == UINT32_C(0x8b100390) && words[2] == UINT32_C(0xf9000211));
    }
    // Literal words for every non-memory generated production row. These
    // cases enter through the real MachineOpcode mapping, with distinct
    // registers and nonzero immediates so a swapped semantic field changes
    // the oracle rather than merely reproducing a symmetric raw-layout test.
    static struct
    {
        u16 opcode;
        u32 operand0;
        u32 operand1;
        u32 operand2;
        u32 payload;
        u32 expected[2];
        u8 expected_count;
        char const* name;
    } const generated_machine_cases[] = {
        {MACHINE_A64_MOV_RR, 3, 5, 0, 0, {UINT32_C(0xaa0503e3)}, 1, "MOV_RR"},
        {MACHINE_A64_MOV32_RR, 3, 5, 0, 0, {UINT32_C(0x2a0503e3)}, 1, "MOV32_RR"},
        {MACHINE_A64_SXTB, 3, 5, 0, 0, {UINT32_C(0x93401ca3)}, 1, "SXTB"},
        {MACHINE_A64_SXTH, 3, 5, 0, 0, {UINT32_C(0x93403ca3)}, 1, "SXTH"},
        {MACHINE_A64_SXTW, 3, 5, 0, 0, {UINT32_C(0x93407ca3)}, 1, "SXTW"},
        {MACHINE_A64_UXTB, 3, 5, 0, 0, {UINT32_C(0x53001ca3)}, 1, "UXTB"},
        {MACHINE_A64_UXTH, 3, 5, 0, 0, {UINT32_C(0x53003ca3)}, 1, "UXTH"},
        {MACHINE_A64_ADD32, 3, 5, 7, 0, {UINT32_C(0x0b0700a3)}, 1, "ADD32"},
        {MACHINE_A64_ADD64, 3, 5, 7, 0, {UINT32_C(0x8b0700a3)}, 1, "ADD64"},
        {MACHINE_A64_SUB32, 3, 5, 7, 0, {UINT32_C(0x4b0700a3)}, 1, "SUB32"},
        {MACHINE_A64_SUB64, 3, 5, 7, 0, {UINT32_C(0xcb0700a3)}, 1, "SUB64"},
        {MACHINE_A64_AND32, 3, 5, 7, 0, {UINT32_C(0x0a0700a3)}, 1, "AND32"},
        {MACHINE_A64_AND64, 3, 5, 7, 0, {UINT32_C(0x8a0700a3)}, 1, "AND64"},
        {MACHINE_A64_ORR32, 3, 5, 7, 0, {UINT32_C(0x2a0700a3)}, 1, "ORR32"},
        {MACHINE_A64_ORR64, 3, 5, 7, 0, {UINT32_C(0xaa0700a3)}, 1, "ORR64"},
        {MACHINE_A64_EOR32, 3, 5, 7, 0, {UINT32_C(0x4a0700a3)}, 1, "EOR32"},
        {MACHINE_A64_EOR64, 3, 5, 7, 0, {UINT32_C(0xca0700a3)}, 1, "EOR64"},
        {MACHINE_A64_MUL32, 3, 5, 7, 0, {UINT32_C(0x1b077ca3)}, 1, "MUL32"},
        {MACHINE_A64_MUL64, 3, 5, 7, 0, {UINT32_C(0x9b077ca3)}, 1, "MUL64"},
        {MACHINE_A64_SDIV32, 3, 5, 7, 0, {UINT32_C(0x1ac70ca3)}, 1, "SDIV32"},
        {MACHINE_A64_SDIV64, 3, 5, 7, 0, {UINT32_C(0x9ac70ca3)}, 1, "SDIV64"},
        {MACHINE_A64_UDIV32, 3, 5, 7, 0, {UINT32_C(0x1ac708a3)}, 1, "UDIV32"},
        {MACHINE_A64_UDIV64, 3, 5, 7, 0, {UINT32_C(0x9ac708a3)}, 1, "UDIV64"},
        {MACHINE_A64_SREM32, 3, 5, 7, 0, {UINT32_C(0x1ac70ca3), UINT32_C(0x1b079463)}, 2, "SREM32"},
        {MACHINE_A64_SREM64, 3, 5, 7, 0, {UINT32_C(0x9ac70ca3), UINT32_C(0x9b079463)}, 2, "SREM64"},
        {MACHINE_A64_UREM32, 3, 5, 7, 0, {UINT32_C(0x1ac708a3), UINT32_C(0x1b079463)}, 2, "UREM32"},
        {MACHINE_A64_UREM64, 3, 5, 7, 0, {UINT32_C(0x9ac708a3), UINT32_C(0x9b079463)}, 2, "UREM64"},
        {MACHINE_A64_LSL32, 3, 5, 7, 0, {UINT32_C(0x1ac720a3)}, 1, "LSL32"},
        {MACHINE_A64_LSL64, 3, 5, 7, 0, {UINT32_C(0x9ac720a3)}, 1, "LSL64"},
        {MACHINE_A64_ASR32, 3, 5, 7, 0, {UINT32_C(0x1ac728a3)}, 1, "ASR32"},
        {MACHINE_A64_ASR64, 3, 5, 7, 0, {UINT32_C(0x9ac728a3)}, 1, "ASR64"},
        {MACHINE_A64_LSR32, 3, 5, 7, 0, {UINT32_C(0x1ac724a3)}, 1, "LSR32"},
        {MACHINE_A64_LSR64, 3, 5, 7, 0, {UINT32_C(0x9ac724a3)}, 1, "LSR64"},
        {MACHINE_A64_NEG32, 3, 5, 0, 0, {UINT32_C(0x4b0503e3)}, 1, "NEG32"},
        {MACHINE_A64_NEG64, 3, 5, 0, 0, {UINT32_C(0xcb0503e3)}, 1, "NEG64"},
        {MACHINE_A64_NOT32, 3, 5, 0, 0, {UINT32_C(0x2a2503e3)}, 1, "NOT32"},
        {MACHINE_A64_NOT64, 3, 5, 0, 0, {UINT32_C(0xaa2503e3)}, 1, "NOT64"},
        {MACHINE_A64_CMP32, 5, 7, 0, 0, {UINT32_C(0x6b0700bf)}, 1, "CMP32"},
        {MACHINE_A64_CMP64, 5, 7, 0, 0, {UINT32_C(0xeb0700bf)}, 1, "CMP64"},
        {MACHINE_A64_CMP_ZERO, 5, 0, 0, 0, {UINT32_C(0xf10000bf)}, 1, "CMP_ZERO"},
        {MACHINE_A64_CSET, 3, 0, 0, 5, {UINT32_C(0x1a9f47e3)}, 1, "CSET"},
        {MACHINE_A64_FMOV_TO_VEC, 5, 0, 0, 3, {UINT32_C(0x9e6700a3)}, 1, "FMOV_TO_VEC"},
        {MACHINE_A64_FMOV_FROM_VEC, 3, 0, 0, 5, {UINT32_C(0x9e6600a3)}, 1, "FMOV_FROM_VEC"},
        {MACHINE_A64_READ_SP, 3, 0, 0, 7, {UINT32_C(0x91001fe3)}, 1, "READ_SP"},
        {MACHINE_A64_WRITE_SP, 5, 0, 0, 7, {UINT32_C(0x91001cbf)}, 1, "WRITE_SP"},
        {MACHINE_A64_RET, 0, 0, 0, 0, {UINT32_C(0xd65f03c0)}, 1, "RET"},
    };
    for (u32 case_index = 0; case_index < BUSTER_ARRAY_LENGTH(generated_machine_cases); case_index += 1)
    {
        u8 bytes[8] = {0};
        u32 byte_count = 0;
        bool error = false;
        bool emitted = machine_a64_test_emit_generated_opcode((u8*)bytes, sizeof(bytes), generated_machine_cases[case_index].opcode,
                                                               generated_machine_cases[case_index].operand0,
                                                               generated_machine_cases[case_index].operand1,
                                                               generated_machine_cases[case_index].operand2,
                                                               generated_machine_cases[case_index].payload, &byte_count, &error);
        BUSTER_TEST_RAW(arguments,
                        emitted && !error && byte_count == generated_machine_cases[case_index].expected_count * sizeof(u32) &&
                            memcmp(bytes, generated_machine_cases[case_index].expected, byte_count) == 0,
                        string_format(arguments->arena, S8("a64 generated machine opcode {S8}"), generated_machine_cases[case_index].name));
    }
    BUSTER_TEST(arguments, buster_aarch64_metadata_test_packed_access_count() == 0);
    // Branch-relaxation seam coverage.  These probes keep the sparse-layout
    // acceptance checks independent of a giant byte buffer: the fixed long
    // transfer handles every aligned signed delta, while the classifier pins
    // the direct/short/long decisions at both architectural boundaries.
    {
        s64 long_deltas[] = {
            -4,
            0,
            4,
            INT64_C(0x12345678),
            -INT64_C(0x12345678),
            INT64_MIN,
            INT64_MAX - 3,
        };
        for (u32 delta_index = 0; delta_index < BUSTER_ARRAY_LENGTH(long_deltas); delta_index += 1)
        {
            u8 bytes[28] = {0};
            u32 byte_count = 0;
            bool emitted = machine_a64_test_emit_long_branch(bytes, sizeof(bytes), long_deltas[delta_index], &byte_count);
            BUSTER_TEST(arguments, emitted && byte_count == 28);
            u32 words[7] = {0};
            memcpy(words, bytes, sizeof(words));
            BUSTER_TEST(arguments, words[0] == UINT32_C(0x10000010) && words[5] == UINT32_C(0x8b110210) && words[6] == UINT32_C(0xd61f0200));
            u64 reconstructed = ((u64)(words[1] >> 5) & UINT64_C(0xffff)) | (((u64)(words[2] >> 5) & UINT64_C(0xffff)) << 16) |
                                (((u64)(words[3] >> 5) & UINT64_C(0xffff)) << 32) | (((u64)(words[4] >> 5) & UINT64_C(0xffff)) << 48);
            BUSTER_TEST(arguments, reconstructed == (u64)long_deltas[delta_index]);
            u8 repeat[28] = {0};
            u32 repeat_count = 0;
            BUSTER_TEST(arguments, machine_a64_test_emit_long_branch(repeat, sizeof(repeat), long_deltas[delta_index], &repeat_count) && repeat_count == byte_count &&
                                      memcmp(repeat, bytes, sizeof(bytes)) == 0);
        }
        u8 bytes[28] = {0};
        u32 byte_count = 0;
        BUSTER_TEST(arguments, !machine_a64_test_emit_long_branch(bytes, 27, 0, &byte_count));
        BUSTER_TEST(arguments, !machine_a64_test_emit_long_branch(bytes, sizeof(bytes), 2, &byte_count));
    }
    {
        s64 b_min = -INT64_C(134217728);
        s64 b_max = INT64_C(134217724);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B, 0, b_min) == 0);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B, 0, b_max) == 0);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B, 0, b_min - 4) == 2);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B, 0, b_max + 4) == 2);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B, 0, 2) == UINT8_MAX);

        s64 bcc_min = -INT64_C(1048576);
        s64 bcc_max = INT64_C(1048572);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, bcc_min) == 0);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, bcc_max) == 0);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, bcc_min - 4) == 1);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, bcc_max + 4) == 1);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, b_min + 4) == 1);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, b_max + 4) == 1);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, b_min) == 2);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, b_max + 8) == 2);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 14, bcc_max) == 0);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 14, bcc_max + 4) == UINT8_MAX);
        BUSTER_TEST(arguments, machine_a64_test_branch_relaxation_tier(A64_OPCODE_B_COND, 0, 2) == UINT8_MAX);
    }
    {
        // Two interacting edges force a genuine convergence sequence without
        // allocating their 200 MiB virtual layout.  The first BCC starts in
        // the medium tier, its fallthrough B is at the adjacent word, and a
        // later long B insertion shifts the BCC target across B's upper
        // boundary, upgrading it atomically to inverse-cond + long transfer.
        u32 sparse_code_size = 200100000u;
        MachineA64TestSparseFixup sparse_fixups[] = {
            {.source_offset = 1000000u,
             .target_offset = 135217728u,
             .row_offset = 1000000u,
             .call_offset = 1000016u,
             .epilog_offset = 1000020u,
             .opcode = A64_OPCODE_B_COND,
             .condition = 0},
            {.source_offset = 1000004u,
             .target_offset = 1200000u,
             .row_offset = 1000004u,
             .call_offset = 1000024u,
             .epilog_offset = 1000028u,
             .opcode = A64_OPCODE_B,
             .condition = 0xff},
            {.source_offset = 50000000u,
             .target_offset = 200000000u,
             .row_offset = 50000000u,
             .call_offset = 50000016u,
             .epilog_offset = 50000020u,
             .opcode = A64_OPCODE_B,
             .condition = 0xff},
        };
        MachineA64TestSparseFixup sparse_repeat[BUSTER_ARRAY_LENGTH(sparse_fixups)];
        memcpy(sparse_repeat, sparse_fixups, sizeof(sparse_fixups));
        u32 sparse_final_size = 0;
        u32 sparse_repeat_size = 0;
        bool sparse_valid = machine_a64_test_relax_sparse(arguments->arena, sparse_code_size, sparse_fixups,
                                                           BUSTER_ARRAY_LENGTH(sparse_fixups), &sparse_final_size);
        bool sparse_repeat_valid = machine_a64_test_relax_sparse(arguments->arena, sparse_code_size, sparse_repeat,
                                                                  BUSTER_ARRAY_LENGTH(sparse_repeat), &sparse_repeat_size);
        BUSTER_TEST(arguments, sparse_valid && sparse_repeat_valid && sparse_final_size == sparse_code_size + 52u && sparse_repeat_size == sparse_final_size);
        BUSTER_TEST(arguments, memcmp(sparse_fixups, sparse_repeat, sizeof(sparse_fixups)) == 0);
        BUSTER_TEST(arguments, sparse_fixups[0].tier == 2 && sparse_fixups[1].tier == 0 && sparse_fixups[2].tier == 2);
        // BCC source remains at P, while its fallthrough B moves from P+4
        // past the 32-byte long conditional expansion. The later B source
        // and all targets/call/epilog/row metadata move by the same checked
        // insertions used by production.
        BUSTER_TEST(arguments, sparse_fixups[0].source_offset == 1000000u && sparse_fixups[0].target_offset == 135217780u);
        BUSTER_TEST(arguments, sparse_fixups[1].source_offset == 1000032u && sparse_fixups[1].target_offset == 1200028u);
        BUSTER_TEST(arguments, sparse_fixups[2].source_offset == 50000028u && sparse_fixups[2].target_offset == 200000052u);
        BUSTER_TEST(arguments, sparse_fixups[1].row_offset == 1000032u && sparse_fixups[1].call_offset == 1000052u &&
                                  sparse_fixups[1].epilog_offset == 1000056u);
        BUSTER_TEST(arguments, sparse_fixups[2].row_offset == 50000028u && sparse_fixups[2].call_offset == 50000068u &&
                                  sparse_fixups[2].epilog_offset == 50000072u);

        MachineA64TestSparseFixup overflow_fixup = {
            .source_offset = UINT32_MAX - 7u,
            .target_offset = 0,
            .row_offset = UINT32_MAX - 7u,
            .call_offset = UINT32_MAX - 7u,
            .epilog_offset = UINT32_MAX - 7u,
            .opcode = A64_OPCODE_B,
            .condition = 0xff,
        };
        u32 overflow_size = 0;
        BUSTER_TEST(arguments, !machine_a64_test_relax_sparse(arguments->arena, UINT32_MAX - 3u, &overflow_fixup, 1, &overflow_size));
        MachineA64TestSparseFixup malformed_fixup = overflow_fixup;
        malformed_fixup.source_offset = 2;
        BUSTER_TEST(arguments, !machine_a64_test_relax_sparse(arguments->arena, 64, &malformed_fixup, 1, &overflow_size));
    }
    IrProgram* machine_a64_program = machine_test_compile_c(arguments->arena, S8("machine-stage11.c"), machine_c_source_stage11, machine_a64_target);
    BUSTER_TEST(arguments, machine_a64_program != 0);
    if (machine_a64_program && machine_a64_program->module_count)
    {
        IrModule* machine_a64_module = machine_a64_program->modules;
        CodegenModule a64_none_module = codegen_generate_canonical_module(arguments->arena, machine_a64_program, machine_a64_module, machine_a64_target,
                                                                          (CodegenModuleOptions){0});
        BUSTER_TEST(arguments, a64_none_module.error == CODEGEN_ERROR_NONE);
        String8 a64_supported_names[] = {
            S8_INITIALIZER("add"),     S8_INITIALIZER("mul"),    S8_INITIALIZER("widen"),  S8_INITIALIZER("narrow"),
            S8_INITIALIZER("negate"),  S8_INITIALIZER("bitnot"), S8_INITIALIZER("lnot"),   S8_INITIALIZER("less"),
            S8_INITIALIZER("uless"),   S8_INITIALIZER("six"),    S8_INITIALIZER("seven"),  S8_INITIALIZER("sum_to"),
            S8_INITIALIZER("readp"),   S8_INITIALIZER("writep"), S8_INITIALIZER("divide"), S8_INITIALIZER("srem"),
            S8_INITIALIZER("udiv"),    S8_INITIALIZER("shl"),    S8_INITIALIZER("sar"),    S8_INITIALIZER("shr"),
            S8_INITIALIZER("local_pair"), S8_INITIALIZER("kagg_take"), S8_INITIALIZER("big_make"),
            S8_INITIALIZER("arr_lit"), S8_INITIALIZER("bits"), S8_INITIALIZER("union_tail"), S8_INITIALIZER("locals_array"),
            S8_INITIALIZER("fmath"), S8_INITIALIZER("f32math"), S8_INITIALIZER("fcompare"), S8_INITIALIZER("fnegate"),
            S8_INITIALIZER("fnan"), S8_INITIALIZER("fuconv"), S8_INITIALIZER("ucvt"), S8_INITIALIZER("stack_tail"),
            S8_INITIALIZER("vsum"), S8_INITIALIZER("vec_pass"), S8_INITIALIZER("big_take"),
            S8_INITIALIZER("fpair_tail"), S8_INITIALIZER("pair_spill"),
            S8_INITIALIZER("aligned_local"), S8_INITIALIZER("aligned_spot"), S8_INITIALIZER("vlit_make"),
            S8_INITIALIZER("vquad_add"), S8_INITIALIZER("vf4_scale"), S8_INITIALIZER("amix"),
            S8_INITIALIZER("vla_fill"), S8_INITIALIZER("pick4"), S8_INITIALIZER("v8_pass"), S8_INITIALIZER("v2_make"),
        };
        MachineEncodeResult a64_encoded[BUSTER_ARRAY_LENGTH(a64_supported_names)] = {0};
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(a64_supported_names); name_index += 1)
        {
            IrFunction* ir_function = machine_test_ir_function_find(machine_a64_module, a64_supported_names[name_index]);
            BUSTER_TEST(arguments, ir_function != 0);
            if (!ir_function)
            {
                continue;
            }
            MachineSelectResult selected = {0};
            a64_encoded[name_index] = machine_test_encode(arguments->arena, machine_a64_program, ir_function, machine_a64_target, &selected);
            BUSTER_TEST_RAW(arguments, selected.supported,
                            string_format(arguments->arena, S8("a64 select {S8} failed at opcode {u32}"), a64_supported_names[name_index],
                                          (u32)selected.failed_opcode));
            BUSTER_TEST(arguments, a64_encoded[name_index].valid);
            BUSTER_TEST(arguments, a64_encoded[name_index].byte_count > 16 && a64_encoded[name_index].byte_count % 4 == 0);
            if (!a64_encoded[name_index].valid)
            {
                continue;
            }
            // The prologue shape is fixed: stp x29, x30, [sp, #-16]!;
            // mov x29, sp.
            u32 first_word = 0;
            u32 second_word = 0;
            memcpy(&first_word, a64_encoded[name_index].bytes, sizeof(first_word));
            memcpy(&second_word, a64_encoded[name_index].bytes + 4, sizeof(second_word));
            BUSTER_TEST(arguments, first_word == 0xa9bf7bfd && second_word == 0x910003fd);
            // Every return emits one recorded epilogue.
            BUSTER_TEST(arguments, a64_encoded[name_index].epilog_count >= 1);
        }
        // Placement over the AArch64 rows: the shared MIR_STACK builder
        // consumes the target description the selector stamped.
        IrFunction* a64_add_function = machine_test_ir_function_find(machine_a64_module, S8("add"));
        if (a64_add_function)
        {
            MachineSelectResult a64_add_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_add_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_add_selected.supported);
            BUSTER_TEST(arguments, a64_add_selected.function.target == machine_target_aarch64());
            MachineStackPlacement a64_add_placement = machine_stack_placement_build(arguments->arena, &a64_add_selected.function);
            BUSTER_TEST(arguments, a64_add_placement.valid);
            BUSTER_TEST(arguments, a64_add_placement.reload_count > 0 && a64_add_placement.spill_count > 0);
            BUSTER_TEST(arguments, a64_add_placement.frame_size % 16 == 0);
        }
        IrFunction* a64_srem_function = machine_test_ir_function_find(machine_a64_module, S8("srem"));
        if (a64_srem_function)
        {
            MachineSelectResult a64_srem_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_srem_function, machine_a64_target);
            bool saw_divide = false;
            bool saw_multiply = false;
            bool saw_subtract = false;
            bool saw_remainder_pseudo = false;
            bool one_definition_each = true;
            u32* definition_counts = a64_srem_selected.supported
                                          ? arena_allocate(arguments->arena, u32, a64_srem_selected.function.virtual_register_count ?
                                                                                     a64_srem_selected.function.virtual_register_count
                                                                                                                           : 1)
                                          : 0;
            for (u32 register_index = 0; definition_counts && register_index < a64_srem_selected.function.virtual_register_count; register_index += 1)
            {
                definition_counts[register_index] = 0;
            }
            for (u32 row_index = 0; a64_srem_selected.supported && row_index < a64_srem_selected.function.instruction_count; row_index += 1)
            {
                MachineInstruction* row = a64_srem_selected.function.instructions + row_index;
                u16 opcode = row->opcode;
                saw_divide |= opcode == MACHINE_A64_SDIV32 || opcode == MACHINE_A64_SDIV64 || opcode == MACHINE_A64_UDIV32 || opcode == MACHINE_A64_UDIV64;
                saw_multiply |= opcode == MACHINE_A64_MUL32 || opcode == MACHINE_A64_MUL64;
                saw_subtract |= opcode == MACHINE_A64_SUB32 || opcode == MACHINE_A64_SUB64;
                saw_remainder_pseudo |= opcode == MACHINE_A64_SREM32 || opcode == MACHINE_A64_SREM64 || opcode == MACHINE_A64_UREM32 || opcode == MACHINE_A64_UREM64;
                MachineOpcodeInfo const* info = machine_opcode_info(opcode);
                for (u32 slot = 0; info && slot < info->operand_count; slot += 1)
                {
                    MachineRef operand_ref = row->operands[slot];
                    u32 role = info->operand_info[slot] & ((1u << MACHINE_OPERAND_ROLE_BITS) - 1u);
                    if (definition_counts && machine_ref_kind(operand_ref) == MACHINE_REF_VIRTUAL_REGISTER &&
                        (role == MACHINE_OPERAND_ROLE_DEFINE || role == MACHINE_OPERAND_ROLE_USE_DEFINE))
                    {
                        u32 register_index = machine_ref_payload(operand_ref);
                        if (register_index < a64_srem_selected.function.virtual_register_count)
                        {
                            definition_counts[register_index] += 1;
                            one_definition_each &= definition_counts[register_index] == 1;
                        }
                    }
                }
            }
            BUSTER_TEST(arguments, a64_srem_selected.supported && saw_divide && saw_multiply && saw_subtract && !saw_remainder_pseudo && one_definition_each);
        }
        // Callee-saved encoding: a value pinned to X27 for its whole
        // lifetime forces the prologue save at the top of the frame area
        // and the epilogue restore, which nothing in the C corpus's
        // two-instruction lifetimes otherwise exercises.
        IrFunction* a64_pin_function = machine_test_ir_function_find(machine_a64_module, S8("add"));
        if (a64_pin_function)
        {
            MachineSelectResult a64_pin_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_pin_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_pin_selected.supported);
            if (a64_pin_selected.supported && a64_pin_selected.function.virtual_register_count)
            {
                u32* forced_pins = arena_allocate(arguments->arena, u32, a64_pin_selected.function.virtual_register_count);
                for (u32 register_index = 0; register_index < a64_pin_selected.function.virtual_register_count; register_index += 1)
                {
                    forced_pins[register_index] = UINT32_MAX;
                }
                forced_pins[0] = MACHINE_A64_X27;
                MachineStackPlacement a64_pinned_placement =
                    machine_fast_placement_build_pinned(arguments->arena, &a64_pin_selected.function, forced_pins, 1u << MACHINE_A64_X27, 0, 0, 0, 0, 0, 0, 0);
                BUSTER_TEST(arguments, a64_pinned_placement.valid);
                BUSTER_TEST(arguments, (a64_pinned_placement.callee_saved_mask >> MACHINE_A64_X27) & 1u);
                MachineEncodeResult a64_pinned_encoded = machine_encode_aarch64(arguments->arena, &a64_pin_selected.function, &a64_pinned_placement);
                BUSTER_TEST(arguments, a64_pinned_encoded.valid);
                if (a64_pinned_encoded.valid)
                {
                    // The save is one scaled str of x27 against SP; its
                    // exact offset rides at the top of the frame area.
                    u32 a64_pin_area = a64_pinned_placement.frame_size + 8;
                    u32 save_word = 0xf90003e0u | (((a64_pin_area - 8) / 8) << 10) | ((u32)MACHINE_A64_SP << 5) | MACHINE_A64_X27;
                    u32 restore_word = 0xf94003e0u | (((a64_pin_area - 8) / 8) << 10) | ((u32)MACHINE_A64_SP << 5) | MACHINE_A64_X27;
                    bool save_found = false;
                    bool restore_found = false;
                    for (u32 byte_offset = 0; byte_offset + 4 <= a64_pinned_encoded.byte_count; byte_offset += 4)
                    {
                        u32 word = 0;
                        memcpy(&word, a64_pinned_encoded.bytes + byte_offset, sizeof(word));
                        save_found |= word == save_word;
                        restore_found |= word == restore_word;
                    }
                    BUSTER_TEST(arguments, save_found && restore_found);
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
                    CodegenExecutable a64_pinned_executable = codegen_make_executable((CodegenFunction){
                        .code = {.pointer = a64_pinned_encoded.bytes, .length = a64_pinned_encoded.byte_count},
                    });
                    BUSTER_TEST(arguments, a64_pinned_executable.error == CODEGEN_ERROR_NONE);
                    if (a64_pinned_executable.address)
                    {
                        typedef s64 MachineTestA64Pinned(s64, s64);
                        MachineTestA64Pinned* pinned_call = 0;
                        memcpy(&pinned_call, &a64_pinned_executable.address, sizeof(pinned_call));
                        BUSTER_TEST(arguments, (s32)pinned_call(19, 23) == 42);
                        codegen_release_executable(a64_pinned_executable);
                    }
#endif
                }
            }
        }
        // Splitting on the second target: the split machinery is
        // target-parameterized and split_phase must select and place
        // cleanly under the AArch64 file. No split can fire here yet —
        // the AArch64 selector does not promote scalar locals the way
        // stage 10i taught the x86-64 one, so the loop values stay
        // frame-resident and QUALITY sees no candidates; when promotion
        // reaches this selector, this block should tighten to the x86-64
        // assertion.
        IrFunction* a64_split_function = machine_test_ir_function_find(machine_a64_module, S8("split_phase"));
        BUSTER_TEST(arguments, a64_split_function != 0);
        if (a64_split_function)
        {
            MachineSelectResult a64_split_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_split_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_split_selected.supported);
            if (a64_split_selected.supported)
            {
                MachineStackPlacement a64_split_quality = machine_quality_placement_build(arguments->arena, &a64_split_selected.function);
                BUSTER_TEST(arguments, a64_split_quality.valid);
            }
        }
        // Direct calls select into fixed-register argument copies plus a
        // relocated call row; float signatures are the current explicit
        // unsupported representative.
        IrFunction* a64_call_function = machine_test_ir_function_find(machine_a64_module, S8("with_call"));
        BUSTER_TEST(arguments, a64_call_function != 0);
        if (a64_call_function)
        {
            MachineSelectResult a64_call_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_call_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_call_selected.supported);
            BUSTER_TEST(arguments, a64_call_selected.function.call_target_count >= 2);
        }
        // Float bodies now select through the FARITH/FCMP rows; label
        // addresses are the current explicit unsupported representative.
        IrFunction* a64_float_function = machine_test_ir_function_find(machine_a64_module, S8("fadd"));
        BUSTER_TEST(arguments, a64_float_function != 0);
        if (a64_float_function)
        {
            MachineSelectResult a64_float_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_float_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_float_selected.supported);
        }
        IrFunction* a64_goto_function = machine_test_ir_function_find(machine_a64_module, S8("goto_probe"));
        BUSTER_TEST(arguments, a64_goto_function != 0);
        if (a64_goto_function)
        {
            MachineSelectResult a64_goto_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_goto_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_goto_selected.supported);
            if (a64_goto_selected.supported)
            {
                MachineStackPlacement a64_goto_placement = machine_stack_placement_build(arguments->arena, &a64_goto_selected.function);
                MachineEncodeResult a64_goto_encoded =
                    machine_encode_aarch64(arguments->arena, &a64_goto_selected.function, &a64_goto_placement);
                BUSTER_TEST(arguments, a64_goto_placement.valid && a64_goto_encoded.valid && a64_goto_encoded.byte_count != 0);
            }
        }
        // An aggregate literal built and passed onward: kagg's call keeps it
        // out of the raw-copy execution list, so it is asserted as selection
        // coverage like with_call.
        IrFunction* a64_kagg_function = machine_test_ir_function_find(machine_a64_module, S8("kagg"));
        BUSTER_TEST(arguments, a64_kagg_function != 0);
        if (a64_kagg_function)
        {
            MachineSelectResult a64_kagg_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_kagg_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_kagg_selected.supported);
        }
        // Stack-argument callers: call_stack_tail spills the ninth and tenth
        // long into the outgoing area, and nine spills its ninth double —
        // both call-bearing or float-signatured, so selection coverage only.
        IrFunction* a64_stack_caller_function = machine_test_ir_function_find(machine_a64_module, S8("call_stack_tail"));
        BUSTER_TEST(arguments, a64_stack_caller_function != 0);
        if (a64_stack_caller_function)
        {
            MachineSelectResult a64_stack_caller_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_stack_caller_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_stack_caller_selected.supported);
            BUSTER_TEST(arguments, a64_stack_caller_selected.function.outgoing_bytes == 16);
        }
        IrFunction* a64_nine_function = machine_test_ir_function_find(machine_a64_module, S8("nine"));
        BUSTER_TEST(arguments, a64_nine_function != 0);
        if (a64_nine_function)
        {
            MachineSelectResult a64_nine_selected =
                machine_select_canonical_function(arguments->arena, machine_a64_program, a64_nine_function, machine_a64_target);
            BUSTER_TEST(arguments, a64_nine_selected.supported);
        }
        // Module wiring: MIR_STACK on the AArch64 target routes the subset
        // through the machine path and counts the rest.
        CodegenModule a64_mir_module = codegen_generate_canonical_module(arguments->arena, machine_a64_program, machine_a64_module, machine_a64_target,
                                                                         (CodegenModuleOptions){
                                                                             .register_allocator = CODEGEN_REGISTER_ALLOCATOR_MIR_STACK,
                                                                         });
        BUSTER_TEST(arguments, a64_mir_module.error == CODEGEN_ERROR_NONE);
        BUSTER_TEST(arguments, a64_none_module.statistics.fallback_function_count == 0);
        BUSTER_TEST(arguments, a64_mir_module.statistics.fallback_function_count > 0);
        IrFunction* a64_mir_add = machine_test_ir_function_find(machine_a64_module, S8("add"));
        if (a64_mir_add)
        {
            CodegenFunctionDescriptor* a64_add_descriptor = 0;
            for (u32 descriptor_index = 0; descriptor_index < a64_mir_module.function_count; descriptor_index += 1)
            {
                if (a64_mir_module.functions[descriptor_index].symbol.value == a64_mir_add->symbol.value)
                {
                    a64_add_descriptor = a64_mir_module.functions + descriptor_index;
                    break;
                }
            }
            BUSTER_TEST(arguments, a64_add_descriptor != 0);
            BUSTER_TEST(arguments, a64_add_descriptor && a64_add_descriptor->code_size > 16);
            // The machine prologue's actions mirror the canonical AArch64
            // shape: the frame-pointer pair's allocation first, the x28
            // save last, and one recorded epilogue per return.
            BUSTER_TEST(arguments, a64_add_descriptor && a64_add_descriptor->unwind_action_count >= 5 &&
                                       a64_add_descriptor->unwind_actions[0].kind == CODEGEN_UNWIND_ACTION_ALLOCATE_STACK &&
                                       a64_add_descriptor->unwind_actions[0].value == 16);
            BUSTER_TEST(arguments, a64_add_descriptor && a64_add_descriptor->epilog_count >= 1);
            BUSTER_TEST(arguments, a64_add_descriptor && a64_add_descriptor->prolog_size >= 24 && a64_add_descriptor->prolog_size % 4 == 0);
        }
#if BUSTER_CPU_ARCH_AARCH64 && !BUSTER_WINDOWS && !BUSTER_SANITIZE
        // Native execution differential: the canonical NONE module is the
        // oracle at each function's entry offset. Only call-free,
        // global-free functions execute from raw code copies.
        CodegenExecutable a64_none_executable = codegen_make_executable((CodegenFunction){
            .code = a64_none_module.code,
        });
        BUSTER_TEST(arguments, a64_none_executable.error == CODEGEN_ERROR_NONE);
        typedef s64 MachineTestA64Call2(s64, s64);
        typedef s64 MachineTestA64Call7(s64, s64, s64, s64, s64, s64, s64);
        typedef s64 MachineTestA64Call3(s64, s64, s64);
        typedef s64 MachineTestA64Call10(s64, s64, s64, s64, s64, s64, s64, s64, s64, s64);
        // Matches the corpus Big layout; returned through the X8 hidden
        // pointer, which the host compiler stages for a struct-returning
        // call — identical under AAPCS64 and the Darwin convention.
        typedef struct MachineTestA64Big
        {
            s64 a;
            s64 b;
            s64 c;
        } MachineTestA64Big;
        typedef MachineTestA64Big MachineTestA64CallBig(s64);
        // Sixteen-byte short vectors ride V0 in and out under AAPCS64 and
        // the Darwin convention alike.
        typedef float MachineTestA64V4 __attribute__((vector_size(16)));
        typedef MachineTestA64V4 MachineTestA64CallV4(MachineTestA64V4);
        // The literal-constructing form: two integer arguments in, the
        // sixteen-byte vector result out through V0, so the vector ARRAY
        // path executes with scalar-only incoming registers.
        typedef s32 MachineTestA64VLit __attribute__((vector_size(16)));
        typedef MachineTestA64VLit MachineTestA64CallVLit(s32, s32);
        // The arithmetic forms: two sixteen-byte vectors in V0/V1, the
        // result out through V0, so the VARITH chunk rows execute between
        // the entry captures and the return load on both conventions.
        typedef MachineTestA64VLit MachineTestA64CallVQuad(MachineTestA64VLit, MachineTestA64VLit);
        typedef MachineTestA64V4 MachineTestA64CallVF4(MachineTestA64V4, MachineTestA64V4);
        // Sub-sixteen-byte short vectors ride the low bytes of V0 in and
        // out under AAPCS64 and the Darwin convention alike, so the sized
        // V transfers execute natively on either host.
        typedef u8 MachineTestA64V8 __attribute__((vector_size(8)));
        typedef MachineTestA64V8 MachineTestA64CallV8(MachineTestA64V8);
        typedef u8 MachineTestA64V2 __attribute__((vector_size(2)));
        typedef MachineTestA64V2 MachineTestA64CallV2(s32);
        // Matches the corpus MachineBig3 layout: a twenty-four-byte
        // composite passes indirectly behind a caller-side copy that the
        // host compiler stages, identically under both conventions.
        typedef struct MachineTestA64Big3
        {
            s64 a;
            s64 b;
            s64 c;
        } MachineTestA64Big3;
        typedef s64 MachineTestA64CallBig3(MachineTestA64Big3, s64);
        // Stack-passed aggregate shapes with nothing after them, so the
        // subset's open-file quirks never diverge from the host compiler's
        // conventions: the HFA lands at the stack base once V0-V7 are
        // full, and the two-part pair lands there once X0-X6 leave one
        // integer register short.
        typedef struct MachineTestA64FPair
        {
            double left;
            double right;
        } MachineTestA64FPair;
        typedef double MachineTestA64CallFPairTail(double, double, double, double, double, double, double, double, MachineTestA64FPair);
        typedef struct MachineTestA64Pair2
        {
            s64 low;
            s64 high;
        } MachineTestA64Pair2;
        typedef s64 MachineTestA64CallPairSpill(s64, s64, s64, s64, s64, s64, s64, MachineTestA64Pair2);
        for (u32 name_index = 0; name_index < BUSTER_ARRAY_LENGTH(a64_supported_names) && a64_none_executable.address; name_index += 1)
        {
            if (!a64_encoded[name_index].valid)
            {
                continue;
            }
            IrFunction* ir_function = machine_test_ir_function_find(machine_a64_module, a64_supported_names[name_index]);
            u32 none_offset = UINT32_MAX;
            for (u32 entry_index = 0; entry_index < a64_none_module.entry_count; entry_index += 1)
            {
                if (ir_function && a64_none_module.entries[entry_index].symbol.value == ir_function->symbol.value)
                {
                    none_offset = a64_none_module.entries[entry_index].offset;
                    break;
                }
            }
            BUSTER_TEST(arguments, none_offset != UINT32_MAX);
            CodegenExecutable a64_machine_executable = codegen_make_executable((CodegenFunction){
                .code = {.pointer = a64_encoded[name_index].bytes, .length = a64_encoded[name_index].byte_count},
            });
            BUSTER_TEST(arguments, a64_machine_executable.error == CODEGEN_ERROR_NONE);
            if (none_offset == UINT32_MAX || !a64_machine_executable.address)
            {
                continue;
            }
            // The shape-bearing signatures need their real calling forms:
            // kagg_take's two-part aggregate arrives as two integer
            // registers plus the salt in the third, and big_make returns
            // through the X8 hidden pointer the caller must provide — a
            // raw two-argument call would hand it garbage.
            bool is_pair_take = string_equal(a64_supported_names[name_index], S8("kagg_take"));
            bool is_indirect_make = string_equal(a64_supported_names[name_index], S8("big_make"));
            bool is_writep = string_equal(a64_supported_names[name_index], S8("writep"));
            bool is_readp = string_equal(a64_supported_names[name_index], S8("readp"));
            bool is_many = string_equal(a64_supported_names[name_index], S8("six")) || string_equal(a64_supported_names[name_index], S8("seven"));
            bool is_stack_tail = string_equal(a64_supported_names[name_index], S8("stack_tail"));
            bool is_vsum = string_equal(a64_supported_names[name_index], S8("vsum"));
            bool is_vec_pass = string_equal(a64_supported_names[name_index], S8("vec_pass"));
            bool is_vlit_make = string_equal(a64_supported_names[name_index], S8("vlit_make"));
            bool is_vquad_add = string_equal(a64_supported_names[name_index], S8("vquad_add"));
            bool is_vf4_scale = string_equal(a64_supported_names[name_index], S8("vf4_scale"));
            bool is_v8_pass = string_equal(a64_supported_names[name_index], S8("v8_pass"));
            bool is_v2_make = string_equal(a64_supported_names[name_index], S8("v2_make"));
            bool is_big_take = string_equal(a64_supported_names[name_index], S8("big_take"));
            bool is_fpair_tail = string_equal(a64_supported_names[name_index], S8("fpair_tail"));
            bool is_pair_spill = string_equal(a64_supported_names[name_index], S8("pair_spill"));
            bool is_loop = string_equal(a64_supported_names[name_index], S8("sum_to"));
            bool wide_result = string_equal(a64_supported_names[name_index], S8("widen")) ||
                               string_equal(a64_supported_names[name_index], S8("bitnot")) ||
                               string_equal(a64_supported_names[name_index], S8("sar")) ||
                               string_equal(a64_supported_names[name_index], S8("arr_lit")) ||
                               string_equal(a64_supported_names[name_index], S8("bits")) ||
                               string_equal(a64_supported_names[name_index], S8("union_tail")) ||
                               string_equal(a64_supported_names[name_index], S8("ucvt")) ||
                               string_equal(a64_supported_names[name_index], S8("udiv")) ||
                               string_equal(a64_supported_names[name_index], S8("aligned_spot")) ||
                               string_equal(a64_supported_names[name_index], S8("amix")) ||
                               string_equal(a64_supported_names[name_index], S8("vla_fill")) ||
                               string_equal(a64_supported_names[name_index], S8("pick4")) || is_readp;
            bool is_division = string_equal(a64_supported_names[name_index], S8("divide")) ||
                               string_equal(a64_supported_names[name_index], S8("srem")) ||
                               string_equal(a64_supported_names[name_index], S8("udiv"));
            s64 probe_arguments[][2] = {
                {0, 0}, {1, 2}, {-1, 5}, {123456789, -987654321}, {-2147483647, 2147483647}, {40, 2}, {7, -7},
            };
            bool all_equal = true;
            for (u32 probe_index = 0; probe_index < BUSTER_ARRAY_LENGTH(probe_arguments); probe_index += 1)
            {
                s64 left = probe_arguments[probe_index][0];
                s64 right = probe_arguments[probe_index][1];
                if (is_loop)
                {
                    left &= 63;
                }
                if (is_division)
                {
                    right |= 1;
                }
                void* none_address = (u8*)a64_none_executable.address + none_offset;
                void* machine_address = a64_machine_executable.address;
                MachineTestA64Call2* none_call = 0;
                MachineTestA64Call2* machine_call = 0;
                memcpy(&none_call, &none_address, sizeof(none_call));
                memcpy(&machine_call, &machine_address, sizeof(machine_call));
                if (is_pair_take)
                {
                    MachineTestA64Call3* none_call3 = 0;
                    MachineTestA64Call3* machine_call3 = 0;
                    memcpy(&none_call3, &none_address, sizeof(none_call3));
                    memcpy(&machine_call3, &machine_address, sizeof(machine_call3));
                    all_equal &= none_call3(left, right, left ^ right) == machine_call3(left, right, left ^ right);
                }
                else if (is_indirect_make)
                {
                    MachineTestA64CallBig* none_call_big = 0;
                    MachineTestA64CallBig* machine_call_big = 0;
                    memcpy(&none_call_big, &none_address, sizeof(none_call_big));
                    memcpy(&machine_call_big, &machine_address, sizeof(machine_call_big));
                    MachineTestA64Big none_big = none_call_big(left);
                    MachineTestA64Big machine_big = machine_call_big(left);
                    all_equal &= none_big.a == machine_big.a && none_big.b == machine_big.b && none_big.c == machine_big.c;
                }
                else if (is_writep)
                {
                    s32 none_cell = 0;
                    s32 machine_cell = 0;
                    none_call((s64)(u64)&none_cell, right);
                    machine_call((s64)(u64)&machine_cell, right);
                    all_equal &= none_cell == machine_cell;
                }
                else if (is_readp)
                {
                    s64 cell = left * 3 + right;
                    all_equal &= none_call((s64)(u64)&cell, 0) == machine_call((s64)(u64)&cell, 0);
                }
                else if (is_many)
                {
                    MachineTestA64Call7* none_call7 = 0;
                    MachineTestA64Call7* machine_call7 = 0;
                    memcpy(&none_call7, &none_address, sizeof(none_call7));
                    memcpy(&machine_call7, &machine_address, sizeof(machine_call7));
                    s32 none_result = (s32)none_call7(left, right, left + 1, right + 1, left - 2, right - 2, left + 3);
                    s32 machine_result = (s32)machine_call7(left, right, left + 1, right + 1, left - 2, right - 2, left + 3);
                    all_equal &= none_result == machine_result;
                }
                else if (is_stack_tail)
                {
                    // Ten arguments: the ninth and tenth travel through the
                    // caller's outgoing stack area on AAPCS64.
                    MachineTestA64Call10* none_call10 = 0;
                    MachineTestA64Call10* machine_call10 = 0;
                    memcpy(&none_call10, &none_address, sizeof(none_call10));
                    memcpy(&machine_call10, &machine_address, sizeof(machine_call10));
                    all_equal &= none_call10(left, right, left + 1, right + 1, left - 2, right - 2, left + 3, right + 3, left ^ right, right - left) ==
                                 machine_call10(left, right, left + 1, right + 1, left - 2, right - 2, left + 3, right + 3, left ^ right, right - left);
                }
                else if (is_vsum)
                {
                    // The variadic body driven through a non-variadic
                    // seven-register call form: the anonymous parts sit in
                    // X1-X6 under both AAPCS64 and the Darwin convention
                    // precisely because the host compiler does not know
                    // the callee is variadic, so the register path of the
                    // four-word va_list model executes on either host. The
                    // overflow tail stays with the qemu differential.
                    MachineTestA64Call7* none_call7 = 0;
                    MachineTestA64Call7* machine_call7 = 0;
                    memcpy(&none_call7, &none_address, sizeof(none_call7));
                    memcpy(&machine_call7, &machine_address, sizeof(machine_call7));
                    all_equal &= none_call7(6, left, right, left + 1, right - 2, left ^ right, right - left) ==
                                 machine_call7(6, left, right, left + 1, right - 2, left ^ right, right - left);
                }
                else if (is_vec_pass)
                {
                    MachineTestA64CallV4* none_callv = 0;
                    MachineTestA64CallV4* machine_callv = 0;
                    memcpy(&none_callv, &none_address, sizeof(none_callv));
                    memcpy(&machine_callv, &machine_address, sizeof(machine_callv));
                    MachineTestA64V4 vector_probe = {(float)left, (float)right, (float)(left + 1), (float)(right - 3)};
                    MachineTestA64V4 none_vector = none_callv(vector_probe);
                    MachineTestA64V4 machine_vector = machine_callv(vector_probe);
                    for (u32 lane = 0; lane < 4; lane += 1)
                    {
                        all_equal &= none_vector[lane] == machine_vector[lane];
                    }
                }
                else if (is_vlit_make)
                {
                    MachineTestA64CallVLit* none_callvl = 0;
                    MachineTestA64CallVLit* machine_callvl = 0;
                    memcpy(&none_callvl, &none_address, sizeof(none_callvl));
                    memcpy(&machine_callvl, &machine_address, sizeof(machine_callvl));
                    MachineTestA64VLit none_vector = none_callvl((s32)left, (s32)right);
                    MachineTestA64VLit machine_vector = machine_callvl((s32)left, (s32)right);
                    for (u32 lane = 0; lane < 4; lane += 1)
                    {
                        all_equal &= none_vector[lane] == machine_vector[lane];
                    }
                }
                else if (is_vquad_add)
                {
                    MachineTestA64CallVQuad* none_callvq = 0;
                    MachineTestA64CallVQuad* machine_callvq = 0;
                    memcpy(&none_callvq, &none_address, sizeof(none_callvq));
                    memcpy(&machine_callvq, &machine_address, sizeof(machine_callvq));
                    MachineTestA64VLit left_probe = {(s32)left, (s32)right, (s32)(left + 7), (s32)(right - 9)};
                    MachineTestA64VLit right_probe = {(s32)(right * 3), (s32)(left ^ right), (s32)left, (s32)(right + 1)};
                    MachineTestA64VLit none_vector = none_callvq(left_probe, right_probe);
                    MachineTestA64VLit machine_vector = machine_callvq(left_probe, right_probe);
                    for (u32 lane = 0; lane < 4; lane += 1)
                    {
                        all_equal &= none_vector[lane] == machine_vector[lane];
                    }
                }
                else if (is_vf4_scale)
                {
                    MachineTestA64CallVF4* none_callvf = 0;
                    MachineTestA64CallVF4* machine_callvf = 0;
                    memcpy(&none_callvf, &none_address, sizeof(none_callvf));
                    memcpy(&machine_callvf, &machine_address, sizeof(machine_callvf));
                    MachineTestA64V4 value_probe = {(float)left, (float)right, (float)(left + 2), (float)(right - 5)};
                    MachineTestA64V4 scale_probe = {2.0f, -0.5f, (float)right, 1.25f};
                    MachineTestA64V4 none_vector = none_callvf(value_probe, scale_probe);
                    MachineTestA64V4 machine_vector = machine_callvf(value_probe, scale_probe);
                    for (u32 lane = 0; lane < 4; lane += 1)
                    {
                        all_equal &= none_vector[lane] == machine_vector[lane];
                    }
                }
                else if (is_v8_pass)
                {
                    MachineTestA64CallV8* none_callv8 = 0;
                    MachineTestA64CallV8* machine_callv8 = 0;
                    memcpy(&none_callv8, &none_address, sizeof(none_callv8));
                    memcpy(&machine_callv8, &machine_address, sizeof(machine_callv8));
                    MachineTestA64V8 probe_vector = {(u8)left, (u8)right, (u8)(left + 1), (u8)(right + 2), (u8)(left ^ right), 7, 8, (u8)(right - left)};
                    MachineTestA64V8 none_vector = none_callv8(probe_vector);
                    MachineTestA64V8 machine_vector = machine_callv8(probe_vector);
                    for (u32 lane = 0; lane < 8; lane += 1)
                    {
                        all_equal &= none_vector[lane] == machine_vector[lane];
                    }
                }
                else if (is_v2_make)
                {
                    MachineTestA64CallV2* none_callv2 = 0;
                    MachineTestA64CallV2* machine_callv2 = 0;
                    memcpy(&none_callv2, &none_address, sizeof(none_callv2));
                    memcpy(&machine_callv2, &machine_address, sizeof(machine_callv2));
                    MachineTestA64V2 none_vector = none_callv2((s32)left);
                    MachineTestA64V2 machine_vector = machine_callv2((s32)left);
                    for (u32 lane = 0; lane < 2; lane += 1)
                    {
                        all_equal &= none_vector[lane] == machine_vector[lane];
                    }
                }
                else if (is_big_take)
                {
                    MachineTestA64CallBig3* none_call_big3 = 0;
                    MachineTestA64CallBig3* machine_call_big3 = 0;
                    memcpy(&none_call_big3, &none_address, sizeof(none_call_big3));
                    memcpy(&machine_call_big3, &machine_address, sizeof(machine_call_big3));
                    MachineTestA64Big3 big_probe = {left, right, left ^ right};
                    all_equal &= none_call_big3(big_probe, right - left) == machine_call_big3(big_probe, right - left);
                }
                else if (is_fpair_tail)
                {
                    MachineTestA64CallFPairTail* none_call_fpair = 0;
                    MachineTestA64CallFPairTail* machine_call_fpair = 0;
                    memcpy(&none_call_fpair, &none_address, sizeof(none_call_fpair));
                    memcpy(&machine_call_fpair, &machine_address, sizeof(machine_call_fpair));
                    MachineTestA64FPair fpair_probe = {(double)left, (double)right};
                    all_equal &= none_call_fpair(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, fpair_probe) ==
                                 machine_call_fpair(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, fpair_probe);
                }
                else if (is_pair_spill)
                {
                    MachineTestA64CallPairSpill* none_call_pair = 0;
                    MachineTestA64CallPairSpill* machine_call_pair = 0;
                    memcpy(&none_call_pair, &none_address, sizeof(none_call_pair));
                    memcpy(&machine_call_pair, &machine_address, sizeof(machine_call_pair));
                    MachineTestA64Pair2 pair_probe = {left, right};
                    all_equal &= none_call_pair(1, 2, 3, 4, 5, 6, 7, pair_probe) == machine_call_pair(1, 2, 3, 4, 5, 6, 7, pair_probe);
                }
                else if (wide_result)
                {
                    all_equal &= none_call(left, right) == machine_call(left, right);
                }
                else
                {
                    all_equal &= (s32)none_call(left, right) == (s32)machine_call(left, right);
                }
            }
            BUSTER_TEST_RAW(arguments, all_equal, a64_supported_names[name_index]);
            codegen_release_executable(a64_machine_executable);
        }
        codegen_release_executable(a64_none_executable);
#endif
    }

    return result;
}
#endif
